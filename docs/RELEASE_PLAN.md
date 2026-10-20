# quark 1.0.0 — release plan

Living checklist for the road to 1.0.0. Every step ends with a commit + push (build, `build/quark_tests`
green, VERSION bump, changelog), so work can resume from the last ticked box after any interruption.
Status marks: `[ ]` todo, `[~]` in progress, `[x]` done (with the version it shipped in).

Findings, measurements and decisions go in `docs/AUDIT.md` (created in phase 1) so nothing lives only in
a chat log.

## Phase 0 — fix reported bug
- [x] Depot: north trench stairs rose toward the trench instead of the street (0.33.1)
- [x] Hamlet ladders: rungs stuck out into the climb space, roof overhang / parapet / glow trim blocked the
      top, climb volume ended too low; new `quark_tests ladders` check climbs every ladder on every map (0.33.2)
- [x] Hamlet verticality: climbable church roof, two-storey house, rooftop plank bridge, lookout tower (0.33.2)

## Phase 1 — audit (no behaviour changes, only notes + screenshots)
Write each finding into `docs/AUDIT.md` as: area / what / severity (bug, polish, perf) / fix plan.
1. [x] **Menus & UI logic** — walk every screen with `--screenshot` debug flags (main, host, play vs bots,
       server browser, settings + all tabs, HUD editor, controls, crosshair, leaderboard, map editor,
       pause, vote menu, every picker). Check: text overflow, alignment, dead buttons, back/escape
       paths, state that leaks between screens (the vs-bots/ranked bug class), defaults.
2. [x] **Modes** — read every rule path in `gamemodes.cpp` (12 modes): start/end conditions, scoring,
       respawn, round flow, overtime, disconnect mid-round, late join, spectators, 1-player edge cases.
       Bot playtest each mode on its map (`quark_tests playtest`), record numbers.
3. [x] **Maps** — screenshot all 15 (orbit + spawn view), rate style / gameplay / creativity, list
       remaining issues (stair directions, spawn sightlines, stuck spots, pits, symmetry on ranked).
       Automated: add a check that every stair run leads onto the surface it touches.
4. [x] **Gameplay & balance** — weapons table (TTK, DPS, range role), alt-fires, movement numbers vs
       references (Quake / Diabotical-style arena shooters), duel rulesets, vampire fade, ranked ladders.
       Weapons/movement are owner-defined placeholders: report proposals, change only clear bugs.
5. [x] **Website** — layout at 390 / 1280 px, links, live data (leaderboard, changelog, weapons),
       screenshots up to date (15 maps), copy accuracy vs the game.
6. [x] **Code scan** — dead code, duplicated logic, debug leftovers, per-frame allocations, O(n²) loops
       in hot paths, redundant state; list each with file:line in AUDIT.md.
7. [x] Rank everything found (impact × effort), decide the fix list for 1.0.0.

## Execution order (decided after the audit)
1. Rule bugs + menu polish from AUDIT.md §1-2 (one release).
2. Phase 2 client frame rate (AUDIT §6) incl. the anti-aliasing option.
3. Phase 3 server CPU (AUDIT §7) — snapshot send rate becomes a server setting (default keeps
   interpolation delay low; measured and reported).
4. Phase 4 bandwidth (AUDIT §8).
5. Phase 5 final scan, 1.0.0.

## Phase 2 — client frame rate (judge by 1% / 0.1% lows, not averages)
Measure first: a fixed benchmark (`--perf` style run: scripted camera over each map, bots fighting,
records avg / 1% / 0.1% frame times) so every change is compared against numbers.
Candidates (only kept if the numbers improve and the screenshot is unchanged):
- [ ] Profile CPU side of a frame (Tracy build): HUD text batching, dynamic box emission, interpolation,
      per-frame vector allocations, snapshot decode.
- [ ] World draw: frustum culling of world geometry in chunks (one VBO, per-chunk draw ranges), state
      changes, shadow pass cost (cascade size, only dynamic casters), sky pass.
- [ ] Shader cost: world fragment shader branches (material switch, seams, tonemap) — keep the look,
      cut ALU (precompute per-material constants, move hash/seam math where cheap).
- [ ] Upload paths: dynamic VBO orphaning / persistent mapping, avoid re-uploading unchanged data.
- [ ] **Anti-aliasing option** (off by default): MSAA 2x/4x via the default framebuffer (settings →
      video), applied on restart or by recreating the window; FXAA as a cheap alternative if MSAA costs
      too much on integrated GPUs.
- [ ] Frame pacing: input → present latency path, vsync / frame cap behaviour, GPU stalls (glFinish,
      readbacks).

## Phase 3 — server CPU (target: steady 1000 Hz on a 1-vCPU micro server)
Measure first: a server benchmark (`quark_tests serverbench`): 16 bots + projectiles on the busiest maps,
report µs per tick (avg / p99) and % of the 1 ms budget.
- [ ] Profile the tick: pmove per player, collision queries (grid), bots (nav Dijkstra, LOS raycasts,
      per-tick scans), projectiles, lag-comp rewind, snapshot building + delta encoding, logging.
- [ ] Bots: cache paths (repath on goal change / timer only), throttle LOS checks, avoid per-tick
      allocations, cheaper nav (precomputed next-hop tables per map if node count allows).
- [ ] Snapshots: build each client's snapshot only at the send rate, reuse shared per-tick state,
      avoid per-client re-encoding of identical data.
- [ ] Idle server: sleep properly when empty (no busy-wait), no work for disconnected slots.
- [ ] Memory / allocation churn in hot loops (std::string, vectors per tick).

## Phase 4 — netcode bandwidth
Measure: bytes per snapshot and kbit/s per client at 8 / 16 players (existing net test prints this).
- [ ] Field quantization review (positions, angles, velocities) vs prediction error.
- [ ] Delta encoding coverage (players, projectiles, mode state), skip unchanged entities entirely.
- [ ] Snapshot send rate vs tick rate; visibility culling (already: hidden players) — extend to projectiles.
- [ ] Reliable event batching; packet header size.
- [ ] Verify no regression in prediction error / interpolation delay (net test thresholds).

## Phase 5 — final scan & 1.0.0
- [ ] Re-run every test, playtest, benchmark; compare with phase 2–4 baselines.
- [ ] Re-screenshot all maps + menus; refresh site images.
- [ ] Final code scan for sloppy / redundant / slow code introduced along the way.
- [ ] VERSION 1.0.0, changelog summarising the road to 1.0, report back: what changed, measured gains,
      why it's ready (and any known issues left).
