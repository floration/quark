# The bot editor's simulator: `sim`, `simbatch`, `simcheck`

Three `quark_maptool` commands for the bot editor. They are built into the map editor's helper: CMake globs this
folder into the `quark_maptool` target.

- `sim` runs one headless match and streams it as NDJSON. It can run at real time × `speed` for watching, or as
  fast as the CPU goes.
- `simbatch` runs N seeded matches with the same setup on every core, and reports per bot, per brain and per match.
- `simcheck` compiles a brain the way `sim` does, without running a match.

Each match is the game's own `Server` and bots (`BotSim`, `tools/maptool/map/botsim.h`), stepped tick by tick.
It doesn't read sockets or the real-time clock, so the same request and seed always gives the same match, whether
`sim` or `simbatch` runs it.

```
cmake --build build --target quark_maptool
quark_maptool sim [--stream] [request.json]
quark_maptool simbatch [--stream] [request.json]
quark_maptool simcheck [request.json]
python3 tools/maptool/bot/test_sim.py [path/to/quark_maptool]
```

The request is a JSON object, from the file argument or from stdin. With `--stream`, the request is stdin's first
line and the lines after it are controls.

The answer is always NDJSON: one JSON object per line, flushed as it's written. The last line is `{"type":"end",...}`.

On a bad request the answer is one line `{"error": "..."}` (with `"type":"error"` under `--stream`) and exit code 1.

## The request (`sim` and `simbatch`)

| key | default | what |
|---|---|---|
| `map` | | A map name (`"temple"`, or one of your maps), a path to a `.qmap`, the page's `{name, source, text}`, or the map JSON (`parse`'s form). `text` / `file` / `name` at the top level work too, as in every helper command. |
| `mode` | the map's `for` mode, else `ffa` | `"vs"`, `"bomb"`, ... (or the number) |
| `bots` | 4 bots | `N` bots, or a list of bot specs (below). 1 to 32 bots. |
| `skill` | `"ADVANCED"` | Skill for bots that give none: `0..3` or `BEGINNER` / `INTERMEDIATE` / `ADVANCED` / `EXPERT`. |
| `brains` | | `{name: ".qbrain text"}`: brains the request carries, named by the bots' `brain`. |
| `seconds` (`secs`) | 120 (`simbatch`: 900) | The most game time to run, 0.1..7200 s, counted from the match start. |
| `until` | `"matchEnd"` | `"matchEnd"` stops on the tick the match ends, or at `seconds`. `"seconds"` runs on through the end screen and the next match, up to `seconds` (see "Match end"). |
| `timeLimit` | 0 (the mode's own) | `ServerConfig::timeLimit`, in s: the length of an FFA match (600 by default). |
| `scoreLimit` | 0 (the mode's own) | `ServerConfig::scoreLimit`: KOTH points (100 by default), or the FFA score that ends the match. |
| `seed` | 1 | The match's random seed (spawn picks, bot dice). `simbatch` uses seed, seed+1, ... |
| `tickRate` | 125 | 1000, 500, 250 or 125 Hz. Bots play the same at every rate. |
| `rate` | 30 | `sim`: frames per game second. Each frame is a whole number of ticks; `start.rate` is the rate actually used. |
| `realtime` | true | `sim`: pace the frames at real time × `speed`. false: as fast as it goes. |
| `speed` | 1 | `sim`, with `realtime`: 0.25 is slow motion, 4 is four times real time, 0 is as fast as it goes. |
| `paths` | true | `sim`: send each bot's nav route (`path`). It's sent only when it changes. |
| `weapons` | | A weapon set (`.qweapons` text, or `{text}`): the match plays with it (see "Weapon sets"). |
| `qmode` | | A custom mode text (`.qmode` key=value lines, or `{text}`): its rules; `weapons` goes on top of it. |
| `weaponsAllowed` | every weapon | The weapon pool: `["rocket", "rifle"]` (names, a set's labels, or ids) or a bitmask. |

### A bot

```
{ team: "a" | "b",            // team modes; missing: alternates a, b, a, ... (free for all: ignored)
  skill: 0..3 | "EXPERT"...,  // missing: the request's skill
  name: "ANNA",               // missing: the server's "BOT <NAME>"
  brain: "mybrain",           // missing / "default": the built-in brain
  pos: [x,y,z], yaw: rad | yawDeg: deg,   // a start (missing: the map's spawns, as the game picks them)
  objective: {...} }          // an objective override (missing / "default": none)
```

`yaw` uses the map's convention, the same as a spawn's yaw (radians, the player's yaw).

A placed bot is moved to `pos` the first time it's alive in the match. The round start puts everyone on a spawn
without a death, so this happens on the first tick after the forced start. If the bot respawns before the round
first goes live, it's moved again.

`brain` is looked up in this order:

1. the request's `brains[name]` (a string, or `{text}`);
2. a path to a file;
3. `<user data>/bots/<name>.qbrain` (the player's bots folder);
4. `tools/bots/<name>.qbrain` (the repo, from the working folder or next to the install).

A brain that isn't found is an error. So is one that doesn't compile, on a build that has brains (see "Brains").

### Objective overrides (`objective`)

An override says where the bot goes when it isn't fighting, instead of what its brain picks. While a bot is fighting
(a target in sight), it fights exactly as usual. Once it gets to the place, it stands there: it still aims, shoots
and uses things.

| `type` | fields | what |
|---|---|---|
| `default` | | No override. |
| `goto` | `pos`, `radius` (2) | Walks to `pos` and stands once within `radius` (in xz, and within 2.5 m in height). |
| `hold` | `pos`, `radius` (4) | Walks to `pos` and stands once within 2 m. It fights inside its radius: past half the radius, the fight's moves lose the part that leads farther out, with no dashes or grapple. If it's pushed out of `radius` (by a blast), it steps back in while it fights and walks back to the middle. |
| `route` (`follow`) | `points: [[x,y,z], ...]`, `loop` (false) | Walks to each point in turn; a point is reached within 2 m. With `loop` it starts again after the last point; without it, it holds the last one. After a death it starts again from the first point. |
| `attack` | `objectiveIndex` | Goes for the mode's objective as an attacker would. KOTH: the hill. Points: point `objectiveIndex`, or the one under attack. Race: checkpoint `objectiveIndex`, or its next one. Bomb: site `objectiveIndex` (default 0, the site attackers go for); a planted or dropped bomb and the cart are left to the bot's own code. Modes with no objective (vs, ffa, duel, arena, tourney): enemy spawn `objectiveIndex`, or the nearest one (ffa: the farthest spawn). Hide and seek and tag have no objective: no override. |
| `defend` | `objectiveIndex` | Holds the objective (radius 6 m). KOTH: the hill. Points: the point. Bomb: site `objectiveIndex`, or the site the game gives that bot. Payload: 10 m ahead of the cart. No objective: own spawn `objectiveIndex`, or the nearest one. |

Every type also takes `chase` (default true). With `chase: false` the bot forgets enemies it can't see, so it doesn't
hunt them, doesn't follow leads and doesn't turn to sounds. It only fights what it sees. Use it for a `hold` that
has to stay put.

`{kind: "objective", role: "attack" | "defend", target: {index}}` (the page's mock form) is read as `attack` /
`defend` with `objectiveIndex = target.index`.

**How it's done, without touching the game code.** `BotBrain::script`, the tests' input hook, is set for override
bots on each tick. It calls `BotSim::think`, which clears the hook and calls the game's own `botThink`. So the bot
thinks in the same order, on the same tick, as it would without the override.

For that one call, the things `bots.cpp`'s `objective()` reads are pointed at the override's target, and they're put
back before the bot's command runs:

- `BotBrain::goal` / `goalUntil` (free for all, team vs, duel, arena, tourney, hide and seek, tag: the modes that roam);
- the hill (KOTH);
- the point under attack (points);
- the bot's next checkpoint (race);
- the bomb sites, plus a dropped or planted bomb's position, and the escort target (bomb);
- the payload track (payload).

The bot then walks there with all of its own movement code: nav routes, jumps, ladders, the grapple and the pit
safety. Once it's there, its movement keys are let go. The server, and every other bot, sees the real map and mode
state throughout.

**Limits of overrides:**

- The bot walks there with the game's own nav. Where the nav can't walk a route, an override bot gets stuck where a
  roaming bot would have picked another goal. Examples are two spots on one surface with a wall or a crate between
  them, or a ledge the pit safety refuses.
  - To soften this, an override bot that hasn't got a metre closer in 6 s (not counting fighting or a frozen phase)
    plays on its own for 4 s, then heads for the target again. Its decision shows `(stuck: detour)` meanwhile.
  - Native bots get stuck in the same places. On depot (bomb), team A's carrier stays behind its spawn's front wall
    for the whole round, with no override at all.
- In the roaming modes, a lead (an enemy just seen or heard, `decision: "lead"`) still comes before the override's
  goal, unless `chase: false`.
- A `hold` or `goto` radius under 2 m works as 2 m, because the game re-picks a roam goal within 2 m of it.
- Bomb: a defender holding a site with the bomb planted elsewhere presses use at its post (it does nothing). A
  carrier with a `goto` presses use at the target. Planting only works in a real site.
- Every think of an override bot pays for a few swaps and copies (no allocations).

## `sim`'s lines

```
{"type":"start", map, mode, tickRate, rate, seconds, until, realtime, speed, seed, teams, brainSupport,
   bots:[{id, name, team, skill, brain, brainLabel, brainStatus, start?, startYaw?, objective}],
   weaponSet, weaponLogic, weaponsAllowed, qmode, weaponWarnings?}
{"type":"warning", warning}
{"type":"frame", t, bots:[...], events:[...]}            // every 1/rate s of game time
{"type":"end", t, reason, winner, leader?, bots:[...], result, weapons:[...], perf}
```

The fields of a frame bot:

| field | what |
|---|---|
| `id` | The player slot (as in kill events). |
| `team` | `"a"`, `"b"` or `"spectator"`. |
| `alive` | |
| `pos`, `vel` | [x,y,z]: m (the feet), m/s. Millimetres. |
| `yaw`, `pitch` | rad (0.1 mrad). |
| `health` | 0..100 (the game has no armor). |
| `weapon`, `weaponId` | `"RIFLE"`, ... and the WPN_* number. |
| `state` | `dead`, `fight` (a target in sight), `hunt` (one remembered), `hook` or `move`. |
| `decision` | What chose the current behaviour (below). |
| `target` | The player it's fighting or hunting (-1: none). |
| `goal` | Where its route leads now: the objective, a lead, the override's target. null while dead. |
| `waypoint` | The next point it steers for. null while dead. |
| `path` | Only when it changed since the bot's last frame (and on the first frame): the nav surfaces still to cross, in order. `[]` means none. |
| `onGround`, `crouched`, `hooked` | |
| `kills`, `deaths`, `score`, `damage` | This match so far. |

`decision` is read from the bot's state (the branches of `bots.cpp`):

- `dead`
- `react`: an enemy in view, still reacting
- `fight`, `hunt`, `fight+objective`, `hunt+objective`: bomb, carrying it or defusing, keeps going while fighting
- `objective:checkpoint`, `objective:hill`, `objective:cart`, `objective:point`
- bomb: `objective:site`, `objective:guard site`, `objective:plant`, `objective:escort`, `objective:pick up bomb`,
  `objective:guard bomb`, `objective:defuse`
- hide and seek / tag: `seek`, `flee`, `hide`, `roam`
- modes with no objective: `lead` (heading for someone just seen or heard) or `roam`
- an override that isn't fighting: `override:goto` / `hold` / `route` / `attack` / `defend`, plus ` (there)` once
  it's standing at the target, or ` (stuck: detour)`

A brain from the brains branch makes its own choices. The labels describe the state that results, not the brain's
slots.

An event is `{t, kind, who?, victim?, pos, detail?}`, with t in game seconds since the match start (negative: in the
warmup). The kinds are:

| kind | detail |
|---|---|
| `kill` | `who` killed `victim`; `detail` is the weapon, `fell`, `self` or `world` |
| `spawn` | |
| `round` | "round N end: ..." |
| `match` | "TEAM A wins the match", "BOT ANNA wins the match", ... |
| `point` | |
| `checkpoint`, `finish` | race |
| `score` | "a-b" |
| `bomb` | none, carried, dropped, planted, exploded, defused |
| `hill` | a, b or none |
| `tag` | |
| `phase` | warmup, freeze, live, round end, match end |
| `overtime` | |
| `modifier` | duel's round modifier |

The end line:

- `reason`:
  - `matchEnd`: the tick the match ended, with `until: "matchEnd"`;
  - `time`: `seconds` ran out;
  - `stopped`: a `stop` control;
  - `mapChanged`: a custom mode's map pool moved on.
- `winner` is null if no match ended. Otherwise it's `{team: "a"|"b"|null, player: id|null, text, matchesEnded}`:
  the latest match's. A team mode's player win also gives that player's team.
- `leader` is given when no match ended: `{team}` or `{player}` by score, or null when they're level.
- `bots[]`: `{id, name, team, skill, brain, brainLabel, kills, deaths, score, damage, objTime, shots, hits, aliveTime,
  won}`. `damage` is to enemies, `objTime` is seconds on the objective, and `hits` are shots that damaged an enemy.
- `result`: the server's state, as the `bots` command gives it.
- `perf`: `{wallSeconds, startSeconds, simSeconds, ticks, ticksPerSecond, speedup}`. `speedup` is game seconds per
  wall second spent stepping.

### Controls (`--stream`: stdin lines after the request)

- `pause`, `resume`
- `step`: one frame while paused
- `speed <x>`: 0 means as fast as it goes
- `realtime on` / `realtime off`
- `stop`: an end line with `reason: "stopped"`

An unknown control gets a `warning` line.

## `simbatch`

`simbatch` takes the same request, plus:

| key | default | what |
|---|---|---|
| `matches` | 8 | Matches, with seeds `seed`, `seed+1`, ... (1..10000). |
| `seeds` | | A list of seeds instead. |
| `threads` | the cores, up to 16 | Matches run at once. |
| `progressEvery` | 0.5 | Seconds of wall time between progress lines (0: none). |
| `variants` | | A list of setups (1..1000) played in one batch: each is an object of request keys laid over the request (its `map`, `mode`, `bots`, `brains`, `seed` / `matches` / `seeds` ... replace the request's). |
| `maps` | | With `variants`: `{key: map}`. A variant whose `map` is one of these keys plays that map, so many variants on one map send its text once. |

Matches run unthrottled, and `until` is `matchEnd` unless the request says otherwise. With `"seconds"`, a match's
numbers are the last match's: the game resets them when a new match starts.

```
{"type":"start", map, mode, tickRate, matches, threads, seconds, until, seeds, teams, brainSupport,
   bots:[{index, name, team, skill, brain, objective}], weaponSet, weaponLogic, weaponsAllowed, qmode, weaponWarnings?}
{"type":"match", done, total, index, seed, reason, t, winner, leader?, score:[a,b], wallSeconds, ticks,
   bots:[...the sim end line's bots]}                        // as each match finishes, in finishing order
{"type":"progress", done, total, running, wallSeconds}      // every progressEvery s
{"type":"end", reason: "done"|"stopped", matchesPlayed, errors, timeouts,
   bots:[{index, name, team, skill, brain, brainLabel, brainStatus, objective, ...the totals}],
   brains:[{brain, botMatches, ...the totals}],
   weapons:[{weapon, kills, damage, shots, hits, accuracy, ttk, ttkSamples, deaths, heldSeconds, dps}],
   teamWins:{a, b, draws} | playerWins:[{player, wins}] + draws,
   matches:[{index, seed, reason, t, winner, leader?, score, wallSeconds}],
   perf:{wallSeconds, threads, matchesPerSecond, ticks, ticksPerSecondPerMatch, ticksPerSecondTotal}}
```

- The totals are `matches` (`botMatches` for a brain), `wins`, `winRate`, `kills`, `deaths`, `kd`, `damage`, `score`,
  `objTime`, `shots`, `hits`, `accuracy`, `aliveTime`, `aliveShare`, `killsPerMin` and `damagePerMin`.
- A win is a match the bot's team won (team modes) or the bot won (free for all). A brain's totals add up every bot
  playing it, over every match.
- `timeouts` counts matches that hit `seconds` before their end. They have no winner and aren't counted in
  `teamWins` / `playerWins`; their `leader` says who was ahead.
- The only control is `stop`: no new matches start, and the running ones end within a game second
  (`reason: "stopped"`).

### Variants (several setups in one batch)

With `variants`, every setup's matches go to one pool of threads, setup after setup, so a thread that is done with
one setup starts the next one's matches while the last ones of the first are still running (one `simbatch` per map
leaves threads idle at each map's tail, and starts a process per map). The bot editor sends one batch for all its
maps, sides and sweep values this way. Each variant plays exactly the matches it would play as its own request
(same seeds, same matches: the maps are written once to the batch's folder as `zz_simbatch_<k>`).

- A variant that fails to parse fails the request: `{"error": "variant 2: ..."}`.
- The start line is the first variant's, with `matches` = all of them and `variants: [{variant, map, mode, matches,
  seeds, teams, bots, ...}]`.
- Match lines carry `variant` (its index) and `index` (the match's place in its variant's seeds).
- The end line keeps `reason`, `matchesPlayed`, `errors`, `timeouts`, `weapons` (every match) and `perf` for the whole
  batch, and puts the rest per setup in `variants: [{variant, map, mode, matchesPlayed, errors, timeouts, bots,
  brains, weapons, teamWins | playerWins + draws, matches}]`.
- An older `quark_maptool` ignores `variants` and plays the request alone (its start line has no `variants`): the
  page then runs the other setups one batch each.

## Weapon sets (`weapons`, `qmode`, `weaponsAllowed`)

The weapons editor's keys (tools/maptool/weapon/DESIGN.md section 4: its `wpnfight` is `sim` / `simbatch` with them;
the code is tools/maptool/weapon/wpn_rules.cpp). They make the match a custom mode, as hosting one does:

- `qmode` is read with the game's `CustomRules::fromText` (clamped like a `.qmode` file; a text without a
  `custommode=` line is named `wpn-editor`). Its `mode=` is the match's mode when the request has no `mode`, its
  `timelimit` / `scorelimit` the limits when the request gives none. Its map pool is dropped (the request's map
  plays). A `weaponset=<name>` in it loads `custom_weapons/<name>.qweapons` from your data folder unless `weapons` is
  given.
- `weapons` is compiled by the game (`compileWeaponSet`); a set with errors fails the request with
  `"weapons line L:C: ..."`. Its warnings (clamped values) are the start line's `weaponWarnings`.
- `weaponsAllowed` is `CustomRules::weapons` (the pool: bots pick from it, spawns swap a weapon it doesn't allow).
- The rules go on the server right after BotSim starts it (`ServerConfig::custom`; the server thread applies them with
  `setActiveRules` on its next tick, as `Server::frame` does). The warmup before the forced start played the standard
  rules; the players alive then get the rules' health and an allowed weapon at once.
- The start lines echo `weaponSet` (the set's `weapons "<name>"`, null without one), `weaponLogic` (it has hit / kill
  slots), `weaponsAllowed` (names, null = all) and `qmode` (its name, null without one). The page tells a helper that
  applies sets from an older one by `weaponSet` being there at all.

Per weapon numbers (`sim`'s and `simbatch`'s end lines, `weapons`; the weapons used, every bot together, every match
for `simbatch`): `kills` from the kill events (exact); `shots`, `hits` (shots that damaged an enemy) and `damage`
(to enemies) are each player's counters, sampled between steps and put on the weapon the player held at the previous
sample (every frame for `sim`, every 0.2 s of game time for `simbatch`: a weapon switched between two samples gets the
shots of the other); `accuracy` = hits / shots; `ttk` = the mean time from the start of the victim's fight (its first
damage after 3 s without any, as sampled) to the kill, over `ttkSamples` kills; `deaths` = deaths while holding it;
`heldSeconds` = time alive holding it; `dps` = damage / heldSeconds. Sampling every 0.2 s costs `simbatch` about 5%
(each step reads the game log once).

## `simcheck`

`{text}` or `{brain, brains?}` returns `{brainSupport, ok, label?, error?}`. `error` is `"line:col message"`, as
`sim` would report it.

## Brains

Data-driven bot brains (`src/game/botbrain.h`) are being built on the `bot-brains` branch. `botsim.cpp` includes the
header when `__has_include` finds it, unless `QUARK_NO_BOTBRAIN` is defined.

| | with the header | without it (testing today) |
|---|---|---|
| `start.brainSupport` | true | false |
| what each bot plays | its brain's text, compiled (`compileBrain`) and given to the bot (`setBotBrain`) | the built-in brain |
| `brainLabel` | `name#hash` | the name asked for |
| `brainStatus` | `applied` | `unsupported`, with a `warning` line |
| a brain that doesn't compile | an error before the match starts | |

Bots with no brain report `default`.

The whole seam is one `#if` block, plus `BotSim::start` and `BotSim::checkBrain` (botsim.cpp). It uses
`compileBrain`, `BrainDiag`, `brainLabel` and `setBotBrain`. It was built and linked against the bot-brains branch at 07aa17c
plus that worktree's uncommitted changes (24 Oct): copies of `default.qbrain` with `reactMs` 40 and 1500 were applied
(`brainStatus: applied`, each its own `name#hash`) and played each other in `simbatch` (harbor, 12 matches: the fast
brain won 12, k/d 2.3 against 0.2), and a broken one was refused with its line:col error.

## Match end

When the match ends, the game shows its end screen for 10 s, then starts a new match on the same map. It changes
the map only for a custom mode's map pool (a vote needs human players, and there are none).

`until: "matchEnd"` (the default) stops on the very tick the phase becomes `match end`. The `match` event, `winner`
and the final numbers are the finished match's.

`until: "seconds"` runs on. A new match resets everyone's numbers, so the end line's are the current match's, and
`winner.matchesEnded` counts the matches that ended.

## Threads and determinism

The same request and seed always give the same match: frames, events and numbers (checked by `test_sim.py`).
`simbatch`'s seed-N match is exactly `sim`'s seed-N match.

What makes several servers in one process safe:

- The game keeps its custom rules and `qrand` per thread.
- `bots.cpp` keeps its route budget in thread_locals ("a process can host several servers").
- The nav scratch belongs to each graph.
- The user data folder is global, so `simbatch` prepares one folder before any thread starts
  (`BotSim::prepareSharedDir`: the map is written once) and gives each match its own game log.

`simbatch` runs each match on a fresh thread. Otherwise `bots.cpp`'s thread_local route budget (`g_pathTick` /
`g_pathsThisTick`), left over from the previous match on that thread, could deny a bot its first route search, and a
match's result would depend on which thread ran it.

## Throughput

Linux, 4 cores, release build (`build-sim`), as fast as it goes, 125 Hz, expert bots, 120 s matches, 30 frames/s.
The other worker's build was running at the same time.

| match | ticks/s | real time | NDJSON |
|---|---|---|---|
| temple duel, 2 bots | 107-114k | 855-914x | 3.5 MB |
| harbor ffa, 6 bots | 48-51k | 390-410x | |
| harbor ffa, 8 bots | 26-30k | 210-245x | 12 MB |
| bastion vs, 10 bots | 35k | 280x | |
| lodestone points, 12 bots | 24k | 190x | |
| summit koth, 12 bots | 20k | 165x | |
| midtown points, 16 bots | 12-13k | 100x | 24 MB |
| depot bomb, 6 bots (5 with overrides) | 53k | 430x | |

- Starting a match (map, server, nav graph, 1 s of warmup) takes 0.01-0.05 s.
- Writing the frames costs about as much again as stepping at 30 frames/s with 8+ bots. With `realtime` at
  speed 1, that's about 100 KB/s.
- `simbatch`, harbor ffa with 8 bots, 60 s matches, 16 matches: 4.0 matches/s on 1 thread, 8.0 on 2 and 12.1 on 4.
  That's 33k ticks/s per match on 1-2 threads and 28k on 4.

## What would need game code

- **A public `Server::stepTick()`.** `simulate()` and `sendSnapshots()` are reached through the explicit-instantiation
  trick (botsim.cpp). The map editor's README says the same.
- **An objective override slot.** The `BotBrain::script` hook and the patching of what `objective()` reads would go
  away with a real "goal override" field in `BotBrain`, read by `objective()` before the mode's switch, or with a
  brain slot `objective.override` (a `goto <pos>` action) on the brains branch. With it, the bot would also re-plan
  around unreachable goals the way it does for its own.
- **Stats the game keeps.** Hits and damage per victim, and time per objective are only in the verbose log
  (`LOG_VERBOSE` writes every tick, too slow for this). A per-player `hitsTaken` / `damageTaken` and per-match
  objective events (captures, plants, defuses with the player) would make the batch report richer.
- **A match-end hook.** The winner is parsed from the game log's "match end: ..." line. A
  `Server::onMatchEnd(winnerTeam, winnerPlayer)` callback (or a `ModeState` field) would be exact.
