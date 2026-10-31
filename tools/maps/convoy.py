"""DESERT CONVOY (1.19.6): multi point on a highway through a desert town to an airfield, under a sandstorm sky,
472 x 140 m, attacked west to east through five points about 80 m apart:
  A  the checkpoint (x -160): barriers across half the road, a guard hut of 1/8 m sheet (shoot-through: don't hold
     the point from inside it), concrete blocks, a guard tower (6 m, a ladder) as the defenders' perch, and the
     burnt-out convoy (two trucks, an armoured car) west of it on the road;
  B  the fuel station (x -80, south of the road): a forecourt under a canopy (its roof at 5 m, walkable: a ladder up
     a column, and a plank from the shop's roof, which has stairs), pump islands, the storage tanks behind;
  C  the market town (x 0): the street widens into a square (a well, market carts and stalls) between four
     flat-roofed mud-brick blocks, each cut by a 4 m alley from the road to the souk / the north edge; plank bridges
     join the roofs across the square's north and south ends (stairs up in two corners); the four gate houses are
     hollow (doors through, a partition, a stair to the roof); a tower (12 m, a stair round its outside, open on top)
     to the north, a covered souk lane to the south (1/8 m awnings and stall boards: shoot-through);
  D  the wadi bridge (x 82): the highway crosses a dry riverbed 6 m deep on a concrete bridge with a girder underside
     (hooks), barriers on its deck; a ford (ramps down and up) to the north is the second crossing; the point is the
     deck and the riverbed under it (two levels);
  E  the airfield (x 180): the point is the hangar's mouth (pallets, a tug), a cargo plane inside (a hollow hold up a
     ramp at its tail, wings and four engines), the control tower behind the hangar's south-east corner (12 m, a stair
     round it, the defenders' perch) and fuel bowsers and tank traps on the apron.
Each stage's attackers spawn a little past the point they took (south of the road), its defenders between the next
point and the one after (north): see SPAWNS for the measured arrival times. Three sandstorm drifts (smoke, sight only)
lie on the open highway between the points. Dunes close the long sides, clip from 2 m up them.
Fix round (1.19.6 review, q's answers): the four spawn shelters that stood in lanes (the road at the town's west gate,
the souk, the airfield's gate) are gone, the six off-lane ones kept (they stop each stage's groups seeing each other)
and att2 moved onto the road's edge; point arrival (attackers minus defenders, nav) A -0.1, B -0.8, C +0.3 (was
+1.0), D -0.1, E +0.2 s. Each stage has 3-4 big set pieces on its flanks (a bus and a truck on their sides with fires,
ruins with half roofs, collapsed billboards as slopes, sand drifts crossing the long flank lines, tank traps, scrap and
tyres), and every point has cover in its box (on-point cover A 15%, B 15%, C 12%, D 35%+, E 20%; it was 1% at B);
the town has alleys and four houses with rooms, the plane is built, the wadi has boulders, a car, a dead tree. Cover
0.057 -> 0.080 (points with cover 11% -> 16%), decor 1.3 -> 2.4, 28 lights, 950 brushes: still short of 0.15, which
only clutter across the open desert would reach (q: lanes clean at 24 m/s). The control tower moved behind the hangar
and both towers got stairs; perch races (walk, slowest spawn, attackers / defenders): guard tower 11.6 / 6.5, canopy
8.8 / 7.8, town tower 13.5 / 11.9, control tower 18.7 / 8.1 s (it was 12.6 / 15.4). 20 FFA spawns. Bots (points, 8,
8 seeds): A 3, B 2, 3 at the 600 s cap."""
import math, random
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

HX, HZ = 236, 70
LID = 70.0
ROAD = 7.0                          # the highway: z -7..7
WADI = (70.0, 95.0)                 # the riverbed's x range (it runs along z the whole width)
WADI_Y = -6.0
FORD = (-50.0, -38.0)               # the ford's z range (ramps down and up)
POINTS = {"A": (-168, -8, -152, 8, 0.0), "B": (-88, 10, -72, 26, 0.0), "C": (-10, -10, 10, 10, 0.0),
          "D": (75, -6, 90, 6, 0.0), "E": (172, -12, 188, 12, 0.0)}
# staged spawns: stage -> (x, z) of the four attackers' middle, (x, z) of the four defenders' middle
SPAWNS = {0: ((-214, 22), (-120, -38)),
          1: ((-136, 30), (-41, -1)),
          2: ((-46, -3), (64, -30)),
          3: ((14, 40), (140, -40)),
          4: ((128, 30), (222, 0))}
# the town's blocks: the four round the square are cut by a 4 m alley (x +-26..30) from the road to the souk / the
# north edge; the four gate houses (x +-42..54) are hollow (doors through, a stair to the roof): HOLLOW
HOUSES = [(-26, -34, -16, -12, 5.0), (16, -34, 26, -12, 4.5), (-26, 12, -16, 34, 4.5), (16, 12, 26, 34, 5.0),
          (-36, -34, -30, -12, 5.0), (30, -34, 36, -12, 4.5), (-36, 12, -30, 34, 4.5), (30, 12, 36, 34, 5.0),
          (-54, -32, -42, -14, 4.0), (42, 14, 54, 32, 4.0), (-54, 14, -42, 30, 4.5), (42, -30, 54, -14, 4.5),
          (-40, 46, -10, 58, 4.0), (10, 46, 40, 58, 4.0)]
HOLLOW = {(-54, -32), (42, 14), (-54, 14), (42, -30)}
HANGAR = (168, -24, 212, 24)


def build_ground(k):
    wx0, wx1 = WADI
    holes = [(wx0, -HZ, wx1, HZ)]
    for t in tiles([(-HX, -HZ, HX, HZ)], 40):
        for (x0, z0, x1, z1) in minus(t, holes + [(-HX, -ROAD, HX, ROAD), (136, -40, HX, 40)]):
            k.box("sunbakedhardpan", x0, -7, z0, x1, 0, z1)
    # the road (sand blown over it in places), the apron
    for t in tiles([(-HX, -ROAD, HX, ROAD)], 40):
        for (x0, z0, x1, z1) in minus(t, holes + [(136, -40, HX, 40)]):
            sandy = int((x0 + 1000) // 40) % 3 == 1
            k.box("sandburiedroad" if sandy else "asphalt", x0, -7, z0, x1, 0, z1)
    for t in tiles([(136, -40, HX, 40)], 40): k.box("oilstainedconcrete", t[0], -7, t[1], t[2], 0, t[3])
    for x in range(-228, 136, 12):
        if wx0 - 2 < x < wx1 + 2: continue
        k.box("roadmarkings", x, 0, -0.12, x + 5, 0.01, 0.12)
    # the wadi: its bed, the ford's ramps (west bank down, east bank up)
    for t in tiles([(wx0, -HZ, wx1, HZ)], 40): k.box("driedriverbed", t[0], -8, t[1], t[2], WADI_Y, t[3])
    k.ramp("driedriverbed", wx0, FORD[0], wx0 + 10, FORD[1], WADI_Y, 0, "-x")
    k.ramp("driedriverbed", wx1 - 10, FORD[0], wx1, FORD[1], WADI_Y, 0, "+x")
    # the bridge: its deck at the road's level, girders under it (hooks), two piers, rails
    k.box("precastconcrete", wx0, -1, -ROAD, wx1, 0, ROAD)
    for z in (-5, 0, 5): k.box("bridgegirder", wx0, -2.2, z - 0.4, wx1, -1, z + 0.4)
    for x in (78, 87): k.box("weatheredconcrete", x - 0.6, WADI_Y, -5, x + 0.6, -2.2, 5)
    for z in (-ROAD, ROAD - 0.3): k.box("galvanizedsteel", wx0, 0, z, wx1, 1.1, z + 0.3)
    for (x0, z0, x1, z1) in ((76.5, 2.2, 80.5, 2.8), (84.5, -2.8, 88.5, -2.2)):         # (concrete barriers: low cover)
        k.box("precastconcrete", x0, 0, z0, x1, 1.1, z1)


def edges(k):
    # dunes along the long sides (walkable up to their crest, clip over it), walls of dune at the ends
    for x in range(-HX, HX, 59):
        x1 = min(HX, x + 59)
        if WADI[0] < x1 and x < WADI[1]:
            for (a, b) in ((x, WADI[0]), (WADI[1], x1)):
                if b > a:
                    k.wedge("reddunes", 2, a, 0, 56, b, 12, HZ); k.wedge("reddunes", 3, a, 0, -HZ, b, 12, -56)
            continue
        k.wedge("reddunes", 2, x, 0, 56, x1, 12, HZ); k.wedge("reddunes", 3, x, 0, -HZ, x1, 12, -56)
    k.wedge("reddunes", 1, -HX, 0, -56, -HX + 10, 12, 56); k.wedge("reddunes", 0, HX - 10, 0, -56, HX, 12, 56)
    for (x0, z0, x1, z1) in ((-HX - 2, -HZ - 2, HX + 2, -HZ), (-HX - 2, HZ, HX + 2, HZ + 2), (-HX - 2, -HZ, -HX, HZ), (HX, -HZ, HX + 2, HZ)):
        k.box("clip", x0, -8, z0, x1, LID, z1)
        k.box("ripplesand", x0, -8, z0, x1, 12, z1)
    k.box("clip", -HX - 2, LID, -HZ - 2, HX + 2, LID + 1, HZ + 2)
    # (clip from 2 m up the dunes: their slopes are a wall, not a balcony down the long sides)
    zc = 56 + 14 * 2 / 12; xc = HX - 10 + 10 * 2 / 12
    k.box("clip", -HX, -8, zc, HX, LID, HZ); k.box("clip", -HX, -8, -HZ, HX, LID, -zc)
    k.box("clip", -HX, -8, -zc, -xc, LID, zc); k.box("clip", xc, -8, -zc, HX, LID, zc)
    # (the wadi runs on under the dunes: its ends walled)
    for z in (-HZ, HZ): k.box("sandstoneashlar", WADI[0], -8, z - (1 if z < 0 else 0), WADI[1], 0, z + (1 if z > 0 else 0))


def truck(k, x, z, burnt=True):
    """a burnt-out army truck along x: cab, bed (2.4 m wide, 8 m long)"""
    m = "tankarmour" if burnt else "powdercoatgreen"
    k.box(m, x - 4, 0.5, z - 1.2, x - 1.8, 2.8, z + 1.2)
    k.box("aircraftwreckage", x - 1.8, 0.8, z - 1.2, x + 4, 1.6, z + 1.2)
    k.box("leantocanvas", x - 1.6, 1.6, z - 1.2, x + 3.8, 1.725, z + 1.2)
    for dx in (-3, 0.5, 3):
        k.box("engineblock", x + dx - 0.5, 0, z - 1.25, x + dx + 0.5, 0.8, z + 1.25)


def checkpoint(k):
    """A: barriers across half the road each way (offset), a guard hut of thin sheet, blocks, the guard tower, the
    burnt convoy to its west"""
    for (x, z0, z1) in ((-176, -7, 0), (-146, 0, 7), (-166, 9, 15), (-154, -15, -9)):
        k.box("precastconcrete", x - 0.4, 0, z0, x + 0.4, 1.1, z1)
    hx0, hz0, hx1, hz1 = -164, 10, -158, 14                        # the guard hut (1/8 m sheet walls)
    k.box("corrugated", hx0, 0, hz0, hx1, 2.6, hz0 + 0.125); k.box("corrugated", hx0, 0, hz1 - 0.125, hx1, 2.6, hz1)
    k.box("corrugated", hx0, 0, hz0, hx0 + 0.125, 2.6, hz1); k.box("corrugated", hx1 - 0.125, 0, hz0 + 1.6, hx1, 2.6, hz1)
    k.box("corrugated", hx0 - 0.2, 2.6, hz0 - 0.2, hx1 + 0.2, 2.8, hz1 + 0.2)
    for (x, z) in ((-172, -11), (-150, 11), (-162, -14)):
        k.box("precastconcrete", x - 1, 0, z - 1, x + 1, 1.5, z + 1)
    # the guard tower: four legs, a platform at 6 m with a sandbag lip, a ladder on its west side
    tx, tz = -150, -18
    for (dx, dz) in ((-1.6, -1.6), (1.2, -1.6), (-1.6, 1.2), (1.2, 1.2)):
        k.box("wideplanks", tx + dx, 0, tz + dz, tx + dx + 0.4, 6, tz + dz + 0.4)
    k.box("wideplanks", tx - 2, 5.7, tz - 2, tx + 2, 6, tz + 2)
    for (a, b, c, d) in ((tx - 2, tz - 2, tx + 2, tz - 1.6), (tx - 2, tz + 1.6, tx + 2, tz + 2), (tx + 1.6, tz - 1.6, tx + 2, tz + 1.6)):
        k.box("sandbagwall", a, 6, b, c, 7, d)
    k.box("wideplanks", tx - 2.2, 0, tz - 0.6, tx - 2.0, 6, tz + 0.6)
    k.ladder(tx - 2.2, tz, 0, 6, "e")
    # the burnt convoy (on the road west of the point, staggered so a lane stays open)
    truck(k, -190, -3.5); truck(k, -204, 3.5)
    k.box("tankarmour", -199, 0, -5, -193, 1.6, -1.5); k.box("tankarmour", -197.5, 1.6, -4.2, -194.5, 2.4, -2.3)


def fuel_station(k, m):
    """B: the forecourt and canopy (roof 5 m), the shop (roof 4.5 m, stairs up its side, a plank to the canopy), the
    tanks behind"""
    k.box("concretepavers", -96, 0, 8, -64, 0.02, 32)
    for (x, z) in ((-87, 11), (-73, 11), (-87, 25), (-73, 25)):
        k.box("galvanizedsteel", x - 0.4, 0, z - 0.4, x + 0.4, 4.6, z + 0.4)
    k.box("galvanizedsteel", -90, 4.6, 8, -70, 5.0, 28)                           # the canopy (its roof walkable)
    k.box("hazard", -90, 5.0, 8, -70, 5.05, 8.3); k.box("hazard", -90, 5.0, 27.7, -70, 5.05, 28)
    k.box("galvanizedsteel", -80.6, 0, 8, -79.4, 4.6, 8.4)                       # (a post at the canopy's north edge,
    k.ladder(-80, 8, 0, 5.0, "s")                                                 #  a ladder up it from the road side)
    for (x, z) in ((-82, 14), (-78, 14), (-82, 22), (-78, 22)):                 # the pump islands
        k.box("kerbstones", x - 1.6, 0, z - 0.6, x + 1.6, 0.25, z + 0.6)
        k.box("powdercoatyellow", x - 0.5, 0.25, z - 0.35, x + 0.5, 1.7, z + 0.35)
    # the shop: solid walls, a door each end, its roof at 4.5 with stairs up its west side, a plank to the canopy
    x0, z0, x1, z1 = -96, 32, -74, 42
    t = 0.4
    k.box("stucco", x0, 0, z0, x0 + t, 4.5, z1); k.box("stucco", x1 - t, 0, z0, x1, 4.5, z1)
    k.box("stucco", x0 + t, 0, z1 - t, x1 - t, 4.5, z1)
    k.box("stucco", x0 + t, 0, z0, -90, 4.5, z0 + t); k.box("stucco", -87, 0, z0, -80, 4.5, z0 + t); k.box("stucco", -77, 0, z0, x1 - t, 4.5, z0 + t)
    k.box("stucco", -90, 2.6, z0, -87, 4.5, z0 + t); k.box("stucco", -80, 2.6, z0, -77, 4.5, z0 + t)
    k.box("glass", -87, 1.0, z0 + 0.1, -80, 2.4, z0 + 0.3)
    k.box("stucco", -87, 0, z0, -80, 1.0, z0 + t)
    k.box("concreterooftiles", x0 - 0.2, 4.5, z0 - 0.2, x1 + 0.2, 4.9, z1 + 0.2)
    k.stairs("sandstoneashlar", x0 - 3, z0 + 1, x0, z1 - 1, 4.9, "z", -1)
    k.box("wideplanks", -86, 4.75, 28, -83, 5.0, z0 - 0.2)                       # (plank: shop roof to canopy)
    for x in (-92, -82):
        k.box("galvanizedsteel", x, 2.0, z1 - 1.2, x + 2.4, 2.8, z1 - 0.4)           # (shelves on the back wall)
    k.light(-85, 3.8, 37, 9, 0.7)
    # the storage tanks behind (big cover; the yard south of the shop)
    for x in (-100, -86, -72):
        k.disc("electroplatednickel", x, 50, 4, 0, 5, n=8)


def wrap_stair(k, mat, x0, z0, x1, z1, top, w=1.4):
    """a solid stair round the outside of a tower (x0..x1, z0..z1): up its +z face (+x), its +x face (-z), its -z face
    (-x) and its -x face (+z) to top beside the -x face's +z end, a quarter of the height a flight, landings at the corners"""
    q = top / 4
    k.stairs(mat, x0, z1, x1, z1 + w, q, "x", 1)
    k.box(mat, x1, 0, z1, x1 + w, q, z1 + w)
    k.stairs(mat, x1, z0, x1 + w, z1, 2 * q, "z", -1, base=q)
    k.box(mat, x1, 0, z0 - w, x1 + w, 2 * q, z0)
    k.stairs(mat, x0, z0 - w, x1, z0, 3 * q, "x", -1, base=2 * q)
    k.box(mat, x0 - w, 0, z0 - w, x0, 3 * q, z0)
    k.stairs(mat, x0 - w, z0, x0, z1, top, "z", 1, base=3 * q)


def hollow_house(k, x0, z0, x1, z1, h):
    """a gate house with rooms: doors on its road end, its far end and its town side, a partition with a door, a
    stair along its outer wall up through a hatch to the roof, a rug and crates against the walls"""
    t = 0.4
    cx = (x0 + x1) / 2
    road_z, far_z = (z1, z0) if z1 < 0 else (z0, z1)
    town_x = x1 if cx < 0 else x0                      # the side toward the square
    out_x = x0 if cx < 0 else x1
    def wall_x(zf, gaps):                              # a wall along x at zf (its outer face), door gaps at x
        za, zb = (zf, zf + t) if zf == z0 else (zf - t, zf)
        xs = [x0] + sum([[g - 1.1, g + 1.1] for g in sorted(gaps)], []) + [x1]
        for i in range(0, len(xs), 2): k.box("stucco", xs[i], 0, za, xs[i + 1], h, zb)
        for g in gaps: k.box("stucco", g - 1.1, 2.6, za, g + 1.1, h, zb)
    def wall_z(xf, gaps):
        xa, xb = (xf, xf + t) if xf == x0 else (xf - t, xf)
        zs = [z0 + t] + sum([[g - 1.1, g + 1.1] for g in sorted(gaps)], []) + [z1 - t]
        for i in range(0, len(zs), 2): k.box("stucco", xa, 0, zs[i], xb, h, zs[i + 1])
        for g in gaps: k.box("stucco", xa, 2.6, g - 1.1, xb, h, g + 1.1)
    wall_x(road_z, [cx]); wall_x(far_z, [cx])
    wall_z(town_x, [(z0 + z1) / 2]); wall_z(out_x, [])
    # the stair along the outer wall, rising away from the road; the hatch over its top half
    sx0, sx1 = (x0 + t, x0 + t + 1.6) if out_x == x0 else (x1 - t - 1.6, x1 - t)
    run = 2 * h
    if road_z < far_z: sz0, sz1, rising = z0 + t + 1.6, z0 + t + 1.6 + run, 1
    else: sz0, sz1, rising = z1 - t - 1.6 - run, z1 - t - 1.6, -1
    k.stairs("sandstoneashlar", sx0, sz0, sx1, sz1, h, "z", rising)
    hz0, hz1 = (sz0 + run * 0.2, sz1 + 0.6) if rising > 0 else (sz0 - 0.6, sz1 - run * 0.2)
    for (a, b, c, d) in minus((x0 + t, z0 + t, x1 - t, z1 - t), [(sx0 - 0.4, hz0, sx1 + 0.4, hz1)]):
        k.box("concreterooftiles", a, h - 0.4, b, c, h, d)
    # the partition across the middle (a door at its town end), the floor
    zm = (z0 + z1) / 2 + (2.5 if road_z < far_z else -2.5)
    ia, ib = sorted((sx1 if out_x == x0 else sx0, town_x - t if town_x == x1 else town_x + t))
    pa, pb = (ia, ib - 2.6) if town_x == x1 else (ia + 2.6, ib)          # (the door: 2.6 m at the town end)
    k.box("crumblingplaster", pa, 0, zm - 0.15, pb, h - 0.4, zm + 0.15)
    k.box("hextileterracotta", x0 + t, 0, z0 + t, x1 - t, 0.02, z1 - t)
    k.box("leantocanvas", ia + 1.0, 0.02, (road_z + zm) / 2 - 1.5, ib - 1.0, 0.05, (road_z + zm) / 2 + 1.5)
    k.box("woodencrate", ia + 0.1, 0, far_z + (0.5 if far_z == z0 else -1.7), ia + 1.3, 1.2, far_z + (1.7 if far_z == z0 else -0.5))
    k.light(cx, h - 1.0, (road_z + zm) / 2, 8, 0.6)


def town(k):
    """C: four mud-brick blocks round the square, smaller houses beyond, the tower, plank bridges, the souk"""
    for (x0, z0, x1, z1, h) in HOUSES:
        if (x0, z0) in HOLLOW: hollow_house(k, x0, z0, x1, z1, h)
        else: k.box("stucco", x0, 0, z0, x1, h, z1)
        sides = [(x0, z0, x1, z0 + 0.4), (x0, z1 - 0.4, x1, z1), (x0, z0 + 0.4, x0 + 0.4, z1 - 0.4), (x1 - 0.4, z0 + 0.4, x1, z1 - 0.4)]
        if x1 == -16: sides.pop(3)        # (open toward the square: the stairs and the plank bridges arrive there)
        if x0 == 16: sides.pop(2)
        for (a, b, c, d) in sides:
            k.box("mudandstrawdaub", a, h, b, c, h + 0.8, d)                       # (the parapet)
    k.box("flagstonepaving", -16, 0, -34, 16, 0.02, 34)
    # stairs up to the roofs: the north-west block's east face, the south-east block's west face
    k.stairs("sandstoneashlar", -16, -26, -13, -15, 5, "z", -1)
    k.stairs("sandstoneashlar", 13, 15, 16, 26, 5, "z", 1)
    # the plank bridges across the square's ends (roof to roof)
    k.box("wideplanks", -16, 4.7, -31, 16, 5.0, -28.6)
    k.box("wideplanks", -16, 4.7, 28.6, 16, 5.0, 31)
    # the tower north of the square: solid masonry 6 x 6, its open top at 12 behind a 1.1 m parapet, a stair round
    # its outside (no roof: rockets reach it)
    tx, tz = 0, -44
    k.box("sandstoneashlar", tx - 3, 0, tz - 3, tx + 3, 12, tz + 3)
    wrap_stair(k, "sandstoneashlar", tx - 3, tz - 3, tx + 3, tz + 3, 12)
    for (a, b, c, d) in ((tx - 3, tz - 3, tx + 3, tz - 2.6), (tx - 3, tz + 2.6, tx + 3, tz + 3), (tx - 3, tz - 2.6, tx - 2.6, tz + 0.4), (tx + 2.6, tz - 2.6, tx + 3, tz + 2.6)):
        k.box("sandstoneashlar", a, 12, b, c, 13.1, d)
    k.box("charredtimber", tx - 1.5, 13.1, tz - 3, tx + 1.5, 13.3, tz - 2.6)
    # the souk: a covered lane along x south of the blocks (awnings 1/8 m, stall boards 1/8 m)
    k.box("leantocanvas", -40, 3.4, 37, 40, 3.525, 45)
    for x in range(-36, 37, 8):
        k.box("wideplanks", x - 0.1, 0, 37, x + 0.1, 3.4, 37.2); k.box("wideplanks", x - 0.1, 0, 44.8, x + 0.1, 3.4, 45)
        if x % 16 == 4:
            k.box("wideplanks", x + 1, 0, 44.875, x + 5, 1.1, 45)                 # (stall boards on the south side)
            k.box("woodencrate", x + 1.5, 0, 43.6, x + 2.7, 1.0, 44.8)
    for x in range(-32, 33, 16):
        k.box("lightblock", x - 0.2, 3.0, 40.8, x + 0.2, 3.4, 41.2); k.light(x, 2.8, 41, 9, 0.7)


def wadi_decor(k):
    """the riverbed: boulders along its banks, a car washed against the north pier, a dead tree, pebble bars; the ford
    (z -50..-38) and the run under the bridge stay clear"""
    for (x, z, r, h) in ((73.5, -24, 2.2, 2.6), (91, -16, 2.6, 3.0), (75, 22, 2.4, 2.2), (90, 34, 2.0, 2.8), (82, 50, 2.8, 2.4),
                         (73.5, -62, 2.4, 3.2), (91, 60, 2.2, 2.6), (90.5, -30, 1.6, 1.8)):
        k.disc("windscouredrock", x, z, r, WADI_Y, WADI_Y + h, n=7)
        k.disc("windscouredrock", x + 0.4, z - 0.3, r * 0.6, WADI_Y + h, WADI_Y + h + 0.7, n=6)
    for (x0, z0, x1, z1) in ((76, -34, 84, -30), (78, 12, 88, 18), (74, 40, 80, 46)):
        k.box("riverpebbles", x0, WADI_Y, z0, x1, WADI_Y + 0.03, z1)
    # a car on its side by the bridge's south edge, crates between the piers (cover under the bridge: D's lower level)
    k.box("rustedsheet", 80.5, WADI_Y, 7.6, 84.5, WADI_Y + 2.0, 9.1); k.box("sootysteel", 80.8, WADI_Y, 9.1, 84.2, WADI_Y + 1.6, 9.4)
    k.box("woodencrate", 86, WADI_Y, -3.5, 87.4, WADI_Y + 1.4, -2.1); k.box("woodencrate", 79, WADI_Y, 1.6, 80.2, WADI_Y + 1.2, 2.8)
    # a dead tree on the east bank
    k.box("bonedrywood", 92.6, WADI_Y, 25.6, 93.2, WADI_Y + 6.5, 26.2)
    k.bar("bonedrywood", (92.9, 25.9), (90.0, 23.5), 0.3, WADI_Y + 4.0, WADI_Y + 4.3)
    k.bar("bonedrywood", (92.9, 25.9), (94.6, 28.6), 0.3, WADI_Y + 5.2, WADI_Y + 5.5)


def airfield(k):
    """E: the gate (barrier blocks with gaps), the hangar (open west, back doors east), the cargo plane, the tower,
    the bowsers"""
    for (z0, z1) in ((-40, -26), (-18, -9), (9, 18), (26, 40)):
        k.box("precastconcrete", 135.6, 0, z0, 136.4, 1.1, z1)
    x0, z0, x1, z1 = HANGAR
    t = 0.6
    k.box("corrugated", x0, 0, z0, x1, 14, z0 + t); k.box("corrugated", x0, 0, z1 - t, x1, 14, z1)
    k.box("corrugated", x0, 10, z0 + t, x0 + t, 14, z1 - t)                       # (the front: open below 10 m)
    for (a, b) in ((z0 + t, -14), (-10, 10), (14, z1 - t)):                       # (the back: two doors, 4 m)
        k.box("corrugated", x1 - t, 0, a, x1, 14, b)
    for (a, b) in ((-14, -10), (10, 14)): k.box("corrugated", x1 - t, 4, a, x1, 14, b)
    k.box("standingseamroof", x0 - 0.5, 14, z0 - 0.5, x1 + 0.5, 14.6, z1 + 0.5)
    for x in range(172, 211, 8): k.box("galvanizedsteel", x - 0.3, 12, z0 + t, x + 0.3, 12.6, z1 - t)
    # the cargo plane: a hollow fuselage (its hold open only at the tail, up a ramp from the hangar's mouth), a
    # nose, a tail fin and tailplanes, wings over the hold with four engines to hook
    fx0, fx1 = 186, 206
    k.box("steeldeckplate", fx0, 0.8, -2.7, fx1, 1.2, 2.7)                            # (the hold's floor)
    for zs in (-1, 1):
        za, zb = sorted((zs * 2.7, zs * 3.0))
        k.box("aircraftskin", fx0, 0.8, za, fx1, 6, zb)
        k.box("anodizedblack", 199, 2.0, zs * 3.0 - 0.02 * zs, 201, 3.6, zs * 3.0)      # (its side doors: shut)
    k.box("aircraftskin", fx0, 5.6, -2.7, fx1, 6, 2.7)
    k.box("aircraftskin", fx1, 0.8, -3, fx1 + 0.4, 6, 3)
    k.prism("aircraftskin", [(fx1 + 0.4, -3), (fx1 + 5, -1.2), (fx1 + 5, 1.2), (fx1 + 0.4, 3)], 1.2, 5.4)
    k.box("smokedglass", fx1 + 2.5, 4.2, -1.9, fx1 + 4.0, 5.0, 1.9)
    k.box("aircraftskin", fx0, 6, -0.2, fx0 + 4.5, 9.5, 0.2)                          # (the fin)
    k.box("aircraftskin", fx0, 6.2, -6, fx0 + 3, 6.6, 6)                              # (the tailplanes)
    k.ramp("steeldeckplate", 182, -2.2, 186, 2.2, 0, 1.2, "+x")
    k.box("aircraftskin", 194, 6, -20, 200, 6.6, 20)                                  # (the wings, high)
    for z in (-14, -8, 8, 14):
        k.tube_x("sootysteel", 192, 198.5, 4.9, z, 1.0)
        k.box("aircraftskin", 195, 5.6, z - 0.2, 197, 6, z + 0.2)
    for (x, z) in ((190, -2.0), (190, 2.0), (204, 0)):
        k.box("anodizedblack", x - 0.5, 0, z - 0.5, x + 0.5, 0.8, z + 0.5)
    # the control tower behind the hangar's south-east corner (the defenders' side): solid to 12, a stair round it,
    # the cab on top (a parapet, glass, a roof); it watches the apron's south lane and the hangar's back doors
    tx, tz = 212, -34
    k.box("precastconcrete", tx - 3, 0, tz - 3, tx + 3, 12, tz + 3)
    wrap_stair(k, "precastconcrete", tx - 3, tz - 3, tx + 3, tz + 3, 12)
    for (a, b, c, d) in ((tx - 3, tz - 3, tx + 3, tz - 2.8), (tx - 3, tz + 2.8, tx + 3, tz + 3), (tx - 3, tz - 2.8, tx - 2.8, tz + 0.4), (tx + 2.8, tz - 2.8, tx + 3, tz + 2.8)):
        k.box("precastconcrete", a, 12, b, c, 13, d)
        k.box("glass", a, 13, b, c, 14.6, d)
    k.box("precastconcrete", tx - 3, 14.6, tz - 3, tx + 3, 15, tz + 3)
    k.box("galvanizedsteel", tx - 0.1, 15, tz - 0.1, tx + 0.1, 18, tz + 0.1)            # (the mast)
    k.box("anodizedblack", tx + 1, 12, tz - 2.6, tx + 2.6, 13, tz - 1)                    # (the console)
    k.light(tx, 13.8, tz, 8, 0.6)
    # fuel bowsers on the apron (cover between the gate and the hangar)
    for (x, z) in ((150, 12), (158, -16), (146, -8)):
        k.box("powdercoatyellow", x - 4, 0.5, z - 1.2, x - 2, 2.6, z + 1.2)
        k.disc("electroplatednickel", x + 1, z, 1.2, 0.5, 2.8, n=8)
        k.box("anodizedblack", x - 3.6, 0, z - 1.25, x + 3.6, 0.6, z + 1.25)


def cover(k):
    """the open desert between the points: rock outcrops and low dune humps off the road (30-45% of each approach
    under cover), wrecked cars on the road; all big, few, and nothing in the road's middle"""
    for (x, z, w, d, h) in ((-188, -32, 5, 3, 2.4), (-176, 42, 4, 4, 2.0), (-138, -22, 5, 3, 2.2), (-126, 46, 6, 3, 2.6),
                            (-110, 18, 4, 3, 1.8), (-64, -26, 4, 4, 2.2), (-100, -52, 6, 4, 2.6), (48, 26, 5, 3, 2.2),
                            (56, -48, 6, 4, 2.6), (110, -26, 5, 3, 2.2), (116, 46, 4, 4, 2.0), (154, 30, 5, 3, 2.2)):
        k.box("windscouredrock", x - w / 2, 0, z - d / 2, x + w / 2, h, z + d / 2)
        k.box("windscouredrock", x - w / 2 + 0.6, h, z - d / 2 + 0.5, x + w / 2 - 1.0, h + 0.8, z + d / 2 - 0.4)
    for (x0, z0, x1, z1) in ((-200, 30, -184, 38), (-150, -48, -132, -40), (-34, -60, -12, -52), (100, 38, 120, 46), (24, -60, 44, -52)):
        zc = (z0 + z1) / 2
        k.wedge("ripplesand", 3, x0, 0, z0, x1, 1.6, zc); k.wedge("ripplesand", 2, x0, 0, zc, x1, 1.6, z1)
    for (x, z) in ((-120, -4), (-52, 3.5), (36, -3.5), (112, 3.5)):
        k.box("rustedsheet", x - 2, 0.3, z - 1, x + 2, 1.0, z + 1); k.box("rustedsheet", x - 1.1, 1.0, z - 0.9, x + 0.9, 1.5, z + 0.9)


def bus_on_side(k, x, z):
    """a bus on its side along x (12 m long, its roof facing +z): 2.6 m tall, 3.2 m across, its windows a dark band,
    a fire in its engine bay"""
    k.box("batteredyellowpaint", x - 6, 0, z - 1.6, x + 6, 2.6, z + 1.6)
    k.box("sootysteel", x - 5.6, 0.6, z - 1.62, x + 5.6, 1.6, z - 1.6)
    for dx in (-4, 3.5):
        k.box("engineblock", x + dx - 0.5, 0.3, z + 1.6, x + dx + 0.5, 1.3, z + 1.9)
    k.box("emberbed", x + 4.4, 2.6, z - 1.0, x + 5.6, 2.8, z + 1.0); k.light(x + 5, 4, z, 10, 0.9)


def truck_on_side(k, x, z, along="x"):
    """an army truck on its side (cab and bed 11 m, 2.4 m tall, 2.6 m across) and its spilt load"""
    if along == "x":
        k.box("powdercoatgreen", x - 5.5, 0, z - 1.3, x - 2.5, 2.4, z + 1.3); k.box("tankarmour", x - 2.5, 0, z - 1.3, x + 5.5, 2.4, z + 1.3)
        k.box("woodencrate", x + 6, 0, z - 0.8, x + 7.2, 1.2, z + 0.4)
    else:
        k.box("powdercoatgreen", x - 1.3, 0, z - 5.5, x + 1.3, 2.4, z - 2.5); k.box("tankarmour", x - 1.3, 0, z - 2.5, x + 1.3, 2.4, z + 5.5)
        k.box("woodencrate", x - 0.8, 0, z + 6, x + 0.4, 1.2, z + 7.2)


def billboard(k, x, z, face):
    """a collapsed billboard: one post still standing (7 m, its frame), the board fallen against it as a slope (10 m
    wide, 3 m up over 4: walkable), the other post snapped"""
    d = 1 if face == "+z" else -1
    k.ramp("sandblastedsign", x - 5, z - (4 if d > 0 else 0), x + 5, z + (0 if d > 0 else 4), 0, 3, face)
    k.box("galvanizedsteel", x - 4.4, 0, z - 0.2, x - 4.0, 7, z + 0.2); k.box("galvanizedsteel", x - 4.4, 6.0, z - 0.2, x + 1, 6.3, z + 0.2)
    k.box("galvanizedsteel", x + 4.0, 0, z - 0.2, x + 4.4, 1.8, z + 0.2)


def ridge(k, x, z0, z1, h=2.5, w=3.5):
    """a sand drift across the flank (along z), h high, its two faces walkable"""
    k.wedge("rustdunes", 0, x - w, 0, z0, x, h, z1); k.wedge("rustdunes", 1, x, 0, z0, x + w, h, z1)


def ruin(k, x0, z0, x1, z1, h=3.4, door="+z"):
    """a ruined roadside building: walls with a door and a window, half its roof fallen in (the other half on beams)"""
    t = 0.4
    xm, zm = (x0 + x1) / 2, (z0 + z1) / 2
    for (zf, za, zb) in ((z0, z0, z0 + t), (z1, z1 - t, z1)):
        g = door == ("+z" if zf == z1 else "-z")
        k.box("bulletpockedwall", x0, 0, za, xm - 1.1, h, zb); k.box("bulletpockedwall", xm + 1.1, 0, za, x1, h * 0.7, zb)
        k.box("bulletpockedwall", xm - 1.1, 0 if not g else 2.6, za, xm + 1.1, h * (0.85 if g else 0.4), zb)
    k.box("bulletpockedwall", x0, 0, z0 + t, x0 + t, h, z1 - t)
    k.box("bulletpockedwall", x1 - t, 0, z0 + t, x1, 1.2, zm - 1); k.box("bulletpockedwall", x1 - t, 0, zm + 1, x1, h * 0.6, z1 - t)
    k.box("concreterooftiles", x0 - 0.2, h, z0 - 0.2, xm + 0.6, h + 0.3, z1 + 0.2)
    k.box("charredtimber", xm + 0.6, h - 0.3, z0 + t, xm + 3.0, h, z0 + 0.8); k.box("charredtimber", xm + 0.6, h - 0.3, z1 - 0.8, xm + 3.0, h, z1 - t)
    k.box("collapsedbrick", xm + 1, 0, zm - 1.6, x1 - 0.6, 0.9, zm + 1.2)


def tank_traps(k, x0, z0, nx, nz, step=4.0):
    """a field of hedgehogs: three crossed steel bars each (1.5 m tall: you jump over them, you hide behind a cluster)"""
    for i in range(nx):
        for j in range(nz):
            x, z = x0 + i * step + (j % 2) * step / 2, z0 + j * step
            k.box("heavyscalerust", x - 1.1, 0, z - 0.15, x + 1.1, 0.3, z + 0.15)
            k.box("heavyscalerust", x - 0.15, 0, z - 1.1, x + 0.15, 0.3, z + 1.1)
            k.box("heavyscalerust", x - 0.18, 0, z - 0.18, x + 0.18, 1.5, z + 0.18)


def flanks(k):
    """3-4 big set pieces per stage on the flanks (z +-20..55) and round each point, never in the road: wrecks,
    collapsed billboards, ruins, sand drifts (each crossing one of the long flank sight lines), tank traps, fires"""
    # A
    bus_on_side(k, -180, -40)
    ruin(k, -211, -34, -201, -26, door="+z")
    ridge(k, -160, -56, -30)
    billboard(k, -156, 46, "-z")
    k.box("scrapheap", -196, 0, 40, -190, 1.8, 46)
    # B
    ruin(k, -101, -38, -91, -30, door="+z")
    truck_on_side(k, -70, -42)
    k.box("emberbed", -67.6, 2.4, -42.8, -66.4, 2.6, -41.2); k.light(-67, 3.6, -42, 10, 0.9)
    ridge(k, -112, -56, -26)
    k.box("scrapheap", -110, 0, 26, -103, 2.0, 31); k.box("tyrestack", -62, 0, 44, -58, 1.6, 46.4); k.box("tyrestack", -57, 0, 44.5, -54.6, 1.0, 47)
    # C (round the town)
    for (x0, x1, z) in ((-34, -24, -46), (24, 34, -44)):
        k.box("bulletpockedwall", x0, 0, z - 0.3, x1, 2.8, z + 0.3); k.box("collapsedbrick", x0 + 2, 0, z + 0.3, x0 + 6, 0.8, z + 1.6)
    ridge(k, -64, -56, -36)
    # D
    truck_on_side(k, 60, 40, along="z")
    k.box("emberbed", 59.4, 2.4, 34, 60.6, 2.6, 35.4); k.light(60, 3.6, 34.6, 10, 0.9)
    ridge(k, 40, -56, -32)
    ruin(k, 98, -40, 107, -32, door="-z")
    billboard(k, 120, -50, "+z")
    ridge(k, 100, 26, 50)
    # on the points themselves (each had nothing in its box to hold behind): A's barrier blocks and a sentry box, C's
    # well in the square's middle and two market carts, E's pallets and a tug in the hangar's mouth
    k.box("precastconcrete", -166, 0, 2, -163, 1.5, 3.2); k.box("precastconcrete", -157, 0, -3.2, -154, 1.5, -2)
    k.box("sandbagwall", -161, 0, -6.5, -158.6, 1.2, -4.2)
    k.disc("sandstoneashlar", 0, 0, 2.4, 0, 0.9, n=8); k.disc("sandstoneashlar", 0, 0, 0.7, 0.9, 1.6, n=6)
    for dx in (-2.0, 1.7): k.box("charredtimber", dx, 0.9, -0.15, dx + 0.3, 3.2, 0.15)
    k.box("charredtimber", -2.0, 3.2, -0.2, 2.0, 3.45, 0.2)
    for (x, z) in ((-6, -5), (6, 5)):
        k.box("cratewood", x - 1.6, 0.4, z - 0.9, x + 1.6, 1.3, z + 0.9); k.box("engineblock", x - 0.4, 0, z - 1.0, x + 0.4, 0.8, z + 1.0)
    for (x, z) in ((175, -9), (175, 8.5), (178, -6.5)):
        k.box("largecratewood", x - 1.2, 0, z - 1.2, x + 1.2, 1.4, z + 1.2)
    k.box("largecratewood", 174.4, 1.4, -9.6, 175.6, 2.4, -8.4)
    k.box("powdercoatyellow", 179, 0.3, 6.5, 182.5, 1.6, 8.5); k.box("anodizedblack", 179.4, 0, 6.6, 182.1, 0.5, 8.4)
    for (x, z) in ((-7, 7), (7, -7)):              # (two more stalls in the square's corners)
        k.box("wideplanks", x - 1.5, 0, z - 1.0, x + 1.5, 1.0, z + 1.0); k.box("leantocanvas", x - 1.8, 2.4, z - 1.3, x + 1.8, 2.525, z + 1.3)
        for (dx, dz) in ((-1.4, -0.9), (1.2, 0.7)): k.box("wideplanks", x + dx, 1.0, z + dz, x + dx + 0.2, 2.4, z + dz + 0.2)
    # B: a car at the pumps, oil drums by the islands, the plant on the canopy's roof (its roof is half the point)
    k.box("powdercoatgreen", -86, 0.3, 17.1, -81.6, 1.0, 18.9); k.box("powdercoatgreen", -85, 1.0, 17.2, -82.6, 1.5, 18.8)
    for (x, z) in ((-75.5, 13.2), (-75.5, 14.8), (-74.2, 14.0)): k.disc("rust", x, z, 0.4, 0, 1.0, n=6)
    for (x0, z0, x1, z1, h) in ((-86, 11, -83, 13, 1.2), (-77, 23, -74, 25.5, 1.2), (-81.5, 16.5, -78.5, 19.5, 1.6)):
        k.box("galvanizedsteel", x0, 5.0, z0, x1, 5.0 + h, z1)
    k.box("powdercoatyellow", -89.5, 5.0, 8.4, -70.5, 5.9, 8.6)                         # (the fascia over the road side)
    # E
    tank_traps(k, 140, 32, 4, 4); tank_traps(k, 162, -54, 4, 3)
    k.box("bulletpockedwall", 144, 0, -46.3, 156, 3.0, -45.7)
    for x in (172, 180): k.disc("electroplatednickel", x, 44, 3, 0, 4.5, n=8)
    ridge(k, 196, 30, 54)
    ridge(k, 186, -56, -36)


def decorate(k):
    """sparing, against walls and off the lanes: fires in the burnt convoy, the station's sign, door frames and window
    shades on the town's houses, lamps in the hangar, runway markings on the apron"""
    for (x, z) in ((-190, -3.5), (-204, 3.5)):
        k.box("emberbed", x - 0.6, 1.6, z - 0.5, x + 0.6, 1.8, z + 0.5); k.light(x, 3, z, 10, 0.9)
    k.box("galvanizedsteel", -66.3, 0, 30.6, -65.7, 8, 31.2); k.box("powdercoatyellow", -68, 8, 30.7, -64, 10, 31.1)   # (the sign)
    for (x0, z0, x1, z1, h) in HOUSES:   # (dark doorways and window shades painted flush on the street faces)
        if (x0, z0) in HOLLOW: continue
        for z in (z0, z1):
            if abs(z) > 40 or abs(z) < 11: continue
            for x in range(int(x0) + 3, int(x1) - 2, 6):
                k.box("charredtimber", x, 0, z - 0.02 if z == z0 else z, x + 1.4, 2.4, z if z == z0 else z + 0.02)
                k.box("leantocanvas", x - 0.2, 2.5, z - 0.6 if z == z0 else z, x + 1.6, 2.6, z if z == z0 else z + 0.6)
    for x in range(176, 211, 8):
        k.box("lightblock", x - 0.6, 11.8, -0.3, x + 0.6, 12, 0.3); k.light(x, 11, 0, 14, 0.7)
    for z in (-34, 34):
        for x in range(140, 230, 12): k.box("roadmarkings", x, 0, z - 0.2, x + 6, 0.01, z + 0.2)
    for (x, z) in ((-8, -46), (8, -46)):   # (palms by the tower)
        k.box("palmtrunk", x - 0.3, 0, z - 0.3, x + 0.3, 7, z + 0.3)
        k.box("jungleleaves", x - 2.2, 7, z - 0.6, x + 2.2, 7.4, z + 0.6); k.box("jungleleaves", x - 0.6, 7, z - 2.2, x + 0.6, 7.4, z + 2.2)


def smoke(k):
    for (x, z0, z1) in ((-122, -20, 20), (40, -24, 18), (122, -18, 22)):
        k.box("smokewall", x - 1.5, 0, z0, x + 1.5, 7, z1)


def spawns(k):
    for st, ((ax, az), (dx, dz)) in SPAWNS.items():
        tx, tz = [(p[0] + p[2]) / 2 for p in (POINTS["ABCDE"[st]],)][0], [(p[1] + p[3]) / 2 for p in (POINTS["ABCDE"[st]],)][0]
        for (i, j) in ((-1.5, -1.5), (1.5, -1.5), (-1.5, 1.5), (1.5, 1.5)):
            # (facing the road, not the shelter's wall: they come out moving along it)
            k.spawn(f"att{st}", ax + i, 0, az + j, 0 if az > 0 else 180)
            k.spawn(f"def{st}", dx + i, 0, dz + j, 0 if dz > 0 else 180)
    for (i, j) in ((-1.5, -1.5), (1.5, -1.5), (-1.5, 1.5), (1.5, 1.5)):
        k.spawn("a", -214 + i, 0, 22 + j, yaw_to(-214, 22, 0, 0)); k.spawn("b", 222 + i, 0, j, yaw_to(222, 0, 0, 0))
    # FFA: 20, spread over all five stages, each by cover and facing along the road or into a building
    for (x, z, tx, tz) in ((-206, -22, -150, -22), (-172, 30, -120, 30), (-146, -30, -146, 10), (-124, 40, -80, 40),
                           (-104, -24, -60, -24), (-80, 45, -80, 10), (-48, -23, -48, 10), (-48, 22, -48, 0),
                           (-28, -6, 0, -6), (28, 40, 0, 40), (0, -38, 0, 0), (48, -23, 48, 10), (48, 23, 48, 0),
                           (66, 30, 100, 30), (104, -24, 104, 0), (120, 20, 160, 20), (150, -36, 190, -36),
                           (160, 40, 200, 40), (196, -6, 160, -6), (220, -20, 220, 20)):
        k.spawn("any", x, 0, z, yaw_to(x, z, tx, tz))


def shelters(k):
    """a sandbag wall beside the staged spawn groups that stand off the lanes (toward their point: no group sees an
    enemy group at the same stage); the groups on the road, by the town's gate, in the souk and at the airfield's gate
    have none (their walls closed those lanes)"""
    for (st, side) in ((0, 0), (0, 1), (1, 0), (2, 1), (3, 1), (4, 1)):
        x, z = SPAWNS[st][side]
        p = POINTS["ABCDE"[st]]
        sx = 1 if (p[0] + p[2]) / 2 > x else -1
        k.box("sandbagwall", x + sx * 4.6 - 0.5, 0, z - 6, x + sx * 4.6 + 0.5, 2.4, z + 6)


def build():
    m = M("Desert Convoy", "points", sun=(0.55, 0.8, 0.3))
    k = Kit(m)
    m.raw("palette base 0.72 0.60 0.44 accent 0.55 0.32 0.18 metal 0.32 0.30 0.28 glow 1.00 0.62 0.25 sky 0.72 0.56 0.38")
    build_ground(k)
    edges(k)
    checkpoint(k)
    fuel_station(k, m)
    town(k)
    airfield(k)
    smoke(k)
    cover(k)
    flanks(k)
    shelters(k)
    wadi_decor(k)
    decorate(k)
    for key in "ABCDE":
        x0, z0, x1, z1, y = POINTS[key]
        for (a, b, c, d) in ((x0, z0, x1, z0 + 0.3), (x0, z1 - 0.3, x1, z1), (x0, z0 + 0.3, x0 + 0.3, z1 - 0.3), (x1 - 0.3, z0 + 0.3, x1, z1 - 0.3)):
            k.box("glow", a, y + (0.02 if key == "B" else 0), b, c, y + 0.06, d)
        lo = WADI_Y if key == "D" else y                # (D: the deck and the riverbed under it)
        if key == "D":
            for (a, b, c, d) in ((x0, z0, x1, z0 + 0.3), (x0, z1 - 0.3, x1, z1)):
                k.box("glow", a, WADI_Y, b, c, WADI_Y + 0.06, d)
        m.raw(f"point {num(x0)} {num(lo)} {num(z0)} {num(x1)} {num(y + 6)} {num(z1)}")
    spawns(k)
    for (x, z) in ((-160, 12), (-80, 18), (0, 0), (82, 0), (190, 0), (-85, 37), (212, -34)):
        k.light(x, 8 if x != 190 else 11, z, 18, 0.6)
    m.raw("setting desert highway convoy")
    m.save("convoy", dress=False)
