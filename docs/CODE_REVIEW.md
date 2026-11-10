# Quark code review

**Date:** 2026-11-10
**Commit reviewed:** `1e7d2bd` on `testing` (VERSION 1.19.8, protocol 71). Commits after it, up to `2e2a2de`, were
checked for anything they already fix or change. The Status column says so.

**How it was made.** There were three stages, all run by Claude:
1. **Research (5 researchers).** Performance and latency; memory safety and undefined behaviour; netcode and network
   security; architecture and code quality; testing, tooling and build.
2. **Review (10 developer reviewers, one per role).** Graphics engine, netcode, security, performance, gameplay, UI and
   tools, build and release, QA and testing, C++ language, platform and systems. They wrote 143 findings.
3. **Leads (5).** Technical director, netcode and security lead, architecture lead, release and QA manager, and a
   skeptic who re-checked every serious claim. They checked the findings against the code, merged duplicates, set the
   final severities and added 32 findings of their own.

When the leads disagreed, this document picks the view with the better evidence. The skeptic and the lead who owns that
area weigh most. Every disagreement is noted where it matters.

**Rules this review keeps (q's).**
- Everything is client-side and predicted, damage and explosions included. Only deaths are decided by the server. Nothing
  here asks for server-decided damage. Flags and logs that check plausibility are fine.
- Performance and latency come first, judged by the 1% and 0.1% lows. Every fix states its frame or tick cost.
- Weapon and movement numbers are q's placeholders. There is no crouch. Maps are out of scope.

**Severity.**
- **S1:** players can be hurt now, on purpose, by anyone.
- **S2:** a real problem players will hit, or a serious risk to releases or ranked.
- **S3:** real, but narrow, or it needs unusual conditions.
- **S4:** small, hygiene, or a cleanup for later.

**Effort** is in Claude working minutes and includes tests. Owner steps (GitHub settings, recording demos) are listed
separately.

**Words used once and explained here.**
- **Tick:** one server step, 1 ms at 1000 Hz.
- **Snapshot:** the server's update of the world sent to a client.
- **Reliable event:** a message the client must acknowledge (an "ack"). The server resends it until the ack arrives.
- **Prediction / reconcile:** the client runs your movement and shots at once. When the server's snapshot arrives, it
  checks its guess and replays your recent commands if the guess was wrong (a "misprediction").
- **Governor:** the server thread that steps the tick and snapshot rates down when the box is overloaded.
- **ABAB:** benchmark build A, then B, then A, then B, so that drift on the machine hits both builds alike.

---

## Where the code stands

Quark is fast and well built for what it does. The frame loop, the frame cap's learnt timer, the raw-mouse thread, the
layout engine, the delta-compressed snapshots and the signed update chain are all better than most commercial engines,
and the reviewers asked that none of them be undone. The serious problems sit in four other places:
- **Trust boundaries on the server.** One modified client can get everyone else disconnected and, in ranked, be handed
  the win. One signing key guards both unreviewed testing builds and production. The production server trusts things
  the testing server and clients tell it.
- **Rules written twice.** The client's fire loop is a hand copy of the server's. That copy has already drifted in four
  ways, the worst being that the laser loses damage whenever a server runs below 1000 Hz.
- **Measurement.** The benchmark cannot see most of the spikes argued about here: its only demo runs no destruction, no
  knockback and no big fights. Capped runs measure pacing, not work, and two runs of the same build differ by about 17% at
  the 0.1% low.
- **Per-player work redone every frame,** which grows with players in view. The low-poly player models now on testing
  made the biggest item 2.3 times larger.

None of this needs a rewrite. The fix plan below is five pushes. Measurement goes first, so that every later change can
be shown to help q's lows rather than argued about.

---

## Top 15

Ordered by harm to players, then by how cheap the fix is.

1. **03-01 (S1) One player can get everyone disconnected and win ranked matches.** A modified client can make the
   server queue reliable events for other players faster than they can acknowledge them. The server then drops them,
   and in ranked a drop counts as leaving, so the attacker is named the winner. **Fix:** a per-player budget on the
   commands that broadcast to everyone, one immediate head-hit event per attacker and victim per batch window, a limit on
   session changes per address, and a two-client net test. **Cost:** one compare per command, about 0 per tick.
   **Effort:** 70-90 min. Full details in the private notes.
2. **03-03 / 07-01 (S2; S1 if the key is a repository secret) One signing key for everything.** The key that signs
   production releases, testing builds, launchers and the ranked server-list pointer is available to the testing
   pipeline, which runs commits nobody reviews. One bad or compromised testing commit could ship a signed build to every
   production launcher. **Fix:** a separate testing key; the production key only in the `production` environment with q
   as the required reviewer; the launcher checks the tag pattern per channel; the server-list pointer signed by an
   offline key. Two phases: launchers learn the new key first, then the key moves. **Cost:** 0 at run time.
   **Effort:** 90-120 min plus about 10 min of q's GitHub settings. Full details in the private notes.
3. **L2-M4 / 07-06 / L2-M3 (S2) The production server trusts what testing and clients report.** The production
   governor believes the testing server's overload report. A testing build with a slow server (no malice needed) can
   therefore hold production at its lowest tick rate. The testing server can also write files the production updater
   trusts. **Fix:** count testing's overload only when production itself shows contention, never step production down
   mid-ranked-match for testing, count network votes per host, and give the updaters a cache only they can write.
   **Cost:** 0 (the governor runs once a second). **Effort:** 40 min. Full details in the private notes.
4. **02-02 / 05-01 / 02-12 + L3-M1, M2, M3 (S2) The fire rules are written twice, and the copies disagree.** The
   client fires at most one shot per command, the server one per tick. Below 1000 Hz (when the governor steps down) the
   laser does 1/N of its damage: half at 500 Hz, an eighth at 125 Hz. The same hand copy lets a tagged player tag back
   during the freeze, lets a bomb planter shoot, and mispredicts the first shot after holdfire. **Fix:** one shared
   `FirePredictor` (in `src/game/firerules.h`) and one `traceShot`, used by server and client. **Cost:** 0 at 1000 Hz;
   the same work as today below it. **Effort:** 40 min for the laser alone, then 90 min for the shared rules and 90 min
   for the tests. Needs one protocol bump.
5. **02-01 / 02-03 / L5-M3 / L2-M5 (S2) Overflowing snapshots invent explosions.** When a snapshot is too big, the
   server cuts the projectile list from the end. The client reads the missing rockets as explosions, the owner reports
   hits that never happened, and the real death records are dropped first. **Fix:** cut by priority (deaths last), mark
   cut entries as hidden instead of gone, and reject a snapshot with trailing bytes. **Cost:** about 50 ns less per
   client per snapshot. **Effort:** 65-75 min.
6. **L2-M1 / 03-04 / 03-09 / 03-02 / 10-02 (S2) Unbounded work on the 1 ms tick thread.** Connect signature checks,
   ranked ticket checks and vote map lookups can each eat whole ticks. Players see late ticks and rubber-band spikes. In
   the worst case new players cannot join. **Fix:** per-address and per-frame signature budgets (or verify on a worker
   thread), a cap on ticket attempts, and vote lookups through the map cache. **Cost:** negative; it removes stalls.
   **Effort:** 60-90 min. Full details in the private notes.
7. **07-03 / 07-05 / 07-07 (S2) Production servers compile their own updates, live.** Every testing push makes the
   production box build a new server binary next to the running game. There is no smoke test and no rollback, and the
   build ignores the host CPU. **Fix:** install CI's prebuilt, signed (PGO) server binary, run a self-test, and keep the
   previous binary for rollback. **Cost:** about 4% less tick time on the box during builds, and no compiles at all.
   **Effort:** 60 + 40 min.
8. **01-04 (S2) Player meshes are re-built on the CPU every frame.** Every player's mesh is transformed vertex by
   vertex on the CPU and uploaded each frame, shadows of off-screen players included. The low-poly set on testing raised
   the mean from 1,277 to 2,952 vertices, so this now sets the 1% low in 16-32 player modes. **Fix:** upload each model
   once and pose it on the GPU with per-player data, keeping the CPU path as a fallback. **Cost:** 250-550 µs less per
   frame at 32 players on the review VM; about half that on q's desktop. **Effort:** 180-240 min.
9. **10-01 + L1-M4 (S2) Latent Sync waits on an untrusted timer.** Latent Sync, the Windows default present mode,
   still waits with a fixed 0.3 ms spin on the OS timer that the frame cap stopped trusting. When the timer is late, the
   flip misses its blank and you see a stutter. **Fix:** reuse the frame cap's learnt lateness (CapWait), and add a LATE
   FLIPS counter to F3 to prove it. **Cost:** 0 frame time; a little more spin CPU. **Effort:** 25 min.
10. **08-05 / L1-M3 / 10-18 / 02-08 (S2, process) We can't measure the costs we argue about.** **Fix:** an uncapped
    benchmark preset, four event demos (destruction, knockback, a 16+ player fight, a ranked round), a
    `tools/bench_compare.py` ABAB comparison, F3 counters for mispredictions (PRED) and late flips, and named threads.
    **Cost:** 0 in play. **Effort:** about 2 h plus q recording the demos.
11. **05-02 (S2) Humans don't get the knife's swing follow-through.** The swing's continuation only runs for bots, so a
    human's swing misses hits a bot's would land. In knife-only modes this decides fights. **Fix:** the client keeps
    tracing the swing arc for up to 120 ms after the press, as bots do, and the server drops one redundant trace.
    **Cost:** a short ray set per swing on the client, one hitscan less on the server. **Effort:** 30 min.
12. **03-08 (S2) Ranked has no plausibility flags.** Damage is the client's call by design, but nothing logs a report
    that can't be true (a weapon the mode doesn't allow, a frozen or rooted shooter, an impossible rate). **Fix:** flag
    and log them with the ranked result. Never refuse or recompute the damage. **Cost:** under 50 ns per report.
    **Effort:** 60 min.
13. **07-02 / L4-M1 (S2; the skeptic says S3) CI doesn't build with q's toolchain, and its install step times out.**
    CI's Windows build uses GCC 13 and the old C runtime from Ubuntu's archive. q builds with GCC 16 and UCRT, and the
    shipped test binary never runs. On 2026-11-10 that archive step hit its time limit several times, so no testing
    prerelease went out. **Fix:** pin CI to one cached toolchain archive, run the movement hash test under Wine, and
    compare the code it generates. **Cost:** 0 in game. **Effort:** 45 min.
14. **01-03 / 01-02 / 04-01 / L1-M2 / 01-08 (S3) Destruction rebuilds instead of patching.** A glass break or a hole
    reads ~50 uniforms back from the GPU, re-meshes every block, may hit a cold shader, copies every brush and rebuilds
    the whole collision grid. That is a hitch on the frame something breaks, mostly on Cobblestone and user maps.
    **Fix:** an early return, warm both world programs, and patch the grid in place. **Cost:** 30-100 µs less on the
    event frame. **Effort:** 15-150 min, smallest first.
15. **08-03 / 02-08 / 02-09 (S3) Mispredictions are invisible, and movement math differs between machines.** The new
    `corrections()` counter on testing isn't on F3 or in a test, and the new 8 m blending hides divergence even better.
    Replays aren't timed. Shared movement calls the platform's `sin`/`cos`/`pow`/`exp`, which can differ between the
    Windows client and the Linux server. **Fix:** F3 PRED counter and replay time, a zero-misprediction test through
    LagRelay, and the deterministic math that already exists in `moveexpr.cpp`. **Cost:** about 20 ns per reconcile;
    the deterministic math adds 20-40 ns per air step. **Effort:** 30-120 min.

---

## Fix plan

Five pushes to `testing`, in this order. Each one is committed locally as its parts pass, and pushed only when q says
so. Before each push: `cmake --build build -j`, `build/quark_tests`, `tools/release_check.sh` and `quark --uitest`.

### Push 1: measurement first, plus tests and CI hygiene

Nothing in later pushes can be shown to help q's lows without this.

**Contents.**
- Benchmark preset `--uncapped` (maxfps 0, present mode immediate), and a one-off 500-cap run to check the learnt
  sleep that VRR players use (L1-M5).
- `tools/bench_compare.py`: at least 3 ABAB runs per side. Flag a regression only if B's median 0.1% low is worse than
  A's worst run.
- F3 counters: PRED (corrections per minute and the longest replay, in µs) and LATE FLIPS per minute.
- Four event demos recorded by q: a destruction round on Cobblestone, a knockback fight, a 16+ player FFA with low-poly
  models, and a ranked 1v1 round.
- Named threads (10-18), so Tracy, PIX and `perf` show "quark mouse" or "quark audio".
- CI: pin the Windows toolchain (07-02 / L4-M1), a known-failures list for the standing red checks (L4-M2), split the
  slow test groups, fix the red tests first.
- Tests: a regression test for the tick-before-snapshot crash (08-01), LagRelay impairment tests (08-13), an
  unknown-group check (08-10), the port collision (08-09), naming a hung group (08-08), and `release_check.sh` running
  the UI test and `benchmark_check.sh` (08-12).

**How to test.** `build/quark_tests` (all groups), plus the new groups. Run `bench_compare.py` on the current build
against itself: it must report "no change". The run-to-run noise it reports sets the bar for every later push. Then
check that F3 shows PRED and LATE FLIPS in a match.

### Push 2: server hardening (no protocol bump)

**Contents.** 03-01 with L2-M2 (broadcast budgets, head-hit coalescing, session churn), L2-M1 / 03-04 / 03-09 (connect
signature budgets, per-address limits), 03-02 (ticket attempts), 10-02 (vote lookups through the cache), L2-M4 /
L2-M3 / 07-06 (governor trust, updater cache), 03-08 (ranked flags, log only), 02-03 and 04-06, 10-03 (clean shutdown
on SIGTERM) and 10-04 (line-buffered logs), 07-03 / 07-05 / 07-07 (prebuilt server binary, self-test, rollback).

**How to test.**
- A new net test with two headless clients: one floods, the other must stay connected and keep its ranked result.
- A connect test: a flooding host must not stop a stock client joining within its normal retries.
- A governor unit test: testing reporting overload with production idle must leave production at 1000 Hz.
- `quark_tests duel`, `rules` and the ranked groups.
- Tick timing on a 32-bot server before and after: the late-tick count must not rise.
- `autoupdate.sh --dry-run` against a fake release list (L4-M4).

### Push 3: prediction parity, under one protocol bump

q marks this push `[final]` so the sanitizers run on it. If the pending rubberbanding change (protocol 72) lands first,
share its bump or take the next number.

**Contents.**
- `src/game/firerules.h` with one `FirePredictor`, plus one shared `traceShot` (02-12, 02-02, 05-09, L3-M1..M4).
- Deterministic `sin`/`cos`/`pow`/`exp` in shared movement (02-09, L3-M6).
- Hidden snapshot entries instead of truncation (02-01).
- Rules floats sent exactly (05-05), root and freeze transitions predicted (02-13), the `rootCmd` order, the frozen
  gate, the kill plane (05-06) and the refund clock (05-07).
- Demo converters for the old protocol, as CLAUDE.md requires.

**How to test.**
- `quark_tests modelockstep2`, `movement` and `duel`.
- A new zero-misprediction test: a scripted duel through LagRelay at 30, 80 and 150 ms must show 0 corrections with
  every weapon and in tag, bomb and holdfire maps.
- The laser's damage per second at 1000, 500, 250 and 125 Hz must be equal.
- Run the movement hash (pmovehash) on the Linux build and on the Windows build under Wine: they must match.
- The demo converter test for the old protocol.
- F3 PRED near 0 in a real match on q's PC.

### Push 4: client spikes, each behind its own A/B

**Contents.** 10-01, 01-02, 01-03, 01-05, 01-06 / 10-05, 01-07 / 06-01, 06-04, 01-09 / 10-14, 01-01. Then 01-04
(GPU-resident meshes) as its own change after the 16+ player demo exists.

**How to test.** For each change, `bench_compare.py` ABAB on the uncapped preset and on the matching event demo. A
change ships only if its 0.1% low doesn't get worse beyond the measured noise. Check F3 avg / 1% / 0.1% on q's PC, with
Tracy for the event frames (glass break, F12, a settings save). For 01-04, check F3 lows with 31 bots on low-poly
players before and after.

### Push 5: signing key split, in two phases

**Contents.**
- **Phase 1:** a production release that knows a testing public key, and the launcher checks the tag pattern per
  channel.
- **Phase 2,** after that release is out: testing builds sign with the testing key; the production key moves to the
  `production` environment only, with q as required reviewer; the server-list pointer is signed offline.
- Also: record the launcher's source commit in the manifest (07-08), and turn on immutable releases (03-06).

**How to test.** `quark_tests launcher` for both channels and both keys. A testing manifest signed with the production
key must still install during phase 1 and be refused for the production channel. Then a manual install from the
launcher's TESTING and PRODUCTION channels, and the RELEASE_CHECKLIST rows for the launcher and servers.

### Later, and what not to do

- **Later:** an in-place hole grid (04-01), staggered snapshots plus a delta ring sized by time (04-04), and structural
  cleanups (a `protocol_version.h`, finishing the settings table, consolidating `text.h`, one mode-param table).
- **Rejected as over-engineering** (architecture lead): splitting `Game`, an ECS, `std::function` command maps, pimpl,
  a unity build.
- **Don't trade lows for CPU:** 04-05 (learning at a 1000 cap) and the game-thread half of 10-06 must not cost q's lows
  to save CPU.

---

## All findings by area

**Status:**
- **open:** not fixed at `2e2a2de`.
- **fixed on testing:** fixed by a commit after `1e7d2bd` (the commit is named).
- **partly fixed:** a commit after `1e7d2bd` fixed part of it.
- **known:** already written down in the repository's docs or the team's notes.

**Merged:** the other IDs folded into the row. **L1..L5** are the leads: L1 technical director, L2 netcode and security,
L3 architecture, L4 release and QA, L5 skeptic.

### Performance and latency

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 01-04 | S2 | src/client/renderer.cpp:1911-1968, :2086-2103 | Player meshes are transformed per vertex on the CPU and uploaded each frame, off-screen shadows too; this sets the 1% low with 16-32 players. Fix: GPU-resident meshes. | open; worse on testing (d06458b: 2,952 vertices mean, was 1,277) | L5-M2 |
| 10-01 | S2 | src/client/game.cpp:12796, :12827; src/client/config.h:30 | Latent Sync (Windows default) waits with a fixed 0.3 ms spin on the timer the frame cap stopped trusting; late flips stutter. Fix: reuse CapWait's learnt lateness. | open | — |
| 10-02 | S2 | src/game/server.cpp:174, :201; src/game/map.cpp:39-52 | Votes stat map files on the tick thread before the map-info cache; a crafted vote costs hundreds of file-system calls in one tick. Ranked live matches are immune (server.cpp:1735). Fix: go through the cache, cap per tick. | open | — |
| 01-01 | S3 | src/client/renderer.cpp:1588-1603; src/client/game.cpp:3770 | GPU LATE LATCH reads the aim per draw, so one frame can mix several aims, with no cull pad. Off by default and on q's PC; fix before it ever becomes a default. | open | — |
| 01-02 | S3 | src/client/renderer.cpp:2266, :1515-1545 | Warm-up compiles one of the two world programs (picked by the previous map), so the first hole on a new map hits a cold shader. Only Cobblestone among official maps. 15 min. | open | — |
| 01-03 | S3 | src/client/renderer.cpp:1307, :968-988; src/game/map.cpp:1755 | A glass break or hole reads ~50 uniforms back (`glGetUniform*`) and re-meshes every block. About 30-100 µs on the event frame (first estimate of 0.1-0.5 ms was too high). | open | — |
| 01-05 | S3 | src/client/renderer.cpp:1894-1898 | `boxPitched` (arms, projectiles) has no view culling, unlike `box()`. 15 min. | open | — |
| 01-06 | S3 | src/client/renderer.cpp:3386-3427; src/client/game.cpp:1044; src/client/main.cpp:251-252 | F12 screenshot: a 50-100 ms frame inside the event poll (readback and PNG encode on the game thread). Fix: async PBO plus an encoder thread. | open | 10-05 |
| 01-07 | S3 | src/client/lang.cpp:185, :329-342; src/client/renderer.cpp:2691 | Non-English UI: the translation cache grows on dynamic strings and is cleared all at once (232 µs clear, then a 1.56 ms re-translate frame). | open | 04-03, 06-01 |
| 01-08 | S3 | src/client/renderer.cpp:2266; src/client/renderer.h:337-341 | One hole switches the whole world to the program that loses the early depth test (GPU). The renderer's own note: all-Cobblestone 0.1% low 800 vs 2,800 fps. | open | — |
| 04-01 | S3 (S2 on hole maps) | src/game/server.cpp:3628; src/game/map.cpp:1758, :1840, :1906 | Hole cutting and explosions rebuild the whole collision grid on server and clients; the first break of each round copies every brush (6-17 µs on official maps, 176 µs on Solar). Only Cobblestone's hole bricks in the official pool. | open | L1-M2 |
| 04-02 | S3 | src/client/game.cpp:3068, :2938; src/client/lightmap.cpp:320-322 | Nine health-bar sight rays per player every frame; and a 500 m sun ray per player even off-screen. | sun ray fixed on testing (1b371b1); health-bar rays open | 01-12, L1-M1 |
| 04-04 | S3 | src/game/server.cpp:771; src/game/server.h:239, :459-466 | Every client's snapshot is built on the same tick (a burst), and the sent ring is big and cold. Don't shrink it to 64 entries (64 ms at 1000/s breaks delta bases): size it by time and stagger by client. Measure first. | open; per-client rates in team notes | 02-06 |
| 04-06 | S3 | src/game/server.cpp:3537 | `blocksTouched` copies the block list on every command. | open | — |
| 10-06 | S3 | src/client/gpuprefs.cpp:209; src/client/lightmap.cpp:290-295; src/client/game.cpp:580 | Bake workers run above normal apps; the mouse and audio threads aren't above the game thread. Lower the workers now; touch the game thread only with an A/B (it was q's measured choice). | open | — |
| 10-10 | S3 | src/client/game.cpp:12765; src/client/gpuprefs.cpp:83 | Latent Sync predicts the blank from an integer refresh rate (143.98 Hz becomes 144), so it drifts. | open | — |
| 01-09 | S4 | src/client/main.cpp:254-255; src/client/game.cpp:1381 | The event poll sits between the mouse read and the frame, adding poll time to mouse-to-photon. Swap the order. | open | 10-14 |
| 01-10 | S4 | src/client/renderer.cpp:927 | World shader compiles are serial and leak on failure. Load time only. | known (team notes: parallel compile and program binary) | — |
| 01-11 | S4 | src/client/renderer.cpp:831-844 | Stream buffers are orphaned at full capacity. About 0 on NVIDIA. | open | — |
| 01-13 | S4 | src/client/renderer.cpp:183-201 | Cobblestone's hash may be costly per pixel; an integer hash could be cheaper (speculative; the look must not change). | known (team notes) | — |
| 01-14 | S4 | src/client/main.cpp:169-173 | No GL debug-context path to surface driver warnings. | open | — |
| 04-05 | S4 | src/common/net.h:128; src/common/net.cpp:377 | At a cap of 1000 fps or more, CapWait spins the whole wait and never learns. Document it; q's call. | open | — |
| 04-07 | S4 | src/game/server.h (ServerPlayer) | `ServerPlayer` is 28.6 KB; hot flags and cold history share it, and about 11 player scans run per snapshot tick. | known (research) | — |
| 06-10 | S4 | src/client/game.cpp:8413 | A `Settings` copy every frame. | open | — |
| 06-11 | S4 | src/client/game.cpp:8697-8706; src/client/ui.cpp:133 | Menu-side costs are multiplied while SEARCH is open. | open | — |
| 10-15 | S4 | src/client/game.cpp:184, :2203 | Audio setters called every frame, and the device reopens at start-up. | open | — |
| 10-16 | S4 | src/game/server.cpp:1053; src/common/net.cpp:52 | Small syscalls on the tick thread. | open | — |
| 10-17 | S4 | src/client/isa.cpp:146-163; src/client/gpuprefs.cpp:413 | The AVX2 child is the exe Windows sees (GPU preference, firewall, driver profiles); the Intel query hard-codes "quark.exe". | open | — |
| L1-M5 | S4 | src/client/main.cpp:236-241 | At the 1000 cap the benchmark validates nothing about the learnt sleep VRR players use. Run one 500-cap benchmark. | open | — |

### Netcode and prediction

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 02-01 | S2 | src/game/server.cpp:3076-3158, :3957, :4077-4083; src/client/game.cpp:2467-2504 | An overflowing snapshot cuts projectiles from the end: the client reads them as explosions, the owner reports phantom hits, real death records go first. | open | L5-M3 |
| 02-02 | S2 | src/game/server.cpp:2600-2601, :2678; src/client/game.cpp:2363 | The client fires at most one shot per command, so below 1000 Hz the laser does 1/N damage (half at 500 Hz, an eighth at 125 Hz). | open | 05-01 |
| 02-12 | S2 | src/client/weaponpredict.h:22-31; src/game/server.cpp:2615-2619; src/client/game.cpp:2320-2380 | Weapon-switch and fire rules are written twice; the client loop is a hand copy of `Server::runPlayerCmd`. Root cause of 02-02 and L3-M1..M4. Raised from S4 by the architecture lead. | open | — |
| 02-03 | S3 | src/game/server.cpp:3995; src/common/buffer.h:38-41 | A snapshot's world part can be dropped silently when it doesn't fit. | open | — |
| 02-04 | S3 | src/game/netclient.cpp:633; src/game/server.cpp:4005 | During a map download the client sends no acks, so reliable events pile up and a long download can drop the player. | open | — |
| 02-05 | S3 | src/client/game.cpp:2307-2448; src/game/server.cpp:1583 | Remote players stair-step at their sender's frame rate. | open | — |
| 02-07 | S3 | src/game/netclient.cpp:762, :1104 | 62.5 Hz demos play back with about 3 ms of interpolation delay. | open | — |
| 02-08 | S3 | src/game/netclient.h:72; src/game/netclient.cpp:592-607; src/client/game.cpp:14240-14258 | Mispredictions can't be seen, and replays aren't timed. | partly fixed on testing (36aa89f adds `corrections()`; not on F3, no test) | 08-03 |
| 02-09 | S3 | src/game/pmove.cpp:141, :156, :520, :888, :999; CMakeLists.txt:114-128 | Shared movement calls libm `sin`/`cos`/`pow`/`exp`, which can differ between the MinGW client and the glibc server; cross-toolchain results are never compared. | known (docs/REVIEW.md:485, :564) | 05-10, 08-04, L3-M6 |
| 02-10 | S4 | src/game/protocol.h:48-51 | Input packets repeat whole commands; delta-code them (protocol bump). | open | — |
| 02-11 | S4 | src/game/netclient.cpp:679-688 | String-command resends have no pacing. | open | — |
| 02-13 | S4 | src/game/netclient.cpp:636; src/game/pmove.cpp:285 | Freeze and root changes mispredict for one round trip and replay on every poll. | open | — |
| 02-14 | S4 | src/game/netclient.cpp:283 | Snapshot allocation churn on the client. | open | — |
| 02-15 | S4 | src/common/buffer.h:30-31 | Remote position precision at 1000 snapshots/s (protocol bump). | open | — |
| 02-16 | S4 | src/game/netclient.cpp:676; src/game/server.cpp:1546 | `setInputBatch(n > cmdsPerPacket)` would drop commands (latent; no caller does it). | open | — |
| L2-M5 | S4 | src/game/protocol.cpp:333, :381; src/game/netclient.cpp:291 | The client accepts snapshots with trailing bytes; with 02-03 a mis-read world part can be acked as a delta base. | open | — |
| L5-M5 | S4 | src/game/netclient.cpp (36aa89f) | The new 8 m correction blending hides divergence even better; put `corrections()` on F3 and in a test in the same release. | new on testing | — |

### Security

Still-open security items are described here as problem, impact and fix only. Full details in the private notes.

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 03-01 | S1 | src/game/server.cpp (string commands, `sendEvent`, `weaponHit`, `dropPlayer`) | One modified client can make the server queue reliable events for other players until they are dropped; in ranked a drop counts as leaving, so the attacker can be handed wins. Fix: budgets on broadcasting commands, head-hit coalescing, session-churn limits, a net test. | open | L2-M2 |
| 03-03 | S2 (S1 if a repository secret) | .github/workflows/release.yml (release-testing); src/common/sign.cpp:18-19; src/launcher/releases.cpp | One Ed25519 key signs production, testing, launchers and the server-list pointer, and the testing pipeline (unreviewed commits) can use it. Channel binding rests on release data the same CI token controls. Fix: split keys, production key in `production` only, tag check, offline pointer key. | open | 07-01, 07-16, L5-M1 |
| L2-M1 | S2 | src/game/server.h:40; src/game/server.cpp:1115-1157 | The connect-signature budget is shared by the whole server, so one host can stop new players joining; a burst of checks in one frame delays ticks; and nothing limits sessions per address. Fix: per-address budget, per-frame cap or a verify thread, per-address sessions. | open | 03-04, 03-09 |
| L2-M4 | S2 | src/game/governor.cpp:76-95 | Production's governor believes testing's overload report, so a slow testing build can hold production at its lowest rate. Fix: count it only with production contention; never mid-ranked-match. | open | — |
| 07-06 | S2 | tools/server/install.sh; tools/server/autoupdate.sh | The servers can write the release-list cache the production updater trusts, so a compromised testing server could hold production on an old build. Fix: an updater-only cache, separate users. Raised from S3 by the netcode/security lead. | open | — |
| 03-08 | S2 | src/game/server.h:778; src/game/server.cpp (hit reports) | Ranked has no plausibility flags on client hit reports. Fix: flag and log with the result; never refuse or recompute damage. Raised from S3 by the netcode/security lead. | open | — |
| 03-02 | S3 | src/game/server.cpp:1649-1656 | A ranked participant can make the tick thread re-check bad ticket signatures until their good one lands. Fix: attempts per match, one check per tick. Lead 02 says S2; the skeptic S3 (only a participant, and it ends once they sign); S3 chosen. | open | — |
| L2-M3 | S3 | src/game/server.cpp:565, :623-629 | The governor's network vote counts sessions, not hosts, and trusts client-reported loss. Fix: one vote per address; ignore young or idle sessions. | open | — |
| 03-05 | S3 | src/game/ranked.cpp:37-46, :217; src/client/community.cpp:47 | The ranked secret is sent with requests instead of a signature over them. Fix: signed Worker requests. | open | — |
| 03-06 | S3 | src/launcher/releases.cpp:240; src/launcher/main.cpp:436-438 | Unsigned pre-1.17 releases still install, and their assets can be swapped. Fix: refuse them without a known hash; immutable releases. | open | — |
| 03-07 | S3 | CMakeLists.txt:528; src/client/main.cpp:595-605 | Some DLLs load before `hardenDllSearch` runs (DLL planting). Fix: set the search path at link time. | open | — |
| 03-10 | S4 | src/common/http.cpp:65 | The system curl reads the user's own curl config. Pass `-q`. | open | 07-19 |
| 03-11 | S4 | src/common/upnp.cpp:97-99; src/game/server.cpp:1049 | UPnP: a hostile LAN device can burn CPU and set the external address. | open | — |
| 03-12 | S4 | src/game/server.cpp:1587-1594, :1657 | Any player in an edit session can edit the host's map file. Fix: host-granted editing. | open | — |
| 03-13 | S4 | src/game/netclient.cpp:167, :414-418 | Small client hardening gaps off the attack path. | open | — |

### Gameplay correctness

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 05-02 | S2 | src/game/server.cpp:2694; src/client/game.cpp:13399 | The knife's swing continuation runs only for bots; human swings miss follow-through hits. | open | — |
| 05-03 | S3 | src/game/modelogic.cpp:284-319; src/game/gamemodes.cpp:2046; src/game/modeparams.h:24 | Two mode-param tables with different ranges and no enforcement; an out-of-range double cast to int is undefined; `roundTimePerPlayer` is missing from `kParams`. The skeptic lowered it from S2 (needs an operator-written .qmode). | open | L5-M4, L3-M5 |
| 05-04 | S3 | src/game/gamemodes.cpp:77; src/game/modelogic.cpp:80 | "0 = no clock" ends the round at once in 7 modes. | open | — |
| 05-06 | S3 | src/game/pmove.cpp:926; src/game/map.h:1529; src/game/server.cpp:2560 | pmove clamps height at -100 m, but a map's kill plane can be lower, so a player can hang below the map. | open | — |
| 05-07 | S3 | src/game/server.cpp:2764 | The weapon-set kill refund uses the wrong clock and isn't predicted. | open | — |
| 05-08 | S3 | src/game/server.cpp:3208-3217 | The server traces players for shots it's told to ignore (wasted tick work). | open | — |
| 05-09 | S3 | src/game/server.cpp:2769; src/client/game.cpp:13805 | The hitscan rule exists three times, and the client traces each pellet two or three times. | known in part (team notes: pellet rays) | — |
| L3-M1 | S3 | src/client/game.cpp:2363; src/game/server.cpp:2591-2592; src/game/gamemodes.cpp:1723-1748 | Tag: a newly tagged "it" can tag back during the freeze (the client's freeze gate differs from the server's). | open | — |
| L3-M2 | S3 | src/client/game.cpp:2316-2441; src/game/netclient.cpp:636; src/game/server.cpp:2587 | Bomb: a planter or defuser can shoot, because `rootCmd` clears fire only after the client has fired. | open | — |
| L3-M3 | S3 | src/game/server.cpp:2678-2689, :3062; src/client/game.cpp:2328 | Holdfire: the server applies the cooldown during the bow and the client doesn't, so the first shot mispredicts. | open | — |
| 05-05 | S4 | src/game/customrules.cpp:107-120; src/game/server.cpp:1834-1836 | Rules floats go out with 6 digits, so server and client predict with slightly different numbers. The architecture lead lowered it (the skeptic says S3). | open | — |
| 05-11 | S4 | src/game/bots.cpp:805; src/game/pmove.cpp:1010 | Numbers retyped outside their tables. | open | — |
| L3-M4 | S4 | src/game/server.cpp:2667-2670; src/client/game.cpp:2358-2362 | Semi-auto fire edge detection differs between server and client. | open | — |
| L3-M7 | S4 | src/game/server.h:737-738; src/client/game.h:1356-1360; src/game/bots.cpp:739 | The body-scale and eye-height rule is written 3+ times with different formulas. | open | — |
| L3-M8 | S4 | src/game/server.cpp:2677; src/client/game.cpp:2377 | Comments say "laser every 10 ms" but refire is 1 ms; that assumption hid 02-02. | open | — |

### UI and tools

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 06-02 | S3 | src/client/ui.cpp:30; src/client/editor.cpp:2393 | A typed slider value is lost on click-away. | open | — |
| 06-03 | S3 | src/client/editor.cpp:467 | Editor undo joins edits to different objects. | open | — |
| 06-04 | S3 | src/client/config.cpp:440-442; src/client/game.cpp:990 | Settings saves ignore failure (a Windows rename fails while another program holds the file) and run synchronously, in matches too. | open | — |
| 06-05 | S3 | src/client/config.cpp:402-429 | `bind_version` is written as a literal, apart from its migrations. | open | — |
| 06-06 | S3 | src/client/game.cpp:7917-7937 | Settings SEARCH only understands English. | open | — |
| 06-09 | S3 | src/client/editor.cpp:455-457 | The editor rebuilds the whole map per box step, and copies it whole online. | open | — |
| 06-15 | S3 | src/client/config.cpp:45, :356-358 | The settings table is half done; the rest is a long hand-written ladder. Raised from S4. | known (research) | — |
| 06-07 | S4 | src/client/game.cpp:771, :1134 | Text input is ASCII-only in a 10-language game. | open | — |
| 06-08 | S4 | src/client/layout.cpp:25-27 | A `thread_local` with a destructor is back (the MinGW thread-exit crash rule). | known rule (team notes); this case open | 09-05 |
| 06-12 | S4 | src/client/editor.cpp:2262-3538; src/client/game.cpp:8376 | The two longest UI functions can be split at zero frame cost. | open | — |
| 06-13 | S4 | src/client/config.cpp:155-163 | The `ultra_margin_us` migration depends on line order. | open | — |
| 06-14 | S4 | src/client/game.cpp:4563; src/client/binds.cpp:6-36 | KEY OVERLAY still shows CROUCH, and the game has no crouch. | open | — |
| L3-M9 | S4 | src/client/game.cpp:2800, :2816, :3473 | `drawWorld` sends a network command and holds screenshot-only debug lineups. | open | — |

### Build, release and CI

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 07-02 | S2 | .github/workflows/release.yml:198-213; tools/build_windows.ps1:27 | CI's Windows toolchain (GCC 13, old C runtime) isn't q's (GCC 16, UCRT); the shipped test binary never runs; the archive install step timed out repeatedly on 2026-11-10, blocking testing prereleases. The skeptic says S3 (every client predicts against a Linux server anyway); the release lead S2 for the blocked releases; S2 chosen. | open | L4-M1 |
| 07-03 | S2 | .github/workflows/release.yml:268-269; tools/server/install.sh:26-33 | Dedicated-server updates compile on the live production box, for every testing push. | open | — |
| 07-05 | S2 | tools/server/autoupdate.sh:227-231; tools/server/install.sh:203-205, :283 | No smoke test and no rollback for the server binary. Raised from S3 by the release lead. | open | — |
| 07-04 | S3 | tools/embed_maps.py:51-53 | Embedded data is written as decimal initializer lists (slow, memory-hungry compiles). | open | — |
| 07-07 | S3 | tools/server/autoupdate.sh:223; tools/server/install.sh:183; CMakeLists.txt:136 | Box builds ignore the host CPU: always the v2 baseline, and a VPS with an older virtual CPU would crash-loop. | open | — |
| 07-08 | S3 | tools/launcher_build.sh:12-23; tools/sign/release_manifest.sh:43 | The signed launcher is built from a moving branch, and its commit isn't recorded. | open | — |
| 07-09 | S3 | CMakeLists.txt:126 | Hardening flags come from the distro's compiler defaults by accident; the MinGW build gets none. | open | — |
| 07-10 | S3 | CMakeLists.txt:61-70 | q's Windows exes get two manifests, and the linker silently drops one. | open | — |
| 07-11 | S3 | CLAUDE.md:18-29; docs/github rules | The CLAUDE.md release section is stale, and agents follow it. | open | — |
| L4-M3 | S3 | src/ (no crash handler); CMakeLists.txt:78 | Neither game nor server records anything on a crash, and release builds carry no symbols. | open | — |
| L4-M5 | S3 | docs/RELEASE_CHECKLIST.md §2 | q's local "production" and "testing" builds use a different toolchain from the shipped ones, so the checklist's lows comparison compares local builds. | open | — |
| 07-12 | S4 | cmake/build_id.cmake:8-12 | Every CI build is labelled "-dirty". | open | — |
| 07-13 | S4 | CMakeLists.txt:47-55 | Testing builds' Windows file version drops the build number. | open | — |
| 07-14 | S4 | src/client/isa.cpp:23-27 | The AVX2 hand-over checks only part of x86-64-v3. | open | — |
| 07-15 | S4 | CMakeLists.txt (Windows LTO) | q's local LTO links run serially. Link time only. | open | — |
| 07-17 | S4 | src/client/lightmap.cpp:660-666; src/game/bots.cpp:891 | GCC 16 warnings, including `Vec3::operator[]` as `(&x)[i]`. | open | 09-09 |
| 07-18 | S4 | .github/workflows/release.yml:188-196 | CI wall time can be cut. | open | — |
| L4-M6 | S4 | .github/workflows/release.yml:28-30 | A final run tests the branch's current commit; nothing ties it to the commit the production PR merges. | open | — |
| L4-M7 | S4 | docs/HANDOVER.md:1, :345 | HANDOVER still describes the world after 1.17.0. | open | — |

### Testing

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 08-05 | S2 | tests/tests.cpp:9759; src/game/benchmark.h:4-6 | Frame-time regressions can't be caught: the only demo runs no event paths, capped runs measure pacing, and noise is ~17% at the 0.1% low. The skeptic says S3; the technical director S2 (process, since every perf fix depends on it); S2 chosen. | open | L1-M3 |
| 08-01 | S3 | src/game/netclient.cpp:636; src/game/netclient.h:64-66 | The Windows tick-before-snapshot crash is fixed, but there's no regression test. | fixed before review (ee7ba62, bf62976); test open | — |
| 08-02 | S3 | tests/tests.cpp:83-95 | Linux tests run at a 4x clock, which hides one-frame ordering windows that Windows and players hit. | open | — |
| 08-06 | S3 | tests/fuzz/fuzz_packet.cpp:94-125 | Fuzz gaps on bytes that a server or a shared file controls. | open | — |
| 08-07 | S3 | tests/fuzz/fuzz_server.cpp:23 | `fuzz_server` is stateful and size-capped. | open | — |
| 08-08 | S3 | tests/tests.cpp:10638-10641; tests/groupproc.cpp:79-90 | A hang in the parallel run doesn't name the group, and it leaves children running. | open | — |
| 08-09 | S3 | tests/tests.cpp:9472 | `hidden` and `race` can collide on a port. | known (team notes) | — |
| 08-10 | S3 | tests/rules_tests.h:3168; tests/tests.cpp:10731 | `quark_tests rules <unknown>` passes. | known (team notes) | — |
| 08-11 | S3 | tests/tests.cpp:10735-10800 | Rerunning an area needs a hand-written map of groups. | open | — |
| 08-12 | S3 | tools/release_check.sh:12-41 | The pre-push gate skips the UI test and the benchmark check. | open | — |
| 08-13 | S3 | src/common/net.h:40-51; tests/rules_tests.h:2026-2032 | No impaired-network tests. LagRelay, added on testing (PR #210), makes them easy. | open (tool now on testing) | — |
| 08-14 | S3 | tests/tests.cpp:1344 | Movement tests pin placeholder numbers, so q's tuning turns them red. Raised from S4. | open | — |
| L1-M4 | S3 | src/client/game.cpp:12818-12828 | Latent Sync knows when a flip is late but never counts it; add LATE FLIPS to F3. | open | — |
| L4-M2 | S3 | tests/tests.cpp:1333-1358 | Standing red checks on testing (UI test settings NAV and menus, movement, modelockstep2, materials) hide new failures. | known (team notes) | — |
| L4-M4 | S3 | tools/server/autoupdate.sh; tools/server/pick_release.py | The code that changes production servers by itself has no automated test. | open | — |
| 08-15 | S4 | tests/groupproc.cpp:98-101 | The test runner gives no crash triage. | open | — |
| 08-16 | S4 | (Windows test runs) | Windows PowerShell turns expected stderr into error records, so a pass reads like a failure. | open | — |
| 08-17 | S4 | tests/server_tests.h:458-466 | Windows CPU-time asserts use wall time. | open | — |
| L4-M8 | S4 | src/client/gltf.cpp; src/client/decoders.cpp; tests/fuzz/ | No fuzzer for the override asset parsers (glTF, audio). | open | — |

### C++ and code health

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 09-01 | S3 | src/client/gltf.cpp:307, :559 | The glTF node walk is exponential on shared nodes, with no budget (a small file can hang loading). | open | — |
| 09-02 | S3 | src/client/game.cpp:10507-10510 | `std::clamp` with `lo > hi` on the main menu (undefined; blocks turning on `_GLIBCXX_ASSERTIONS`). | open | — |
| 09-03 | S3 | src/client/gltf.cpp:239 | glTF float-to-int casts of out-of-range values are undefined. | open | — |
| 09-10 | S3 | src/game/gamedefs.h:19 | Compile-time fan-out: a few headers rebuild most of the game. Raised from S4. | open | — |
| 09-11 | S3 | src/common/upnp.cpp:43; gamefiles.cpp:22; theme.cpp:26 | Duplicated helpers have started to drift. Raised from S4. | open | — |
| 09-04 | S4 | src/common/buffer.h:23-25 | `Writer`/`Reader` do per-byte I/O, and a negative length is a trap. | open | — |
| 09-06 | S4 | src/game/demo.cpp:38-44 | Demo version ranges come out backwards for testing builds. | open | — |
| 09-07 | S4 | src/game/server.h:736; src/common/asyncwriter.h:41-44 | `glog` is printf-like without the format attribute. | open | — |
| 09-08 | S4 | src/ | No `[[nodiscard]]` on parse and signature-verify functions. | open | — |
| 09-12 | S4 | src/server/main.cpp:174-187 | The detached stdin thread outlives `main()`'s locals. | open | — |
| 09-13 | S4 | src/client/gpuprefs.h:16-26 | `RasterClock` breaks the rule of three. | open | — |

Closed as not a bug: `std::clamp` at src/game/server.cpp:640 and :702 (`rateTopLevel_` is at most 3), and
src/game/pmove.h:202.

### Platform

| ID | Sev | Where | Finding | Status | Merged |
|---|---|---|---|---|---|
| 10-03 | S3 | tools/server/install.sh:372-385; src/game/server.cpp:375-405 | The dedicated server dies on SIGTERM or Ctrl+C without a clean shutdown (results, logs). | open | — |
| 10-04 | S3 | src/game/server.cpp:145, :826 | Server logs are fully buffered under systemd, so a crash loses the last lines. | open | — |
| 10-07 | S3 | src/client/audio.cpp:848 | WASAPI doesn't follow a change of default device. | open | — |
| 10-08 | S3 | src/client/audio.cpp:1284 | ALSA: an unrecoverable write error spins a core. | open | — |
| 10-09 | S3 | src/client/audio.cpp:1212 | ALSA's blocking open can hang start-up and quit. | open | — |
| 10-11 | S3 | src/client/audio.h:118; src/client/audio.cpp:1189 | macOS audio: the latency setting is never applied. | open | — |
| 10-12 | S3 | (GLFW Cocoa backend) | macOS: mouse motion is accelerated (no raw input), and the app doesn't declare latency-critical activity. | open | — |
| 10-13 | S3 | src/game/botbrain.cpp:2199; src/game/governor.cpp:174 | Bot brains fail to load under a non-ASCII Windows profile path. | open | — |
| 10-18 | S4 | (all threads) | About 20 thread kinds, none named. | open | — |

---

## How to measure

- **Benchmark uncapped.** q plays uncapped, so measure uncapped: maxfps 0, immediate present, GPU latch off. Capped
  runs at 1000 fps measure the cap's pacing, not the work: CapWait spins away any work under about 0.8 ms.
- **Know the noise.** Two back-to-back runs of the same build differed by about 17% at the 0.1% low (p99.9 of 215 vs
  252 µs, 4,410 vs 3,683 fps). A single A-vs-B run proves nothing below that.
- **Run ABAB, at least 3 per side.** Call it a regression only if B's median 0.1% low is worse than A's worst run.
  `tools/bench_compare.py` (push 1) does this.
- **Use event demos.** The shipped demo has 7 bots and runs no destruction, knockback or big fight. Use the four event
  demos from push 1 for the change at hand.
- **F3 counters.** avg / 1% low / 0.1% low FPS (1% low = 1 / p99 frame time; 0.1% low = 1 / p99.9). Push 1 adds PRED
  (corrections per minute, longest replay) and LATE FLIPS per minute. Check F3 on q's PC after each change.
- **Old results.** The 1.19.7.14 run's low 0.1% low (588 fps) was that build's 0.5 ms Windows timer grid: the build
  predates the CapWait fix (ac05d52, first in 1.19.7.15). It was the build, not the machine.
- **Tracy** (`QZONE` is wired) for single event frames. With named threads (10-18) it shows each thread by name.

---

## Done well — don't undo

- **Main loop order:** cap wait, fence, network, spin to the deadline, mouse, frame, Latent hold, swap, fence. FRAME
  CAP OFFSET absorbs the variable parts.
- **CapWait:** absolute deadlines with no catch-up; it learns the PC's p99.9 timer lateness, rising at once and falling
  slowly. 10-01 asks to reuse it, not replace it.
- **Raw mouse thread:** a message-only window with lock-free atomics that never lose counts; motion while unfocused is
  dropped; `ClipCursor` instead of per-frame `SetCursorPos`.
- **Rendering:** about 10 draw calls a frame, one orphaned dynamic upload, cached uniforms, no per-frame `glGetError`,
  `glFinish` or readback; GPU timestamps read only when available.
- **Layout engine:** a declaration hash skips the whole solve on an unchanged frame; fixed capacity and per-thread scratch,
  so no allocation in steady state. Text fit and wrap caches.
- **`UiExp` contract,** checked by `quark --uitest`: every widget declares what it does. Settings SEARCH reuses the
  pages' own draw code.
- **Netcode:** a sticky `bad()` reader, NaN/Inf mapped to 0, every count and id range-checked before use; delta
  compression against the acked snapshot with the server recording exactly what the client holds; O(1) base lookup; no
  allocation when building snapshots; reconcile once per poll.
- **Determinism:** `-fno-fast-math -ffp-contract=off` on every toolchain and the LTO link; `roundHalfAway` and `angQ`
  bit-exact; own `exp`/`log`/`pow` in movement expressions.
- **Pre-auth design:** a keyed cookie bound to address, port, nonce and time; no reply bigger than its request; PROBE
  capped per address and globally; the ranked connect proof signs cookie, nonce and the dialled address.
- **Update chain:** Ed25519 package signatures plus a signed manifest binding tag, version, channel and sha256; the
  launcher verifies before parsing and checks the staged copy; the server updater refuses downgrades and checks out an
  exact SHA.
- **CI hygiene:** Actions pinned by SHA, `persist-credentials: false`, no `${{ }}` inside `run:`, no
  `pull_request_target`, `check_secrets` that never prints a hit, a simhash gate on the PGO server build.
- **Mode logic VM:** bounded loops with cost tracking, saturating conversions, guarded `% 0`; data-driven mode numbers
  with a lockstep test between native and `.qlogic` defaults.
- **Map text reader:** reproduces `istream >>` exactly with a correct fast path; every coordinate range-checked.
- **Tests:** timing tests rerun only when the harness measured a host pause; per-run port blocks; the manual `SimClock`;
  CPU-only groups niced; zero-allocation gates and fault injection.
- **Settings and files:** `writeFileAtomic` everywhere; every saved config key loads again; file watching on its own
  thread for a measured reason.

---

## Questions for q

Each can be answered in one line.

**Security and ranked**
1. Is `QUARK_SIGNING_KEY` only an environment secret (not a repository secret), with the environments' deployment
   rules set?
2. May we add a separate testing key, and an offline key for the server-list pointer?
3. Are the proposed budgets on renames, model changes and team changes OK, and may head hits be coalesced per batch
   window (the first one stays immediate)?
4. When a player's reliable queue overflows, should the server resync them instead of dropping them?
5. Are per-address limits OK, given players behind one shared address (carrier NAT, LAN parties)?
6. Should the governor ignore testing's overload unless production is also overloaded?
7. In ranked, should a report with a weapon the mode doesn't allow be only flagged, or refused?
8. Log-only flags for hits from a frozen or rooted shooter: OK?
9. Should signed Worker requests replace sending the ranked secret?
10. Refuse pre-1.17 packages without a known hash, and turn on immutable releases?
11. Edit sessions: host-only editing unless the host allows others?

**Gameplay rules**
12. Should the laser do full damage per second at every tick rate?
13. Tag: may a newly tagged "it" tag back during the freeze?
14. Bomb: may a planter or defuser shoot?
15. Knife follow-through: for humans too, or bots only?
16. The weapon-set kill refund: for humans, or bots only?
17. Deterministic movement math: OK to pay 20-40 ns per air step and one protocol bump?
18. What ranges should mode params have, and should "0" mean "no clock"?
19. A protocol bump for 02-10 and 02-15 (smaller input packets, finer remote positions)?
20. Should bursty remote players be smoothed (02-05)?
21. Ranked: a fixed 10 ms interpolation delay?

**Performance and measurement**
22. Will you record the four benchmark demos, and may the benchmark get an uncapped option?
23. Do you play with the English UI?
24. Re-test the health bars at 4-8 ms?
25. Keep the full spin at caps of 1000 and above?
26. Latent Sync: use the learnt wait?
27. GPU-resident player meshes after the FFA demo exists?
28. GPU LATE LATCH: fix it, or drop it?
29. Cobblestone: try an integer hash, if the look stays the same?
30. Check F3 lows with 16-31 bots on low-poly players before production?
31. Stagger snapshots by client above 24 players?
32. Bake threads at below normal, and the game thread at above normal under the high priority class (only with an
    A/B)?
33. Defer in-match config saves until the round ends?

**Servers and CI**
34. Install CI's PGO server binary on the boxes instead of building there?
35. Will you re-run `install.sh` on the boxes once that lands?
36. Pin CI to WinLibs GCC 16 UCRT, and run the movement hash under Wine?
37. Batch the fixes into the five pushes above?
38. For the red tests: a known-failures list, or update the numbers?
39. Add the UI test and benchmark check to `release_check.sh` (+6-8 min)?
40. Should the checklist's performance rows use `bench_compare.py`?

**UI and code**
41. Allow non-ASCII text input (names, chat, search)?
42. Remove the CROUCH cell from KEY OVERLAY?
43. Add `[[nodiscard]]` and format attributes, and consolidate `text.h`?
44. Move the settings ladder into the table in batches?
