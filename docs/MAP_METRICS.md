# How maps are measured

`build/quark_tests mapreport` measures every map in `maps/` and writes [MAP_REPORT.md](MAP_REPORT.md). Run it after you change a map's generator (`tools/maps/<name>.py`, or the shared `tools/maps/prefabs.py`) and rebuild (the build regenerates `maps/`; `python3 tools/make_maps.py --map <name>` makes one map alone).

`sh tools/map_shots.sh` then takes 16 screenshots of each map and saves them as one 4×4 sheet in `docs/map_shots/<map>.jpg`:
- **8 orbit shots**, from above, every 45°.
- **8 eye-level shots**, from floor spots spread as far apart as possible. Each one faces the longest open view from that spot.

The numbers say *what* a map is. The sheets are for the judgement calls: whether it reads well, whether it looks like a place, whether its landmarks stand out.

## The game the numbers are tuned to
- **Movement** (all intended, the owner's call: decision 5.4):
  - running speed: 8 m/s;
  - jump: 7.3 m/s up, clears about 1.2 m; holding jump bunny-hops on the first grounded tick, before friction (auto-bhop);
  - step-up: 0.55 m;
  - air strafing gains speed without a limit;
  - dash: a 0.12 s burst, then you keep your speed + 3 m/s, at most 32 m/s (the limit is intended); it can be tilted up, which turns speed into height;
  - grapple: 45 m reach;
  - ladders.
- **The movement envelope** the tech checks use is not written down here but measured, with the game's own movement code, each time the report runs (so it follows any change to movement). The report's first table lists it:
  - **speed from a standstill**: a good player holding jump, dashing on cooldown while the dash still adds speed, and strafing (each millisecond the strafe that gains the most): the metres of moving it takes to reach 12, 16, 20, 24 and 28 m/s. Circling builds speed too, so the metres count wherever they're run;
  - **take-offs**: from each of those speeds, a jump and, after 0-0.4 s, a dash tilted 0-90° up (or none): every arc followed until 40 m below the take-off. The envelope is the highest any arc is at each distance across; a slower take-off is always possible, so each speed's envelope includes the slower ones'. Past 28 m/s an upward dash gets no higher (its exit speed is capped), so the speeds stop there;
  - **grapple pulls**: the speed a pull leaves you with, from each speed.
- **Weapon ranges** decide which fights a sightline allows:

| band | distance | weapons |
|---|---|---|
| close | under 6 m | shotgun, knife |
| mid | 6–20 m | rifle, rockets, stickies |
| long | 20–24 m | laser (to its reach: 24 m since 1.17), crossbow |
| extreme | over 24 m | sniper (and the scoped rifle) only |

The long band ends at the laser's reach, read from the weapon table (`weaponDef(WPN_LASER).range`), so it follows any change to the laser.

## Samples and paths
- **Playable floor:** surfaces you can walk to from a spawn and that are above the kill height, on the collision-checked walk grid (`tests/walkgrid.h`: the player's real hull, steps, jumps, gaps and ladders; see *Mode fit* below). Floor nobody can walk to gets no credit in any metric (floor area, levels, height range, loops, samples, the footprint). Roofs and wall tops only a grapple reaches don't count as floor (they are still used for the movement-tech check, as cheese surfaces).
- **Reachable spots:** the walk grid's standing spots a player gets to from any spawn. The loops and the ways onto an area are counted on these.
- **Floor samples:** a point every 2 m on every walkable surface of the bots' nav graph, kept only where a player fits. A surface under 2 m across (a stair step, a beam) gets one row down its middle. Up to 1500 per map for the sightline metrics, evenly spread; the route and shortcut checks use up to 2500.
- **Walking distance:** measured along the nav graph the bots use. Links cost centre-to-centre distance, plus extra for gaps you jump and steps you climb. From your spot to your floor's centre, and from the last floor's centre to the target, it's a straight line.
- **Area objectives** (the hill, bomb sites, capture points): distance to the nearest standable floor inside the area. A team's distance is the mean over its spawns; if any of its spawns can't walk there, it's unreachable.

## Decor / detail
| metric | how it's measured |
|---|---|
| detail brushes | Brushes thinner than 0.6 m or smaller than 2 m³: trim, rails, rungs, glow strips, signs. One buried in solid on all six sides (a seam inside a wall, trim sunk into the floor) dresses nothing and doesn't count. Backdrop scenery outside the walls is counted separately as **skyline**. |
| detail per 100 m² | Detail brushes ÷ walkable floor area × 100. Density, so big maps aren't rewarded for size. |
| bare walls | From every floor sample, 16 rays at eye height. Each wall hit 6–40 m away (a wall you look at) is checked for a detail brush within 1 m. The share with none is "bare". Lower is better. |
| materials, glow, metal, lights | Counts straight from the map file (each material on its own: trim isn't counted as lava; clip isn't a material anyone sees). |

**Decor score** = 4·min(detail per 100 m² ÷ 16, 1) + 4·(1 − bare walls) + 2·min(lights ÷ 40, 1)

## Layout and sightlines
| metric | how it's measured |
|---|---|
| floor m² | Total walkable surface. |
| levels | Floor heights, grouped in 1.5 m bands, that have at least 10 m² of floor. |
| height range | Highest floor minus lowest floor, among the levels (a post top or a crate doesn't make a map vertical). |
| loops | Ways round, counted on the reachable walk grid (they used to be counted as cycles between nav-graph surfaces, which counted floor fragments: a flat platform had 112). The standing spots are gathered into 4 m tiles; in each tile, each piece you can move about in without leaving it is a node (a floor and a crate you can jump onto are one node; the two sides of a wall, or a street and the roof over it, are two). Nodes link where a **two-way** move (walk, step, jump up and back down, a gap jumped both ways, a ladder) crosses into the next tile; a drop you can't climb back up is no way round. Every 2 × 2 block of linked tiles is filled in, so what's left (links − nodes + parts − filled blocks: the first Betti number) is the holes you can go round: a route round each obstacle wider than a tile, each building, each pit; and up one way, down another (stairs and a ladder) where the two differ. A flat platform has none; a crate or a pillar narrower than a tile makes none. Whether an obstacle 3–7 m across fills a whole tile depends on where the grid falls, so the count is the mean over four placements of the grid, half a tile apart. Scored per 1000 m². |
| dead ends | Playable surfaces with over 4 m² you can stand on and get to, only one nav link, and no more than one side you can step off. Crate tops and ledges you can drop off are not dead ends; nor is the sill of an arch cut through a wall (its wall top is under the wall above it but for the arch). Each one costs 0.3. |
| visible pairs | 30,000 random pairs of floor samples; the share that can see each other at eye height (1.6 m). |
| range bands | Of the pairs that see each other, the share in each weapon band above. |
| cover | From each sample, 16 directions at chest height (1 m). A direction counts as blocked if something solid is within 3 m. "Cover" is the share of samples with at least 4 blocked directions. |

**Layout score** = 2·min(levels ÷ 8, 1) + 2·min(loops per 1000 m² ÷ 3, 1) + 3·min(cover ÷ 0.55, 1) + 3·range mix − 0.3·dead ends

Full marks for loops at **3 per 1000 m²**: rooms about 18 m across with four ways out each (a room graph with E = 2V has about V loops, one per 330 m²). The old count needed 25 per 1000 m² because it counted floor fragments; 19 of 20 maps got full marks.

**Range mix** is the evenness of the close / mid / long shares: their entropy, 0–1. A map where every fight happens at one range scores 0; an even three-way split scores 1. Extreme range is left out because only the sniper uses it.

## Movement
Movement is the mean of six sub-scores, each 0–10:

| sub-score | how it's measured | 10 when |
|---|---|---|
| grapple anchors | From each sample, rays at 29° and 57° above level in 8 directions, out to 45 m. The sample counts if one hits something at least 3 m above your feet. | 80% of the floor has an anchor |
| speed lanes | From each sample, the farthest straight run with floor under you, in any of 16 directions (1 m steps; drops up to 2.5 m allowed). The 90th percentile. Strafe-jump and dash runs need these. | 40 m |
| vertical | How high the floor you play on goes: 5 × min(height range ÷ 12 m, 1), plus 0.5 per floor level (up to 6), plus min(jump links ÷ 0.25, 1), plus 1 if the map has ladders. A flat platform scores about 1, however many little jumps it has (Dojo used to score 10 with a 0 m height range). | 12 m of height, 6 levels, jumps, ladders |
| headroom | Share of samples with more than 6 m clear above: room for vertical dashes and rocket jumps. | 90% |
| risk | Share of samples within 2 m of a lethal drop. | 8–35% (race maps always 10) |
| tech | See below. | |

### Tech: what dash, grapple and good jumps do to the map
Two graphs over the same floor samples:
- **The run:** links the bots' nav graph allows: along a surface, steps, ordinary jumps (gaps up to 2.6 m, 1.15 m up) and ladders.
- **The tech path:** all of that, plus leaps and grapple pulls, with your speed as part of where you are:
  - **Speed** is one of the envelope's speeds. Moving along the path builds it as the envelope's run-up says (a path that circles to gain speed pays for the metres it circles). A ladder costs all of it; a grapple pull leaves you at what the pull gives.
  - **A leap** goes anywhere under the envelope of the speed you have: standing (8 m/s) that is 13.5 m across level, 4.2 m up at most; at 28 m/s, 61 m across or 12.5 m up. The arc must be clear for a player-wide body: three lines (the middle and both shoulders, so a 0.4 m slit doesn't count as a way through) up out of the take-off, across at chest height over the higher end (or over the arc's own apex, when only a fast take-off reaches), and down into the landing. Clip blocks it (players collide with clip). Leaps longer than 24 m are taken between every third sample, a few metres apart, to keep the report quick.
  - **A grapple pull** reaches any surface hit within 45 m and lands you on the floor next to the anchor; the way there must be open to a player (the hook passes through clip, you don't). A pull leaves you at 24 m/s, or at your own speed if that was faster (28 m/s in, 28 m/s out: the envelope table's last row).
  - Nobody stands inside clip, whatever the nav graph says, so those samples are left out of both graphs.

Both are measured in metres along the samples, so the two compare like for like.

**Out of bounds:** with the whole envelope, from the first spawn, the report lists the floor a good player reaches that the run doesn't (roofs, wall tops: fine) and how much of it lies outside the playable floor's footprint (its bounding box, plus 1 m). Any out-of-bounds floor fails the map: keep players in with clip or walls they can't dash up.

Every objective (each race checkpoint, bomb site, capture point and the hill) and every spawn also gets a sample of its own, and on the biggest maps (where the samples are a few metres apart) two samples on one floor or two linked floors are a run as far as three sample spacings apart: a thin sampling must not cut a leg in two.

**Race maps**, per leg (from one checkpoint to the next):
- **Shortcut:** run length ÷ tech length. A good player should not cut a leg: 10 at 1.25× or less, 0 at 2.5×. The worst leg counts. A leg the run can't do at all scores 0 (it used to count as a 1.00× leg).
- **Challenge, measured on the tech path:** what the course still asks of a player who dashes and grapples well.
  - Jumps per 100 m: full marks at 8. A dash-jump counts as one jump; a grapple pull counts as none.
  - Narrow footing actually run on (under 2 m wide: beams, ledges, pads): full marks at 30%.
  - How much of the course the grapple carries you: 2 points at 0%, none at 30% or more.
  - The three are worth 4, 4 and 2 points.
- On a map built for racing, tech = the lower of the two. On a map where the race is a side mode, only the shortcut counts.

The report lists every leg's length, jumps, narrow metres and tech length, so you can see which stretch is too easy or can be cut.

**Other modes:** for every spawn to every objective (or to the other spawns), how much shorter the tech path is on average. 10 between 1.15× and 1.8×: tech should pay off without making the map trivial. Under 1.15× it barely matters; over 1.8× it skips the map.

## Modes (balance)
Each mode the map supports gets the measurement that matters for it:

| mode | measured |
|---|---|
| vs / duel / 1v1 | Walking distance from A's spawns to the nearest B spawn, and from B's to A's. **Fairness = shorter ÷ longer.** It doesn't depend on direction, so a symmetric map scores 1.00. Also the time to cross at 8 m/s. |
| king of the hill | Each team's mean walking distance to the hill. Fairness = shorter ÷ longer. |
| neutral payload | Each team's distance to the cart's starting point (halfway along the track). Fairness = shorter ÷ longer. |
| payload | Attackers to the cart, defenders to the halfway point, and the track length. One-sided by design (sides swap), so no fairness score. |
| bomb | Each team's walk to every site, and the attackers' arrival minus the defenders' there (positive: the defenders are there first). The owner's band (decision B2) is **−1 to +1 s at every site**: the game is fast, and a side that takes a site first holds it. (The sides swap spawns with roles, and the band is symmetric, so it holds for both halves.) |
| ffa / tourney | Spawn count, the closest pair of spawns, and the share of spawn pairs that can see each other. Lower is better: spawns in view of each other mean spawn kills. |
| race | Walking length of the checkpoint route, as time at running speed (8 m/s) and at a good strafe-jump speed (20 m/s). |
| multi point | For each point: the attackers' and the defenders' walk to it from the spawns that stage uses, and the attackers' arrival minus the defenders'. **−1 to +1 s at every point** (B2). |
| hide and seek / tag | Hiding spots: the share of floor samples visible from fewer than 5% of the others. |
| ranked | The share of sightlines under 20 m (rifle / rocket fights) and over 20 m (laser / crossbow / sniper). Vanilla wants a mix, vampire (laser) wants mid range, instagib (sniper) wants long. |

**Balance score** = 10 × the fairness of the map's **worst** objective (C1: per team, for every objective of every team mode the map offers, not the mean of the modes): vs / duel / 1v1 / a tourney match (the a / b spawns) score their spawn fairness, king of the hill its hill, neutral payload its cart; every bomb site and capture point scores the timing band (1 within −1..+1 s, falling to 0 four seconds outside it). The report names the worst objective. Payload is one-sided (sides swap) and free for all, hide, tag and race have no teams, so a map with only those has no balance score. Measuring through the nav graph has about ±15% noise, so mirrored maps can read 0.85–1.00. **Below 0.85 is worth checking** in the shots.

## Creativity and originality
| metric | how it's measured |
|---|---|
| setting | Each map declares the place it is (`setting city block`, `setting space station`...). A setting no other map has is worth 1.5. |
| features | Present or not: ladders, lethal drops (1% or more of the floor within 2 m of a drop that kills, measured: a `void` line alone isn't one), water / lava (40 m² or more of it: a fountain's basin isn't), wallbangs, a moving cart, 5+ floor levels, 12 m+ of height, indoor and outdoor (15–85% of the floor roofed), fully indoor (over 85%), irregular outline (the footprint fills under 80% of its bounding box), lit interiors (12+ lights), lots of hiding spots (over 15%). |
| creativity score | 0.75 per feature, plus 0.5 for each feature at most a quarter of the maps have (up to 2), plus 1.5 for a setting of its own. A map can't reach 7 on a checklist alone. |
| originality | Each map is turned into 9 numbers: levels, height range, loops per area, visible pairs, cover, grapple anchors, speed lanes, short-range share, detail density. These are normalised across all maps. Distance to the nearest other map is the root mean square of the differences, reduced by up to half when the top-down footprints match. **Footprint** = a 24×24 grid of floor heights, compared in all 4 rotations. |
| originality score | 8 × distance to the nearest map, capped at 10. "Most similar map" names that neighbour. |

## Mode fit
Each map is built for one mode (its `for` line), and is judged on what that mode needs.

**A map built for every mode** (no `modes` line: Lodestone, owner decision B1) is graded in **every** mode it offers too: the report lists each mode's fit under it. The bar holds its attack / defend timing (every bomb site and capture point) and its balance (every objective of every mode) in all of them, as for any map; its mode-fit score is its built-for mode's, and any other mode under 7 is printed as a `NOTE` and marked "below 7" in the report (a duel or team-vs fit can't reach 7 on an 83,000 m² map: those modes want 300–3,000 m²).

**Unreachable scores 0.** First, every route each mode the map offers needs is walked on a collision-checked walk grid (`tests/walkgrid.h`): columns every 0.25 m, the player's real hull against the real collision (clip included), steps of 0.55 m, jumps up 1.15 m, gaps up to 2.5 m along a jump's arc, ladders, crouching, but no dash or grapple. The routes: each team's spawns to every enemy spawn (vs, duel, 1v1), every free-for-all spawn to every other (free for all, hide, tag, a tourney's warm-up), every team spawn to every bomb site and the hill, the whole payload track (within push range, every 4 m), each multi point stage's spawns to every point from theirs on, the race spawns to every checkpoint. One missing route and mode fit is 0 (the report's "routes" line says which); `quark_tests` fails it too.

| mode | what it needs (10 when) |
|---|---|
| duel / 1v1 / tourney | fair spawns (3), a compact map of 300–2000 m² (2.5), spawns 2.5–7 s apart at a run (2.5), loops (2) |
| team vs | fair spawns (3), 700–3000 m² (2.5), spawns 4–9 s apart (2.5), loops (2) |
| bomb | both sides reach every site within −1..+1 s of each other, at the worst site (4), two sites (3), loops (3) |
| king of the hill | equal walks to the hill (4), 4+ ways onto it (3), cover on 35% of it (3, measured every 1 m on the hill). **Ways onto it** are counted on the walk grid: every move from outside onto it, by the direction it comes from (8 sectors round its middle) and its height (3 m bands). A whole open side is one way; a doorway, a stair, a drop from a balcony are others. (Counting nav links gave Downtown's plaza 110.) |
| payload / neutral payload | a 70–160 m track (3.5), cover on 35% of the floor beside it (3.5), fairness (3, neutral only) |
| free for all | 12+ spawns in the pool free for all draws from (3), no spawn in sight of another (4; 30% in sight scores nothing), loops (3) |
| race | the race tech check above |
| hide and seek / tag | hiding spots on 15% of the floor and loops; hide weighs hiding 6:4, tag weighs loops 6:4 (a chase needs ways round) |

Loops in mode fit score full marks at 3 per 1000 m², as in layout (they were 20 per 1000 m² of the old count).
| multi point | 3–6 points (1.5), 25–90 m of walking from one point to the next (2), both sides' stage spawns reach each point within −1..+1 s of each other (3), cover on 35% of each point (2, measured every 1 m on the point), 3+ ways onto each point (1.5, counted like the hill's). Every part is graded at its **worst** point, not the average. |

## The bar
Every map must score **at least 7 in decor, layout, movement, creativity, mode fit and balance** (balance where it has team modes). It must also declare one mode and a setting, nothing out of bounds may be reachable, and at **every bomb site and capture point of every mode it offers** the attackers' arrival minus the defenders' must be within **−1..+1 s** (owner decision B2; the report's *Balance* table lists each). A category a map is let off by its own data (a `# exempt <category> ... : <why>` line its generator writes: Dojo, the owner's bare eye-to-eye arena kept simple on purpose, is let off layout, movement and creativity) isn't held to the bar; no map is let off anything by its name.

**Human-made maps** (the owner's rule, [JUDGMENT_CALLS.md](JUDGMENT_CALLS.md)) are measured and scored exactly the same way, and every check `quark_tests` runs on maps runs on them too, but they don't fail the run: their misses are printed as `HUMAN-MADE` and listed in the report's *Human-made maps* section, for balancing. A map is human-made when it has a `handmade` line (the in-game editor writes one on every save), when `tools/make_maps.py` didn't write it (its first line is `# generated by tools/make_maps.py`), or when it's an override in `assets/overrides/maps/`. Generated maps keep the strict bar.

The tests also climb every ladder and walk up every staircase with the real movement code (`quark_tests ladders`, `quark_tests stairs`). No staircase may cut through another walkable surface, or hang over one with less than a player's height of clearance. And for every map:
- `quark_tests allmaps`: the routes above, walked (and the bots' nav graph links them too).
- `quark_tests spawns`: every pool the server draws spawns from, in every offered mode, side and stage, holds a full 8-player server's share (8 for free for all, hide, tag, race and a tourney's warm-up; 4 a side in team modes, 2 in duel, 1 in 1v1), 1.5 m apart. Every spawn faces 2 m of open space at eye height; spawns used together stand 1.5 m apart; in team modes no spawn sees an enemy spawn (except on a `holdfire` map, which starts both sides face to face on purpose).
- `quark_tests cartsweep`: the payload cart's box, every 10 cm along the track, touches nothing solid, clip or trim.
- ranked maps (`quark_tests` maps group): every brush, spawn (A with B, yaw turned), ladder, light and objective has its point-symmetric twin, and the sun is overhead.

Debugging: `MR_DEAD=<map>` lists dead ends, `MR_LEVELS=<map>` the floor area per height band, and `MR_PATH=1` every link of the tech paths. `LADDER_TRACE=<map>` (with `LADDER_AT=<x>`) and `STAIRS_TRACE=<map>` trace a climb. `MR_OOB=<map>` lists the out-of-bounds floor it reaches (outside the playable surfaces' bounding box plus 1 m), `MR_COVER=<map>` prints every sightline sample with how many of its 16 directions are blocked (to see where a map lacks cover), `MR_FIELD=<map>` prints every nav surface's walk to each objective and `MR_FROM=<map>` each surface's mean walk from team A's and team B's spawns (for placing spawns and sites in the timing band), `MR_ONLY=<map>` measures one map alone (quick, but its originality and rare-feature points are then against itself only) and `MR_TIME=1` times the envelope. Originality is left out: a map can't help resembling the others. `quark_tests` fails if any map is below the bar, and `quark_tests mapreport` prints which.

Every structure on a map should have a reason to be there, for the game or for the place. [MAP_STRUCTURES.md](MAP_STRUCTURES.md) lists them.

## What the numbers can't judge
Look at the sheets for:
- **Readability:** can you tell where you are from each eye-level shot?
- **Landmarks:** does each area have something you'd name it by?
- **Theme:** do the colours and props make it look like a place?
- **Flow:** does the route the scores describe match what the orbit shots show?
