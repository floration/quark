# Physics editor: native commands

The physics editor's half of `quark_maptool` (the map editor's helper, `tools/map_editor/native/README.md`): test
custom movement physics without opening the game. CMake globs this folder into quark_maptool, next to the map and
bot editors' commands; nothing else is needed to register them. The interface the page uses is `../INTERFACE.md`.

```
quark_maptool physparams  [request.json]     every movement setting, its range, unit, base and C++ constant
quark_maptool physroom                       the built-in test room (.qmap text) and its parts
quark_maptool physpresets                    the built-in scenarios ({"run": true}: with standard results)
quark_maptool physsim     [request.json]     runs a scenario under one or more physics sets
quark_maptool physdrive   --stream           drive the real pmove line by line (WASD + mouse from the page)
quark_maptool physexport  [request.json]     a physics set as .qmode text and as a list of C++ constants
quark_maptool physexpr    [request.json]     a set's movement equations: errors, static / dynamic, deps, sampled curves
```

Every command runs the game's own `pmove()` (src/game/pmove.cpp) unchanged, one 1 ms command at a time like the
1000 Hz server, with the physics set applied through `setActiveRules(CustomRules)` (per thread). Values go through
`CustomRules::fromText`, so they are clamped exactly as a custom mode file is. Movement is the owner's: these
commands only drive the constants, they never change them.

Examples:

```
$ echo '{"physics":{"gravity":0.5},"preset":"standing_jump","compare":["standard"]}' | quark_maptool physsim
{"scenario":{...},"map":"room:flat","runs":[{"label":"physics","metrics":{"jumpHeight":2.4452,"airTime":1.326,...},
 "events":[{"t":0.001,"kind":"jump",...},{"t":1.327,"kind":"land","airTime":1.326,"height":2.4452,...}],"trace":[...]},
 {"label":"standard","metrics":{"jumpHeight":1.2208,"airTime":0.659,...},...}],"simMs":2.5,"ms":2.9}

$ echo '{"physics":{"gravity":0.8,"move":{"dash_charges":1.5}}}' | quark_maptool physexport
{"qmode":"custommode=physics\nmode=ffa\n...","cpp":"src/game/pmove.cpp: kGravity = 22.0f -> 17.6f   // gravity x0.8\n
src/game/pmove.h: kDashCharges = 2 -> 3   // dash_charges x1.5\n...","changes":[...]}

$ quark_maptool physdrive --stream
{"map":"room:flat"}
{"type":"start","t":0,"pos":[0,0,0],"vel":[0,0,0],...,"onGround":true,"dashCharges":2,...,"map":"room:flat","yaw":-90,"physics":{...}}
{"cmd":[{"forward":127,"yaw":-90,"msec":16}]}
{"type":"state","t":0.016,"pos":[0.019,0,0],"vel":[2.221,0,0],"speed":2.221,"hspeed":2.221,"flags":1,...,"events":[],"us":64.4}
stop
{"type":"done","t":0.016}
```

Movement equations (v2, `../INTERFACE.md`): any physics set may carry `"expr": {"<key>": "<expression>"}` (or
`expr.<key>=` lines in its qmode). They go into `CustomRules::exprs` through fromText like every value, and
`setActiveRules` builds them exactly as the game does (src/game/moveexpr.h: static ones resolved, dynamic ones compiled
and run by pmove every 1 ms step), so physsim and physdrive play them. Every answer's `physics` lists them
(`expr`, `exprs`: ok / error / dynamic / deps / inputs / value).

```
$ echo '{"expr":{"friction":"curve(p.hspeed, 2, 12, 2, 0.6, \"inout\")","air_cap":"sped"},"samples":5}' | quark_maptool physexpr
{"items":[{"key":"friction","ok":true,"dynamic":true,"deps":[],"inputs":["p.hspeed"],"value":2,"absolute":14,...,
  "samples":[{"input":"p.hspeed","unit":"m/s","x":[0,12.5,25,37.5,50],"y":[2,0.6,0.6,0.6,0.6],"abs":[14,4.2,...]}]},
 {"key":"air_cap","ok":false,"error":"col 1: unknown name 'sped' (a setting key, p.<input>, base.<key>, pi, ...)",...}],
 "order":["friction"],"dynamic":1,"ops":2,...}

$ echo '{"physics":{"expr":{"friction":"curve(p.hspeed, 2, 9.5, 2, 1)"}},"preset":"stop","compare":["standard"]}' | quark_maptool physsim
... stopDistance 0.8077 m in 0.213 s (standard 1.2058 m in 0.364 s)
```

## The test room (testroom.cpp)

Made in code as `.qmap` text (the game's format, `version 2`) and parsed with `Map::parse`, so collision, ladders and
the step-up behave as in any map. Lanes side by side along z, every floor's top at y = 0, faced along +x (yaw -90):

| part | z | what |
|---|---|---|
| flat | -60..60 | 440 x 120 m of floor: jumps, run-up, stopping, dashes, bhop chains |
| wall | 70..90 | a 6 m wall across the lane at x 20 (start 1.6 m from it) |
| ladder | 100..120 | an 8 m tower at x 20..30, a ladder up its -x face (the volume reaches 9 m) |
| drop | 130..150 | a 20 m ledge ending at x 10 |
| gaps | 160..180 | platforms over a pit 4 m down, gaps 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16 m (6 m platforms) |
| stairs | 190..210 | 0.5 m risers to 3 m, a landing, 0.75 m risers (over the 0.55 m step-up) to 6 m |
| grapple | 220..260 | a ceiling at 12 m over a 140 m floor |

## Presets with the standard physics (1.18.10)

`echo '{"run":true}' | quark_maptool physpresets`:

| preset | result |
|---|---|
| standing_jump | jumpHeight 1.2208 m, airTime 0.659 s (2v/g = 0.667; the ground probe lands 5 cm early) |
| running_jump | jumpDistance 6.27 m, 9.5 m/s |
| run_up | 9.5 m/s, 99% of it in 0.082 s, 90% in 0.072 s |
| stop | from 9.5 m/s: 1.21 m in 0.364 s |
| bhop_plain (W only) | 9.5 m/s at every landing |
| bhop_strafe (strafe bot) | landings 17.6, 23.3, 27.8, 31.7, 35.2, 38.3, 41.3, 44.0, 46.5 m/s; 48.6 m/s after 7 s |
| dash | 2.84 m in 0.119 s, leaves at 11 m/s |
| dash_jump | 24 m/s, 15.8 m, 1.21 m high |
| super_dash (45 deg down, from a run) | 14.5 m/s (9.5 + 5), a 0.78 m hop, 7.6 m |
| dash_wall | bounces off at 16 m/s |
| grapple_swing | pulled to 24 m/s (its top speed), lands 42 m away |
| ladder_climb | 1.25 s through the 9 m volume (the step-up skips the last lip: 7.2 m/s against a 6.75 m/s climb) |
| stairs | climbs to 3 m, stopped by the 0.75 m risers (step x1.4 climbs to 6 m) |
| max_gap | clears the 7 m gap (a 6.27 m jump + the 0.8 m hull), not the 8 m |
| fall | 20 m in 1.35 s, lands at 29.6 m/s (no fall damage in quark) |

## Timings

Linux, 4 cores, release build, whole process runs:

- physsim, a 10 s scenario (run, strafe-bot bhops, a dash jump) x 3 physics sets, 10 ms trace: 26 ms (7 ms of it
  pmove; one thread per set), 516 KB of JSON.
- physpresets with `run: true` (all 15 presets): 14 ms.
- physdrive: a 16 ms batch of commands is answered in about 50 us of compute.
- physparams, physroom, physexport: under 1 ms.
- physexpr, three equations sampled 101 points each: 0.3 ms of compute, 3.5 ms whole process.

## Code

| file | what |
|---|---|
| `physics.h` | the shared declarations (namespace `phys`: no clashes with the other editors' files) |
| `physics.cpp` | physics sets (JSON / .qmode -> CustomRules through fromText, equations included), `physparams` (with the equation language), `physexport` and its table of C++ constants |
| `expr.cpp` | `physexpr`: a set's equations built by the game's moveexpr, each dynamic one sampled over its inputs |
| `testroom.cpp` | the test room, `physroom`, the request's map (`room`, `room:<part>`, a name, `{text|file|name|map}`) |
| `sim.cpp` | the scenario runner, the strafe bot, the events, the metrics, the presets, `physsim`, `physpresets`, `physdrive` |
| `test_phys.py` | runs every command and checks the numbers (python3 stdlib): `python3 tools/physics_editor/native/test_phys.py [quark_maptool]` |

### What is copied from the game (and checked)

The helper never reads the sources at run time. A few numbers it needs are not public, so they are written here, and
`test_phys.py` checks each against src/game/pmove.cpp / pmove.h:

- physexport's table: each multiplier's constant names, files and base values (`kConsts`, physics.cpp).
- the strafe bot (sim.cpp `strafeYaw`): pmove's A/D factors `kStrafeCapAD` 1.1 and `kStrafeAccelAD` 1.15, the strafe
  boost's ramp from `cross` to 1.25 x cross, and the x10 on the air acceleration. The bases themselves come from
  `moveParamBase`. The `"search"` strafe bot needs none of them, and the test checks that both agree.
- the events: dash, dash jump, super dash and jump are what pmove says each 1 ms call did (`MoveState::events`, ME_DASH /
  ME_SUPER / ME_HYPER / ME_JUMP, the same flags the game's sounds read), so a held dash that chains its charges and a
  jump tapped just before landing (1.18.20) show up; landings, ladders, walls, steps and the grapple come from
  comparing the states either side of the ms.

The strafe bot's computed angle reads the active set's tuning for the state it aims from (`stepTuning`, pmove.h), so
with dynamic equations on the air settings it aims by the values pmove's next step uses, as `"search"` does.

The game's bots (src/game/bots.cpp) air-strafe with their own copy of the angle (acos(2.3 / speed)); it is inline in
their think function and can't be called from here.
