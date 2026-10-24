# Handover: quark after 1.17.0

This document is for whoever continues quark after 1.17.0. That is the owner, working in a new Claude Code project (in the cloud or local), or any fresh session. It covers:
- how the project is laid out;
- how it is built, tested and released;
- what runs where;
- the rules the owner has set;
- what is still open.

Read [CLAUDE.md](../CLAUDE.md) first: it holds the standing rules, and any session working on quark loads it automatically.

---

## 0. Starting the next project (read this first)

The work up to 1.17.0 was done in one long claude.ai cloud session. **1.17.0 shipped on 2026-10-23.** Everything that matters is pushed to GitHub. The cloud container (its worktrees, agent branches, scratch files and test packages) is thrown away, so nothing below depends on it.

### Where things stand

- **Released:** v1.17.0, the latest production release, tagged on `b64ce4b` (CI run 120): signed Windows, Linux and macOS builds. The website (floration.github.io/quark, with `updates/1.17.0/`) was published by a site-only run from `77643ca` (run 122). The tag's own website job had been skipped: section 12.
- **Branches (restructure, 2026-10-23):** new work goes to `testing`, created from the default branch `claude/compassionate-ritchie-07ah6i` at `e7dad3c`. The default branch is protected (ruleset: PRs only, merge commits only, the `from-testing` check, no force pushes or deletions) and `v*` tags are restricted to the owner. The launcher, the website and the Discord bot have their own branches (`launcher`, `site`, `discord-bot`; section 4).
- **Deployed by the owner, and checked from here afterwards:**
  - **The ranking server:** the 1.17 SQL ran on D1, the 1.17.0 `worker.js` is live (`/announce` answers "POST only", `GET /disputes` is refused), and the cron and `ADMIN_KEY` are set.
  - **The game server** `129.151.8.15:27500`: on 1.17.0, listed by the Worker, auto-updating. Its testing server on the next port waits until a testing build is newer than production.
  - **The Discord bot:** re-installed with the auto-updater, with new keys in `/etc/quark-discord.env`.
- **Not switched on yet:** automatic Worker deploys (the `deploy-worker` job). It needs an API token and three values in GitHub ([OPERATIONS.md](OPERATIONS.md) section 3). Until then, the Worker is updated by hand for each version that changes it.
- **Unmerged work:** none.
- **Quick dash (answered, 1.18.2):** the owner chose a shorter cooldown: 2 charges and a 0.4 s cooldown, no third charge. 1.18.7: a third of the new dash timings, 0.2 s apart and a 0.4 s recharge.
- **Dash recharge (owner, 1.18.7):** "dashes take 1.2 seconds to recharge, but you can use them 0.6 seconds apart, so when you jump in the air with 2 dashes, you can use both of them within 1 second, without recharging any charges", with "landing refills, 1.2 s wait": any dash waits 0.6 s from the end of the previous one, and a spent charge comes back by the usual refills (the floor, a bhop landing, the dash techs, a syringe) only 1.2 s after the dash that spent it ended. Owner: "exempt the dash jump": a dash jump gives back the charge its own dash spent at once (both kept from full), other charges still wait. Protocol 43 (the own movement state carries each recharge, and the speed a dash started from).
- **Dash tech speeds (owner, 1.18.7):** "the wall bounce dash, super, and hyper should all have their horizontal speed changed from 24/24/32 into 16/24/+5 of current". In the code: wall bounce = the dash wall bounce (now 16 m/s, x the custom DASH SPEED), super = the dash jump (still the burst, 24), hyper = the super dash aimed down (now the horizontal speed you had when that dash started + 5, at least what the dash leaves with on its own (11 from a standstill), at most 36; custom SUPER DASH BOOST now scales the +5).

### Setting up the project

**A Claude Code project in the cloud** (claude.ai/code), which is how the owner works today, with nothing installed locally:
1. Create the project or environment on the repository `floration/quark`. Every session in the repository loads [CLAUDE.md](../CLAUDE.md) automatically, and CLAUDE.md points here.
2. Give the environment a setup script, so each session can build, test, take screenshots and make test packages:
   ```
   sudo apt-get update && sudo apt-get install -y cmake g++ python3 zip xvfb \
     libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev mingw-w64
   ```
   Node 22 or newer is needed for the Worker's tests (`node tools/master/test.mjs`). Cloud images usually have it already.
3. **What a cloud session cannot do:**
   - It can push only to its own `claude/...` branch: no tags, no other branches. So a cloud session's work reaches `testing` through a pull request from its branch, which the owner merges. It opens that PR only once the owner says to push.
   - **Production tags are the owner's.** With no local clone, use a GitHub Codespace (repository → **Code** → **Codespaces** → create one, then use its terminal):
     ```
     git fetch origin && git tag v<VERSION> origin/<default branch> && git push origin v<VERSION>
     ```
     Delete the Codespace afterwards.
   - **If a release's website deploy fails or is skipped:** Actions → **build-and-release** → **Run workflow** on the default branch, with **site** ticked. It publishes only the website, for the latest release.
4. **Test packages before a tag:** `tools/make_test_packages.sh` builds unsigned Windows and Linux packages of the working tree into `build/test-packages/`. The session sends them to the owner, who tries them before tagging.

**A local Claude Code project** (later, on the owner's machine):
1. Clone: `git clone https://github.com/floration/quark && cd quark`, then `git config core.hooksPath tools/githooks` (the secrets check runs before every commit). Opening Claude Code in that folder loads CLAUDE.md.
2. Install the build prerequisites (README "Build"):
   - **Linux:** the packages in the setup script above.
   - **Windows:** MinGW-w64 (gcc), cmake and python3. MSVC isn't supported.
   - **macOS:** the Xcode command line tools, cmake and python3.
3. Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`. The maps are generated during the build.
   On Windows, `tools\build_windows.ps1 -Setup` does steps 1-3 and stores each build on the machine ([LOCAL_BUILD.md](LOCAL_BUILD.md)).
4. A local session pushes with the owner's own git credentials, so it can push `testing` and tags directly.
5. A heavy test run competes with the game and other work for CPU, and the timing groups (netcode, tick rate) fail under load (section 3).

**Either way:**
- Before every push, run `tools/release_check.sh` (section 3) and get exit 0. From 1.17.1 on, the bot and map groups count again.
- **Republishing the studio page:** the page host serves `.gltf` and `.qmap` files only with an explicit type (`application/json` and `text/plain`), and one publish takes at most 255 files. Compare the new `manifest.json` with the published one and send only what changed.

### How the owner works

These preferences come from the 1.17.0 session. They are not all in CLAUDE.md:
- **Commit locally, push only when told** (CLAUDE.md). "Push" covers every local commit at once.
- **During long work:**
  - give a status report about every 10 minutes;
  - end each status report with an ETA for each part and a time until push;
  - give every estimate as one exact number, never a range;
  - when a task finishes, say so in one line, plus whether it is ahead of, on or behind schedule.
- **Short answers.** When the owner asks for a command, give the command first.
- **Test packages before tagging.** The owner tries the build by hand first ("build is perfect lets go").
- **Bot and map tests were waived for 1.17.0 only.** They count again from 1.17.1, locally (CI runs no gameplay tests: section 4).

### First things to do

1. **The restructure** (sections 4 and 9): done on 2026-10-23, except one step on the bot's box: move the bot to
   its branch before 1.17.1 ships to production ([OPERATIONS.md](OPERATIONS.md) section 2).

   Optional: rename the default branch from `claude/compassionate-ritchie-07ah6i` to `main` in GitHub settings. Check `tools/server/autoupdate.sh`, the bot's `autoupdate.sh` and `release.yml` first: they look the default branch up rather than naming it, but test one update after the rename.
2. **The quick-dash question** above.
3. **Performance check on real hardware** (REMAINING.md 0.3): the F3 1% and 0.1% lows against 1.16 on Midtown, Hightown and Lodestone. Include the cost of drawing stuck knives, bolts and syringes: 0.1 ms in the draw function, and 3–5 fps lost under software GL in the worst case.
4. **Automatic Worker deploys** ([OPERATIONS.md](OPERATIONS.md) section 3), whenever convenient.


### Notes from the 1.17.0 session (things learned the hard way)

**Releases and CI**
- **1.17.0 leftovers in `release.yml`:** the `base != '1.17.0'` / `base == '1.17.0'` conditions were removed with the CI test jobs (2026-10-24).
- **A tag made with the workflow token doesn't start a run.** That's why tags come from the owner (a Codespace or a local clone), and why the site-only run exists.
- **The release job refuses a release that already exists.** If a production run fails after publishing, use the site-only run for the website. If it failed before publishing, fix it, delete the tag (`git push origin :refs/tags/v<V>`) and tag again.
- **A job that `needs` a job which can be skipped** must use `if: "!cancelled() && ..."` with explicit `needs.X.result` checks. Without them, GitHub's implicit `success()` skips it whenever anything further back was skipped (this hit the 1.17.0 site deploy).
- **Expect about 3.5 minutes** for a build-only CI run (all three platforms).
- **The first testing prerelease will be the first real run of** the testing server's standby on the owner's box. Check `systemctl status quark-testing` once it starts.

**Cloud-session quirks**
- **The cloud session's permission check blocked, even with the owner's approval in chat:**
  - pushing a tag;
  - writing a workflow that makes tags;
  - at first, editing `release.yml` (flagged as a CI bypass). It went through once the owner explicitly chose that option.

  So ask the owner before touching release machinery, and say exactly what you'll change.
- **The git proxy answers HTTP 403** for any ref except the session's own branch. Don't retry; report it.
- **`pkill -f <pattern>` kills your own shell** when the pattern is also in the command line (exit 144). Kill by PID instead.
- **The GitHub tools can disconnect for a few minutes;** they come back.

**Tests**
- **`tools/release_check.sh`:**
  - Xvfb needs a moment before the first map; the script waits for its socket.
  - Lodestone and Midtown need up to 300 s for their first screenshot under software GL, because of the light bake.
- **Always run tests with a private `TMPDIR`.** Group logs and data dirs otherwise collide with other runs, and a port held by another run fails `Server::start` silently.

**Small cleanups noticed, not done**
- **A gcc warning** at `src/client/community.cpp:119`: `addChat({...})` leaves `Feed::spans` out of the initializer. Harmless; add `{}`.
- **The clang-only unused warnings** are listed in REMAINING.md section 2.

**Assets**
- **SUPER, HYPER and LOCATOR are synthesized in `audio.cpp`**, like every default sound. The owner may replace them with the sound editor (tools/sound_editor), which lists all 33 sounds of each kit.


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
- the owner's local editors (model, sound, map, ...), where the owner replaces AI-made assets (the Asset Studio page was retired 2026-10-24).

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
| `tools/studio/` | `build.py`: the asset export's file builder for the website /assets/ page (the Asset Studio page was retired 2026-10-24) |
| `tools/modes/`, `tools/mode_editor/` | The 13 built-in modes' logic (`*.qlogic`, `GRAMMAR.md`; embedded at build time) and the gamemode editor (local page on 127.0.0.1:8771, README there) |
| `tools/launcher_build.sh` | Builds the shipped launcher from the `launcher` branch (release jobs) |
| `site/` (not in `testing`) | The website: the `site` branch, checked out there as a worktree (README) |
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

**Before every push:** `tools/release_check.sh` runs every test group (4 at a time, then the timing-sensitive ones alone) and hosts every map headless in FFA with a screenshot. It exits 0 when everything passed. `--skip "bots maps"` leaves groups out, as for 1.17.0 only. Run the UI test (`build/quark --uitest`, [UI_TEST.md](UI_TEST.md)) too. CI runs neither (owner, 2026-10-24: CI checks meta, builds and sanitizers only), so these local runs are the only automated gameplay and UI check; the commit meant for production needs them passed on it ([RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) section 0).

**Test packages for the owner:** `tools/make_test_packages.sh [out_dir]` builds unsigned Windows (MinGW cross-compile) and Linux packages of the working tree, `quark-<VERSION>-windows-test.zip` and `quark-<VERSION>-linux-test.tar.gz`, into `build/test-packages/`. The owner tries them before tagging.

**Timing-sensitive tests:** the netcode, tick-rate and interpolation-delay groups fail on a loaded machine. Run the full suite on an idle machine with a private `TMPDIR` before calling a failure real. If one netcode timing check fails only on Windows (`tools/build_windows.ps1 -Tests`), the owner's decision (C10) is to give only that check a Windows-specific tolerance.

## 4. Branches and releases

A cloud session can push only its own `claude/...` branch, so its work reaches `testing` by a pull request the owner merges (section 0).

**Branches** (the restructure the owner asked for, done on 2026-10-23):

| Branch | What it is |
|---|---|
| `testing` | Where all new work lands. Every push publishes a signed prerelease `test-<VERSION>-test.<N>`. Dependabot targets it. |
| Default branch (protected) | Updated only by a PR from `testing`. Production releases come only from a `v<VERSION>` tag on it. |
| `launcher` | The launcher that ships: a full copy of the tree, branched at 1.17.0. It should almost never change. Every platform job builds `quark_launcher` from it (`tools/launcher_build.sh`, without the testing suffix) and packages that one, so a release only changes the launcher when this branch does. A launcher change goes here (by PR) **with a higher `VERSION`**, or installed launchers won't take it (`selfupdate::isNewer`). `src/launcher/` stays in `testing` too: the game links its launcher-upgrade code, and the tests cover it. Keep the two compatible (release manifest, signatures, qpak format). |
| `site` | The website source (its files at the top level). The linux job and the pages job check it out into `site/`; production releases build the site from it, plus the changelogs and weapon tables, into `gh-pages`. A change here goes live with the next production release or a site-only run. `master.txt` and its signature are committed here. |
| `discord-bot` | The bot (its files at the top level, split out with `git subtree split`). The bot's box follows this branch's head ([OPERATIONS.md](OPERATIONS.md) section 2). Its secrets live only on the bot's server. |
| `gh-pages` | The published website. Written by CI only. |
| `map-tests` | Later: maps uploaded by the owner's dev map editor (section 9). |

**Release flow:**
1. Work is committed on `testing`, with `VERSION` bumped and `changelogs/<VERSION>.md` written.
2. The push to `testing` runs CI: meta checks and the Windows, Linux and macOS builds (no gameplay or UI tests: those run locally before the push). It publishes the signed testing prerelease (Windows + Linux; macOS files follow). The build meant for production is marked final (`[final]` in its head commit's subject line or the merged PR's title, or a manual run with **final** ticked), which runs the sanitizers on it; nothing waits for them, but the checklist wants them green. Nothing else runs the sanitizers (work branches included; only a human adds the final mark, never an agent); production tags never do.
3. **The owner tests that build by hand with [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).** Nothing goes further until the owner says it passed.
4. A PR from `testing` to the default branch; the owner merges it.
5. The owner tags it: `git tag v<VERSION> <merge commit> && git push origin v<VERSION>`. CI publishes the production release only when Windows, Linux and macOS all build; then it deploys the website. Dedicated servers update to it by themselves.

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
| Ranking server | Cloudflare Worker + D1 database. Address in `site/master.txt`. | Automatic once switched on ([OPERATIONS.md](OPERATIONS.md) section 3): after each production release, the `deploy-worker` job applies new D1 migrations (`tools/master/migrations/`) and deploys `worker.js`. Until then, by hand: run the SQL listed in `tools/master/README.md` for the version in the D1 console, then paste `worker.js` and deploy. Cron trigger `*/15 * * * *`. Secrets: `ADMIN_KEY` and `BOT_KEY`. |
| Dedicated servers | Linux boxes (the Oracle one), installed with `tools/server/install.sh`: each runs a **production** server (`quark.service`, your port) and a **testing** server (`quark-testing.service`, the next port, "[TESTING]", ranked never rated; `--no-testing` to leave it out) | Automatic, each to the latest of its channel: production to production releases (every minute), testing to the newest testing prerelease (every 2 minutes), and the testing server runs only while that is newer than production (owner: off when both are on the same update). `autoupdate.sh` verifies the signed manifest, builds that exact commit (one build at a time), and restarts between matches. A load governor (`/var/lib/quark-governor`, `src/game/governor.h`) shares the box: throttles the empty server, then the one not in a ranked match, then the other, to 125 Hz; then closes testing until production is calm. Production never closes. Logs: `journalctl -u quark-update -u quark-testing-update`; failures at crit priority (`journalctl -p crit -t quark-update`, naming the instance), optionally sent to `QUARK_ALERT_WEBHOOK`. Behind a different forwarded port or in a container, run with `--own-addr host:port`. |
| Discord bot | The bot's Linux box, installed with `install.sh` | Automatic after re-running `install.sh` once ([OPERATIONS.md](OPERATIONS.md) section 2): every 10 minutes it moves to the newest production release's bot, after its tests and a dry run, and rolls back on failure. Secrets in `/etc/quark-discord.env`. After the move to its own branch or repository, it follows that branch instead. |
| Website | GitHub Pages (`gh-pages`) | Automatic, on each production release |

**The owner's 1.17.0 deployment steps are all done** (2026-10-23): the 1.17 SQL and the Worker, the cron and `ADMIN_KEY`, `install.sh` on the server box (game server and bot), and new bot keys. Production 1.17.0 shipped before 50 testing prereleases piled up, so launchers 1.16 and older found it.

**Where the keys are, for changing them by hand:**
- the bot: `sudo nano /etc/quark-discord.env` (`DISCORD_TOKEN`, `GUILD_ID`, `BOT_KEY`), then `sudo systemctl restart quark-discord`. A new `BOT_KEY` goes in the Worker's secrets too.
- the Worker: Cloudflare → the Worker → Settings → Variables and Secrets (`ADMIN_KEY`, `BOT_KEY`).
- the server box: `/etc/quark/update.env` (`QUARK_ALERT_WEBHOOK`, `GITHUB_TOKEN`).

## 7. Rules the owner has set (all in CLAUDE.md)

- **Performance and latency first.** Judge frame rate by 1% and 0.1% lows. Anything that can be made faster or lower-latency should be. For example, input is sent every tick, not batched.
- **Weapons and movement** in `gamedefs.h` / `pmove.cpp` change only when the owner specifies them.
- **Maps:**
  - each map is built for one mode and offered for that mode plus FFA; Lodestone is offered in every mode except team vs, duel, 1v1 and tourney (too big for them);
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

- `kProtocolVersion` (gamedefs.h) is 42 for 1.17.0 (37 to 41 in its earlier development builds). Any wire change bumps it, and old and new can't play together.
- `kOldestDemoProtocol` and the demo converter (`src/game/demo.cpp`, `kRecordFormatSince = 24`) decide which demos play directly and which are converted. A snapshot layout change needs a converter step.
- Snapshot projectile bits: 1 new, 2 velocity, 4 lob, 8 stuck, 16 out of sight, 32 on a player (u8 player, then its offset from their feet: i8 x, u8 y, i8 z; protocol 40).
- The player's own movement state's opt bits: 1 hooked, 2 dash, 4 dash charges + cooldown (a u8 + a u16 in ms, sent when the charges aren't 2 or the cooldown runs; protocol 42; 41 had the u8 alone, older the cooldown f32), 8 hook cooldown, 16 hook target, 32 pull point, 64 jump buffer + grace, 128 dash bounces.
- Name colour rides in EV_PLAYERINFO's i16 `c` (RGB565, 0 = default): no wire change.
- Maps are identified by the sha256 of their content: the ranked allowlist, light caches (`cache/<sha256>.qlm`), downloads (`maps/downloads/<sha256>.qmap`) and edit-session chains.
- Packages ship the official maps' light bakes in `maps/lightcache/<sha256>.qlbz`: the cache file's bytes zlib'd (84 MB raw, ~12 MB shipped; not `.qlm` also because installed launchers keep every `.qlm` through updates). release.yml's `lightcache` job makes them once (`quark --bake-maps <dir>`, no window, writes raw `.qlb`; `tools/pack_lightcache.py` compresses); the game inflates one (Midtown ~20 ms) when its own cache misses and never writes them.
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
| [OPERATIONS.md](OPERATIONS.md) | Automatic updates for the dedicated servers, the Discord bot and the Cloudflare Worker; every secret and how to keep it secret |
| [ASSETS.md](ASSETS.md) | The asset export, overrides |
| [MAP_STRUCTURES.md](MAP_STRUCTURES.md), [MAP_METRICS.md](MAP_METRICS.md), [MAP_REPORT.md](MAP_REPORT.md) | Why each map is built the way it is, how maps are measured, the latest scores |
| `tools/master/README.md`, the `discord-bot` branch's README | Deploying the ranking server and the bot |
| [AUDIT.md](AUDIT.md), [RELEASE_PLAN.md](RELEASE_PLAN.md) | History from the road to 1.0.0 |

## 11. Open items at handover

See [REMAINING.md](../REMAINING.md) for the full list, and section 0 "First things to do". The final state of 1.17.0 (tests, maps, what's known and kept) is in section 12 below.

## 12. State at 1.17.0 (shipped)

**Released on 2026-10-23 as 1.17.0 (production).** The owner pushed tag `v1.17.0` on `b64ce4b` after trying unsigned test packages of that commit ("build is perfect lets go"). There was no hand checklist and no further look-over, by the owner's call ("push 1.17.0 once everything is finished"). Release run 120 built Windows, Linux and macOS, signed everything and published the release as latest.
- **The website job was skipped in that run.** `pages` and `deploy-worker` had no status function, so GitHub's implicit `success()` saw the skipped CI test jobs further back and skipped them too. `77643ca` fixed it with `!cancelled()` and explicit results, as the release job already had. It also added the site-only manual run, which published the site (run 122).
- **The owner's play-test before the tag** added these to 1.17.0 (all in both changelogs):
  - the knife slash comes out of the knife model, with no box at its end;
  - holding jump refills only one dash charge (landing without jump held refills both);
  - the editor grid goes down to 1/8 m;
  - SUPER, HYPER and LOCATOR sounds (33 per kit);
  - A/D strafing beats W-only air control from 9.5 m/s, done by weakening W, not by boosting A/D;
  - knives, bolts and syringes stay stuck: in the world until the match ends, in a player until they die. Each is one reliable event, kept by the client (cap 256) and drawn without shadows.
- **The first push failed on macOS**, because Apple's clang rejected a `sigaction` call in the tests; the fix is `7552461`.

- **Tests before the push** (this machine, Linux): every `quark_tests` group except the bot and map groups passed. That includes the timing-sensitive netcode, tick rate and governor groups, each run on its own. The owner waived the bot and map tests for this release only ("those will be replaced anyway"). Every map was instead hosted headless in FFA and screenshotted: all 21 load and run. Lodestone and Midtown need over a minute on a loaded machine with software GL before their first light bake finishes. The movement agent's last full run, which included bots and maps, passed everything but the timing groups under load.
- **CI for 1.17.0 builds only, with no CI tests** (owner). The test jobs skip while VERSION is 1.17.0. From 1.17.1 they ran one test group per runner on Linux and Windows, and the release waited for them, until the owner removed CI's gameplay and UI tests on 2026-10-24 (section 4).
- **Decided in the owner's pre-release interview:**
  - The 1.2 s dash cooldown is back on top of the 2 charges, timed from each dash's end (protocol 42). That balances what charges alone allowed: a dash every 0.12 s on the ground, and a super dash on every bunny hop. (1.18.7 replaced it: 0.6 s between dashes plus a 1.2 s recharge per spent charge, protocol 43; see "Dash recharge" above.)
  - Kept as they are: the ~5.6 m hop over a ladder top (five maps got a stop at narrow tops) and the standing jump landing at 7.3 m/s (air drift 8).
  - Disputed ranked matches are logged and never rated; the testing server runs only while testing is ahead of production; the damage flash is off by default.
  - Quick dash: answered in 1.18.2, a 0.4 s cooldown and 2 charges instead of a third charge (1.18.7: 0.2 s apart, 0.4 s recharge).
- **The UI test** (`quark --uitest`) clicks every widget on every screen: 2,259 widgets on 109 screens, with an Esc check on each.
- **Map scores:** see MAP_REPORT.md (regenerated with the 1.17 movement; every map at least 7).

