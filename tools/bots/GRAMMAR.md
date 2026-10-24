# Bot brain files (`.qbrain`): grammar and JSON AST

A bot brain is the bot's choices and numbers: per-level parameters, curves, weapon lists, tables and the bodies of
fixed decision points ("slots") that the game calls. The rest of the bot (navigation, pit / lava safety, grapple
safety, the hand that turns the view, air-strafe trigonometry) is the body: C++ that a brain tunes through
parameters but can't switch off.

* Built-in: `tools/bots/default.qbrain` (embedded in the game at build time). It is exactly the native bot; the game
  runs it as native code, and `quark_tests` runs it through the interpreter in lockstep with the native code.
* User brains: `<user data>/bots/<name>.qbrain`. A file only overrides what it writes: a parameter, curve, list,
  table or slot it leaves out is the built-in one.
* Text is the source of truth. The editor works on the JSON AST below; `print(parse(text)) == text` for canonical
  text (what the printer writes), so text, blocks and graph are three views of one tree.
* `quark_tests exportbrain <out.json>` writes the default brain (text, AST) and the catalog (every parameter,
  slot, sensor, output, builtin with types, units, ranges and docs). `quark_tests checkbrain <file>` compiles a
  file and prints errors and warnings as JSON with line:col.

---------------------------------------------------------------------------------------------------------------

## 1. Lexical rules

* UTF-8 text, `\n` or `\r\n` line ends. Tabs are an error (indent with spaces).
* `#` starts a comment to the end of the line (there are no `#` inside strings other than the brain name).
* Names: `[A-Za-z_][A-Za-z0-9_]*`, optionally followed by `.part` segments with the same rule
  (`hill.floor`, `bomb.pos`). Case-sensitive.
* Numbers: `DIGITS [ "." DIGITS ] [ ("e"|"E") ["+"|"-"] DIGITS ] [ "f" ]` (no leading `.`, no hex). In an
  expression the text gives the type: an `f` suffix is `f32` (`0.5f`), a `.` or exponent without it is `f64`
  (`0.5`, `1e-3`), plain digits are `int` (`3`). In data (param values, curve points, table entries) numbers take
  the declared type, may start with `-`, and must not carry `f`.
* Strings: only the brain name, `"..."` with no `"` or `\` inside, at most 64 bytes.
* Keywords: `version brain levels param curve list table slot if elif else return goto fallback seek evade pass
  and or not in yes no true false`. `steps` and `points` are keywords only right after `curve ... =`
  (elsewhere `points` is the mode). Names may not be keywords.

## 2. File structure

```
version 1
brain "default"
levels BEGINNER INTERMEDIATE ADVANCED EXPERT
<declarations, in any order>
```

* `version N`: the format version, the first line (like maps). This document is version 1. A file of an older
  version is converted on load by the converter table (`kBrainUpgrades`, one step per version; the original is
  kept in `bots/old/v<N>/`). A newer version than the game knows is an error.
* `brain "<name>"`: the display name (shown on the join line with the hash: `BOT ANNA (EXPERT, brain mybrain#1a2b3c4d)`).
* `levels`: optional; when present exactly these four names in this order (the game has 4 skill levels).
  The printer always writes it.
* The header lines carry no comments.

### 2.1 Declarations

```
param <name> <type> = <v> [<v> <v> <v>]           # one value: every level; four: BEGINNER .. EXPERT
curve <name>(<arg> <f32|f64>) = <expression in arg>
curve <name>(<arg> f32) = steps <base>, <x>: <y>, <x>: <y> ...
curve <name>(<arg> f32) = points <x>: <y>, <x>: <y> ...
list <name> = <weapon> <weapon> ...
table <name> = <weapon> <number>, <weapon> <number>, ... [, else <number>]
slot <slotname>:                                   # then an indented block
slot objective <mode>:
```

* **param** types: `f32`, `f64`, `int`, `bool` (values `yes` / `no`; `true` / `false` are read as those).
  Values are rounded to the type (an f32 param holds a float). The catalog's parameters (section 6, and
  `exportbrain`) have a fixed type: a file may only change their values. A file may add its own parameters;
  their trailing comment is their doc.
* **curve**: a named function of one argument, used like a builtin (`reactDist(dist)`).
  * expression curve: the expression is used in place, with the argument bound to the call's argument (which is
    evaluated once). It may use params and other curves declared before it (no recursion), but no sensors.
    Its type is the expression's.
  * `steps base, x1: y1, x2: y2 ...` (f32, x ascending): `base`, then `y_k` for the last `x_k` with `arg > x_k`.
  * `points x1: y1, x2: y2 ...` (f32, x strictly ascending, 1-16 points): piecewise linear, flat beyond the ends:
    `y_k + (y_{k+1} - y_k) * ((arg - x_k) / (x_{k+1} - x_k))` in f32.
  * steps / points curves are data like params: the native default reads them, so retuning them costs nothing.
* **list**: weapons, best first, no repeats, 1-11 entries. Weapon names: `rifle rocket knife sniper laser shotgun
  sticky crossbow bubble syringe locator` (and `none` in expressions: no weapon).
* **table**: an f32 per weapon; `else` is the value for weapons not listed (0 when there's no `else`).
* **slot**: the body of a decision point the game calls (section 5). Each slot at most once per file
  (`objective` once per mode). A slot that isn't in the file is the built-in one. `objective <mode>` for a mode
  that neither the file nor the built-in brain writes runs the brain's `objective default`.
* Names of params, curves, lists and tables are unique across all of them and must not be a keyword, a builtin,
  a weapon, a mode, a sensor or an output name (of any slot).

### 2.2 Statements (inside a slot)

Blocks are indented (canonical: 2 spaces per level; the parser takes any consistent deeper indent).

```
<name> = <expr>                 # assign a slot output or a local (a local's type is its first assignment's)
if <expr>: <simple statement>   # inline form
if <expr>:                      # block form
  <statements>
elif <expr>: ...                # any number
else: ...
return                          # leave the slot (outputs keep what they were given)
return <expr> [, <expr> ...]    # assign the slot's outputs in catalog order, then leave
goto <point>                    # objective only: go there, and leave
fallback                        # objective only: no objective: hunt what was last seen / heard, else roam
seek                            # objective only (hide / tag, as "it"): wander, follow sounds and locator finds
evade                           # objective only (hide / tag, as a runner): flee "it" when near, else hide / wander
pass                            # nothing (an empty block)
```

* Outputs start at 0 / `no` / `none` each call. A local must be assigned before it is read on every path
  (checked: an error otherwise). Locals and outputs share a slot-wide scope.
* An objective slot must end in an action on every path (`goto`, `fallback`, `seek`, `evade`); the
  others may fall off the end.
* `if` conditions must be `bool` (no truthiness of numbers).

### 2.3 Expressions

Precedence, lowest first (all binary operators left-associative except `?:`, comparisons don't chain):

| Level | Operators | Notes |
|---|---|---|
| 1 | `c ? a : b` | right-associative; lazy: only the taken branch is evaluated |
| 2 | `or` | short-circuit |
| 3 | `and` | short-circuit |
| 4 | `not x` | |
| 5 | `< <= > >= == !=`, `x in <list>` | one per level: `a < b < c` is an error |
| 6 | `+ -` | |
| 7 | `* /` | |
| 8 | unary `-` | `-` straight before a number literal is part of the literal |
| 9 | `f(args)`, `table[x]`, names, literals, `( )` | |

Values: number literals, `yes`/`no`, names (a local or output, a sensor, a param, a weapon, a mode, a list, a
point), calls of builtins and curves, `table[weapon]`.

### 2.4 Types

`bool`, `int`, `f32`, `f64`, `weapon`, `mode`, `list` (a reference to a list), `point` (a position: only in `goto`).

Arithmetic and comparisons follow C++ exactly, which is what makes the default brain bit-identical to the native
code: `int` with `f32` is `f32`; `int` or `f32` with `f64` is `f64`; `f32` arithmetic is done in float, rounded
after every operation. `int / int` is an error (write `1.0 / 2`). There are no implicit narrowing conversions:
an `f64` into an `f32` output, param or curve argument needs `f32(x)`. `bool` is not a number. `==` / `!=` also
compare two weapons, two modes or two bools.

### 2.5 Builtins

| Call | Type | Notes |
|---|---|---|
| `roll()` | f32 | a uniform random number in [0, 1): ONE draw from the bot's dice |
| `gauss()` | f32 | a normal random number (mean 0, sd 1): TWO draws |
| `chance(rate)` | bool | an event happening `rate` times a second, this tick: `rate > 0 and roll() < 1 - exp(-rate * dt)` (no draw when `rate <= 0`). `rate` f32 |
| `min(a, b)`, `max(a, b)` | common type | C++ `std::min` / `std::max`: `b < a ? b : a`, `a < b ? b : a` |
| `clamp(x, lo, hi)` | common type | `x >= lo ? (x > hi ? hi : x) : lo` |
| `abs(x)`, `sqrt(x)`, `exp(x)` | type of x | f32 or f64 (an int is an error) |
| `f32(x)`, `f64(x)` | | conversions (from int, f32, f64) |
| `allowed(w)` | bool | the server's weapon rules allow weapon `w` here |
| `firstAllowed(list, w)` | weapon | the first weapon of the list that is allowed, else `w` |
| `site(i)` | point | bomb site `i` (modulo the number of sites) |

The dice are shared by everything the bot does, so the ORDER of draws matters for reproducing a match; `and`,
`or` and `?:` don't evaluate what they skip (and draw nothing there).

Modes (for `mode ==` and `slot objective <mode>`): `vs bomb race koth payload ffa npayload duel arena tourney
hide tag points` (`arena` is 1v1) and `default`.

## 3. Canonical text (what the printer writes)

* The header lines, then every declaration in the AST's order. One space around binary operators, `=` and after
  commas and `:`; `f(a, b)`; `table[x]`.
* A declaration or statement is preceded by its `comments` lines (section 4), at its indentation. A trailing
  comment is written after one space.
* Numbers: the shortest decimal that reads back as the same value of its type (f32 values: as a float), written
  like JavaScript's `Number.prototype.toString` (`0.05`, `150`, `1e-7`, `1.5e+21`). In expressions an `f32`
  literal gets an `f` and a `.0` if it has no `.` or exponent (`1.0f`, `0.35f`, `1e-7f`), an `f64` a `.0` likewise
  (`1.0`, `0.8`), an `int` neither. Negative literals: `-1.0f`.
* Parentheses: only where needed, with these extra ones for readability: an `and` inside an `or`, a `?:` as the
  condition or the then-branch of another `?:` (not the else-branch: `a ? b : c ? d : e`), and the operand of
  `not` when it is a comparison or `in` (`not (a < b)`, `not (w in hookWeapons)`).
* `if` / `elif` / `else` arms are written inline (`if c: x = 1`) when their body is one statement that is not an
  `if` and has no comments of its own; otherwise as a block.
* Params: `param <name> <type> = <values>`, one value when all four are equal.
* Indent 2 spaces per block level; no line wrapping.

## 4. JSON AST (editor interchange)

Fields not listed are ignored on input. Every node may carry `"id"` (a string; the editor's layout key, kept by
the editor; the game ignores it and the exporter numbers nodes `n1`, `n2`, ... in document order). Blocks / graph
positions are not part of the text: the editor keeps them in a sidecar (`<name>.qbrain.layout.json`), keyed by id.

```jsonc
{
  "format": "qbrain-ast",
  "version": 1,                       // the text's `version`
  "name": "default",                  // `brain "..."`
  "levels": ["BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"],
  "decls": [ /* declarations in file order */ ],
  "tail": [ "# comment lines after the last declaration" ]
}
```

**Comments** on any declaration, statement, `elif` arm or `else`:
* `"comments"`: the lines between the previous node and this one, in order: each a comment line as written
  (starting with `#`, trailing spaces removed) or `""` for a blank line (several blank lines in a row are one).
  Omitted when empty.
* `"trail"`: a comment at the end of the node's first line (`"# full horizontal view cone"`). Omitted when none.

**Declarations** (`"kind"`):

```jsonc
{"kind": "param", "name": "fov", "type": "f32", "values": [1.55, 1.7, 1.85, 2]}   // 1 or 4 values; bool: true/false
{"kind": "curve", "name": "reactAngle", "arg": "off", "argType": "f32", "form": "expr", "body": <expr>}
{"kind": "curve", "name": "reactDist", "arg": "dist", "argType": "f32", "form": "steps", "base": 1, "points": [[40, 1.25]]}
{"kind": "curve", "name": "custom", "arg": "d", "argType": "f32", "form": "points", "points": [[0, 1], [20, 1.1]]}
{"kind": "list", "name": "close", "items": ["shotgun", "knife"]}
{"kind": "table", "name": "idealRange", "entries": [["shotgun", 4], ["sniper", 40]], "else": 14}   // "else" optional
{"kind": "slot", "name": "react", "body": [<stmt>...]}
{"kind": "slot", "name": "objective", "mode": "koth", "body": [<stmt>...]}
```

Numbers in JSON are the canonical decimal of the value (an f32 param 1.55 is `1.55`, not 1.5499999523).

**Statements** (`"op"`):

```jsonc
{"op": "set", "name": "r", "value": <expr>}
{"op": "if", "arms": [{"cond": <expr>, "body": [<stmt>...], "trail": "# on the if line"},
                      {"cond": <expr>, "body": [...], "comments": [...], "trail": "..."}],   // elif arms
             "else": {"body": [<stmt>...], "comments": [...], "trail": "..."}}               // optional
{"op": "return", "values": [<expr>...]}          // [] = plain return
{"op": "goto", "point": <expr>}                  // a point: {"op":"name","name":"hill.floor"} or site(i)
{"op": "fallback"}  {"op": "seek"}  {"op": "evade"}  {"op": "pass"}
```

The `if` node's own `comments` are the lines before the `if`; `arms[0]` has no comments, only a trail (the `if`
line's).

**Expressions** (`"op"`):

```jsonc
{"op": "num", "type": "f32", "v": 0.5}           // type: "f32" | "f64" | "int"; v may be negative
{"op": "bool", "v": true}
{"op": "name", "name": "fov", "ref": "param"}    // "ref" (param | sensor | local | output | weapon | mode | list |
                                                 //  point) is written by the exporter, ignored on input
{"op": "+", "a": <expr>, "b": <expr>}            // also - * / < <= > >= == != and or
{"op": "neg", "a": <expr>}                       // unary minus of a non-literal
{"op": "not", "a": <expr>}
{"op": "?:", "c": <expr>, "a": <expr>, "b": <expr>}
{"op": "in", "a": <expr>, "list": "hookWeapons"}
{"op": "call", "fn": "max", "args": [<expr>...]} // builtins and curves
{"op": "index", "table": "idealRange", "index": <expr>}
```

Parentheses are not nodes: the printer puts them back (section 3).

## 5. Slots

The game calls each slot at a fixed point with read-only **sensors** and takes its **outputs**. Every slot also
sees the common sensors `now` (f64, game seconds), `dt` (f32, the tick length), `mode` (mode), `level` (int,
0-3), `myId` (int, the bot's player id), `health` (f32) and `itMode` (bool: hide and seek or tag). Times written
to outputs are absolute (`now + ...`). The authoritative list, with docs and units, is `exportbrain`'s catalog.

| Slot | Called | Sensors | Outputs (catalog order) |
|---|---|---|---|
| `react` | a threat comes into view | `off` f32 (rad off the aim), `dist` f32, `sinceSeen` f64 | `r` f32 (reaction, s) |
| `hearing` | each look (20 Hz), per threat not in view | `dist` f32, `gunAge` f64 (s since it fired), `onGround` bool, `groundSpeed` f32, `dashing` bool | `heard` bool |
| `targetScore` | each look, per enemy in view | `dist` f32, `angleOff` f32, `current` bool | `score` f32 (lowest wins) |
| `objective <mode>` | each tick not fighting (and urgent) | objective sensors below | an action |
| `urgent` | each tick | objective sensors | `urgent` bool (keep going for the objective while fighting) |
| `pickWeapon` | each fight tick | `dist` f32 | `pick` weapon |
| `throwKnife` | each fight tick | `weapon` weapon, `dist` f32, `weaponRange` f32 | `throw` bool |
| `idealRange` | each fight tick | `weapon` weapon | `range` f32 (m) |
| `rejudge` | when the last `next` is due (and each new target) | - | `lead` f32, `drop` f32, `speed` f32, `next` f64 |
| `fire` | each fight tick, target in view | `err` f32 (aim error, rad), `dist` f32, `weapon` weapon, `throwKnife` bool, `weaponRange` f32, `ownBlast` bool (a blast weapon's splash would hit us) | `fire` bool, `alt` bool |
| `fightHook` | fight, target in view, grapple ready | `weapon` weapon, `dist` f32, `hookRange` f32 | `try` bool, `retry` f64, `go` bool, `hold` f64 |
| `strafe` | fight, when the last `next` is due | - | `dir` f32 (-1, 0, 1), `next` f64 |
| `pulledDash` | fight, pulled by an enemy grapple, dash ready, no drop near | `ideal` f32 | `dash` bool |
| `fightJump` | fight, on the ground, no drop near | - | `jump` bool |
| `fightDash` | fight, on the ground, no drop near, dash ready | `dist` f32 | `dash` bool, `closeIn` bool (else sideways) |
| `glance` | not fighting, nothing heard, when the last `next` is due | `idle` bool | `yaw` f32 (rad), `until` f64, `next` f64 |
| `hookRetry` | route grapple, when the last try is due | `grappleOnly` bool | `next` f64 |
| `routeHook` | route grapple, per candidate anchor | `rise` f32, `flat` f32 (m), `grappleOnly` bool | `want` bool |
| `bhop` | moving | `fighting` bool, `wpDist` f32 | `hop` bool |
| `runDash` | not fighting, on the ground, dash ready | `wpDist` f32, `straight` f32 (cos to the view) | `dash` bool |
| `dashJump` | a run dash was chosen | `hopping` bool, `wpDist` f32 | `jump` bool |
| `unstick` | when the last `next` is due | `moving` bool, `moved` f32 (m), `hasTarget` bool | `stuck` bool, `side` f32, `until` f64, `next` f64 |

Objective sensors: `checkpointsLeft`, `hasHill`, `hasCart`, `pointActive`, `bombPlanted`, `bombDropped`,
`bombCarried`, `attacker`, `carrierIsMe`, `carrierAlive`, `navReady`, `it` (bool), `siteCount` (int).
Points (for `goto`): `me.pos`, `checkpoint.next`, `hill.floor`, `cart.goal`, `point.floor`, `bomb.pos`,
`carrier.pos`, `site(i)`. A point whose map feature is missing is where the bot stands.

## 6. Parameters the game reads

Every `param` in `default.qbrain` is read by the game (the body's numbers included); their types are fixed.
`exportbrain` lists them with unit, range and doc. Hidden switches of the old code are parameters of their own
with the same values: `canLead` (was `leadMean >= 0.8`), `dashJump` (was the expert level), `fightHookChance`,
`hookRetry`, `hookRise`, `hookFlat` (were `airStrafe > 0.8`), `fleePredict` (was `predict`).

## 7. Changelog

* v1 (1.18.x): first version.
