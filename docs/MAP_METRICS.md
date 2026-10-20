# How maps are measured

`build/quark_tests mapreport` measures every map in `maps/` and writes [MAP_REPORT.md](MAP_REPORT.md). Run it after you change a map or `tools/make_maps.py`.

`sh tools/map_shots.sh` then takes 16 screenshots of each map and saves them as one 4×4 sheet in `docs/map_shots/<map>.jpg`:
- **8 orbit shots**, from above, every 45°.
- **8 eye-level shots**, from floor spots spread as far apart as possible. Each one faces the longest open view from that spot.

The numbers say *what* a map is. The sheets are for the judgement calls: whether it reads well, whether it looks like a place, whether its landmarks stand out.

## The game the numbers are tuned to
- **Movement**:
  - running speed: 8 m/s;
  - jump: 7 m/s up, clears about 1.2 m;
  - step-up: 0.55 m;
  - dash: a 0.12 s burst, then you keep your speed + 3 m/s; it can be tilted up or down;
  - grapple: 45 m reach;
  - strafe-jumping builds speed;
  - ladders.
- **Weapon ranges** decide which fights a sightline allows:

| band | distance | weapons |
|---|---|---|
| close | under 6 m | shotgun, knife |
| mid | 6–20 m | rifle, rockets, stickies |
| long | 20–32 m | laser (max 32 m), crossbow |
| extreme | over 32 m | sniper (and the scoped rifle) only |

## Samples and paths
- **Floor samples:** a point every 2 m on every walkable surface of the bots' nav graph, kept only where a player fits. A surface under 2 m across (a stair step, a beam) gets one row down its middle. Up to 1500 per map for the sightline metrics, evenly spread; the route and shortcut checks use up to 2500.
- **Walking distance:** measured along the nav graph the bots use. Links cost centre-to-centre distance, plus extra for gaps you jump and steps you climb. From your spot to your floor's centre, and from the last floor's centre to the target, it's a straight line.
- **Area objectives** (the hill, bomb sites): distance to the nearest standable floor inside the area.

## Decor / detail
| metric | how it's measured |
|---|---|
| detail brushes | Brushes thinner than 0.6 m or smaller than 2 m³: trim, rails, rungs, glow strips, signs. Backdrop scenery outside the walls is counted separately as **skyline**. |
| detail per 100 m² | Detail brushes ÷ walkable floor area × 100. Density, so big maps aren't rewarded for size. |
| bare walls | From every floor sample, 16 rays at eye height. Each wall hit 6–40 m away (a wall you look at) is checked for a detail brush within 1 m. The share with none is "bare". Lower is better. |
| materials, glow, metal, lights | Counts straight from the map file. |

**Decor score** = 4·min(detail per 100 m² ÷ 12, 1) + 4·(1 − bare walls) + 2·min(lights ÷ 30, 1)

## Layout and sightlines
| metric | how it's measured |
|---|---|
| floor m² | Total walkable surface. |
| levels | Floor heights, grouped in 1.5 m bands, that have at least 10 m² of floor. |
| height range | Highest floor minus lowest floor. |
| loops | Independent cycles in the nav graph (links − surfaces + connected parts). More loops means more ways around: flanks, rotations, escapes. Scored per 1000 m². |
| dead ends | Surfaces over 4 m² that you can reach from a spawn, with only one nav link and no more than one side you can step off. Crate tops and ledges you can drop off are not dead ends. Each one costs 0.15. |
| visible pairs | 30,000 random pairs of floor samples; the share that can see each other at eye height (1.6 m). |
| range bands | Of the pairs that see each other, the share in each weapon band above. |
| cover | From each sample, 16 directions at chest height (1 m). A direction counts as blocked if something solid is within 3 m. "Cover" is the share of samples with at least 4 blocked directions. |

**Layout score** = 2·min(levels ÷ 8, 1) + 2·min(loops per 1000 m² ÷ 20, 1) + 3·min(cover ÷ 0.45, 1) + 3·range mix − 0.15·dead ends

**Range mix** is the evenness of the close / mid / long shares: their entropy, 0–1. A map where every fight happens at one range scores 0; an even three-way split scores 1. Extreme range is left out because only the sniper uses it.

## Movement
Movement is the mean of six sub-scores, each 0–10:

| sub-score | how it's measured | 10 when |
|---|---|---|
| grapple anchors | From each sample, rays at 29° and 57° above level in 8 directions, out to 45 m. The sample counts if one hits something at least 3 m above your feet. | 80% of the floor has an anchor |
| speed lanes | From each sample, the farthest straight run with floor under you, in any of 16 directions (1 m steps; drops up to 2.5 m allowed). The 90th percentile. Strafe-jump and dash runs need these. | 40 m |
| vertical | Jump links ÷ 0.5 × 7, plus 0.5 per floor level (up to 6), plus 1 if the map has ladders. | |
| headroom | Share of samples with more than 6 m clear above: room for vertical dashes and rocket jumps. | 90% |
| risk | Share of samples within 2 m of a lethal drop. | 8–35% (race maps always 10) |
| tech | See below. | |

### Tech: what dash, grapple and good jumps do to the map
Two graphs over the same floor samples:
- **The run:** links the bots' nav graph allows: along a surface, steps, ordinary jumps (gaps up to 2.6 m, 1.15 m up) and ladders.
- **The tech path:** all of that, plus dash-jumps (up to 16 m across, 3.5 m up, any drop, with the arc clear of solids) and grapple pulls (any surface hit within 45 m, landing on the floor next to the anchor).

Both are measured in metres along the samples, so the two compare like for like.

**Race maps**, per leg (from one checkpoint to the next):
- **Shortcut:** run length ÷ tech length. A good player should not cut a leg: 10 at 1.25× or less, 0 at 2.5×. The worst leg counts.
- **Challenge:** what the run asks of you. Jumps per 100 m (full marks at 6) and the share of the route on narrow footing under 2 m wide (beams, ledges, stepping stones; full marks at 25%). 5 points each.
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
| bomb | Each team's distance to the nearest site. Defenders should arrive first so they can set up. |
| ffa / tourney | Spawn count, the closest pair of spawns, and the share of spawn pairs that can see each other. Lower is better: spawns in view of each other mean spawn kills. |
| race | Walking length of the checkpoint route, as time at running speed (8 m/s) and at a good strafe-jump speed (20 m/s). |
| hide and seek / tag | Hiding spots: the share of floor samples visible from fewer than 5% of the others. |
| ranked | The share of sightlines under 20 m (rifle / rocket fights) and over 20 m (laser / crossbow / sniper). Vanilla wants a mix, vampire (laser) wants mid range, instagib (sniper) wants long. |

**Balance score** = 10 × mean fairness of the map's team modes. Measuring through the nav graph has about ±15% noise, so mirrored maps can read 0.85–1.00. **Below 0.85 is worth checking** in the shots.

## Creativity and originality
| metric | how it's measured |
|---|---|
| features | Present or not: ladders, lethal drops, water / lava, wallbangs, a moving cart, 5+ floor levels, 12 m+ of height, indoor and outdoor (15–85% of the floor roofed), fully indoor (over 85%), irregular outline (the footprint fills under 80% of its bounding box), lit interiors (12+ lights), lots of hiding spots (over 15%). |
| creativity score | 1 per feature, plus 0.5 for each feature at most a quarter of the maps have (up to 2). |
| originality | Each map is turned into 9 numbers: levels, height range, loops per area, visible pairs, cover, grapple anchors, speed lanes, short-range share, detail density. These are normalised across all maps. Distance to the nearest other map is the root mean square of the differences, reduced by up to half when the top-down footprints match. **Footprint** = a 24×24 grid of floor heights, compared in all 4 rotations. |
| originality score | 8 × distance to the nearest map, capped at 10. "Most similar map" names that neighbour. |

## The bar
Every map must score **at least 7 in decor, layout, movement, creativity and balance** (where it has team modes). Originality is left out: a map can't help resembling the others. `quark_tests` fails if any map is below the bar, and `quark_tests mapreport` prints which.

Every structure on a map should have a reason to be there, for the game or for the place. [MAP_STRUCTURES.md](MAP_STRUCTURES.md) lists them.

## What the numbers can't judge
Look at the sheets for:
- **Readability:** can you tell where you are from each eye-level shot?
- **Landmarks:** does each area have something you'd name it by?
- **Theme:** do the colours and props make it look like a place?
- **Flow:** does the route the scores describe match what the orbit shots show?
