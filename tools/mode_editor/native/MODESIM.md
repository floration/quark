# The gamemode editor's headless runs: `modesim`, `modebatch`, `modecheck`

Three `quark_maptool` commands for the gamemode editor (`tools/mode_editor/`, design section 5.4). CMake globs this
folder into the `quark_maptool` target, next to the map editor's and the bot editor's commands.

- `modesim` plays one match of a mode on a map with bots and streams it as NDJSON: the score timeline, phase changes,
  round results, kills, objective events and the match end. It's deterministic: the same request and seed always
  give the same match.
- `modebatch` plays N seeded matches of the same setup on every core but one. It reports per team, per bot and per
  match: win rates, match lengths, rounds, score spread, timeouts, and a CSV of the matches.
- `modecheck` compiles a `.qmode` the way `modesim` does and checks that a map has what the logic reads. The answer
  is `quark_tests checkmode`'s JSON. It's there for an install that has `quark_maptool` but not `quark_tests`.

```
cmake --build build --target quark_maptool
quark_maptool modesim [--stream] [request.json]
quark_maptool modebatch [--stream] [request.json]
quark_maptool modecheck [request.json]
python3 tools/mode_editor/native/test_modesim.py [path/to/quark_maptool]   (by hand; not in CI)
```

The request is a JSON object, read from the file argument or from stdin. With `--stream`, the request is stdin's first
line and the lines after it are controls. The answer is NDJSON: one JSON object per line, flushed as it's written.
The last line is `{"type":"end",...}`. On a bad request the answer is one line `{"error": "...", "errors"?: [...]}`
(with `"type":"error"` under `--stream`) and exit code 1.

## How a match runs

Each match is the game's own `Server` (src/game/server.cpp) with bots, built here with `ServerConfig::custom` from the
request's rules. It is stepped tick by tick exactly as `frame()` steps it (`simulate`, the clock, the snapshot pass),
with no sockets read and no real-time clock. So a run is the same every time for the same map, rules, bots and seed.
The steps:

1. The map is written into a temporary data folder, which becomes the game's user data folder for the run. The
   player's real folder is never touched. The folder is removed afterwards.
2. `qsrand(seed)` seeds this thread's generator. That drives spawn picks, the bots' generators and `random(...)` in
   logic.
3. The server starts. The helper waits for the nav graph, and the bots join.
4. One second of warmup runs, then `forcestart`. Time `t = 0` is the match start. The first frame comes at `t = 0`.
5. It runs until the match ends (`until: "matchEnd"`, the default) or `seconds` of game time have passed.

`modebatch` runs each match on a fresh thread, because bots.cpp keeps per-thread state. So batch seed N is the same
match as `modesim` seed N.

Not modelled: humans, network latency, a map pool's rotation (`maps=` is ignored, and the match stops at its end), the
in-game vote. Messages the game sends only to clients (`s.message`, such as "overtime!" or a payload checkpoint) never
reach bots. The events below come from the game log and from state changes instead.

## Mode logic and the game hook (`logicApplied`)

The rules text is compiled with the game's language (`src/game/modelogic.h`). That gives the label (`name#hash8`),
the slots the file changes (interpreted), their worst-case cost, and errors with line:col. A logic error refuses the
run.

Whether the server actually runs the logic is up to the game side (mode editor phase 3: `src/game/gamemodes.*`,
`server.*`). Every answer says what happened:

- `logicApplied: true` only when the started server reports the logic it runs (`Server::logic_`, detected at
  compile time) and that logic has the file's hash.
- Until that hook is in the game, `logicApplied` is `false` and `logic.why` says so. The match plays the base mode
  with the file's key=value rules (time limit, score limit, rounds, weapons, movement...). Those do apply: they go
  through `CustomRules` as the game hosts a custom mode.
- `logic.hook` is `"pending"` (this build's server has no hook) or `"game"`.

The seam is the "logic hook" block at the top of `modesim_cmd.cpp` (`applyLogicHook`, `serverLogic`). When G1's API
lands, wiring it is a few lines there. Slot traces (`trace`) and `perf.logicNs` need per-slot timings from the game
side. Until then there are no `slot` lines and `logicNs` is `null`.

## `modesim` request

| key | default | what |
|---|---|---|
| `map` | | A map name (`"depot"`, or one of the player's maps), a path to a `.qmap`, `{name, text}`, or the map JSON (`parse`'s form). `text` / `file` / `name` at the top level work too. |
| `rules` (`text`) | none | The `.qmode` text: key=value lines, then `logic 1` and the logic. A file with logic but no `custommode=` line plays as a custom mode named after the logic (a warning says so). Empty: the built-in mode. |
| `mode` | the rules' `mode=`, else the logic's `base`, else the map's `for` mode, else `ffa` | `"vs"`, `"bomb"`, ..., `"1v1"` (or `"arena"`), or the number. Ignored, with a warning, when the rules name another. |
| `bots` | 8 (duel and 1v1: 2) | `N`, or a list of `{team: "a"/"b", skill, name}`. 1 to 32. Team modes alternate A, B, A, ... |
| `skill` | `"ADVANCED"` | `0..3` or `BEGINNER` / `INTERMEDIATE` / `ADVANCED` / `EXPERT`. |
| `seed` | 1 | Same seed, same match. |
| `seconds` (`secs`) | 900 | The most game time to run, 0.1..7200, counted from the match start. A bomb match can last 13 rounds of about 115 s, so the page sends 1800. |
| `until` | `"matchEnd"` | `"matchEnd"` (`"end"`) stops on the tick the match ends. `"seconds"` runs on to `seconds` (through the end screen and the next match). |
| `tickRate` | 125 | 1000, 500, 250 or 125. Bots play the same at every rate. |
| `rate` | 4 | Frames per game second, 0.1..1000 (the tick rate caps it). |
| `realtime` / `speed` | `false` / 1 | Paced at `speed` x real time when `realtime`; otherwise as fast as the CPU goes. |
| `trace` | `false` | Slot calls with their cost. Needs the game hook: until then, a warning and no `slot` lines. |
| `positions` | `true` | Player positions in frames. |

## `modesim` answer

```
{"type":"start","map":"depot","base":"bomb","mode":"bomb","label":"rush#648ff484","custom":"rush",
 "logicApplied":false,"interpreted":[],
 "logic":{"present":true,"applied":false,"hook":"pending","label":"rush#648ff484","hash":"648ff484",
          "interpreted":["plant"],"uses":["bomb"],"needs":["sites"],"worstCase":{"plant":11},"why":"..."},
 "teams":true,"tickRate":125,"seconds":900,"until":"matchEnd","warnings":["..."],"seed":5,"rate":4,
 "realtime":false,"speed":1,"trace":false,"bots":[{"id":0,"name":"BOT ANNA","team":"a","skill":"ADVANCED"},...],
 "serverLog":["..."]}
{"type":"frame","t":12.5,"tick":1687,"phase":"live","round":1,"clock":92.5,"scoreA":0,"scoreB":0,"flags":[],
 "attackers":"a","obj":{"bomb":{"state":"carried","carrier":4,"pos":[..]}},
 "players":[{"id":0,"team":"a","alive":true,"pos":[x,y,z],"score":0,"kills":0,"deaths":0,"objTime":0},...],
 "events":[{"type":"kill","killer":4,"victim":1,"weapon":"rifle","pos":[..],"t":12.31},...]}
...
{"type":"end","t":391.68,"reason":"matchEnd","label":"rush#648ff484","logicApplied":false,
 "winner":{"team":"b","player":null,"name":"TEAM B","text":"TEAM B wins the match"},
 "rounds":4,"roundResults":[{"type":"roundEnd","round":1,"text":"TEAM B wins the round (time)","reason":"time",
                             "winner":"b","scoreA":0,"scoreB":1,"t":108},...],
 "result":{"scoreA":0,"scoreB":3,"round":4,"phase":"matchEnd","players":[{"id","name","team","score","kills",
           "deaths","objTime","damage","won"},...]},
 "perf":{"ticks":48960,"simSeconds":1.4,"ticksPerSecond":35000,"speedup":280,
         "tickNs":{"avg":25000,"p99":60000,"max":2000000},"logicNs":null,"wallSeconds":1.5,"startSeconds":0.02}}
```

- `start.interpreted`: the slots the server interprets (empty unless `logicApplied`). `logic.interpreted`: the
  slots the file changes against its base mode's default, which are the ones the game would interpret.
- **frame**: `phase` is `warmup` | `freeze` | `live` | `roundEnd` | `matchEnd`. `clock` is the phase's time left
  (s). `flags` is any of `overtime`, `contested`, `drain`, `rps`. `attackers` appears in team modes. `obj` carries
  the base mode's objective: `bomb {state none|carried|dropped|planted|exploded|defused, carrier, pos, timer,
  acting, progress}`, `cart {dist, len}`, `hill {owner, target}`, `point {index, count, capture}`. Players carry
  `it` / `out` (hide, tag), `found` (hide), and `checkpoint` / `finished` (race). Names are in `start.bots`.
  Positions are rounded to 1 cm.
- **events** (in each frame, since the last one, in time order; `t` in s since the match start):

| type | fields | from |
|---|---|---|
| `kill` | `killer` (-1 world), `victim`, `weapon` (a weapon name, `fell`, `bombed`, `drained`, `grapple`, `self`, `world`), `pos` | game log |
| `roundEnd` | `round`, `text`, `reason` (the part in brackets: `time`, `bomb exploded`, ...), `winner` (`a`/`b`/null), `player`, `scoreA`, `scoreB` | game log |
| `matchEnd` | `text`, `winner`, `player` | game log |
| `phase` | `phase`, `from`, `round` | state |
| `score` | `scoreA`, `scoreB` | state |
| `plant` / `defuse` / `explode` / `bombDropped` / `bombCarried` / `bombReset` | `who`, `pos` | state |
| `capture` | `index`, `point` (`A`...), `team` | game log |
| `hill` | `owner` (`a`/`b`/null) | state |
| `cartCheckpoint` | `bonus` (s added to the clock), `dist` | state (payload: the clock jumps) |
| `checkpoint` / `finish` | `who`, `index` | state (race) |
| `it` | `who` | state (hide, tag) |
| `tag` | `who`, `victim` | game log |
| `overtime`, `modifier`, `tiebreak`, `tourney` | `text` | game log |
| `log` | `text` | any other game log line (a mode's `log()` / announcements, once the game logs them) |

- **end**: `reason` is `matchEnd` | `time` | `stopped`. `winner` is null unless the match ended. `leader` is who
  was ahead when time ran out first (`{team}` or `{player}`, null when level). `rounds` counts round results.
  `perf.speedup` is game seconds per wall second while ticking. `perf.tickNs` is `Server::simulate` per tick (movement,
  bots, rules and logic together).
- **controls** (`--stream`, one per line): `pause`, `resume`, `step` (one frame while paused), `stop`,
  `speed <x>` (paced at x), `speed max`, `realtime on|off`.

## `modebatch`

The request is `modesim`'s setup plus:

| key | default | what |
|---|---|---|
| `seeds` | 8 | `N` seeds from `firstSeed`, or a list of seeds (`matches` is an alias for N) |
| `firstSeed` | `seed`, else 1 | |
| `threads` | every core but one | at most 64, and at most N |
| `matchTimeout` | 0 (none) | wall seconds a match may take (`reason: "wallTimeout"`) |
| `progressEvery` | 1 | wall seconds between `progress` lines |

The answer:

```
{"type":"start", ...modesim's start fields..., "matches":6,"threads":3,"seeds":[...],"bots":[{"index","team","skill"}]}
{"type":"match","done":1,"total":6,"index":2,"seed":5,"reason":"matchEnd","t":730.66,"winner":{...},
 "rounds":7,"scoreA":0,"scoreB":7,"wallSeconds":2.3,"players":[...end.result.players...]}
{"type":"progress","done":3,"total":6,"wallSeconds":4.1}
{"type":"end","reason":"done","label","base","map","logicApplied","logic",
 "matchesPlayed":6,"errors":0,"timeouts":0,
 "teams":{"a":{"wins":0,"winRate":0},"b":{"wins":6,"winRate":1},"draws":0},     (team modes; else "draws")
 "players":[{"index","name","team","skill","matches","wins","winRate","kills","deaths","score","objTime","damage"}],
 "length":{"avg","min","p50","max"},"rounds":7.67,"scoreSpread":6.33,
 "matches":[{"index","seed","reason","t","winner","leader"?,"rounds","scoreA","scoreB","wallSeconds"}],
 "csv":"index,seed,reason,seconds,rounds,winnerTeam,winnerPlayer,winnerName,scoreA,scoreB,wallSeconds\n...",
 "perf":{"wallSeconds","threads","matchesPerSecond","ticks","ticksPerSecondPerMatch","speedupPerMatch","speedupTotal"}}
```

- A **timeout** is a match that reached `seconds` (or `matchTimeout`) before it ended. It counts in `timeouts`,
  not in wins or draws. Wins plus draws plus timeouts equals the matches played.
- `players` holds per-match averages per bot slot. `scoreSpread` is the mean |A - B| in team modes, else the winner's
  score minus the runner-up's.
- The only control is `stop` (the matches that are running finish; `reason: "stopped"`).

## `modecheck`

Request: `{text | rules | file, base?, map?}`. The answer is `checkmode`'s JSON (`modeDiagJson`): `{ok, errors,
warnings, name, base, hash, label, uses, interpreted, slots, params, needs}`. It adds `map` and an error for each
feature the logic needs that the map lacks (hill, bomb site, payload track, multi point zone, race checkpoint), and
`serverRunsLogic` (whether this build's server has the logic hook). Exit 1 on errors, with `error` set to the first
one.

## Speed (this machine: 4 cores, Release build)

From `test_modesim.py`, 60 s of game time per mode on its own map: about 180x (koth, 8 bots) to 1100x (duel, 2 bots)
real time on one core. A whole bomb match on depot (731 s, 8 bots) takes 2.7 s, including the start. `modebatch` runs
6 bomb matches on depot in about 7 s on 3 threads: 0.8 matches/s, 230x real time per match, 640x in total. Server tick
`avg / p99` is 20-45 us / 50-90 us with 8 bots. Single ticks of 2-14 ms show up as `max` (not
investigated; this machine was busy): judge by p99.
