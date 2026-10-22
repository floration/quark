# Fix handoff: working through docs/REVIEW.md

This file is the working plan for fixing what [REVIEW.md](REVIEW.md) found. It's written for a fresh Claude Code session (or a person) starting from commit `2ce3a44`, the commit the review was done on. Read [CLAUDE.md](../CLAUDE.md) first, then §2 ("Fix first") of REVIEW.md, then this file. REVIEW.md has the evidence for every ID used here; this file has the order, the approach and the rules.

Keep the **fix log** at the bottom up to date as you go: it's how the next session knows where you stopped.

---

## 1. Ground rules

These come from CLAUDE.md and from what the review learned.

1. **Commit locally, never push.**
   - Commit each fix when it's built and tested.
   - **Any push to any branch publishes a public release that every launcher auto-installs, and redeploys the website** (REVIEW SUP-2). That includes pushing a "PR branch".
   - Only push when the user says the owner wants it, and then push all local commits together.
2. **Before every commit:** build, run `quark_tests`, and check `git status` for stray files (see §2).
3. **No version bump per fix.**
   - `VERSION` + `changelogs/<VERSION>.md` are done once, at push time, as their own commit. History uses the subject `X.Y.Z: version and changelog`.
   - Keep the fix log below good enough to write that changelog from.
4. **One fix (or one tight group with the same root cause) per commit.**
   - Subject ≤ 72 characters, imperative, with the REVIEW id, e.g. `Reject snapshot player ids >= kMaxPlayers (REVIEW CLI-1)`.
   - Body: what was wrong, what changed, and how it's tested.
5. **Weapon and movement numbers are the owner's.** Don't change values in `src/game/gamedefs.h` or the movement constants in `src/game/pmove.cpp` (CLAUDE.md). Fixing *logic* bugs there is fine; changing *tuning* is not.
6. **Maps:**
   - Never hand-edit `maps/*.qmap`; edit `tools/make_maps.py` and re-run it.
   - **Never loosen a metric to make a map pass** (CLAUDE.md). If a corrected metric fails a map, fix the map, or stop and report to the user.
7. **Version constants:**
   - Wire format changes → bump `kProtocolVersion` ([gamedefs.h:15](../src/game/gamedefs.h#L15)).
   - Lightmap algorithm or cache layout changes → bump `kBakeVersion` ([lightmap.cpp](../src/client/lightmap.cpp)).
8. **Performance and latency come first** (CLAUDE.md). For anything on the frame or tick path, compare `--bench` 1% / 0.1% lows before and after, and note the numbers in the commit body.
9. **Every fix gets a regression test where it can be tested headless.**
   - Tests link only `quark_core`. Client-only logic (for example the `/filled` parser in `community.cpp`) can move into a small `quark_core` function so it can be tested; that's an acceptable refactor.
   - Use the repro inputs in `.review-kit/` (see [its README](../.review-kit/README.md)).
   - Copy the specific repro you need into `tests/` (e.g. `tests/data/`). Never commit `.review-kit/` itself.
10. **Owner decisions.** Items marked **Owner** below change game design, the release process, deployment or player data. Don't implement those on your own:
    - write the proposal in the fix log;
    - ask the user, who relays to the owner;
    - move on.
    Items marked **Bug** are clear defects (crashes, code that contradicts its own comments or intent, broken behaviour); fix them.
11. **If REVIEW.md is wrong** about something you touch, fix the code correctly and correct REVIEW.md in the same commit.
12. **Out of scope:**
    - deploying the Cloudflare Worker or the Discord bot (code changes are fine; the owner deploys);
    - creating signing keys;
    - anything on GitHub except read-only `gh` queries.

## 2. This machine (Windows 10) and how to build

The review found that the MSVC configuration in CMakeLists.txt doesn't build, and that the test suite can't pass on Windows until PERF-14 is fixed. Use MinGW-w64 GCC 13, which is the same compiler family as the release.

**Git Bash setup:**
```bash
export PATH="/c/msys64/mingw64/bin:/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
The full build takes about 15 s.

**Running the tests:**
- Run `build/quark_tests` from the repo root, as CLAUDE.md says. Start it in the background; a full run takes about 4-5 minutes once PERF-14 is fixed.
- The suite writes files as a side effect, including the tracked `docs/map_shots/*.cams` and `maps/temple.qmap` next to the exe (REVIEW ENG-5). After a run, check `git status`. Keep changes only if your fix legitimately changed maps; otherwise `git checkout -- docs/map_shots`.
- **Known flaky checks:** `tests.cpp:1814` and `:1818` (ranked vote → map switch → fixed 0.5 s pump) failed in 3 and 1 of 4 runs. If only those fail, re-run once. If they fail twice, or anything else fails, investigate.
- Ports 28520-29997 must be free.

**Other tools:**
- Screenshots: CLAUDE.md's Xvfb line is for Linux. On Windows run `build/quark --host ffa --map bastion --screenshot out.png` directly.
- Sanitizers: MSVC ASan + libFuzzer are installed (`.review-kit/build/msvc.bat` wraps `vcvars64`). The fuzz harnesses and their build script are in `.review-kit/fuzz/`. GCC trap-UBSan works with `-fsanitize=undefined -fsanitize-undefined-trap-on-error`.

## 3. Batch 0: set up (do this first)

1. Confirm the branch with the user. The checkout is on a local branch `main` at `2ce3a44`, the review commit, with no upstream. The project's default branch is `claude/compassionate-ritchie-07ah6i`. Work on the current branch unless the user says otherwise.
2. Commit `docs/REVIEW.md` and `docs/FIXES_HANDOFF.md` together, as the first commit (docs only, no tests needed).
3. Configure and build (§2). Note any warnings.
4. **Fix PERF-14 first.** It's the prerequisite for testing anything on Windows.
   - The bug: `sleepMs` in [net.cpp:191](../src/common/net.cpp#L191) uses `std::this_thread::sleep_for`, which sleeps about 15 ms per call on MinGW.
   - The fix: on Windows, use `Sleep((DWORD)ms)`. `netInit` already sets `timeBeginPeriod(1)`. See `.review-kit/build/sleepMs_windows.patch`.
   - Keep the non-Windows path unchanged.
   - This also fixes the client frame cap ([main.cpp:109](../src/client/main.cpp#L109)).
5. Run the full suite and record the baseline in the fix log. Expect a pass apart from the known flaky checks.

## 4. Batch 1: crashes, latency, broken lighting (all **Bug**)

Do these roughly in this order. Each line has the REVIEW id, then where, then the approach, then the test. The line numbers are at `2ce3a44`.

| # | ID | Where | Approach | Test |
|---|---|---|---|---|
| 1 | COR-1 | [community.cpp:20](../src/client/community.cpp#L20), [:107-111](../src/client/community.cpp#L107) | Split each `/filled` member into who / name-hex / discord-hex *from the right*, because `who` may be `d:<name>`; `quarkdata.py:479` already does this. Replace `fromHex`'s `std::stoi` with a non-throwing decoder that rejects bad input. | Parse a row containing a `d:<name>:<hex>:<hex>` member without throwing; garbage hex returns empty |
| 2 | CLI-1 | [protocol.cpp:154](../src/game/protocol.cpp#L154); [netclient.cpp:456](../src/game/netclient.cpp#L456); [game.cpp:801](../src/client/game.cpp#L801), [:884-888](../src/client/game.cpp#L884), [:936-937](../src/client/game.cpp#L936), [:1896-1898](../src/client/game.cpp#L1896), [:2130](../src/client/game.cpp#L2130), [:4157-4162](../src/client/game.cpp#L4157) | In `readSnapshotWorld`, `return false` for player id ≥ `kMaxPlayers`, duplicate player ids, and projectile owner ≥ `kMaxPlayers`. Check `clientId`, `selfWeapon` and the mode byte (< `MODE_COUNT`) at decode. Add a bounds guard in `interpolatedPlayers`. In game.cpp, validate `EV_KILL` / `EV_BRACKET` ids, rounds and matches (including negatives) before indexing. | Encode a snapshot with id 33 → decode returns false. Play `.review-kit/fuzz/repro/demo_oob_id33.body.qdemo` through `NetClient` without a crash. |
| 3 | CLI-4 | [netclient.cpp:664-666](../src/game/netclient.cpp#L664), [:690](../src/game/netclient.cpp#L690) | `size_t` arithmetic; require `n <= size - p - 13` before advancing; cap the demo file size | `.review-kit/client/hang.qdemo` → `startPlayback` returns an error quickly |
| 4 | CLI-5 | [netclient.cpp:350](../src/game/netclient.cpp#L350), [:650-677](../src/game/netclient.cpp#L650) | Start the replay at `max(ack+1, nextSeq_ - ringSize)`. Reset `nextSeq_`, `cmds_`, `predicted_` and `predictedSeq_` in `connect`, `startPlayback` and `disconnect`. | A snapshot with an ack far behind costs ≤ ringSize iterations |
| 5 | CLI-2 | [netclient.cpp:121-133](../src/game/netclient.cpp#L121), [:694-706](../src/game/netclient.cpp#L694); `Map::load` in [map.cpp](../src/game/map.cpp) | One `validMapName()` (`[A-Za-z0-9_-]{1,40}`, not a Windows device name: see the CLI-2 log row) used at `PKT_ACCEPT` (reject the connection with a clear error), in the demo map record and in `Map::load`. Keep the existing `useMap` check. | `../x`, `a/b`, `C:x` and empty names are rejected on every path |
| 6 | CLI-3, NET-3, NET-15 | [map.cpp](../src/game/map.cpp): `parse` (127+), `applyMapEdit` (330-480), `buildGrid` (530-553); [server.cpp:1880-1906](../src/game/server.cpp#L1880) | **One validator shared by parse, edit and download:**<br>• finite coordinates within ±4096, checked *after* group expansion (`part` fractions × the group box);<br>• caps on total brushes, including generated ones (keep ≤ 65,535, or switch `cellBrushes` to `uint32_t`), lights, ladders and groups;<br>• the `(int)std::ceil` grid math only after validation.<br>**For edits:** re-parse the serialized result before accepting, broadcasting or autosaving; count only editable brushes in the minimum-size check; clear `parts` when a group's type changes; keep a `.bak` on autosave.<br>Catch exceptions at the map load boundaries (download, demo, `Map::load`). | Every `.review-kit/fuzz/repro/map_*.qmap` is rejected with an error, not a crash. Every `edit_*.txt` sequence is refused or leaves a map that re-parses. The group-part bypass from REVIEW NET-3 is refused. |
| 7 | CLI-6 | [customrules.cpp:38-39](../src/game/customrules.cpp#L38), [:78](../src/game/customrules.cpp#L78); [config.cpp:40-42](../src/client/config.cpp#L40), [:79](../src/client/config.cpp#L79); [game.cpp:2131](../src/client/game.cpp#L2131) | `!std::isfinite(v)` → default, in the rules clamp and in the `weapon=` path. Truncate before trimming in `safeName`. Clamp and check `shadowres`, sensitivity and FOV. Restrict `mode_hud_*` values to safe filename characters. | `rules_nan*.txt` and `rules_spaces_name.txt` → finite values, and the `toText` / `fromText` round-trip holds |
| 8 | MAP-1 | [map.cpp:678-688](../src/game/map.cpp#L678) | `raycastAny` skips material 9 (clip), like `raycast` / `raycastBullet` do. Bump `kBakeVersion`. This also fixes server visibility and bot line of sight through clip "glass". | A ray through a clip wall in a small test map: `raycastAny` = no hit; player collision with clip is unchanged |
| 9 | PERF-4 | [game.cpp:3835-3860](../src/client/game.cpp#L3835) | `low_latency=1`: in `beforeInput`, wait on the fence from two frames back, i.e. the slot `afterSwap` is about to overwrite, not the one it just wrote | `--bench` on Bastion: setting 1 should land between "off" (≈4.3k fps) and "ultra" (≈2.8k) on this machine; record the numbers |
| 10 | PERF-1 | [server.cpp:18-20](../src/game/server.cpp#L18), [:442](../src/game/server.cpp#L442), [:956](../src/game/server.cpp#L956), [:984-995](../src/game/server.cpp#L984) | When a client's command queue stays deeper than ~2 for N consecutive ticks (e.g. 32), allow one extra command per tick until it's shallow again. That's a bounded catch-up, which keeps the anti-speedhack token bucket meaningful. Optionally expose the queue depth on F3. | A headless test: inject a 12-command burst, then steady input; the queue returns to ≤ 2 within a bounded number of ticks; the speedhack test still passes |
| 11 | PERF-7 | [renderer.cpp:546-557](../src/client/renderer.cpp#L546) | Assign `viewProj_` *before* extracting the frustum planes. Better: share one extraction helper with `drawWorldChunks` (~:966). | Visual check: no one-frame pop-in on fast turns |
| 12 | PERF-6 | [netclient.h:152-156](../src/game/netclient.h#L152) | Prediction rings: 256 → 1024 (≥ 1 s at 1000 Hz). Check the memory and the replay cost. | The existing prediction tests, plus a 400 ms-latency loopback session without rubber-banding |
| 13 | COR-3 | [config.cpp:107](../src/client/config.cpp#L107) | Clamp `bot_mode` to `MODE_COUNT-1` | Save and reload each mode |
| 14 | COR-2 | [config.cpp:134-137](../src/client/config.cpp#L134), [profiles.cpp:37-42](../src/client/profiles.cpp#L37), the `hud.cfg` save, [ranked.cpp:47-51](../src/game/ranked.cpp#L47), the custom-mode saves | Write to `*.tmp`, then rename, reusing `saveMapFile`'s pattern ([map.cpp:76](../src/game/map.cpp#L76)). `quark.key` gets mode 0600 on POSIX. | Unit test for the helper |
| 15 | NET-7, NET-6 | [server.cpp:489-495](../src/game/server.cpp#L489), [:593-627](../src/game/server.cpp#L593) | Chat cooldown plus a token bucket, and the name filter applied to chat text. A vote cooldown per player. Check map names against a cached `listMaps()` instead of parsing files on the tick. A per-tick budget for string commands. | Chat flood → limited; `callvote tmaps` with many names doesn't load files |
| 16 | BOT-1, BOT-2 | [tools/discordbot/bot.py:264](../tools/discordbot/bot.py#L264), [:383-397](../tools/discordbot/bot.py#L383), [:303](../tools/discordbot/bot.py#L303), [:444](../tools/discordbot/bot.py#L444) | `commands.Bot(..., allowed_mentions=discord.AllowedMentions.none())`, opting in per message where a role ping is intended. `discord.utils.escape_markdown` on server and player names, strip URLs and backticks. Note for the owner: drop Mention Everyone from the invite and redeploy the bot. | `python -I tools/discordbot/test_quarkdata.py`, plus a unit test for the escaping helper |

## 5. Batch 2: game-mode rule bugs

| # | ID | Kind | Where | Approach |
|---|---|---|---|---|
| 1 | GAME-3 | **Bug** (contradicts its own comment) | [gamemodes.cpp:764](../src/game/gamemodes.cpp#L764) | Overtime while *attackers* are on the point (`na > 0`), not only while it's contested |
| 2 | GAME-1 | **Bug** (exploit) | [gamemodes.cpp:164-176](../src/game/gamemodes.cpp#L164), [server.cpp:510-540](../src/game/server.cpp#L510) | In tag, "it" leaving or switching to spectator counts as being eliminated (`out`). No team changes while a tag round is live. `out` survives a reconnect, keyed by rank id, falling back to name. |
| 3 | GAME-5 | **Bug** | [gamemodes.cpp:953](../src/game/gamemodes.cpp#L953) | A dropped bomb goes to the floor below (raycast down), or to `lastGround`, never in mid-air. Add a bomb-reset timer if it's out of reach. |
| 4 | GAME-10 | **Bug** | [server.cpp:1576-1594](../src/game/server.cpp#L1576) | Don't remove an alive balance bot mid-round; defer to the round end |
| 5 | MAP-7 | **Bug** | [gamemodes.cpp:997](../src/game/gamemodes.cpp#L997) | Use `canSupportMode` for the spawn-type choice so Colosseum's a/b spawns and race spawns are used |
| 6 | GAME-11 | **Bug** (contradicts [gamedefs.h:5-7](../src/game/gamedefs.h#L5)) | [server.cpp:166-178](../src/game/server.cpp#L166) | Change the tick rate only in warmup or between rounds |
| 7 | GAME-12 | **Bug** | `rand()` users in server.cpp and gamemodes.cpp | Seed and use the existing `Server::rng_` (or a per-instance `mt19937`) instead of unseeded `rand()` |
| 8 | GAME-2a | **Bug** | [server.cpp:1176-1177](../src/game/server.cpp#L1176), [:907](../src/game/server.cpp#L907), [:976-977](../src/game/server.cpp#L976) | Heals (syringe, vampire, regen) cap at *this life's* max health (duel modifiers), not `maxHealth()` |
| 9 | M6 (§5.5) | **Bug** (UI) | [game.cpp:2829-2880](../src/client/game.cpp#L2829) | The FFA scoreboard shows the value it sorts by (frags) |
| 10 | GAME-4 | **Owner** | [gamemodes.cpp:713](../src/game/gamemodes.cpp#L713), [:794-801](../src/game/gamemodes.cpp#L794) | Propose overtime while the cart is touched (payload / neutral payload) |
| 11 | GAME-2b | **Owner** | syringe | Propose: ban the syringe in round modes and duel; throws reset only dash and hook; the stalemate drain must out-pace any heal |
| 12 | GAME-7 | **Owner** | [server.cpp:1646-1648](../src/game/server.cpp#L1646) | Propose: absolute-majority votes; no map or ranked votes while live |
| 13 | GAME-6, GAME-8, GAME-9, §5.3, §5.4 | **Owner** (tuning) | gamedefs / pmove | Knockback cap, knife throw cooldown, size-modifier hull, movement model: write them up as proposals only |

For each **Bug** here, add a headless test that drives the mode through the exact situation: overtime with only attackers on the point, "it" switching to spectator, a bomb carrier killed mid-air, and so on.

## 6. Batch 3: trust boundaries and anti-cheat (protocol changes, so bump `kProtocolVersion`)

| ID | Kind | Approach |
|---|---|---|
| NET-1, NET-2, NET-4 | **Bug** | A stateless challenge handshake:<br>1. CONNECT → server replies `PKT_CHALLENGE` with a cookie = HMAC(server secret, ip, port, time / 10 s) and creates no state.<br>2. The client re-sends CONNECT with the cookie, and only then gets a slot.<br>3. No snapshots go to a slot until its first `PKT_INPUT`.<br>4. A nonce change on an existing address only takes effect after a valid cookie.<br>5. `PKT_INFO_REQUEST` must be padded to at least the reply size; rate-limit it per /24.<br>6. Rate-limit `PKT_MAP_REQUEST` per player.<br>Test: spoofed CONNECT without a cookie gets no slot; an address that never sent input gets no snapshots. |
| NET-5 | **Bug** | Cap the rewind at measured ping + interp + ~30 ms. One O(1) history lookup per shot (index by tick), interpolating between the two bracketing ticks. Apply it to the knife too. |
| NET-9 | **Bug** | Spread seed = server-side per-player shot counter mixed with a per-match salt sent in `PKT_ACCEPT`. The client mirrors both so tracers still match. Test: a client can't choose seeds. |
| NET-10 | **Bug** | Don't send health, weapon, `PS_FIRING` or `PS_HASBOMB` for hidden players. No 20 m earshot exemption in hide and seek. Send the seeker nothing about hiders during freeze. Visibility-cull projectiles. Dead players' chat goes to the dead and spectators only (VS, bomb). |
| NET-11 | **Bug** | On reliable-event overflow, disconnect with a reason (or full resync) instead of silently dropping |
| PERF-5, NET-13 | **Bug** | Game time as 64-bit integer ms on server and client: `Server::time()`, `renderTime_`, interpolation and timers. Floats only for local deltas. |
| NET-8 | **Owner** | Propose an rcon-style admin password instead of "127.x is admin" |
| NET-12, WEB-* | **Owner** | Needs Worker changes (batch 7) |

## 7. Batch 4: frame-time and tick-time performance (all **Bug**)

Measure before and after with `--bench`, and with `.review-kit/client/lm_timed.cpp` for the lightmap.

| ID | Approach | Acceptance |
|---|---|---|
| PERF-2 | Lightmap prepass: bucket faces by (axis, sign, plane, material) for `covered()` / `needLift`; use the map grid in `pointSolid`; check the `.qlm` *before* the prepass (store the edge/lift masks and atlas layout in it; bump `kBakeVersion`) | Midtown cached load < 1 s (today 13-14 s); cold bake ≤ a few seconds |
| PERF-3 | Bake on a worker thread from a private `Map` copy with `dynSolids` cleared. `frame()` keeps calling `net_.poll()` / `tick()` / `glfwPollEvents()` and draws the loading bar. Defer map swaps until the bake is done. | Joining Midtown on a dedicated server doesn't time out; the window never shows "Not Responding" |
| PERF-8 | Reuse per-client snapshot scratch buffers; store compact deltas; size the `sent` ring to the ack window | Server tick p99.9 improves on a 16-bot server |
| PERF-9 | Stagger the visibility raycasts across ticks and cap rays per pass | No tick over budget on Midtown with 32 players |
| PERF-10 | Build the `NavGraph` at map load, off the tick thread; stagger bot repaths; A* with reused buffers | No tick spike when bots first think |
| PERF-11 | Cache `rescanMaps` results by path/mtime/size; serialize maps lazily on `EV_EDIT`; async server-list refresh with a generation counter; a demo writer thread; swap announcer PCM with a `shared_ptr` | No main-thread hitch over 2 ms in those flows |
| PERF-12, PERF-13 | Cache `findDataPath` results; compute `interpolatedPlayers` once per frame; one `sendInput` after the tick loop; a static editor VBO; query `GL_MAX_TEXTURE_SIZE` for the atlas | CPU frame time on Bastion ≤ today's 0.13 ms |

## 8. Batch 5: maps and map validation

Follow rule 6: **never loosen a metric to pass a map.** Re-run `tools/make_maps.py`, then `quark_tests`, after each change. Expect `docs/MAP_REPORT.md` and `docs/map_shots/*.cams` to change legitimately here. Note that regenerating maps changes their sha256, and CMake regenerates the ranked allowlist from that automatically.

| ID | Kind | Approach |
|---|---|---|
| MAP-3 | **Bug** (the test is broken) | Collision-checked reachability (walk grid: 0.5 m cells, real `playerBox` / `boxSolid`, 0.55 m step-up, 1.15 m jumps, 2.5 m gaps, ladders). The reference implementation is in `.review-kit/maps/map_harness.cpp`. Unreachable counts as a failure and scores 0. Add goals for vs / duel / 1v1 / ffa / tourney. |
| MAP-2 | **Bug** | `make_maps.py` emits enough `any` spawns for every offered mode (≥ max players, ≥ 1.5 m apart), Anvil first, because it hosts the ranked 4-player instagib FFA. Test the pool `spawnPlayer` actually draws from ([server.cpp:769-771](../src/game/server.cpp#L769)). |
| MAP-5 | **Bug** | A cart-sweep test against static geometry. In the generator, a keep-clear registry that every placement checks, not just `dress` / `clutter`. Fix the Railyard box at track 102-106 m and the Lodestone pillar and dais at 117.5 m and 127.5 m. |
| MAP-4 | **Bug** | Fix the metrics listed in REVIEW MAP-4 **one at a time**: loops from a portal graph, vertical from the height range, per-team per-objective balance, unreachable = 0, worst-objective grading, no name-based exemptions. After each, report which maps now fail and stop for the user's call if a map needs real redesign. |
| MAP-10, MAP-11 | **Bug** | Real `stairs` and `setting` keywords (the engine already has `group stairs`). A `version N` header; unknown optional keys warn instead of failing; consistent errors for gameplay keys. Make `Map::parse` reset the whole object (COR-7). |
| MAP-16, MAP-17, MAP-18 | **Bug** | Spawn sanity checks: facing, spacing, enemy spawns in mutual line of sight. A full mirror check for ranked maps. Docs fixes; regenerate `MAP_REPORT.md`. |
| MAP-6, MAP-8, MAP-9, MAP-13, MAP-14 | **Owner** | Lodestone and Midtown balance, editor vs generator ownership, the storage layout, splitting the generator: proposals |

## 9. Batch 6: engineering

| ID | Kind | Approach |
|---|---|---|
| ENG-1 | **Bug** | The test suite passes on Windows: the sleep fix (batch 0); `setvbuf(..., _IONBF, 0)` on Windows ([tests.cpp:2376](../tests/tests.cpp#L2376), [:2458](../tests/tests.cpp#L2458)); `Server` heap-allocated in tests and [server/main.cpp:100](../src/server/main.cpp#L100); `__builtin_popcount` → `std::bitset<32>(x).count()`. Adding a Windows CI job is **Owner**: write it, don't push it. |
| ENG-2 | **Bug** | Exception boundaries: catch `std::exception` around map, demo and pak loading and around Worker-reply parsing; report and recover instead of terminating |
| ENG-3 | **Bug** | Bring the fuzz harnesses into `tests/fuzz/` behind a CMake option (`QUARK_FUZZ`, MSVC `/fsanitize=fuzzer` or clang), plus a small regression corpus in `tests/data/` |
| ENG-5 | **Bug** | Hermetic tests: write to a temp directory, use ephemeral ports, make `tests.cpp:1814` deterministic |
| ENG-4 | **Owner** | Fix or delete the MSVC CMake branch. A global `NOMINMAX` and a correct build-type default are safe either way. |
| COR-4, COR-5, COR-6, ENG-6 | Mixed | Editor undo on a copy (**Bug**); Unicode paths via `std::filesystem::path` end to end (**Bug**); rules out of process globals, passing a `const Rules&` into pmove and weapons (**Bug**, but a big refactor, so plan it first); build and repo hygiene (**Owner**) |

## 10. Batch 7: needs the owner

Prepare the code or a proposal; don't deploy.

| IDs | What you can do | What needs the owner |
|---|---|---|
| SUP-5, SUP-6, SUP-7 | Launcher code: reject rooted, drive, UNC, stream and reserved names; cap entry sizes; catch `u8path`; extract to `game.new/` then rename; join the thread instead of `detach()`; skip prereleases; paginate; no auto-downgrade; `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` at startup. Test with `.review-kit/fuzz/repro/pak_*`. | Shipping it (a launcher update) |
| SUP-3, SUP-4 | Workflow edits compatible with today's process: `permissions: contents: read` by default with `write` only in release and pages; `persist-credentials: false`; SHA-pinned actions; a `concurrency` group; replace the `eval()` in `site_changelog.py` | Approving the CI change |
| SUP-1, SUP-2 | Write the proposal: Ed25519-signed qpaks verified by the launcher; tag-only releases | Key management; changing the "every push releases" process (CLAUDE.md) |
| SUP-8, SUP-11 | `autoupdate.sh`: derive platforms from the release assets; signed tags; a dedicated user; systemd hardening. User data directory migration plan. | Server operators; a player-data migration |
| WEB-1 … WEB-13 | Worker code with local tests (`tools/master/test.mjs`; the mocks in `.review-kit/worker/`): match tickets binding the player set; reject reports from outsiders; dispute instead of void; per-IP list caps; never trust `?ip=`; POST-only writes; atomic rating writes and queue fills; indexes and caching; pruning | Deploying to Cloudflare; the ranked redesign (WEB-1/2/3 change how ranked works) |
| BOT-3, BOT-4, BOT-5 | Channel caps with per-server error handling; the install-script secret handling; least privilege | Redeploying the bot; Discord permission changes |

## 11. Definition of done (per commit)

- [ ] Builds with MinGW (§2) without new warnings.
- [ ] `build/quark_tests` passes, apart from the known flaky checks and only after one retry.
- [ ] A regression test for the bug (or a stated reason it can't be headless, plus a manual check).
- [ ] Hot-path changes: `--bench` numbers before and after in the commit body.
- [ ] `git status` is clean of stray test outputs.
- [ ] Protocol or bake version bumped if needed.
- [ ] Fix log updated (below), in the same commit.

## 12. Fix log

Work happens on the local branch `review-fixes` (made from `2ce3a44` at the user's request; it gets merged into `main`, the new production branch, later). Commit hashes below are filled in by the commit after each one, because a commit can't name its own hash.

| Date | REVIEW id | Commit | Result / notes |
|---|---|---|---|
| 2026-10-21 | (batch 0) | `75c2acb` | REVIEW.md and this file committed. MinGW GCC 13 build: OK in ~11 s, 3 warnings (the known baseline: `NOMINMAX` redefined in openurl.cpp, unused `serial` in tests.cpp, unused `dms` in game.cpp). |
| 2026-10-21 | PERF-14 | `fbaae61` | `sleepMs` uses `Sleep()` on Windows (non-Windows path unchanged). New `testSleepPrecision`: 20 × `sleepMs(1)` must average < 5 ms; measured 1.93 ms. **Baseline suite (Windows, serial): all tests passed, 4m12s**, no flaky failure on this run. The run rewrites every `maps/*.qmap` with LF line endings (`testResetMaps`, contents identical; ENG-5): `git checkout -- maps` after each run. |
| 2026-10-21 | COR-1 (+CLI-7) | `fb39fde` | The `/filled` parser moved into `quark_core` (`parseFilledRow` / `parseFilledMembers` / `fromHexText` in ranked.cpp) so it can be tested. Members split from the right (who may be `d:<name>`), the members field is the rest of the line (a Discord name with a space no longer drops the row's tail), and hex decoding never throws (bad hex → empty). Test `testFilledParse`. Suite: 1 FAIL, the known flaky ranked-vote check (`tests.cpp:1814`, now :1841), twice in a row, so investigated: `setRanked` parses (and sha256s) map files on the server tick until it finds the ladder's map (~140 ms real for instagib from Foundry; 385 ms to scan all 21), so the test's fixed 0.5 s pump (4× clock) ends before the switch. Pre-existing, not from this fix; the test is made to wait in the next commit (ENG-5), the tick stall is NET-6 (#15). |
| 2026-10-22 | ENG-5 (part) | `c957a2b` | The ranked-vote test (`tests.cpp:1814` at `2ce3a44`) waits up to 4 s for the map switch instead of a fixed 0.5 s: `setRanked` loads maps on the tick (NET-6, fixed later), and on this machine the switch landed at ~0.58 s of the 4× test clock. `quark_tests ranked` 3/3 clean; full suite (with CLI-1 on top): all tests passed, 252 s. The rest of ENG-5 (temp dirs, ports, deterministic time) is batch 6. |
| 2026-10-22 | CLI-1 | `a950b5c` | Decode rejects: player ids ≥ `kMaxPlayers` or repeated, player weapon ≥ `WPN_COUNT`, projectile owner ≥ `kMaxPlayers`, `clientId` ≥ `kMaxPlayers`, `selfWeapon` ≥ `WPN_COUNT`, mode ≥ `MODE_COUNT`. Guards also in `PKT_ACCEPT`, the demo map record, `interpolatedPlayers`, `EV_KILL` → spectate target, `applyModeRules`. `EV_BRACKET` goes through a new `parseBracket()` (protocol.cpp): ids outside [-2, 32) become -2, round/match clamped, ≤ 8 rows; `drawBracket` bounds its next-round lookup. Test `testSnapshotBounds`: encoder → decoder for every bad field, the review's `demo_oob_id33.qdemo` (now `tests/data/`, found via a `QUARK_TEST_DATA` define) plays without a crash and yields no snapshot, bracket clamping. Full suite: all passed (252 s). |
| 2026-10-22 | CLI-4 | `41cb533` | Demo record lengths checked as `n > size - pos - 13` in size_t (no `13 + n` wrap) in both the `startPlayback` length scan and `pollPlayback`; demos over 512 MB (~9 h) refused before reading. Test `testDemoLengthWrap` plays the review's `hang.qdemo` (now `tests/data/demo_len_wrap.qdemo`): refused in < 2 s. Full suite: all passed (253 s). |
| 2026-10-22 | CLI-5 | `2c95240` | Reconcile replays from `max(ack+1, nextSeq_ - kCmdRing)`; new `kCmdRing` constant replaces the `% 256` / `[256]` sites (so PERF-6 is a one-line change); `resetPrediction()` (seq, commands, predicted states, error offset) on connect, disconnect and so `startPlayback`. `lastReplayCount()` exposes the replay length. Test `testReplayBound` uses a new `FakeServer` (a scripted UDP server in tests.cpp: answers CONNECT, sends crafted ACCEPT / snapshots): 3000 commands, then an ack 2999 behind → 256 replayed (was 2998). Full suite: all passed (252 s). |
| 2026-10-22 | CLI-2 | (next) | `validMapName()` (map.cpp) at `PKT_ACCEPT` (disconnects with "server sent a bad map name" before any download), in the demo map record, in `Map::load`, `useMap`, the server's map / tmaps votes and autosave, and the editor's save. **REVIEW corrected:** the rule is `[A-Za-z0-9_-]{1,40}` minus Windows device names (con, prn, aux, nul, com1-9, lpt1-9), not `[a-z0-9_-]{1,32}`: the editor already saves mixed-case names up to 40, and `maps/con.qmap` opens the console device. Test `testMapNames`: bad names refused by `validMapName` and `Map::load`; a FakeServer accepting with `../../pwned` → disconnected, no map; a demo whose map record is `../../pwned` doesn't play. Full suite: all passed (252 s). |


**Owner decisions pending** (proposals waiting for the owner):

- (none yet)
