# quark: full adversarial review (v2)

| | |
|---|---|
| **Reviewed** | Local checkout `2ce3a44` (1.15.1), 2026-10-21/22. The remote is already ahead (`46b7560`, v1.16.0, macOS builds re-enabled), so recheck anything touched since. |
| **Method** | Six parallel reviewers each read their area in full: netcode/server, client (two passes), maps/content pipeline, backend/ops/supply chain, build/tests/fuzzing, gameplay design. They built the code out of tree with MSVC and MinGW and wrote harnesses outside the repo. They also ran the test suite on Windows, ran MSVC ASan + libFuzzer over 7 harnesses (about 11M executions), and built with trap-mode UBSan. I then re-traced the headline claims myself; those are marked ✓. |
| **Safety of the review** | No live service was attacked. Worker attacks ran against a local copy of `worker.js` on in-memory SQLite. GitHub was only read with `gh`. Nothing in the repo was changed except this file. |
| **Not in this document** | Crafted packets, files and requests that reproduce the findings are left out on purpose. The reviewers' harnesses still exist outside the repo; ask for them, ideally to turn them into regression tests. |

**How each finding was verified:**
- **R:** reproduced (harness, build, local mock, or an end-to-end run).
- **T:** traced through the code end to end.
- **P:** plausible, not fully demonstrated.
- **✓:** additionally re-checked by the lead reviewer.

**Severity:**
- **Critical:** breaks a system for everyone, or remote compromise, at low effort.
- **High:** remote crash/DoS, a game-breaking exploit, or a bug that breaks normal play.
- **Medium:** real, bounded impact.
- **Low:** hardening, hygiene, polish.

---

## 1. Executive summary

The first review said the engine core was careful and the edges were weak. That holds, but the deep pass found that **several things that look fine are broken in normal play**, not only under attack:

- **Midtown can't be joined.** The client spends 13-14 s on the main thread preparing the map, even with the lighting cache, so the server's 10 s timeout drops the player. This was verified end to end.
- **Baked sunlight is wrong on 18 of 21 maps.** Shadow rays don't skip the invisible clip walls, so most floors are in shadow. Dojo is 85% clip-shadowed and only 11% sunlit.
- **Queueing can crash every queued client.** When a queue fills with a Discord user in it, every game client that is queued or has the queue menu open crashes. Reproduced.
- **Players can be stuck with permanent extra input latency.** After any hitch the server's input queue holds 12 commands forever: +12 ms at 1000 Hz, +96 ms at 125 Hz. This is invisible on the F3 overlay.
- **The default "low latency" setting is mislabelled.** It behaves like "ultra" and costs about 35% of average fps for no latency benefit.
- **Any frame cap stutters on the shipping Windows build.** `sleepMs(1)` takes about 15 ms with MinGW's `sleep_for`, so a capped client runs at about 66 fps with 15 ms spikes.
- **Several mode rules are broken:**
  - Multi-point overtime is inverted.
  - Payload has no overtime.
  - Tag's "it" escapes elimination by switching to spectator for a moment.
  - The syringe out-heals the VS stalemate drain, so rounds never end.
  - The syringe undoes duel health modifiers.
- **The map tests prove less than they claim.** The bot nav graph ignores walls, many grading metrics are saturated or gameable, and FFA maps ship 2-spawn pools. That includes Anvil, which hosts the ranked 4-player instagib FFA.
- **The test suite can't pass on Windows**, the main player platform. With MinGW it times out with 26 failures (the same 15 ms sleep), and with MSVC it crashes at startup. Even with the sleep fixed, one ranked check fails 3 runs in 4.

On security, every major v1 finding was confirmed, several are worse than stated, and there are new ones:

- **Hostile server vs. client:** an unchecked player id writes past a stack array on every client; a map name becomes a file path, now also via shared demo files; a crafted demo hangs the game.
- **Map editing:** any player in a voted edit session can crash the server with validated edit ops.
- **Ranked:**
  - Anyone can farm rating to #1 with no game played: 160 requests in the local mock.
  - Anyone who knows a match id can void that match, and losers can void their own losses.
  - One IP can push every real server off the server list.
  - A modest request volume exhausts the Worker's free-plan quota and takes every online feature down until midnight UTC.
- **Discord:** the bot can be made to ping `@everyone` through a game server's name.
- **Supply chain:**
  - Updates are unsigned.
  - Published binaries have already failed to match their tags: v0.26.1 and v0.26.3 had their assets replaced by other commits' builds.
  - The CI write token sits in `.git/config` during every build job.
- **Fuzzing confirmed the client-side bugs independently.** ASan + libFuzzer reproduced the client stack overflow from a 370-byte demo, a 240 GB allocation from a 42-byte map, and path escapes in the update extractor. There are **zero `catch` blocks in `src/`**, so any exception thrown on untrusted input ends the process.

---

## 2. Fix first

These are ordered by players affected × effort. Most are small.

| # | ID | Fix | Effort |
|---|---|---|---|
| 1 | COR-1 | Queue-fill crash: split queue members from the right; use a non-throwing hex decode | 10 lines |
| 2 | MAP-1 | Make `raycastAny` skip clip (material 9); bump `kBakeVersion` | 2 lines |
| 3 | PERF-4 | `low_latency=1`: wait on the *older* fence | 1 line |
| 4 | PERF-14 | `sleepMs` on Windows: `Sleep()` or a high-resolution waitable timer instead of `std::this_thread::sleep_for` | 5 lines |
| 5 | PERF-1 | Drain the input backlog with an extra command per tick when the queue stays deep; show queue depth on F3 | ~10 lines |
| 6 | GAME-1/2/3/4 | Tag spectator escape, syringe heal and reset rules, multi-point and payload overtime | ~60 lines |
| 7 | CLI-1, CLI-4 | Range-check snapshot player ids, projectile owners, event ids and the mode byte at decode; fix the demo length wrap | ~20 lines |
| 8 | CLI-2 | One `validMapName()` at `PKT_ACCEPT`, in demo playback and in `Map::load`; key caches by content hash | ~20 lines |
| 9 | PERF-2/3 | Bucket faces by plane in the lightmap prepass; bake a private map copy off the main thread; keep pumping the network | 1-2 days |
| 10 | SUP-2/3 | Tag-only releases with a concurrency group and least-privilege, SHA-pinned Actions; `persist-credentials: false` | 1 hour |
| 11 | BOT-1 | `allowed_mentions=AllowedMentions.none()` as the bot default; escape names; drop Mention Everyone | 10 lines |
| 12 | NET-3, NET-15 | Validate edit geometry *after* group expansion; cap generated brushes; `uint32` cell indices; re-parse the result before accepting or autosaving an edit | ~40 lines |
| 13 | MAP-2 | Spawn pools sized for every offered mode (Anvil first); test the pool `spawnPlayer` actually draws from | generator + test |
| 14 | ENG-1 | Make the test suite pass on Windows (sleep fix, `setvbuf`, heap-allocated `Server`), and run it in CI | ~1 day |

---

## 3. Security

### 3.1 Game server and netcode

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| NET-1 | **Critical** | T ✓ | **The handshake has no return-routability check.** [server.cpp:295-337](../src/game/server.cpp#L295) creates a session from a spoofed `PKT_CONNECT` with an attacker-chosen nonce. `sendSnapshots` streams to every connected slot whether or not it ever sent input ([:1473-1528](../src/game/server.cpp#L1473)), at 500 snapshots/s by default. One tiny spoofed packet carrying the known nonce refreshes the timeout ([:270](../src/game/server.cpp#L270)), so a third party can be flooded indefinitely. `PKT_MAP_REQUEST` gives about 72× amplification per packet (17 B in, ~1.2 KB out, [:273-284](../src/game/server.cpp#L273)). Spoofed connects can also fill every slot. | Stateless challenge cookie (HMAC of address + time). No snapshots until the cookie is echoed and a first input arrives. Rate-limit map requests. |
| NET-2 | High | T | **Kick any player by spoofing their address.** A CONNECT from a connected address with a different nonce drops the existing player before any challenge ([:308-312](../src/game/server.cpp#L308)). | Only accept a session change after the challenge. |
| NET-3 | High | R | **Any editor can crash the server, and voted edits are open to everyone.** `applyMapEdit` limits coordinates to ±4096. But a custom group's `part` fractions are only checked against ±4096 and then multiplied by a group box of up to 8192 m ([map.cpp:423](../src/game/map.cpp#L423), [:476](../src/game/map.cpp#L476)), so validated ops produce bounds in the ±800 km range. The grid allocation then needs about 1.7·10¹¹ cells, which means `bad_alloc` on the tick thread. Separately, 256 groups × 1024 parts make 262k generated brushes. The 60k cap only counts `b ins`. Indices past 65,535 overflow the `uint16` cell list ([map.cpp:544](../src/game/map.cpp#L544)), and those brushes vanish from collision (reproduced). On any unranked server a lone player can pass their own `callvote edit on`. Each `g part` op also re-serializes and rebuilds the whole map on the tick (7.7 ms per op). | Validate geometry after group expansion. Cap total generated brushes. Use `uint32` indices. Catch exceptions around edits. Rate-limit edit ops. |
| NET-4 | Medium | T | **Info-request amplification:** 5 bytes in, about 300 out, unauthenticated, unthrottled ([:257-265](../src/game/server.cpp#L257)). | Padded requests and a per-source rate limit. |
| NET-5 | Medium | T | **Lag compensation is client-steered and imprecise.** The client chooses `viewTick` up to 500 ms back ([:1247-1249](../src/game/server.cpp#L1247)), and that includes the knife, so it decides tags and finds in tag and hide-and-seek. History lookup takes the newest entry before `vt` with no interpolation ([:1094-1098](../src/game/server.cpp#L1094)): up to 8 ms stale at 125 Hz, about 26 cm at dash speed against a 0.40 m half-width. The lookup is also a linear walk per player per ray: about 62k iterations for one shotgun blast, about 3M/s for a laser. | Cap the rewind at measured ping + interp + tolerance. Rewind once per shot with an O(1) index. Interpolate between bracketing ticks. |
| NET-6 | Medium | T | **Clients can trigger tick-thread stalls.** `callvote map` / `mode` / `tmaps` read and parse map files on the tick thread ([:599](../src/game/server.cpp#L599), [:619](../src/game/server.cpp#L619)). There is no vote cooldown, and up to 16 string commands per packet with no per-packet budget. This hurts the 1% and 0.1% lows the project optimises for. | Check names against a cached `listMaps()`. Vote cooldown. Per-tick string-command budget. |
| NET-7 | Medium | T | **`say` has no cooldown and no sanitising** ([:489-495](../src/game/server.cpp#L489)). One client floods every player's reliable-event queue, which evicts real events (NET-11). It also injects control or escape sequences into the operator's console and log. | Chat cooldown plus a token bucket; apply the name filter to chat. |
| NET-8 | Medium | T | **Admin is decided by "source IP is 127.x"** ([:27](../src/game/server.cpp#L27), [:333](../src/game/server.cpp#L333)). Behind any local relay or tunnel, every remote player becomes admin. | An admin password, or admin only for the in-process host. |
| NET-9 | Medium | R | **Shotgun and bubble spread come from the client-chosen `cmd.seq`** ([gamedefs.h:155-161](../src/game/gamedefs.h#L155), [server.cpp:441](../src/game/server.cpp#L441), [:1258](../src/game/server.cpp#L1258)). At 15 m: 5.8/20 pellets hit honestly, 9.4 with the best of 16 seqs, 11.7 with the best of 256, i.e. **about 2× damage**. | Seed from a server-side shot counter plus a per-match salt. The client can mirror both for tracers. |
| NET-10 | Medium (High in hide and seek) | T | **Information leaks that decide modes:**<br>• "Hidden" players still send health, weapon and flags, including `PS_FIRING` and `PS_HASBOMB` (which also marks "it"). Only position, yaw, pitch, hook and keys are stripped ([protocol.cpp:96-114](../src/game/protocol.cpp#L96)). Ranked deliberately hides health bars, yet every client knows every enemy's exact HP.<br>• Everyone within 20 m is always sent, through walls ([server.cpp:1399](../src/game/server.cpp#L1399)).<br>• The seeker's head-start blackout is client-side only: a black rectangle, while hiders' positions and footsteps still arrive ([game.cpp:1763-1768](../src/client/game.cpp#L1763)).<br>• Projectiles go to everyone ([server.cpp:1465-1469](../src/game/server.cpp#L1465)).<br>• Dead players see everyone and can chat to the living ([:1485](../src/game/server.cpp#L1485)). | Health only for visible players. No earshot exemption in hide and seek. Send the seeker nothing during freeze. Cull projectiles. Dead players chat only to the dead. |
| NET-11 | Low-Med | T | **Reliable events are silently dropped past 256 unacked** ([:745](../src/game/server.cpp#L745)), and the client can't detect gaps ([netclient.cpp:316-318](../src/game/netclient.cpp#L316)). Losing `EV_RANKED` means the match is never rated. Losing an `EV_RULES` chunk desyncs custom-mode prediction for the whole session. | Disconnect with a reason, or resync full state. |
| NET-12 | Low | T | **Ranked-id squatting.** `rankid` is first-come with no proof, and ids are broadcast ([:496-503](../src/game/server.cpp#L496)). | Sign a server nonce with the key. |
| NET-13 | Low | P | `timeMs_` is a `uint32` that wraps after 49.7 days; lag-comp and ping comparisons aren't wrap-aware ([:1096](../src/game/server.cpp#L1096), [:433](../src/game/server.cpp#L433)). | 64-bit server time. |
| NET-14 | Low | P | The `uint16` projectile id can collide with a long-lived sticky after 65k shots, giving the client a wrong delta base. | Widen the id or scrub it on reuse. |
| NET-15 | Medium | R | **Accepted edits can produce a map that won't load, and the session autosaves it over `maps/<name>.qmap`** ([server.cpp:1903](../src/game/server.cpp#L1903)). Found by fuzzing:<br>• The minimum-size check on `b del` counts *generated* brushes ([map.cpp:369](../src/game/map.cpp#L369)), so every real box can be deleted, and deleting the groups then leaves 0 brushes. Edit indices also point at generated brushes, whose positions shift whenever groups change.<br>• `g set` that turns a custom group into a room keeps its `parts` ([map.cpp:424](../src/game/map.cpp#L424)), and the saved file then fails to parse.<br>Any edit-session participant can trigger either. | Before broadcasting or autosaving, re-parse the serialized result. Count only editable brushes. Clear `parts` on a type change. Keep a `.bak` on autosave. |

### 3.2 Client vs. a hostile server or shared files

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| CLI-1 | **High** | R ✓ | **Out-of-bounds writes and reads from server-supplied ids.**<br>• The snapshot player id is never range-checked ([protocol.cpp:154](../src/game/protocol.cpp#L154)), and `interpolatedPlayers` writes `out[pb.id]` into a 32-entry stack `std::array` ([netclient.cpp:456](../src/game/netclient.cpp#L456)). A server, or a demo, with id ≥ 32 corrupts the client's stack every frame. ASan reports a `stack-buffer-overflow` write of 60 bytes per player from a 370-byte demo built with the project's own encoder. In 7.1M fuzz executions the snapshot decoder never misbehaved itself, but 994 corpus inputs decoded an id ≥ 32.<br>• Same class: `EV_KILL` → `specTarget_` → `players[specTarget_]` ([game.cpp:801](../src/client/game.cpp#L801) → [:1172](../src/client/game.cpp#L1172)).<br>• `EV_BRACKET` indices, negative values included ([:884-888](../src/client/game.cpp#L884), [:1896-1898](../src/client/game.cpp#L1896), [:4157-4162](../src/client/game.cpp#L4157)).<br>• The mode byte indexes `cfg_.modeHud[13]` / `modeCtl[13]` ([:936-937](../src/client/game.cpp#L936), [:2130](../src/client/game.cpp#L2130)). | Validate every id against `kMaxPlayers` (players, projectile owners, `clientId`, `selfWeapon`), and modes against `MODE_COUNT`, once at decode. Reject duplicate ids. Keep the demo as a regression test. |
| CLI-2 | **High** | R | **A server- or demo-supplied map name becomes a file path.** `useMap` checks for `/\.` ([netclient.cpp:242](../src/game/netclient.cpp#L242)), but the raw name still flows to `startDownload` ([:132](../src/game/netclient.cpp#L132)). From there it reaches the download cache ([:163](../src/game/netclient.cpp#L163), with `create_directories`), the lightmap cache ([lightmap.cpp:59](../src/client/lightmap.cpp#L59)) and demo recording ([game.cpp:3646](../src/client/game.cpp#L3646)). **New route:** a `.qdemo` map record sets the name with no check at all ([netclient.cpp:694-706](../src/game/netclient.cpp#L694)). A shared demo whose name walks up directories was accepted, and watching it writes a `.qlm` outside `maps/`. | One `validMapName()` at `PKT_ACCEPT`, in `pollPlayback` and in `Map::load`. Caches keyed by content hash. *Correction (fix CLI-2): the rule is `[A-Za-z0-9_-]{1,40}` minus Windows device names (con, nul, com1, ...), not `[a-z0-9_-]{1,32}`: the editor already saves mixed-case names up to 40 characters, and a device name opens the device instead of a file.* |
| CLI-3 | High | R | **A hostile map crashes or hangs the client.**<br>• `Map::parse` has no limits: a 42-byte map with one big box asks ASan for a 240 GB grid at `buildGrid` ([map.cpp:537](../src/game/map.cpp#L537)). In release that's an uncaught `bad_alloc`, and the process ends.<br>• With a box at the edge of the float range, the `(int)std::ceil` casts are UB (they trap under UBSan, [map.cpp:535](../src/game/map.cpp#L535)). In release the map *loads* with a 1×1 grid and **the floor under the spawn isn't solid**.<br>• More than 65,535 brushes silently drop out of collision.<br>• The lightmap prepass is O(F²) + O(F·B) (PERF-2); extrapolated, a 200k-box map takes *hours*.<br>• Light, ladder and group sizes are uncapped, unlike the edit path. All of this is reachable by download and by demo. | One shared validator for parse, edit and download, with bounds after group expansion and capped counts. Reject non-finite and out-of-range values as errors instead of silently producing non-solid geometry. Catch exceptions around load. |
| CLI-4 | Medium | R ✓ | **A crafted demo freezes the game.** `p += 13 + n` is computed in `uint32_t` and wraps to 0 ([netclient.cpp:664-666](../src/game/netclient.cpp#L664); same at [:690](../src/game/netclient.cpp#L690)), so the length scan never ends. | `size_t` arithmetic; require `n <= size - p - 13`; cap the demo size. |
| CLI-5 | Medium | T | **Prediction replay is unbounded and stale in demos.** It runs from the server or demo `ackCmdSeq` to `nextSeq_` ([netclient.cpp:350](../src/game/netclient.cpp#L350)), possibly millions of iterations per snapshot. `startPlayback` and `disconnect` never reset `nextSeq_`, `cmds_` or `predictedSeq_` ([:650-677](../src/game/netclient.cpp#L650)), so the viewer's old commands are replayed onto the recorded player. | Start at `max(ack+1, nextSeq_-ring)`; reset prediction state on connect, playback and disconnect. |
| CLI-6 | Medium | R | **NaN and unchecked values from rules and config.**<br>• `clampf_` passes NaN through ([customrules.cpp:78](../src/game/customrules.cpp#L78)), so a server's `EV_RULES` can set gravity or speed to NaN, and per-weapon values to NaN through `weapon=` (`sscanf %f`). Downstream, `Writer::vecq` casts `clampf(NaN)` to `int16_t`, which is UB.<br>• A mode name of 40 spaces plus a character makes an *active* custom mode with an all-space name, because `safeName` trims before truncating ([customrules.cpp:38-39](../src/game/customrules.cpp#L38)). Its own `toText()` is then rejected by `fromText()`.<br>• `quark.cfg` accepts `nan` for sensitivity and FOV ([config.cpp:40-42](../src/client/config.cpp#L40)).<br>• `shadowres` goes unchecked into `glTexImage2D` ([:79](../src/client/config.cpp#L79)).<br>• `mode_hud_*` values become path components ([game.cpp:2131](../src/client/game.cpp#L2131)). | `std::isfinite` plus clamps everywhere a float is read. |
| CLI-7 | Low | T | `fromHex` uses `std::stoi`, which throws ([community.cpp:20](../src/client/community.cpp#L20)). See COR-1 for the crash this causes in normal use. | Non-throwing decode. |
| CLI-8 | Low | T | Caches keyed by name ([lightmap.cpp:59](../src/client/lightmap.cpp#L59)): a downloaded or edited map that reuses an official name evicts the official cache, costing up to 24 s of re-bake on big maps. The editor's bake of an unsaved map does the same ([editor.cpp:1113](../src/client/editor.cpp#L1113)). | `cache/<sha256>.qlm` in a user directory. |

### 3.3 Ranked and online services (Cloudflare Worker)

The Worker header says "a lying host just produces a mismatch." It doesn't, and the leaderboards drive Discord roles.

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| WEB-1 | **Critical** | R | **Rating farming with no game.** An account is any random 64-hex secret, and the Worker can't tell whether a match happened. Against a local copy, 10 fake 16-player FFAs (160 POSTs) took a fresh account to display rating 690, #1 on the board. The Worker allows 32 entries per match ([worker.js:211](../tools/master/worker.js#L211)); the game caps at 16. | Worker-issued match tickets at ranked match start, binding players and server. Limit what new accounts can do (e.g. ratings only move against established players). |
| WEB-2 | **High** | R ✓ | **Any account that knows a match id can void that match, and losers can always void their own loss.** The "in this match" check tests the reporter's *own* list ([worker.js:237](../tools/master/worker.js#L237)), and any disagreement voids the match ([:250-252](../tools/master/worker.js#L250)). The match id is broadcast to every client, spectators included ([gamemodes.cpp:83](../src/game/gamemodes.cpp#L83)). Leaving before the end also escapes rating, because only connected players are counted ([server.cpp:1923](../src/game/server.cpp#L1923)). | Accept reports only from the ticket's player set. A lone dissent is a dispute, not a void. A non-report or leave counts as a loss. |
| WEB-3 | High | T | **The host decides results.** Clients forward `EV_RANKED` verbatim, without comparing it to what they saw ([game.cpp:811-820](../src/client/game.cpp#L811)). | Clients check the result against their own scoreboard; signed results. |
| WEB-4 | High | T | **Race times are self-reported.** The only checks are `ms ≥ 5000`, increasing splits, and last split == ms ([worker.js:52-54](../tools/master/worker.js#L52)). | Server-signed runs, or demo upload with server-side verification. |
| WEB-5 | High | R | **The server list can be taken over or emptied.**<br>• `/list` returns `ORDER BY seen DESC LIMIT 200`, and one IP may announce unlimited ports ([worker.js:308](../tools/master/worker.js#L308)): 200 announces from one IP removed every real server.<br>• `?ip=` is trusted whenever the caller isn't IPv4 ([:288](../tools/master/worker.js#L288)), so an IPv6 caller can remove any server or list any ip:port.<br>• Writes happen on GET with `access-control-allow-origin: *`, so any web page can make its visitors' browsers do this. | At most N entries per IP. Never trust `?ip=`. POST-only writes without CORS `*`. Verify servers before listing. |
| WEB-6 | High | T | **Quota exhaustion takes every online feature down.**<br>• The free plan is 100k requests/day.<br>• The bot alone polls about 20k/day.<br>• Each client with the queue menu open polls about 17k/day.<br>• `/crowns` and `/leaderboard` full-scan `players` (`LIKE 'ladder:%'` plus an expression sort), and WEB-1 inflates that table cheaply.<br>• `GET /queue` runs a `DELETE`. | Cloudflare rate limits. Cache GETs for 30-60 s. A ladder column with an index, or a top-N table. Lower poll rates. |
| WEB-7 | Medium | R | **Lost update on ratings.** Read-modify-write with no version check ([worker.js:260-272](../tools/master/worker.js#L260)): concurrent matches for one player overwrite each other, which can erase a loss. Reproduced 50/50 with simulated latency. | Version column with conditional update, or a Durable Object per player. |
| WEB-8 | Medium | R | **Queue double fill.** No transaction around read, insert and delete ([:157-164](../tools/master/worker.js#L157)), causing duplicate fills and pings. Reproduced 50/50. | Atomic batch or Durable Object. |
| WEB-9 | Medium | T | **Discord identity is unverified.**<br>• `/link` accepts any name ([:76-86](../tools/master/worker.js#L76)).<br>• The bot also matches by nickname or global name ([bot.py:554-564](../tools/discordbot/bot.py#L554)), so a guild member can take the crown role of a linked player who isn't in the guild.<br>• Queue fills ping every linked member 5 times ([bot.py:566-585](../tools/discordbot/bot.py#L566)). | Discord OAuth, or a one-time code DM'd by the bot. Ping only verified links. |
| WEB-10 | Medium | T | **Stats and queues can be inflated.** Lifetime stats take 300 kills every 20 s forever, and fake secrets can fill queues. | Signed results; per-IP and per-id rate limits. |
| WEB-11 | Medium | T | **The Worker URL can be changed by any push.** The client fetches it from GitHub Pages `master.txt` ([master.cpp:5](../src/game/master.cpp#L5)), and every push to any branch force-redeploys `site/`. The live value is a personal `*.workers.dev` subdomain: confirm the project controls that Cloudflare account. Whoever controls it receives every player's ranked secret. | Pin the URL in the binary, or sign `master.txt`. Pages only from tags. |
| WEB-12 | Low | T | **Worker hygiene:** tables never pruned (matches, reports, filled; a names or lifetime row per new secret); `formData()` throws a 500 on bad bodies; no input-length limits; no `isFinite` guard in `rateTeams`; `/crowns` and `/filled` publicly map ranked id ↔ in-game name ↔ Discord name. | Cron pruning, try/catch and size caps, finite guards, privacy notice. |
| WEB-13 | Low | T | **Ranked key exposure on Linux/macOS.** The ranked secret is on the curl command line ([http.cpp:76](../src/common/http.cpp#L76)), and `quark.key` is created `0644` and written non-atomically ([ranked.cpp:47-51](../src/game/ranked.cpp#L47)). | libcurl or `--data @-`; mode 0600; tmp+rename; a user config directory. |

### 3.4 Discord bot

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| BOT-1 | **High** | T | **Mention injection.** Server info is posted without `allowed_mentions` ([bot.py:383-397](../tools/discordbot/bot.py#L383)), with no bot-wide default ([:264](../tools/discordbot/bot.py#L264)). The README's invite grants **Mention Everyone**, and the ping roles are created mentionable. A game server whose name contains a mass mention pings the whole guild. The cooldown is per address, so each new port means a new channel and a new ping. | `Bot(allowed_mentions=AllowedMentions.none())`, opting in per message. Drop Mention Everyone. Per-IP cooldown. |
| BOT-2 | Medium | T | **Markdown and link injection.** Server names are inserted unescaped, so masked phishing links render in role-ping messages. Leaderboard names can break out of the code block ([worker.js:41](../tools/master/worker.js#L41), [bot.py:303](../tools/discordbot/bot.py#L303), [:444](../tools/discordbot/bot.py#L444)). | `escape_markdown`, strip URLs and backticks. |
| BOT-3 | Medium | P | **Channel flood.** One text and one voice channel per listed server (up to 200). A category holds 50. The create call isn't in a try, so one failure aborts every tick: no state save, and stale channels are never deleted. | Cap live channels, require a minimum uptime, catch errors per server. |
| BOT-4 | Medium | T | **Secrets leak at install time.** [install.sh:45](../tools/discordbot/install.sh#L45) passes `DISCORD_TOKEN` and `BOT_KEY` as env arguments through `sudo`, so they show in process listings and sudo logs. The README's alternative unit puts tokens in a world-readable unit file. | Export in a subshell; document `EnvironmentFile=` with mode 600 only. |
| BOT-5 | Low | T | **Over-privileged, loose dependencies.** Mention Everyone and Manage Roles are broader than needed; `requirements.txt` has ranges and no hashes; `.gitignore` misses `config.json`, `state.json` and `.venv`; "queue for all" sends 16 requests per click with no cooldown. The history scan for committed secrets came back clean. | Least privilege, pinned and hashed dependencies, extend `.gitignore`. |

### 3.5 Supply chain: launcher, CI, server operations

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| SUP-1 | **Critical** | T | **Updates are unsigned.** No hash or signature check anywhere ([launcher/main.cpp:231-300](../src/launcher/main.cpp#L231)). The download is *any* asset whose name ends in `quark-<platform>.qpak`, and its host isn't checked ([:253](../src/launcher/main.cpp#L253)). A leaked token, a compromised account or a compromised Action means code runs on every player's machine. | Sign each qpak with Ed25519 in a protected release environment. Embed the public key and verify before extracting. Authenticode-sign the exes. |
| SUP-2 | **High** | R | **The release process has already shipped binaries that don't match their tags.** Any push to any branch releases with `make_latest`, there is no concurrency group, and overlapping runs re-upload assets. v0.26.1 (tag `d2e0393`) had all 9 assets created 3 min *after* publication, by the run for `502c1f4`. v0.26.3 (tag `bd69ca2`) carries assets built from `342fdeb`. There is no branch protection and no ruleset. | `push: tags: ['v*']` from the default branch only, a concurrency group, fail if the release exists, protected main with required reviews, `SHA256SUMS` plus build attestations. |
| SUP-3 | High | T | **The CI write token is exposed to build code.** `contents: write` is granted to every job, and `actions/checkout` persists it in `.git/config` while cmake, apt and the build run. A build step could push gh-pages (and so `master.txt`, WEB-11) or rewrite releases. No action is SHA-pinned. | `contents: read` by default, `write` only in release and pages, `persist-credentials: false`, SHA pins with Dependabot. |
| SUP-4 | Medium | R | **The pages job runs `eval()` with the write token.** `site_changelog.py:22` evals `gamedefs.h` text in that job. It has failed at least 3 times *after* the release published, leaving the site stale. | Regex or `ast.literal_eval`; `actions/deploy-pages`; pages only from tags. |
| SUP-5 | Medium | R | **The launcher's extractor writes outside the install directory on Windows.** Compiled with the release compiler, and fuzzed with MSVC's STL, the path check ([main.cpp:167](../src/launcher/main.cpp#L167)) accepted drive-absolute, root-relative, drive-relative and UNC names, NTFS stream names, `CON`/`NUL` and trailing dots. A hostile entry size throws inside the worker thread and terminates the launcher ([:171](../src/launcher/main.cpp#L171)). So does a name that isn't valid UTF-8, through `fs::u8path`. The install overwrites in place, so quitting mid-install leaves a half-updated game, and `detach()` leaves the thread using the stack `State` ([:527](../src/launcher/main.cpp#L527)): a use-after-free at exit. | Require relative paths with no root name and no reserved or stream names. Cap entry sizes. Extract to `game.new/` then rename. Heap state, or join the thread. |
| SUP-6 | Medium | T | **Release selection can pick the wrong version or downgrade.** Prereleases count as "latest". Only 50 of 98 releases are fetched, so pins outside that window silently fall back ([main.cpp:34](../src/launcher/main.cpp#L34), [:324-326](../src/launcher/main.cpp#L324)). Any version different from the installed one is installed, so a newest release without this platform's asset means an automatic downgrade ([:327](../src/launcher/main.cpp#L327)). | Skip prereleases and drafts, paginate, never auto-downgrade. |
| SUP-7 | Medium | P | **DLL planting when the launcher runs from Downloads.** It imports `winhttp`, `winmm` and `opengl32`, and GLFW loads `opengl32`, `dwmapi` and `dinput8` by name. None is a KnownDLL, and the exe's own folder is searched first. | `/DEPENDENTLOADFLAG:0x800`; `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` first thing; delay-load. |
| SUP-8 | Medium | R | **Dedicated-server auto-update is fragile and over-trusting.**<br>• It builds whatever is on the branch, with no signed tag check.<br>• Installed with `sudo`, it runs as root ([install.sh:20](../tools/server/install.sh#L20), [:33](../tools/server/install.sh#L33)).<br>• A failed `--ff-only` merge silently stops all future updates ([autoupdate.sh:20](../tools/server/autoupdate.sh#L20)).<br>• The hard-coded platform list ([:16](../tools/server/autoupdate.sh#L16)) froze every server on 1.14.1 for about 4 h, during v1.14.2–v1.15.1, which had no macOS assets, and for about 2.5 h before that. It healed at v1.16.0.<br>• The units have no systemd hardening, and the server can write the `autoupdate.sh` the timer executes. | Signed tags; a dedicated user; `ProtectSystem=strict`, `NoNewPrivileges`, etc.; read-only code; derive platforms from the release; alert when updates stall. |
| SUP-9 | Low | T | **Shell commands built from paths** on Linux/macOS ([install.cpp:86](../src/launcher/install.cpp#L86), [:180](../src/launcher/install.cpp#L180), [launcher/main.cpp:100](../src/launcher/main.cpp#L100)). Inputs are local or GitHub-controlled. | `posix_spawn` with an argv array. |
| SUP-10 | Low | T | **TLS settings:** TLS 1.1 enabled and 1.3 never offered ([http.cpp:21](../src/common/http.cpp#L21), [launcher/main.cpp:66](../src/launcher/main.cpp#L66)). Don't just "use OS defaults": Windows 7, a supported target, *needs* the explicit TLS 1.2 flag. | Request TLS 1.2 \| 1.3, retrying with 1.2 alone where the 1.3 flag is rejected; drop 1.1. |
| SUP-11 | Low | T | **User data lives in the install folder, and uninstall deletes it.** That includes `quark.key` (the ranked identity), `quark.cfg`, `hud.cfg`, demos, huds, controls, groups and `race_times.log` (`findDataPath("maps")/..`). `remove_all(gameDir())` deletes all of it ([install.cpp:261](../src/launcher/install.cpp#L261)). Screenshots go to the working directory. | Per-user config and data directories. |
| SUP-12 | Low | T | **Site and launcher hygiene.** The site loads Google Fonts (no SRI possible, visitor IPs go to Google) and has no CSP. XSS was checked and refuted: everything passes through `esc()`. The launcher's single-instance mutex can be squatted (same-user DoS). | Self-host fonts; CSP. |

---

## 4. Correctness and performance

**Measured baseline (MSVC build, RTX 3090 Ti, 24 threads).**
- Bastion vs bots: 2.7-2.8k fps average, 1% low 900-980, 0.1% low 690-720.
- CPU frame time: 0.13-0.14 ms average, 0.19 ms at the 99th percentile, 0.24 ms at the 99.9th.
- So **the CPU frame is tiny, and the 1% and 0.1% lows come from the GPU, present and fence side.** That is why PERF-4 matters.
- Caveat: shipped Windows builds use MinGW `-O2`, not MSVC.

### 4.1 Bugs that hit players in normal use

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| COR-1 | **High** | R ✓ | **Queue-fill crash.** Discord queue members look like `d:<name>:<hex>:<hex>`. The client splits at the *first* `:` and hex-decodes the raw Discord name, `std::stoi` throws, nothing catches it, and the process terminates ([community.cpp:107-111](../src/client/community.cpp#L107), [:20](../src/client/community.cpp#L20)). Every client that is queued or has the queue menu open crashes within 10 s of such a fill. No attacker is needed. | Split from the right (as `quarkdata.py:479` does); non-throwing hex; try/catch around all Worker-reply parsing. |
| PERF-1 | **High** | T ✓ | **The input backlog never drains.** Tokens refill at exactly the command arrival rate ([server.cpp:956](../src/game/server.cpp#L956), [:984-995](../src/game/server.cpp#L984)), and the queue holds 12 commands ([:18](../src/game/server.cpp#L18), [:442](../src/game/server.cpp#L442)). After any client hitch, 12 commands stay queued indefinitely: **+12 ms of input latency at 1000 Hz, +96 ms at 125 Hz**, until clock drift or a packet loss happens to clear it. If the client clock runs slightly fast, the server drops the oldest command, which loses inputs. | When depth stays above 1-2 for N ticks, run an extra command per tick, or collapse old ones. Show queue depth on F3. |
| PERF-2 | **High** | R | **Midtown can't be joined.** Three quadratic prepasses run on the main thread *before* the cache check, with no progress callback and no network keepalive: the `pointSolid` cull is O(F·B), `covered()` is O(F²), and `needLift` is O(F²) ([lightmap.cpp:28-33](../src/client/lightmap.cpp#L28), [:86-216](../src/client/lightmap.cpp#L86)). Midtown takes 13-14 s *with* a cached `.qlm`, and 24 s cold. The server drops silent clients after 10 s. Verified end to end: "CONNECTION TO SERVER LOST" on a dedicated server, "player dropped: timed out" on a listen server. Lodestone (8-14 s) is marginal on slower CPUs. The window shows "Not Responding" because `drawBakeProgress` never polls events ([game.cpp:312-323](../src/client/game.cpp#L312)). | Bucket faces by (axis, sign, plane, material). Use the map grid in `pointSolid`. Check the cache before preprocessing. Bake on a worker thread while `frame()` keeps polling the network and drawing a loading bar. |
| PERF-3 | High | T | **The bake reads the live network map.** The progress callback calls `net_.poll()` and `net_.tick()` while 24 bake threads read `net_.map()` ([game.cpp:297-305](../src/client/game.cpp#L297)). A map change during the bake replaces the map: a use-after-free. Every snapshot also rewrites `dynSolids`, a data race. On payload maps the cart is already in `dynSolids` ([netclient.cpp:334-335](../src/game/netclient.cpp#L334)), so its shadow is baked into the cached lightmap at its start position. | Bake a private copy (0.23 ms to copy on Midtown) with `dynSolids` cleared; defer map swaps until the bake finishes. |
| PERF-14 | **High** for anyone who sets `maxfps` | R ✓ | **The frame cap sleeps about 15 ms per call on the shipping MinGW build.** The cap loops `sleepMs(1)` until 1.5 ms before the target ([main.cpp:109](../src/client/main.cpp#L109)), and `sleepMs` is `std::this_thread::sleep_for` ([net.cpp:191](../src/common/net.cpp#L191)). Measured: MinGW `sleep_for(1ms)` takes 15.15 ms, while `Sleep(1)` after `timeBeginPeriod(1)` takes 1.91 ms and MSVC's `sleep_for` 1.89 ms. Any cap above about 60 fps becomes roughly 66 fps with 15 ms frame spikes, exactly what the 1% and 0.1% lows measure. The default `maxfps=0` (uncapped) avoids it, and the server waits in `select()`, so it isn't affected. The same sleep makes the test suite time out on Windows (§7.3). Measured with MSYS2 GCC 13 and winpthreads; CI's Ubuntu mingw-w64 is likely the same but wasn't verified. | On Windows, `Sleep()`, or a high-resolution waitable timer with `Sleep` as the Win7 fallback. Then spin only the last ~1 ms. |
| PERF-4 | Medium | R ✓ | **The default `low_latency=1` behaves like "ultra".** `afterSwap` stores frame N's fence and flips the index, and the next `beforeInput` waits on that same fence ([game.cpp:3835-3860](../src/client/game.cpp#L3835)). So zero frames are ever queued. On Bastion: off gives 4329 fps average / 1482 at the 1% low; on gives 2812 / 924; ultra gives 2825 / 964. That is about 35% of fps lost with no latency gain over ultra. | Wait on the fence from two frames back (the slot about to be overwritten). |
| PERF-5 | Medium | T | **The game clock is float seconds on both client and server.** On the client, `renderTime_` and the `timeMs*0.001f` comparisons ([netclient.h:150](../src/game/netclient.h#L150), [netclient.cpp:441-452](../src/game/netclient.cpp#L441), [:372](../src/game/netclient.cpp#L372)) count from server start. Precision is 1 ms after 2.3 h of uptime, 2 ms after 4.6 h, and 3.9 ms after 9.1 h, at which point consecutive 500 Hz snapshots start to collide. From 18-24 h it's 7.8 ms and most collide. Interpolation becomes stepped and the lag-comp `viewTick` drifts. On the server, `Server::time()` is `timeMs_ * 0.001f` ([server.h](../src/game/server.h)), so *every* server-side float timer (refire, cooldowns, respawns, phases, votes) is quantised the same way: about 8 ms after a day, about 62 ms after a week. `timeMs_` only resets in `start()`, and a `uint32` wraps after 49.7 days (NET-13). Frequent releases restart servers often, which hides this; a stalled auto-update exposes it (SUP-8). | Integer ms (64-bit) for all game time, with doubles or ms-relative floats only for local deltas. |
| PERF-6 | Medium | T | **The prediction ring is 256 commands, which is 256 ms at 1000 Hz** ([netclient.h:152-156](../src/game/netclient.h#L152), [netclient.cpp:341-357](../src/game/netclient.cpp#L341)). Above that RTT the replay skips overwritten commands, so high-ping players rubber-band constantly. | Size the ring to tick rate × 1 s (1024+). |
| PERF-7 | Medium | T ✓ | **Dynamic objects are culled against last frame's camera.** Frustum planes are extracted from `viewProj_` *before* it's updated ([renderer.cpp:546-557](../src/client/renderer.cpp#L546)). Players and projectiles vanish for a frame at the screen edge during flicks, and after spawns and spectator switches. | Assign first, then extract. Share one extraction function with `drawWorldChunks`, which does it right. |
| COR-2 | Medium | T | **Saves are non-atomic.** `quark.cfg`, profiles, `hud.cfg`, `quark.key` and custom modes are truncated then written ([config.cpp:134-137](../src/client/config.cpp#L134), [profiles.cpp:37-42](../src/client/profiles.cpp#L37), [ranked.cpp:50-51](../src/game/ranked.cpp#L50)). A crash or full disk loses every bind, or the ranked identity. | Reuse `saveMapFile`'s tmp+rename. |
| COR-3 | Medium | T | **The vs-bots mode is lost on restart.** `bot_mode` is clamped to 0..5, but the picker offers 13 modes ([config.cpp:107](../src/client/config.cpp#L107)). | Clamp to `MODE_COUNT-1`. |
| COR-4 | Medium | R | **The editor corrupts maps.**<br>• The undo inverse is pushed *before* the op applies, so a partial multi-line failure leaves a stale inverse. Online, inverses address objects by index, so another editor's insert makes your undo hit the wrong object. There is no redo ([editor.cpp:247-263](../src/client/editor.cpp#L247), [:957](../src/client/editor.cpp#L957)).<br>• `trim` and `clip` brushes can't be edited (materials 0..7 only, [map.cpp:390](../src/game/map.cpp#L390)). Moving one fails, and undoing its deletion loses it.<br>• `b set` on a generated brush duplicates it. | Apply to a copy, then commit and push the inverse. Stable object ids. Redo stack. Full material range. |
| COR-5 | Medium | R | **The MSVC build is broken, and non-ASCII filenames don't work.** The MSVC branch doesn't build the client: `windows.h` `min`/`max` macros, and a WIN32 exe with no `WinMain`. With MSVC, `path::string()` throws on non-ANSI filenames, which would crash the demo, map, profile and group lists. The shipped MinGW build returns UTF-8 instead, which the narrow `fopen` can't open, so those files are silently unusable. | `NOMINMAX` plus `/ENTRY:mainCRTStartup` and an MSVC CI job (or drop the branch); `path` and wide APIs end to end. |
| COR-6 | Medium | T | **The rules globals race between threads** (corrects v1 §3). The racing writer is the *main* thread's `setActiveRules`, on `EV_RULES` ([game.cpp:862](../src/client/game.cpp#L862)) and `leaveGame` ([:247](../src/client/game.cpp#L247)). It reassigns strings and vectors and rebuilds `g_weapons` while the host thread reads `weaponDef()` / `moveTuning()`: UB and a possible use-after-free. `Server::start`/`stop` run on the main thread, so those don't race. The `g_init` guard is unsynchronised. `kKnifeRange` is captured at static init and never follows a custom mode ([server.cpp:25](../src/game/server.cpp#L25)). `rand()` from two threads is not UB on these runtimes, only non-deterministic. | Pass a `const Rules&` into pmove and weapons; one rules instance per simulation. |
| COR-7 | Low | R | **`Map::parse` keeps stale fields across reparse:** `title`, `sunDir`, the palette and `dynSolids` survive. | Begin with `*this = Map{}`. |
| COR-8 | Low | T | **Players and projectiles are drawn at stale or wrong positions.** Interpolation falls back to poses up to about 1 s old after clock resets ([netclient.cpp:438-447](../src/game/netclient.cpp#L438)). Projectile extrapolation ignores gravity. | Clamp to the newest snapshot; integrate gravity. |

### 4.2 Frame-time spikes and tick-thread cost

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| PERF-8 | Medium | T | **The server copies every snapshot several times per client.** Per client per snapshot, player and projectile vectors are copied about 3 times, plus a 256-deep history per client: about 800 KB per client, 25 MB at 32 players ([server.cpp:1473-1528](../src/game/server.cpp#L1473)). This is the dominant per-tick copy cost. | Reuse scratch buffers; store compact deltas; shrink the ring to the ack window. |
| PERF-9 | Medium | T | **Visibility raycasts can spike the tick.** The cost is O(humans × players × 32 rays) at 20 Hz, all in one tick ([:1387-1416](../src/game/server.cpp#L1387)): about 30k rays per pass at 32 players, on 9k-brush maps. | Stagger across ticks; cap rays; coarse PVS. |
| PERF-10 | Medium | P | **Bots can stall the tick.** `NavGraph::build` is O(N²) and is built lazily inside `botThink` on the tick thread. Each bot runs a full Dijkstra with allocations every 0.25 s, and `nodeAt` is O(N) ([bots.cpp:155-231](../src/game/bots.cpp#L155)). | Build at load, off the tick thread. Stagger repaths. A* with reused buffers. |
| PERF-11 | Medium | R | **Main-thread hitches:**<br>• `rescanMaps` parses every map: 105 ms, and it's also called from the in-match vote picker ([game.cpp:325-345](../src/client/game.cpp#L325)).<br>• Every remote `EV_EDIT` re-serializes the whole map: 8.4 ms on Midtown ([netclient.cpp:233](../src/game/netclient.cpp#L233)).<br>• Join refresh joins the previous HTTP thread: up to 9 s ([:496](../src/game/netclient.cpp#L496)).<br>• Quit joins `RankService`: up to 8 s.<br>• Demo recording allocates 64 KB and calls `fflush` per snapshot.<br>• `race_times.log` is read mid-run.<br>• Changing the announcer copies PCM under the mixer mutex. | Cache map headers by mtime. Serialize lazily. Async HTTP with a generation counter. A writer thread. Shared_ptr swap for announcer audio. |
| PERF-12 | Low | R | **Per-frame waste:**<br>• `findDataPath` runs per HUD frame (5 `fs::exists`, 10-35 µs, 8-25% of a 0.13 ms CPU frame).<br>• `interpolatedPlayers()` runs 3-4 times per frame.<br>• Projectile vectors are built twice.<br>• About 38 string builds in `drawHud`.<br>• `sendInput` runs once per tick: 4 `sendto` calls per frame at 1000 Hz/240 fps, all carrying the same command window.<br>• The F3 overlay's `GL_TIMESTAMP` read is a sync point, so it perturbs what it measures: p99.9 goes from 0.247 to 0.345 ms and fps drops 9%.<br>• The editor's unbaked view uploads about 21 MB per frame on Midtown.<br>• The shadow pass ignores `castShadow` and uploads `dyn_` twice. | Cache paths; compute once per frame; one send after the tick loop; a static editor VBO. |
| PERF-13 | Low | T | **The lightmap atlas size is never checked.** Midtown is already 2048×4096. `GL_MAX_TEXTURE_SIZE` is never queried and the upload isn't checked ([lightmap.cpp:156-185](../src/client/lightmap.cpp#L156), [renderer.cpp:506](../src/client/renderer.cpp#L506)). | Query the limit; widen before growing; fall back. |

**Already good:**
- Tiny CPU frame.
- Chunked, frustum-culled world drawn in one `glMultiDrawArrays`.
- Shader warm-up during load.
- Raw mouse sampled after the fence wait.
- Event-driven 5 ms audio mixer.
- No GL leaks across map changes.
- Atomic `saveMapFile`.
- Bounds-checked map download.
- F3 percentiles via `nth_element` on a reused buffer.

---

## 5. Gameplay design

Weapon and movement *numbers* are placeholders (CLAUDE.md), so number critiques mean "when you tune these". The *system* findings stand regardless.

### 5.1 Rule bugs and exploits

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| GAME-1 | **Critical** | T ✓ | **Tag: "it" escapes elimination.** "It" switches to spectator: `pickIt()` hands "it" to a random innocent, who is out when time ends, and the original rejoins with `out` never set ([gamemodes.cpp:164-176](../src/game/gamemodes.cpp#L164), [server.cpp:510-540](../src/game/server.cpp#L510)). Related: `kill` or a void death respawns you far from "it" (a free teleport), and reconnecting clears `out`. | A team change or leave by "it" counts as their elimination. No team changes while live. Key `out` by player identity. |
| GAME-2 | **Critical** | T | **The syringe breaks round modes and duel modifiers.**<br>• It self-heals 10 hp/s against a 5 hp/s stalemate drain, and VS has no round time limit, so a cornered last survivor holding it never dies ([gamedefs.h:179](../src/game/gamedefs.h#L179), [gamemodes.cpp:506-526](../src/game/gamemodes.cpp#L506)).<br>• Heals cap at `maxHealth()` (100), not the life's health: "1 HP" and "Glass cannons" are back to 100 in 10 s, and "Tanks" (250 hp) can't heal above 100.<br>• A thrown syringe gives +30 hp and **resets every cooldown** (refire, dash, hook, knife) ([server.cpp:1305-1315](../src/game/server.cpp#L1305)). Two teammates trading throws get unlimited healing and dashes. | Remove it from round modes and duel. Cap heals at the life's max. Throws reset dash and hook only. Base the drain on damage dealt. |
| GAME-3 | High | T ✓ | **Multi-point overtime is inverted.** Overtime runs only while the point is *contested* ([gamemodes.cpp:764](../src/game/gamemodes.cpp#L764)), so attackers capturing alone at 0:00 lose. The optimal defence at 0:00 is to step *off* the point. The comment says the opposite. | Overtime while attackers are on the point. |
| GAME-4 | High | T | **Payload and neutral payload have no overtime.** A cart being pushed at 0:00 loses. In neutral payload, whoever is ahead of the middle at time-out wins, so get a 1 m lead and turtle ([gamemodes.cpp:713](../src/game/gamemodes.cpp#L713), [:794-801](../src/game/gamemodes.cpp#L794)). | Overtime while the cart is touched. |
| GAME-5 | High | T | **The bomb floats where an airborne carrier died.** `bombPos = v.move.pos` ([gamemodes.cpp:953](../src/game/gamemodes.cpp#L953)), pickup needs the feet within 1.2 m, and enemy splash launches about 11 m up. The bomb ends up out of reach, bots can't get it, and the attack times out. | Trace down to the floor, or use `lastGround`; add a bomb reset timer. |
| GAME-6 | High | T | **Knockback stacks with no cap.** Six chained stickies give about 86 m/s, apex about 170 m, for 17.9 hp ([server.cpp:1346-1381](../src/game/server.cpp#L1346)). In race and the ranked time trial it's free: knockback applies before `rulesCanDamage`, every weapon is allowed, and there's no velocity cap. | Strongest single knock per tick; cap self-knock; a defined race loadout. |
| GAME-7 | High | T | **Ranked can be dodged and gamed by design.**<br>• Leaving is free (WEB-2).<br>• A vote passes at timeout if yes > no ([server.cpp:1646-1648](../src/game/server.cpp#L1646)), so in 1v1 the caller's own yes passes a map change or "ranked off" mid-fight unless the opponent presses F2.<br>• No rating decay: τ only applies when you play, so inactive leaders keep crowns forever.<br>• Queues are FIFO with no rating window.<br>• The placement threshold is 5 games in the client but 3 in the Worker.<br>• On a listen server the host has about 0 ping, and projectiles have no antilag. | Absolute-majority votes; no map or ranked votes while live; decay; rating-banded queues; one threshold; prefer dedicated servers for ranked. |
| GAME-8 | Medium | T | **A free knife throw enables instant combos.** The throw has no cooldown and sets `nextFire = now` ([server.cpp:1058-1067](../src/game/server.cpp#L1058)), so throw + crossbow headshot = 105, an instant kill. | Shared cooldown. |
| GAME-9 | Medium | T | **Size modifiers scale the hitbox and eyes, not the hull.** "Double size" puts the eye at 3.2 m on a 1.8 m hull, so the camera ends up inside low ceilings; "Tiny" puts it at 0.96 m ([server.cpp:1151](../src/game/server.cpp#L1151), [:1789-1795](../src/game/server.cpp#L1789)). | Scale the collision hull. |
| GAME-10 | Medium | T | **Removing a balance bot mid-round can end the round.** It's removed the moment a human joins, even if it's the team's last player alive ([server.cpp:1576-1594](../src/game/server.cpp#L1576)), and the round is lost instantly. | Defer removal to the round end. |
| GAME-11 | Medium | T | **The tick rate changes mid-match.** It steps up in any phase, and down within 3 windows in any phase ([server.cpp:166-178](../src/game/server.cpp#L166)), contradicting [gamedefs.h:5-7](../src/game/gamedefs.h#L5). Regen counts ticks, and race times are tick-quantised (±8 ms at 125 Hz). | Change rate only between rounds; time everything in ms or substeps. |
| GAME-12 | Low | T | **`rand()` is never seeded.** Duel modifier order, tourney seeding, the first bomb carrier and the first "it" are reproducible from a fresh server start. `Server::rng_` already exists. | Seed and use `rng_`. |

### 5.2 Mode-by-mode

| Mode | Issues | Direction |
|---|---|---|
| VS | Syringe out-heals the drain (GAME-2). "Chip then hide" is optimal in itemless 1v1 and VS, because the drain kills the lower-HP player first. Bot removal ends rounds (GAME-10). Dead players ghost (NET-10). | Ban the syringe; drain by damage dealt or from round start; dead chat to the dead only |
| Duel | Heals ignore per-life max HP; giant eyes inside ceilings; reproducible modifier order | Per-life max; scale the hull; seed RNG |
| 1v1 / ranked | Leaving is free; silent votes pass; HP leak; host advantage | Signed results; majority votes; cull HP |
| Bomb | Bomb floats (GAME-5); no voluntary drop or pass | Trace to floor; reset timer; drop key |
| KOTH | One contester freezes the clock indefinitely; spawn scoring avoids teammates too | Weighted contest or cap; score spawns by enemies only |
| Payload / NPayload | No overtime; turtle after a 1 m lead; sticky traps ride the cart and survive death | Overtime; clear stickies on death |
| Multi point | Overtime inverted (GAME-3); Midtown timing outside its own band (MAP-8) | Fix overtime; grade the worst point |
| FFA | The scoreboard SCORE column (dmg/10 + objective) isn't the win score (frags); 2-spawn pools (MAP-2) | Show frags; bigger pools |
| Race / time trial | Free sticky launches (GAME-6); tick-quantised times; self-reported board (WEB-4) | Cap knock; substep timing; signed runs |
| Tag | Spectator escape (GAME-1); suicide teleport; reconnect clears `out`; 500 ms knife rewind (NET-5) | Elimination on leave; death penalty; cap rewind |
| Hide and seek | Seeker gets info during freeze; 20 m wallhack (NET-10). Rounds are `clamp(players, 3, 6)` in slot order: with 2 players one seeks twice; with 7+ some never seek. Hiders get +2 just for surviving. | Server-side cull; full rotation |
| Tourney | A disconnect is an instant walkover (reconnect = new joinId, no grace); random seeding | Reconnect grace; seed by rating |

### 5.3 Movement (when you tune it)

- **Three speed systems fight each other:**
  - **Auto-bhop:** holding jump hops on the first grounded tick, *before* friction ([pmove.cpp:104-108](../src/game/pmove.cpp#L104)). With a dash every 1.2 s, anyone reaches 32 m/s in about 9 s without strafing.
  - **Strafing is unbounded:** 12.9 → 42.2 m/s over 8 s of perfect strafing.
  - **Dash brakes at high speed:** exit speed is min(32, h+3), so above 29 m/s a dash slows you. The comment says dashes add speed.
  - **The grapple brakes too:** it clamps to 24 m/s on catch. The comment says momentum is kept.
- **Height from speed:** dash-up turns speed into height, 11.9 m at 29+ m/s. A rocket jump gives 10.6 m for 3.4 hp. There is no fall damage.
- **Map grading misses all of this.** `MAP_METRICS` models a dash-jump as "3.5 m up" and ignores rocket and sticky jumps, so it under-reports shortcuts and out-of-bounds. Viaduct already needed an invisible ceiling.
- **Direction:** one speed model with a soft cap; hook and dash *add* along their direction; jump-timing-based bhop; put the real movement envelope into the map metrics.
- **Tick-rate independence is good for movement:** fixed 1 ms substeps, and prediction matches.

### 5.4 Weapons (perfect aim, 100 hp)

| Weapon | Damage (head) | Refire | DPS | Body TTK (shots) | Notes |
|---|---|---|---|---|---|
| Rifle | 5.95 (8.5) | 0.152 s | 39.3 | 2.42 s (17) | Hitscan to 300 m, zero spread, no falloff: **dominates mid and long range** |
| Rocket | 34, 3.6 m splash | 1.2 s | 28.3 | 2.40 s (3) | 14.3 m/s self-knock |
| Knife | 30 | 0.75 s | 40 | 2.25 s (4) | Throw has no cooldown (GAME-8) |
| Sniper | 34 (42.5) | 1.5 s | 22.7 | 3.0 s (3) | **Strictly worse than the rifle.** Headshot 85 never saves a shot. |
| Laser | 0.04/ms | 1 ms | 40 | 2.5 s | 32 m |
| Shotgun | 20×1.7 (51) | 0.75 s | 45.3 | 1.5 s at ≤4 m | 6 m: 30 dmg; 10 m: 18; 15 m: 9.6. Spread is client-steerable (NET-9). |
| Sticky | 29.75, 3.4 m | 0.75 s | 39.7 | 2.25 s (4) | 4 stacked = 119 instantly |
| Crossbow | 50 (75) | 2.0 s | 25 | 2.0 s (2) | Headshot + any 25 kills |
| Bubble | 10, 1.8 m | 0.2 s | **50** | 1.8 s (10) | Top DPS. It's in the vanilla ranked mask (`0x1FF` includes bit 8), although the comment lists ids 0-7. |

Other weapon notes:
- **The TTK band is flat**, 1.5-3.0 s for every weapon, and scoping the rifle costs 25% fire rate for nothing.
- **Stale comment:** "no single shot over 50" ([gamedefs.h:126](../src/game/gamedefs.h#L126)) is contradicted by the crossbow headshot (75) and the bubble alt fire (150).
- **The hitbox doesn't match the models.** It's axis-aligned and doesn't rotate, and the head zone is the full-width top 20% slab, so shoulders count as headshots. The Heavy model's shoulders exceed the hitbox.
- **Knife swing accuracy is wrong:** each swing adds up to 120 to `shotsFired` at 1000 Hz.

### 5.5 Bots, feedback, onboarding

- **Bots have perfect information:** a 360° target scan with exact positions, leads computed from the exact velocity, no FOV, no hearing, and no memory. A target is forgotten the instant line of sight breaks ([bots.cpp:291-313](../src/game/bots.cpp#L291)).
- **Bot difficulty depends on tick rate.** The expert dash roll fires within about 0.1 s at 1000 Hz but about 0.8 s at 125 Hz.
- **Feedback gaps:**
  - No headshot hit cue.
  - The damage flash isn't directional.
  - **Remote players are silent in the air**: no jump, land or dash sounds. Since holding jump is optimal, enemies move almost silently.
  - Hit sounds arrive an RTT plus up to 50 ms of batching late.
- **The FFA scoreboard is sorted by a different number than the one it shows** ([game.cpp:2829-2880](../src/client/game.cpp#L2829)).
- **Doc and UI drift:** the README still lists crouch and "1/2/3 weapons"; the site and menus say ranked uses default controls, but your own binds are used; custom modes still expose `crouch_speed` although crouch was removed.

### 5.6 Architecture for designers

- **Adding a weapon touches about 10 places:**
  - the def table;
  - the refire, alt-fire and head-damage helpers;
  - `fireWeapon` special cases;
  - the gravity switch;
  - client tracer code that duplicates the spread logic;
  - five bot preference tables;
  - the sound enum order;
  - the slots;
  - two bit masks.
- **Adding a mode touches about 200 sites:** 129 `MODE_*` references server-side and 72 in the client, plus 49 predicate calls. A `GameMode` interface (tick, onDeath, onLeave, respawn, canDamage, spawnType, overtime, hud, botObjective) would have prevented the GAME-1/3/4 class of bugs.
- **Tables are duplicated** across C++, JS and Python: mode names, ladders, queue sizes, the packet layout.
- **Tuning workflow:** custom modes are multipliers only, base values are `constexpr`, and there is no hot reload. Movement flip-flopped 8 → 12 → 8 m/s across 1.14.x.
- **Proposal:** absolute-value tuning files sent like `EV_RULES`, a `reload_tuning` command, and an automated TTK and movement-envelope report in `quark_tests`.

---

## 6. Maps and the content pipeline

### 6.1 Defects

| ID | Sev | Ver | Finding | Fix |
|---|---|---|---|---|
| MAP-1 | **High** | R ✓ | **Invisible clip blocks the baked sun on 18 of 21 maps.** `raycastAny`, used for sun and bounce rays, doesn't skip material 9, unlike `raycast` and `raycastBullet` ([map.cpp:678-688](../src/game/map.cpp#L678) vs [:635](../src/game/map.cpp#L635), [:662](../src/game/map.cpp#L662)). `clip_walls` stacks 80 m of clip on every outer wall. Share of floor that only clip shadows vs. share that ends up sunlit: Anvil 61% / 1%, Dojo 85% / 11%, Summit 62% / 9%, Canal 69% / 8%, Depot 63% / 12%, Railyard 61% / 10%, Viaduct 24% / 0%. `sunVisibility` also darkens players and the cart. Server visibility culling and bot line of sight treat clip "glass" (Hightown and Orbital windows) as opaque too. Nobody noticed because 18 of 20 shot sheets predate clip, `mapReport` deletes clip before measuring, and `testGridQueries` only uses Bastion, which has no clip. | Skip material 9 in `raycastAny`, or add query masks. Bump `kBakeVersion`. Test on a map with clip. Regenerate the shot sheets. |
| MAP-2 | **High** | R | **FFA spawn pools are far too small.** FFA uses only `any` spawns ([server.cpp:769-771](../src/game/server.cpp#L769)). Eight maps that offer FFA have just 2: Anvil, Canal, Clockwork, Depot, Furnace, Railyard, Summit, Temple. Dojo, Viaduct, Midtown and Lodestone have none and fall back to all spawns, and on Lodestone the `race` and `att1` spawns are the same point. **Anvil hosts the ranked 4-player instagib FFA on 2 spawn points**, and `boxSolid` ignores players, so they can spawn inside each other. `mapreport` counts all spawns instead: Harbor reports 12 and uses 10; Anvil reports 6 and uses 2. | The generator emits enough `any` spawns for every offered mode. Test that the pool `spawnPlayer` actually draws from is ≥ max players with ≥ 1.5 m spacing. |
| MAP-3 | **High** | R | **The reachability test proves nothing.** `NavGraph::build` links brush tops within a 2.6 m gap and 1.15 m rise with no collision check, so a room's floor and the outside are the same node ([bots.cpp:174-185](../src/game/bots.cpp#L174)). A spawn sealed inside a box is reported reachable. VS, duel, 1v1, FFA and tourney maps have no reachability goals at all, and `mapreport` scores unreachable spawns as perfectly fair ([mapreport.cpp:743-745](../tests/mapreport.cpp#L743)). With walls respected, all 21 maps happen to be connected today; the safety net is what's broken. | A collision-checked walk grid (the reviewer's harness: 0.5 m grid, real `playerBox`, step-up, jumps, ladders). Unreachable scores 0. |
| MAP-4 | Med-High | R | **The grading metrics are saturated, gameable or wrong.**<br>• *Loops* counts nav-node fragments, not routes: Dojo, a flat platform, has 112 loops, and 19 of 20 maps get maximum loop points.<br>• *Vertical* gives Dojo 10.0 with a 0.0 m height range.<br>• *Balance* is 10 by construction for any mirrored map and is never graded for 9 maps (bomb, payload, multi point, tourney, FFA).<br>• A race leg the graph can't reach scores 10 ("shortcut" ratio 1, [mapreport.cpp:575](../tests/mapreport.cpp#L575)). Lodestone's movement-tech 10.0 comes from 6 legs that all read "−1 m".<br>• KOTH "ways onto the hill" counts nav links: Downtown gets 110.<br>• *Bomb lead* can compare different sites per team.<br>• *Materials* clamps trim into the lava slot ([mapreport.cpp:326](../tests/mapreport.cpp#L326)).<br>• *Decor* counts buried seams.<br>• "Lethal drops" comes from a bare `void` line, and "water/lava" from a fountain.<br>• **Exemptions and loose gates:** Dojo is exempt *by name* in test code (layout 3.8, movement 6.8, creativity 4.5). Temple passes duel mode fit with 0 size points (7,017 m² against a 300-2000 band). | Portal-graph loops; height range for vertical; per-team time to *each* objective for balance; unreachable scores 0; grade the worst objective; exemptions as map data, not names. |
| MAP-5 | Medium | R | **The payload cart drives through solid geometry.** On Railyard, a box sits on the track at 102-106 m ([make_maps.py:913](../tools/make_maps.py#L913)). On Lodestone, a 32 m pillar and a dais are on the track at 117.5 m and 127.5 m. Nothing tests this. The cause is that the generator's `keepouts()` only guards decor and clutter, never hand-placed boxes. | Sweep the cart box in a test; a global keep-clear registry in the generator. |
| MAP-6 | Medium | R | **Lodestone offers every mode but is graded only for points.** Bomb site D: fastest attacker 16 m, fastest defender 102 m, so attackers arrive 10.8 s first and every round is an uncontested plant. KOTH distances are 64 vs 70 m. | Grade every offered mode, or offer fewer. |
| MAP-7 | Medium | T | **Colosseum's tourney never uses its a/b spawns.** The spawn type depends on `supportsMode(MODE_VS)` ([gamemodes.cpp:997](../src/game/gamemodes.cpp#L997)), and Colosseum's `modes` line excludes vs. Race spawns are gated the same way. The mode-fit metric measures spawns the game never uses. | Use `canSupportMode`. |
| MAP-8 | Medium | R | **Midtown's point timing is outside its own band.** At point D defenders lead by 12.7 s, at E by 0.9 s; the band is 2-10 s. It passes because the score averages points. | Score the worst point; re-stage spawns. |
| MAP-9 | Medium | R | **Two authoring paths overwrite each other** (v1 G1, worse than stated). The editor and the server autosave write the same `.qmap` the generator owns. The editor can't edit trim (32-3,656 per map) or clip, and `b set` on generated brushes duplicates them (COR-4). | Generator output as a build artifact; editor work as a per-map overlay of edit ops. |
| MAP-10 | Medium | R | **Round-trip drops metadata kept in comments.** Geometry round-trips bit-identically, but every comment is lost: `# stairs` goes from 6-42 per map to 0, and `# setting` from 1 to 0. `mapreport` then *hard-fails* the map ("no # setting", [mapreport.cpp:962](../tests/mapreport.cpp#L962)), so one editor-saved map turns `quark_tests` red, while stair coverage drops silently. | Real `stairs` / `setting` keywords; the engine already has `group stairs`. |
| MAP-11 | Medium | R | **The format has no version and coerces inconsistently.** An unknown keyword is an error. An unknown name in `modes` rejects the whole map, so old clients can't join a map listing a future mode. But an unknown `for` silently becomes −1, an unknown spawn team silently becomes `any`, and a second `hill` silently replaces the first. | A `version N` header; skip unknown optional keys with a warning; strict errors for gameplay keys. |
| MAP-12 | Medium | R | **Each edit op is O(map size) on the tick:** a full `serialize()` plus grid rebuild, and Midtown's text is 350 KB ([server.cpp:1889-1891](../src/game/server.cpp#L1889)). | Incremental grid updates; serialize on save only. |
| MAP-13 | Low | T | **Identity and storage** (v1 G4/G5, corrected). Name + CRC32 identity. Maps stored as files, as embedded hex and in the qpak. `resetOfficialMaps()` only runs from an editor menu; it's the launcher's extraction that overwrites official names. | sha256 ids; read-only shipped content plus a user directory. |
| MAP-14 | Low | R | **Generator structure** (v1 G6, corrected). Regeneration is byte-reproducible: 21/21 identical in about 1 s, so hashes only churn when shared prefab code changes. The real coupling: Midtown uses prefabs defined in Lodestone's section that capture the global `m` and `POINTS`; there is no `--map` and no `__main__` guard; keep-clear only covers decor (MAP-5). | `prefabs.py` plus one module per map, `--map`, no global `m`, a keep-clear registry. |
| MAP-15 | Low | R | **Brush budget:** Midtown 9,191 brushes / 360 KB, Lodestone 8,102 / 337 KB. About 40% are non-solid trim. | A brush and face budget in the metrics; merge coplanar trim. |
| MAP-16 | Low | R | **Spawn nits:**<br>• Spawns face a surface ≤ 1.1 m away: Foundry, Harbor, Orbital, and Summit (0.5 m).<br>• Pairs 1 m apart: Harbor `any`/`b`, Colosseum `a`/`race`.<br>• Enemy team spawns in mutual line of sight: Downtown 6/9 pairs, Depot 10/25, Canal 4/16. | Spawn sanity checks in tests. |
| MAP-17 | Low | R | **The ranked symmetry test only compares spawn counts.** All 5 ranked maps are mirrored today, but nothing checks positions, yaw, ladders or lights. The sun isn't point-symmetric, so baked shading differs per side. | Test full mirroring; a symmetric or overhead sun for ranked maps. |
| MAP-18 | Low | R | **Docs drift:**<br>• MAP_STRUCTURES calls Gantry "ranked 1v1", but it's FFA.<br>• Temple is listed at 5,700 m² but measures 7,017.<br>• "Neither side of Lodestone is closer" is contradicted by site D (16 vs 102 m).<br>• The 1.14.0 changelog's run speed 12 was reverted.<br>• `MAP_REPORT.md` is stale: no Hightown row, and it links missing `dojo.jpg` and `midtown.jpg`.<br>• Yaw is rounded on save, Hightown has no palette, and the group import caps material at ≤ 5 while `parse` allows ≤ 7. | Regenerate the report and shots in CI. |

### 6.2 Per-map notes

| Map | Built for | Biggest issue | Improvement |
|---|---|---|---|
| anvil | 1v1 (all 3 ladders) | 2 `any` spawns for the ranked 4-player instagib FFA; 1% of floor sunlit | 8-12 mirrored `any` spawns, or drop instagib |
| bastion | vs | 7,714 m² against a 700-3,000 band; FFA spawns 2 m apart | Shrink the outer ring |
| canal | npayload | 13% cover beside the track (35% target) | Barges and crates along the quays |
| clockwork | duel | Fine for duel; FFA pool of 2 | +4 `any` spawns |
| colosseum | tourney | a/b spawns unused (MAP-7); 5,261 m² | Fix spawn gating; wall off the outer ring |
| depot | bomb | Defenders arrive 6.0 s first (good); 10/25 enemy spawn pairs in line of sight | A sight blocker mid-yard |
| dojo | 1v1 vampire | Name-based test exemption; FFA offered with 2 spawns; 85% clip shadow | Don't offer FFA; exemption as data |
| downtown | koth | Enemy spawns in line of sight; 13% cover on the hill | Hill cover; break the spawn sightline |
| foundry | tag | 4% hiding spots | More closets; turn the spawns |
| furnace | 1v1 vampire | Clean; FFA pool of 2 | +`any` spawns |
| gantry | ffa (ranked instagib) | Healthy; docs stale | Fix the docs |
| hamlet | hide | 17,040 m² | Acceptable for hide |
| harbor | ffa | 10 `any` used (12 reported); a spawn pair 1 m apart | More spawns; move the stray pair |
| hightown | hide | Missing from the report and shots; no palette | Regenerate; add a palette |
| lodestone | points + all modes | Bomb site D −10.8 s; cart through a pillar; duplicate spawn; bogus tech score | Fix site D, the track and the grading |
| midtown | points | D +12.7 s, E +0.9 s; can't be joined at all (PERF-2) | Re-stage spawns; fix the prepass |
| orbital | vs | `any` spawns face the hull 1 m away | Rotate them |
| railyard | payload | A box on the track at 102-106 m | Move it |
| summit | koth | Spawns face a rock 0.5 m away | Shift 1.5 m |
| temple | 1v1 vanilla | 7,017 m² (3.5× the band); 7.9 s between spawns | Undo the 1.15 scale-up |
| viaduct | race | FFA offered with 3 spawns all in mutual line of sight; 0% of floor sunlit | Don't offer FFA |

### 6.3 Recommended map architecture

1. **Format v2.**
   - A `version` header; unknown optional keys skipped with a warning.
   - Generic `entity <class> k=v…` markers.
   - Real `setting` and `stairs` keywords; grading exemptions declared in the map.
   - *One* validator shared by parse, edit and download, checking bounds after group expansion, with ≤ 60k total brushes (or `uint32` cell indices) and capped counts.
   - Reset the whole `Map` on parse.
2. **One source of truth per map.**
   - Generator output is a build artifact.
   - Editor work is a per-map overlay of edit ops.
   - Shipped maps are read-only. User maps, downloads and caches live in a user directory keyed by sha256.
3. **Generator.**
   - `prefabs.py` plus one module per map, with `--map`; no global `m`.
   - A keep-clear registry (spawns, objectives, the swept cart path, stair approaches) enforced on every placement.
   - Spawn pools sized for every offered mode.
   - A brush budget, with coplanar trim merged.
4. **Validation in CI on Linux and Windows.**
   - Collision-checked reachability for every offered mode.
   - Spawn sanity: floor, lava, spacing, facing, pool size.
   - A cart sweep against static geometry.
   - Clip occlusion tests.
   - Mode fit graded per offered mode on the worst objective; unreachable scores 0.
   - Balance from per-team time to objectives; loops from a portal graph.
   - `MAP_REPORT` and the shot sheets regenerated as CI artifacts.

---

## 7. Engineering practices

### 7.1 Process
- **There is no gate between a push and every player's machine.** No branch protection, no required review, any-branch releases (SUP-2), unsigned updates (SUP-1). All 160 commits share one author identity (`Claude <noreply@anthropic.com>`), and `worktree-agent-*` branches are merged straight in. So nobody but the author has ever reviewed this code; this review is the first second pair of eyes.
- **There is no `LICENSE` file**, so contributors have no stated terms. Assets (GLFW, Rubik OFL, the SFX) are credited.
- **Docs have drifted from reality:** CLAUDE.md says every push builds macOS, which was false from 1.14.2 to 1.15.1 (SUP-8); see also MAP-18 and §5.5.

### 7.2 Code
- **God object.** `Game` declares about 138 methods and 200 data members across `game.cpp` (4.4k lines), `editor.cpp` and `community.cpp`. `frame()` alone does polling, screen transitions, input, prediction, effects, spectating and rendering. `Server` exposes nearly all of its state as public members.
- **Suggested split:** Session (net, prediction, events) / WorldView / Hud / Menus / Editor / Community, with `frame()` as a short pipeline over them.
- **Comment rot.** Many comments sit on the wrong field: [game.h:39](../src/client/game.h#L39), [:47](../src/client/game.h#L47), [:347](../src/client/game.h#L347), [:359-361](../src/client/game.h#L359), [server.h](../src/game/server.h) (`acc`, `fireLocked`, `model`). [netclient.h:70-74](../src/game/netclient.h#L70) has three conflicting `interpDelay` comments. There are orphaned comments at [game.cpp:2219-2222](../src/client/game.cpp#L2219), and comments contradicting code in pmove (dash and hook) and gamedefs (the 50-damage rule). These make review much harder.
- **Duplication:**
  - Two frustum extractions, one of them buggy (PERF-7).
  - `readTextFile` and `readFile`.
  - Two mtime-sorted directory listers.
  - `findDataPath("maps") + "/../X"` rebuilt in 8+ helpers.
  - Settings loaded twice at startup.
  - Constants duplicated across C++, JS and Python (§5.6).
- **Error handling is inconsistent.** It's mostly bool returns, but exceptions from `stoi`, `bad_alloc` and `std::filesystem` throwing overloads escape to `terminate` (COR-1, CLI-3, SUP-5).
- **Shared state between simulations:** the process-global rules (COR-6).

### 7.3 Build, tests, sanitizers, fuzzing

**Toolchains used.**
- MSVC 19.51 (VS 2026 Build Tools), which ships ASan and libFuzzer but no UBSan.
- MinGW-w64 GCC 13.1, the same family as the release cross-compiler.
- CMake 4.3, Ninja.
- No clang, cppcheck or clang-tidy was available.

**Build results.**

| Build | Result |
|---|---|
| MSVC, default configure | **Fails.** MSVC's platform defaults set `CMAKE_BUILD_TYPE=Debug`, so the "default to Release" guard ([CMakeLists.txt:13](../CMakeLists.txt#L13)) never fires, and the unconditional `/O2` ([:22](../CMakeLists.txt#L22)) clashes with Debug's `/RTC1`. |
| MSVC Release | **Fails:** `windows.h` `min`/`max` (C2589 in `audio.cpp`, launcher); a WIN32 launcher with `int main` (LNK2019 `WinMain`); `__builtin_popcount` in the tests. |
| MSVC with workarounds (`NOMINMAX`, `/ENTRY:mainCRTStartup`, a popcount shim) | OK, 4 exes, about 12 s |
| MinGW GCC 13, client on; extended warnings; trap-UBSan | OK, about 15 s |

So **the MSVC branch, including `/W3 /O2 /fp:fast`, is dead code that doesn't compile.** Its `/W3` also silently overrides any `/W4` passed in `CMAKE_CXX_FLAGS` (D9025 ×87).

**Warnings.**
- Baseline: 3 on GCC, 14 on MSVC.
- With `-Wextra -Wshadow -Wconversion …`: 336, mostly −Wconversion int→float (306).
- MSVC `/W4`: 34.
- Worth fixing: `GameEvent r` shadows the packet `Reader r` inside `handleConnect` ([server.cpp:344](../src/game/server.cpp#L344)), a trap waiting in a packet handler; 15 other shadowings; 3 non-literal format strings. **No warning pointed at a real memory bug.** Every serious bug came from sanitizers, fuzzing or targeted inputs.

**Tests on Windows**, run from a scratch working directory:

| Run | Result |
|---|---|
| Stock MinGW | **Timed out** at the 25-min watchdog in group 4 of 16, after **26 FAILs** (bots, botaim at 0.0 dps, tourney, duel). Root cause: MinGW `sleep_for(1ms)` takes 15 ms (PERF-14), so each `pump()` iteration jumps about 60 ms of game time at the 4× test time scale. |
| Stock MSVC | **Crashes at startup** (0xC0000409): `setvbuf(stdout, nullptr, _IOLBF, 0)` ([tests.cpp:2376](../tests/tests.cpp#L2376), [:2458](../tests/tests.cpp#L2458)) is an invalid-parameter fast-fail on the UCRT. |
| MinGW with `sleepMs` → `Sleep()` | 254 s. Run 1: 2 FAILs (tests.cpp:1814, :1818). Run 2: **all pass**. |
| Trap-UBSan + sleep fix | **No UB trap in the whole suite**; 1 FAIL (:1814) |
| MSVC ASan + fixes | First run: `stack-overflow` entering `testBots`. With a 16 MB stack: **no ASan reports in the whole suite**; 1 FAIL (:1814) |

**Flakiness and isolation.**
- tests.cpp:1814 (a ranked vote, a map switch, then a fixed 0.5 s pump) failed in 3 of 4 fixed runs, and :1818 in 1 of 4.
- The suite is wall-clock driven throughout.
- Ports are fixed (28520-29997); holding UDP 29950 makes `quark_tests rate` fail to bind.
- **Running the suite writes into the source tree:** `docs/map_shots/*.cams` relative to the working directory ([mapreport.cpp:386](../tests/mapreport.cpp#L386)), so tracked files when run from the repo root as CLAUDE.md says; `maps/temple.qmap` next to the exe; `zz_test.qdemo`; `zz_update*.pending`.
- The committed `docs/MAP_REPORT.md` is stale: 50 lines differ, there's no Hightown row, and Orbital shows 539 brushes against 741 now.
- `sizeof(Server)` is 684 KB, from 32 × 21 KB of lag-comp history. It lives on the stack in `quark_server`'s `main()` ([server/main.cpp:100](../src/server/main.cpp#L100)) and in the tests, which overflows MSVC's default 1 MB stack. The client already heap-allocates it.

**Fuzzing.** MSVC ASan + libFuzzer, 6 min per target with `-fork=2`, seeded from all shipped maps, captured real snapshots and demos, and a valid qpak:

| Target | Execs | Result |
|---|---|---|
| Map parse → raycast → serialize → reparse | 267k | 0 fuzzer crashes. Targeted inputs: 240 GB allocation (CLI-3); float→int UB that loads a map with a non-solid floor (CLI-3); a 65,536-brush wrap that drops collision. The 240 GB case was **not** found without a large-number seed. |
| Map edit ops | 83k | 22 crashes, **2 root causes**: unloadable maps that get autosaved (NET-15) |
| Custom rules | 2.3M | NaN plus the all-space name (CLI-6) |
| Snapshot decode/encode | 7.1M | 0: `Reader` is fully bounded. But 994 inputs decode a player id ≥ 32, which feeds CLI-1. |
| Server packets + string commands | 20k | 0, but 30 exec/s makes this weak evidence. Needs a faster harness. |
| Demo playback (real client path) | 801k | 59 crashes, **1 root cause: the CLI-1 stack overflow** (40 reproducible; the rest depend on `timeNow()`) |
| qpak extractor | 43k | 1,441 crashes + 87 OOMs, **3 root causes** (SUP-5): path escapes, uncaught `u8path` exception, unbounded `resize` |

**Checked and fine.**
- `sha256.h` matches hashlib on 303 vectors, covering every padding boundary, plus the FIPS vectors.
- `buffer.h` uses `memcpy`, so there's no aliasing UB.
- All 27 headers compile standalone.
- GLFW is 3.3.10, the final 3.3, Win7-compatible.
- No Win8+-only APIs were found.
- Every one of the 104 `VERSION` bumps has its changelog.

**Cross-platform determinism.** The real difference isn't `/fp:fast`; it's the math library. pmove's `std::sin`/`std::cos` differ by 1 ulp between MinGW and UCRT on 2,614 of 1M yaw values. Today that only costs extra prediction replays (the client re-predicts on a mismatch, [netclient.cpp:342](../src/game/netclient.cpp#L342)), but a table-driven or fixed-point trig in `pmove` would make client and server bit-identical.

**Zero test coverage:**
- The launcher (`extractPak`, `versionCmp`, `jsonString`, `install.cpp`).
- All of `src/client`, because the tests link only `quark_core`: lightmap, config, binds, editor, renderer, `fromHex`.
- `http.cpp`, `master.cpp`, `upnp.cpp`, `NetAddr::parse`.
- Custom-mode load and save.
- No snapshot codec round-trip test and no malformed-packet test.
- There are a few edit-op negatives ([tests.cpp:949](../tests/tests.cpp#L949)).

**Build and repo hygiene.**
- `QUARK_VERSION` is a global define, so all files recompile on every one of the 104 version bumps.
- Embedded maps are a 6.6 MB generated hex header built by a CMake regex.
- The map `GLOB` has no `CONFIGURE_DEPENDS`.
- No LTO or IPO, despite performance being the top priority.
- `-O2` overrides `-O3`.
- No CTest (`enable_testing`/`add_test`) and no `install()` rules.
- **Generated headers are committed:** `src/client/voice_recorded.h` is 20.7 MB and `sfx_recorded.h` several MB per version; `assets/` holds 36 MB of WAVs; `.git` is 59 MB after 160 commits.
- **Commits mix several changes:** median subject 113 characters (max 406), 12.4 files and about 1,000 inserted lines on average, 104 releases in 3 days.

| ID | Sev | Finding | Fix |
|---|---|---|---|
| ENG-1 | **High** | The test suite can't pass on Windows, the main player platform, and CI never runs it there | Sleep fix (PERF-14); `_IONBF`; heap-allocate `Server`; replace `__builtin_popcount` with `std::bitset`; a Windows CI job that runs the tests |
| ENG-2 | High | No exception boundary: **zero `catch` blocks in `src/`**, and `bad_alloc`, `filesystem_error` and `length_error` from untrusted input were all demonstrated | Validate before allocating; catch at the load and parse boundaries (map, demo, pak, Worker replies) |
| ENG-3 | Medium | No sanitizer or fuzz CI, though MSVC already ships ASan and libFuzzer, and the harnesses from this review exist | Commit the 7 harnesses and the repro files as regression tests; nightly 10-minute fuzz runs |
| ENG-4 | Medium | The MSVC CMake branch is dead and broken (Debug vs `/O2`, `min`/`max`, `WinMain`, popcount, `setvbuf`) | Delete it, or fix it and add an MSVC CI job; global `NOMINMAX`; pick the build type correctly for multi-config generators |
| ENG-5 | Medium | Tests aren't hermetic: they write into the source tree, use fixed ports and run on wall-clock timing (1 flaky check in 4 runs) | Temp directories; ephemeral ports; fixed-step simulated time; CI regenerates the map report and fails on a diff |
| ENG-6 | Low | Build hygiene (version define, hex header, no LTO, no CTest), 684 KB `Server` on the stack, committed generated headers | See above; Git LFS for audio; generate headers at build time |

---

## 8. Status of the v1 findings

| v1 | Status | Now |
|---|---|---|
| N1 | Confirmed, worse (snapshots stream to addresses that never sent input) | NET-1 |
| N2 | Confirmed | NET-2 |
| N3 | Confirmed | NET-4 |
| N4 | Confirmed, expanded (knife rewind, no interpolation, linear lookup cost) | NET-5 |
| N5 | Confirmed, broader (`callvote map`/`mode` too) | NET-6 |
| N6 | Confirmed | NET-8 |
| N7 | Confirmed, plus no cooldown | NET-7 |
| N8 | Confirmed | NET-12 |
| N9 | Confirmed, understated (HP leak in ranked, earshot, client-only seeker blackout, dead ghosting) | NET-10 |
| N10 | Confirmed (gaps undetectable; `EV_RANKED`/`EV_RULES` loss) | NET-11 |
| C1 | Confirmed, plus a demo-file route | CLI-2 |
| C2 | Confirmed, plus a server-side bypass through validated edit ops | CLI-3, NET-3 |
| C3 | Much worse (3 quadratic passes before the cache check; Midtown can't be joined) | PERF-2 |
| C4 | Confirmed | CLI-6 |
| C5 | Confirmed | CLI-8 |
| C6 | Confirmed (Low); a separate normal-use crash found | CLI-7, COR-1 |
| R1 | Confirmed and quantified (#1 from 160 requests) | WEB-1 |
| R2 | Confirmed; worse sibling: anyone can void matches | WEB-3, WEB-2 |
| R3 | Confirmed | WEB-4 |
| R4 | Confirmed, plus a nickname fallback | WEB-9 |
| R5 | Confirmed by mock, plus a CSRF route | WEB-5 |
| R6 | Confirmed, raised (quota outage) | WEB-6, WEB-10 |
| R7 | Raised to High (`@everyone`) | BOT-1, BOT-2, BOT-3 |
| R8 | Confirmed, plus `0644` and a non-atomic key write | WEB-13 |
| R9 | Raised to Medium (any push redeploys `master.txt`; check account ownership) | WEB-11 |
| R10 | Confirmed | WEB-12 |
| R11 | Lowered to Info (shipped MinGW GCC 13 `random_device` is fine) | (hygiene) |
| S1 | Confirmed | SUP-1 |
| S2 | Confirmed with history (v0.26.1, v0.26.3 assets from other commits) | SUP-2 |
| S3 | Worse (token persisted in `.git/config` during builds) | SUP-3 |
| S4 | Adjusted: frozen about 4 h (+2.5 h), healed at v1.16.0; still fragile | SUP-8 |
| S5 | Worse (root under sudo; silent stall on a failed merge) | SUP-8 |
| S6 | Confirmed by running the path logic with the release compiler | SUP-5 |
| S7 | Lowered to Low (inputs are local or GitHub-controlled) | SUP-9 |
| S8 | Confirmed; **fix corrected:** "use OS defaults" would break Windows 7, which needs the explicit TLS 1.2 flag | SUP-10 |
| S9 | Confirmed (more data dirs; non-atomic key) | SUP-11 |
| G1 | Confirmed, worse (trim and clip uneditable; undo loses brushes) | MAP-9, COR-4 |
| G2 | Adjusted (geometry round-trips; lost comments break tests) | MAP-10 |
| G3 | Confirmed, plus inconsistent silent coercions | MAP-11 |
| G4/G5 | Confirmed; `resetOfficialMaps` is editor-only, the launcher overwrites | MAP-13 |
| G6 | **Partly refuted:** regeneration is byte-reproducible; hashes don't churn unless shared prefabs change | MAP-14 |
| G7 | Confirmed (40% non-solid trim) | MAP-15 |
| G8 | Confirmed | §6.3 |
| §3 threading | **Corrected:** the writer is `EV_RULES` on the main thread; start/stop don't race; `rand()` isn't UB | COR-6 |
| §3 `/fp:fast` | **Refuted for shipped builds** (all MinGW), and the MSVC branch doesn't compile. The real cross-platform difference is libm: 1-ulp `sin`/`cos` differences on 0.26% of yaw values | COR-5, ENG-4, §7.3 |
| §3 tests Linux-only | **Worse:** the suite can't pass on Windows at all (15 ms sleep; MSVC `setvbuf` crash) | ENG-1 |
| §3 no fuzzing | Confirmed. Fuzzing found CLI-1, NET-15 and SUP-5 within minutes; C2's huge allocation needed a seed | ENG-3 |
| §3 fixed ports | Range corrected (28520-29997); collision demonstrated | ENG-5 |
| §3 unhandled exceptions | **Worse:** zero `catch` blocks in `src/` | ENG-2 |
| §3 god object | Confirmed (about 138 methods, 200 members) | §7.2 |
| §4.2 counts | Corrected (129 server-side + 72 client `MODE_*` refs + 49 predicates) | §5.6 |
| §4.2 "netcode is solid" | **Partly refuted** (input backlog, no projectile antilag, short ranked interp, mid-match rate changes) | PERF-1, GAME-11 |

---

## 9. Roadmap

**This week: correctness hotfixes.**
- COR-1, MAP-1, PERF-4, PERF-14, PERF-1, GAME-1 to GAME-4, CLI-1, CLI-4, CLI-2, NET-15, BOT-1, COR-3.
- Each is small. Together they remove a client crash, two latency bugs, a frame-cap stutter, the broken lighting, map-file corruption in edit sessions, and four mode-deciding rule bugs.
- Then make the suite pass on Windows (ENG-1), so the fixes are tested on the platform players use.

**Next two weeks: trust boundaries.**
- Challenge-cookie handshake (NET-1, NET-2, NET-4).
- One map validator (CLI-3, NET-3).
- Lag-comp cap and server-seeded spread (NET-5, NET-9).
- Visibility culling of HP and earshot (NET-10).
- Release pipeline hardening (SUP-2, SUP-3, SUP-4).
- Signed qpaks and an atomic install (SUP-1, SUP-5, SUP-6).
- Worker rate limits, caching and atomic writes (WEB-5 to WEB-8).

**This quarter: structural.**
- Worker-issued match tickets (WEB-1 to WEB-4).
- Lightmap prepass rewrite and async load (PERF-2, PERF-3).
- Rules out of globals (COR-6).
- A `GameMode` interface (§5.6).
- Map format v2, a generator split and collision-checked validation (§6.3).
- Sanitizer and fuzz CI from this review's harnesses (ENG-3), and an exception boundary at every parser (ENG-2).
- Integer-ms game time on client and server (PERF-5).
- User data out of the install directory (SUP-11).
- Bots with FOV, hearing and memory, and nav built off the tick thread (§5.5, PERF-10).
