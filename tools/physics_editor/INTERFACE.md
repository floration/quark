# Physics editor: helper interface (v2)

The page (web/) talks to `serve.py` (127.0.0.1:8772), which runs the game's helper `quark_maptool` once per request
(JSON on stdin, one JSON answer on stdout; `--stream` = NDJSON, controls on stdin), exactly like the map and bot
editors. The physics commands live in `tools/physics_editor/native/*.cpp`, linked into quark_maptool by the CMake
glob. They run the game's real `pmove()` (src/game/pmove.cpp) with the physics applied through
`setActiveRules(CustomRules)` (src/game/customrules.h): no game code is copied or changed.

## Physics = a custom mode's movement settings

A physics set is the movement part of a `CustomRules` (.qmode text): `gravity`, `speed`, `jump`, `dash`, `grapple`
and every `MV_*` multiplier (`moveParamKey(i)` keys). So any physics set can be saved as
`<data dir>/custom_modes/<name>.qmode` and played in the game right away (Host > Custom modes). Values are
multipliers (1 = standard); the page also shows the absolute value (base x multiplier, `moveParamBase`, `moveParamUnit`).

JSON form of a physics set: `{"gravity":1,"speed":1,"jump":1,"dash":true,"grapple":true,"move":{"<key>":1,...},
"expr":{"<key>":"<expression>",...}}` (missing keys = 1 / true; `expr`: the movement equations, v2 below; an empty
expression removes that key's one). Requests may instead give `"qmode": "<.qmode text>"` (parsed with CustomRules::fromText;
non-movement lines are kept by the page, ignored by the helper).

Clamps: every value goes through `CustomRules::fromText`, so it is clamped exactly as the game clamps a custom mode
file (gravity 0.1..3, speed 0.25..3, jump 0.25..3, every move key 0.1..5; physparams reports them, read from fromText
itself). A physics set may also be the string `"standard"` or a .qmode text, and an object may carry `"qmode"` (its
values first, the object's keys over them) and `"name"`. Answers echo each set as `physics:
{name, gravity, speed, jump, dash, grapple, move:{every key}, expr:{key: text}, exprs:[item...], changed:[keys],
standard}` and list `clamped: [keys]` when a value was out of range (a key that has an equation is never listed:
the equation sets it). `exprs` items are physexpr's (below), without samples; a set with any equation is not
`standard`.

**Units** (v1.1): metres, seconds, m/s. **Angles are degrees** everywhere in requests and answers: yaw 0 faces -z,
-90 faces +x (the game's forward = (-sin yaw, 0, -cos yaw)); pitch > 0 looks up, clamped to +-89. Times `t` are
seconds (whole ms). Positions are the feet (bottom centre of the hull).

## Commands

The native side: `native/README.md` (code, tests: `native/test_phys.py`).

### physparams `{}`
```
{params:[{key, label, unit, base, group, min, max, step: 0.01, dragStep: 0.05, default: 1,
          constants:[{file, name, base}], cpp: "<main constant>", note?, doc? (= note), inactive?}],
 top:[{key: "gravity"|"speed"|"jump", label, unit, base, group: "top", min, max, step, default, constants, cpp, note?}],
 toggles:[{key: "dash"|"grapple", default: true}],
 player:{halfWidth, height, crouchHeight, eye, crouchEye, stepUp},
 constants:{gravity, runSpeed, jumpSpeed, standingJumpHeight, dashCharges, maxDashCharges, dashCooldown, dashRecharge,
            grappleRange, hookCooldown, moveStep, killPlaneY, buttons:{jump, crouch, dash, hook}},
 expr:{inputs:[{name: "p.speed", unit, min, max, rest, desc, flag?, integer?}], functions:[{name, sig, desc, minArgs,
       maxArgs}], ease:[mode names], operators:[...], names:[...], maxLength: 512, value, line},
 moveStep: 0.001, angles: "...", version, ms}
```
`expr` is the equation language (v2 below), read from the game's own tables (src/game/moveexpr.cpp): an input's
min..max is the range the page plots by default, `rest` its value standing still on the floor.
Groups: ground, air, dash, super dash, grapple, ladder, general (the bhop bounce). min/max are what fromText allows.
`constants` names every C++ constant the multiplier scales (dash_speed also scales kDashBounceSpeed, dash_cooldown
kDashRecharge, super_dash_jump kSuperDashJumpMax, speed kAirControlFrom and kCrouchSpeed). `crouch_speed` is
`inactive`: crouching was removed from the game (pmove ignores BTN_CROUCH).

### physsim `{physics|qmode, scenario | preset, map?, compare?: [physics...], sampleMs?: 10}`
`scenario` is a scenario object, or a preset id (as is `preset`): then the preset's scenario, and its map unless
`map` is given. `map`: absent / `"room"` / `"room:<part>"` = the built-in test room (one map: the part only names
where a preset starts), a map name (`"temple"`), or `{text|file|name|map}` like the map editor's requests. Each
physics set runs on its own thread (setActiveRules is per thread).

Answer:
```
{scenario: <the scenario run, as JSON>, map: "<label>",
 runs:[{label, physics, clamped?, metrics:{...}, events:[...],
        trace:[{t, pos, vel, speed, hspeed, flags, dashCharges, dashTimer, onGround, yaw}]}],
 simMs, ms}
```
The trace has a sample every `sampleMs` (1..10000), at every event, and at the end. `label`: the set's name
(`"standard"` for a "standard" compare entry, `"compare <i>"` for an unnamed one).

A scenario: `{name, start:{pos:[x,y,z], yaw, pitch, vel?:[x,y,z], onGround? (found by probing the floor),
dashCharges?}, durationMs (1..120000), steps:[...]}`. A step: `{atMs, forward:-127..127, side, yaw?, pitch?,
yawRate? (deg/s), pitchRate?, jump?, crouch?, dash?, hook?, strafeBot?, strafeSwitch?, strafeSwing?, jumpAtEdge?}`.
A step holds until the next one (yaw / pitch are set when it starts, then the rates turn them). Buttons are held
while true (a "press" = true then false; holding jump bunny-hops). Commands are 1 ms (msec=1).
- `strafeBot: true` steers yaw every ms to the angle that gains the most speed with the step's keys (Quake's optimal
  angle from pmove's air acceleration, with the active set's numbers). `"search"` instead tries angles through pmove
  itself each ms (slow; a cross-check: the two agree within 0.1%).
- `strafeSwitch: true` weaves: swaps A / D when the velocity has swung `strafeSwing` (default 25) degrees past the
  heading the step started facing, so the course stays straight. `"land"`: swap at every landing. Without it the
  bot turns one way (circles).
- `jumpAtEdge: true` presses jump on the last ms the floor holds you (running jumps off edges, the gap course).

Events (each `{t, kind, pos, hspeed, speed, ...}`):

| kind | extra fields | when |
|---|---|---|
| jump | vy, bhop (landed <= 2 ms before), bounce? (a bhop off a hard landing, same ms) | off the floor by jumping |
| land | impact (m/s down), from (jump, dashJump, superDash, fall, launch, hook, ladder, start), airTime, height (apex - takeoff), drop (apex - landing), distance (horizontal) | on the floor again |
| dash | charges (left), dir [x, up, z], aimedDown | a dash starts |
| dashEnd | | its burst ran out |
| dashJump / superDash | vy, charges | a jump during a dash on the floor (aimed down: super dash) |
| bounce | bounces, charges | a dash bounced off a wall |
| hook / unhook | point, dist (hook) | the grapple caught / let go |
| ladder / ladderOff | | in / out of a ladder volume |
| step | rise | the step-up carried you over a ledge |
| wall | before (hspeed) | stopped by a wall (once per contact) |
| stop | | stopped on the floor |

Metrics (whatever applies): duration, topSpeed (horizontal), maxSpeed (3D), timeToTopSpeed (first at 99% of
topSpeed), timeTo90, speedAfter (hspeed at the end), maxHeight (above the start), distance (start to end,
horizontal), pathLength, endPos; speedAtRelease, stopTime, stopDistance (from the first step that lets go of every
key after one that held some, to standing still); airTime, jumpHeight, jumpDistance, takeoff (the first landing
after a takeoff; jumpHeight with `landed: false` when still in the air); jumps, landings, dashes, hopSpeeds[] (hspeed
at each landing), landImpact, fallHeight (the biggest); dashDistance, dashDuration, dashExitSpeed (the first dash);
dashJumpSpeed, superDashSpeed, bounceSpeed; hookDistance, hookTime, hookTopSpeed, speedAtUnhook; ladderTime,
climbHeight, climbRate (in the ladder volume, which reaches 1 m over the top); in the room: part, and on the gap
course gapsCleared, widestGap, gaps:[{width, cleared}]. Quark has no fall damage, so there is no damage-free height.

### physpresets `{run?: false, physics?}`
`{presets:[{id, label, desc, map: "room:<part>", scenario, metrics:[the keys that matter]}], maps:[...], ms}`.
`run: true` runs each with `physics` (default standard) and adds `result: {metric: value}`. Ids: standing_jump,
running_jump, run_up, stop, bhop_plain, bhop_strafe, dash, dash_jump, super_dash, dash_wall, grapple_swing,
ladder_climb, stairs, max_gap, fall. (No crouch walk: crouching was removed from the game.)

### physroom `{}`
`{name: "room", text: "<.qmap>", parts:[{id, map: "room:<id>", label, desc, box:{min,max}, start:[x,y,z], yaw}],
gaps:[{x0, x1, width}], bounds, brushes, ms}`. Made in code (native/testroom.cpp) in the game's map format and
parsed with Map::parse. Parts: flat, wall, ladder, drop, gaps, stairs, grapple (lanes side by side along z, all
facing +x).

### physdrive `--stream`
First line `{physics|qmode, map?, start?:{pos, vel?, yaw?, pitch?, onGround?}, samples?: false}` (no start: the
room part's start, else the map's first spawn). The first answer is `{type: "start", ...the state, map, yaw,
physics, clamped?}`. Then one answer line per control line:
- `{"cmd":[{forward, side, yaw, pitch, buttons, msec}...]}`: `buttons` a number (BTN_* bits: physparams
  constants.buttons), `["jump","dash","hook"]` or `{jump, dash, hook}`; `jump`/`dash`/`hook` booleans on the command
  work too. `msec` 1..100, run as that many 1 ms commands. yaw / pitch default to the previous command's.
- `{"physics":{...}}` or `{"qmode":"..."}`: swap the physics live (the answer carries `physics`, `clamped?`).
- `{"map": ...}`: another map (and a reset).
- `reset`, `{"reset": true}` or `{"reset": true, "start": {...}}` / `{"start": {...}}`: back to the start.
- `stop` (or `{"stop": true}`): answers `{type: "done", t}` and exits.
Each answer: `{type: "state", t, pos, vel, speed, hspeed, flags, onGround, dashCharges, dashTimer, dashCooldownMs,
hookCooldown, hookPoint? (hooked), onLadder, events:[...since the last line], us (compute time), path? (samples: the
position after every ms)}`. A bad line answers `{type: "warning", warning}`. A 16 ms batch takes about 50 us.

### physserve `--stream`
The one-shot commands kept running (serve.py's workers: no process start per request). First line `{}` (options,
none yet); answers `{type: "ready"}`. Then one line per request, `{id, cmd: "physsim" | "physexpr" | "physexport" |
"physparams" | "physpresets" | "physroom", request: {...}}`, answered in order by `{id, code, answer}` where `answer`
is exactly what `quark_maptool <cmd>` prints for that request (`code` its exit code). A map sent as `{text, name}` is
parsed once and kept (the last 4). Another command or a line that isn't JSON answers `code: 1` with `answer.error`
and carries on; `stop` ends it.

### physexport `{physics|qmode, name?, mode?}`
`{qmode: "<.qmode text>", file: "custom_modes/<name>.qmode", cpp: "<list>", changes:[{key, multiplier, file, name,
base, value, equation?}], physics, clamped?, exprs?}`. With equations: the qmode has their `expr.<key>=` lines, and each
working one's plain line holds its value (static: resolved; dynamic: at rest), so a version without equations plays a
sensible fixed value. `cpp` lists them first as `// dynamic: friction = curve(...)   // reads p.hspeed; x2 at rest`,
`// static: jump = sqrt(speed)   // = x1.1` and `// broken (not played...)`, then the constants (an equation's
constant line says `(equation)` / `(equation, at rest)`; `changes[].equation` is "static" / "dynamic"). The .qmode is playable now (Host > Custom modes). `cpp` lists each changed
constant as `src/game/pmove.cpp: kGravity = 22.0f -> 17.6f   // gravity x0.8` (the names are a table in
native/physics.cpp, checked against the sources by test_phys.py), for the owner if a set should become the game's
standard: the owner decides movement; nothing is changed automatically.

## v2: equations and curves (owner request 2026-10-24)

A movement setting can be an equation instead of a fixed multiplier. In the `.qmode` it is one more line per
setting, `expr.<key>=<expression>`, where `<key>` is `gravity`, `speed`, `jump` or a `moveParamKey` key (physparams
`params[].key`). The expression's value is that setting's multiplier (1 = standard), clamped to the setting's min..max
(the plain line's clamps); a NaN result (0 / 0, sqrt or log of a negative) keeps the fixed value. The plain
`<key>=` / `move_<key>=` line stays: it is the fallback (a broken equation, an older game), and physexport writes the
equation's value at rest there. In the game: `src/game/moveexpr.h` / `.cpp`; `CustomRules::exprs` holds the lines as
written (trimmed), one per key, in file order (a later line for the same key replaces it; an empty one, an unknown key,
a control character or more than 512 characters is ignored).

### Static and dynamic

- **Static** equations use only numbers and settings: `friction=sqrt(speed)`. They are worked out once, when the rules
  are set (`setActiveRules`), in dependency order whatever the file order, and then play exactly like a fixed value
  (no cost per step).
- **Dynamic** equations read the player's state through `p.` inputs, directly or through a setting that does
  (`stop_speed=friction * 2` is dynamic when friction's equation is). They run in pmove at the start of every 1 ms step,
  on the server and on every client alike (prediction runs the same code on the same rules text, EV_RULES). Each sees
  the values of the settings before it in dependency order, for that same step.
- **Errors** leave that setting at its fixed value: a syntax error or an unknown name (`col 9: unknown name 'sped'`), a
  cycle (`cycle: friction -> stop_speed -> friction`; a setting may not read itself), an unknown key, more than 512
  characters or 256 parts, brackets nested deeper than about 50, or more than 32 values at once. A setting that reads a
  broken one reads its fixed value.

### Inputs (`p.<name>`, the state at the start of each 1 ms step)

| input | unit | range plotted | at rest | what |
|---|---|---|---|---|
| `p.speed` | m/s | 0..50 | 0 | speed (3D) |
| `p.hspeed` | m/s | 0..50 | 0 | horizontal speed |
| `p.vspeed` | m/s | -40..20 | 0 | vertical speed (up is +) |
| `p.air` | s | 0..3 | 0 | time in the air since leaving the floor (0 on the floor) |
| `p.ground` | s | 0..3 | 0 | time on the floor since landing (0 in the air) |
| `p.onground` | 0/1 | | 1 | on the floor |
| `p.charges` | | 0..10 | 2 | dash charges held |
| `p.dashing` | 0/1 | | 0 | a dash is running |
| `p.hooked` | 0/1 | | 0 | your grapple holds |
| `p.height` | m | 0..50 | 0 | height above the floor below you (0 on it, 50 at most; a sweep down, computed only when read) |

`p.air` / `p.ground` count whole ms (`MoveState::phaseMs`, up to 65.5 s), only while an equation reads them; the
server sends the count with your own movement state (protocol 44) so prediction replays it exactly.

### Language

- Numbers (`2`, `0.5`, `.5`, `1e-3`), `pi`, settings by key (their multiplier), `base.<key>` (that setting's standard
  absolute value: `base.speed` = 9.5, `base.friction` = 7) and `p.<input>`. A name followed by `(` is a function
  (`step(1, x)` is the function, `step` the step height setting).
- Operators, loosest first: `c ? a : b`; `||`; `&&`; comparisons `< <= > >= == !=` (1 or 0, not chained); `+ -`;
  `* /`; unary `- + !`; `^` (power, right-associative: `-2^2` = -4, `2^-1` = 0.5); brackets. Non-zero is true. Both
  sides of `?:` are worked out (nothing has side effects).
- Functions: `sqrt abs min(a, b, ...) max(...) pow exp log floor ceil round sign`, `clamp(x, lo, hi)`,
  `lerp(a, b, t)` (t not clamped), `step(edge, x)` (1 when x >= edge), `ease(t, "mode")` (t clamped to 0..1) and the
  curve primitive `curve(x, x0, x1, y0, y1, "mode")`: x0..x1 mapped to y0..y1 with that easing, flat outside the bounds
  (x1 < x0 runs backwards; x0 == x1 is a step at x0). The mode is optional (`"linear"`) and always a quoted name.
- Ease modes: `linear`; `in`, `out`, `inout` (quadratic); `in3`, `out3`, `inout3` (cubic); `smooth` (3t^2 - 2t^3),
  `smoother` (6t^5 - 15t^4 + 10t^3); `expo` (in: 2^(10t - 10)), `outexpo`; `back` (in, dips below the start first:
  Penner's 1.70158), `outback` (overshoots the end, then settles).

Examples: `friction=curve(p.hspeed, 2, 12, 2, 0.6, "inout")` (grippy slow, slippery fast);
`gravity=p.air > 0.25 ? 1.6 : 0.8` (floaty rise, heavy fall); `ground_accel=1 + 0.5 * ease(p.ground / 0.3, "out")`;
`friction=sqrt(p.hspeed / base.speed)`; `air_accel=sqrt(speed)` (static).

### Determinism and cost

Everything is IEEE double arithmetic: `+ - * /`, sqrt and the roundings are exact on every platform, and the game is
built with `-ffp-contract=off` and no fast-math. `exp`, `log`, `pow` and the expo easings are the game's own code made
of those operations (the C libraries of Linux, MinGW and macOS differ in the last bit); numbers in the text are read by
the game's own code too (strtod depends on the locale). quark_tests checks golden bits of the math and of a compiled
program, so the Windows CI run proves MinGW computes the same. Static parts of a dynamic equation are folded when the
rules load, and it compiles to a few bytecode ops (a curve with constant bounds is 2). Measured (`quark_tests
exprbench`, dev box): pmove about 170 ns per step with or without a custom mode when no equation is dynamic (no
measurable difference), +10 ns with one dynamic equation, +90 ns with five; the interpreter alone 9 to 33 ns per
equation; p.height adds a sweep (~30 ns).

### physexpr `{expr?, key?, physics|qmode?, inputs?, at?, samples?: 101}`
The set's equations as the game builds them, each dynamic one sampled over every input it reads (the curve plot).
`expr`: `{key: text}` over the set's own (an empty text removes one), or one string with `key`. `inputs`:
`{"p.speed": [lo, hi] | {min, max, n} | {values: [...]}}` (default: the input's range; flags are 0 and 1, charges whole
numbers). `at`: the other inputs while one is swept (default: at rest; sweeping p.air or p.height above 0 also sets
p.onground 0). Answer:
```
{items:[{key, text, ok, error?, dynamic, deps:[the setting keys it reads], inputs:["p.hspeed", ...: directly or
         through deps], direct:[...], value (static: resolved; dynamic: at rest; broken: the fixed value),
         absolute (x base), min, max, samples?:[{input, unit, x:[...], y:[multiplier, clamped], abs:[absolute]}]}],
 order:[keys, dependency order], dynamic: <count>, ops: <bytecode ops>, physics, clamped?, ms}
```
physsim, physdrive and physexport take equations in any physics set (`expr` or qmode lines) and run them through the
game's pmove.

### Not covered (yet)

- The game's own custom-mode editor (MOVEMENT page) shows and edits the fixed values only: a mode with equations keeps
  them when saved there, but its sliders don't show them.
- Everything else that reads a movement value for a player uses the value pmove's next step plays by for that player
  (`stepTuning` / `moveValue` in pmove.h, no cost without equations): the HUD's dash pips and hook bar, the server's
  grapple on players (range, cooldown), the syringe refill, the hyper / super sound, the bots and the strafe bot
  (`strafeBot: true`, per step). Two places take one value for a whole command: the server's player probes use its first
  step, and a bot's grapple check (`safeHook`) works the swing out with the values at the moment it plans.
- When `dash_charges` falls (an equation, mid-air), the charges you hold above the new max go; recharging ones keep their
  timers and come back once the max allows them again.
