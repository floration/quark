# Game mode logic (`.qlogic`, the logic section of a `.qmode`): grammar and JSON AST

A mode's logic is its rules as data: when a round ends and who wins it, what a kill scores, how long the clocks run,
who can hurt whom. The mechanics that need geometry, physics or networking (the bomb, the payload cart, multi point
captures, race checkpoints, the stalemate drain, "it", the rock paper scissors tiebreak, the tourney bracket, duel
modifiers) stay native C++: a mode switches them on with `use <object>`, drives them with their step actions and
hears back from them through event slots.

* Built-in: `tools/modes/common.qlogic` (shared by every mode) and one file per mode: `vs bomb race koth payload ffa
  npayload duel arena tourney hide tag points` (`arena` is the 1v1 mode: a name can't start with a digit; the
  `.qmode` key `mode=1v1` reads as it). They are embedded at build time (`tools/modes/embed_modes.py`) and say exactly
  what `src/game/gamemodes.cpp` does; the game runs them as native code, and the lockstep test runs them through the
  interpreter beside the native code.
* A custom mode (`.qmode`): its `key=value` lines (`custommode=`, `mode=`, `maps=`, the custom rules), then optionally
  `logic 1` and logic to the end of the file. A file only overrides what it writes: every param, state, timer,
  curve, proc or slot it leaves out is the base mode's, then common's (section 8).
* Text is the source of truth. The editor works on the JSON AST (section 4); `print(parse(text)) == text` for
  canonical text, so text, blocks and graph are three views of one tree.
* `quark_tests exportmode <out.json>`: the catalog (types, slots with their arguments and when they run, fields,
  arrays, functions, builtins, aggregates, sets, actions, objects, params with units, ranges, docs and each mode's
  default, constants, limits) and every default's text, AST, hash and slot costs.
  `quark_tests checkmode <file> [--map <name>]`: compile, print errors and warnings as JSON with line:col of the whole
  file (exit 1 on errors); with `--map`, also an error for each map feature the logic reads that the map lacks (a
  hill, bomb sites, a payload track, multi point zones, race checkpoints). `quark_tests modeast <file>`: the JSON AST.
  `quark_tests printmode <file>`: canonical text (the key lines verbatim). `quark_tests modebench`: what an
  interpreted slot run costs. `quark_tests modelogic`: the language's tests.
* A mode that runs logic other than the defaults is labelled `name#hash8` (section 9).

---------------------------------------------------------------------------------------------------------------

## 1. Lexical rules

* UTF-8, `\n` or `\r\n` line ends. Tabs are an error (indent with spaces; any consistent depth).
* `#` starts a comment to the end of the line, except inside a string.
* Names: `[A-Za-z_][A-Za-z0-9_]*`, optionally with `.part` segments (`hud.clock`, `cart.best`, `spawn.attack`).
* Numbers: `12` (int), `1.5` / `1e-3` (f64), `1.5f` / `2f` (f32). A leading `-` is part of a literal.
* Strings: `"..."`, at most 120 bytes, no `\` and no `"` inside. Only text arguments take them (section 2.6).
* Operators: `+ - * / % < <= > >= == != = ? : ( ) [ ] ,` and the words `and or not in`.
* Keywords: `logic base param curve use state match round timer proc slot if elif else return pass for in where by
  break and or not yes no true false none A B`. `steps` and `points` are keywords only right after `curve ... =`
  (`points` is also an object's name).
* A line is at most 512 tokens; blocks and expressions nest at most 48 deep.

## 2. File structure

```
logic 1                  # the format version (first line of the logic; no comment before or on it)
base vs                  # optional: the mode it plays as (common in common.qlogic). A .qmode's mode= key says it too
<declarations>
```

Blank lines and comment lines are kept (runs of blank lines fold to one): a comment belongs to the declaration or
statement under it, a trailing comment to its line. In a `.qmode` everything before the `logic` line is the key
lines, kept verbatim; line numbers in messages count from the top of the whole file.

### 2.1 Declarations

| Declaration | Meaning |
|---|---|
| `param <name> <f32\|f64\|int\|bool> = <value>` | a number the logic (and the native code) reads. A built-in param keeps its type (`exportmode` lists them with units and ranges); a new one is the file's own |
| `curve <name>(<x> <f32\|f64>) = <expr>` | a function of one value: sees only its argument, params, constants and other curves |
| `curve <name>(<x> f32) = steps <base>, <x1>: <y1>, ...` | `base` below `x1`, then `y_k` once `x > x_k` (at most 16) |
| `curve <name>(<x> f32) = points <x1>: <y1>, ...` | linear between points, flat outside (1 to 16; x goes up) |
| `use <object>` | switch on a native mechanic (section 7): its step actions, events and params |
| `state <match\|round> <name>[team\|player] <type> = <value>` | storage that lasts the match (reset at `reset` and `matchStart`) or the round (reset at `roundSetup`); `[team]` holds 2 values, `[player]` 32. Types: bool int f32 f64 team player (a team: `A`, `B` or `none`; a player starts as `none`) |
| `timer <name>` | a timer: `startTimer(name, s)`, `stopTimer(name)`, `timerLeft(name)`; `slot timer <name>` runs when it runs out |
| `proc <name>[(<arg> <type>, ...)]:` + block | a procedure: a statement `name(args)` runs it (written out in place, section 2.2) |
| `slot <name>:` + block | the code the engine runs at that point (section 5); `slot timer <name>:` for a timer |

Within one file a name (or a slot) is declared once. Names are case sensitive and can't be a keyword, a constant,
a field, array, function, action, builtin, aggregate, set or a slot argument, or have a `.`.

### 2.2 Statements

```
x = <expr>                      # a new local (its type is the value's), or an assignment to a local, a writable
                                # field, or a state value
arr[<index>] = <expr>           # a writable array (score, teamScore, cart.best ...) or a per-team / per-player state
<action>(<args>)                # an action (section 6), or a proc
if <cond>: <stmt>               # an arm on one line holds one simple statement
if <cond>:                      # ...or an indented block
  <block>
elif <cond>: ...
else: ...
for <p> in <set> [where <cond>]:   # each player of the set, in slot order
  <block>
break                           # leaves the for
return [<expr>]                 # a query slot's result; a bare return leaves the slot (in a proc: the proc)
pass
```

* A local is set before it is read on every path (`x` "may not be set here" otherwise). A local first set to `none`
  takes its type (team or player) from a later assignment in the same or an enclosing block.
* A `for` can't be inside another `for` (nor an aggregate inside an aggregate): that bounds a slot's cost.
* A loop variable can't shadow any name (a slot argument, a local, a field...).
* Procs: written out where they're called (no recursion, at most 4 parameters, no return value). A proc sees its
  parameters, the calling slot's arguments, fields, params and state, not the caller's locals. A proc nobody calls
  is warned about and not checked.
* `endRound`, `endRoundNoScore`, `endRoundText` and `endMatch` end the slot (and the procs it is in). Statements
  after one on the same path are warned about ("never runs"). Any other action that ends the round from inside (a
  step whose event ended it, `kill()` of the last player through slot kill ...) ends the slot as soon as it returns:
  the round's end ends the slot.

### 2.3 Expressions

Precedence, lowest first:

| Level | Operators |
|---|---|
| 1 | `c ? a : b` (right associative) |
| 2 | `or` |
| 3 | `and` |
| 4 | `not` |
| 5 | `< <= > >= == !=`, `p in <set>` (don't chain) |
| 6 | `+ -` |
| 7 | `* / %` |
| 8 | unary `-` |
| 9 | literals, names, calls `f(...)`, indexing `a[i]`, aggregates, `( ... )` |

### 2.4 Types and arithmetic

`bool int f32 f64 team player zone phase bombstate spawn weapon text timer`.

* Arithmetic is C++'s: `int op int` is int (32-bit, wrapping); with an f32 it's f32 (rounded to float after each
  operation); with an f64 it's f64. `int / int` is an error (write `1.0 / 2`). `%` takes ints (`x % 0` is 0).
* Widening is automatic (int to f32 or f64, f32 to f64); narrowing needs `f32(x)` / `int(x)` (int truncates like a
  C++ cast, saturating; NaN is 0).
* `team`, `player`, `phase`, `bombstate`, `spawn`, `weapon` compare with `==` / `!=` only. `none` is no team / player /
  weapon / bomb, typed from where it goes. A spectator's team is neither `A` nor `B`.
* Constants: `A B`, phases `warmup freeze live roundEnd matchEnd`, bomb states `carried dropped planted exploded
  defused`, spawns `spawn.a spawn.b spawn.any spawn.race spawn.attack spawn.defend`, weapons `rifle rocket knife
  sniper laser shotgun sticky crossbow bubble syringe locator`, kill causes `fell bombed drained`, zones `hill sites`.

### 2.5 Sets and aggregates

Sets: `everyone` (connected), `playing` (connected, not spectating), `team(t)` (connected, on team t).

| Aggregate | Value |
|---|---|
| `count(p in S [where c])` | how many |
| `any(p in S [where c])` | is there one (stops at the first) |
| `first(p in S [where c])` | the first in slot order, else none |
| `random(p in S [where c])` | one at random: one `qrand()` draw (`qrand() % n`) when there is anyone, else none and no draw |
| `best(p in S [where c] by v)` | the first highest by `v`, else none |
| `names(p in S [where c])` | their names joined with `, ` (text arguments only) |

`p in S` is a bool: is player p in the set.

### 2.6 Text arguments

`announce`, `log`, `endRound`, `endRoundNoScore`, `endRoundText` and `endMatch` take up to 8 text arguments,
concatenated: string literals, players (their name; `none` is "nobody"), teams (`TEAM A` / `TEAM B`), ints, and the
text builtins `name(p)`, `letter(i)` (0 = `A`) and `names(...)`. An f32 / f64 needs `int(...)`.

### 2.7 Builtins and functions

`min max clamp abs sqrt exp f32 f64 int` (as the bot brain's: C++'s `std::min` / `std::max`, clamp as
`x >= lo ? (x > hi ? hi : x) : lo`). The game's functions (`aliveCount(t)`, `teamCount(t)`, `alive(p)`, `team(p)`,
`isBot(p)`, `onGround(p)`, `useHeld(p)`, `health(p)`, `nearCart(p)`, `mapSpawn(p)`, `inZone(p, z)`, `name(p)`,
`timerLeft(t)`, `other(t)`, `letter(i)`, `site(i)`, `point(i)`, `checkpoint(i)`): `exportmode`'s catalog.
`other(A)` is B and anything else is A (as gamemodes.cpp).

## 3. Canonical text (what the printer writes)

* `logic N`, then `base <mode>` if the file has one, then the declarations in file order.
* Two-space indent per block level. One space around binary operators and after commas; none inside brackets.
* An `if` / `elif` / `else` arm holding one simple statement (no comment of its own) goes on the arm's line
  (`if x: return`); otherwise an indented block. A `for` always has a block.
* Parentheses only where needed, plus: an `and` inside an `or` is bracketed, a comparison or `in` under `not` is
  bracketed, a `?:` as the condition or middle of another is bracketed.
* Numbers: the shortest decimal that reads back as the same value, written like JavaScript's `toString`; f32
  literals end in `f`, and a whole f32 / f64 literal gets `.0` (`2.0f`, `0.5`). Declared values (`param`, `state`,
  curve points) have no suffix: their type is declared.
* Comments are kept where they were; a trailing comment is one space after the code.

## 4. JSON AST (editor interchange)

`modeast` (and `exportmode`'s `defaults.<mode>.ast`):

```
{"format":"qmode-logic-ast","version":1,"base":"vs"|null,"keys":[["custommode","x"],...],
 "decls":[<decl>...],"tail":["# comments after the last declaration", ""]}
```

Every node has `"id":"nN"` (in document order) and may have `"comments":[...]` (lines above it; `""` is a blank
line) and `"trail":"# ..."`.

* decls: `{"kind":"param","name","type","value"}`, `{"kind":"curve","name","arg","argType","form":"expr"|"steps"|
  "points","body"|"base"+"points":[[x,y]...]}`, `{"kind":"use","object"}`, `{"kind":"state","life":"match"|"round",
  "name","per":"team"|"player"|null,"type","value"}`, `{"kind":"timer","name"}`, `{"kind":"proc","name","params":
  [[name,type]...],"body"}`, `{"kind":"slot","name","timer"?,"body"}`.
* statements: `set {name, index?, value}`, `return {values:[expr?]}`, `pass`, `break`, `do {fn, args}`, `for {var,
  set, where?, body}`, `if {arms:[{cond, body, comments?, trail?}], else?:{body}}`.
* expressions: `num {type, v}`, `bool {v}`, `str {v}`, `name {name}`, `neg {a}`, `not {a}`, `?: {c, a, b}`, `in {a,
  set}`, `call {fn, args}`, `agg {fn, var, set, where?, by?}`, `index {array, index}`, and the binary operators by
  their text (`+`, `<=`, `and` ...) with `a`, `b`. A set is `{"set":"everyone"|"playing"|"team","team"?:expr}`.

## 5. Slots

Events are statements; queries `return` a value (a query whose result is never set warns, and gives 0 / no). Every
slot sees the fields (section 6); its arguments are names.

| Slot | Arguments | When |
|---|---|---|
| `reset` | | back to warmup (rulesReset); match state resets first |
| `warmup` | | each warmup tick, before the autostart check |
| `matchStart` | | a match starts, before round 1; match state resets first |
| `roundSetup` | | a round starts: after roundNo++ and the engine's per-round resets (it, found, checkpoints, the bomb, the cart), before the spawns; round state resets first |
| `roundStart` | | after the spawns and the objects' round start, as the freeze begins |
| `live` | | the freeze ends: the round is live |
| `tick` | | each live tick (hud.clock already holds the round clock) |
| `timeUp` | | each live tick after tick, while the round clock has run out |
| `kill` | victim, killer, weapon | a player died: after the engine counted the death and (unless it was a teamkill) the kill |
| `stab` | attacker, victim | hide and seek / tag: it knifed someone while live (after canHit) |
| `leave` / `spectate` | p | a player is leaving (still connected) / going to spectate |
| `roundOver` | | the round-end screen is over: end the match, or fall off the end for the next round |
| `plant` / `defuse` / `explode` | p / p / - | bomb events (use bomb) |
| `cartCheckpoint` / `delivered` | i / - | payload events (use cart); i = checkpoints reached now, 1 = the first |
| `capture` | i | multi point (use points); i = points taken now |
| `checkpoint` / `finish` | p, i / p | race events (use race) |
| `freezeLength` -> f32 | | each round start |
| `roundLength` -> f32 | | the freeze ends (0: no clock) |
| `roundsToWin` -> int | | match start and reset (hud.roundsToWin) |
| `respawnDelay` -> f32 | p | a death or a team join, outside warmup and match end (below 0: no respawn) |
| `canDamage` -> bool | attacker, victim, weapon | each hit |
| `canHit` -> bool | attacker, victim, weapon | each hit, before knockback (no: no damage, no knockback) |
| `frozen` -> bool | p | per player, once a tick |
| `spawnType` -> spawn | p | each spawn |
| `teamLocked` -> bool | | joins and team changes |
| `canAutoStart` -> bool | | each warmup tick on autostart servers |
| `timer <name>` | | the timer ran out (timers are checked once a tick, before tick) |

## 6. Fields, arrays and actions

Read-only fields: `now dt phase roundNo phaseEnd liveSince roundStartTime lastProgress teams ranked tickRate
cfg.timeLimit cfg.scoreLimit cfg.roundsToWin hasHill hasCart siteCount pointCount checkpointCount hud.roundsToWin
bomb.state bomb.carrier bomb.acting bomb.timer cart.dist cart.length cart.checkpoint point.index point.capture`.
Writable: `attackers hud.clock hud.overtime hud.contested hud.drain hud.target hud.owner`.

Arrays (`a[i]`): per player `score objTime it out found sought` (writable), `kills deaths freezeUntil
race.checkpoint race.finished` (read-only); per team `teamScore cart.best cart.completed` (writable), `cart.near`
(read-only). An index out of range reads 0 and writes nothing.

Actions: `addScore(p, n)`, `addTeamScore(t, n)`, `endRound(winner, text...)`, `endRoundNoScore(winner, text...)`,
`endRoundText(text...)`, `endMatch(winner, text...)`, `announce(text...)` (at most 4 a second: past that dropped,
logged once), `log(text...)`, `setClock(s)`, `addTime(s)` (phaseEnd = max(phaseEnd, now) + s), `touchObjective()`,
`holdOvertime()` -> bool, `startTimer(t, s)`, `stopTimer(t)`, `kill(victim, killer, how)` (runs slot kill),
`damage(p, hp)`, `respawn(p)`, `freeze(p, s)`, `swapTeams()` (in the catalog, not available yet: a compile error), and
the objects' steps (section 7). `endRound`'s winner is a team (a player in race, the base mode that scores players);
`endMatch`'s is a team in team modes, a player in ffa, race, tourney, hide and tag. The host writes the round
result as gamemodes.cpp does: `<winner> wins the round (<text>)` (no text: no brackets; 1v1 and tourney name the
player; race `<player> wins the race` / `nobody finished`; no winner: `round draw`).

## 7. Objects (`use`)

| Object | Steps | Events | Params (default) | Native in |
|---|---|---|---|---|
| bomb | `bombStep()`, `giveBomb(p)` | plant, defuse, explode | plantTime 3, defuseTime 6, bombFuse 40, bombRadius 18, defuseRange 1.8, pickupRange 1.2, bombReturn 15 | bomb |
| cart | `cartStep()` | cartCheckpoint, delivered | cartRange 4, cartSpeed 1.6, cartRollback 0.6, cartIdle 10, cartCheckBonus 60 | payload |
| ncart | `neutralCartStep()` | | cartRange, cartSpeed | npayload |
| points | `pointStep()` -> bool | capture | captureTime 12, captureDecay 0.05 | points |
| race | `raceStep()`, `racePractice()` | checkpoint, finish | | race |
| drain | `stalemateDrain()` | | stalemate 30, drainRate 5 | vs, duel, 1v1, tourney |
| it | `pickSeeker()`, `pickRandomIt()`, `tagOut(p)`, `passIt(from, to)` | | hideTime 30, tagFreeze 2 | hide, tag |
| tiebreak | `startTiebreak()` -> bool | | rpsPickTime 6, rpsRevealTime 3 | hide |
| bracket | `tourneyBegin()` -> bool, `tourneyWalkover()` -> bool, `tourneyAdvance(t)` | | | tourney |
| duelmods | `pickDuelModifier()` | | | duel |

An object's step without its `use` is a compile error; a `use` of an object the base mode doesn't use natively is
warned about (the HUD won't show it and bots won't play it). An object's params nobody declares are there at their
defaults. What each step does: `exportmode`'s catalog (`actions[].doc`). Notes: `bombStep` doesn't announce the
plant (slot plant does); `pointStep()` is true when a point was taken this tick or there's no point left to attack;
`cart.near[t]` is set by the last `cartStep()` / `neutralCartStep()`; `cart.best` / `cart.completed` are the
payload and multi point progress (bestProgress_ / completeTime_).

## 8. Merging, native slots, limits

* The logic that runs is common.qlogic, then the base mode's file, then the custom file: a declaration or slot of
  the same name replaces the earlier one (a param or state keeps its type: redeclaring with another type is an
  error). Errors in a default's declaration say which file (`(in common.qlogic)`).
* Every slot of the merged logic is compiled and checked. A slot runs as native code when its written-out text
  (the body without comments, plus every proc, curve, state and timer declaration it uses) is the base default's;
  otherwise it runs in the interpreter. Params are data: changing one keeps every slot native (the native code
  reads the param values). Timer slots always run in the interpreter.
* Limits: 64 params, 32 state declarations, 512 state values (a per-player state takes 32, per-team 2), 8 timers,
  16 procs, 4096 instructions per slot, and 8192 worst-case instructions per slot call (a `for` or aggregate body
  counts 32 times), 64 registers, slots nested 4 deep at run time (deeper: the native code runs, logged once).

## 9. Hash and label

`name#hash8`: name = the `.qmode`'s `custommode=` (else the base mode's name); hash8 = the first 8 hex digits of
sha256 of the key lines (without `custommode=`, `maps=` and `label=`, in file order, each `key=value\n`), then `\n`,
then the canonical merged logic without comments. A comment or a map list doesn't change it; a param does.

## 10. Format versions

`logic N` is the version (kModeLogicVersion, now 1). A change to the format raises it and adds a converter to
`kModeLogicUpgrades` (modelogic.cpp; a static_assert counts them) and a test converting a file of the old version.
Older files are converted on load; a newer version is refused with its line.

## 11. Additions to the design (mode-editor design.md) and what the engine must do

The catalog changes made while writing the defaults (smallest each):

* `%` (int remainder, `x % 0` = 0): payload and multi point alternate sides on `roundNo % 2`.
* Proc parameters `proc name(who player)`, written out in place; a bare `return` in a proc leaves the proc. Tag's
  elimination and quitting share code.
* `p in <set>` (a bool), the `names(...)` text aggregate and the `letter(i)` text builtin: hide and seek's time-out
  names the hiders still hidden; multi point names its points A, B, C.
* Fields `freezeUntil[p]` (read-only f64; set by `freeze(p, s)`) and `cart.near[t]` (players of team t alive at the
  cart at the last cart step): tag's freeze after a tag; neutral payload's overtime touch.
* Action `passIt(from, to)` (object it): tag's handoff with the knife out and the locator reset.
* `pointStep()` returns a bool (a point was taken, or none is left to attack); `cartCheckpoint`'s and `capture`'s
  `i` is the count reached now, 1-based.
* `cart.best` / `cart.completed` serve payload and multi point (both writable f32).
* `none` is typed from context (a bombstate's none is 0, every other none -1); a local first set to `none` takes
  its type from a later assignment.
* The common respawn param is `respawnTime` (`respawn` is an action). `roundsToWin` lives in common
  (`cfg.roundsToWin > 0 ? cfg.roundsToWin : rounds`); modes set `param rounds`. `roundLength` returns `roundTime`,
  `respawnDelay` returns `respawnTime` (warmup: `warmupRespawn`, match end -1).
* `swapTeams()` is in the catalog but not available (a compile error) until its rules are decided.
* `1v1` is `arena` in the logic (`base arena`, `arena.qlogic`); `mode=1v1` in a `.qmode` reads as it.

For the engine binding (ModeHost on Server):

* The kill slot runs where rulesOnDeath's scoring ran (after EV_KILL, before any round end the death causes); the
  engine counts deaths, and kills only for non-teamkills.
* The engine sets `hud.target` to 0 before matchStart (koth's matchStart sets it), and `hud.clock` to the round clock
  before tick.
* After roundOver: the next round starts only if the phase is still roundEnd and no tiebreak (rock paper scissors) is
  running. After matchStart: round 1 starts only if the match didn't end in it (tourney: not enough players). The
  "<Mode> - match started" message stays native (every base but tourney).
* `canHit` false: no damage, no knockback. In hide and seek / tag, a hit that canHit lets through between two
  different players is a stab (slot stab), not damage.
* Two log lines differ from the native ones in wording: multi point's `point 2 taken by TEAM B` (native: `point 2
  taken by team 1`) and tag's `tag ANNA -> BOB` (native: player ids).

## 12. Changelog

* logic 1 (1.18.x): the first version.
