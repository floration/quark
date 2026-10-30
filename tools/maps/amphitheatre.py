"""AMPHITHEATRE RUIN (1.19.6): king of the hill (and team vs, power, free for all) in a ruined Greek theatre on a
hillside over the sea, 120 x 84 m. North is -z. Re-aimed from tourney: the teams come in from the two sides (A west by
the temple podium, B east by the olive terraces), so the layout is a mirror about x = 0 and only the dressing differs.
  The hill: the orchestra, the flat stone circle (22 m) in the middle, a toppled altar and column drums on it.
  The cavea (north): 30 stone tiers (0.45 m rise, 0.9 m deep: one big stair) rising 13.5 m round the orchestra, the
  diazoma (a 2.5 m walk) halfway up, a portico of broken columns on the top terrace. It looks down on the hill.
  The skene (south): the stage (logeion) at 3 m, steps down to the orchestra; the stage building behind it, two
  storeys (3 m and 8 m) and a roof at 12 m with a broken corner; stairs at both ends inside and two up to the roof.
  The parodoi (west and east): the side passages between the cavea and the stage, a gateway at each mouth.
  Outside: a yard each side, a ramp up the hillside to the top terrace; south of the skene, the back yard and the cliff
  (fall: the sea)."""
import math
from .prefabs import M
from .m5kit import Kit, slab, walls, rock, yaw_to

HX, Z0, Z1 = 60, -48, 36
R0 = 11.0              # the orchestra
T0 = 13.0              # the first tier
NT = 30                # tiers
RISE, DEEP = 0.45, 0.9
DIAZ = 15              # the diazoma's tier (2.5 m deep)
TOP = NT * RISE        # 13.5: the top terrace
STAGE = 3.0
UP = 8.0               # the skene's upper floor
ROOF = 12.0
SK = (-17, 15, 17, 23)  # the skene's footprint


def tier_r(i):         # tier i's inner radius
    return T0 + DEEP * i + (2.5 - DEEP if i > DIAZ else 0)


R_OUT = tier_r(NT)     # 41.6: the cavea's back


def build():
    m = M("Amphitheatre Ruin", "koth", sun=(-0.75, 0.5, 0.15))
    m.also = ["vs", "power"]
    k = Kit(m)

    # ---- the ground: the theatre's floor at 0, the back yard to the cliff at z 30 (then the sea: the void)
    slab(k, "scree", -HX, Z0, HX, 30, -4, 0, tile=30)
    k.ring("travertine", 0, 0, 0, R0, 0, 0.1, n=24)               # the orchestra's paving
    k.box("lichencrustedstone", -HX, -16, 20, HX, -4, 30)             # the cliff's face
    k.box("deepocean", -900, -16, -900, 900, -14, 900)
    m.raw("void -8")

    # ---- the cavea: tiers (the north half ring), the top terrace and the hillside round it
    mats = ("limestoneashlar", "largelimestoneashlar")
    for i in range(NT):
        k.ring(mats[(i // 5) % 2] if i != DIAZ else "travertine", 0, 0, tier_r(i), tier_r(i + 1), 0, (i + 1) * RISE,
               n=24, arc=(180, 360))
    k.ring("travertine", 0, 0, R_OUT, R_OUT + 6, 0, TOP + 0.1, n=24, arc=(180, 360))
    k.box("crackedearth", -HX, 0, Z0, HX, TOP, -30)
    # the ramps up the hillside from each yard (rising north to the top terrace)
    for s in (-1, 1):
        x0, x1 = sorted((s * 53, s * 59))
        k.ramp("scree", x0, -30, x1, -3, 0, TOP, "-z")

    stage(k)
    skene(k)
    gates(k)
    portico(k)
    sides(k)
    cover(k)
    edge(k)

    # ---- spawns: A on the temple podium (west), B behind the olive terrace wall (east); free for all round the ruin
    for (x, z) in ((-56, 16), (-52, 16), (-56, 21), (-52, 21)):
        k.spawn("a", x, 1.2, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((56, 16), (52, 16), (56, 21), (52, 21)):
        k.spawn("b", x, 1.2, z, yaw_to(x, z, 0, 0))
    for (x, y, z) in ((0, TOP + 0.1, -46), (-30, TOP, -38), (30, TOP, -38), (-12, UP, 19), (12, UP, 19), (-8, 0, 27), (8, 0, 27),
                      (-50, 0, -6), (50, 0, -6)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, 0))
    m.raw(f"hill -11 0 -11 11 5 11")

    m.raw("setting a ruined Greek theatre on a hillside above the sea, late in the afternoon")
    m.raw("palette base 0.86 0.8 0.68 accent 0.62 0.32 0.2 metal 0.4 0.36 0.3 glow 1 0.8 0.5 sky 0.92 0.74 0.55")
    m.save("amphitheatre", dress=False)


def stage(k):
    """the logeion (3 m) in front of the skene, the steps down to the orchestra, ramps at its ends"""
    x0, z0, x1, z1 = SK
    k.box("marbleblocks", x0, 0, 12, x1, STAGE, z0)
    k.stairs("marbleblocks", -3, 7, 3, 12, STAGE, "z", 1, base=0.1)
    for s in (-1, 1):
        a, b = sorted((s * 17, s * 23))
        k.ramp("polygonalstone", a, 12, b, 15, 0, STAGE, "+x" if s < 0 else "-x")
    # the painted scenery flats (1/8 m: shoot-through) on the stage
    for x in (-11, 11):
        k.box("terracotta", x - 1.6, STAGE, 13.2, x + 1.6, STAGE + 2.6, 13.32)


def skene(k):
    """the stage building: floor at 3 (doors north onto the stage, south to the back yard down steps), the upper floor
    at 8 (an arcade over the stage), the roof at 12 with its north-east corner fallen in"""
    x0, z0, x1, z1 = SK
    t = 0.6
    k.box("marbleblocks", x0, 0, z0, x1, STAGE, z1)
    walls(k, "crumblingplaster", x0, z0, x1, z1, STAGE, UP - 0.4,
          [("n", 0, 3, 3.4), ("n", -10, 2.4, 3.4), ("n", 10, 2.4, 3.4), ("s", -12, 2.4, 3.4), ("s", 12, 2.4, 3.4)], t=t)
    # the upper storey: the arcade (five arches north), windows south
    walls(k, "crumblingplaster", x0, z0, x1, z1, UP - 0.4, ROOF - 0.4,
          [("n", x, 2.6, 2.8) for x in (-12, -6, 0, 6, 12)] + [("s", x, 1.6, 1.4, 1.2) for x in (-8, 8)], t=t)
    # the upper floor (holes over the two end stairs) and the roof (a hole over its stair; the corner fallen in)
    slab(k, "terracotta", x0 + t, z0 + t, x1 - t, z1 - t, UP - 0.4, UP,
         holes=[(x0 + t, 20.2, -11, z1 - t), (11, 20.2, x1 - t, z1 - t)])
    slab(k, "terracotta", x0, z0, x1, z1, ROOF - 0.4, ROOF, holes=[(-8.4, 20.2, 8.4, z1 - t), (10, z0, x1, 19)])
    # the stairs: 3 -> 8 at both ends (rising to the end walls), 8 -> 12 beside them (rising to the middle)
    for s in (-1, 1):
        a, b = sorted((s * 8.4, s * (x1 - t)))
        k.stairs("limestoneashlar", a, 20.4, b, z1 - t, UP, "x", s, base=STAGE, floor=STAGE)
    for s in (-1, 1):
        a, b = sorted((s * 2, s * 8.4))
        k.stairs("limestoneashlar", a, 20.4, b, z1 - t, ROOF, "x", -s, base=UP, floor=UP)
    # the roof's parapet (low: cover lying down), the steps down from the back doors to the yard
    for (a, b, c, d) in ((x0, z1 - 0.4, x1, z1), (x0, z0, x0 + 0.4, z1)):
        k.box("marbleblocks", a, ROOF, b, c, ROOF + 0.7, d)
    for s in (-1, 1):
        k.stairs("limestoneashlar", s * 12 - 1.4, z1, s * 12 + 1.4, z1 + 4.8, STAGE, "z", -1, base=0)
    for (x, z) in ((-12, 19), (12, 19), (0, 17)):
        k.light(x, STAGE + 3.6, z, 9, 0.5)


def gates(k):
    """a gateway at each parodos' mouth (two piers and a lintel, an arch to hook)"""
    for s in (-1, 1):
        x = s * 40
        for (z0, z1) in ((-0.5, 1.5), (10.5, 12.5)):
            k.box("largetemplestone", x - 1, 0, z0, x + 1, 6.5, z1)
        k.box("largetemplestone", x - 1, 6.5, -0.5, x + 1, 7.7, 12.5)


def portico(k):
    """the top terrace's portico: a row of columns round the back of the cavea, some broken short, a stretch of
    architrave on the standing ones; the back wall behind it"""
    r = R_OUT + 2.0
    hs = [6.5, 6.5, 2.2, 6.5, 6.5, 3.8, 1.2, 6.5, 6.5, 2.6, 6.5, 6.5, 4.4]
    for i, h in enumerate(hs):
        a = math.radians(180 + 15 * i)
        x, z = r * math.cos(a), r * math.sin(a)
        k.disc("limestoneashlar", x, z, 0.55, TOP, TOP + h, n=8)
    for (i0, i1) in ((0, 1), (3, 4), (7, 8), (10, 11)):
        a0, a1 = math.radians(180 + 15 * i0), math.radians(180 + 15 * i1)
        k.bar("limestoneashlar", (r * math.cos(a0), r * math.sin(a0)), (r * math.cos(a1), r * math.sin(a1)), 1.0, TOP + 6.5, TOP + 7.3)


def sides(k):
    """the west yard's temple podium (A's spawns on it, a few columns), the east yard's olive terraces (B's)"""
    # the temple podium: a 1.2 m platform, steps all along its east side, four columns, a back wall
    k.box("largetemplestone", -59, 0, 13, -49, 1.2, 25)
    k.stairs("largetemplestone", -49, 13, -47, 25, 1.2, "x", -1, base=0, rise=0.4)
    for (x, z) in ((-50, 13.8), (-50, 24.2), (-58.2, 13.8), (-58.2, 24.2)):
        k.disc("limestoneashlar", x, z, 0.5, 1.2, 7.5, n=8)
    k.box("templestone", -59.6, 0, 12, -59, 4.5, 26)
    k.box("templestone", -50.6, 1.2, 12.6, -49.6, 2.4, 14.6)   # a fallen drum at the podium's corner
    # the olive terrace: a 1.2 m earth terrace held by a drystone wall (B's spawns on it), steps down its west side,
    # a higher wall at its back, olive trees
    k.box("dirt", 49, 0, 13, 59, 1.2, 25)
    k.box("drystonewall", 48.6, 0, 13, 49, 1.2, 25)
    k.stairs("largedrystonewall", 46.6, 13, 48.6, 25, 1.2, "x", 1, base=0, rise=0.4)
    k.box("largedrystonewall", 59, 0, 12, 59.6, 4.5, 26)
    k.box("drystonewall", 49, 1.2, 25, 59, 2.0, 25.8)
    for (x, z) in ((54, 11.0), (57.5, 27.5), (45.5, 27.0)):
        olive(k, x, z)


def olive(k, x, z):
    k.box("treebark", x - 0.3, 0, z - 0.3, x + 0.3, 2.6, z + 0.3)
    k.disc("hedge", x, z, 1.8, 2.6, 4.2, n=6)


def cover(k):
    """the orchestra (the hill): the toppled altar off the middle, two column drums; rubble on the cavea's tiers"""
    k.box("templestone", 3.2, 0, -4.4, 6.0, 1.1, -2.6)
    k.box("templestone", 2.4, 0, -4.6, 3.2, 0.5, -2.4)
    for (x, z) in ((-5.5, 3.5), (-3.0, -7.0)):
        k.tube_x("toppledcolumn", x - 1.4, x + 1.4, 0.5, z, 0.55)
    for i, (r, a, s) in enumerate(((20, 210, 1.6), (24, 300, 1.8), (33, 245, 1.7), (31, 330, 1.5), (18, 268, 1.3))):
        x, z = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        ti = max(0, min(NT - 1, int((r - T0) / DEEP)))
        rock(k, "rubblepile", x, z, s, ti * RISE, 1.6 + s * 0.4, seed=40 + i)


def edge(k):
    """clip round the edge (to 60 m: the roof is 12, the portico 21), nograpple rocks on the cliff's lip"""
    for (x0, z0, x1, z1) in ((-HX - 1, Z0 - 1, HX + 1, Z0), (-HX - 1, Z1, HX + 1, Z1 + 1), (-HX - 1, Z0, -HX, Z1), (HX, Z0, HX + 1, Z1)):
        k.box("clip", x0, -8, z0, x1, 60, z1)
    k.box("clip", -HX, 60, Z0, HX, 61, Z1)
    for i, x in enumerate((-44, -26, 24, 42)):
        rock(k, "nograpple", x, 29.6, 1.4, -0.4, 1.4, seed=60 + i)
