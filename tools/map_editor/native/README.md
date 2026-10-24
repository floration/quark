# quark_maptool

The map editor's native helper: every map-format, physics and bot answer the editor page needs. It is built from
the game's own code (`quark_core`: `src/game` map, pmove, bots, server) and `tests/walkgrid.h`. It is built with the
tests (`QUARK_BUILD_TESTS`, the default), takes no options, and builds on Linux and Windows (MinGW).
On Windows its arguments, the request's `file` paths and its temp files are UTF-8 through the wide APIs (a folder like
`C:\Users\Zoë\Desktop\Quark\map-editor` works), stdin and stdout are binary (answers and NDJSON lines end in `\n`),
and a control line may end in `\r\n`.

```
quark_maptool <command> [--stream] [request.json]
```

The request is a JSON object, read from the file argument or from stdin. The answer is one JSON object on stdout.
On an error the answer is `{"error": "..."}` with exit code 1. With `--stream`, the request is stdin's first line,
the rest of stdin carries controls, and the answer is NDJSON (one object per line). The tool never opens a
socket for HTTP: `serve.py` runs it once per request.

`quark_maptool help` lists the commands, the custom test kinds (with their fields) and the player hull.

## The map in a request

Every command that needs a map takes it in one of these keys, in this order of priority:

| key | what |
|---|---|
| `text` | the `.qmap` text |
| `file` | a path to a `.qmap` |
| `name` | a map the game finds by name (`maps/<name>.qmap`, user maps, embedded maps) |
| `map` | the map's JSON form (as `parse` returns it) |

`mapName` (or `name`, when `text` or `map` is given) names the map in answers.

## Commands

### parse

`{text|file|name|map, withText?}` returns the map's JSON form. The shape is in `../INTERFACE.md`. On top of it the
answer carries:

- `humanMade`: a `handmade` line, or a map not written by make_maps.py;
- `modesSupported` and `modesPossible` (gamemodes.h);
- `killY` and `payloadLength`;
- `bounds`, `warnings` and `ms`;
- `player`: `halfWidth` 0.4, `height` 1.8, `crouchHeight` 1.2, `eye`, `crouchEye`, `stepUp` 0.55,
  `jumpHeight` 1.22, `runSpeed` 9.5, `headFraction` 0.2, `headWidthFraction` 0.45, `headBox` (standing at the
  origin, body scale 1), `grappleRange` and `bodyScales`;
- `serialized`, with `withText: true`: `Map::serialize` of the parsed map.

### serialize

The map JSON (or `{map: ...}`) returns `{text, valid, parseError? | warnings}`. `text` is `Map::serialize` of the
map. `valid` says whether the game's parser reads `text` back. Parsing a map and serializing it again gives the
same text as `Map::serialize`, byte for byte, on all 21 maps.

`-0` is read and written as `-0` (the JSON reader takes `-0` as the number; floats are written with their sign).
JS's `JSON.stringify` and Python's `json` turn `-0` into `0`, so the page sends its requests through `toJson`
(js/api.js), which writes `-0`, and serve.py passes request bodies on as they came.

### check

`{map, only?: [ids], outOfBounds?: bool}` runs the map checks `quark_tests` runs. A check that failed in
`quark_tests` fails here too: all 21 shipped maps pass both.

The answer is `{map, humanMade, checks:[...], pass, failed:[ids], ms}`. Each check is
`{id, name, pass, skipped?, detail, where:[[x,y,z]...], items:[...], ms}`. `detail` is a summary plus the first
12 items. `items` lists every miss, with its own `where`.

The ids, in the order they run:

- `preferred_mode`
- `spawns_in_walls`
- `walk_routes`: walk grid, every spawn to every objective
- `nav_routes`: the bots' nav graph
- `ladders`: pmove climbs each one
- `stairs_walkable`, `stairs_jump`, `stairs_cut`
- `spawn_pools`, `spawn_facing`, `spawn_apart`, `spawn_sight`
- `cart_sweep`
- `ranked_mirror`
- `out_of_bounds`: the map report's tech-reach measure; it is slow, so `outOfBounds: false` skips it

### metrics

`{map, mode?, paths? (true), numbers? (true)}` returns the measurement half of the map report (07ef254
tests/mapreport.cpp). It has no pass/fail bar.

```
{ map, mode, modeSupported,
  objectives: [{name, kind, pos, attackersAre, defendersAre, attackersM, defendersM,
                attackers (s), defenders (s), diff (s, attackers minus defenders)}],
  routes: [{from (spawn index), team, role, to, objective (= objectives[].name), metres, seconds,
            path:[[x,y,z]...], toSpawn?}],
  spawns: {pool, closestPair, pairsInSight},
  reach: {tiers:[{speed, runUp, flatReach, dropReach, rise, pullExit}], buildTime, maxReach, walkGrid},
  numbers: {decor, layout, movement, modes, leads, fairness, worstFair, modeFits, features, setting, exempt,
            scores: {decor, layout, movement, balance, modeFit, ... with their parts}},
  ms }
```

The objectives depend on the mode:

| mode | objectives |
|---|---|
| bomb | each site: A attacks, B defends |
| points | each point, from the staged spawns |
| koth | the hill |
| payload | A to the cart's start, B to its halfway point |
| npayload | the cart |
| vs / duel / 1v1 / tourney | "enemy spawns": the nearest enemy spawn |
| race | each checkpoint, cumulative |

FFA, tag and hide have no objectives: they return routes and numbers only. The numbers match the official report
(lodestone: decor 7.4, layout 7.6, movement 9.4, mode 8.6, balance 8.7). Creativity and originality compare maps
with each other, so only their inputs are given.

### route

`{map, from:[x,y,z], to:[x,y,z], radius? (0.6), nav?: bool}` returns one walk-grid route (the player's hull, no
movement tech):

```
{reachable, metres|null, seconds|null, path:[[x,y,z]...], fromStands, toStands, nav?: {metres, path}, ms}
```

Each end snaps to the standing spot within `radius` of it. `fromStands` / `toStands` say whether one was found.
`path` is simplified (Douglas-Peucker, 0.12 m).

### custom

`{map, tests:[{kind, name?, ...fields}]}` returns:

```
{results:[{name, kind, pass, value, detail, where, paths?, ms}], pass, failed, ms}
```

`help`'s `customKinds` lists every kind as `{kind, title, desc, fields:[{name, type, default, desc}]}`. A field's
type is `place`, `seconds`, `mode`, `team`, `objective`, `string`, `bool` or `number`.

A place is one of:

- `[x,y,z]`
- `{spawn:i}`
- `{team:'a'}`: each of that team's spawns
- `{objective:'hill'|'site 1'|'bomb site 1'|'point A'|'checkpoint 2'|'cart'|'cart start'|'cart end'}`
- `{box:{min,max}}`

| kind | fields | value |
|---|---|---|
| `route_time` | from, to, max?, min?, via (walk\|nav) | the slowest route's seconds |
| `reachable` | from, to, expect (true), via | how many routes were reached |
| `objective_arrival_diff` | mode?, objective?, within (1) | the worst \|diff\| |
| `ladder_climbable` | ladder? | the ladders climbed |
| `max_fall` | from?, to?, max | the biggest drop (m) |
| `line_of_sight` | a, b, expect (false), eye (1.6) | the pairs that see each other |
| `spawn_clearance` | team?, min (1.0), headroom? | the closest wall (m, from the hull's side) |
| `check` | check (an id) | that check, as a test |

`spawn_clearance`'s default of 1.0 m fails several shipped maps: their spawns sit against walls on purpose. Give
it a lower `min` for a real test.

### bots

The request:

```
{map, mode?, bots?: N | [spec...], placed?: [spec...], seconds? (30, 0.1..3600), skill? (advanced),
 seed? (1), tickRate? (125: 1000|500|250|125), rate? (10 Hz), speed?, paths?}
spec: {team: 'a'|'b', skill?: 0..3|'beginner'..'expert', pos?: [x,y,z], yaw?: radians, name?, brain?, ...}
```

This runs a headless match with the game's own server and bots, tick by tick:

- With no `--stream` it runs as fast as the CPU goes: 300-450x real time (depot, 6 expert bots, 30 s: 69 ms).
- The same seed gives the same match.
- `placed` bots (and specs with a `pos`) are moved to their start the first time they are alive after the match
  starts.
- `brain` and any other spec fields are stored and echoed, not used. They are kept for the bot editor.

Without `--stream`, the answer is:

```
{ map, mode, rate, tickRate, seed, seconds,
  bots: [{id, name, team, skill, start?, startYaw?, brain?, trace: [[t,x,y,z,yaw,state]...]}],
  traceFields: ["t","x","y","z","yaw","state"],
  events: [{t, kind, who, victim?, pos, detail?}],
  result: {score, phase, round, ..., players:[{id, kills, deaths, score}], mapChanged},
  ms: {sim, total, speedup} }
```

`state` is one of `dead`, `fight` (a target in sight), `hunt` (a remembered target), `hook` or `move`. Event kinds
are kill, spawn, round, match, point, checkpoint, finish, score, bomb, hill, tag, phase and overtime. Kill, round,
match, point and tag events come from the game log. The rest come from state changes between ticks.

`viaFrame: true` is a cross-check. It runs the server's own `frame()` loop on a clock at 20x real time, instead of
stepping `simulate()`. On depot it gave the same positions as stepping, to the millimetre.

`--stream` sends one JSON line per message, paced at `speed`x real time (default 1; 0 runs as fast as it goes):

```
{"type":"start", map, mode, tickRate, rate, seconds, speed, seed, bots:[{id,name,team,skill,start?,startYaw?,brain?}]}
{"type":"event", t, kind, who, victim?, pos, detail?}
{"type":"sample", t, bots:[{id, pos, yaw, pitch, vel, health, weapon, weaponName, alive, state, target,
                            onGround, crouched, goal, waypoint, path?:[[x,y,z]...], kills, deaths}]}
{"type":"warning", warning}
{"type":"done", t, result:{..., stopped, mapChanged, wallSeconds}}
{"type":"error", error}
```

A sample goes out every 1/`rate` s of game time. `paths` (true when streaming) adds the nav surfaces each bot
still has to cross.

The controls are lines on stdin, after the request line:

- `pause`
- `resume`
- `speed <x>`
- `step`: one sample, while paused
- `stop`

### walkverify

`{map}` is a self-check. It builds the walk grid two ways, threaded (walkfast.cpp) and with tests/walkgrid.h's
own build, and compares them: spots, links, gaps and ladders, with timings. They are identical on temple,
lodestone and midtown.

## Timings

Linux, 4 cores, release build. Each figure is a whole process run, including the map load.

| map | parse | check | metrics | route | custom (3 tests) | bots (4 bots, 10 s) |
|---|---|---|---|---|---|---|
| lodestone | 0.07 s | 1.34 s | 1.72 s | 1.41 s | 1.06 s | 0.05 s |
| midtown | 0.08 s | 1.31 s | 1.79 s | 1.33 s | 1.06 s | 0.05 s |
| gantry | 0.01 s | 1.10 s | 0.96 s | 0.18 s | 0.18 s | 0.03 s |
| temple | 0.01 s | 0.30 s | 0.39 s | 0.07 s | 0.08 s | 0.03 s |
| others | 0.01 s | 0.06-0.8 s | 0.07-0.9 s | 0.04-0.2 s | 0.04-0.2 s | 0.03 s |

The figures depend on these speed-ups:

- **The walk grid build is threaded.** It is about 0.8 s of lodestone's time (tests/walkgrid.h's own build takes
  2.3 s).
- **The movement envelope is cached.** It depends only on the movement code. The cache is
  `<temp>/quark_maptool_envelope_<exe size>_<exe mtime>.bin`, so a rebuild invalidates it.
  `QUARK_MAPTOOL_NO_CACHE=1` turns it off; the first run then takes about 1.5 s longer.
- **The tech graph, walk loops and sight rays are threaded.** The results are merged in item order, so the numbers
  are the same as single-threaded.

`maptoolThreads()` uses all the cores, up to 16.

## Code

| file | what |
|---|---|
| `main.cpp` | the CLI: read the request, dispatch, write the answer |
| `maptool.h`, `common.cpp` | the command registry, `CmdCtx`, the map JSON (`mapToJson`, `mapFromJson`, `loadRequestMap`), JSON helpers (`jvec`, `toVec`, ...) |
| `json.h`, `json.cpp` | a small JSON value: ordered objects; floats written with the shortest digits that read back as the same float |
| `mapjson.cpp` | `parse`, `serialize` |
| `analysis.h`, `measure.cpp` | `Analysis` (the map plus its walk grid and nav graphs, built on demand), `measureJson`, `outOfBounds`, routes |
| `measure_impl.h` | the map report's measurement code (copied) |
| `walkfast.h`, `walkfast.cpp` | `parallelFor`, and the threaded walk grid build |
| `metrics.cpp`, `checks.cpp`, `custom.cpp`, `bots_cmd.cpp` | the commands |
| `botsim.h`, `botsim.cpp` | the bot simulation module |

### Adding a command

Any `.cpp` in this folder or in `tools/bot_editor/native/` is linked in (CMake globs both). A command is one line:

```cpp
#include "maptool.h"
static int runSim(const Json& req, Json& out, CmdCtx& ctx) {
    Map m; std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    ...
    out = Json::object(); out.set("x", 1);
    return 0;
}
MAPTOOL_COMMAND("sim", runSim, "{map, ...} -> ...");
```

Inside a command:

- `ctx.stream` is set by `--stream`.
- `ctx.emit(json)` writes one NDJSON line and flushes it.
- `ctx.control` is stdin, after the request line.
- `ctx.hasFlag("--x")` tests any other flag.

A streaming command emits its own lines and leaves `out` an empty object. Use `Json::parse(text)` and
`json.dump()` for JSON, and `mapToJson` / `mapFromJson` for maps.

### The botsim module

`botsim.h` holds the bot simulation. The bot editor takes it over from here.

```cpp
BotSim sim;
BotSimConfig cfg;              // mapText | mapName, mode, tickRate, seed, bots, forceStart, warmup, viaFrame
cfg.mapText = text; cfg.mode = MODE_BOMB;
BotSpec b; b.team = TEAM_A; b.skill = BOT_EXPERT; b.placed = true; b.pos = {0, 0, 10}; b.yaw = 0;
b.brain = "brains/x.json";     // stored only; b.extra holds any other per-bot JSON (objective overrides, ...)
cfg.bots.push_back(b);
std::string err;
if (!sim.start(cfg, &err)) ...;
std::vector<BotState> st;
while (sim.time() < 60 && !sim.mapChanged()) {
    sim.step(sim.ticksFor(0.1));                 // server ticks
    sim.sample(st);                              // BotState: pos, vel, yaw, pitch, health, weapon, alive, state,
                                                 //   target, goal, waypoint, path, kills, deaths, onGround, ...
    for (auto& e : sim.takeEvents()) ...;        // BotEvent: t, kind, who, victim, pos, detail; e.json()
}
Json result = sim.summary();
sim.stop();                                      // (the destructor stops it too)
```

What `start()` does:

1. It points the game's user data folder at a fresh temporary folder.
2. It writes the map there as `maps/zz_maptool.qmap`.
3. It turns off the server's rate check and seeds `qsrand`.
4. It creates a real `Server` (port 0, LAN only, a simple game log), waits for the nav graph and adds the bots.
5. It runs `warmup` seconds, then `forcestart`.

`step()` runs `Server::simulate()` for each tick, then advances the tick count and the clock, and runs the
snapshot pass at the snapshot rate, exactly as `frame()` does. No sockets are read and no real-time clock is
involved. `server()` gives the real `Server` for anything the API doesn't cover.

Several BotSims can run at once, one per thread (`simbatch` does): the game's user data folder is one global, so
each gets the same `sharedDataDir`, made once by `BotSim::prepareSharedDir()` before the threads start, and runs
each match on a fresh thread (the game keeps `qrand` and its custom rules per thread). A single BotSim with no
`sharedDataDir` makes a folder of its own. `tools/bot_editor/native/SIM.md` has the details.

## Copied logic (dedupe later)

Game code and the tests were read and linked, never edited. Where the logic was static, inside a test, or tangled
with the test framework, it was copied here:

| copy | from | changes |
|---|---|---|
| `measure_impl.h` | tests/mapreport.cpp lines 1-1410: `namespace mr` (metrics, MoveEnvelope, TechGraph, walkLoops, ...), without the cams block | laps for timings; envelope cache; threaded speed build-up, take-offs, pulls, TechGraph samples, walkLoops placements and sight rays (merged in order) |
| `measure.cpp`: score formulas, `outOfBounds` | tests/mapreport.cpp `mapReport()` | only the per-map scores; the out-of-bounds measure as a function |
| `walkfast.cpp`: `buildWalkGrid` | tests/walkgrid.h `WalkGrid::build` | threaded per row and per spot; fills the same struct; `walkverify` proves the result identical. tests/walkgrid.h could take this as is. |
| `checks.cpp` | tests/tests.cpp `testAllMaps` (spawns, modes, routes), `testLadders`, `testStairs`, `testSpawnPools`, `testSpawnSanity`, `testCartSweep`, `testMaps` (ranked mirror) | returns items with positions instead of failing; ladders and stairs threaded |
| `metrics.cpp`: objective placement | tests/mapreport.cpp (pointDist's staged spawns, the cart's halfway point, ...) | as JSON, per objective |
| `botsim.cpp`: `Server::simulate` access | `Server::frame()`'s tick body | `simulate()` and `sendSnapshots()` are private. They are reached with the explicit-instantiation member-pointer trick (`SimTag`/`SnapTag`). A public `Server::stepTick()` in src/game would remove it. |
