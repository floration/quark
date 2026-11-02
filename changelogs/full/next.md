# Next (full, testing builds since 1.19.5)

Every change on `testing` since production 1.19.5, with what changed and why. The release PR renames this file to `changelogs/full/<VERSION>.md`.

## Black Hole: pillars and more space (owner, 2026-11-02: "increase the outer decoration on black hole, and add some pillars on the platform to increase visibility")

- **Pillars.** Four 1 x 1 x 7 m basalt pillars at (±8, ±5) with a glowing band 1 m under each top (`tools/maps/blackhole.py` `pillars`): something to judge height and drift by while flying, against the black sky. Too thin to be cover; the map checks still pass (16 m² of pillar tops only flight reaches, none out of bounds).
- **Outer decoration** (`space`), all non-solid (lava and far scenery), so the map's playable bounds and `Server::flownOut`'s 40 m margin are unchanged: two 3 m jets up and down out of the hole's poles (about 106 m each), a broken debris ring 100-112 m round the disc (every other 10°, far side only), and ten stray rocks as dark silhouettes 60-140 m out, some between the rock and the disc.

## The ball wall (owner, 2026-11-02: "make a block that only stops the ball so it cant fall off the maps (no ceiling tho)")

- New material 596 `ballwall` (map.h `kMatBallWall`, the 11th feature block). It is not solid to players, shots or sight, so it isn't in the collision grid: `Map::buildGrid` collects the ball walls' boxes into `Map::ballWalls`, and `ballMove` (pmove.cpp `ballSweep`) clips the ball against them after the map's own sweep. A wall it hits is plain rubber, like every other surface. A few boxes per map, so the extra loop costs nothing next to the grid sweep, and only the ball pays it.
- Never drawn in play (`hiddenMaterial`: clip and the ball wall; not baked, not lit, skipped by the minimap); the editor draws it see-through with clip.
- **Map format 14** (`upgradeMap13to14`: nothing to change, a format 13 map has no ball wall) and **protocol 68**: an older game can't read the new keyword. The shipped hand-made maps' version lines go to 14 (nothing else in them changes).

## Ball on more maps (owner, 2026-11-02: "ball needs a soccer field map obv ... you can add ball to the western map i forgot the name, and railyard")

- **Stadium** (`tools/maps/stadium.py`, new, built for ball): a 120 x 76 m astroturf pitch with flush painted lines (touchlines, halfway, centre circle, penalty and six-yard boxes), a 14 x 5 m goal 4.5 m deep at each end (galvanized frame, cargo net back, sides and roof: the ball and players stop at it, shots go through), the goal site the inside of the frame. Ball walls 4 m outside the touchlines and 2 m behind the goals, 40 m tall and open to the sky. Four tiers of stands all round (0.5 m steps, walkable; the side stands under a cantilever roof), plexiglass dugouts on the south touchline, floodlight masts in the corners. `holdfire 3`: the teams line up facing each other across the halfway line (a kickoff), so nobody fires for 3 s; that is also why the spawn sight check passes. Every map check passes, out of bounds included.
- **Express**: a goal across each canyon gap (x +-87..90, the gap's whole 9 m width, under the arch); the ball starts in the town square. **Railyard**: the goal is the front band of each team's building inside its three doors (roundhouse / goods shed, point mirrored); the ball starts on the main line at mid-yard.
- `quark_tests ball` now checks every map that offers ball: one goal per team, and the ball starts clear of the map and halfway between the goals (bridge 86 / 86 m, express 89 / 89, qk8 66 / 66, railyard 107 / 107, stadium 62 / 62). `ballStartPoint` / `ballGoalOwner` (gamemodes.h) expose the server's own rules for it.

## Bomb on Museum (owner, 2026-11-02: "add bomb to museum")

- Only lines added to q's hand-made `maps/museum.qmap`: `bomb` on its modes line, two sites (A: Mammals, x -45..-33, z -1..9; B: Minerals, x 36..48, z 0..9) and the bomb spawn sets (five attackers in the entrance hall, five defenders in the service hall). Nav times (attackers vs defenders): A 5.6 s vs 6.3 s, B 6.1 s vs 6.9 s, close to q's rule of the defenders about 1 s behind (the museum is too small for the 5 s walk). Every map check passes.

## Neutral payload on Suspension Bridge (owner, 2026-11-02: "add neutral payload to suspension bridge")

- The track (`bridge.py` `WEST`, point mirrored, 218 m): from A's toll plaza along the north lane (z 6.5) past the container spills at x -60, across the median at x -37.5 to the south lane (z -6.5) past the spills at x -20, and through mid-span (the cart's start) to the other half. The spills stagger the lanes, so the track weaves like the walk lanes do. The median gets 10 m gaps at x +-38 and mid-span for it (it was 4 m gaps every 40 m). Point-symmetric, so both teams' pushes are the same: nav to halfway 12.6 s for both. `cart_sweep` and every other check passes.

## Power bots: no team A edge (work board: "Power mode: bots favour team A")

- Measured with `quark_maptool bots` (8 advanced bots, 610 s): team A won 29-19 over 48 matches on Castle and Bridge, and with the slots swapped (B on the even slots) B won 29-19. So the edge was the slot, not the side: each bot's 20 Hz look phase was `id x golden ratio`, and with teams on alternating slots team A's looks came on average 0.03 of a period before team B's in every meeting.
- In a team mode the phase now comes from the bot's rank on its team (`teamRank`), team B's half a period after team A's, which makes every A-B pair's offset average exactly half a period. After: 43-49 over 96 matches, both slot orders, no pattern. Free for all keeps the slot phases.

## Modes lines

- `prefabs.py` no longer adds `winner` to 1v1 maps' modes lines (WINNER STAYS was retired in 1.19.5: 1v1 is winner stays).
