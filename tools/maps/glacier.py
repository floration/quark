"""GLACIER SUMMIT (1.19.6): king of the hill (and team vs, power, free for all) on a snowy peak, 150 x 130 m. North
is -z. A snowfield at 10 m round a peak whose summit plateau (40 x 40 m) stands at 28 m, its faces 31 degree snow
slopes (walkable), rock bands on them as cover; crevasses in the snowfield north of it (fall in: the void).
  The hill: the observatory on the summit, a 16-sided drum 20 m across with a stepped dome (a slit in it facing
  south), the telescope pier in the middle (cover), a gallery ring inside at 5 m (a stair up the north wall). The hill
  box takes in the floor, the gallery and the terrace in front of its west and east doors.
  Team A: the cable car top station on the west shelf (16 m), the cabin parked at the edge.
  Team B: the climbers' hut on the east shelf (14 m), plank walls (shoot-through) and a stone back wall, the snow
  cat and the ski rack beside it.
  A's hidden way: the service tunnel, from a portal in the west slope up a ramp inside the peak to a hatch (a 4 m
  ladder) beside the telescope pier: unseen, but it ends in a choke in the middle of the room.
  B's high way: the radio mast on the east slope (a ladder to its 33 m platform) and its catwalk bridge into the
  gallery: seen, but it gives height.
  The north face: an ice chute down its middle (slippery: the fast way down) between snow, crevasses (void) on the
  snowfield below it, off the main routes. The south: the long way round between the two shelves.
Ways onto the hill: the two doors, the tunnel's hatch, the mast bridge, the dome's slit (grapple from the terrace
masts), the dome's own steps from the terrace."""
import math
from .prefabs import M
from .m5kit import Kit, fill, slab, walls, rock, yaw_to

HX, HZ = 75, 65
SNOW = 10.0            # the snowfield
SUM = 28.0             # the summit plateau
P = 20.0               # the plateau's half size
SL = 0.6               # the faces' slope (rise per metre)
FOOT = P + (SUM - SNOW) / SL   # 50: where the faces meet the snowfield
A_SH, B_SH = 16.0, 14.0
A_X = -(P + (SUM - A_SH) / SL)  # -40: A's shelf meets the west face
B_X = P + (SUM - B_SH) / SL     # 43.3: B's shelf meets the east face
R = 10.0               # the observatory's radius
GAL = SUM + 5.0        # its gallery
TUN = 2.0              # the tunnel's half width


def surface(x, z):     # the peak's snow surface height over the snowfield
    d = max(abs(x) - P, abs(z) - P, 0)
    return max(SNOW, SUM - SL * d)


def ground(x, z):     # the walk height at (x, z) (the shelves, the peak, the snowfield)
    if abs(z) <= 30 and x <= A_X: return A_SH
    if abs(z) <= 30 and x >= B_X: return B_SH
    return surface(x, z)


def build():
    m = M("Glacier Summit", "koth", sun=(0.25, 1.0, 0.45))
    m.also = ["vs", "power"]
    k = Kit(m)

    # ---- the snowfield (holes: the crevasses north of the peak, ice walls down into the dark)
    crev = [(-34, -62, -14, -56), (14, -60, 30, -55)]
    slab(k, "windpackedsnow", -HX, -HZ, HX, HZ, SNOW - 4, SNOW, holes=crev, tile=30)
    for (x0, z0, x1, z1) in crev:
        for (a, b, c, d) in ((x0, z0 - 0.4, x1, z0), (x0, z1, x1, z1 + 0.4), (x0 - 0.4, z0, x0, z1), (x1, z0, x1 + 0.4, z1)):
            k.box("glacierice", a, SNOW - 16, b, c, SNOW - 4, d)
    m.raw("void -8")

    # ---- the peak: the plateau (rock, the service tunnel in it), four faces and four corners (31 degrees)
    fill(k, "graniterock", (-P, SNOW - 4, -P, P, SUM - 0.4, P), [(-P, 19.9, -TUN, -4, SUM - 0.4, TUN)])
    slab(k, "windpackedsnow", -P, -P, P, P, SUM - 0.4, SUM, holes=[(-R, -R, R, R)], tile=20)
    slab(k, "concrete", -R, -R, R, R, SUM - 0.4, SUM, holes=[(-6, -1.2, -4, 1.2)])          # the observatory's floor
    # the west face, in strips round the tunnel: its floor a ramp from the portal (x -34, 16 m) to 20 m at the
    # plateau's edge, its roof rock under the snow; inside, a ramp on to 24 m and a ladder up to the hatch
    for (z0, z1) in ((-P, -TUN), (TUN, P)):
        k.box("graniterock", -FOOT, SNOW - 4, z0, -P, A_SH, z1)
        k.ramp("windpackedsnow", A_X, z0, -P, z1, A_SH, SUM, "+x")
    k.box("graniterock", -FOOT, SNOW - 4, -TUN, -P, A_SH, TUN)
    k.ramp("concrete", -34, -TUN, -P, TUN, A_SH, 20, "+x")
    s_top = (-SL, 1, 0, SUM + SL * P)                         # y <= 28 + 0.6 (x + 20): the snow
    r_bot = (4 / 14, -1, 0, -(19.5 + 4 / 14 * 34))           # y >= 19.5 + (x + 34) 4/14: the tunnel's roof
    k.cut("graniterock", (-34, 19.5, -TUN, -P, SUM, TUN), [s_top, r_bot])
    k.ramp("concrete", -P, -TUN, -6, TUN, 20, 24, "+x")
    k.box("concrete", -6, 19.9, -TUN, -4, 24, TUN)
    k.ladder(-4, 0, 24, SUM, "e", mat="galvanizedsteel")
    k.light(-27, 20.5, 0, 8, 0.6); k.light(-8, 26, 0, 8, 0.6)
    # the other faces (the north one: an ice chute down its middle)
    k.box("graniterock", P, SNOW - 4, -P, FOOT, SNOW, P)
    k.ramp("windpackedsnow", P, -P, FOOT, P, SNOW, SUM, "-x")
    k.ramp("windpackedsnow", -P, P, P, FOOT, SNOW, SUM, "-z")
    for (x0, x1, mat) in ((-P, -5, "windpackedsnow"), (-5, 5, "ice"), (5, P, "windpackedsnow")):
        k.ramp(mat, x0, -FOOT, x1, -P, SNOW, SUM, "+z")
    # the corners: pyramids, the apex over the plateau's corner
    for (x0, x1, z0, z1, d) in ((-FOOT, -P, -FOOT, -P, 2), (P, FOOT, -FOOT, -P, 3), (P, FOOT, P, FOOT, 0), (-FOOT, -P, P, FOOT, 1)):
        k.wedge("windpackedsnow", d, x0, SNOW, z0, x1, SUM, z1, kind="corner")
    # the shelves: A's (west, 16 m) and B's (east, 14 m), a ramp down off each one's north and south ends
    k.box("windscouredrock", -HX, SNOW - 4, -30, A_X, A_SH - 0.5, 30)
    k.box("windpackedsnow", -HX, A_SH - 0.5, -30, A_X, A_SH, 30)
    k.box("windscouredrock", B_X, SNOW - 4, -30, HX, B_SH - 0.5, 30)
    k.box("windpackedsnow", B_X, B_SH - 0.5, -30, HX, B_SH, 30)
    for (x0, x1, y) in ((-70, -52, A_SH), (52, 70, B_SH)):
        L = (y - SNOW) / SL
        k.ramp("windpackedsnow", x0, 30, x1, 30 + L, SNOW, y, "-z")
        k.ramp("windpackedsnow", x0, -30 - L, x1, -30, SNOW, y, "+z")

    observatory(k)
    station(k)
    hut(k)
    mast(k)
    cover(k)
    decorate(k)
    edge(k)

    # ---- spawns: A in the station, B in the hut, free for all round the peak
    for (x, z) in ((-67, -4), (-63, -4), (-67, 3), (-63, 3)):
        k.spawn("a", x, A_SH, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((64.2, -3), (67.6, -3), (64.2, 2.5), (67.6, 2.5)):
        k.spawn("b", x, B_SH, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((-55, 22), (55, -22), (-40, 56), (35, 56), (-56, -56), (56, 50), (0, -60), (-58, -44)):
        k.spawn("any", x, ground(x, z), z, yaw_to(x, z, 0, 0))
    m.raw(f"hill -13 {SUM:g} -11 13 {SUM + 7:g} 11")

    m.raw("setting an observatory on a snowy peak, a cable car station and a climbers' hut")
    m.raw("palette base 0.8 0.82 0.86 accent 0.2 0.35 0.55 metal 0.3 0.32 0.36 glow 0.6 0.9 1 sky 0.62 0.75 0.9")
    m.save("glacier", dress=False)


def observatory(k):
    """the drum (16 sides, doors west and east, the bridge's opening east over the door), the stepped dome with its
    slit (south), the pier, the gallery and its stair, lamps"""
    y = SUM
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y, y + 4.2, n=16, a0=-11.25, skip=(0, 8))
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y + 4.2, y + 5.0, n=16, a0=-11.25, skip=(0,))
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y + 5.0, y + 7.6, n=16, a0=-11.25, skip=(0,))
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y + 7.6, y + 8.4, n=16, a0=-11.25)
    # the dome: three steps and a cap, the slit (segment 4: south) left open in the steps
    for (r0, r1, y0, y1) in ((R - 2.2, R + 0.3, 8.4, 9.6), (R - 4.2, R - 2.0, 9.6, 10.8), (R - 6.4, R - 4.0, 10.8, 12.0)):
        k.ring("standingseamroof", 0, 0, r0, r1, y + y0, y + y1, n=16, a0=-11.25, skip=(4,))
    k.disc("standingseamroof", 0, 0, R - 6.2, y + 12.0, y + 12.8, n=8)
    # the gallery (a ring walk at 5 m) and its stair, a chord inside the north wall, and the landing onto it
    k.ring("aluminiumtreadplate", 0, 0, R - 2.2, R - 0.6, GAL - 0.4, GAL, n=16, a0=-11.25)
    k.stairs("aluminiumtreadplate", -3.5, -6.0, 4.5, -4.2, GAL, "x", 1, base=y, solid=False)
    k.box("aluminiumtreadplate", 4.5, GAL - 0.4, -6.4, 6.6, GAL, -4.2)
    # the telescope pier (cover south of the middle) and its tube, aimed at the slit
    k.disc("whiteconcrete", 0, 4, 1.5, y, y + 2.4, n=8)
    k.box("chrome", -0.45, y + 2.4, 2.0, 0.45, y + 3.2, 7.0)
    # the hatch (the tunnel's ladder comes up through it): a rail on its west side
    k.box("galvanizedsteel", -6.2, y, -1.2, -6.0, y + 1.0, 1.2)
    for (x, z) in ((5, 5), (-5, -5), (5, -5), (-5, 5)):
        k.box("lightblock", x - 0.3, y + 7.8, z - 0.3, x + 0.3, y + 8.2, z + 0.3)
        k.light(x, y + 7.2, z, 10, 0.7)
    # the terrace masts (hooks for the slit)
    for (x, z) in ((-15, 15), (15, 15)):
        k.box("galvanizedsteel", x - 0.25, y, z - 0.25, x + 0.25, y + 11, z + 0.25)
        k.box("galvanizedsteel", x - 0.8, y + 10.6, z - 0.15, x + 0.8, y + 11, z + 0.15)


def station(k):
    """A: the cable car top station (concrete, the cabin parked in its west door); the spawns inside"""
    y = A_SH
    walls(k, "precastconcrete", -72, -9, -57, 8, y, y + 6, [("e", -0.5, 4, 4.2), ("n", -61, 3.2, 4.2), ("s", -61, 3.2, 4.2), ("w", -1, 4.8, 4.6)], t=0.5)
    k.box("precastconcrete", -72.4, y + 6, -9.4, -56.6, y + 6.6, 8.4)
    k.box("powdercoatyellow", -74.6, y + 0.8, -3.2, -71.5, y + 4.4, 1.2)
    k.box("dustyglass", -74.65, y + 2.6, -2.8, -71.5, y + 3.8, 0.8)
    k.box("galvanizedsteel", -73.4, y + 4.4, -1.3, -72.6, y + 7.4, -0.7)
    k.box("galvanizedsteel", -75, y + 7.2, -1.1, -66, y + 7.4, -0.9)
    k.box("lightblock", -66, y + 5.6, -1, -65, y + 5.9, 0)
    k.light(-65.5, y + 5, -0.5, 12, 0.7)


def hut(k):
    """B: the climbers' hut (plank walls: shoot-through, a stone back wall behind the spawns), the snow cat, the skis"""
    y = B_SH
    x0, z0, x1, z1 = 58, -7, 70, 6
    for (a, b, c, d) in ((x0, z0, x1, z0 + 0.12), (x0, z1 - 0.12, x1, z1)):          # north and south: planks (1/8 m)
        k.box("largefrostedplanks", a, y, b, c, y + 4.2, d)
    k.box("largefrostedplanks", x0, y, z0 + 0.12, x0 + 0.12, y + 4.2, -2)              # west: planks, the door
    k.box("largefrostedplanks", x0, y, 2, x0 + 0.12, y + 4.2, z1 - 0.12)
    k.box("largefrostedplanks", x0, y + 3.4, -2, x0 + 0.12, y + 4.2, 2)
    k.box("riverstonewall", x1 - 0.6, y, z0, x1, y + 4.2, z1)                          # east: stone
    k.wedge("roofshingles", 2, x0 - 0.5, y + 4.2, z0 - 0.5, x1 + 0.5, y + 6.2, (z0 + z1) / 2)
    k.wedge("roofshingles", 3, x0 - 0.5, y + 4.2, (z0 + z1) / 2, x1 + 0.5, y + 6.2, z1 + 0.5)
    k.box("lightblock", 64.2, y + 3.8, -0.8, 64.8, y + 4.1, -0.2)
    k.light(64.5, y + 3.3, -0.5, 10, 0.7)
    # the snow cat (cover south of the hut) and the ski rack (north)
    k.box("powdercoatyellow", 52, y, 9, 57, y + 2.0, 12)
    k.box("dustyglass", 52.5, y + 2.0, 9.4, 55, y + 2.9, 11.6)
    k.box("anodizedblack", 51.6, y, 8.6, 57.4, y + 0.8, 12.4)
    k.box("frostedplanks", 59, y, -10.5, 66, y + 1.6, -10)


def mast(k):
    """B's high way: the radio mast on the east face (a ladder up its west side to the 33 m platform) and the catwalk
    bridge from it into the dome's gallery"""
    mx = 30.0
    base = surface(mx + 1, 0) - 0.6
    for (x, z) in ((mx - 1, -1), (mx + 1, -1), (mx - 1, 1), (mx + 1, 1)):
        k.box("galvanizedsteel", x - 0.2, base, z - 0.2, x + 0.2, GAL + 9, z + 0.2)
    k.box("galvanizedsteel", mx - 1.2, base, 1.0, mx + 1.2, GAL - 0.4, 1.2)            # the ladder's backing (south)
    foot = surface(mx - 1.2, 0)                                                       # a concrete footing at its foot
    k.box("concrete", mx - 1.2, base, 1.2, mx + 1.2, foot, 3.0)
    k.ladder(mx, 1.2, foot, GAL, "n", mat="galvanizedsteel")
    k.box("aluminiumtreadplate", mx - 1.6, GAL - 0.4, -1.6, mx + 1.6, GAL, 1.0)        # the platform
    k.box("aluminiumtreadplate", R - 0.6, GAL - 0.4, -1.0, mx - 1.0, GAL, 1.0)          # the bridge
    for z in (-1.1, 1.0):
        k.box("galvanizedsteel", R + 0.2, GAL, z, mx - 1.0, GAL + 1.1, z + 0.1)
    k.box("anodizedblack", mx - 1.6, GAL, -1.9, mx + 1.6, GAL + 2.2, -1.6)              # the radio cabinet, and a rail
    k.box("galvanizedsteel", mx + 1.6, GAL, -1.7, mx + 1.7, GAL + 1.1, 1.0)
    k.box("glow", mx - 0.3, GAL + 9, -0.3, mx + 0.3, GAL + 9.5, 0.3)


def cover(k):
    """rock bands on the faces (cover on the way up), a few boulders on the shelves and the snowfield"""
    bands = [(-31, -12, 2.6), (-30, 11, 2.4), (31, 12, 2.4), (33, -10, 2.6), (-10, 31, 2.4), (12, 33, 2.2), (-12, -31, 2.2), (14, -30, 2.4)]
    for i, (x, z, r) in enumerate(bands):
        rock(k, "graniterock", x, z, r, surface(x, z) - 1.5, 2.8, seed=i)
    for i, (x, z, r) in enumerate(((-50, 18, 2.0), (50, -18, 2.0), (-30, 54, 2.6), (32, 54, 2.4), (-45, -52, 2.4), (44, 48, 2.2))):
        rock(k, "glacierpolishedgranite", x, z, r, ground(x, z) - 0.6, 2.4, seed=20 + i)


def decorate(k):
    """a little, off the lanes: ice patches on the south snowfield, marker poles round the crevasses, the station's
    bullwheel overhead, the weather instruments on the summit terrace"""
    for (x0, z0, x1, z1) in ((-22, 54, -10, 60), (8, 52, 22, 58)):
        k.box("ice", x0, SNOW, z0, x1, SNOW + 0.04, z1)
    for (x0, z0, x1, z1) in ((-34, -62, -14, -56), (14, -60, 30, -55)):
        for (x, z) in ((x0 - 1.2, z1 + 1.2), (x1 + 1.2, z1 + 1.2), ((x0 + x1) / 2, z1 + 1.4)):
            k.box("powdercoatyellow", x - 0.08, SNOW, z - 0.08, x + 0.08, SNOW + 1.6, z + 0.08)
    k.disc("galvanizedsteel", -68, -0.5, 2.2, A_SH + 4.8, A_SH + 5.2, n=8)
    k.box("anodizedblack", -68.2, A_SH + 5.2, -0.7, -67.8, A_SH + 6, -0.3)
    for (x, z) in ((-15, -15), (15, -15)):
        k.box("whiteconcrete", x - 0.5, SUM, z - 0.5, x + 0.5, SUM + 1.1, z + 0.5)
        k.box("galvanizedsteel", x - 0.06, SUM + 1.1, z - 0.06, x + 0.06, SUM + 2.6, z + 0.06)
        k.box("galvanizedsteel", x - 0.5, SUM + 2.5, z - 0.04, x + 0.5, SUM + 2.6, z + 0.04)


def edge(k):
    """the cornice round the edge (a low snow lip), clip over it up to 90 m (the dome's top is 40.8 m)"""
    for (x0, z0, x1, z1) in ((-HX - 1, -HZ - 1, HX + 1, -HZ), (-HX - 1, HZ, HX + 1, HZ + 1), (-HX - 1, -HZ, -HX, HZ), (HX, -HZ, HX + 1, HZ)):
        k.box("nograpple", x0, SNOW - 4, z0, x1, SNOW + 1.2, z1)
        k.box("clip", x0, SNOW + 1.2, z0, x1, 90, z1)
    k.box("clip", -HX, 90, -HZ, HX, 91, HZ)
