"""MONORAIL (1.19.6): neutral payload in a city at blue hour, 270 x 132 m. An elevated monorail deck 8 m up runs 233 m
west to east (two 15-degree S-bends each side) over a wide avenue, between the old town (west: brick, stucco and
shingle roofs, a canal, the clock tower) and the new district (east: white panels and glass, a reflecting pool, the
glass tower). The cart starts at the middle station; A (west) pushes it east to B's end station, B pushes it west.
Pushing needs the deck (the cart counts players within 4 m). The deck is 8 m wide: the 2 m beam the cart runs on and
a walkway either side behind thin shoot-through rails.
Every half has the same ways up: its end station's two escalators, three stair towers beside the deck, two terrace
buildings with a bridge to it, and the middle station's escalator; and the same deck cover (a signal box, a gantry,
the stations' kiosks and windbreaks). The layout is point-symmetric (each team's half is the other's, turned), only
the look differs, so every timing is the same for both sides. The middle station is also the hill (koth); power plays
the whole map."""
import math, random
from .prefabs import M, minus
from .bigkit import Kit, num, yaw_to

LID = 60.0
DECK = 8.0
HX, HZ = 130, 54        # the playable city (perimeter blocks outside it)
# the track, west half (east = the same turned about the middle)
WEST = [(-115, 0), (-100, 0), (-78, 0), (-58, -5.36), (-38, -5.36), (-18, 0)]
PATH = WEST + [(0, 0)] + [(-x, -z) for (x, z) in reversed(WEST)]
THEME = {   # per side (-1 west / old town, +1 east / new district)
    "facade": {-1: ["londonstock", "stucco", "reclaimedbrick", "creambrick"], 1: ["whiteconcrete", "scifipanels", "fibrecementboard", "darkconcrete"]},
    "ground": {-1: "cobbledsetts", 1: "sidewalkslabs"},
    "street": {-1: "asphalt", 1: "asphalt"},
    "trim": {-1: "terracotta", 1: "chrome"},
    "light": {-1: 0.85, 1: 0.75},
}


def build():
    m = M("Monorail", "npayload", sun=(-0.6, 0.45, 0.35))
    m.also = ["koth", "power"]
    m.raw("setting futuristic city monorail at dusk")
    m.raw("palette base 0.42 0.44 0.52 accent 0.55 0.3 0.22 metal 0.3 0.32 0.38 glow 1.0 0.85 0.6 sky 0.07 0.09 0.22")
    k = Kit(m)
    R = random.Random(1198)

    def T(x, z, s): return (x, z) if s < 0 else (-x, -z)
    def B(mat, x0, y0, z0, x1, y1, z1, s):
        (a, b), (c, d) = T(x0, z0, s), T(x1, z1, s)
        k.box(mat if isinstance(mat, str) else mat[s], a, y0, b, c, y1, d)
    def both(mat, x0, y0, z0, x1, y1, z1):
        for s in (-1, 1): B(mat, x0, y0, z0, x1, y1, z1, s)
    def stairs(mat, x0, z0, x1, z1, top, axis, rising, s, base=0.0, solid=True):
        (a, b), (c, d) = T(x0, z0, s), T(x1, z1, s)
        k.stairs(mat, min(a, c), min(b, d), max(a, c), max(b, d), top, axis, rising if s < 0 else -rising, base=base, solid=solid)
    def light(x, y, z, r, i, s):
        a, b = T(x, z, s); k.light(a, y, b, r, i)
    def spawn(team, x, y, z, yaw, s):
        a, b = T(x, z, s); k.spawn(team, a, y, b, yaw if s < 0 else yaw + 180)
    def clipcap(x0, z0, x1, z1, top, s):
        B("clip", x0, top, z0, x1, LID, z1, s)

    # ================= the ground, the canal / pool, the perimeter =================
    k.box("asphalt", -HX, -1, -24, HX, 0, 24)                                # the avenue under the deck
    for s in (-1, 1):
        g = THEME["ground"][s]
        B(g, -HX, -1, 24, 0, 0, 46.5, s); B(g, -HX, -1, -46.5, 0, 0, -24, s)   # pavements and side streets
        B(g, -HX, -1, -54, 0, 0, -46.5, s)                                    # (the outer street opposite the canal)
        # the canal (old town, north) / reflecting pool (new district, south): sunk 1.5 m, water to -0.5, a bridge
        # at every cross street, steps out at both ends
        B("rainwetpaving" if s < 0 else "polishedconcrete", -HX, -2.5, 46.5, -22, -1.5, 53.5, s)
        B("clearlagoon", -HX, -1.5, 46.5, -22, -0.5, 53.5, s)
        B(g, -HX, -1, 53.5, 0, 0, 54, s); B(g, -22, -1, 46.5, 0, 0, 53.5, s)
        for (x0, x1) in ((-108, -100), (-82, -74), (-52, -44)):
            B("largeformatporcelain" if s > 0 else "largecobbledsetts", x0, -0.4, 46.5, x1, 0, 53.5, s)
        stairs("concretepavers", -128, 46.5, -122, 53.5, 0, "x", -1, s, base=-1.5)
        stairs("concretepavers", -28, 46.5, -22, 53.5, 0, "x", 1, s, base=-1.5)
    # the perimeter blocks (clipped)
    for s in (-1, 1):
        x = -HX
        while x < 0:
            w = R.uniform(14, 26); x1 = min(0, x + w); h = R.uniform(16, 28)
            B(THEME["facade"][s][R.randrange(4)], x, 0, HZ, x1, h, HZ + 12, s)
            B(THEME["facade"][s][R.randrange(4)], x, 0, -HZ - 12, x1, R.uniform(16, 28), -HZ, s)
            x = x1
        B(THEME["facade"][s][0], -HX - 12, 0, -HZ - 12, -HX, 24, HZ + 12, s)
    k.box("clip", -HX - 12, 0, -HZ - 12, HX + 12, LID, -HZ); k.box("clip", -HX - 12, 0, HZ, HX + 12, LID, HZ + 12)
    k.box("clip", -HX - 12, 0, -HZ, -HX, LID, HZ); k.box("clip", HX, 0, -HZ, HX + 12, LID, HZ)
    k.box("clip", -HX - 12, LID, -HZ - 12, HX + 12, LID + 2, HZ + 12)

    # ================= the deck =================
    for (a, b) in zip(PATH, PATH[1:]):
        if abs(a[0]) > 100 and abs(b[0]) > 100: continue                      # (inside the end stations)
        if abs(a[0]) < 12 and abs(b[0]) < 12: continue                         # (inside the middle station)
        ax, az = a; bx, bz = b
        if abs(ax) > 100: ax = math.copysign(100, ax)
        if abs(bx) > 100: bx = math.copysign(100, bx)
        if abs(ax) < 12: ax = math.copysign(12, bx)
        if abs(bx) < 12: bx = math.copysign(12, ax)
        k.bar("whiteconcrete", (ax, az), (bx, bz), 8, DECK - 0.8, DECK)
        k.bar("polishedconcrete", (ax, az), (bx, bz), 2, DECK - 2.2, DECK - 0.8)
    for (x, z) in [(-78, 0), (-58, -5.36), (-38, -5.36), (-18, 0)]:          # the joints at the bends
        for s in (-1, 1):
            a, b = T(x, z, s)
            k.disc("whiteconcrete", a, b, 4.33, DECK - 0.8, DECK, n=8)
            k.box("polishedconcrete", a - 1, 0, b - 1, a + 1, DECK - 2.2, b + 1)    # a pillar under each
    for (x, z) in [(-89, 0), (-48, -5.36)]:
        both("polishedconcrete", x - 1, 0, z - 1, x + 1, DECK - 2.2, z + 1)
    # the rails: thin panels (shoot-through) along both edges, gaps where the stairs and bridges come up
    GAPS = {(-100, -78, 1): [(-99, -95), (-80, -77.8)], (-100, -78, -1): [(-97, -94)], (-58, -38, 1): [(-42, -39)], (-58, -38, -1): [(-50, -46)]}
    for s in (-1, 1):
        for (x0, x1, zc) in ((-100, -78, 0), (-58, -38, -5.36)):
            for side in (1, -1):
                z = zc + side * 4
                gaps = sorted(GAPS.get((x0, x1, side), []))
                pos = x0
                for (g0, g1) in gaps + [(x1, x1)]:
                    if g0 > pos: B("carbonfibre", pos, DECK, z - 0.06 if side > 0 else z, g0, DECK + 1.1, z if side > 0 else z + 0.06, s)
                    pos = max(pos, g1)
        for (a, b) in (((-78, 0), (-58, -5.36)), ((-38, -5.36), (-18, 0))):
            dx, dz = b[0] - a[0], b[1] - a[1]; L = math.hypot(dx, dz); px, pz = -dz / L * 3.94, dx / L * 3.94
            for sg in (1, -1):   # (short axis-aligned panels down the bend: a long diagonal one's box would cross the cart's)
                n = int(math.ceil(L / 2.0))
                for j in range(n):
                    xa = a[0] + sg * px + dx * j / n; xb = a[0] + sg * px + dx * (j + 1) / n
                    zm = a[1] + sg * pz + dz * (j + 0.5) / n
                    B("carbonfibre", xa, DECK, zm - 0.06, xb, DECK + 1.1, zm + 0.06, s)
    # deck lamps down the walkways, a signal box per half, the gantries
    for s in (-1, 1):
        for x in (-95, -85, -52, -44):
            zc = 0 if x < -78 else -5.36
            B("lightblock", x, DECK, zc + 3.3, x + 0.4, DECK + 0.25, zc + 3.7, s)
        B("electricalcabinet", -84, DECK, 2.2, -82.4, DECK + 2, 3.8, s)       # signal boxes on the walkways (solid cover)
        B("electricalcabinet", -56, DECK, -9.0, -54.4, DECK + 2, -7.4, s)
        for (gx, z0, z1) in ((-89, -8.4, 8.4), (-30, -9.8, 3.4)):
            B("castiron", gx - 0.3, 0, z0 - 0.6, gx + 0.3, DECK + 3.5, z0, s); B("castiron", gx - 0.3, 0, z1, gx + 0.3, DECK + 3.5, z1 + 0.6, s)
            B("castiron", gx - 0.3, DECK + 3, z0 - 0.6, gx + 0.3, DECK + 3.5, z1 + 0.6, s)
            B("glow", gx - 0.31, DECK + 2.8, -1.2 if gx < -50 else -4.2, gx + 0.31, DECK + 3, 1.2 if gx < -50 else -2.2, s)
        for x in (-90, -68, -48, -28):
            light(x, DECK + 4, 0 if x < -60 else -4, 18, 0.7, s)

    # ================= the stations =================
    for s in (-1, 1):
        # the end station (A's west / B's east): platform at 8 over the concourse, two escalators up from it
        holes = [(-122, 10, -106, 14), (-122, -14, -106, -10)]
        for (a, b, c, d) in minus((-130, -14, -100, 14), holes): B("terrazzo", a, DECK - 0.8, b, c, DECK, d, s)
        for zz in ((10, 14), (-14, -10)):
            stairs("tactilepaving", -122, zz[0], -106, zz[1], DECK, "x", 1, s)
            B("chrome", -122, 0, zz[0] - 0.2 if zz[0] > 0 else zz[1], -106, 1.0 + DECK, zz[0] if zz[0] > 0 else zz[1] + 0.2, s)
        for (x, z) in ((-129.4, -13.4), (-129.4, 12.8), (-100.6, -13.4), (-100.6, 12.8)):
            B("whiteconcrete" if s > 0 else "castiron", x, 0, z, x + 0.6, 13.5, z + 0.6, s)
        B("carbonfibre" if s > 0 else "corrugated", -130, 13.5, -14, -100, 13.9, 14, s)   # its canopy
        B("glow", -129, 13.3, -0.5, -101, 13.5, 0.5, s)
        B("bluetintedglass", -112, 0, -8, -111.88, 6.5, 8, s)                         # the spawn screen (shoot-through glass)
        B("electricalcabinet", -126, DECK, 5, -124, DECK + 2.2, 7.5, s); B("electricalcabinet", -126, DECK, -7.5, -124, DECK + 2.2, -5, s)
        B("hologrampanel" if s > 0 else "neonmaze", -119, DECK + 0.4, 8.4, -114, DECK + 2.6, 8.52, s)   # kiosk signs (thin)
        B("cratewood" if s < 0 else "whiteconcrete", -119, DECK, 8.52, -114, DECK + 2.6, 9.8, s)       # the kiosk
        light(-115, 6, 0, 16, 0.8, s); light(-115, DECK + 4.5, 0, 18, 0.8, s)
        for i, z in enumerate((-6, -2, 2, 6)): spawn("a" if s < 0 else "b", -126, 0, z, -90, s)
        # the middle station's half: its escalator, a kiosk, a windbreak
        stairs("tactilepaving", -28, -12, -12, -8, DECK, "x", 1, s)
        B("chrome", -28, 0, -12.2, -12, 1.0 + DECK, -12, s)
        B("bluetintedglass", -8, DECK, 3.4, -2, DECK + 2.2, 3.52, s)                   # windbreak (shoot-through)
        B("cratewood" if s < 0 else "whiteconcrete", -10, DECK, 8, -6, DECK + 2.4, 11, s)   # kiosk
        B("hologrampanel" if s > 0 else "neonmaze", -10, DECK + 0.5, 7.88, -6, DECK + 2.2, 8, s)
        for (x, z) in ((-11.4, -13.4), (-11.4, 12.8)):
            B("whiteconcrete" if s > 0 else "castiron", x, DECK, z, x + 0.6, 13.5, z + 0.6, s)
        light(-20, 5, -10, 14, 0.7, s)
    k.box("terrazzo", -12, DECK - 0.8, -14, 12, DECK, 14)
    k.box("carbonfibre", -12, 13.5, -14, 12, 13.9, 14); k.box("glow", -11, 13.3, -0.5, 11, 13.5, 0.5)
    k.box("polishedconcrete", -2, 0, -6, 2, DECK - 0.8, -2); k.box("polishedconcrete", -2, 0, 2, 2, DECK - 0.8, 6)   # its piers
    k.light(0, DECK + 4.5, 0, 20, 0.9); k.light(0, 5, 0, 16, 0.6)
    m.raw(f"hill -6 {num(DECK)} -6 6 {num(DECK + 3)} 6")
    for (x, z) in PATH: m.raw(f"path {num(x)} {num(DECK)} {num(z)}")

    # ================= the stair towers and the terrace buildings (per half) =================
    for s in (-1, 1):
        fac = THEME["facade"][s]
        # T1 south of the first straight (top at x -96), T3 north of it (top at x -78), T2 north of the second (top at -40)
        stairs("concretepavers" if s > 0 else "largecobbledsetts", -96, -7.5, -80, -4, DECK, "x", -1, s)
        stairs("concretepavers" if s > 0 else "largecobbledsetts", -94, 4, -78, 7.5, DECK, "x", 1, s)
        stairs("concretepavers" if s > 0 else "largecobbledsetts", -56, -1.36, -40, 2.14, DECK, "x", 1, s)
        for (x0, x1, z0, z1) in ((-96, -80, -7.7, -7.5), (-94, -78, 7.5, 7.7), (-56, -40, 2.14, 2.34)):
            B(THEME["trim"][s], x0, 0, z0, x1, 1.0 + DECK, z1, s)                    # their outer balustrades
        # N2 (north, x -100..-82): bridge from the deck at x -99..-95
        for (bx0, bz0, bx1, bz1, sx0, sx1, sz0, sz1, door, dz, notch) in (
                (-100, 24, -82, 44, -98, -86, 40, 43.6, (-93, -89), 24, (-99, -95)),
                (-72, -44, -44, -24, -70, -58, -43.6, -40, (-62, -58), -24, (-50, -46))):
            f = fac[(bx0 // 7) % 4]
            t = 0.4
            # walls with a 4 m door on the avenue side and one on the far side
            far = bz1 if dz == bz0 else bz0
            for zz, dd in ((dz, door), (far, (bx0 + 3, bx0 + 7))):
                zt0, zt1 = (zz, zz + t) if zz == bz0 else (zz - t, zz)
                B(f, bx0, 0, zt0, dd[0], DECK - 0.4, zt1, s); B(f, dd[1], 0, zt0, bx1, DECK - 0.4, zt1, s)
                B(f, dd[0], 4.2, zt0, dd[1], DECK - 0.4, zt1, s)
            B(f, bx0, 0, bz0 + t, bx0 + t, DECK - 0.4, bz1 - t, s); B(f, bx1 - t, 0, bz0 + t, bx1, DECK - 0.4, bz1 - t, s)
            stairs("wideplanks" if s < 0 else "terrazzo", sx0, sz0, sx1, sz1, DECK, "x", 1, s)
            for (a, b, c, d) in minus((bx0, bz0, bx1, bz1), [(sx0, sz0, sx1, sz1)]):
                B("concreterooftiles" if s < 0 else "galvanizedgrating", a, DECK - 0.4, b, c, DECK, d, s)   # the roof terrace
            # parapets (gap at the bridge)
            pz0, pz1 = (bz0, bz0 + 0.3) if dz == bz0 else (bz1 - 0.3, bz1)
            B(f, bx0, DECK, pz0, notch[0], DECK + 1.0, pz1, s); B(f, notch[1], DECK, pz0, bx1, DECK + 1.0, pz1, s)
            qz0, qz1 = (bz1 - 0.3, bz1) if dz == bz0 else (bz0, bz0 + 0.3)
            B(f, bx0, DECK, qz0, bx1, DECK + 1.0, qz1, s)
            B(f, bx0, DECK, bz0 + 0.3, bx0 + 0.3, DECK + 1.0, bz1 - 0.3, s); B(f, bx1 - 0.3, DECK, bz0 + 0.3, bx1, DECK + 1.0, bz1 - 0.3, s)
            # the bridge to the deck
            dzc = 0 if notch[0] < -78 else -5.36
            e0, e1 = (dzc + 4, bz0) if dz == bz0 else (bz1, dzc - 4)
            B("fibreglassgrating" if s > 0 else "wideplanks", notch[0], DECK - 0.4, e0, notch[1], DECK, e1, s)
            for xx in (notch[0], notch[1] - 0.12):
                B("carbonfibre", xx, DECK, min(e0, e1), xx + 0.12, DECK + 1.1, max(e0, e1), s)
            light((bx0 + bx1) / 2, 5.5, (bz0 + bz1) / 2, 14, 0.7, s)

    # ================= the other blocks (clipped tops), street cover, the landmarks =================
    for s in (-1, 1):
        fac = THEME["facade"][s]
        for (x0, x1, z0, z1) in ((-130, -108, 24, 46), (-74, -52, 24, 46), (-44, -22, 24, 46),
                                  (-130, -108, -46, -24), (-100, -80, -46, -24), (-36, -16, -46, -24)):
            h = R.uniform(11, 18)
            f = fac[R.randrange(4)]
            B(f, x0, 0, z0, x1, h, z1, s)
            clipcap(x0, z0, x1, z1, h, s)
            side_z = z0 if z0 > 0 else z1   # the avenue face
            # lit windows on the avenue face (flush strips)
            for wx in range(int(x0) + 2, int(x1) - 2, 4):
                for wy in (2.2, 5.6, 9.0):
                    if wy + 1.4 < h - 0.5 and R.random() < 0.6:
                        if side_z > 0: B("glow", wx, wy, side_z - 0.04, wx + 1.6, wy + 1.4, side_z, s)
                        else: B("glow", wx, wy, side_z, wx + 1.6, wy + 1.4, side_z + 0.04, s)
            if s < 0:   # old town: shingle roofs (inside the clip)
                k_dir = 2 if (x1 - x0) < (z1 - z0) else 0
                a, b = T(x0, z0, s); c, d = T(x1, z1, s)
                k.wedge("roofshingles", 2, min(a, c), h, min(b, d), max(a, c), h + 4, (b + d) / 2)
                k.wedge("roofshingles", 3, min(a, c), h, (b + d) / 2, max(a, c), h + 4, max(b, d))
        # the landmark: the old town's clock tower / the new district's glass tower (on the -44..-22 north block)
        B("londonstock" if s < 0 else "scifipanels", -37, 0, 31, -29, 30 if s < 0 else 36, 39, s)
        B("glow" if s > 0 else "gold", -37.05, 22 if s < 0 else 12, 34, -36.99, 25 if s < 0 else 34, 36, s)
        # street cover in the avenue (mirrored footprints): planters / market stalls, a kiosk, bus shelters
        for (x0, z0, x1, z1) in ((-70, 12, -64, 15), (-34, -16, -28, -13), (-110, -20, -104, -17), (-60, 16, -56, 20)):
            B("hedge" if s > 0 else "cratewood", x0, 0, z0, x1, 1.2 if s > 0 else 1.6, z1, s)
        for (x0, z0, x1, z1) in ((-90, 14, -84, 16), (-45, -18, -39, -16)):
            B("bluetintedglass" if s > 0 else "castiron", x0, 0, z0, x1, 2.6, z0 + 0.12, s)   # shelter backs (thin: shoot-through)
            B("carbonfibre" if s > 0 else "corrugated", x0, 2.6, z0, x1, 2.8, z1, s)
        for (x, z) in ((-120, 20), (-70, -20), (-30, 20), (-100, -20), (-50, 20)):
            B("castiron", x, 0, z, x + 0.25, 6, z + 0.25, s); B("lightblock", x - 0.3, 6, z - 0.3, x + 0.55, 6.3, z + 0.55, s)
            light(x, 5, z, 14, THEME["light"][s], s)
        light(-60, 4, 50, 18, 0.6, s); light(-110, 4, 40, 16, 0.6, s); light(-90, 4, -40, 16, 0.6, s)
    # free for all: eight spawns round the city (point-symmetric pairs; save()'s top-up only sees plain boxes)
    for (x, y, z) in ((-118, 0, 19), (-78, 0, 35), (-40, 0, -35), (-52, DECK, -8)):
        for s in (-1, 1):
            a, b = T(x, z, s); k.spawn("any", a, y, b, yaw_to(a, b, 0, 0))
    m.save("monorail", dress=False)
