"""DESERT CONVOY (1.19.6): multi point on a highway through a desert town to an airfield, under a sandstorm sky,
472 x 140 m, attacked west to east through five points about 80 m apart:
  A  the checkpoint (x -160): barriers across half the road, a guard hut of 1/8 m sheet (shoot-through: don't hold
     the point from inside it), concrete blocks, a guard tower (6 m, a ladder) as the defenders' perch, and the
     burnt-out convoy (two trucks, an armoured car) west of it on the road;
  B  the fuel station (x -80, south of the road): a forecourt under a canopy (its roof at 5 m, walkable: a ladder up
     a column, and a plank from the shop's roof, which has stairs), pump islands, the storage tanks behind;
  C  the market town (x 0): the street widens into a square between four flat-roofed mud-brick blocks; plank bridges
     join the roofs across the square's north and south ends (stairs up in two corners), a tower (16 m, a ladder
     inside) to the north, a covered souk lane to the south (1/8 m awnings and stall boards: shoot-through);
  D  the wadi bridge (x 82): the highway crosses a dry riverbed 6 m deep on a concrete bridge with a girder underside
     (hooks); a ford (ramps down and up) to the north is the second crossing;
  E  the airfield (x 180): the point is the hangar's mouth, a cargo plane inside (a ramp into its hold), the control
     tower (14 m, a ladder) and fuel bowsers on the apron.
Each stage's attackers spawn a little past the point they took (south of the road), its defenders between the next
point and the one after (north): see SPAWNS for the measured arrival times. Three sandstorm drifts (smoke, sight only)
lie on the open highway between the points. Dunes with clip above them close the long sides."""
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
          2: ((-52, 9), (64, -30)),
          3: ((14, 40), (140, -40)),
          4: ((128, 30), (222, 0))}
HOUSES = [(-36, -34, -16, -12, 5.0), (16, -34, 36, -12, 4.5), (-36, 12, -16, 34, 4.5), (16, 12, 36, 34, 5.0),
          (-54, -32, -42, -14, 4.0), (42, 14, 54, 32, 4.0), (-54, 14, -42, 30, 4.5), (42, -30, 54, -14, 4.5),
          (-40, 46, -10, 58, 4.0), (10, 46, 40, 58, 4.0)]
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


def town(k):
    """C: four mud-brick blocks round the square, smaller houses beyond, the tower, plank bridges, the souk"""
    for (x0, z0, x1, z1, h) in HOUSES:
        k.box("stucco", x0, 0, z0, x1, h, z1)
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
    # the tower north of the square: 6 x 6, its platform at 16 behind a parapet, a ladder inside
    tx, tz = 0, -44
    k.box("sandstoneashlar", tx - 3, 0, tz - 3, tx + 3, 16, tz - 2.5)
    k.box("sandstoneashlar", tx - 3, 0, tz - 2.5, tx - 2.5, 16, tz + 3)
    k.box("sandstoneashlar", tx + 2.5, 0, tz - 2.5, tx + 3, 16, tz + 3)
    k.box("sandstoneashlar", tx - 2.5, 2.8, tz + 2.5, tx + 2.5, 16, tz + 3)
    k.box("sandstoneashlar", tx - 2.5, 0, tz + 2.5, tx - 1.2, 2.8, tz + 3); k.box("sandstoneashlar", tx + 1.2, 0, tz + 2.5, tx + 2.5, 2.8, tz + 3)
    k.box("sandstoneashlar", tx - 3, 16, tz - 3, tx + 3, 19.5, tz - 2.5)                      # (the back wall, over the ladder)
    k.box("sandstoneashlar", tx - 2.5, 15.6, tz - 1.6, tx + 2.5, 16, tz + 2.5)
    k.box("sandstoneashlar", tx - 2.5, 15.6, tz - 2.5, tx + 0.4, 16, tz - 1.6)
    k.ladder(tx + 1.4, tz - 2.5, 0, 16, "n")
    for (a, b, c, d) in ((tx - 3, tz - 3, tx + 3, tz - 2.6), (tx - 3, tz + 2.6, tx + 3, tz + 3), (tx - 3, tz - 2.6, tx - 2.6, tz + 2.6), (tx + 2.6, tz - 2.6, tx + 3, tz + 2.6)):
        for (y0, y1) in ((16, 17.1),):
            k.box("sandstoneashlar", a, y0, b, c, y1, d)
    k.box("sandstoneashlar", tx - 3.3, 19.5, tz - 3.3, tx + 3.3, 20, tz + 3.3)
    for (dx, dz) in ((-3, -3), (2.6, -3), (-3, 2.6), (2.6, 2.6)):
        k.box("sandstoneashlar", tx + dx, 17.1, tz + dz, tx + dx + 0.4, 19.5, tz + dz + 0.4)
    # the souk: a covered lane along x south of the blocks (awnings 1/8 m, stall boards 1/8 m)
    k.box("leantocanvas", -40, 3.4, 37, 40, 3.525, 45)
    for x in range(-36, 37, 8):
        k.box("wideplanks", x - 0.1, 0, 37, x + 0.1, 3.4, 37.2); k.box("wideplanks", x - 0.1, 0, 44.8, x + 0.1, 3.4, 45)
        if x % 16 == 4:
            k.box("wideplanks", x + 1, 0, 44.875, x + 5, 1.1, 45)                 # (stall boards on the south side)
            k.box("woodencrate", x + 1.5, 0, 43.6, x + 2.7, 1.0, 44.8)


def wadi_decor(k):
    pass


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
    # the cargo plane: fuselage (a ramp up into its hold at the tail, facing west), wings to hook
    k.box("aircraftskin", 186, 1.2, -3, 208, 6, 3)
    k.ramp("steeldeckplate", 182, -2.2, 186, 2.2, 0, 1.2, "+x")
    k.box("aircraftskin", 194, 4.6, -20, 200, 5.2, 20)
    for (x, z) in ((190, -2.5), (190, 2.5), (204, 0)):
        k.box("anodizedblack", x - 0.5, 0, z - 0.5, x + 0.5, 1.2, z + 0.5)
    # the control tower (south of the apron): a 6 x 6 shaft, the cab at 14, a ladder inside, a door west
    tx, tz = 150, -46
    k.box("precastconcrete", tx - 3, 0, tz - 3, tx + 3, 14, tz - 2.6); k.box("precastconcrete", tx - 3, 0, tz + 2.6, tx + 3, 14, tz + 3)
    k.box("precastconcrete", tx + 2.6, 0, tz - 2.6, tx + 3, 14, tz + 2.6)
    k.box("precastconcrete", tx - 3, 0, tz - 2.6, tx - 2.6, 14, tz - 1.2); k.box("precastconcrete", tx - 3, 0, tz + 1.2, tx - 2.6, 14, tz + 2.6)
    k.box("precastconcrete", tx - 3, 2.8, tz - 1.2, tx - 2.6, 14, tz + 1.2)
    for (a, b, c, d) in minus((tx - 4, tz - 4, tx + 4, tz + 4), [(tx + 1.6, tz - 0.6, tx + 2.6, tz + 0.6)]):
        k.box("precastconcrete", a, 14, b, c, 14.4, d)
    k.ladder(tx + 2.6, tz, 0, 14.4, "e")
    for (a, b, c, d) in ((tx - 4, tz - 4, tx + 4, tz - 3.8), (tx - 4, tz + 3.8, tx + 4, tz + 4), (tx - 4, tz - 3.8, tx - 3.8, tz + 3.8), (tx + 3.8, tz - 3.8, tx + 4, tz + 3.8)):
        k.box("precastconcrete", a, 14.4, b, c, 15.4, d)
        k.box("glass", a, 15.4, b, c, 17, d)
    k.box("precastconcrete", tx - 4.3, 17, tz - 4.3, tx + 4.3, 17.4, tz + 4.3)
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


def decorate(k):
    """sparing, against walls and off the lanes: fires in the burnt convoy, the station's sign, door frames and window
    shades on the town's houses, lamps in the hangar, runway markings on the apron"""
    for (x, z) in ((-190, -3.5), (-204, 3.5)):
        k.box("lightblock", x - 0.6, 1.6, z - 0.5, x + 0.6, 2.0, z + 0.5); k.light(x, 3, z, 10, 0.9)
    k.box("galvanizedsteel", -66.3, 0, 30.6, -65.7, 8, 31.2); k.box("powdercoatyellow", -68, 8, 30.7, -64, 10, 31.1)   # (the sign)
    for (x0, z0, x1, z1, h) in HOUSES:   # (dark doorways and window shades painted flush on the street faces)
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
    for (x, z) in ((-200, -30), (-130, 30), (-60, -20), (-26, 0), (26, 4), (60, 30), (120, -30), (200, 34)):
        k.spawn("any", x, 0, z, yaw_to(x, z, 0, 0))


def shelters(k):
    """each staged spawn group behind a low wall toward its point (sandbags for the army, crates and a tarp)"""
    for st, ((ax, az), (dx, dz)) in SPAWNS.items():
        p = POINTS["ABCDE"[st]]
        for (x, z) in ((ax, az), (dx, dz)):
            tx = (p[0] + p[2]) / 2
            side = 1 if tx > x else -1
            k.box("sandbagwall", x + side * 4.6 - 0.5, 0, z - 6, x + side * 4.6 + 0.5, 2.4, z + 6)


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
    shelters(k)
    decorate(k)
    for key in "ABCDE":
        x0, z0, x1, z1, y = POINTS[key]
        for (a, b, c, d) in ((x0, z0, x1, z0 + 0.3), (x0, z1 - 0.3, x1, z1), (x0, z0 + 0.3, x0 + 0.3, z1 - 0.3), (x1 - 0.3, z0 + 0.3, x1, z1 - 0.3)):
            k.box("glow", a, y + (0.02 if key == "B" else 0), b, c, y + 0.06, d)
        m.raw(f"point {num(x0)} {num(y)} {num(z0)} {num(x1)} {num(y + 6)} {num(z1)}")
    spawns(k)
    for (x, z) in ((-160, 12), (-80, 18), (0, 0), (82, 0), (190, 0), (-85, 37), (150, -46)):
        k.light(x, 8 if x != 190 else 11, z, 18, 0.6)
    m.raw("setting desert highway convoy")
    m.save("convoy", dress=False)
