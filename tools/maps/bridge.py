"""SUSPENSION BRIDGE (1.19.6): power and team vs (and FFA) on a suspension bridge in fog, its deck jammed with
stopped traffic. 252 x 48 m: the span is 160 m between two portal towers (x +-80), the deck 24 m wide at 0 (four
lanes and a 1.1 m median barrier), a maintenance walkway at -6 m under both deck edges (cross gantries every 40 m,
hatch ladders up through the deck, a stair down from each end's abutment), the void at -30 and the sea far below.
  towers  two legs each (x +-80, z +-16.5), cross beams at 20 and 40 m; inside each leg a ladder through a hatch to
          each beam (the beams are walkable, a door from the leg onto each), the saddles at 60.
  cables  the main cables (z +-13.5) sag from the saddles to 4 m over the deck at mid-span, walkable from the middle
          up until they pass 45 degrees (about 30 m up, x +-57: past there you slide); hangers every 10 m to hook.
  ends    A (west): the city end, a toll plaza (six booths under a canopy, the toll office on two floors);
          B (east): the mountain end, a tunnel portal in the rock with a maintenance bay and a lay-by (a parked lorry).
The traffic (cars, two buses with 1/8 m side panels: shoot-through, a lorry with a container, a car on its side) is
placed point symmetrically round mid-span: different vehicles, the same footprints, a 3 m walk lane everywhere. Fog
banks (smoke: sight only) across the deck at x +-20 and +-60 break the 160 m straight. Edge rails are 1.6 m: higher
than a jump, so a fall off the bridge (death in vs, a 2 s trip home in power) is a choice, not an accident."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

SPAN = 80.0          # towers at x +-SPAN
END = 126.0          # the land ends at x +-END
DECK_Z = 12.0        # the deck z -12..12
LAND_Z = 24.0        # the land z -24..24
WALK_Y = -6.0        # the under-deck walkway
CABLE_Z = 13.5
TOP = 60.0           # the saddles
VOID = -30.0
LID = 110.0


def cable_y(x):
    return 4.0 + (TOP - 4.0) * (x / SPAN) ** 2


def slanted(k, mat, xa, ya, xb, yb, z0, z1, h):
    """a bar from (xa, ya) to (xb, yb) (its top surface), h thick vertically, z0..z1 across"""
    s = (yb - ya) / (xb - xa)
    c = ya - s * xa
    k.cut(mat, (xa, min(ya, yb) - h, z0, xb, max(ya, yb), z1), [(-s, 1, 0, c), (s, -1, 0, -(c - h))])


def leg(k, x, sz):
    """a tower leg at (x, sz * 16.5): a hollow 4 x 5 m shell from the pier at -30 to the saddle at 60, a door onto the
    deck's landing and onto each beam, a ladder through a hatch up to each beam's floor"""
    zi, zo = sz * 14.0, sz * 19.0                                  # its inner (deck side) and outer faces
    za, zb = sorted((zi, zo))
    x0, x1 = x - 2.0, x + 2.0
    t = 0.5
    lo = min(zi + sz * t, zo - sz * t); hi = max(zi + sz * t, zo - sz * t)   # the inside, along z
    k.box("weatheredconcrete", x0, VOID, za, x1, 0, zb)                 # (solid below the deck)
    k.box("weatheredconcrete", x0, 0, za, x0 + t, TOP, zb); k.box("weatheredconcrete", x1 - t, 0, za, x1, TOP, zb)
    k.box("weatheredconcrete", x0 + t, 0, min(zo, zo - sz * t), x1 - t, TOP, max(zo, zo - sz * t))      # the outer wall
    # the inner wall with doors at 0, 20 and 40 (2.2 wide, 2.6 tall)
    wa, wb = sorted((zi, zi + sz * t))
    ys = [0.0, 20.0, 40.0]
    y = 0.0
    for yd in ys:
        if yd > y: k.box("weatheredconcrete", x0 + t, y, wa, x1 - t, yd, wb)
        k.box("weatheredconcrete", x0 + t, yd, wa, x - 1.1, yd + 2.6, wb); k.box("weatheredconcrete", x + 1.1, yd, wa, x1 - t, yd + 2.6, wb)
        y = yd + 2.6
    k.box("weatheredconcrete", x0 + t, y, wa, x1 - t, TOP, wb)
    k.box("weatheredconcrete", x0, TOP, za, x1, TOP + 1.0, zb)                                           # its cap
    # floors inside at 20 and 40, each with a hatch over the ladder below it
    ladders = [((x, zo - sz * t), "s" if sz > 0 else "n", 0.0, 20.0), ((x0 + t, (lo + hi) / 2), "w", 20.0, 40.0)]
    for i, yf in enumerate((20.0, 40.0)):
        (lx, lz), face, _, _ = ladders[i]
        if face in "sn": hole = (lx - 0.7, min(lz, lz - sz * 0.9), lx + 0.7, max(lz, lz - sz * 0.9))
        else: hole = (lx, lz - 0.7, lx + 0.9, lz + 0.7)
        for (a, b, c, d) in minus((x0 + t, lo, x1 - t, hi), [hole]): k.box("steeldeckplate", a, yf - 0.4, b, c, yf, d)
    for ((lx, lz), face, y0, y1) in ladders: k.ladder(lx, lz, y0, y1, face)
    k.box("steeldeckplate", x0 + t, -0.4, lo, x1 - t, 0, hi)
    k.light(x, 2.4, (lo + hi) / 2, 6, 0.6); k.light(x, 22.4, (lo + hi) / 2, 6, 0.6); k.light(x, 42.4, (lo + hi) / 2, 6, 0.6)


def car(k, x, z, mat, n=0, flip=False):
    """a car (4 x 2 m, 1.5 m tall: a body and a cabin); flip: on its side (2 x 1.5 m across, 2 m tall)"""
    if flip:
        k.box(mat, x - 2, 0, z - 0.75, x + 2, 2.0, z + 0.75); k.box("chrome", x - 1.6, 0.4, z + 0.75, x + 1.6, 1.6, z + 0.95)
        return
    k.box(mat, x - 2, 0.3, z - 1, x + 2, 1.0, z + 1)
    k.box(mat, x - 1.1 + (0.4 if n % 2 else 0), 1.0, z - 0.9, x + 0.9 + (0.4 if n % 2 else 0), 1.5, z + 0.9)
    for dx in (-1.3, 1.3):
        k.box("anodizedblack", x + dx - 0.35, 0, z - 1.02, x + dx + 0.35, 0.6, z + 1.02)


def bus(k, x, z):
    """a bus (12 x 2.6 m, 3.2 m tall): a solid floor, roof and ends, its sides 1/8 m panels (shoot-through)"""
    k.box("aircraftskin", x - 6, 0.4, z - 1.3, x + 6, 0.9, z + 1.3)
    k.box("aircraftskin", x - 6, 2.9, z - 1.3, x + 6, 3.2, z + 1.3)
    k.box("aircraftskin", x - 6, 0.9, z - 1.3, x - 5.6, 2.9, z + 1.3); k.box("aircraftskin", x + 5.6, 0.9, z - 1.3, x + 6, 2.9, z + 1.3)
    for zz in (z - 1.3, z + 1.175):
        k.box("aircraftskin", x - 5.6, 0.9, zz, x + 5.6, 2.9, zz + 0.125)
    for dx in (-4.2, 3.8):
        k.box("anodizedblack", x + dx - 0.5, 0, z - 1.32, x + dx + 0.5, 0.9, z + 1.32)


def lorry(k, x, z):
    """a lorry: a cab (2.5 m) and a 12 m container on a trailer"""
    k.box("powdercoatyellow", x - 7.5, 0.4, z - 1.25, x - 5, 3.0, z + 1.25)
    k.box("galvanizedsteel", x - 5, 0.6, z - 1.2, x + 7.2, 1.2, z + 1.2)
    k.box("shippingcontainer", x - 4.8, 1.2, z - 1.22, x + 7.0, 3.8, z + 1.22)
    for dx in (-6.2, 4.5, 6.0):
        k.box("anodizedblack", x + dx - 0.5, 0, z - 1.27, x + dx + 0.5, 0.6, z + 1.27)


# the traffic on the half x < 0 (mirrored round mid-span for the other half): (kind, x, z, material)
TRAFFIC = [("car", -73, 8.6, "powdercoatgreen"), ("car", -70, -3.2, "chrome"), ("car", -64, 3.2, "powdercoatyellow"),
           ("car", -55, -8.6, "anodizedblack"), ("bus", -48, 3.2, None), ("car", -40, 8.6, "electroplatednickel"),
           ("car", -36, -8.6, "powdercoatgreen"), ("car", -30, -3.2, "chrome"), ("lorry", -22, 8.6, None),
           ("car", -14, -3.2, "powdercoatyellow"), ("car", -10, 3.2, "anodizedblack"), ("flip", -5, -8.6, "chrome"),
           ("car", -84, -8.6, "electroplatednickel"), ("car", -86, 3.2, "powdercoatgreen")]


def decorate(k):
    """sparing and off the lanes: the city's towers and the mountain beyond the bounds, expansion joints, warning lamps
    on the towers, a sign gantry over the toll plaza, ivy-free concrete everywhere else"""
    import random
    R = random.Random("bridge")
    # the city (A): towers of different heights, unlit, fading into the fog
    x = -130.0
    for row in range(3):
        z = -70.0
        while z < 70:
            w = R.choice((10, 14, 18)); h = R.choice((24, 36, 48, 70))
            k.box("far", x - 12 - row * 20, VOID, z, x - row * 20, h, z + w)
            z += w + R.choice((3, 6))
    # the mountain (B): rock in steps rising away from the road
    for (x0, z0, x1, z1, y) in ((128, -60, 150, 60, 50), (150, -70, 180, 70, 80), (90, 26, 128, 44, 30), (90, -44, 128, -26, 30),
                                (100, 44, 140, 70, 55), (100, -70, 140, -44, 55)):
        k.box("windscouredrock", x0, VOID, z0, x1, y, z1)
    # expansion joints across the deck (flush), and at the abutments
    for x in (-90, -40, 0, 40, 90):
        k.box("steeldeckplate", x - 0.3, 0, -DECK_Z, x + 0.3, 0.01, DECK_Z)
    # red warning lamps on the tower tops and at the beams' ends
    for x in (-SPAN, SPAN):
        for sz in (-1, 1):
            k.box("lightblock", x - 0.4, TOP + 1, sz * 16.5 - 0.4, x + 0.4, TOP + 1.6, sz * 16.5 + 0.4)
    # the toll plaza's sign gantry (over the booths' canopy: out of the way) and its stripes
    k.box("galvanizedsteel", -110.3, 0, -23.5, -109.7, 8.5, -23); k.box("galvanizedsteel", -110.3, 0, 23, -109.7, 8.5, 23.5)
    k.box("galvanizedsteel", -110.4, 8.5, -23.5, -109.6, 9.1, 23.5)
    for z in (-15, -5, 5, 15): k.box("roadmarkings", -110.45, 7.0, z - 3, -110.4, 8.4, z + 3)
    for z in (-12.5, -6, 0.5, 6, 12.5):
        if abs(z) < 1: continue
        k.box("hazard", -108, 0, z - 0.15, -100, 0.02, z + 0.15)
    # the tunnel's lit band and its portal's name stone
    k.box("lightblock", 112.4, 6.6, -5.4, END - 0.5, 6.8, -5.2); k.box("lightblock", 112.4, 6.6, 5.2, END - 0.5, 6.8, 5.4)
    k.box("precastconcrete", 111.6, 8.2, -4, 112, 10, 4)


def build():
    m = M("Suspension Bridge", "power", sun=(0.3, 1.0, 0.2))
    m.also = ["vs"]
    k = Kit(m)
    m.raw("palette base 0.66 0.66 0.66 accent 0.55 0.22 0.16 metal 0.34 0.35 0.37 glow 1.00 0.78 0.45 sky 0.80 0.82 0.84")
    m.raw(f"void {num(VOID)}")

    # ---- the deck: asphalt lanes, markings flush, the median, 1.6 m edge rails (gaps at the tower landings)
    for t in tiles([(-SPAN - 10, -DECK_Z, SPAN + 10, DECK_Z)], 30):
        k.box("asphalt", t[0], -1.2, t[1], t[2], 0, t[3])
    for z in (-6.0, 6.0):
        for x in range(-88, 89, 8): k.box("roadmarkings", x - 2, 0, z - 0.1, x + 2, 0.01, z + 0.1)
    for (a, b) in ((-90, -62), (-58, -22), (-18, 18), (22, 58), (62, 90)):          # (gaps to cross every 40 m)
        k.box("precastconcrete", a, 0, -0.4, b, 1.1, 0.4)
    for sz in (-1, 1):
        z0, z1 = sorted((sz * DECK_Z, sz * (DECK_Z + 0.3)))
        for (a, b) in ((-SPAN - 10, -SPAN - 2.2), (-SPAN + 2.2, SPAN - 2.2), (SPAN + 2.2, SPAN + 10)):
            k.box("galvanizedsteel", a, 0, z0, b, 1.6, z1)
        za, zb = sorted((sz * DECK_Z, sz * 14.0))
        for x in (-SPAN, SPAN): k.box("steeldeckplate", x - 2.2, -1.2, za, x + 2.2, 0, zb)        # (the landing to the leg)
        fz0, fz1 = sorted((sz * DECK_Z, sz * (DECK_Z + 0.3)))
        k.box("bridgegirder", -SPAN - 10, -2.6, fz0, SPAN + 10, -1.2, fz1)                          # (the edge girder)
    for x in range(-80, 81, 10):                                                                    # cross girders
        k.box("bridgegirder", x - 0.3, -2.2, -DECK_Z, x + 0.3, -1.2, DECK_Z)

    # ---- the walkway under both edges at -6 (3 m), cross gantries every 40 m, hatch ladders, rails
    for sz in (-1, 1):
        za, zb = sorted((sz * 9.0, sz * 12.0))
        k.box("meshcatwalk", -90, WALK_Y - 0.3, za, 90, WALK_Y, zb)
        ra, rb = sorted((sz * 11.9, sz * 12.0))
        k.box("galvanizedsteel", -90, WALK_Y, ra, 90, WALK_Y + 1.1, rb)
        for x in range(-80, 81, 20): k.box("galvanizedsteel", x - 0.15, WALK_Y + 0.3 - 0.3, ra, x + 0.15, -2.2, rb)   # (hangers)
        for x in (-65.0, -25.0, 25.0, 65.0):
            # a hatch through the deck (and the girder's lip) with a ladder up a post on its outer side
            k.ladder(x, sz * 10.0, WALK_Y, 0, "n" if sz > 0 else "s")
            pz0, pz1 = sorted((sz * 9.7, sz * 10.0))
            k.box("galvanizedsteel", x - 0.6, WALK_Y, pz0, x + 0.6, -1.2, pz1)
    for x in (-40.0, 0.0, 40.0):
        k.box("meshcatwalk", x - 1.5, WALK_Y - 0.3, -9, x + 1.5, WALK_Y, 9)
    # cut the hatches out of the deck: rebuild the deck tiles round them
    hatches = [(x - 0.75, z0, x + 0.75, z1) for x in (-65.0, -25.0, 25.0, 65.0) for (z0, z1) in ((-11.0, -10.0), (10.0, 11.0))]
    lines = []
    for l in m.lines:
        w = l.split()
        if w[:1] == ["asphalt"] and float(w[2]) == -1.2:
            r = tuple(map(float, (w[1], w[3], w[4], w[6])))
            for (a, b, c, d) in minus(r, hatches): lines.append(f"asphalt {num(a)} -1.2 {num(b)} {num(c)} 0 {num(d)}")
        else: lines.append(l)
    m.lines = lines
    for (a, b, c, d) in hatches:   # (a yellow frame round each hatch)
        k.box("hazard", a - 0.2, 0, b - 0.2, c + 0.2, 0.02, b); k.box("hazard", a - 0.2, 0, d, c + 0.2, 0.02, d + 0.2)

    # ---- the towers and the cables
    for x in (-SPAN, SPAN):
        for sz in (-1, 1): leg(k, x, sz)
        for (y0, y1) in ((18.5, 20.0), (38.5, 40.0)):
            k.box("precastconcrete", x - 1.6, y0, -14, x + 1.6, y1, 14)
            k.box("galvanizedsteel", x - 1.6, y1, -14, x - 1.5, y1 + 1.1, 14); k.box("galvanizedsteel", x + 1.5, y1, -14, x + 1.6, y1 + 1.1, 14)
        k.box("precastconcrete", x - 1.6, 54, -14, x + 1.6, 58, 14)                 # (the top strut)
    for sz in (-1, 1):
        z0, z1 = sorted((sz * (CABLE_Z - 0.6), sz * (CABLE_Z + 0.6)))
        xs = [-80, -72, -64, -56, -48, -40, -32, -24, -16, -8, 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80]
        for (xa, xb) in zip(xs, xs[1:]):
            slanted(k, "galvanizedsteel", xa, cable_y(xa), xb, cable_y(xb), z0, z1, 1.0)
        for x in range(-70, 71, 10):   # the hangers, deck to cable
            k.box("oxidizedsteel", x - 0.1, 0, sz * CABLE_Z - 0.1, x + 0.1, cable_y(x) - 1.0, sz * CABLE_Z + 0.1)
        for x in (-SPAN, SPAN): k.box("oxidizedsteel", x - 1.6, TOP - 2, z0 - 0.4, x + 1.6, TOP + 1, z1 + 0.4)      # (the saddle)
        # the back stays down to the anchorages (too steep to walk)
        for s in (-1, 1):
            xa, xb = sorted((s * SPAN, s * 112.0))
            ya, yb = (TOP, 2.0) if s > 0 else (2.0, TOP)
            slanted(k, "galvanizedsteel", xa, ya, xb, yb, z0, z1, 1.0)
            k.box("precastconcrete", s * 110 - 2, 0, z0 - 1, s * 110 + 2 if s > 0 else s * 110 + 2, 2.5, z1 + 1)   # (the anchorage)

    # ---- the land at both ends: an abutment down to the void, stairs down to the walkway
    stairs_holes = []
    for s in (-1, 1):
        for sz in (-1, 1):
            xa, xb = sorted((s * 90.0, s * 97.0)); za, zb = sorted((sz * 9.0, sz * 12.0))
            stairs_holes.append((xa, za, xb, zb))
    for s in (-1, 1):
        xa, xb = sorted((s * (SPAN + 10), s * END))
        for t in tiles([(xa, -LAND_Z, xb, LAND_Z)], 20):
            for (a, b, c, d) in minus(t, stairs_holes):
                k.box("concretepavers" if s < 0 else "asphalt", a, VOID, b, c, 0, d)
        for sz in (-1, 1):
            xa, xb = sorted((s * 90.0, s * 97.0)); za, zb = sorted((sz * 9.0, sz * 12.0))
            k.box("weatheredconcrete", xa, VOID, za, xb, WALK_Y - 0.3, zb)
            k.stairs("weatheredconcrete", xa, za, xb, zb, 0, "x", s, base=WALK_Y)
    # the land's wings beside the deck at the abutment (z 12..24 over the void stops at x +-90)

    # ---- A's end: the toll plaza
    for i, z in enumerate((-15.75, -9.25, -2.75, 2.75, 9.25, 15.75)):
        if abs(z) < 1: continue
        k.box("precastconcrete", -105, 0, z - 1.25, -103, 1.1, z + 1.25)
        k.box("glass", -105, 1.1, z - 1.25, -103, 2.6, z + 1.25)
        k.box("precastconcrete", -105, 2.6, z - 1.25, -103, 3.0, z + 1.25)
    k.box("precastconcrete", -106, 6.0, -19, -102, 6.6, 19)                      # the canopy
    for z in (-18.5, 18.5): k.box("precastconcrete", -104.4, 0, z - 0.4, -103.6, 6.0, z + 0.4)
    # the toll office: two floors, stairs inside, its door to the plaza
    x0, z0, x1, z1 = -124, -24, -112, -16
    k.box("precastconcrete", x0, 0, z0, x0 + 0.4, 8, z1); k.box("precastconcrete", x1 - 0.4, 0, z0, x1, 8, z1 - 3.5)
    k.box("precastconcrete", x1 - 0.4, 2.6, z1 - 3.5, x1, 8, z1)
    k.box("precastconcrete", x0, 0, z1 - 0.4, x1 - 6, 8, z1); k.box("precastconcrete", x1 - 3, 0, z1 - 0.4, x1, 8, z1)
    k.box("precastconcrete", x1 - 6, 2.6, z1 - 0.4, x1 - 3, 8, z1)
    k.box("glass", x0 + 0.4, 5.0, z1 - 0.4, x1 - 6, 7.0, z1 - 0.3)
    for (a, b, c, d) in minus((x0 + 0.4, z0, x1 - 0.4, z1 - 0.4), [(x0 + 0.4, z0 + 0.4, x0 + 2.4, z0 + 7)]):
        k.box("steeldeckplate", a, 3.6, b, c, 4.0, d)
    k.stairs("precastconcrete", x0 + 0.4, z0 + 0.4, x0 + 2.4, z0 + 7, 4, "z", -1)
    k.box("precastconcrete", x0, 8, z0, x1, 8.4, z1)
    k.light((x0 + x1) / 2, 3.2, (z0 + z1) / 2, 9, 0.7); k.light((x0 + x1) / 2, 7.2, (z0 + z1) / 2, 9, 0.7)
    k.box("concretepavers", -END, 0, 8, -116, 1.0, 9)                             # (planter kerbs, crouch cover)
    for z in (-6, 6): k.box("kerbstones", -114, 0, z - 1.5, -112, 0.8, z + 1.5)

    # ---- B's end: the tunnel portal in the rock, the maintenance bay, the lay-by
    k.box("graniterock", 112, 0, -LAND_Z, END, 24, -6); k.box("graniterock", 112, 0, 6, END, 24, LAND_Z)
    k.box("graniterock", 112, 7.5, -6, END, 24, 6)
    k.box("tunnellining", 112, 0, -6, 112.4, 7.5, -5.6); k.box("tunnellining", 112, 0, 5.6, 112.4, 7.5, 6)
    k.box("tunnellining", 112, 7, -6, END, 7.5, 6)
    k.box("tunnellining", END - 0.5, 0, -6, END, 7, 6)
    for z in (-5.6, 5.4): k.box("tunnellining", 112.4, 0, z, END - 0.5, 7, z + 0.2)
    for x in (116, 121): k.light(x, 6, 0, 9, 0.8)
    # the bay (north, a roof on six pillars) and the lay-by (south, the parked lorry)
    for x in (96, 102, 108):
        for z in (-22, -15):
            k.box("precastconcrete", x - 0.5, 0, z - 0.5, x + 0.5, 6, z + 0.5)
    k.box("precastconcrete", 94, 6, -24, 112, 6.5, -13)
    k.box("galvanizedsteel", 98, 0, -23.5, 106, 2.2, -22.5)                      # (a workbench row on the back)
    lorry(k, 102, 18.5)
    for z in (-14, 14): k.light(103, 5, z, 10, 0.6)

    # ---- the traffic (point symmetric round mid-span)
    for (kind, x, z, mat) in TRAFFIC:
        for (px, pz) in ((x, z), (-x, -z)):
            if kind == "car": car(k, px, pz, mat, n=int(abs(px)))
            elif kind == "flip": car(k, px, pz, mat, flip=True)
            elif kind == "bus": bus(k, px, pz)
            else: lorry(k, px, pz)

    # ---- fog banks across the deck (sight only)
    # (each leaves a lane 8 m wide on alternate sides, so no line along the deck misses all four, and the gaps are
    #  where the walk goes: bots and the walk checks take smoke as a wall)
    for (x, z0, z1) in ((-60.0, -DECK_Z, 4.0), (-20.0, -4.0, DECK_Z), (20.0, -DECK_Z, 4.0), (60.0, -4.0, DECK_Z)):
        k.box("smokewall", x - 1, 0, z0, x + 1, 6, z1)

    # ---- the bounds: clip walls round the whole bridge up to the lid, the lid 45 m over the towers' tops; the sea
    for (x0, z0, x1, z1) in ((-END - 2, -LAND_Z - 2, END + 2, -LAND_Z), (-END - 2, LAND_Z, END + 2, LAND_Z + 2),
                             (-END - 2, -LAND_Z, -END, LAND_Z), (END, -LAND_Z, END + 2, LAND_Z)):
        k.box("clip", x0, VOID, z0, x1, LID, z1)
    k.box("clip", -END - 2, LID, -LAND_Z - 2, END + 2, LID + 1, LAND_Z + 2)
    k.box("far", -600, -51, -600, 600, -50, 600)
    # the land's edge at the far ends and sides: the city's facades (A), the mountain (B), all out of reach
    k.box("far", -END - 40, VOID, -LAND_Z - 2, -END - 2, 30, LAND_Z + 2)
    k.box("windscouredrock", END + 2, VOID, -LAND_Z - 30, END + 40, 60, LAND_Z + 30)
    for sz in (-1, 1):
        za, zb = sorted((sz * (LAND_Z + 2), sz * (LAND_Z + 30)))
        k.box("windscouredrock", SPAN + 10, VOID, za, END + 2, 40, zb)

    # ---- spawns
    for (x, z) in ((-108, -3), (-108, 3), (-111, -1.5), (-111, 1.5)):
        k.spawn("a", x, 0, z, yaw_to(x, z, 0, z))
        k.spawn("b", -x, 0, -z, yaw_to(-x, -z, 0, -z))
    for (x, z, y) in ((-50, 9, 0), (50, -9, 0), (-26, -9.5, 0), (26, 9.5, 0), (-70, 10.5, WALK_Y), (70, -10.5, WALK_Y),
                      (-30, -10.5, WALK_Y), (30, 10.5, WALK_Y)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, z))

    # ---- lamps along the deck every 20 m (the fog's depth cue), a few under it
    for x in range(-80, 81, 20):
        for sz in (-1, 1):
            if abs(x) == 80: continue
            z = sz * 12.15
            k.box("galvanizedsteel", x - 0.12, 1.6, z - 0.12, x + 0.12, 7, z + 0.12)
            k.box("lightblock", x - 0.3, 7, z, x + 0.3, 7.2, z - sz * 1.2)
            k.light(x, 6.5, z - sz * 1.0, 14, 0.7)
        k.light(x + 10, -3.2, 0, 14, 0.5)
    k.light(-104, 5.4, 0, 16, 0.7)
    decorate(k)
    m.raw("setting suspension bridge in fog")
    m.save("bridge", dress=False)
