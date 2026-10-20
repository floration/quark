# quark 1.0.0 audit

Phase 1 of `docs/RELEASE_PLAN.md`. Each finding: **where**, what, severity (**bug** / **perf** / **polish**),
and the fix. `[x]` = fixed (version), `[ ]` = open, `[-]` = won't fix (with reason).

## 1. Menus & UI
Screens walked: main, host, play vs bots, settings, controls, crosshair, HUD editor, leaderboard, pause,
vote, every picker (host / bot / live map + mode, rulesets, tourney lists, ranked ladders), map editor.

- [ ] **bug** — `--picker 5` (live map picker opened with no game) segfaulted in `drawPicker`. Only reachable
      by debug flag, but the picker now closes itself when there's no connection.
- [ ] **polish** — the VS mode tile says "MAP: FOUNDRY": Foundry (all-mode) was marked `for vs` and sorts
      before Bastion. All-mode maps other than Bastion/Colosseum get no `for` line ("ALL MODES").
- [ ] **polish** — the Tag tile shows no default map (Hamlet is `for hide`); a mode without its own map
      falls back to the hide map for tag.
- [ ] **polish** — menu screens can't be screenshotted headless (`--backdrop` hides menus, no backdrop =
      no screenshot). Add `--menushot` so audits can capture them.
- Checked and fine: pause menu, vote menu, leaderboard layout, map / mode pickers (greyed entries explain
  why), host screen, editor panel (0.31).

## 2. Modes (rules review + bot playtests)
Playtest (bots, 40 s, `quark_tests playtest`): every mode starts, scores, ends; hide and seek: seeker found
4/7 in a 5 min round; tag: 2 eliminations in 3 rounds.

- [ ] **bug** — ranked: the losing player can quit before the end and the match isn't rated (only one
      player connected at the end). Record both participants at match start; a leaver loses.
- [ ] **bug** — tourney bracket stores slot numbers: a newcomer reusing a departed entrant's slot plays
      their match. Store a per-connection join id.
- [ ] **bug** — tourney: `team a/b` while a bracket runs lets spectators into a 1v1 (or both finalists onto A).
      Refuse team changes during a running tourney (spectate only).
- [ ] **bug** — tourney stalls if a finalist leaves (draws forever when both leave; 3 forfeit rounds when
      one leaves). Walkover: advance the remaining player at once.
- [ ] **bug** — tag: if everyone else leaves, the last player is eliminated at the timer and the match
      ends with "nobody left". End the match with that player winning as soon as ≤ 1 remain.
- [ ] **bug** — FFA with zero players never ends (enters overtime every tick). No players → draw.
- [ ] **bug** — hide and seek round count follows the live player count (leavers end the match early,
      joiners extend it). Fix the round count at match start.
- [ ] **bug** — hide and seek: hiders who fall into the void count as "found" (seeker +3). Only knife
      finds count; a void death is just out.
- [ ] **bug** — bomb dropped by a carrier who fell off the map lands below the kill plane: unreachable.
      Drop it at the carrier's last grounded spot.
- [ ] **polish** — `it` / `out` flags carry into warmup after a hide / tag match (the old seeker keeps
      the knife). Reset in `rulesReset`.
- [ ] **polish** — payload tiebreaker reuses rounds 1-2 progress. Reset the per-team bests for 3-4.
- [ ] **polish** — vote ballots are per slot: a newcomer in a leaver's slot inherits the vote. Clear the
      slot's ballot on connect / drop.
- [ ] **polish** — tag: a new "it" picked because the old one left gets no 2 s freeze. Give it the same.
- [-] koth same-tick double finish favours A — unreachable in practice.

## 3. Maps (style / gameplay / creativity, out of 10)
| map | style | gameplay | creativity | notes |
|---|---|---|---|---|
| gantry | 8 | 8 | 7 | owner's favourite; open bay + crane cabs |
| hamlet | 8 | 8 | 8 | ladders fixed 0.33.2; verticality added |
| temple | 8 | 7 | 7 | |
| furnace | 7 | 7 | 7 | |
| anvil | 7 | 7 | 7 | |
| crucible | 7 | 7 | 8 | molten moat |
| depot | 7 | 7 | 7 | stairs fixed 0.33.1 |
| harbor | 7 | 7 | 7 | |
| railyard | 7 | 7 | 7 | |
| canal | 7 | 7 | 7 | |
| summit | 7 | 7 | 7 | |
| bastion | 7 | 7 | 7 | |
| foundry | 7 | 7 | 7 | |
| colosseum | 7 | 7 | 7 | |
| viaduct | 7 | 7 | 8 | race |
Automated per map: spawns clear, preferred mode supported, every spawn reaches every objective, ranked
maps point-symmetric, every ladder climbable. Remaining polish: none blocking.

## 4. Gameplay & balance (weapons and movement are owner-defined: proposals only)
Perfect-aim time to kill from 100 hp: knife 1.4 s (3 hits, 2.8 m), crossbow 1.5 s (2 hits), shotgun 2.0 s
(3 blasts), sticky 2.0 s (3), rifle 3.1 s (15), laser 3.3 s (beam, 32 m), rocket 4.0 s (3), sniper 4.0 s (3).
- Rocket and sniper share the slowest TTK; in arena-shooter references (Quake / Diabotical) the rocket is the
  all-round weapon and kills in ~2-3 direct hits at ~0.8 s refire. Proposal for the owner, not applied:
  rocket refire 2.0 → 1.0 s (TTK 2.0 s), splash unchanged.
- Sniper TTK 4.0 s at 3 hits makes a 2-hit crossbow strictly better at range ≤ 90 m/s travel. Proposal: sniper
  50 dmg (2 hits, 2.0 s) or headshots.
- No changes to numbers in 1.0 without the owner's call.

## 5. Website
Checked at 390 px and 1280 px: layout, links, live leaderboard / changelog / weapons, 15 map screenshots
(0.33). Fine. Refresh screenshots once more before 1.0.

## 6. Code: client frame time
- [ ] **perf (high)** — `U()` uniform lookup is a linear scan, ~40 calls per frame; dyn uniforms uploaded
      twice. Resolve locations once per program; upload constants (palette, sun, fog, samplers) only on
      change.
- [ ] **perf (high)** — world fragment shader: `pow(…, 2.2)` on uniforms per pixel; glow / far materials run
      the full lighting path then discard it. Linearise palette on the CPU; branch early for mat ≥ 4.
- [ ] **perf (high)** — dynamic geometry has no CPU frustum culling (players, projectiles, lines, labels).
      Cull non-caster boxes outside the view frustum.
- [ ] **perf (high)** — F3 stats: allocate + full sort of frame times every 0.5 s (a spike in the very
      thing it measures). Persistent buffer + `nth_element`.
- [ ] **perf (med)** — dyn VBO uploaded up to 3× per frame, shadow-only copy duplicates dyn vertices.
      One upload, sub-range draws.
- [ ] **perf (med)** — HUD builds `std::string`s every frame (to_string / concatenation). snprintf into stack
      buffers / cache text that only changes with its inputs.
- [ ] **perf (med)** — `interpolatedProjectiles()` returns a new vector per call; scoreboard copies +
      sorts every frame while open; `playerName()` returns by value.
- [ ] **perf (med)** — audio mixer holds its mutex while mixing (a `play()` from the game thread can wait a
      whole mix period); `erase` from the middle of the voice list. Swap a pending list; swap-and-pop.
- [ ] **perf (low)** — frame cap drifts below target (`frameStart = now`); `glClearColor(pow…)` per frame;
      duplicate `lin` lambdas in drawWorld.
- [ ] **feature** — anti-aliasing: settings option (off / 2x / 4x / 8x MSAA), multisampled scene FBO
      resolved before the UI so the HUD doesn't pay for it; off by default.
- [ ] **polish** — misplaced comment in `config.h`; duplicated "return to editor" logic.

## 7. Code: server CPU
- [ ] **perf (high)** — a snapshot is built, delta-encoded and sent to every client **every tick** (1000/s).
      Separate send rate from tick rate (tick 1000 Hz, snapshots at a configurable rate, default 125 Hz);
      prediction / reconciliation unchanged (acks ride on whichever snapshot arrives).
- [ ] **perf (med)** — per-send vector copies of the player / projectile lists per client; linear search of
      the 256-entry sent ring for the delta base; mode state encoded twice per client; O(n²) entity matching
      in the delta encoder. Shared per-send lists + hidden bitmask, index ring by tick, encode mode once,
      id-indexed base lookup.
- [ ] **perf (med)** — bot nav: Dijkstra with O(n²) min-select + 3 allocations per call, re-run whenever a
      moving goal shifts 1 m; ladder link lookup is a scan. Precompute next-hop table per map.
- [ ] **perf (low-med)** — bot target scan raycasts every enemy in range 10×/s; check by distance first,
      stop at the first visible.
- [ ] **perf (low)** — hit-flush scans 1024 cells per tick; keep a pending flag.
- [ ] **perf (low-med)** — idle loop spins the last 0.3 ms of every tick (≈30% CPU with nobody connected).
      Empty server: long sleeps; populated: allow a little lateness instead of spinning.

## 8. Netcode bandwidth
- [ ] send rate (above) is by far the largest saving (1000 → 125 packets/s per client).
- [ ] self block: hook point and dash fields only when active (flag byte); `timeMs` derived from tick.
- [ ] projectiles: velocity only when it changes (bounces / sticks), position each send.
- [ ] events: resend unacked reliable events at most every ~RTT/2, not every send.
- [ ] tick rate field only when it changes.
