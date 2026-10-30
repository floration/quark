"""EXPRESS (1.19.6): neutral payload through a desert railway town on a hot afternoon, 270 x 90 m.
The track runs west to east, flat, through the town's main street. The cart starts under the water tower in the town
square at the middle; A (west, the station) pushes it east to the mine-head yard, B (east) pushes it west to the
station. Re-aimed from the suggested one-way design: each half is the other's mirror (x -> -x), only the look differs
(the west half is the station end: platform, freight shed, saloon, store, hotel, the bank; the east half the mine end:
ore dock, powder shed, cantina, assay office, boarding house, the jail), so every timing is the same for both teams.
A half, from the end: the yard (the team's spawn), a bend through the canyon gap (rock rims at 8 m, a natural arch
over the track, a stair up each rim), the edge of town, the main street (three false-fronted buildings a side with
shoot-through plank fronts, one solid stone one, a balcony walk at 4 m along both sides, back alleys behind), then
the square. The water tower's walkway (12 m, a ladder from each side street) overlooks both streets; it is koth's
hill underneath."""
import random
from .prefabs import M
from .bigkit import Kit, num, yaw_to

LID = 50.0
HX, HZ = 135, 45
TY = 0.06            # the track's line: the cart rides just over the sleepers (their tops 0.05)
WEST = [(-124, 0), (-104, 0), (-92, -12), (-74, -12), (-62, 0)]
PATH = WEST + [(0, 0)] + [(-x, z) for (x, z) in reversed(WEST)]
LOOK = {   # per side: (-1 west / the station end, +1 east / the mine end)
    "north": {-1: ["wideplanks", "cratewood", "mudandstrawdaub"], 1: ["mudandstrawdaub", "wideplanks", "cratewood"]},
    "south": {-1: ["cratewood", "wideplanks"], 1: ["wideplanks", "cratewood"]},
    "names": {-1: "saloon store hotel / feed bank", 1: "cantina assay boarding / livery jail"},
}


def build():
    m = M("Express", "npayload", sun=(-0.5, 0.85, 0.45))
    m.also = ["koth", "power"]
    m.raw("setting desert railway town, hot afternoon")
    m.raw("palette base 0.72 0.58 0.42 accent 0.62 0.32 0.18 metal 0.42 0.38 0.34 glow 1.0 0.8 0.5 sky 0.86 0.7 0.5")
    k = Kit(m)
    R = random.Random(1196)

    def X(x, s): return x if s < 0 else -x
    def B(mat, x0, y0, z0, x1, y1, z1, s): k.box(mat, X(x0, s), y0, z0, X(x1, s), y1, z1)
    def W(mat, d, x0, y0, z0, x1, y1, z1, s):
        if s > 0 and d in (0, 1): d = 1 - d
        k.wedge(mat, d, X(x0, s), y0, z0, X(x1, s), y1, z1)
    def stairs(mat, x0, z0, x1, z1, top, axis, rising, s, base=0.0):
        k.stairs(mat, min(X(x0, s), X(x1, s)), z0, max(X(x0, s), X(x1, s)), z1, top, axis,
                 rising if (s < 0 or axis == "z") else -rising, base=base)
    def light(x, y, z, r, i, s): k.light(X(x, s), y, z, r, i)

    # ================= the ground, the track, the dunes, the bounds =================
    k.box("sunbakedhardpan", -HX, -1, -HZ, HX, 0, HZ)
    k.box("sandburiedroad", -60, -1, -10, 60, 0.02, 10)                      # the main street (a hair over the pan)
    for (a, b) in zip(PATH, PATH[1:]):
        k.bar("railwaysleepers", a, b, 2.6, -0.5, TY - 0.01)                 # the track bed
    for (x, z) in PATH[1:-1]:
        k.disc("railwaysleepers", x, z, 1.3, -0.5, TY - 0.01, n=8)
    for (x, z) in PATH: m.raw(f"path {num(x)} {num(TY)} {num(z)}")
    for sz in (1, -1):   # the dunes beyond the back alleys (walkable, clip at the edge)
        z0, z1 = (31, HZ) if sz > 0 else (-HZ, -31)
        k.wedge("ripplesand", 2 if sz > 0 else 3, -HX, 0, z0, HX, 6, z1)
        k.box("reddunes", -HX, 0, HZ if sz > 0 else -HZ - 10, HX, 12, HZ + 10 if sz > 0 else -HZ)
    k.box("clip", -HX - 10, 0, -HZ - 10, HX + 10, LID, -HZ); k.box("clip", -HX - 10, 0, HZ, HX + 10, LID, HZ + 10)
    for s in (-1, 1):
        B("redcanyonrock", -HX - 10, 0, -HZ, -HX, 16, HZ, s)
        B("clip", -HX - 10, 0, -HZ, -HX, LID, HZ, s)
    k.box("clip", -HX - 10, LID, -HZ - 10, HX + 10, LID + 2, HZ + 10)

    for s in (-1, 1):
        # ================= the yard: the station / the ore dock, the freight / powder shed =================
        B("wideplanks", -132, 0, 4, -106, 1.0, 9, s)                          # the platform / the loading dock
        for x in (-131, -123, -115, -107):
            B("castiron" if s < 0 else "logcabin", x, 1.0, 8.4, x + 0.4, 6, 8.8, s)
        B("corrugated" if s < 0 else "scaffoldboards", -132, 6, 3.5, -106, 6.3, 10, s)    # its canopy
        stairs("wideplanks", -106, 4, -103, 9, 1.0, "x", -1, s)               # steps off its end
        # the station building / the mine office: a hall with doors front (platform) and back, and onto the street
        f = "wideplanks" if s < 0 else "mudandstrawdaub"
        B(f, -134, 0, 12, -110, 0.3, 26, s)
        for (x0, x1) in ((-134, -126), (-122, -110)):
            B(f, x0, 0, 12, x1, 7, 12.3, s)                                   # the front (door 4 m)
        B(f, -126, 3.2, 12, -122, 7, 12.3, s)
        for (x0, x1) in ((-134, -114), ):
            B(f, x0, 0, 25.7, x1, 7, 26, s)                                   # the back (door 4 m at the east end)
        B(f, -114, 3.2, 25.7, -110, 7, 26, s)
        B(f, -134, 0, 12.3, -133.7, 7, 25.7, s)
        B(f, -110.3, 0, 12.3, -110, 7, 17, s); B(f, -110.3, 0, 21, -110, 7, 25.7, s); B(f, -110.3, 3.2, 17, -110, 7, 21, s)
        B("corrugated" if s < 0 else "rust", -134.5, 7, 11.5, -109.5, 7.4, 26.5, s)
        B("sandblastedsign", -128, 7.4, 12, -116, 9.4, 12.12, s)               # the name board (thin)
        B("woodencrate", -131, 0.3, 22, -129, 1.8, 24, s); B("woodencrate", -118, 0.3, 14, -116, 1.4, 15.6, s)
        light(-122, 5, 19, 14, 0.7, s); light(-119, 5, 6.5, 16, 0.75, s)
        # the freight shed / the powder shed
        f = "corrugated" if s < 0 else "rustedsheetfence"
        B("wideplanks", -122, 0, -28, -102, 0.3, -14, s)
        B(f, -122, 0, -14.3, -116, 6, -14, s); B(f, -110, 0, -14.3, -102, 6, -14, s); B(f, -116, 3.4, -14.3, -110, 6, -14, s)
        B(f, -122, 0, -28, -102, 6, -27.7, s)
        B(f, -122, 0, -27.7, -121.7, 6, -14.3, s)
        B(f, -102.3, 0, -27.7, -102, 6, -24, s); B(f, -102.3, 0, -18, -102, 6, -14.3, s); B(f, -102.3, 3.4, -24, -102, 6, -18, s)
        B("corrugated", -122.5, 6, -28.5, -101.5, 6.3, -13.5, s)
        B("woodencrate", -120, 0.3, -26, -117, 2.4, -23, s); B("haybales" if s < 0 else "barricade", -108, 0.3, -26, -105, 1.5, -24, s)
        light(-112, 5, -21, 12, 0.6, s)
        # spawns in the yard, facing down the line
        for z in (-4.5, -2.5, -0.5, 1.5):   # (north of the canyon gap's line: the rim hides the far yard)
            k.spawn("a" if s < 0 else "b", X(-130, s), 0.05, z, -90 if s < 0 else 90)
        B("woodencrate", -127, 0, -14, -124, 2.2, -11, s); B("woodencrate", -127, 2.2, -13.5, -125, 3.4, -11.5, s)

        # ================= the canyon gap: rock rims (8 m), the arch over the track =================
        B("redcanyonrock", -90, 0, -6, -76, 8, 22, s)
        B("redcanyonrock", -90, 0, -HZ + 3, -76, 8, -18, s)
        B("badlands", -90, 8, -6, -76, 8.3, 22, s); B("badlands", -90, 8, -HZ + 3, -76, 8.3, -18, s)
        B("crossbeddedsandstone", -85, 7, -18, -81, 8.3, -6, s)                # the arch (walkable: rim to rim)
        W("crossbeddedsandstone", 2, -85, 4.5, -18, -81, 7, -15, s)            # its feet
        W("crossbeddedsandstone", 3, -85, 4.5, -9, -81, 7, -6, s)
        B("desertvarnish", -76.05, 1, -5, -76, 6, 20, s)                       # (streaks on the rim's face)
        stairs("crossbeddedsandstone", -76, 18.5, -60, 21.5, 8.3, "x", -1, s)  # up the north rim from the edge of town
        stairs("crossbeddedsandstone", -101.5, -30, -90, -27, 8.3, "x", 1, s)  # up the south rim from the yard
        B("boulderfield", -74, 0, -24, -70, 1.8, -20, s); B("boulderfield", -96, 0, 10, -93, 1.4, 13, s)
        for (x, z, h) in ((-96, 24, 3.4), (-70, -32, 2.8), (-66, 26, 3.0), (-100, -8, 2.6)):
            B("cactus", x - 0.3, 0, z - 0.3, x + 0.3, h, z + 0.3, s)
            B("cactus", x + 0.3, 1.2, z - 0.2, x + 1.0, 1.6, z + 0.2, s); B("cactus", x + 0.7, 1.6, z - 0.2, x + 1.0, h - 0.6, z + 0.2, s)
        light(-83, 4, -12, 16, 0.6, s)

        # ================= the main street: three buildings a side, balconies, back alleys =================
        for side in (1, -1):
            fronts = LOOK["north" if side > 0 else "south"][s]
            for i, (x0, x1) in enumerate(((-60, -44), (-42, -28), (-26, -12))):
                stone = side < 0 and i == 2                                     # the bank / the jail (thick stone)
                f = "sandstoneashlar" if stone else fronts[i % len(fronts)]
                zf, zb = 12 * side, 24 * side                                   # the front and back faces
                def Z(a, b): return (min(a, b), max(a, b))
                t = 0.4 if stone else 0.12                                      # (plank fronts: shoot-through)
                za, zb2 = Z(zf, zf + side * t)
                xm = (x0 + x1) / 2
                B(f, x0, 0, za, xm - 1.2, 3.6, zb2, s); B(f, xm + 1.2, 0, za, x1, 3.6, zb2, s)   # the front, a door
                za, zb2 = Z(zb, zb - side * 0.3)
                B(f, x0, 0, za, xm - 1.2, 3.6, zb2, s); B(f, xm + 1.2, 0, za, x1, 3.6, zb2, s)   # the back, a door
                B(f, x0, 0, *Z(zf, zb)[0:1], x0 + 0.3, 3.6, Z(zf, zb)[1], s)
                B(f, x1 - 0.3, 0, Z(zf, zb)[0], x1, 3.6, Z(zf, zb)[1], s)
                B(f, xm - 1.2, 2.8, Z(zf, zf + side * t)[0], xm + 1.2, 3.6, Z(zf, zf + side * t)[1], s)
                B(f, xm - 1.2, 2.8, Z(zb, zb - side * 0.3)[0], xm + 1.2, 3.6, Z(zb, zb - side * 0.3)[1], s)
                B("wideplanks", x0, 0, *Z(zf, zb)[0:1], x1, 0.2, Z(zf, zb)[1], s)
                B(f, x0, 3.6, Z(zf, zb)[0], x1, 8, Z(zf, zb)[1], s)              # the upper storey (solid)
                B(f, x0 + 1, 8, Z(zf, zf + side * 0.12)[0], x1 - 1, 10, Z(zf, zf + side * 0.12)[1], s)   # the false front
                B("corrugated", x0, 8, Z(zf + side * 0.12, zb)[0], x1, 8.3, Z(zf + side * 0.12, zb)[1], s)
                B("glassbottles" if i == 0 else "woodencrate", x0 + 0.6, 0.2, *Z(zb - side * 1.3, zb - side * 0.3)[0:1], x0 + 4, 1.6, Z(zb - side * 1.3, zb - side * 0.3)[1], s)
                for wx in (x0 + 2, x1 - 4):
                    B("glow", wx, 5, *Z(zf, zf - side * 0.04)[0:1], wx + 2, 6.6, Z(zf, zf - side * 0.04)[1], s)
                light(xm, 3, side * 18, 10, 0.6, s)
            # the balcony walk at 4 m (over the boardwalk), its posts and thin rail, a stair at its town-edge end
            zf = 12 * side
            def Z(a, b): return (min(a, b), max(a, b))
            B("wideplanks", -60, 0, *Z(zf, zf - side * 2)[0:1], -12, 0.25, Z(zf, zf - side * 2)[1], s)      # the boardwalk
            B("scaffoldboards", -60, 3.8, *Z(zf, zf - side * 2)[0:1], -12, 4.0, Z(zf, zf - side * 2)[1], s)
            for x in (-60, -48, -36, -24, -12.3):
                B("wideplanks", x, 0.25, *Z(zf - side * 1.75, zf - side * 2)[0:1], x + 0.3, 3.8, Z(zf - side * 1.75, zf - side * 2)[1], s)
            B("picketfence", -60, 4.0, *Z(zf - side * 1.9, zf - side * 2)[0:1], -12, 5.0, Z(zf - side * 1.9, zf - side * 2)[1], s)
            stairs("wideplanks", -66, *(sorted((zf, zf - side * 2))[0:1]), -60, sorted((zf, zf - side * 2))[1], 4.0, "x", 1, s)
            # the back alley's fence (gaps onto the dunes)
            za = 30 * side
            for (x0, x1) in ((-104, -86), (-74, -50), (-40, -6)):
                B("rustedsheetfence", x0, 0, *Z(za, za + side * 0.12)[0:1], x1, 2.0, Z(za, za + side * 0.12)[1], s)
        # the street's cover: a stagecoach, a water trough, hitching rails, the sign gantry, lamps
        B("logcabin", -36, 0.7, -7, -30, 2.6, -3.6, s)                         # the stagecoach's body
        for x in (-35.5, -31):
            B("castiron", x, 0, -7.2, x + 0.5, 1.2, -6.9, s); B("castiron", x, 0, -3.7, x + 0.5, 1.2, -3.4, s)
        B("wideplanks", -50, 0, 4.5, -46, 0.9, 6, s)                            # the trough
        B("clearlagoon", -49.8, 0.6, 4.7, -46.2, 0.85, 5.8, s)
        for (x0, z0) in ((-58, 7.2), (-22, -7.6)):
            B("wideplanks", x0, 0.9, z0, x0 + 6, 1.05, z0 + 0.12, s)
            B("wideplanks", x0, 0, z0, x0 + 0.15, 0.9, z0 + 0.12, s); B("wideplanks", x0 + 5.85, 0, z0, x0 + 6, 0.9, z0 + 0.12, s)
        for z0, z1 in ((-10, -9.6), (9.6, 10)):
            B("wideplanks", -40.2, 0, z0, -39.8, 6.2, z1, s)
        B("wideplanks", -40.2, 5.8, -10, -39.8, 6.2, 10, s)                     # the sign gantry over the street
        B("sandblastedsign", -40.06, 4.2, -5, -39.94, 5.8, 5, s)                 # its board (thin)
        for x in (-56, -20):
            for z in (-9.5, 9.5):
                B("castiron", x, 0, z - 0.1, x + 0.2, 4.0, z + 0.1, s); B("lightblock", x - 0.2, 4.0, z - 0.3, x + 0.4, 4.25, z + 0.3, s)
                light(x, 3.6, z, 14, 0.6, s)
        # telegraph poles along the line out of town (cross-arms at 7 m: hooks)
        for (x, z) in ((-100, 8), (-68, -22), (-118, -8)):
            B("treebark", x - 0.2, 0, z - 0.2, x + 0.2, 8, z + 0.2, s); B("wideplanks", x - 0.1, 7, z - 1.2, x + 0.1, 7.2, z + 1.2, s)
        # the landmark outside the walls: the station's signal tower / the mine's headframe
        B("castiron" if s < 0 else "logcabin", -150, 0, 6, -146, 26 if s > 0 else 18, 10, s)
        B("rust", -151, 24 if s > 0 else 16, 5, -145, 25 if s > 0 else 17, 11, s)

    # ================= the square: the water tower (koth's hill under it) =================
    for (x, z) in ((-4.5, -4.5), (4, -4.5), (-4.5, 4), (4, 4)):
        k.box("wideplanks", x, 0, z, x + 0.5, 11.6, z + 0.5)
    k.box("scaffoldboards", -6.5, 11.6, -6.5, 6.5, 12, 6.5)
    k.disc("logcabin", 0, 0, 4.6, 12, 17, n=8)
    k.wedge("corrugated", 0, -4.6, 17, -4.6, 0, 19, 4.6); k.wedge("corrugated", 1, 0, 17, -4.6, 4.6, 19, 4.6)
    for (x0, z0, x1, z1) in ((-6.5, -6.5, 6.5, -6.38), (-6.5, 6.38, 6.5, 6.5), (-6.5, -6.38, -6.38, 6.38), (6.38, -6.38, 6.5, 6.38)):
        k.box("picketfence", x0, 12, z0, x1, 13, z1)                           # its walkway rail (thin)
    for sz in (1, -1):   # the two ladder posts (side streets: equal for both teams)
        z0, z1 = (6.5, 7.5) if sz > 0 else (-7.5, -6.5)
        k.box("wideplanks", -0.5, 0, z0, 0.5, 12, z1)
        k.ladder(0, 7.62 * sz, 0, 12, "n" if sz > 0 else "s")
    k.box("sandburiedroad", -12, -1, -12, 12, 0.02, -10); k.box("sandburiedroad", -12, -1, 10, 12, 0.02, 12)
    k.box("wideplanks", -10, 0, 9, -6, 0.9, 10.5); k.box("wideplanks", 6, 0, -10.5, 10, 0.9, -9)    # benches
    k.light(0, 10, 0, 18, 0.8); k.light(0, 14, 0, 14, 0.6)
    m.raw("hill -8 0 -8 8 4 8")

    # free for all: eight spawns (mirrored pairs)
    for (x, y, z) in ((-50, 0, 27), (-96, 0, -22), (-83, 8.3, 10), (-24, 4.0, 11)):
        for s in (-1, 1):
            k.spawn("any", X(x, s), y + 0.05, z, yaw_to(X(x, s), z, 0, 0))
    m.save("express", dress=False)
