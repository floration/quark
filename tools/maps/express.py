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
hill underneath.
Fix round 2 (q's review answers, 2026-10-30): street cover (a freight wagon on a siding, crates and barrels, a water
stand by the square whose spout and a hanging freight board hang over the line under eye height so the 150 m line
breaks, the stagecoach skirted, the trough 1.3 m, the bank's / jail's porch a solid 1.3 m wall); the walkway's side
street edges a solid parapet (it still sees down the main street); ladders to the saloon's and hotel's roofs, the hotel
three storeys and the store one; the canyon gap 26 m long along the bend with stepped rims to 15.5 m, scree, varnish
and a curved arch; the yard pulled in 20 m (clip at 115); a corral, a wagon and a windmill on the flanks, alley crates,
telegraph poles every ~16 m with wires, warm lamp caps."""
import random
from .prefabs import M
from .bigkit import Kit, num, yaw_to

LID = 50.0
HX, HZ = 115, 45
TY = 0.06            # the track's line: the cart rides just over the sleepers (their tops 0.05)
WEST = [(-108, 0), (-104, 0), (-92, -12), (-74, -12), (-62, 0)]
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
        B("wideplanks", -115, 0, 4, -104, 1.0, 9, s)                          # the platform / the loading dock
        for x in (-114, -109, -104.4):
            B("castiron" if s < 0 else "logcabin", x, 1.0, 8.4, x + 0.4, 6, 8.8, s)
        B("corrugated" if s < 0 else "scaffoldboards", -115, 6, 3.5, -104, 6.3, 10, s)    # its canopy
        stairs("wideplanks", -107, 2, -104, 4, 1.0, "z", 1, s)                # steps off its track side
        # the station building / the mine office: a hall with a door front (platform) and back (alley)
        f = "wideplanks" if s < 0 else "mudandstrawdaub"
        B(f, -115, 0, 12, -101, 0.3, 26, s)
        for (x0, x1) in ((-115, -110), (-106, -101)):
            B(f, x0, 0, 12, x1, 7, 12.3, s)                                   # the front (door 4 m)
        B(f, -110, 3.2, 12, -106, 7, 12.3, s)
        B(f, -115, 0, 25.7, -105, 7, 26, s); B(f, -105, 3.2, 25.7, -101, 7, 26, s)   # the back (door at the east end)
        B(f, -101.3, 0, 12.3, -101, 7, 25.7, s)
        B("corrugated" if s < 0 else "rust", -115, 7, 11.5, -100.5, 7.4, 26.5, s)
        B("sandblastedsign", -112, 7.4, 12, -104, 9.4, 12.12, s)               # the name board (thin)
        B("woodencrate", -114, 0.3, 22, -112, 1.8, 24, s); B("woodencrate", -104, 0.3, 14, -102, 1.4, 15.6, s)
        light(-108, 5, 19, 14, 0.7, s); light(-109, 5, 6.5, 16, 0.75, s)
        # the freight shed / the powder shed (doors north, onto the yard, and east)
        f = "corrugated" if s < 0 else "rustedsheetfence"
        B("wideplanks", -115, 0, -25, -104, 0.3, -14, s)
        B(f, -115, 0, -14.3, -112, 6, -14, s); B(f, -108, 0, -14.3, -104, 6, -14, s); B(f, -112, 3.4, -14.3, -108, 6, -14, s)
        B(f, -115, 0, -25, -104, 6, -24.7, s)
        B(f, -104.3, 0, -24.7, -104, 6, -22, s); B(f, -104.3, 0, -18, -104, 6, -14.3, s); B(f, -104.3, 3.4, -22, -104, 6, -18, s)
        B("corrugated", -115, 6, -25.5, -103.5, 6.3, -13.5, s)
        B("woodencrate", -114, 0.3, -23.5, -111, 2.4, -20.5, s); B("haybales" if s < 0 else "barricade", -109, 0.3, -24, -106, 1.5, -22, s)
        light(-109, 5, -19, 12, 0.6, s)
        # spawns in the yard, facing down the line
        for z in (-4.5, -2.5, -0.5, 1.5):   # (north of the canyon gap's line: the north rim hides the far yard)
            k.spawn("a" if s < 0 else "b", X(-111, s), 0.05, z, -90 if s < 0 else 90)
        B("woodencrate", -113, 0, -12, -110, 2.2, -9, s); B("woodencrate", -113, 2.2, -11.5, -111, 3.4, -9.5, s)

        # ================= the canyon gap (x -102..-76, along the track's bend): stepped rims, the arch =================
        # each rim: a trail ledge at 8.3 along the gap, the rock stepping back behind it to 12 and 15.5 (clipped)
        for (x0, x1, z0, z1, inner) in ((-102, -92, 2, 30, 2), (-92, -76, -6, 30, -6),              # north
                                        (-102, -92, -HZ, -16, -16), (-92, -76, -HZ, -18, -18)):     # south
            B("redcanyonrock", x0, 0, z0, x1, 8, z1, s)
            B("badlands", x0, 8, z0, x1, 8.3, z1, s)
            sg = 1 if inner == z0 else -1                                      # (+1: the rim is north of the gap)
            for (back, top) in ((4.5, 12), (9, 15.5)):
                za, zb = (inner + sg * back, z1) if sg > 0 else (z0, inner + sg * back)
                xa, xb = x0 + (0.8 if top > 12 else 0), x1 - (0.6 if top > 12 else 0)
                if x1 == -76 and sg > 0: xb = -80                              # (a strip at 8.3 where the town's stair lands)
                if x0 == -102 and sg < 0: xa = -98                             # (and where the yard's stair lands)
                B("redcanyonrock", xa, 8.3, za, xb, top, zb, s)
                B("clip", xa, top, za, xb, LID, zb, s)
            # desert varnish streaks down the gap face and scree at its foot
            for xs in (x0 + 1.5, (x0 + x1) / 2, x1 - 3):
                zf0, zf1 = (inner - 0.05, inner) if sg > 0 else (inner, inner + 0.05)
                B("desertvarnish", xs, 0.8, zf0, xs + 1.2 + (xs % 3) * 0.4, 7.6, zf1, s)
            W("scree", 2 if sg > 0 else 3, x0 + 1, 0, inner - 1.6 if sg > 0 else inner, x1 - 1, 1.4, inner if sg > 0 else inner + 1.6, s)
        B("crossbeddedsandstone", -85, 7, -18, -81, 8.3, -6, s)                # the arch (walkable: rim to rim)
        for (zw, d, zs) in ((-18, 2, 1), (-6, 3, -1)):                         # its curved underside: two corner pieces a side
            k.wedge("crossbeddedsandstone", d, X(-85, s), 4.0, zw, X(-81, s), 7, zw + zs * 2.2, up=1)
            k.wedge("crossbeddedsandstone", d, X(-85, s), 6.2, zw + zs * 2.2, X(-81, s), 7, zw + zs * 4.4, up=1)
        stairs("crossbeddedsandstone", -76, 18.5, -60, 21.5, 8.3, "x", -1, s)  # up the north rim from the edge of town
        stairs("crossbeddedsandstone", -112, -30, -102, -27, 8.3, "x", 1, s)   # up the south rim from the yard
        B("boulderfield", -74, 0, -24, -70, 1.8, -20, s)
        B("boulderfield", -72, 0, -3, -69, 1.9, 0, s)                          # edge-of-town cover
        B("woodencrate", -64, 0, -7.5, -61.5, 1.8, -5, s); B("woodencrate", -106.5, 0, -6.5, -104, 1.6, -4, s)
        k.disc("rust", X(-97.5, s), 0.9, 0.6, 0, 1.3, n=8); k.disc("rust", X(-96.2, s), 0.6, 0.6, 0, 1.3, n=8)   # (barrels in the gap)
        for (x, z, h) in ((-66, 26, 3.0), (-70, -32, 2.8), (-106, -10, 2.6)):
            B("cactus", x - 0.3, 0, z - 0.3, x + 0.3, h, z + 0.3, s)
            B("cactus", x + 0.3, 1.2, z - 0.2, x + 1.0, 1.6, z + 0.2, s); B("cactus", x + 0.7, 1.6, z - 0.2, x + 1.0, h - 0.6, z + 0.2, s)
        light(-83, 4, -12, 16, 0.6, s); light(-97, 4, -6, 14, 0.5, s)
        # flank pieces round the edge of town: a corral (solid log rails, gaps), a wrecked wagon and a windmill on the dunes
        for (x0, x1) in ((-69, -66), (-64.5, -62)):
            B("logcabin", x0, 0, -27.3, x1, 1.2, -27, s)
        B("logcabin", -69, 0, -27, -68.7, 1.2, -22, s); B("logcabin", -62.3, 0, -27, -62, 1.2, -24, s)
        B("cratewood", -40, 0, 32.5, -35, 2.4, 35, s); B("castiron", -40.3, 0, 32.3, -39.5, 1.0, 35.2, s)
        for (px, pz) in ((-84, 34), (-81, 34), (-84, 37), (-81, 37)):
            B("treebark", px, 0, pz, px + 0.3, 10, pz + 0.3, s)
        B("scaffoldboards", -84.5, 10, 33.5, -80.2, 10.3, 37.8, s)
        B("rust", -83, 10.3, 35.2, -82, 11.6, 36.2, s)
        B("corrugated", -82.6, 11.6, 33, -82.4, 15, 38.6, s); B("corrugated", -82.6, 12.4, 35.7, -82.4, 14.2, 36.7, s)

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
                # storeys: the store a single one (roof 3.9), the hotel three (roof 11.8), the others two (roof 8.3)
                top = 3.6 if (side > 0 and i == 1) else 11.5 if (side > 0 and i == 2) else 8
                if top > 3.6: B(f, x0, 3.6, Z(zf, zb)[0], x1, top, Z(zf, zb)[1], s)   # the upper storeys (solid)
                B(f, x0 + 1, top, Z(zf, zf + side * 0.12)[0], x1 - 1, top + 2, Z(zf, zf + side * 0.12)[1], s)   # the false front
                B("corrugated", x0, top, Z(zf + side * 0.12, zb)[0], x1, top + 0.3, Z(zf + side * 0.12, zb)[1], s)
                if top > 8:
                    for wx in (x0 + 2, x1 - 4):
                        B("glow", wx, 8.6, *Z(zf, zf - side * 0.04)[0:1], wx + 2, 10.2, Z(zf, zf - side * 0.04)[1], s)
                if side > 0 and i in (0, 2):                                    # a ladder up the back to the roof
                    k.ladder(X(x0 + 3, s), zb + 0.12, 0, top + 0.3, "n")
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
            if side < 0:   # the bank's / jail's porch: a solid 1.3 m stone wall along the boardwalk (a gap at its door)
                B("sandstoneashlar", -26, 0.25, -10.3, -20.2, 1.3, -10, s); B("sandstoneashlar", -17.8, 0.25, -10.3, -12.3, 1.3, -10, s)
            # crates and barrels in the back alley (cover on the flank, a lane left clear)
            for (cx, cz) in ((-55, 26.2), (-33, 27.2), (-18, 25.6)):
                B("woodencrate", cx, 0, *Z(side * cz, side * (cz + 1.5))[0:1], cx + 1.6, 1.5, Z(side * cz, side * (cz + 1.5))[1], s)
            k.disc("rust", X(-47, s), side * 27.4, 0.55, 0, 1.25, n=8)
            # the back alley's fence (gaps onto the dunes)
            za = 30 * side
            for (x0, x1) in ((-104, -86), (-74, -50), (-40, -6)):
                B("rustedsheetfence", x0, 0, *Z(za, za + side * 0.12)[0:1], x1, 2.0, Z(za, za + side * 0.12)[1], s)
        # the street's cover: a stagecoach, a water trough, hitching rails, the sign gantry, lamps
        B("logcabin", -36, 0.7, -7, -30, 2.6, -3.6, s)                         # the stagecoach's body
        B("charredtimber", -35.6, 0, -6.7, -30.4, 0.7, -3.9, s)                # its solid skirt (no legs showing)
        for x in (-35.5, -31):
            B("castiron", x, 0, -7.2, x + 0.5, 1.2, -6.9, s); B("castiron", x, 0, -3.7, x + 0.5, 1.2, -3.4, s)
        B("wideplanks", -30, 0, 7.2, -26, 1.3, 8.6, s)                          # the trough (1.3 m: cover)
        B("clearlagoon", -29.8, 1.0, 7.4, -26.2, 1.25, 8.4, s)
        # a freight wagon on a siding beside the track (solid to the ground), crates, the water stand over the line
        B("railwaysleepers", -56, -0.5, 2.0, -42, 0.04, 4.8, s)
        B("castiron", -54, 0, 1.9, -44, 0.7, 4.9, s)
        B("cratewood" if s < 0 else "rustedsheet", -54.5, 0.7, 1.8, -43.5, 2.8, 5.1, s)
        B("corrugated", -54.7, 2.8, 1.7, -43.3, 3.0, 5.2, s)
        B("woodencrate", -26, 0, 5.4, -23, 1.6, 8.8, s); B("woodencrate", -25.6, 1.6, 5.8, -23.4, 2.6, 8.2, s)   # by the trough
        B("woodencrate", -22, 0, -8.6, -19, 1.6, -5.4, s); B("woodencrate", -21.6, 1.6, -8.2, -19.6, 2.6, -6.2, s)
        k.disc("rust", X(-17.6, s), -7.2, 0.6, 0, 1.3, n=8); k.disc("rust", X(-17.6, s), -5.9, 0.6, 0, 1.3, n=8)
        B("sandstoneashlar", -16.5, 0, 2, -12.5, 1.6, 5.6, s)                  # the water stand: a solid base,
        for (lx, lz) in ((-16.3, 2.2), (-13, 2.2), (-16.3, 5.1), (-13, 5.1)):
            B("wideplanks", lx, 1.6, lz, lx + 0.3, 3.6, lz + 0.3, s)          # legs,
        k.disc("logcabin", X(-14.5, s), 3.8, 2.0, 3.6, 6.4, n=8)               # the tank,
        B("castiron", -15, 1.58, -1.6, -14, 2.6, 2.0, s)                       # and its spout arm over the track
        B("castiron", -15, 2.6, -0.2, -14, 3.8, 2.0, s)
        for (x0, z0) in ((-64, 7.2), (-16, -7.6)):
            B("wideplanks", x0, 0.9, z0, x0 + 6, 1.05, z0 + 0.12, s)
            B("wideplanks", x0, 0, z0, x0 + 0.15, 0.9, z0 + 0.12, s); B("wideplanks", x0 + 5.85, 0, z0, x0 + 6, 0.9, z0 + 0.12, s)
        for z0, z1 in ((-10, -9.6), (9.6, 10)):
            B("wideplanks", -40.2, 0, z0, -39.8, 6.2, z1, s)
        B("wideplanks", -40.2, 5.8, -10, -39.8, 6.2, 10, s)                     # the sign gantry over the street
        B("sandblastedsign", -40.06, 4.2, -5, -39.94, 5.8, 5, s)                 # its board (thin)
        B("cratewood", -40.4, 1.58, -1.6, -39.6, 3.2, 1.6, s)                   # a hanging freight board (the cart passes under)
        B("castiron", -40.05, 3.2, -1.2, -39.95, 5.8, -1.1, s); B("castiron", -40.05, 3.2, 1.1, -39.95, 5.8, 1.2, s)
        for x in (-56, -20):
            for z in (-9.5, 9.5):
                B("castiron", x, 0, z - 0.1, x + 0.2, 4.0, z + 0.1, s); B("glow", x - 0.2, 4.0, z - 0.3, x + 0.4, 4.25, z + 0.3, s)
                light(x, 3.6, z, 14, 0.6, s)
        # telegraph poles along the line out of town (cross-arms at 7 m: hooks)
        poles = [(-110, -7), (-97, -12), (-80, -16.5), (-64, -9), (-46, -9.6)]
        for (x, z) in poles:
            B("treebark", x - 0.2, 0, z - 0.2, x + 0.2, 8, z + 0.2, s); B("wideplanks", x - 0.1, 7, z - 1.2, x + 0.1, 7.2, z + 1.2, s)
        for (a, b) in zip(poles, poles[1:]):                                   # the wires (high over everything)
            for dz in (-1.0, 1.0):
                k.bar("castiron", (X(a[0], s), a[1] + dz), (X(b[0], s), b[1] + dz), 0.04, 7.2, 7.24)
        # the landmark outside the walls: the station's signal tower / the mine's headframe
        B("castiron" if s < 0 else "logcabin", -124, 0, 6, -120, 26 if s > 0 else 20, 10, s)
        B("rust", -125, 24 if s > 0 else 18, 5, -119, 25 if s > 0 else 19, 11, s)

    # ================= the square: the water tower (koth's hill under it) =================
    for (x, z) in ((-4.5, -4.5), (4, -4.5), (-4.5, 4), (4, 4)):
        k.box("wideplanks", x, 0, z, x + 0.5, 11.6, z + 0.5)
    k.box("scaffoldboards", -6.5, 11.6, -6.5, 6.5, 12, 6.5)
    k.disc("logcabin", 0, 0, 4.6, 12, 17, n=8)
    k.wedge("corrugated", 0, -4.6, 17, -4.6, 0, 19, 4.6); k.wedge("corrugated", 1, 0, 17, -4.6, 4.6, 19, 4.6)
    for (x0, z0, x1, z1) in ((-6.5, -6.38, -6.38, 6.38), (6.38, -6.38, 6.5, 6.38)):
        k.box("picketfence", x0, 12, z0, x1, 13, z1)                           # its walkway rail down the street (thin)
    for sz in (1, -1):   # solid 1.2 m parapets to the side streets (gaps at the ladders)
        za, zb = (6.2, 6.5) if sz > 0 else (-6.5, -6.2)
        k.box("logcabin", -6.5, 12, za, -0.6, 13.2, zb); k.box("logcabin", 0.6, 12, za, 6.5, 13.2, zb)
    for sz in (1, -1):   # the two ladder posts (side streets: equal for both teams)
        z0, z1 = (6.5, 7.5) if sz > 0 else (-7.5, -6.5)
        k.box("wideplanks", -0.5, 0, z0, 0.5, 12, z1)
        k.ladder(0, 7.62 * sz, 0, 12, "n" if sz > 0 else "s")
    k.box("sandburiedroad", -12, -1, -12, 12, 0.02, -10); k.box("sandburiedroad", -12, -1, 10, 12, 0.02, 12)
    k.box("wideplanks", -10, 0, 9, -6, 0.9, 10.5); k.box("wideplanks", 6, 0, -10.5, 10, 0.9, -9)    # benches
    k.light(0, 10, 0, 18, 0.8); k.light(0, 14, 0, 14, 0.6)
    m.raw("hill -8 0 -8 8 4 8")

    # free for all: eight spawns (mirrored pairs)
    for (x, y, z) in ((-50, 0, 27), (-70, 0, -14), (-84, 8.3, -3.5), (-24, 4.0, 11)):
        for s in (-1, 1):
            k.spawn("any", X(x, s), y + 0.05, z, yaw_to(X(x, s), z, 0, 0))
    m.save("express", dress=False)
