# Physics editor: helper interface (v1.1)

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

JSON form of a physics set: `{"gravity":1,"speed":1,"jump":1,"dash":true,"grapple":true,"move":{"<key>":1,...}}`
(missing keys = 1 / true). Requests may instead give `"qmode": "<.qmode text>"` (parsed with CustomRules::fromText;
non-movement lines are kept by the page, ignored by the helper).

Clamps: every value goes through `CustomRules::fromText`, so it is clamped exactly as the game clamps a custom mode
file (gravity 0.1..3, speed 0.25..3, jump 0.25..3, every move key 0.1..5; physparams reports them, read from fromText
itself). A physics set may also be the string `"standard"` or a .qmode text, and an object may carry `"qmode"` (its
values first, the object's keys over them) and `"name"`. Answers echo each set as `physics:
{name, gravity, speed, jump, dash, grapple, move:{every key}, changed:[keys], standard}` and list `clamped: [keys]`
when a value was out of range.

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
 moveStep: 0.001, angles: "...", version, ms}
```
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

### physexport `{physics|qmode, name?, mode?}`
`{qmode: "<.qmode text>", file: "custom_modes/<name>.qmode", cpp: "<list>", changes:[{key, multiplier, file, name,
base, value}], physics, clamped?}`. The .qmode is playable now (Host > Custom modes). `cpp` lists each changed
constant as `src/game/pmove.cpp: kGravity = 22.0f -> 17.6f   // gravity x0.8` (the names are a table in
native/physics.cpp, checked against the sources by test_phys.py), for the owner if a set should become the game's
standard: the owner decides movement; nothing is changed automatically.

## v2: equations and curves (draft, owner request 2026-10-24)

A setting can be an expression instead of a fixed multiplier. In the `.qmode` it is one extra line per setting:
`expr.<key>=<expression>`, where `<key>` is `gravity`, `speed`, `jump` or a `moveParamKey` key. The expression's value
is that setting's multiplier (1 = standard), clamped to the setting's min..max. The plain `<key>=` /
`move_<key>=` line stays as well, so an older game reads a sensible fixed value. The editor writes the expression's
value at the standard state there.

- **Static** expressions use only numbers and other settings, by key: `friction=sqrt(speed)`. These are worked out
  once, when the rules load, in dependency order. Cycles are an error.
- **Dynamic** expressions also read the player's state, through `p.` inputs:
  - `p.speed`, `p.hspeed`, `p.vspeed` (m/s)
  - `p.air` and `p.ground` (s since leaving or landing)
  - `p.charges` (dash charges)
  - `p.dashing` and `p.hooked` (0/1)
  - `p.height` (m above the ground under you, capped at 50)

  These are evaluated in pmove at every 1 ms step, on the server and the client alike. They must be deterministic and
  cheap: compile to bytecode or a baked table, measure it, with a budget of under 50 ns per active expression per step.
- **Functions:**
  - `sqrt abs min max pow exp log floor ceil sign`
  - `clamp(x,lo,hi)`, `lerp(a,b,t)`, `step(edge,x)`
  - `ease(t,"in"|"out"|"inout"|"smooth"|"linear"|"expo"|"back")`, with t clamped to 0..1
  - `curve(x, x0, x1, y0, y1, "inout")`, the main curve primitive: it maps x in [x0,x1] to [y0,y1] with that easing, and is flat outside the bounds
- **Numbers and operators:** `+ - * / ^`, comparisons giving 0/1, `?:`, and parentheses.
- **Errors:** a parse error leaves the setting at its fixed value, and the helper's answer lists the error.
