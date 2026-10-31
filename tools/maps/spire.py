"""SPIRE (1.19.6): instagib free for all (and 1v1, duel, team vs, tourney) on the open top floors of a skyscraper
going up above the clouds, 84 x 84 m, 44 m tall. North is -z.
  Five floor slabs at 0, 8, 16, 24 and 32 m, offset in a spiral round the core (south, east, north, west, then a
  smaller crown over the south), so each floor overhangs the clouds and sees the floors above and below it.
  The structure is the cover: a 12 m grid of concrete columns runs floor to floor wherever two floors overlap (and
  down into the clouds under the slabs with nothing below; rebar starts on the top of the columns that end), and
  steel mullions every 4 m stand along the slab edges that have a storey above them, a few panes of thick glass in.
  The core (12 x 12 m) is solid: the lift shaft, a wall in the middle of every floor.
  The walk routes are outside, two straight stairs between each pair of floors at different corners (scaffold
  stair towers), so every floor is a loop: up one stair, round the floor, up or off the other; dropping is free.
  Hanging loads (formwork, rebar bundles, pallets) at 12-37 m in the open air: cover in the air and grapple anchors.
  The tower crane on the north floor (16 m): a ladder up its mast (plated on two sides, a rest deck with a solid
  parapet at 30 m) to the cab at 44 m and the jib out east.
  Each floor has three or four clusters of construction cover (formwork stacks, pallets, a mixer, block piles); the
  floor at 24 m is the finished one (partition stubs, a glass meeting room). Off the floors' edges: the clouds (the
  void at -8).
  1v1, duel and vs start both sides on the 16 m floor, at its east and west ends (the same height; the floor is
  60 m long, the crane and a site hut between them).
(1.19.6 round 3, q: "spire is a good idea just needs major refining": floors 72 x 48 to 60 x 42 m, the core's slow
stair replaced by eight outside flights, the column grid, mullions and cover clusters, 1v1 spawns level, the crane's
rest deck and plating, lamps in every covered bay.)"""
from .prefabs import M
from .m5kit import Kit, slab, walls, yaw_to

H = 42
FLOORS = [   # (x0, z0, x1, z1, y, material)
    (-36, -6, 24, 36, 0, "polishedconcrete"),
    (-6, -36, 36, 24, 8, "freshgreyconcrete"),
    (-24, -36, 36, 6, 16, "freshgreyconcrete"),
    (-36, -24, 6, 36, 24, "largeformatporcelain"),
    (-22, -6, 18, 26, 32, "polishedconcrete"),
]
C = 6.0                 # the core's half size
TOP = 36.4              # the core's roof
CRANE = (6, -31)        # the crane's mast (on the north floor, half way between its ends)
JIB = 44.0
# the outside stairs: (x0, z0, x1, z1, base, top, axis, rising): each from a floor's edge region up to the next
# floor's edge
STAIRS = [
    (-22, 10, -6, 14, 0, 8, "x", 1),       # 0 -> 8: up to the east floor's west edge
    (2, 24, 18, 28, 0, 8, "x", 1),         # 0 -> 8: along its south edge (a landing at the top)
    (26, 6, 30, 22, 8, 16, "z", -1),       # 8 -> 16: up to the north floor's south edge (east)
    (8, 6, 12, 22, 8, 16, "z", -1),        # 8 -> 16: (west of it)
    (6, -10, 22, -6, 16, 24, "x", -1),     # 16 -> 24: up to the west floor's east edge (A's side) ...
    (-22, -28, -6, -24, 16, 24, "x", 1),   # 16 -> 24: along its north edge (B's side; a landing at the top)
    (-12, -10, 4, -6, 24, 32, "x", -1),    # 24 -> 32: ... and straight on up along the crown's north edge
    (-34, 14, -22, 18, 24, 32, "x", 1),    # 24 -> 32: up to its west edge
]
LANDINGS = [(18, 24, 22, 28, 8, 1), (-6, -28, -2, -24, 24, 1), (-16, -10, -12, -6, 32, 0)]   # (.., rail at x1 or x0)   # (x0, z0, x1, z1, y): the tops of the flights that run along an edge
GRID = (-30, -18, -6, 6, 18, 30)


def inside(f, x, z, m=0.0):
    return f[0] - m <= x <= f[2] + m and f[1] - m <= z <= f[3] + m


def in_core(x, z, m=0.0):
    return abs(x) < C + m and abs(z) < C + m


def build():
    m = M("Spire", "ffa", sun=(-0.6, 0.35, -0.5))
    m.also = ["1v1", "duel", "vs", "tourney"]
    k = Kit(m)
    keep = keepouts()

    # ---- the floors (each less the core's footprint), the edge beams under them
    for (x0, z0, x1, z1, y, mat) in FLOORS:
        slab(k, mat, x0, z0, x1, z1, y - 0.6, y, holes=[(-C, -C, C, C)], tile=24)
        k.box("bridgegirder", x0, y - 1.4, z0, x1, y - 0.6, z0 + 0.6)
        k.box("bridgegirder", x0, y - 1.4, z1 - 0.6, x1, y - 0.6, z1)
    core(k)
    stairs(k)
    columns(k, keep)
    mullions(k, keep)
    crane(k)
    loads(k)
    cover(k)
    shear_walls(k)
    office(k)
    lamps(k)
    edge(k)

    # ---- spawns: 1v1 / vs at the two ends of the 16 m floor; free for all spread over every floor
    for (x, z) in ((32, -32), (33, -23), (31, -12), (33, -2)):
        k.spawn("a", x, 16, z, yaw_to(x, z, 6, -16))
    for (x, z) in ((-20, -33), (-21, -20), (-19, -12), (-21, -2)):
        k.spawn("b", x, 16, z, yaw_to(x, z, 6, -16))
    for (x, y, z) in ((-30, 0, 31), (20, 0, 16), (31, 8, -30), (33, 8, 16), (-12, 16, 2), (18, 16, -1),
                      (-26, 24, -14), (-4, 32, 20)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, 0))

    m.raw("setting the open top floors of a skyscraper going up above the clouds at sunset")
    m.raw("palette base 0.7 0.7 0.72 accent 0.95 0.7 0.15 metal 0.35 0.36 0.4 glow 1 0.25 0.15 sky 0.98 0.62 0.4")
    m.save("spire", dress=False)


def keepouts():
    """(x0, z0, x1, z1, y0, y1) volumes no column or mullion may stand in: the stairs with their run-ups, the crane,
    the hut"""
    out = []
    for (x0, z0, x1, z1, b, t, ax, r) in STAIRS:
        if ax == "x": out.append((x0 - 3, z0 - 1.5, x1 + 3, z1 + 1.5, b, t + 3))
        else: out.append((x0 - 1.5, z0 - 3, x1 + 1.5, z1 + 3, b, t + 3))
    for (x0, z0, x1, z1, y, _) in LANDINGS: out.append((x0 - 1.5, z0 - 1.5, x1 + 1.5, z1 + 1.5, y - 8, y + 3))
    cx, cz = CRANE
    out.append((cx - 4, cz - 4, cx + 4, cz + 4, 16, 50))
    out.append((-1, -25, 11, -16, 16, 20))
    return out


def blocked(keep, x0, z0, x1, z1, y0, y1):
    return any(a < x1 and c > x0 and b < z1 and d > z0 and e < y1 and f > y0 for (a, b, c, d, e, f) in keep)


def core(k):
    """the core: solid from the clouds to its roof, lift doors on its faces where a floor meets it, rebar on the roof"""
    k.box("castinplacewalls", -C, -20, -C, C, TOP, C)
    k.box("castinplacewalls", -C, TOP, -C, C, TOP + 0.5, C)
    k.box("exposedrebar", -C, TOP + 0.5, -C, C, TOP + 1.3, -C + 0.3)
    for (x0, z0, x1, z1, y, _) in FLOORS:
        for (sx, sz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if not inside((x0, z0, x1, z1), sx * (C + 1), sz * (C + 1)): continue
            if sx:
                a, b = sorted((sx * C, sx * (C + 0.15))); g0, g1 = sorted((sx * C, sx * (C + 0.2)))
                k.box("steeldoorskin", a, y, -1.4, b, y + 3, 1.4); k.box("glow", g0, y + 3.2, -0.3, g1, y + 3.4, 0.3)
            else:
                a, b = sorted((sz * C, sz * (C + 0.15))); g0, g1 = sorted((sz * C, sz * (C + 0.2)))
                k.box("steeldoorskin", -1.4, y, a, 1.4, y + 3, b); k.box("glow", -0.3, y + 3.2, g0, 0.3, y + 3.4, g1)
    for (x, z) in ((-5.5, -5.5), (5.5, 5.5)):
        k.box("glow", x - 0.2, TOP + 0.5, z - 0.2, x + 0.2, TOP + 0.9, z + 0.2)


def stairs(k):
    """the outside flights: steel deck treads, a scaffold post each side of the top, warning paint on the first step"""
    for (x0, z0, x1, z1, y, r) in LANDINGS:
        k.box("steeldeckplate", x0, y - 0.3, z0, x1, y, z1)
        xr = x1 - 0.1 if r else x0
        k.box("galvanizedsteel", xr, y, z0, xr + 0.1, y + 1.1, z1)   # (a rail at the far end)
    for (x0, z0, x1, z1, b, t, ax, r) in STAIRS:
        k.stairs("steeldeckplate", x0, z0, x1, z1, t, ax, r, base=b, floor=b)
        if ax == "x":
            xe = (x1 - 0.3) if r > 0 else (x0 + 0.3)
            for z in (z0 - 0.15, z1 + 0.15):
                if any(f[4] == t and inside(f, xe, z) for f in FLOORS): continue   # (not where you step off sideways)
                k.box("galvanizedsteel", xe - 0.075, b, z - 0.075, xe + 0.075, t + 1.1, z + 0.075)
            xs = x0 if r > 0 else x1 - 0.3
            k.box("hazard", xs, b, z0, xs + 0.3, b + 0.02, z1)
        else:
            ze = (z1 - 0.3) if r > 0 else (z0 + 0.3)
            for x in (x0 - 0.15, x1 + 0.15):
                if any(f[4] == t and inside(f, x, ze) for f in FLOORS): continue
                k.box("galvanizedsteel", x - 0.075, b, ze - 0.075, x + 0.075, t + 1.1, ze + 0.075)
            zs = z0 if r > 0 else z1 - 0.3
            k.box("hazard", x0, b, zs, x1, b + 0.02, zs + 0.3)


def columns(k, keep):
    """0.8 m columns on the 12 m grid: between every two floors that both cover a grid point, down into the clouds
    under the lowest, rebar starts on the top floor's"""
    for x in GRID:
        for z in GRID:
            if in_core(x, z, 1.0): continue
            fs = sorted([f for f in FLOORS if inside(f, x, z)], key=lambda f: f[4])
            if not fs: continue
            x0, x1, z0, z1 = x - 0.4, x + 0.4, z - 0.4, z + 0.4
            k.box("precastconcrete", x0, -20, z0, x1, fs[0][4] - 0.6, z1)
            for lo, hi in zip(fs, fs[1:]):
                if blocked(keep, x0 - 0.5, z0 - 0.5, x1 + 0.5, z1 + 0.5, lo[4], hi[4]): continue
                k.box("precastconcrete", x0, lo[4], z0, x1, hi[4] - 0.6, z1)
            top = fs[-1]
            if not blocked(keep, x0, z0, x1, z1, top[4], top[4] + 1):
                k.box("exposedrebar", x0, top[4], z0, x1, top[4] + 0.4, z1)


def frange(a, b, s):
    out, v = [], a
    while v <= b + 1e-6: out.append(v); v += s
    return out


def mullions(k, keep):
    """steel mullions every 4 m along each slab edge that has a storey (8 m) over it, the head under that slab;
    a pane of thick glass in a few bays (solid: it stops shots)"""
    panes = {(0, "e", 2), (1, "n", 3), (2, "s", 4), (3, "w", 2), (0, "n", 1)}
    for fi, (x0, z0, x1, z1, y, _) in enumerate(FLOORS):
        for side in "nsew":
            if side in "ns":
                zz = z0 + 0.2 if side == "n" else z1 - 0.2
                pts = [(x, zz) for x in frange(x0 + 2, x1 - 1.9, 4)]
            else:
                xx = x0 + 0.2 if side == "w" else x1 - 0.2
                pts = [(xx, z) for z in frange(z0 + 2, z1 - 1.9, 4)]
            run = set()
            for n, (px, pz) in enumerate(pts):
                above = [f for f in FLOORS if f[4] == y + 8 and inside(f, px, pz, -0.1)]
                if above and not in_core(px, pz, 0.5) and not blocked(keep, px - 0.6, pz - 0.6, px + 0.6, pz + 0.6, y, y + 2):
                    k.box("anodizedblack", px - 0.15, y, pz - 0.15, px + 0.15, y + 7.4, pz + 0.15)
                    run.add(n)
            for n in sorted(run):   # the glass: one pane between two neighbouring mullions in the chosen bay
                if (fi, side, n) in panes and n + 1 in run:
                    px, pz = pts[n]
                    if side in "ns": k.box("bluetintedglass", px + 0.15, y, pz - 0.125, px + 3.85, y + 3.4, pz + 0.125)
                    else: k.box("bluetintedglass", px - 0.125, y, pz + 0.15, px + 0.125, y + 3.4, pz + 3.85)


def crane(k):
    """the tower crane: a mast from the north floor to the cab at 44 m, plated north and south (cover on the climb),
    a ladder inside, a rest deck with a solid parapet at 30 m; the jib out east, the counterweight west"""
    cx, cz = CRANE
    y0 = FLOORS[2][4]
    yb = y0 + 0.5
    k.box("precastconcrete", cx - 2.4, y0, cz - 2.4, cx + 2.4, yb, cz + 2.4)               # the base pad
    for (x, z) in ((cx - 1.2, cz - 1.2), (cx + 1.2, cz - 1.2), (cx - 1.2, cz + 1.2), (cx + 1.2, cz + 1.2)):
        k.box("powdercoatyellow", x - 0.15, yb, z - 0.15, x + 0.15, JIB - 0.4, z + 0.15)
    k.box("powdercoatyellow", cx - 1.35, yb, cz - 1.6, cx + 1.35, JIB - 0.4, cz - 1.35)     # the plating (north)
    k.box("powdercoatyellow", cx - 1.35, yb, cz + 1.35, cx + 1.35, JIB - 0.4, cz + 1.6)     # (south)
    k.box("powdercoatyellow", cx - 0.6, yb, cz - 1.35, cx + 0.6, JIB - 0.4, cz - 0.9)       # the ladder's backing
    k.ladder(cx, cz - 0.9, yb, JIB, "n", mat="galvanizedsteel")
    # the rest deck at 30 m (round the mast, off its open east and west sides), its solid parapet
    ry = 30.0
    for (a, b, c, d) in ((cx - 3.5, cz - 3.5, cx + 3.5, cz - 1.6), (cx - 3.5, cz + 1.6, cx + 3.5, cz + 3.5),
                         (cx - 3.5, cz - 1.6, cx - 1.35, cz + 1.6), (cx + 1.35, cz - 1.6, cx + 3.5, cz + 1.6)):
        k.box("steeldeckplate", a, ry - 0.3, b, c, ry, d)
    walls(k, "powdercoatyellow", cx - 3.5, cz - 3.5, cx + 3.5, cz + 3.5, ry, ry + 1.2, t=0.25)
    k.light(cx + 2.5, ry + 2, cz, 8, 0.6)
    # the cab: a floor with the ladder's hole, walls (doors east and west onto the jib), a roof
    slab(k, "anodizedblack", cx - 2, cz - 2, cx + 2, cz + 2, JIB - 0.4, JIB, holes=[(cx - 0.7, cz - 0.9, cx + 0.7, cz + 0.2)])
    walls(k, "anodizedblack", cx - 2, cz - 2, cx + 2, cz + 2, JIB, JIB + 2.6, [("e", cz, 1.4, 2.2), ("w", cz, 1.4, 2.2)], t=0.3)
    k.box("anodizedblack", cx - 2, JIB + 2.6, cz - 2, cx + 2, JIB + 3.0, cz + 2)
    k.box("smokedglass", cx - 2.05, JIB + 1.0, cz - 1.6, cx - 1.7, JIB + 2.2, cz - 0.9)
    # the jib (east, over the open air) and the counter jib (west) with its weights; rails along the jib
    k.box("powdercoatyellow", cx + 2, JIB - 0.4, cz - 0.9, 40, JIB, cz + 0.9)
    k.box("powdercoatyellow", -20, JIB - 0.4, cz - 0.9, cx - 2, JIB, cz + 0.9)
    for z in (cz - 0.9, cz + 0.8):
        k.box("powdercoatyellow", cx + 2, JIB, z, 40, JIB + 1.0, z + 0.1)
    k.box("precastconcrete", -19, JIB, cz - 1.2, -14, JIB + 2.4, cz + 1.2)
    k.box("nograpple", -19, JIB + 2.4, cz - 1.2, -14, JIB + 2.5, cz + 1.2)
    k.box("galvanizedsteel", cx - 0.2, JIB + 3.0, cz - 0.2, cx + 0.2, JIB + 7.0, cz + 0.2)    # the tower top
    for x in (cx, 39.5, -19.5):
        k.box("glow", x - 0.2, JIB + (7.0 if x == cx else 1.0), cz - 0.2, x + 0.2, JIB + (7.4 if x == cx else 1.4), cz + 0.2)
    # the trolley and its hook load (formwork at 28 m, over the east end of the north floor)
    k.box("galvanizedsteel", 29, JIB - 0.9, cz - 0.8, 31, JIB - 0.4, cz + 0.8)
    k.box("galvanizedsteel", 29.95, 30.4, cz - 0.05, 30.05, JIB - 0.9, cz + 0.05)
    k.box("scaffoldboards", 27.5, 28, cz - 1.5, 32.5, 30.4, cz + 1.5)


def loads(k):
    """hanging loads in the open air between the floors (cables up to the floor above or the sky)"""
    for (x, y, z, sx, sz, mat) in ((-30, 12, -16, 4, 2.4, "bentrebarbundle"), (32, 20, 30, 3, 3, "palletstack"),
                                   (-14, 13, 30, 5, 2.4, "scaffoldboards"), (30, 27, 14, 2.4, 4.5, "bentrebarbundle"),
                                   (-31, 30, -31, 3, 3, "palletstack"), (12, 37, 16, 4, 2.4, "scaffoldboards")):
        k.box(mat, x - sx / 2, y, z - sz / 2, x + sx / 2, y + 2.2, z + sz / 2)
        over = [f[4] - 1.4 for f in FLOORS if f[4] > y and inside(f, x, z)]
        k.box("galvanizedsteel", x - 0.05, y + 2.2, z - 0.05, x + 0.05, min(over + [y + 14]), z + 0.05)


def cluster(k, kind, x, y, z, turn=False):
    """a cover island: two or three pieces together, 2.2-2.4 m tall (a standing player hides), about 5 x 2.5 m"""
    def b(mat, a0, b0, a1, b1, h, y0=0.0):
        if turn: a0, b0, a1, b1 = b0, a0, b1, a1
        k.box(mat, x + a0, y + y0, z + b0, x + a1, y + y0 + h, z + b1)
    if kind == "formwork":      # stacked formwork panels, a pallet stack beside them, boards leant on the back
        b("scaffoldboards", -2, -0.7, 1.2, 0.7, 2.4); b("palletstack", 1.4, -0.9, 2.8, 0.5, 1.4)
        b("plywoodboardup", -1.9, 0.7, 1.1, 0.9, 2.2)
    elif kind == "rebar":       # a rebar bundle on a precast block, a block beside it
        b("precastconcrete", -2.2, -0.8, 0.6, 0.8, 1.0); b("bentrebarbundle", -2.2, -0.6, 0.6, 0.6, 0.9, 1.0)
        b("precastconcrete", 0.9, -1.1, 2.3, 1.1, 2.4)
    elif kind == "mixer":       # a mixer drum on its base, a sand bag pile
        b("precastconcrete", -1.2, -1.2, 1.2, 1.2, 0.6)
        k.disc("powdercoatyellow", x, z, 1.1, y + 0.6, y + 2.6, n=8)
        b("sandbagwall", 1.5, -1.0, 2.7, 1.0, 1.2)
    elif kind == "blocks":      # cinder block pallets, two high, and one beside
        b("cinderblock", -1.6, -0.8, 0.0, 0.8, 2.3); b("palletstack", 0.2, -0.8, 1.6, 0.6, 1.2)
        b("cinderblock", 0.2, -0.8, 1.6, 0.6, 1.0, 1.2)


def cover(k):
    """three or four cover islands on each floor, 15-20 m apart, clear of the stairs, the core's faces and the
    floors' edges"""
    for (kind, x, y, z, turn) in (
            ("formwork", -26, 0, 26, False), ("rebar", -8, 0, 30, True), ("mixer", -27, 0, 1, False), ("blocks", 14, 0, 12, False),
            ("formwork", 20, 8, -28, False), ("mixer", 28, 8, -10, False), ("rebar", 0, 8, -22, True), ("blocks", 0, 8, 16, False),
            ("formwork", -14, 16, -12, True), ("formwork", 28, 16, -17, True), ("rebar", 19, 16, -29, False), ("rebar", -8, 16, -31, False),
            ("blocks", 6, 16, 0, False), ("formwork", 8, 16, -13, False),
            ("blocks", -16, 0, 22, True), ("formwork", 10, 0, 33, False), ("mixer", -13, 0, 3, False),
            ("formwork", 31, 8, 2, True), ("rebar", 14, 8, -14, False),
            ("blocks", -14, 24, 12, False), ("formwork", -27, 24, 4, True), ("mixer", -22, 24, 30, False),
            ("formwork", -6, 32, 14, False), ("mixer", 10, 32, 20, False), ("blocks", -16, 32, 4, True)):
        cluster(k, kind, x, y, z, turn)


def shear_walls(k):
    """a concrete shear wall in each storey that has a floor over it: structure, and the big piece of cover that
    breaks the storey's long line (6 m long, floor to slab)"""
    for (x0, z0, x1, z1, y) in ((12, 4, 18, 4.5, 0), (22, -22, 22.5, -16, 8), (-12, -22, -11.5, -16, 16), (-4, 16, 2, 16.5, 24)):
        k.box("castinplacewalls", x0, y, z0, x1, y + 7.4, z1)
        k.light((x0 + x1) / 2 + (1.2 if z1 - z0 > 1 else 0), y + 4, (z0 + z1) / 2 + (1.2 if x1 - x0 > 1 else 0), 7, 0.4)


def office(k):
    """the north floor's site hut (between the 1v1 ends) and the 24 m floor's fit-out: partition stubs and a glass
    meeting room (thick glass: solid)"""
    y = FLOORS[2][4]
    k.box("insulatedpanel", 0, y, -24, 10, y + 3, -17)                 # the hut
    k.box("corrugated", -0.4, y + 3, -24.4, 10.4, y + 3.3, -16.6)
    k.box("smokedglass", 2, y + 1.2, -17.1, 8, y + 2.4, -16.95)
    k.box("smokedglass", 2, y + 1.2, -24.05, 8, y + 2.4, -23.9)
    y = FLOORS[3][4]
    for (x0, z0, x1, z1) in ((-30, -10, -29.7, -2), (-26, 6, -18, 6.3), (-4, 24, -3.7, 32), (-30, -20, -24, -19.7)):
        k.box("painteddrywall", x0, y, z0, x1, y + 3, z1)
    # the meeting room: glass on a sill all round, a door east, carpet, a table
    walls(k, "anodizedblack", -34, 22, -24, 32, y, y + 3.2,
          [("n", -29, 8.4, 2.8, 0.8), ("s", -29, 8.4, 2.8, 0.8), ("w", 27, 8.4, 2.8, 0.8), ("e", 27, 2.4, 2.6)],
          t=0.3, glass="bluetintedglass")
    k.box("officecarpettiles", -33.7, y, 22.3, -24.3, y + 0.02, 31.7)
    k.box("wideplanks", -31, y, 25, -27, y + 0.8, 29)
    k.light(-29, y + 2.8, 27, 9, 0.7)


def lamps(k):
    """a work lamp in every column bay that has a storey over it (the covered floors read in the haze), and one at
    two corners of every floor"""
    for cx in (-24, -12, 0, 12, 24):
        for cz in (-24, -12, 0, 12, 24):
            if in_core(cx, cz, 2): continue
            for (x0, z0, x1, z1, y, _) in FLOORS:
                if inside((x0, z0, x1, z1), cx, cz) and any(f[4] == y + 8 and inside(f, cx, cz) for f in FLOORS):
                    k.light(cx, y + 5.5, cz, 11, 0.55)
    for (x0, z0, x1, z1, y, _) in FLOORS:
        k.light(x0 + 3, y + 3, z0 + 3, 12, 0.5); k.light(x1 - 3, y + 3, z1 - 3, 12, 0.5)


def edge(k):
    """the clouds below (the void at -8), clip round the edge and the lid at 89 m"""
    m = k.m
    m.raw("void -8")
    k.box("far", -400, -21, -400, 400, -20, 400)
    for (x0, z0, x1, z1) in ((-H - 1, -H - 1, H + 1, -H), (-H - 1, H, H + 1, H + 1), (-H - 1, -H, -H, H), (H, -H, H + 1, H)):
        k.box("clip", x0, -8, z0, x1, 89, z1)
    k.box("clip", -H, 89, -H, H, 90, H)
