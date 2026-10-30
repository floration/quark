"""RIVERSIDE (1.19.6): tag, hide and seek, free for all, power and 1v1 at a riverside technology facility, 240 x 180 m.
A canalised river snakes across the site from the north-west to the south-east (three straight reaches with concrete
embankments, 1 m deep, steps out every 30 m; white rapids down the middle of the two east-flowing reaches carry you
downstream at 12 m/s: a chase escape) and splits it into two banks:
  the research campus (north-east): the lab block (3 floors, 2 stairwells, drywall partitions you can dash through),
  the server annex (a hatch up to its roof), the workshop (roller shutter, mezzanine), radio tower A (40 m, ladder in
  two sections, a rest platform at 20 m, the dish platform at 32 m);
  the power side (south-west): the substation (a fenced yard of transformers and busbar gantries, the fence stops
  players but not sight or shots, gates every 30 m; a shield-walled corridor through its core), the control house,
  the cooling-tower stub (an octagonal drum, stairs inside up to its rim), radio tower B (45 m).
Three crossings: the road bridge and the weir footbridge over the middle reach (the core loop between them is about
110 m) and the covered pipe bridge at 8 m over the west reach. Power lines on pylons are grapple rails 15-20 m up.
Teams (power, vs, duel, 1v1): A on the campus, B on the power side, each the same walk from the road bridge.
Every doorway is 4 m high (duel's double-size players are 3.6 m tall)."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

HX, HZ = 120, 90
BED, SURF = -1.4, -0.4           # the river's bed and surface
# the river's three reaches (x0, z0, x1, z1): west (flows +x), middle (along z), east (flows +x)
R1, R2, R3 = (-HX, -57.5, 12.5, -32.5), (-12.5, -57.5, 12.5, 57.5), (-12.5, 32.5, HX, 57.5)
DOOR_H = 4.2


def walls(k, mat, x0, z0, x1, z1, y0, y1, doors=(), t=0.4):
    """four walls round x0..x1, z0..z1 (outer faces), from y0 to y1; doors = [(side, centre, width, height)], side n
    (z0), s (z1), w (x0), e (x1); a door's height from y0"""
    for side in "nswe":
        ds = sorted((c - w / 2, c + w / 2, h) for (sd, c, w, h) in doors if sd == side)
        if side in "ns":
            za, zb = (z0, z0 + t) if side == "n" else (z1 - t, z1)
            a, b = x0, x1
        else:
            za, zb = (x0, x0 + t) if side == "w" else (x1 - t, x1)
            a, b = z0 + t, z1 - t
        pos = a
        for (c0, c1, h) in ds + [(b, b, 0)]:
            if c0 > pos:
                if side in "ns": k.box(mat, pos, y0, za, c0, y1, zb)
                else: k.box(mat, za, y0, pos, zb, y1, c0)
            if c1 > c0 and y0 + h < y1:
                if side in "ns": k.box(mat, c0, y0 + h, za, c1, y1, zb)
                else: k.box(mat, za, y0 + h, c0, zb, y1, c1)
            pos = max(pos, c1)


def slab(k, mat, x0, z0, x1, z1, y0, y1, holes=()):
    for (a, b, c, d) in minus((x0, z0, x1, z1), holes): k.box(mat, a, y0, b, c, y1, d)


def lattice_tower(k, cx, cz, h, w, mat="galvanizedsteel", top_mat="nograpple"):
    """a lattice tower: four legs, girdles every 5 m, diagonal braces on two faces; above 30 m the legs and girdles
    can't be hooked (nobody perches out of reach)"""
    for (sx, sz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        x, z = cx + sx * (w / 2 - 0.2), cz + sz * (w / 2 - 0.2)
        k.box(mat, x - 0.2, 0, z - 0.2, x + 0.2, min(h, 30), z + 0.2)
        if h > 30: k.box(top_mat, x - 0.2, 30, z - 0.2, x + 0.2, h, z + 0.2)
    y = 5.0
    while y < h - 0.5:
        m = mat if y < 30 else top_mat
        k.box(m, cx - w / 2, y, cz - w / 2, cx + w / 2, y + 0.2, cz - w / 2 + 0.2)
        k.box(m, cx - w / 2, y, cz + w / 2 - 0.2, cx + w / 2, y + 0.2, cz + w / 2)
        y += 5.0


def tower_ladders(k, cx, cz, sgn):
    """a radio tower's climb: two ladder sections on spines outside one face (sgn 1: the -z face, -1: the +z face),
    the rest platform at 20 and the dish platform at 32 reaching out over them, holed where each section comes up"""
    fz = cz - sgn * 4.0                     # the spines' outer face
    for (y0, y1, sx) in ((0, 20, -1.0), (20, 32, 1.0)):
        x = cx + sx
        a, b = sorted((fz, fz + sgn * 0.4))
        k.box("galvanizedsteel", x - 0.6, y0, a, x + 0.6, y1, b)
        k.ladder(x, fz, y0, y1, "s" if sgn > 0 else "n")
    for (y, sx) in ((20, -1.0), (32, 1.0)):
        x = cx + sx
        hz0, hz1 = sorted((fz, fz - sgn * 0.8))
        z0, z1 = sorted((cz - sgn * 4.8, cz + sgn * 3.5))
        for (a, b, c, d) in minus((cx - 3.5, z0, cx + 3.5, z1), [(x - 0.6, hz0, x + 0.6, hz1)]):
            k.box("galvanizedgrating", a, y - 0.3, b, c, y, d)


def build():
    m = M("Riverside", "tag", sun=(0.3, 1.0, 0.25))
    m.also = ["hide", "power", "1v1", "duel", "vs"]
    k = Kit(m)

    # ---- the ground: roads (asphalt), the campus plaza (pavers), lawns (grass), the substation yard (dirt), all cut
    # round the river; the river: embankment walls, bed, water, rapids, steps out
    river = [R1, R2, R3]
    roads = [(-HX, -6, -12.5, 6), (12.5, -6, HX, 6),            # the road over the road bridge, both banks
             (74, -HZ, 80, -6)]                                  # a lane up the campus
    yard = (-108, -25, -66, 20)
    plaza = (20, -55, 110, -10)
    holes = river + roads + [yard, plaza]
    for (x0, z0, x1, z1) in tiles(minus((-HX, -HZ, HX, HZ), holes), 30): k.box("dewygrass", x0, -1, z0, x1, 0, z1)
    for r in roads:
        for (x0, z0, x1, z1) in tiles(minus(r, river), 30): k.box("asphalt", x0, -1, z0, x1, 0, z1)
    for (x0, z0, x1, z1) in tiles(minus(plaza, roads), 30): k.box("concretepavers", x0, -1, z0, x1, 0, z1)
    for (x0, z0, x1, z1) in tiles(minus(yard, roads), 25): k.box("dirt", x0, -1, z0, x1, 0, z1)
    # the river bed, its water (clear lagoon, 0.9 x speed), the rapids down the east-flowing reaches (12 m/s, +x)
    for (x0, z0, x1, z1) in river:
        k.box("riverbankclay", x0, BED - 1, z0, x1, BED, z1)
    for (x0, z0, x1, z1) in minus(R2, [R1, R3]): k.box("clearlagoon", x0, BED, z0, x1, SURF, z1)
    for (x0, z0, x1, z1) in (R1, R3):
        zc = (z0 + z1) / 2
        k.box("clearlagoon", x0, BED, z0, x1, SURF, zc - 3); k.box("clearlagoon", x0, BED, zc + 3, x1, SURF, z1)
        k.box("whiterapids", x0, BED, zc - 3, x1, SURF, zc + 3)
    # embankments: the ground's edge is the river's wall (the floor tiles stop at it); a kerb along it
    # steps out of the water every 30 m or so (each flight in the river against its wall, 3 steps of 0.47)
    def steps(x0, z0, x1, z1, axis, rising):
        k.stairs("precastconcrete", x0, z0, x1, z1, 0, axis, rising, base=BED, solid=True)
    for x in (-105, -75, -40, -15 + 0.01):
        if x < -16: steps(x, -36, x + 3, -32.5, "z", 1); steps(x + 6, -57.5, x + 9, -54, "z", -1)
    for x in (25, 55, 85, 110):
        steps(x, 32.5, x + 3, 36, "z", -1); steps(x - 6, 54, x - 3, 57.5, "z", 1)
    for z in (-45, -20, 15, 45):
        if -32 < z < 32: steps(-12.5, z, -9, z + 3, "x", -1); steps(9, z - 6, 12.5, z - 3, "x", 1)

    # ---- perimeter: a concrete wall all round, clip above
    W = 5.0
    k.box("precastconcrete", -HX - 1, BED - 1, -HZ - 1, -HX, W, HZ + 1); k.box("precastconcrete", HX, BED - 1, -HZ - 1, HX + 1, W, HZ + 1)
    k.box("precastconcrete", -HX, BED - 1, -HZ - 1, HX, W, -HZ); k.box("precastconcrete", -HX, BED - 1, HZ, HX, W, HZ + 1)
    m.clip_walls(-HX, -HZ, HX, HZ, W)
    # (a clip lid at 52 m over everything: the towers' tops plus the grapple stay inside)
    k.box("clip", -HX, 52, -HZ, HX, 53, HZ)

    # ---- crossings
    # the road bridge over the middle reach (z -6..6): a deck flush with the roads, parapets
    k.box("precastconcrete", -13.5, -0.7, -6, 13.5, 0, 6)
    for z in (-6, 5.6):
        k.box("precastconcrete", -12.5, 0, z, 12.5, 1.0, z + 0.4)
    # the weir with its footbridge over the middle reach (z 28..32): the weir under the water, sluices either side of
    # the middle (crouch through them in the water), the deck on top flush with the banks
    k.box("waterstainedconcrete", -12.5, BED, 28, -6, -0.1, 32); k.box("waterstainedconcrete", -3, BED, 28, 3, -0.1, 32)
    k.box("waterstainedconcrete", 6, BED, 28, 12.5, -0.1, 32)
    k.box("galvanizedgrating", -12.5, -0.1, 29, 12.5, 0, 31)
    for z in (28.9, 31.0):
        k.box("galvanizedsteel", -12.5, 0, z, 12.5, 1.0, z + 0.1)
    # the covered pipe bridge over the west reach (x -60..-52, floor at 8): stairs up at both ends
    PB = "trapezoidcladding"
    k.box("steeldeckplate", -60, 7.6, -64, -52, 8, -26)                       # the deck
    for (z0, z1) in ((-64, -56), (-53, -47), (-44, -38), (-35, -26)):        # the sides: windows between panels
        k.box(PB, -60, 8, z0, -59.6, 12.8, z1); k.box(PB, -52.4, 8, z0, -52, 12.8, z1)
    for (z0, z1) in ((-56, -53), (-47, -44), (-38, -35)):
        k.box(PB, -60, 8, z0, -59.6, 9.2, z1); k.box(PB, -52.4, 8, z0, -52, 9.2, z1)
        k.box(PB, -60, 11.4, z0, -59.6, 12.8, z1); k.box(PB, -52.4, 11.4, z0, -52, 12.8, z1)
    k.box(PB, -60.2, 12.8, -64.2, -51.8, 13.2, -25.8)                         # the roof
    k.box("precastconcrete", -59, BED - 1, -59, -53, 7.6, -56)               # piers on the banks' edges
    k.box("precastconcrete", -59, BED - 1, -34, -53, 7.6, -31)
    k.stairs("precastconcrete", -59.6, -26, -52.4, -10, 8, "z", -1)          # south: from the power side
    k.stairs("precastconcrete", -59.6, -80, -52.4, -64, 8, "z", 1)           # north: from the campus' north lot

    # ---- the campus (north-east bank)
    # the lab block (x 30..70, z -45..-20): 3 floors of 5 m; stairwells at both ends (stacked flights); a corridor down
    # the middle with drywall partitions either side (dash through them); doors at both ends and the long sides
    LX0, LZ0, LX1, LZ1 = 30, -45, 70, -20
    walls(k, "precastconcrete", LX0, LZ0, LX1, LZ1, 0, 15, [("w", -32.5, 4, DOOR_H), ("e", -32.5, 4, DOOR_H), ("s", 50, 5, DOOR_H), ("n", 50, 5, DOOR_H)])
    for (fy, hole) in ((5, (33, -44.6, 43, -41)), (10, (33, -44.6, 43, -41))):
        holes = [hole, (LX1 - 43 + LX0, -44.6, LX1 - 33 + LX0, -41)]
        slab(k, "polishedconcrete", LX0 + 0.4, LZ0 + 0.4, LX1 - 0.4, LZ1 - 0.4, fy - 0.4, fy, holes)
    slab(k, "concreterooftiles", LX0, LZ0, LX1, LZ1, 15, 15.4, [(61, -25.8, 63, -24.4)])
    for fy in (0, 5):   # the stairwells: flights stacked above each other, the landing strip west / east of them
        k.stairs("polishedconcrete", 33, -44.6, 43, -41, fy + 5, "x", 1, base=fy, solid=False)
        k.stairs("polishedconcrete", LX1 - 43 + LX0, -44.6, LX1 - 33 + LX0, -41, fy + 5, "x", -1, base=fy, solid=False)
    for fy in (0, 5, 10):   # drywall partitions off the corridor (z -34..-31): offices either side
        for x in (46, 50, 54):
            if x != 50: k.box("drywall", x, fy, LZ0 + 0.4, x + 0.2, fy + 4.6, -36)
            k.box("drywall", x, fy, -29, x + 0.2, fy + 4.6, LZ1 - 0.4)
        k.light(50, fy + 4, -32.5, 18, 0.9); k.light(38, fy + 4, -30, 12, 0.7); k.light(62, fy + 4, -30, 12, 0.7)
    # a ladder up to the roof hatch in the north-east corner
    k.box("precastconcrete", 61.4, 10, -27, 62.6, 15, -25.8)            # the hatch pillar: up its face, out onto the roof
    k.ladder(62, -25.8, 10, 15.4, "n")
    # the server annex (x 80..104, z -45..-25): one tall floor, a hatch and ladder up to the roof
    walls(k, "insulatedpanel", 80, -45, 104, -25, 0, 6, [("w", -35, 4, DOOR_H), ("s", 98, 4, DOOR_H)])
    slab(k, "concreterooftiles", 80, -45, 104, -25, 6, 6.4, [(100.5, -43.4, 102.5, -42)])
    k.box("insulatedpanel", 100.9, 0, -42, 102.1, 6, -40.8)
    k.ladder(101.5, -42, 0, 6.4, "s")
    for z in (-41, -37, -33):
        k.box("serverrack", 84, 0, z, 96, 2.4, z + 1.2)                      # rack rows: crouch behind them
    k.light(92, 5, -35, 14, 0.9)
    # the workshop (x 30..58, z 0..26): a roller-shutter opening on the river side, a mezzanine along the east wall
    walls(k, "trapezoidcladding", 30, 0, 58, 26, 0, 9, [("w", 13, 6, 5.0), ("n", 50, 4, DOOR_H), ("e", 8, 4, DOOR_H)])
    k.box("corrugated", 29.6, 9, -0.4, 58.4, 9.4, 26.4)
    k.box("steeldeckplate", 50, 4.1, 0.4, 57.6, 4.5, 25.6)
    k.stairs("steeldeckplate", 46, 21.6, 50, 25.6, 4.5, "x", 1)
    for (x0, z0, x1, z1) in ((34, 4, 40, 6), (34, 18, 40, 20)):
        k.box("greasestainedmetal", x0, 0, z0, x1, 1.1, z1)                  # workbenches / shelving: hide spots
    k.box("cardboardboxes", 52, 0, 2, 54, 1.2, 4)
    k.light(44, 8, 13, 18, 0.9)
    # radio tower A (x 88..94, z 5..11): 40 m; a ladder in two sections, the rest platform at 20, the dish at 32
    lattice_tower(k, 91, 8, 40, 6)
    tower_ladders(k, 91, 8, 1)
    # the north lot (the campus' strip north of the west reach): parked vans, a guard hut
    for (x0, z0) in ((-40, -84), (-31, -84), (-22, -84), (-36, -72), (-20, -72)):
        k.box("aircraftskin", x0, 0, z0, x0 + 2.4, 2.6, z0 + 5.5)
    walls(k, "insulatedpanel", -104, -84, -92, -74, 0, 5.0, [("s", -98, 3, DOOR_H), ("e", -79, 3, DOOR_H)])
    k.box("concreterooftiles", -104.3, 5.0, -84.3, -91.7, 5.3, -73.7)

    # ---- the power side (south-west bank)
    # the substation yard (x -100..-55, z -25..20): a chain-link fence with gates every 30 m, transformers in rows,
    # busbar gantries over them, a shield-walled corridor through the middle (shots stopped, players through)
    def fence_x(x0, x1, z, gates):
        for (a, _, b, _) in minus((x0, z, x1, z + 0.1), [(g - 2, z, g + 2, z + 0.1) for g in gates]): k.box("chainlinkmesh", a, 0, z, b, 2.6, z + 0.1)
    def fence_z(z0, z1, x, gates):
        for (_, a, _, b) in minus((x, z0, x + 0.1, z1), [(x, g - 2, x + 0.1, g + 2) for g in gates]): k.box("chainlinkmesh", x, 0, a, x + 0.1, 2.6, b)
    fence_x(-108, -66, -25, (-96, -78)); fence_x(-108, -66, 19.9, (-96, -78))
    fence_z(-25, 20, -108, (-12, 8)); fence_z(-25, 20, -66.1, (-12, 8))
    for (x0, z0) in ((-103, -20), (-103, -6), (-103, 8), (-76, -20), (-76, -6), (-76, 8)):
        k.box("electricalcabinet", x0, 0, z0, x0 + 6, 4.0, z0 + 5)          # transformers (gaps between: hide spots)
        k.box("radiatorfins", x0 - 0.5, 0.5, z0 + 1, x0, 3.5, z0 + 4)
    for x in (-100, -73):   # busbar gantries along z over each row
        for z in (-22, -2, 18):
            k.box("galvanizedsteel", x - 0.25, 0, z - 0.25, x + 0.25, 8, z + 0.25)
        k.box("galvanizedsteel", x - 0.3, 8, -22.3, x + 0.3, 8.6, 18.3)
    k.box("shieldwall", -90, 0, -24, -89.9, 3.5, 19); k.box("shieldwall", -86.1, 0, -24, -86, 3.5, 19)
    # the control house (x -45..-25, z -25..-10)
    walls(k, "precastconcrete", -45, -25, -25, -10, 0, 5.5, [("e", -17.5, 4, DOOR_H), ("w", -17.5, 4, DOOR_H), ("s", -35, 4, DOOR_H)])
    k.box("concreterooftiles", -45.3, 5.5, -25.3, -24.7, 5.9, -9.7)
    k.box("controlpanel", -42, 0, -24.6, -32, 1.2, -23.2)
    k.light(-35, 4.6, -17.5, 14, 0.9)
    # the cooling-tower stub: an octagonal drum 20 m across (x -60..-40, z 28..48), 15 m tall, a door north and south;
    # a free-standing stair in the middle (two flights, a landing at 6) up to a ledge along the north wall at 12
    cx, cz, R = -50, 38, 10.0
    c = R * math.tan(math.pi / 8)
    CT = "boardformedconcrete"
    for (z0, z1) in ((cz - R, cz - R + 0.8), (cz + R - 0.8, cz + R)):         # north / south sides, a door each
        k.box(CT, cx - c, 0, z0, cx - 2, 15, z1); k.box(CT, cx + 2, 0, z0, cx + c, 15, z1); k.box(CT, cx - 2, DOOR_H, z0, cx + 2, 15, z1)
    k.box(CT, cx - R, 0, cz - c, cx - R + 0.8, 15, cz + c); k.box(CT, cx + R - 0.8, 0, cz - c, cx + R, 15, cz + c)
    for (sx, sz) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):                     # the short diagonal sides
        k.prism(CT, [(cx + sx * c, cz + sz * R), (cx + sx * R, cz + sz * c), (cx + sx * (R - 0.8), cz + sz * c), (cx + sx * c, cz + sz * (R - 0.8))], 0, 15)
    k.stairs("galvanizedgrating", cx - 1.5, cz - 6, cx + 1.5, cz + 2, 6, "z", 1, solid=False)              # 0 -> 6
    k.box("galvanizedgrating", cx - 1.5, 5.6, cz + 2, cx + 4.5, 6, cz + 5)                                 # the landing
    k.stairs("galvanizedgrating", cx + 1.5, cz - 6, cx + 4.5, cz + 2, 12, "z", -1, base=6, solid=False)    # 6 -> 12
    k.box("galvanizedgrating", cx - c, 11.7, cz - R + 0.8, cx + c, 12, cz - 5.8)                           # the ledge
    k.light(cx, 10, cz, 14, 0.8)
    # radio tower B (x -108..-102, z 42..48): 45 m, the same ladder in two sections
    lattice_tower(k, -105, 45, 45, 6)
    tower_ladders(k, -105, 45, -1)
    # the south strip (power side, past the east reach): a cable-drum store and the pump house
    for (x, z) in ((25, 70), (31, 74), (25, 80), (40, 68), (46, 78)):
        k.disc("cratewood", x, z, 1.6, 0, 1.2, n=8)
    walls(k, "redengineeringbrick", 80, 64, 100, 80, 0, 6, [("n", 90, 4, DOOR_H), ("w", 72, 4, DOOR_H)])
    k.box("concreterooftiles", 79.7, 6, 63.7, 100.3, 6.4, 80.3)
    k.disc("castiron", 90, 74, 2.0, 0, 2.4, n=8)

    # ---- power lines: two pylons, cables (solid, thin) from tower to pylon to pylon to tower, 15-20 m up
    for (px, pz) in ((-30, 30), (40, -60)):
        lattice_tower(k, px, pz, 22, 3)
        k.box("galvanizedsteel", px - 4, 20, pz - 0.3, px + 4, 20.5, pz + 0.3)
    for (a, b, y) in (((-105, 45), (-30, 30), 18.6), ((-30, 30), (40, -60), 19.6), ((40, -60), (91, 8), 18.6)):
        k.bar("leadsheet", a, b, 0.16, y, y + 0.16)

    # ---- decoration and hiding places: trees in clumps on the lawns, hedges, parked cars, cable reels (off the roads
    # and doorways; crouch spots for hide and seek, loops for tag)
    decorate(k)

    # ---- spawns: tag, hide and free for all over both banks; A on the campus, B on the power side
    for (x, z) in ((75, -40), (77, -30), (73, -50), (86, -52)):
        k.spawn("a", x, 0, z, yaw_to(x, z, 50, -10))
    for (x, z) in ((-77, 40), (-73, 30), (-79, 52), (-70, 62)):
        k.spawn("b", x, 0, z, yaw_to(x, z, -40, 10))
    for (x, z) in ((40, -15), (100, 15), (-10, -75), (-110, -70), (-30, 0), (-60, 70), (60, 66), (-114, -5)):
        k.spawn("any", x, 0, z, yaw_to(x, z, 0, 0))

    m.raw("setting riverside technology facility")
    m.save("riverside", dress=False)




def decorate(k):
    import random
    R = random.Random("riverside")
    def tree(x, z, s=1.0):
        k.box("treebark", x - 0.3, 0, z - 0.3, x + 0.3, 3.2 * s, z + 0.3)
        k.box("jungleleaves", x - 2.2 * s, 2.6 * s, z - 2.2 * s, x + 2.2 * s, 4.6 * s, z + 2.2 * s)
        k.box("jungleleaves", x - 1.4 * s, 4.6 * s, z - 1.4 * s, x + 1.4 * s, 6.0 * s, z + 1.4 * s)
    # clumps: (centre, count, spread) on open lawns (both banks)
    clumps = [((100, 22), 4, 7), ((20, 20), 3, 4), ((-35, 45), 4, 6), ((-95, 35), 3, 5),
              ((-30, 75), 4, 8), ((105, -75), 3, 6), ((-85, -78), 3, 6), ((60, 82), 3, 6), ((-20, 25), 2, 3)]
    for ((cx, cz), n, sp) in clumps:
        for i in range(n):
            tree(cx + R.uniform(-sp, sp), cz + R.uniform(-sp, sp), R.uniform(0.85, 1.15))
    # hedges round the campus plaza and along the power side's lawn: crouch behind them
    for (x0, z0, x1, z1) in ((20, -12, 28, -10.8), (34, -12, 46, -10.8), (56, -12, 70, -10.8),
                             (-40, 20, -28, 21.2), (-22, 20, -15, 21.2), (-50, -40 + 9, -48.8, -28 + 9)):
        k.box("hedge", x0, 0, z0, x1, 1.3, z1)
    # parked cars beside the roads (not on them)
    for (x, z) in ((24, 8), (31, 8), (-26, -10), (-33, -10), (100, -8), (-100, 8)):
        k.box("powdercoatgreen" if x > 0 else "batteredyellowpaint", x - 2.2, 0, z - 1.0, x + 2.2, 1.0, z + 1.0)
        k.box("dustyglass", x - 1.2, 1.0, z - 0.9, x + 1.0, 1.6, z + 0.9)
    # cable reels on the power side's lawn (low cover)
    for (x, z) in ((-40, 2), (-45, 8), (-20, -28)):
        k.disc("cratewood", x, z, 1.4, 0, 1.4, n=8)
    # lamp posts along the road (floodlights' glow; they light the bake)
    for x in (-100, -70, -40, 30, 60, 90):
        k.box("galvanizedsteel", x - 0.15, 0, 6.6, x + 0.15, 7, 6.9)
        k.box("glow", x - 0.4, 6.9, 6.1, x + 0.4, 7.1, 7.1)
        k.light(x, 6.4, 6.6, 14, 0.6)
    # aircraft warning lights on the towers' tops
    for (x, z, h) in ((91, 8, 40), (-105, 45, 45)):
        k.box("glow", x - 0.4, h, z - 0.4, x + 0.4, h + 0.6, z + 0.4)
