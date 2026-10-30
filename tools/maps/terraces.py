"""RICE TERRACES (1.19.6): team vs (and power, king of the hill, free for all) on a sunny hillside of flooded rice
terraces, 160 x 120 m. North is -z: the hill climbs 28 m from the valley stream (south, 0 m) to the temple (north).
  The terraces: twelve bands follow the contour (curved, cut in 8 m pieces), each 6 m deep with a 2.1 m stone riser
  (too high to jump: you cross at set places). Each band is a flooded paddy (clear lagoon, 0.35 m deep, x0.9 speed;
  every third paddy is a mud bath, x0.6) with a dyke 1.2 m wide along its front edge (on every second band an
  embankment 1.2 m over the water: crouch cover) and cross dykes every 32 m, staggered (the fast lanes, dry).
  Ways up a riser: stone stairs, earth ramps and bamboo ladders, staggered so no line runs straight up the hill.
  Team A: the farmhouse (west) on its threshing yard at 10.5 m (the terraces' fifth band, levelled).
  Team B: the water mill (east) on its platform at 6.3 m by the stream, with a covered stair up to 10.5 m.
  The temple (north, 28 m): a walled court 30 x 24 m with a gate south, east and west, a bell tower (ladder to 12 m),
  the shrine at the back. The hill (king of the hill) is 20 x 16 m in the court.
  The valley (south, 0 m): the stream (clear lagoon, east-west: no current) with A's west stair and B's mill race
  steps down to it: the low lane.
Field huts with woven (shoot-through) walls and haystacks break the long lines along the dykes."""
import math
from .prefabs import M
from .m5kit import Kit, slab, walls, rock, yaw_to

HX, HZ = 80, 60
NB, DEPTH, RISE = 12, 6.0, 2.1     # the bands
VALLEY = 44.0                      # band 1's front edge (the valley south of it)
PADDY = 0.4                        # a paddy's floor under its band's dyke tops
TEMPLE = 27.3                      # the temple platform (one riser over band 12)
COLS = [(-80 + 8 * i, -72 + 8 * i) for i in range(20)]
FARM_COLS = (0, 1, 2)              # x -80..-56: bands 1-5 levelled (the farm's threshing yard)
MILL_COLS = (17, 18, 19)           # x 56..80: bands 1-3 levelled (the mill platform)
STAIR_COL = 17                     # x 56..64: bands 4-5 merged, the mill's covered stair in front of them


def h(k): return RISE * k                                    # band k's dyke tops (k = 0: the valley)
def zf(k, x):                                                # band k's front edge (k = NB + 1: the temple platform's)
    if k <= 1: return VALLEY
    return VALLEY - DEPTH * (k - 1) + 1.6 * math.sin(x / 17.0 + 0.15 * k)   # (near-parallel: every band ~6 m deep)


# the crossings of each riser (the front of band k, from band k - 1): (x, kind), kind stairs / ramp / ladder, 26 m apart
# and staggered from riser to riser; west and east of the levelled yards (bands 6 up) one more each side
def crossings(k):
    if k == NB + 1:   # up to the temple platform: the grand stair and two side stairs
        return [(0, "grand"), (-34, "stairs"), (34, "stairs")]
    kinds = ["stairs", "ramp", "stairs", "ladder"]
    out, x, i = [], -50 + ((k * 13) % 22), k
    while x <= 50:
        out.append((x, kinds[i % 4])); x += 26; i += 1
    if k > 5: out += [(-68 + (k % 3) * 4, "stairs"), (68 - (k % 3) * 4, "stairs" if k % 2 else "ramp")]
    return out


def build():
    m = M("Rice Terraces", "vs", sun=(0.2, 1.0, 0.55))
    m.also = ["power", "koth"]
    k = Kit(m)

    gaps = {kk: [x for (x, _) in crossings(kk)] for kk in range(2, NB + 2)}

    # ---- the valley: banks, the stream's bed and water
    slab(k, "rootyloam", -HX, VALLEY, HX, 50, -1, 0, tile=40)
    slab(k, "rootyloam", -HX, 58, HX, HZ, -1, 0, tile=40)
    slab(k, "riverbankclay", -HX, 50, HX, 58, -1.6, -0.6, tile=40)
    for (x0, x1) in ((-80, -40), (-40, 0), (0, 40), (40, 80)):
        k.box("clearlagoon", x0, -0.6, 50, x1, -0.15, 58)

    # ---- the bands: one prism per 8 m column between this band's front and the next's, solid down to -1, the
    # paddy floor 0.4 m under the dykes; water (or mud) over the paddy; the front dyke along the riser's top
    for kk in range(1, NB + 1):
        for (ci, (x0, x1)) in enumerate(COLS):
            if ci in FARM_COLS and kk <= 5: continue
            if ci in MILL_COLS and kk <= 3: continue
            if ci == STAIR_COL and kk in (4, 5): continue
            top = h(kk) - PADDY
            poly = [(x0, zf(kk, x0)), (x1, zf(kk, x1)), (x1, zf(kk + 1, x1)), (x0, zf(kk + 1, x0))]
            k.prism("drystonewall", poly, -1, top)
            # the paddy's water (or mud): inside the front dyke, following the curve (a liquid acts as its box: the box's
            # slack lies in the dyke and the next riser, never over open air)
            mat = "mudbath" if (kk + ci) % 3 == 0 else "clearlagoon"
            k.prism(mat, [(x0, zf(kk + 1, x0)), (x1, zf(kk + 1, x1)), (x1, zf(kk, x1) - 1.2), (x0, zf(kk, x0) - 1.2)], top, h(kk) - 0.05)
    # the levelled yards: the farm (bands 1-5 at 10.5) and the mill platform (bands 1-3 at 6.3), and bands 4-5 by the
    # mill's covered stair (one block at 10.5)
    for (cols, kmax, mat) in ((FARM_COLS, 5, "hardpan"), (MILL_COLS, 3, "riverstonewall"), ((STAIR_COL,), 5, "drystonewall")):
        for ci in cols:
            x0, x1 = COLS[ci]
            kmin = 4 if cols == (STAIR_COL,) else 1
            poly = [(x0, zf(kmin, x0)), (x1, zf(kmin, x1)), (x1, zf(kmax + 1, x1)), (x0, zf(kmax + 1, x0))]
            k.prism("drystonewall", poly, -1, h(kmax) - 0.5)
            k.prism(mat, poly, h(kmax) - 0.5, h(kmax))
    # the front dykes (embankments 1.2 m over the water on every second band), gapped at the crossings
    for kk in range(2, NB + 1):
        hi = h(kk) + (0.8 if kk % 2 == 0 else 0.0)
        xs = [x for x in range(-80, 81, 4)]
        for (a, b) in zip(xs, xs[1:]):
            mid = (a + b) / 2
            ci = int((mid + 80) // 8)
            if ci in FARM_COLS and kk <= 5 or ci in MILL_COLS and kk <= 3 or ci == STAIR_COL and kk in (4, 5): continue
            if any(abs(mid - g) < 4.5 for g in gaps[kk]): continue
            k.bar("dirt", (a - 0.05, zf(kk, a) - 0.6), (b + 0.05, zf(kk, b) - 0.6), 1.2, h(kk) - PADDY - 0.01, hi)
        # cross dykes every 32 m (dry lanes up and down the band), clear of the crossings below and above
        for c in range(-64 + 16 * (kk % 2), 65, 32):
            ci = int((c + 80) // 8)
            if ci in FARM_COLS and kk <= 5 or ci in MILL_COLS and kk <= 3 or ci == STAIR_COL and kk in (4, 5): continue
            if any(abs(c - g) < 3.5 for g in gaps.get(kk + 1, [])): continue
            za = min(zf(kk, c - 0.6), zf(kk, c + 0.6)) - 1.0
            zb = max(zf(kk + 1, c - 0.6), zf(kk + 1, c + 0.6))
            k.box("dirt", c - 0.6, h(kk) - PADDY - 0.01, zb - 0.3, c + 0.6, h(kk), za)

    # ---- the crossings: stone stairs, earth ramps, bamboo ladders against each riser
    for kk in range(2, NB + 2):
        top = h(kk) - PADDY if kk <= NB else TEMPLE   # (onto the paddy floor, through the dyke's gap)
        for (x, kind) in crossings(kk):
            ci = int((x + 80) // 8)
            yard = ci in FARM_COLS and kk - 1 <= 5 or ci in MILL_COLS and kk - 1 <= 3
            base = h(kk - 1) - (0 if yard else PADDY)
            w = 6.0 if kind == "grand" else 3.0
            zs = [zf(kk, x + d) for d in (-w / 2, 0, w / 2)] if kk <= NB else [-28.0]
            zr0, zr1 = min(zs), max(zs)
            n = math.ceil((top - base) / 0.5 - 1e-6)
            # (the band below's depth, its front dyke and a step in front of the flight left clear)
            room = min(zf(kk - 1, x + d) for d in (-w / 2, 0, w / 2)) - 1.6 - zr1 if kk > 2 else 6
            if kind in ("stairs", "grand"):
                k.stairs("templestone" if kk > NB else "graniteblocks", x - w / 2, zr0 - 0.6, x + w / 2, zr1 + min(n * 0.8, room), top, "z", -1, base=base)
            elif kind == "ramp":
                k.ramp("dirt", x - 2, zr0 - 0.4, x + 2, zr1 + min(5.2, room), base, top, "-z")
            else:   # a bamboo ladder on the riser
                k.box("bamboo", x - 0.9, base, zr0 - 0.1, x + 0.9, h(kk), zr1 + 0.25)
                k.ladder(x, zr1 + 0.25, base, h(kk), "n", mat="bamboo")
    # the temple platform (28 m) and the hill behind it
    k.box("drystonewall", -HX, -1, -HZ, HX, TEMPLE - 0.5, -28)
    k.box("largetemplestone", -HX, TEMPLE - 0.5, -HZ, HX, TEMPLE, -28)

    # ---- the valley stairs: A's west stair down from the farm, B's mill race steps
    k.stairs("graniteblocks", -80, VALLEY, -56, 48, h(5), "x", -1, base=0)
    k.stairs("riverstonewall", 54, VALLEY, 66, 48, h(3), "x", 1, base=0)
    # B's covered stair: from the mill platform (6.3) up to the fifth band (10.5), a roof over it
    zz, z0 = max(zf(4, 57), zf(4, 60), zf(4, 63)), min(zf(4, 57), zf(4, 60), zf(4, 63))
    k.stairs("wideplanks", 57, z0 - 0.6, 63, zz + 9, h(5), "z", -1, base=h(3))
    for (x, z) in ((56.6, zz + 9), (63.4, zz + 9), (56.6, zz + 2), (63.4, zz + 2)):
        k.box("bamboo", x - 0.2, h(3), z - 0.2, x + 0.2, h(5) + 3.0, z + 0.2)
    k.wedge("thatch", 0, 55.5, h(5) + 3.0, zz - 1, 60, h(5) + 4.2, zz + 10)
    k.wedge("thatch", 1, 60, h(5) + 3.0, zz - 1, 64.5, h(5) + 4.2, zz + 10)

    temple(k)
    decorate(k)
    farm(k)
    mill(k)
    outer(k)

    # ---- spawns
    for (x, z) in ((-71.5, 36), (-67.5, 36), (-71.5, 40), (-67.5, 40)):
        k.spawn("a", x, h(5), z, yaw_to(x, z, 0, -34))
    for (x, z) in ((70, 38), (74, 38), (70, 41), (74, 41)):
        k.spawn("b", x, h(3), z, yaw_to(x, z, 0, -34))
    # free for all: the farm yard, the mill platform, the valley banks, two cross dykes, the temple platform
    for (x, y, z) in ((-60, h(5), 20), (62, h(3), 40), (-40, 0, 46), (40, 0, 46), (-30, TEMPLE, -40), (30, TEMPLE, -40)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, 0))
    for (kk, c) in ((6, -32), (8, 32)):
        k.spawn("any", c, h(kk), (zf(kk, c) - 1.2 + zf(kk + 1, c)) / 2, 90 if c > 0 else -90)
    m.raw(f"hill -10 {TEMPLE:g} -54 10 {TEMPLE + 6:g} -38")
    # king of the hill starts nearer the temple (map format 11 spawn sets): dry stone pads at the two ends of band 10,
    # a stone screen on each pad's inner side (the two pads look along the same band)
    for side, team in ((-1, "koth-a"), (1, "koth-b")):
        zc = (zf(11, side * 75) + zf(10, side * 75) - 1.2) / 2
        k.box("graniteblocks", side * 71, h(10) - PADDY - 0.01, zc - 2.2, side * 79, h(10), zc + 2.2)
        k.box("templestone", min(side * 70.4, side * 71), h(10), zc - 2.6, max(side * 70.4, side * 71), h(10) + 3.0, zc + 2.6)
        for (x, z) in ((side * 77, zc - 1.1), (side * 74, zc - 1.1), (side * 77, zc + 1.1), (side * 74, zc + 1.1)):
            k.spawn(team, x, h(10), z, yaw_to(x, z, 0, -40))

    m.raw("setting a sunny hillside of flooded rice terraces, a farm, a mill and a temple")
    m.raw("palette base 0.62 0.58 0.5 accent 0.55 0.2 0.15 metal 0.3 0.3 0.3 glow 1 0.85 0.55 sky 0.55 0.75 0.8")
    m.save("terraces", dress=False)


def temple(k):
    """the walled court on the platform: gates south, east and west, a bell tower (ladder), the shrine at the back"""
    y = TEMPLE
    walls(k, "templestone", -15, -58, 15, -34, y, y + 4,
          [("s", 0, 6, 4.5), ("w", -46, 5, 4.5), ("e", -46, 5, 4.5)], t=0.8)
    # the pagoda gate over the south opening: posts and a two-step roof (above the opening)
    for x in (-4, 3.2):
        k.box("terracotta", x, y, -35, x + 0.8, y + 5, -33.4)
    k.box("roofshingles", -6, y + 5, -36, 6, y + 5.6, -32.4)
    k.box("roofshingles", -4.5, y + 5.6, -35.4, 4.5, y + 6.4, -33)
    # the shrine: a small hall at the back of the court (cover on the hill's north edge)
    walls(k, "templestone", -8, -58, 8, -54.5, y, y + 4.5, [("s", 0, 3, 3.2)])
    k.box("roofshingles", -9, y + 4.5, -59, 9, y + 5.2, -53.8)
    # the bell tower (west corner of the court): a 4 m square on posts, a ladder up to its 12 m platform
    for (x, z) in ((-14, -57), (-10.6, -57), (-14, -53.6), (-10.6, -53.6)):
        k.box("largetemplestone", x, y, z, x + 0.6, y + 12, z + 0.6)
    k.box("wideplanks", -14.2, y + 11.6, -57.2, -9.8, y + 12, -53.2)
    k.box("roofshingles", -15, y + 15, -58, -9, y + 15.6, -52.4)
    for (x, z) in ((-14, -57), (-10.6, -57), (-14, -53.6), (-10.6, -53.6)):
        k.box("largetemplestone", x, y + 12, z, x + 0.6, y + 15, z + 0.6)
    k.box("bamboo", -13.4, y, -53.2, -10.6, y + 11.6, -53.0)
    k.ladder(-12, -53.0, y, y + 12, "n", mat="bamboo")
    k.box("lightblock", -12.3, y + 13.5, -55.3, -11.7, y + 14.1, -54.7)


def farm(k):
    """A's side: the two-storey farmhouse (spawns in its yard behind it), the barn"""
    y = h(5)
    walls(k, "logcabin", -79, 22, -63, 30, y, y + 7, [("e", 26, 3.4, 3.2), ("s", -71, 3.4, 3.2), ("n", -71, 2.0, 2.2, 1.0)])
    slab(k, "wideplanks", -78.6, 22.4, -63.4, 29.6, y + 3.0, y + 3.4, holes=[(-74, 22.4, -66, 24.4)])
    k.wedge("thatch", 2, -79.6, y + 7, 21.4, -62.4, y + 9.5, 26)
    k.wedge("thatch", 3, -79.6, y + 7, 26, -62.4, y + 9.5, 30.6)
    k.box("shojiscreen", -71.06, y + 3.4, 24.6, -70.94, y + 6.4, 29.6)   # (upstairs: a paper wall)
    k.stairs("wideplanks", -74, 22.4, -66, 24.4, y + 3.4, "x", -1, base=y)
    walls(k, "logcabin", -64, 35, -57, 43, y, y + 5, [("n", -60.5, 4, 3.6)])
    k.wedge("thatch", 0, -64.6, y + 5, 34.4, -60.5, y + 7, 43.6)
    k.wedge("thatch", 1, -60.5, y + 5, 34.4, -56.4, y + 7, 43.6)
    k.light(-71, y + 3, 26, 10, 0.6)


def mill(k):
    """B's side: the stone mill (spawns on its floor), the static water wheel over the stream, the granary"""
    y = h(3)
    walls(k, "riverstonewall", 66, 34, 78, 44, y, y + 6, [("w", 35.6, 2.2, 3.4), ("n", 72, 3.4, 3.4), ("n", 68, 2.2, 3.4)])
    k.box("wideplanks", 66.4, y - 0.05, 34.4, 77.6, y, 43.6)
    k.wedge("roofshingles", 2, 65.4, y + 6, 33.4, 78.6, y + 8.5, 39)
    k.wedge("roofshingles", 3, 65.4, y + 6, 39, 78.6, y + 8.5, 44.6)
    # the wheel: a static ring on the stream side, its axle (a hook)
    k.box("wideplanks", 70, 0, 44, 71.2, y + 2.5, 50.5)
    k.box("castiron", 69.4, y - 1.5, 44, 71.8, y - 0.8, 47.5)
    k.box("galvanizedsteel", 70.2, y - 1.4, 44, 71, y - 0.9, 49)
    walls(k, "riverstonewall", 68, 28, 76, 32.5, y, y + 4, [("s", 72, 3, 3)])
    k.box("roofshingles", 67.6, y + 4, 27.6, 76.4, y + 4.4, 32.9)
    k.light(72, y + 4, 39, 10, 0.6)


def outer(k):
    """the map's edge: walls stepped up the hill on the west and east, the valley's south bank, the temple's back
    wall; clip over them up to 85 m (the bell tower's roof is 43.6 m)"""
    for x0, x1 in ((-82, -80), (80, 82)):   # (a bamboo grove: its top climbs the hill with the bands)
        k.box("bamboo", x0, -1, -28, x1, 5, HZ + 2)
        k.ramp("bamboo", x0, -28, x1, HZ + 2, 5, TEMPLE + 6, "-z")
        k.box("bamboo", x0, -1, -HZ - 2, x1, TEMPLE + 6, -28)
    k.box("bamboo", -82, -1, HZ, 82, 5, HZ + 2)
    k.box("templestone", -82, -1, -HZ - 2, 82, TEMPLE + 6, -HZ)
    k.box("clip", -82, -1, -HZ - 2, -80, 85, HZ + 2)
    k.box("clip", 80, -1, -HZ - 2, 82, 85, HZ + 2)
    k.box("clip", -80, 5, HZ, 80, 85, HZ + 2)
    k.box("clip", -80, TEMPLE + 6, -HZ - 2, 80, 85, -HZ)
    k.box("clip", -80, 85, -HZ, 80, 86, HZ)


def decorate(k):
    """sparingly, off the dykes and crossings: field huts (woven walls: shoot-through) and haystacks on a few paddies
    (the long lines along the bands broken every 30-40 m), stone lanterns and an incense burner in the court (cover
    on the hill), lanterns on the gate, stepping stones along the stream's edges, bamboo clumps at the map's ends"""
    import random
    R = random.Random("terraces")
    taken = {kk: [x for (x, _) in crossings(kk)] + [x for (x, _) in crossings(kk + 1)] for kk in range(2, NB + 1)}
    def free(kk, x, r): return all(abs(x - g) > r + 3 for g in taken[kk]) and all(abs(x - c) > r + 1 for c in range(-64 + 16 * (kk % 2), 65, 32))
    huts = 0
    for (kk, x) in ((3, -20), (4, 30), (6, -36), (7, 10), (8, 44), (9, -12), (10, 24), (11, -44)):
        if not free(kk, x, 2.5): x += 7
        if not free(kk, x, 2.5): continue
        zc = (zf(kk, x) - 1.2 + zf(kk + 1, x)) / 2
        y = h(kk) - PADDY
        if huts < 4 and kk % 2:   # a hut: woven walls (1/8 m: shoot-through), a door each side, a thatch roof
            for (x0, z0, x1, z1) in ((x - 2, zc - 1.5, x + 2, zc - 1.44), (x - 2, zc + 1.44, x + 2, zc + 1.5)):
                k.box("bamboo", x0, y, z0, x1, y + 2.6, z1)
            for xx in (x - 2, x + 1.94):
                k.box("bamboo", xx, y, zc - 1.44, xx + 0.06, y + 0.9, zc + 1.44)
            k.wedge("thatch", 2, x - 2.4, y + 2.6, zc - 1.9, x + 2.4, y + 3.8, zc)
            k.wedge("thatch", 3, x - 2.4, y + 2.6, zc, x + 2.4, y + 3.8, zc + 1.9)
            huts += 1
        else:                     # haystacks (hay bales: only blasts break them)
            k.box("haybales", x - 1.2, y, zc - 1.0, x + 1.2, y + 1.6, zc + 1.0)
            k.box("haybales", x - 0.6, y + 1.6, zc - 0.6, x + 0.6, y + 2.4, zc + 0.6)
    # the court: two stone lanterns and the incense burner (cover on the hill), lanterns on the gate
    y = TEMPLE
    for (x, z) in ((-6, -42), (6, -50)):
        k.box("largetemplestone", x - 0.6, y, z - 0.6, x + 0.6, y + 1.0, z + 0.6)
        k.box("templestone", x - 0.3, y + 1.0, z - 0.3, x + 0.3, y + 1.8, z + 0.3)
        k.box("lightblock", x - 0.45, y + 1.8, z - 0.45, x + 0.45, y + 2.4, z + 0.45)
    k.disc("bronze", 4.5, -42.5, 1.3, y, y + 1.1, n=8)   # (off the hill's centre: the bots' goal)
    k.disc("bronze", 4.5, -42.5, 0.9, y + 1.1, y + 1.5, n=8)
    for x in (-5.2, 5.2):
        k.box("lightblock", x - 0.3, y + 3.2, -33.2, x + 0.3, y + 4.0, -32.6)
    k.box("jade", -6, y + 5.6, -32.5, 6, y + 5.8, -32.3)
    # stepping stones along the stream's edges (out of its middle)
    for i, x in enumerate(range(-70, 71, 20)):
        rock(k, "riverstonewall", x + R.uniform(-3, 3), 51.2 if i % 2 else 56.8, 0.9, -0.6, 0.7, seed=i)
    # bamboo clumps at the ends of the bands (against the outer walls)
    for kk in range(5, NB + 1, 2):
        for side in (-1, 1):
            x = side * 77.5
            z = (zf(kk, x) - 1.6 + zf(kk + 1, x)) / 2
            for (dx, dz) in ((0, 0), (1.1, 0.8), (-0.4, 1.3)):
                k.box("bamboo", x + side * -abs(dx) - 0.15, h(kk) - PADDY, z + dz - 0.15, x + side * -abs(dx) + 0.15, h(kk) + 7, z + dz + 0.15)
