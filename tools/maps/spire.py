"""SPIRE (1.19.6): instagib free for all (and 1v1, duel, team vs, tourney) on the open top floors of a skyscraper
going up above the clouds, 90 x 90 m, 44 m tall. North is -z.
  Five floor slabs at 0, 8, 16, 24 and 32 m, offset in a spiral round the core (south, east, north, west, then a
  smaller crown over the south), so each floor overhangs the clouds and sees the floors above and below it.
  The core (12 x 12 m, solid concrete) holds the stair: two flights a floor round its middle, doors west and east on
  every level (a small landing outside where the floor isn't). The walk route up.
  Hanging loads (formwork, rebar bundles, a pallet cage) at 12, 20 and 28 m in the open air between the floors:
  cover in the air and grapple anchors.
  The tower crane on the north floor (16 m): a ladder up its mast to the cab at 44 m and the jib out across the top.
  Cover on the floors: stacks of blocks, rebar bundles, column stubs: all solid (instagib: a thin panel is no
  cover). Off the floors' edges: the clouds (the void at -8)."""
import math
from .prefabs import M
from .m5kit import Kit, slab, walls, yaw_to

H = 45
FLOORS = [   # (x0, z0, x1, z1, y, material)
    (-42, -6, 30, 42, 0, "polishedconcrete"),
    (-6, -42, 42, 30, 8, "freshgreyconcrete"),
    (-30, -42, 42, 6, 16, "freshgreyconcrete"),
    (-42, -30, 6, 42, 24, "largeformatporcelain"),
    (-26, -6, 22, 30, 32, "polishedconcrete"),
]
C = 6.0                 # the core's half size
CT = 0.5                # its walls
CRANE = (14, -24)       # the crane's mast (on the north floor)
JIB = 44.0


def build():
    m = M("Spire", "ffa", sun=(-0.6, 0.35, -0.5))
    m.also = ["1v1", "duel", "vs", "tourney"]
    k = Kit(m)

    # ---- the floors (each less the core's footprint; the crane's mast through the 24 m floor if it's there)
    for (x0, z0, x1, z1, y, mat) in FLOORS:
        slab(k, mat, x0, z0, x1, z1, y - 0.6, y, holes=[(-C, -C, C, C)], tile=24)
        k.box("bridgegirder", x0, y - 1.4, z0, x1, y - 0.6, z0 + 0.6)       # the edge beams under the slabs
        k.box("bridgegirder", x0, y - 1.4, z1 - 0.6, x1, y - 0.6, z1)
    core(k)
    crane(k)
    loads(k)
    cover(k)
    decorate(k)
    edge(k)

    # ---- spawns: 1v1 / vs pairs on the 8 m floor (east) and the 24 m floor (west); free for all spread on all
    for (x, z) in ((34, -30), (36, -22), (34, 18), (36, 24)):
        k.spawn("a", x, 8, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((-34, -18), (-36, -10), (-34, 30), (-36, 36)):
        k.spawn("b", x, 24, z, yaw_to(x, z, 0, 0))
    for (x, y, z) in ((-34, 0, 36), (22, 0, 36), (-36, 0, 0), (36, 8, -36), (-22, 16, -36), (36, 16, 0),
                      (-36, 24, 18), (16, 32, 24)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, 0))

    m.raw("setting the open top floors of a skyscraper going up above the clouds at sunset")
    m.raw("palette base 0.7 0.7 0.72 accent 0.95 0.7 0.15 metal 0.35 0.36 0.4 glow 1 0.25 0.15 sky 0.98 0.62 0.4")
    m.save("spire", dress=False)


def core(k):
    """the core: walls with doors west and east on each level, and inside, each level's two flights (south, rising
    east, to the mid landing; north, rising west, to the next level), the level's floor between them"""
    tops = [f[4] for f in FLOORS]
    i0, i1 = -C + CT, C - CT
    for n, y in enumerate(tops):
        y1 = tops[n + 1] if n + 1 < len(tops) else y + 4.4
        walls(k, "castinplacewalls", -C, -C, C, C, y, y1, [("w", 0, 3, 3.2), ("e", 0, 3, 3.2)], t=CT)
        # the level's floor: the middle strip and the west landing (the slab under them down to the level below)
        if n > 0:
            k.box("castinplacewalls", i0, y - 0.4, -2.5, i1, y, 2.5)
            k.box("castinplacewalls", i0, y - 0.4, i0, -4, y, i1)
        else:
            k.box("castinplacewalls", i0, -0.6, i0, i1, 0, i1)
        if n + 1 < len(tops):
            mid = y + 4
            k.stairs("castinplacewalls", -4, 2.5, 2.4, i1, mid, "x", 1, base=y, floor=y)
            k.box("castinplacewalls", 2.4, mid - 0.4, i0, i1, mid, i1)                    # the mid landing (east)
            k.stairs("castinplacewalls", -4, i0, 2.4, -2.5, y1, "x", -1, base=mid, floor=mid - 0.4)
        # the landings outside the doors (where the floor doesn't reach)
        for s in (-1, 1):
            x = s * C
            if not any(f[0] <= x + s * 2 <= f[2] and f[1] <= 0 <= f[3] and f[4] == y for f in FLOORS):
                a, b = sorted((x, x + s * 4))
                k.box("bridgegirder", a, y - 0.6, -2.5, b, y, 2.5)
                k.box("galvanizedsteel", a, y, -2.5, b, y + 1.1, -2.4)
                k.box("galvanizedsteel", a, y, 2.4, b, y + 1.1, 2.5)
    # the roof (over the crown's level) and its rebar
    top = tops[-1] + 4.4
    k.box("castinplacewalls", -C, top, -C, C, top + 0.5, C)
    k.box("exposedrebar", -C, top + 0.5, -C, C, top + 1.3, -C + 0.3)
    for x in (-4, 4):
        k.light(x, tops[-1] + 3.6, 0, 8, 0.5)


def crane(k):
    """the tower crane: a lattice mast (a ladder up inside it) from the north floor to the cab at 44 m, the jib out
    east and west, the counterweight; warning lamps"""
    cx, cz = CRANE
    y0 = FLOORS[2][4]
    for (x, z) in ((cx - 1.2, cz - 1.2), (cx + 1.2, cz - 1.2), (cx - 1.2, cz + 1.2), (cx + 1.2, cz + 1.2)):
        k.box("powdercoatyellow", x - 0.15, y0, z - 0.15, x + 0.15, JIB - 0.4, z + 0.15)
    for y in range(int(y0) + 4, int(JIB), 4):
        k.box("powdercoatyellow", cx - 1.35, y, cz - 1.35, cx + 1.35, y + 0.15, cz - 1.05)
        k.box("powdercoatyellow", cx - 1.35, y, cz + 1.05, cx + 1.35, y + 0.15, cz + 1.35)
    k.box("powdercoatyellow", cx - 0.6, y0, cz - 1.05, cx + 0.6, JIB - 0.4, cz - 0.9)           # the ladder's backing
    k.ladder(cx, cz - 0.9, y0, JIB, "n", mat="galvanizedsteel")
    # the cab: a floor with the ladder's hole, walls (doors east and west onto the jib), a roof
    slab(k, "anodizedblack", cx - 2, cz - 2, cx + 2, cz + 2, JIB - 0.4, JIB, holes=[(cx - 0.7, cz - 0.9, cx + 0.7, cz + 0.2)])
    walls(k, "anodizedblack", cx - 2, cz - 2, cx + 2, cz + 2, JIB, JIB + 2.6, [("e", cz, 1.4, 2.2), ("w", cz, 1.4, 2.2)], t=0.3)
    k.box("anodizedblack", cx - 2, JIB + 2.6, cz - 2, cx + 2, JIB + 3.0, cz + 2)
    k.box("smokedglass", cx - 2.05, JIB + 1.0, cz - 1.6, cx - 1.7, JIB + 2.2, cz - 0.9)
    # the jib (east, over the open air) and the counter jib (west) with its weights; rails along the jib
    k.box("powdercoatyellow", cx + 2, JIB - 0.4, cz - 0.9, 44, JIB, cz + 0.9)
    k.box("powdercoatyellow", -14, JIB - 0.4, cz - 0.9, cx - 2, JIB, cz + 0.9)
    for z in (cz - 0.9, cz + 0.8):
        k.box("powdercoatyellow", cx + 2, JIB, z, 44, JIB + 1.0, z + 0.1)
    k.box("precastconcrete", -13, JIB, cz - 1.2, -8, JIB + 2.4, cz + 1.2)
    k.box("nograpple", -13, JIB + 2.4, cz - 1.2, -8, JIB + 2.5, cz + 1.2)
    k.box("galvanizedsteel", cx - 0.2, JIB + 3.0, cz - 0.2, cx + 0.2, JIB + 7.0, cz + 0.2)    # the tower top
    for x in (cx, 43.5, -13.5):
        k.box("glow", x - 0.2, JIB + (7.0 if x == cx else 1.0), cz - 0.2, x + 0.2, JIB + (7.4 if x == cx else 1.4), cz + 0.2)
    # the trolley and its hook load (formwork at 28 m)
    k.box("galvanizedsteel", 33, JIB - 0.9, cz - 0.8, 35, JIB - 0.4, cz + 0.8)
    k.box("galvanizedsteel", 33.95, 30.4, cz - 0.05, 34.05, JIB - 0.9, cz + 0.05)
    k.box("scaffoldboards", 31.5, 28, cz - 1.5, 36.5, 30.4, cz + 1.5)


def loads(k):
    """hanging loads in the open air between the floors (cables up to the floor above or the sky)"""
    for (x, y, z, sx, sz, mat) in ((-34, 12, -30, 4, 2.4, "bentrebarbundle"), (36, 20, 36, 3, 3, "palletstack"),
                                   (-20, 14, 34, 5, 2.4, "scaffoldboards"), (36, 28, 10, 2.4, 4.5, "bentrebarbundle"),
                                   (-36, 30, -36, 3, 3, "palletstack"), (0, 12, -36, 5, 2.4, "scaffoldboards")):
        k.box(mat, x - sx / 2, y, z - sz / 2, x + sx / 2, y + 2.2, z + sz / 2)
        over = [f[4] - 1.4 for f in FLOORS if f[4] > y and f[0] <= x <= f[2] and f[1] <= z <= f[3]]
        k.box("galvanizedsteel", x - 0.05, y + 2.2, z - 0.05, x + 0.05, min(over + [y + 14]), z + 0.05)


def cover(k):
    """on the floors: block stacks, rebar bundles and column stubs (solid: instagib), placed near the spawns and
    along the edges, the lanes between left open"""
    items = [  # (x, z, floor index, sx, sz, h, material)
        (-30, 34, 0, 3, 2, 2.0, "palletstack"), (16, 34, 0, 2, 4, 1.4, "bentrebarbundle"), (-30, 2, 0, 2.4, 2.4, 2.4, "precastconcrete"),
        (10, 16, 0, 1, 1, 7.4, "precastconcrete"), (-14, 16, 0, 1, 1, 7.4, "precastconcrete"),
        (30, -30, 1, 3, 2, 2.0, "palletstack"), (30, 22, 1, 2, 3, 1.4, "bentrebarbundle"), (18, 0, 1, 1, 1, 7.4, "precastconcrete"),
        (28, -4, 1, 2.4, 2.4, 2.4, "precastconcrete"),
        (-16, -30, 2, 3, 2, 2.0, "palletstack"), (30, -4, 2, 2, 3, 1.4, "bentrebarbundle"), (-16, -12, 2, 1, 1, 7.4, "precastconcrete"),
        (-28, -14, 3, 2, 3, 2.0, "palletstack"), (-28, 26, 3, 3, 2, 1.4, "bentrebarbundle"), (-14, 34, 3, 2.4, 2.4, 2.4, "precastconcrete"),
        (-14, 10, 3, 1, 1, 7.4, "precastconcrete"),
        (10, 20, 4, 3, 2, 2.0, "palletstack"), (-14, 20, 4, 2, 3, 1.4, "bentrebarbundle"),
    ]
    for (x, z, fi, sx, sz, h, mat) in items:
        y = FLOORS[fi][4]
        k.box(mat, x - sx / 2, y, z - sz / 2, x + sx / 2, y + h, z + sz / 2)
    for (x0, z0, x1, z1, y, _) in FLOORS:   # work lamps at the floors' corners
        k.light(x0 + 3, y + 3, z0 + 3, 12, 0.5); k.light(x1 - 3, y + 3, z1 - 3, 12, 0.5)


def decorate(k):
    """a little: the first curtain-wall panes going in (thick tinted glass: they stop shots), warning lamps on the
    core's roof"""
    for (x0, z0, x1, z1, y) in ((-24, -42, -16, -41.75, 16), (26, 41.75, 30, 42, 0), (41.75, -12, 42, -4, 8)):
        k.box("bluetintedglass", x0, y, z0, x1, y + 3.4, z1)
    top = FLOORS[-1][4] + 4.9
    for (x, z) in ((-5.5, -5.5), (5.5, 5.5)):
        k.box("glow", x - 0.2, top, z - 0.2, x + 0.2, top + 0.4, z + 0.2)


def edge(k):
    """the clouds below (the void at -8), clip round the edge and the lid at 89 m"""
    m = k.m
    m.raw("void -8")
    k.box("far", -400, -21, -400, 400, -20, 400)
    for (x0, z0, x1, z1) in ((-H - 1, -H - 1, H + 1, -H), (-H - 1, H, H + 1, H + 1), (-H - 1, -H, -H, H), (H, -H, H + 1, H)):
        k.box("clip", x0, -8, z0, x1, 89, z1)
    k.box("clip", -H, 89, -H, H, 90, H)
