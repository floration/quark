"""RIVERSIDE (1.19.6): tag, hide and seek, free for all, power and 1v1 at a riverside technology facility, 240 x 180 m.
A canalised river snakes across the site from the north-west to the south-east (three straight reaches with concrete
embankments, 1 m deep, steps out every 30 m; white rapids down the middle of the two east-flowing reaches carry you
downstream at 12 m/s: a chase escape) and splits it into two banks:
  the research campus (north-east): the lab block (3 floors, 2 stairwells, rooms off a 7 m corridor behind drywall
  you can dash through, small windows, a parapeted roof up a hatch), the server annex (a hatch up to its roof), the
  workshop (roller shutter, mezzanine), the stores, the chiller compound, the car park canopy, the generator shed,
  radio tower A (40 m, ladder in two sections, a rest platform at 20 m, the dish platform at 32 m);
  the power side (south-west): the substation (a yard walled on its river and bridge sides, chain-link on the
  others, gates every 30 m, three short shield walls down its core), the control house, the switch house, the
  cooling-tower stub (an octagonal drum, stairs inside up to a ledge and on to a parapeted rim walk at 15 m: the
  power side's perch), the weir hut, radio tower B (45 m);
  the north lot (a gatehouse on a wall, a logistics shed, a berm) and the south strip (two store sheds against the
  wall, the drum store, the pump house).
The open ground is broken by solid buildings, berms and low walls (few, large pieces: momentum stays): of every pair
of ground points 50-100 m apart, 12% see each other (44% before the 1.19.6 review).
Three crossings: the road bridge and the weir footbridge over the middle reach (the core loop between them is about
110 m) and the covered pipe bridge at 8 m over the west reach. Power lines on pylons are grapple rails 15-20 m up
(clipped above: nobody stands on them).
Teams (power, vs, duel, 1v1): A on the campus (between the lab and the annex), B on the power side (between the
switch house and the cooling tower); each walks 11-14.5 s to the road bridge, and the two teams' spawns are seen from
about as much ground (8 m grid: A 63, B 60 points); neither side's perch sees the other's spawns. The weir and the
pipe bridge are B's (closer), the lab roof and the annex are A's.
Every doorway is 4 m high, the roof hatches 2 x 2 m and the tower ladder holes 1.8 x 1.8 m (duel's double-size
players are 3.6 m tall, 1.6 m wide)."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

HX, HZ = 120, 90
BED, SURF = -1.4, -0.4           # the river's bed and surface
# the river's three reaches (x0, z0, x1, z1): west (flows +x), middle (along z), east (flows +x)
R1, R2, R3 = (-HX, -57.5, 12.5, -32.5), (-12.5, -57.5, 12.5, 57.5), (-12.5, 32.5, HX, 57.5)
DOOR_H = 4.2


def walls(k, mat, x0, z0, x1, z1, y0, y1, doors=(), t=0.4, glass="glass"):
    """four walls round x0..x1, z0..z1 (outer faces), from y0 to y1; doors = [(side, centre, width, height[, sill])],
    side n (z0), s (z1), w (x0), e (x1); a door's top from y0; with a sill it is a window (wall below the sill, a
    pane of solid glass filling it)"""
    for side in "nswe":
        ds = sorted((c - w / 2, c + w / 2, h, (rest[0] if rest else 0)) for (sd, c, w, h, *rest) in doors if sd == side)
        if side in "ns":
            za, zb = (z0, z0 + t) if side == "n" else (z1 - t, z1)
            a, b = x0, x1
        else:
            za, zb = (x0, x0 + t) if side == "w" else (x1 - t, x1)
            a, b = z0 + t, z1 - t
        def put(m, p0, q0, p1, q1):
            if side in "ns": k.box(m, p0, q0, za, p1, q1, zb)
            else: k.box(m, za, q0, p0, zb, q1, p1)
        pos = a
        for (c0, c1, h, sill) in ds + [(b, b, 0, 0)]:
            if c0 > pos: put(mat, pos, y0, c0, y1)
            if c1 > c0:
                if y0 + h < y1: put(mat, c0, y0 + h, c1, y1)
                if sill > 0:
                    put(mat, c0, y0, c1, y0 + sill)
                    if glass: put(glass, c0, y0 + sill, c1, y0 + h)
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
    # diagonal braces: one per 5 m panel on each face, alternating, below 30 m
    y = 0.0; i = 0
    while y < min(h, 30) - 0.5:
        y1 = min(y + 5.0, 30.0, h)
        for (axis, f) in (("x", cz - w / 2 + 0.05), ("x", cz + w / 2 - 0.05), ("z", cx - w / 2 + 0.05), ("z", cx + w / 2 - 0.05)):
            c0 = (cx if axis == "x" else cz) - w / 2 + 0.2; c1 = c0 + w - 0.4
            a, b = (c0, c1) if i % 2 == 0 else (c1, c0)
            brace(k, mat, axis, f, a, y, b, y1)
        y = y1; i += 1


def brace(k, mat, axis, f, a, ya, b, yb, hw=0.08, t=0.15):
    """a straight diagonal bar in a vertical plane: along `axis` from (a, ya) to (b, yb), at f across it"""
    L = math.hypot(b - a, yb - ya)
    na, ny = -(yb - ya) / L, (b - a) / L
    d0 = na * a + ny * ya
    lo, hi = sorted((a, b))
    if axis == "x":
        box = (lo, ya, f - t / 2, hi, yb, f + t / 2)
        planes = [(na, ny, 0, d0 + hw), (-na, -ny, 0, -d0 + hw)]
    else:
        box = (f - t / 2, ya, lo, f + t / 2, yb, hi)
        planes = [(0, ny, na, d0 + hw), (0, -ny, -na, -d0 + hw)]
    k.cut(mat, box, planes)


def berm(k, x0, z0, x1, z1, h, axis, mat="dewygrass"):
    """an earth berm h tall along `axis` (x: its long side along x), grass slopes on both long sides, a flat top"""
    if axis == "x":
        d = (z1 - z0) / 3
        k.wedge(mat, 2, x0, 0, z0, x1, h, z0 + d); k.box(mat, x0, 0, z0 + d, x1, h, z1 - d); k.wedge(mat, 3, x0, 0, z1 - d, x1, h, z1)
    else:
        d = (x1 - x0) / 3
        k.wedge(mat, 0, x0, 0, z0, x0 + d, h, z1); k.box(mat, x0 + d, 0, z0, x1 - d, h, z1); k.wedge(mat, 1, x1 - d, 0, z0, x1, h, z1)


def tower_ladders(k, cx, cz, sgn):
    """a radio tower's climb: two ladder sections on spines outside one face (sgn 1: the -z face, -1: the +z face),
    the rest platform at 20 and the dish platform at 32 reaching out over them, holed where each section comes up
    (1.8 x 1.8 m holes: duel's double-size players fit)"""
    fz = cz - sgn * 4.0                     # the spines' outer face
    for (y0, y1, sx) in ((0, 20, -1.0), (20, 32, 1.0)):
        x = cx + sx
        a, b = sorted((fz, fz + sgn * 0.4))
        k.box("galvanizedsteel", x - 0.6, y0, a, x + 0.6, y1, b)
        k.ladder(x, fz, y0, y1, "s" if sgn > 0 else "n")
    for (y, sx) in ((20, -1.0), (32, 1.0)):
        x = cx + sx
        hz0, hz1 = sorted((fz, fz - sgn * 1.8))
        z0, z1 = sorted((cz - sgn * 6.0, cz + sgn * 3.5))
        for (a, b, c, d) in minus((cx - 3.5, z0, cx + 3.5, z1), [(x - 0.9, hz0, x + 0.9, hz1)]):
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
    # gravel: the bank walk along the south strip, the weir path to the cooling tower, the apron by the switch house,
    # the north lot's yard in front of the logistics shed
    paths = [(-12.5, 57.5, HX, 62), (-60, 27, -12.5, 32), (-72, 30, -60, 48), (-46, -76, -12, -66)]
    holes = river + roads + [yard, plaza] + paths
    for (x0, z0, x1, z1) in tiles(minus((-HX, -HZ, HX, HZ), holes), 30): k.box("dewygrass", x0, -1, z0, x1, 0, z1)
    for (x0, z0, x1, z1) in tiles(paths, 30): k.box("gravel", x0, -1, z0, x1, 0, z1)
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
    # the lab block (x 30..70, z -45..-20): 3 floors of 5 m; stairwells at both ends (stacked flights, in the north
    # offices); a 7 m corridor down the middle (z -36..-29) between drywall walls, rooms off it with two doorways each
    # (or dash straight through the drywall: a tag escape); doors at both ends and the long sides; small windows
    LX0, LZ0, LX1, LZ1 = 30, -45, 70, -20
    for fy in (0, 5, 10):
        wins = [("n", x, 2.0, 3.2, 1.4) for x in (38, 62)] + [("s", x, 2.0, 3.2, 1.4) for x in (36, 64)]
        if fy == 0:
            wins += [("w", -32.5, 4, DOOR_H), ("e", -32.5, 4, DOOR_H), ("s", 50, 5, DOOR_H), ("n", 50, 4, DOOR_H)]
        else:
            wins += [("w", -32.5, 2.0, 3.2, 1.4), ("e", -32.5, 2.0, 3.2, 1.4), ("s", 50, 2.0, 3.2, 1.4)]
        walls(k, "precastconcrete", LX0, LZ0, LX1, LZ1, fy, fy + 5 if fy < 10 else 15, wins)
    for (fy, hole) in ((5, (33, -44.6, 43, -41)), (10, (33, -44.6, 43, -41))):
        holes = [hole, (LX1 - 43 + LX0, -44.6, LX1 - 33 + LX0, -41)]
        slab(k, "polishedconcrete", LX0 + 0.4, LZ0 + 0.4, LX1 - 0.4, LZ1 - 0.4, fy - 0.4, fy, holes)
    slab(k, "concreterooftiles", LX0, LZ0, LX1, LZ1, 15, 15.4, [(60.8, -25.8, 63.2, -23.6)])
    for fy in (0, 5):   # the stairwells: flights stacked above each other, the landing strip west / east of them
        k.stairs("polishedconcrete", 33, -44.6, 43, -41, fy + 5, "x", 1, base=fy, solid=False)
        k.stairs("polishedconcrete", LX1 - 43 + LX0, -44.6, LX1 - 33 + LX0, -41, fy + 5, "x", -1, base=fy, solid=False)
    # rooms: north 30..46 (west stairs), 46..54, 54..70 (east stairs); south 30..42, 42..58, 58..70
    NDOORS = [(34, 2.6), (42.5, 2.6), (48, 2.4), (52, 2.4), (57.5, 2.6), (66, 2.6)]
    SDOORS = [(33.5, 3), (38.5, 3), (45.5, 3), (54.5, 3), (61.5, 3), (66.5, 3)]
    for fy in (0, 5, 10):
        top = fy + 4.6
        for (z0, z1, ds) in ((-36.2, -36, NDOORS), (-29, -28.8, SDOORS)):
            cuts = [(c - w / 2, z0, c + w / 2, z1) for (c, w) in ds]
            for (a, b_, c, d) in minus((LX0 + 0.4, z0, LX1 - 0.4, z1), cuts):
                k.box("drywall", a, fy, b_, c, top, d)
            for (c, w) in ds:
                k.box("drywall", c - w / 2, fy + DOOR_H, z0, c + w / 2, top, z1)
        for x in (46, 54):
            k.box("drywall", x - 0.1, fy, LZ0 + 0.4, x + 0.1, top, -36.2)
        for x in (42, 58):
            k.box("drywall", x - 0.1, fy, -28.8, x + 0.1, top, LZ1 - 0.4)
        k.light(50, fy + 4, -32.5, 18, 0.9); k.light(38, fy + 4, -24, 12, 0.7); k.light(62, fy + 4, -24, 12, 0.7)
    # a ladder up to the roof hatch (2.4 x 2.2 m: duel's double-size players fit) in the south-east office
    k.box("precastconcrete", 61.4, 10, -27, 62.6, 15, -25.8)            # the hatch pillar: up its face, out onto the roof
    k.ladder(62, -25.8, 10, 15.4, "n")
    # the roof: a 1.2 m parapet all round (no view down into the power side's lawns from the edge), a plant room,
    # vents and planters along the edges (the middle stays clear)
    for (x0, z0, x1, z1) in ((LX0, LZ0, LX1, LZ0 + 0.4), (LX0, LZ1 - 0.4, LX1, LZ1), (LX0, LZ0 + 0.4, LX0 + 0.4, LZ1 - 0.4), (LX1 - 0.4, LZ0 + 0.4, LX1, LZ1 - 0.4)):
        k.box("precastconcrete", x0, 15.4, z0, x1, 16.6, z1)
    walls(k, "insulatedpanel", 36, -42, 46, -36, 15.4, 18.4, [("s", 41, 3, 2.8)])   # the plant room (headroom over the west stairs)
    k.box("concreterooftiles", 35.8, 18.4, -42.2, 46.2, 18.7, -35.8)
    for (x, z) in ((52, -43), (56, -43), (60, -43)):
        k.box("galvanizedsteel", x - 0.8, 15.4, z - 0.8, x + 0.8, 16.6, z + 0.8)   # vents against the north parapet
    for x0 in (32, 40, 48):
        k.box("precastconcrete", x0, 15.4, LZ1 - 1.6, x0 + 5, 16.0, LZ1 - 0.4); k.box("hedge", x0 + 0.2, 16.0, LZ1 - 1.4, x0 + 4.8, 16.6, LZ1 - 0.6)
    # the server annex (x 80..104, z -45..-25): one tall floor, a few small windows, a 2 x 2 m hatch and a ladder up
    # to the roof
    walls(k, "insulatedpanel", 80, -45, 104, -25, 0, 6, [("w", -35, 4, DOOR_H), ("s", 98, 4, DOOR_H),
                                                       ("n", 88, 2.0, 3.6, 1.6), ("s", 86, 2.0, 3.6, 1.6), ("e", -32, 2.0, 3.6, 1.6)])
    slab(k, "concreterooftiles", 80, -45, 104, -25, 6, 6.4, [(100.5, -44, 102.5, -42)])
    k.box("insulatedpanel", 100.9, 0, -42, 102.1, 6, -40.8)
    k.ladder(101.5, -42, 0, 6.4, "s")
    for z in (-41, -37, -33):
        k.box("serverrack", 84, 0, z, 96, 2.4, z + 1.2)                      # rack rows: crouch behind them
    k.light(92, 5, -35, 14, 0.9)
    # the workshop (x 30..58, z 0..26): a roller-shutter opening on the river side, a mezzanine along the east wall
    walls(k, "trapezoidcladding", 30, 0, 58, 26, 0, 9, [("w", 13, 6, 5.0), ("n", 50, 4, DOOR_H), ("e", 8, 4, DOOR_H),
                                                         ("e", 20, 2.4, 3.4, 1.6), ("s", 40, 2.4, 3.4, 1.6)])
    k.box("rollershutter", 29.4, 5.0, 9.6, 30.0, 6.4, 16.4)                 # the shutter, rolled up in its box over the opening
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
    # the north lot (the campus' strip north of the west reach): a guard hut, a gatehouse on a wall across the lot, a
    # logistics shed, two vans and an earth berm (the lot's long lines are broken every 40-60 m)
    walls(k, "insulatedpanel", -104, -84, -92, -74, 0, 5.0, [("s", -98, 3, DOOR_H), ("e", -79, 3, DOOR_H)])
    k.box("concreterooftiles", -104.3, 5.0, -84.3, -91.7, 5.3, -73.7)
    k.box("precastconcrete", -80.5, 0, -90, -79.5, 3.5, -74)                # the lot's wall
    walls(k, "redengineeringbrick", -84, -74, -76, -63, 0, 5.0, [("w", -68.5, 4, DOOR_H), ("e", -68.5, 4, DOOR_H),
                                                               ("n", -80, 2.0, 3.4, 1.4), ("s", -80, 2.0, 3.4, 1.4)])
    k.box("concreterooftiles", -84.3, 5.0, -74.3, -75.7, 5.4, -62.7)
    walls(k, "trapezoidcladding", -42, -88, -16, -76, 0, 6.0, [("w", -82, 4.4, DOOR_H), ("e", -82, 4.4, DOOR_H), ("s", -29, 4.4, DOOR_H)])
    k.box("corrugated", -42.3, 6.0, -88.3, -15.7, 6.4, -75.7)
    for x in (-38, -21):
        k.box("cardboardboxes", x, 0, -87.4, x + 3, 2.0, -85.4)
    for (x0, z0) in ((-37, -70), (-22, -70)):
        k.box("aircraftskin", x0, 0, z0, x0 + 5.5, 2.6, z0 + 2.4)
    berm(k, -10, -74, 8, -64, 2.6, "x")
    # the campus' stores (north of the lab: x 24..56, z -84..-72) and the chiller compound (east of the lane)
    walls(k, "insulatedpanel", 24, -84, 56, -72, 0, 5.0, [("w", -78, 4.4, DOOR_H), ("e", -78, 4.4, DOOR_H), ("s", 40, 4.4, DOOR_H),
                                                       ("s", 30, 2.0, 3.4, 1.4), ("s", 50, 2.0, 3.4, 1.4)])
    k.box("concreterooftiles", 23.7, 5.0, -84.3, 56.3, 5.4, -71.7)
    walls(k, "precastconcrete", 86, -82, 114, -56, 0, 3.2, [("w", -69, 5, 4), ("s", 100, 5, 4), ("n", 100, 5, 4)])
    for (x0, z0) in ((90, -78), (90, -66), (104, -78), (104, -66)):
        k.box("galvanizedsteel", x0, 0, z0, x0 + 6, 2.6, z0 + 6)            # chillers
        k.box("steeldeckplate", x0 + 1, 2.6, z0 + 1, x0 + 5, 2.8, z0 + 5)
    # the bike shelter on the plaza west of the lab (a solid back wall to the river)
    k.box("precastconcrete", 16, 0, -50, 16.6, 3.0, -24)
    k.box("corrugated", 16, 3.0, -50.3, 21, 3.3, -23.7)
    for z in (-50, -37.2, -24.4):
        k.box("galvanizedsteel", 20.6, 0, z, 21, 3.0, z + 0.4)

    # ---- the power side (south-west bank)
    # the substation yard (x -108..-66, z -25..20): a solid 3 m wall on its river (north) and road-bridge (east) sides,
    # a chain-link fence on the other two, gates every 30 m, transformers in rows, busbar gantries over them; down the
    # middle three short shield walls, 0.25 m thick (shots stopped, players through: cover, not a tube)
    def fence_x(x0, x1, z, gates, mat="chainlinkmesh", h=2.6, t=0.1):
        for (a, _, b, _) in minus((x0, z, x1, z + t), [(g - 2, z, g + 2, z + t) for g in gates]): k.box(mat, a, 0, z, b, h, z + t)
    def fence_z(z0, z1, x, gates, mat="chainlinkmesh", h=2.6, t=0.1):
        for (_, a, _, b) in minus((x, z0, x + t, z1), [(x, g - 2, x + t, g + 2) for g in gates]): k.box(mat, x, 0, a, x + t, h, b)
    fence_x(-108, -66, -25.4, (-96, -78), "precastconcrete", 3.0, 0.4); fence_x(-108, -66, 19.9, (-96, -78))
    fence_z(-25, 20, -108, (-12, 8)); fence_z(-25.4, 20, -66.4, (-17, 10), "precastconcrete", 3.0, 0.4)
    for (x0, z0) in ((-103, -20), (-103, -6), (-103, 8), (-76, -20), (-76, -6), (-76, 8)):
        k.box("electricalcabinet", x0, 0, z0, x0 + 6, 4.0, z0 + 5)          # transformers (gaps between: hide spots)
        k.box("radiatorfins", x0 - 0.5, 0.5, z0 + 1, x0, 3.5, z0 + 4)
    for x in (-100, -73):   # busbar gantries along z over each row
        for z in (-22, -2, 18):
            k.box("galvanizedsteel", x - 0.25, 0, z - 0.25, x + 0.25, 8, z + 0.25)
        k.box("galvanizedsteel", x - 0.3, 8, -22.3, x + 0.3, 8.6, 18.3)
    for (z0, z1) in ((-23, -12), (-7, 4), (9, 18)):
        k.box("shieldwall", -90.25, 0, z0, -90, 3.5, z1); k.box("shieldwall", -86, 0, z0 + 2, -85.75, 3.5, z1 + 2)
    # the control house (x -45..-25, z -25..-10)
    walls(k, "precastconcrete", -45, -25, -25, -10, 0, 5.5, [("e", -17.5, 4, DOOR_H), ("w", -17.5, 4, DOOR_H), ("s", -35, 4, DOOR_H),
                                                         ("n", -40, 2.0, 3.6, 1.6), ("n", -30, 2.0, 3.6, 1.6), ("s", -28, 2.0, 3.6, 1.6)])
    k.box("concreterooftiles", -45.3, 5.5, -25.3, -24.7, 5.9, -9.7)
    k.box("controlpanel", -42, 0, -24.6, -32, 1.2, -23.2)
    k.light(-35, 4.6, -17.5, 14, 0.9)
    # the switch house (x -98..-72, z 30..46): a long low hall beside the cooling tower, B's spawns between the two
    walls(k, "redengineeringbrick", -98, 30, -72, 46, 0, 6.0, [("w", 38, 4.4, DOOR_H), ("e", 38, 4.4, DOOR_H), ("s", -85, 4.4, DOOR_H),
                                                           ("n", -91, 2.0, 3.6, 1.6), ("n", -79, 2.0, 3.6, 1.6), ("s", -93, 2.0, 3.6, 1.6)])
    k.box("concreterooftiles", -98.3, 6.0, 29.7, -71.7, 6.4, 46.3)
    for x in (-93, -84, -77):
        k.box("electricalcabinet", x, 0, 32, x + 3, 2.4, 35)
    k.box("controlpanel", -95, 0, 42, -88, 1.2, 43.4)
    k.light(-85, 5, 38, 14, 0.9)
    # the cooling-tower stub: an octagonal drum 20 m across (x -60..-40, z 28..48), 15 m tall, a door north and south;
    # a free-standing stair in the middle (two flights, a landing at 6) up to a ledge along the north wall at 12, and a
    # last flight from the ledge up to the rim walk at 15 over the north wall (a 1.2 m parapet): the power side's perch
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
    k.stairs("galvanizedgrating", cx - c, cz - 7.2, cx + 1.0, cz - 4.8, 15, "x", -1, base=12, solid=False)  # 12 -> 15
    k.box("galvanizedgrating", cx - c, 14.7, cz - R + 0.8, cx + c, 15, cz - 7.2)                           # the rim walk
    k.box(CT, cx - c, 15, cz - R, cx + c, 16.2, cz - R + 0.4)                                              # its parapet
    k.light(cx, 10, cz, 14, 0.8)
    # radio tower B (x -108..-102, z 42..48): 45 m, the same ladder in two sections
    lattice_tower(k, -105, 45, 45, 6)
    tower_ladders(k, -105, 45, -1)
    # the power side's lawn: two earth berms (2.6 m, grass slopes) with gaps between them
    berm(k, -42, 8, -20, 16, 2.6, "x")
    berm(k, -100, 61, -76, 69, 2.6, "x")
    # the south strip (power side, past the east reach): two store sheds, a cable-drum store and the pump house
    # (the sheds stand against the south wall, each with a door at both ends and one to the strip; the strip's lane
    # between them and the river is broken by the drum store's low walls and the berm)
    walls(k, "trapezoidcladding", -48, 72, -18, 90, 0, 6.0, [("w", 81, 4.4, DOOR_H), ("e", 81, 4.4, DOOR_H), ("n", -33, 4.4, DOOR_H),
                                                          ("n", -42, 2.0, 3.4, 1.4), ("n", -24, 2.0, 3.4, 1.4)])
    k.box("corrugated", -48.3, 6.0, 71.7, -17.7, 6.4, 90)
    walls(k, "trapezoidcladding", 34, 74, 64, 90, 0, 5.0, [("w", 82, 4.4, DOOR_H), ("e", 82, 4.4, DOOR_H), ("n", 49, 4.4, DOOR_H),
                                                         ("n", 40, 2.0, 3.2, 1.4), ("n", 58, 2.0, 3.2, 1.4)])
    k.box("corrugated", 33.7, 5.0, 73.7, 64.3, 5.4, 90)
    for (x, z) in ((40, 80), (58, 86)):
        k.disc("cratewood", x, z, 1.6, 0, 1.2, n=8)
    # the cable-drum store: two low walls in an L, drums behind them
    k.box("precastconcrete", 2, 0, 62, 22, 2.4, 62.6); k.box("precastconcrete", 21.4, 0, 62.6, 22, 2.4, 72)
    for (x, z) in ((6, 66), (12, 68), (17, 65)):
        k.disc("cratewood", x, z, 1.6, 0, 1.2, n=8)
    walls(k, "redengineeringbrick", 80, 64, 100, 80, 0, 6, [("n", 90, 4, DOOR_H), ("w", 72, 4, DOOR_H)])
    k.box("concreterooftiles", 79.7, 6, 63.7, 100.3, 6.4, 80.3)
    k.disc("castiron", 90, 74, 2.0, 0, 2.4, n=8)
    # the east strip: a berm along the perimeter and a generator shed
    berm(k, 104, -30, 112, -8, 2.6, "z")
    walls(k, "insulatedpanel", 100, 16, 116, 26, 0, 4.5, [("w", 21, 4, DOOR_H), ("e", 21, 4, DOOR_H)])
    k.box("concreterooftiles", 99.7, 4.5, 15.7, 116.3, 4.8, 26.3)
    k.disc("castiron", 108, 21, 1.6, 0, 2.2, n=8)
    # the weir hut on the west bank beside the footbridge's landing, a kiosk and a bin store on the west edge
    walls(k, "redengineeringbrick", -24, 34, -14.5, 44, 0, 4.5, [("e", 39, 3.0, 3.4, 1.4), ("n", -19, 4.0, DOOR_H), ("s", -19, 4.0, DOOR_H)])
    k.box("concreterooftiles", -24.3, 4.5, 33.7, -14.2, 4.8, 44.3)
    k.box("electricalcabinet", -120, 0, 24, -111, 3.5, 32)
    k.box("precastconcrete", -120, 0, -72, -112, 3.0, -66)
    # the power side's south lawn: a retaining wall with a planted top, a gap at each end
    k.box("precastconcrete", -46, 0, 54, -24, 2.4, 55); k.box("hedge", -46, 2.4, 54, -24, 3.0, 55)
    # the SE lawn (campus, between the road and the east reach): a car park canopy with solid back walls (south, east)
    k.box("corrugated", 60, 3.4, 14, 84, 3.7, 28)
    k.box("precastconcrete", 60, 0, 27.6, 84, 3.4, 28); k.box("precastconcrete", 83.6, 0, 14, 84, 3.4, 27.6)
    for (x, z) in ((60, 14), (72, 14)):
        k.box("galvanizedsteel", x, 0, z, x + 0.4, 3.4, z + 0.4)
    # ---- power lines: two pylons, cables (solid, thin: grapple rails) from tower to pylon to pylon to tower, 15-20 m up
    for (px, pz) in ((-30, 30), (40, -60)):
        lattice_tower(k, px, pz, 22, 3)
        k.box("galvanizedsteel", px - 4, 20, pz - 0.3, px + 4, 20.5, pz + 0.3)
    for (a, b, y) in (((-105, 45), (-30, 30), 18.6), ((-30, 30), (40, -60), 19.6), ((40, -60), (91, 8), 18.6)):
        k.bar("leadsheet", a, b, 0.16, y, y + 0.16)
        k.bar("clip", a, b, 0.7, y + 0.16, y + 1.4)     # (no standing on them: no tightrope perch over the bridges)

    # ---- decoration and hiding places: trees in clumps on the lawns, hedges, parked cars, cable reels (off the roads
    # and doorways; crouch spots for hide and seek, loops for tag)
    decorate(k)

    # ---- spawns: tag, hide and free for all over both banks; A on the campus, B on the power side
    for (x, z) in ((75, -40), (77, -30), (73, -50), (86, -52)):
        k.spawn("a", x, 0, z, yaw_to(x, z, 50, -10))
    for (x, z) in ((-70, 37), (-69, 44), (-80, 52), (-56, 53)):
        k.spawn("b", x, 0, z, yaw_to(x, z, -40, 10))
    for (x, z) in ((40, -15), (98, 12), (-10, -79), (-110, -70), (-30, 0), (-60, 72), (50, 80), (-114, -5)):
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
    clumps = [((93, 27), 2, 2.5), ((20, 20), 3, 4), ((-35, 45), 4, 6), ((-114, 40), 2, 2.5),
              ((-68, 80), 4, 6), ((66, -62), 2, 3), ((-110, -62), 3, 4), ((60, 82), 3, 6), ((-20, 25), 2, 3)]
    for ((cx, cz), n, sp) in clumps:
        for i in range(n):
            tree(cx + R.uniform(-sp, sp), cz + R.uniform(-sp, sp), R.uniform(0.85, 1.15))
    # hedges round the campus plaza and along the power side's lawn: crouch behind them
    for (x0, z0, x1, z1) in ((20, -12, 28, -10.8), (34, -12, 46, -10.8), (56, -12, 70, -10.8),
                             (-40, 20, -28, 21.2), (-22, 20, -15, 21.2), (-50, -40 + 9, -48.8, -28 + 9)):
        k.box("hedge", x0, 0, z0, x1, 2.0, z1)
    # parked cars beside the roads (not on them)
    for (x, z) in ((18, 8), (24.5, 8), (-26, -10), (-33, -10), (100, -8), (-100, 8)):
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
