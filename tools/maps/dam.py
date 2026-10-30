"""DAM (1.19.6): neutral payload at a hydroelectric dam in a pine valley, a morning in the hills, 256 x 136 m.
The cart starts in the gatehouse at the middle of the dam's crest (20 m up, the reservoir behind it). A (west) pushes
it east along the crest, down the east access road (a 45 m ramp, then a hairpin pad, then a 38 m ramp along the hill's
foot) to B's depot in the valley; B pushes it west down the other side to A's depot.
Each half is the other's mirror (x -> -x), so every timing is the same for both teams; only the light differs.
Ways onto a half's road: the road itself from the depot, a stair from the valley floor to the hairpin pad, the
maintenance ladder up the dam's face to the crest, and (for swimmers knocked into the reservoir) a ladder out of the
water. The gatehouse's control room (26 m, windows down both halves of the crest) is the perch over the cart's start
and the hill in koth. The valley floor below the dam (powerhouse, river, pines) is the flank between the depots."""
import random
from .prefabs import M, minus
from .bigkit import Kit, num, yaw_to

LID = 60.0
CREST = 20.0
HX, Z0, Z1 = 128, -40, 96          # the play area (rock and clip round it)
# the track, west half (east = the mirror): A's depot, the lower road (rising east), the hairpin pad, the upper road
# (rising north), the crest to the gatehouse
WEST = [(-80, 0, 55), (-42, 10, 55), (-37, 10, 55), (-37, 10, 50), (-37, CREST, 5), (-37, CREST, 0)]
PATH = WEST + [(0, CREST, 0)] + [(-x, y, z) for (x, y, z) in reversed(WEST)]


def build():
    m = M("Dam", "npayload", sun=(0.7, 0.62, 0.3))
    m.also = ["koth", "power"]
    m.raw("setting hydroelectric dam in a pine valley, morning")
    m.raw("palette base 0.55 0.56 0.56 accent 0.85 0.62 0.2 metal 0.4 0.42 0.44 glow 1.0 0.82 0.5 sky 0.66 0.74 0.8")
    m.raw("void -12")
    k = Kit(m)
    R = random.Random(1196)

    def X(x, s): return x if s < 0 else -x
    def B(mat, x0, y0, z0, x1, y1, z1, s):
        k.box(mat, X(x0, s), y0, z0, X(x1, s), y1, z1)
    def both(mat, x0, y0, z0, x1, y1, z1):
        for s in (-1, 1): B(mat, x0, y0, z0, x1, y1, z1, s)
    def W(mat, d, x0, y0, z0, x1, y1, z1, s):   # a wedge (d 0 +x, 1 -x, 2 +z, 3 -z: the way it rises)
        if s > 0 and d in (0, 1): d = 1 - d
        k.wedge(mat, d, X(x0, s), y0, z0, X(x1, s), y1, z1)
    def stairs(mat, x0, z0, x1, z1, top, axis, rising, s, base=0.0):
        k.stairs(mat, min(X(x0, s), X(x1, s)), z0, max(X(x0, s), X(x1, s)), z1, top, axis,
                 rising if (s < 0 or axis == "z") else -rising, base=base)
    def light(x, y, z, r, i, s): k.light(X(x, s), y, z, r, i)
    def clip(x0, z0, x1, z1, top, s): B("clip", x0, top, z0, x1, LID, z1, s)
    def pine(x, z, y, h, s):
        B("treebark", x - 0.35, y, z - 0.35, x + 0.35, y + h, z + 0.35, s)
        for j, (r, y0) in enumerate(((2.6, 0.35), (2.0, 0.55), (1.3, 0.75))):
            B("pineboughs", x - r, y + h * y0, z - r, x + r, y + h * y0 + h * 0.22, z + r, s)

    # ================= the ground, the river, the bounds =================
    k.box("forestfloor", -HX, -1, 5, HX, 0, 40)
    for s in (-1, 1):
        B("forestfloor", -HX, -1, 40, -5, 0, Z1, s)
        B("graniterock", -5, -3.2, 40, 0, -2.2, Z1, s)                        # the river bed
        B("kerbstones", -5.4, -2.2, 40, -5, 0, Z1, s)                         # its banks
    k.box("clearlagoon", -5, -2.2, 40, 5, -0.6, Z1)
    for (z0, z1) in ((60, 66),):
        k.box("wideplanks", -6, -0.35, z0, 6, 0, z1)                          # a footbridge
        k.box("wideplanks", -6, 0, z0, 6, 1.0, z0 + 0.12); k.box("wideplanks", -6, 0, z1 - 0.12, 6, 1.0, z1)
    # rock round the play area, clip over it
    k.box("graniterock", -HX - 12, 0, Z0 - 12, HX + 12, 34, Z0); k.box("clip", -HX - 12, 34, Z0 - 12, HX + 12, LID, Z0)
    k.box("graniterock", -HX - 12, 0, Z1, HX + 12, 24, Z1 + 12); k.box("clip", -HX - 12, 24, Z1, HX + 12, LID, Z1 + 12)
    for s in (-1, 1):
        B("graniterock", -HX - 12, 0, Z0, -HX, 30, Z1, s); clip(-HX - 12, Z0, -HX, Z1, 30, s)
    k.box("clip", -HX - 12, LID, Z0 - 12, HX + 12, LID + 2, Z1 + 12)

    # ================= the dam: the reservoir, the crest, the face =================
    k.box("graniterock", -44, 0, Z0, 44, 12, -5)
    k.box("deepocean", -44, 12, Z0, 44, 18, -5)
    k.box("waterstainedconcrete", -44, 0, -5, 44, CREST - 0.4, 5)
    for (a, b, c, d) in minus((-44, -5, 44, 5), [(-38, 3.5, -36, 5), (36, 3.5, 38, 5)]):   # (the track bed comes up through it)
        k.box("asphalt", a, CREST - 0.4, b, c, CREST, d)
    k.box("roadmarkings", -44, CREST, -0.1, -8, CREST + 0.01, 0.1); k.box("roadmarkings", 8, CREST, -0.1, 44, CREST + 0.01, 0.1)
    k.wedge("freshgreyconcrete", 3, -32, 0, 5, 32, CREST, 12.3)               # the downstream face (70 degrees)
    k.box("waterstainedconcrete", -32, 0, 11.2, 32, 3.0, 12.3)               # the toe (water-stained band)
    for s in (-1, 1):
        # the parapets (1.1 m: crouch cover), gaps for the ladders
        for (x0, x1) in ((-44, -11), (-9, -8)):
            B("boardformedconcrete", x0, CREST, -5, x1, CREST + 1.1, -4.4, s)
        for (x0, x1) in ((-32, -22), (-20, -8)):
            B("boardformedconcrete", x0, CREST, 4.4, x1, CREST + 1.1, 5, s)
        # the reservoir ladder (out of the water onto the crest)
        B("boardformedconcrete", -11.5, 12, -7.5, -8.5, 18.25, -5, s)          # a landing just out of the water
        k.ladder(X(-10, s), -5.12, 18.25, CREST, "s")
        # the maintenance tower on the face with its ladder from the valley floor
        B("castinplacewalls", -23, 0, 5, -19, CREST, 12.3, s)
        B("galvanizedgrating", -23, CREST - 0.05, 5, -19, CREST, 12.3, s)
        for (x0, x1) in ((-23, -22.9), (-19.1, -19)):
            B("powdercoatyellow", x0, CREST, 5, x1, CREST + 1.1, 12.3, s)
        B("powdercoatyellow", -23, CREST, 12.2, -22, CREST + 1.1, 12.3, s); B("powdercoatyellow", -20, CREST, 12.2, -19, CREST + 1.1, 12.3, s)
        k.ladder(X(-21, s), 12.42, 0, CREST, "n")
        light(-21, 3, 14, 10, 0.6, s)
        # the spillway gates' piers on the face (looks: a stepped band)
        for x in (-30, -26):
            k.wedge("weatheredconcrete", 3, X(x, s) - 0.6, 0, 5, X(x, s) + 0.6, CREST + 0.6, 12.9)

    # ================= the gatehouse (the middle: the cart's start, koth's hill, the control room) =================
    k.box("freshgreyconcrete", -8, 0, -7, 8, CREST, -5); k.box("freshgreyconcrete", -8, 0, 5, 8, CREST, 7)
    k.box("steeldeckplate", -7.5, CREST - 0.05, -6.5, 7.5, CREST, 6.5)
    for s in (-1, 1):
        # its end walls (a door 7 m wide onto the crest, control-room windows above), long walls
        B("castinplacewalls", -8, CREST, -7, -7.5, 30, -3.5, s); B("castinplacewalls", -8, CREST, 3.5, -7.5, 30, 7, s)
        B("castinplacewalls", -8, 25, -3.5, -7.5, 27, 3.5, s); B("castinplacewalls", -8, 29.2, -3.5, -7.5, 30, 3.5, s)
        # the stair to the control room
        stairs("galvanizedgrating", -7.5, -6.5, -1.5, -4, 26, "x", -1, s, base=CREST)
        B("powdercoatyellow", -7.5, CREST, -4, -1.5, 26, -3.9, s)
        B("controlpanel", -7.4, 26, 4.6, -3.5, 27.2, 6.4, s)                  # the control desks (cover up there)
        B("gaugecluster", -7.4, 27.2, 6.3, -3.5, 28.6, 6.4, s)
        B("electricalcabinet", -7.4, CREST, 4.8, -5.4, CREST + 2.4, 6.4, s)    # cabinets on the valve floor
        light(-4, CREST + 4, 0, 14, 0.8, s)
    for (z0, z1, wz) in ((-7, -6.5, (-6.5, -6.5)), (6.5, 7, (6.5, 6.5))):
        k.box("castinplacewalls", -7.5, CREST, z0, 7.5, 27, z1)
        k.box("castinplacewalls", -7.5, 29.2, z0, 7.5, 30, z1)
        for (x0, x1) in ((-7.5, -4.5), (-1.5, 1.5), (4.5, 7.5)):
            k.box("castinplacewalls", x0, 27, z0, x1, 29.2, z1)                # (windows between)
    # the upper floor (holes over the stairs), the roof
    k.box("steeldeckplate", -7.5, 25.5, -4, 7.5, 26, 6.5)
    k.box("steeldeckplate", -1.5, 25.5, -6.5, 1.5, 26, -4)
    k.box("corrugated", -8.5, 30, -7.5, 8.5, 30.4, 7.5)
    k.box("glow", -6, 25.3, -0.4, 6, 25.5, 0.4)
    k.light(0, 28.5, 0, 12, 0.7)
    m.raw(f"hill -6 {num(CREST)} -5 6 {num(CREST + 4)} 5")
    for (x, y, z) in PATH: m.raw(f"path {num(x)} {num(y)} {num(z)}")

    # ================= the crest's cover: the maintenance truck, the gantry crane, lamps =================
    for s in (-1, 1):
        B("powdercoatyellow", -28, CREST, 1.9, -23.5, CREST + 2.6, 4.3, s)     # the truck's box body
        B("rivetedplates", -23.5, CREST, 1.9, -21.4, CREST + 2.0, 4.3, s)      # its cab
        B("dustyglass", -22.3, CREST + 1.2, 2.0, -21.4, CREST + 1.9, 4.2, s)
        B("castiron", -27.6, CREST, 1.8, -26.8, CREST + 0.6, 1.9, s); B("castiron", -22.8, CREST, 1.8, -22, CREST + 0.6, 1.9, s)
        # the gantry crane over the intake gates: legs on the parapets, a girder each side, a bridge across
        for z0, z1 in ((-5, -4.4), (4.4, 5)):
            B("bridgegirder", -34.3, CREST, z0, -33.7, CREST + 8, z1, s); B("bridgegirder", -14.3, CREST, z0, -13.7, CREST + 8, z1, s)
            B("bridgegirder", -34.3, CREST + 8, z0, -13.7, CREST + 8.8, z1, s)
        B("powdercoatyellow", -21, CREST + 8, -4.4, -19, CREST + 8.8, 4.4, s)
        B("hazard", -20.5, CREST + 6.4, -0.5, -19.5, CREST + 8, 0.5, s)          # its hook block
        for x in (-40, -16):
            B("castiron", x, CREST, 4.5, x + 0.2, CREST + 6, 4.7, s)
            B("lightblock", x - 0.4, CREST + 6, 4.2, x + 0.6, CREST + 6.3, 4.8, s)
            light(x, CREST + 5, 3, 16, 0.75, s)

    # ================= the access roads (per half) =================
    for s in (-1, 1):
        # the upper road: from the crest down the hill's foot to the hairpin pad (10 m over 45 m)
        B("retainingwallblocks", -42, 0, 5, -32, 10, 50, s)
        W("asphalt", 3, -42, 10, 5, -38, CREST, 50, s); W("asphalt", 3, -36, 10, 5, -32, CREST, 50, s)
        # the track bed: a 2 m rail cutting down the middle of each ramp, stepped so the cart's box never meets a floor
        # (its bottom rides the track line; each 1 m step's top is the line's lowest over the cart's reach)
        def yu(z): return min(CREST, max(10.0, 10 + (50 - z) / 45 * 10))
        def yl(x): return min(10.0, max(0.0, (x + 80) / 38 * 10))
        z = 3.5
        while z < 50:
            z1 = min(50, z + 1); B("railwaysleepers", -38, 0, z, -36, yu(z1 + 1.3) - 0.01, z1, s); z = z1
        x = -80.0
        while x < -40.5:
            x1 = min(-40.5, x + 1); t = yl(x - 1.3) - 0.01
            if t > 0.02: B("railwaysleepers", x, 0, 54, x1, t, 56, s)
            x = x1
        B("retainingwallblocks", -32.6, 0, 5, -32, 11.1, 50, s)                # its guard wall over the valley
        W("retainingwallblocks", 3, -32.6, 11.1, 5, -32, CREST + 1.1, 50, s)
        # the hairpin pad, its guard walls (a gap for the valley stair)
        for (a, b, c, d) in minus((-42, 50, -32, 60), [(-42, 54, -40.5, 56)]): B("asphalt", a, 0, b, c, 10, d, s)
        B("roadmarkings", -42, 10, 59, -32.6, 10.01, 59.2, s)
        B("retainingwallblocks", -32.6, 10, 54, -32, 11.1, 60, s)
        B("retainingwallblocks", -42, 10, 59.4, -32.6, 11.1, 60, s)
        # the lower road: from the pad down to the depot (10 m over 38 m), guard wall on the valley side
        W("asphalt", 0, -80, 0, 50, -42, 10, 54, s); W("asphalt", 0, -80, 0, 56, -42, 10, 60, s)
        B("retainingwallblocks", -80, 0, 59.4, -42, 1.1, 60, s)
        W("retainingwallblocks", 0, -80, 1.1, 59.4, -42, 11.1, 60, s)
        # the stair from the valley floor up to the pad
        stairs("boardformedconcrete", -32, 54, -27, 74, 10, "z", -1, s)
        B("boardformedconcrete", -32, 0, 50, -27, 10, 54, s)
        B("rivetedplates", -27.1, 0, 50, -27, 11.1, 74, s)                     # its rail (thin: shoot-through)
        # road lamps
        for (x, z) in ((-32.3, 18), (-32.3, 38), (-56, 59.7), (-72, 59.7)):
            y = CREST + 1.1 - (z - 5) / 45 * 10 if x > -33 else 1.1 + (x + 80) / 38 * 10
            B("castiron", x - 0.1, y, z - 0.1, x + 0.1, y + 4.5, z + 0.1, s)
            B("lightblock", x - 0.4, y + 4.5, z - 0.4, x + 0.4, y + 4.8, z + 0.4, s)
            light(x, y + 4, z, 16, 0.75, s)
        # the hills (rock, clipped tops): inside the hairpin, behind the crest, north of the depot
        B("graniterock", -80, 0, 5, -42, 24, 50, s); clip(-80, 5, -42, 50, 24, s)
        B("graniterock", -HX, 0, Z0, -44, 30, 5, s); clip(-HX, Z0, -44, 5, 30, s)
        B("graniterock", -HX, 0, 5, -80, 24, 30, s); clip(-HX, 5, -80, 30, 24, s)
        W("scree", 3, -80, 0, 30, -96, 3, 34, s)                              # (a scree apron at the hill's foot)
        for (x, z, y) in ((-70, 15, 24), (-55, 30, 24), (-64, 42, 24), (-100, 15, 24), (-115, 22, 24), (-90, -20, 30), (-60, -10, 30)):
            pine(x, z, y, R.uniform(7, 10), s)

    # ================= the depots (A west, B east) =================
    for s in (-1, 1):
        B("castinplacewalls", -122, 0, 40, -120, 9, 70, s)                     # the back wall
        for (z0, z1) in ((40, 42), (68, 70)):
            B("castinplacewalls", -120, 0, z0, -98, 7, z1, s)                  # side walls
        B("corrugated", -122, 7, 40, -96, 7.4, 70, s)                          # the shed's roof
        for z in (42, 68):
            B("castiron", -96.4, 0, z - 0.4 if z > 50 else z, -96, 7, z if z > 50 else z + 0.4, s)
        B("woodencrate", -104, 0, 43, -101, 2, 46, s); B("woodencrate", -104, 0, 64, -101, 2, 67, s)
        B("gabionwall", -86, 0, 46.5, -82, 1.2, 48.5, s); B("gabionwall", -86, 0, 61.5, -82, 1.2, 63.5, s)
        light(-108, 6, 55, 18, 0.8, s)
        for z in (49, 53, 57, 61):
            k.spawn("a" if s < 0 else "b", X(-112, s), 0.05, z, -90 if s < 0 else 90)

    # ================= the valley floor: the powerhouse, cover, pines =================
    k.box("castinplacewalls", -14, 0, 16, 14, 12, 34); k.box("clip", -14, 12, 16, 14, LID, 34)
    k.box("corrugated", -14.5, 12, 15.5, 14.5, 12.4, 34.5)
    k.box("waterstainedconcrete", -5, -0.4, 34, 5, 0, 40)                      # the tailrace apron
    k.box("rivetedplates", -3, 1, 33.9, 3, 5, 34, )                            # the outlet's gate
    for s in (-1, 1):
        for x0 in (-13, -6):
            B("glow", x0, 7, 34, x0 + 3, 9, 34.05, s)                          # lit windows
        B("electricalcabinet", -22, 0, 26, -18, 3, 30, s)                      # a transformer
        B("gabionwall", -46, 0, 70, -40, 1.6, 72, s)
        B("graniterock", -60, 0, 84, -55, 2.4, 88, s); B("graniterock", -96, 0, 80, -92, 1.8, 83, s)
        B("graniterock", -20, 0, 76, -15, 2.2, 80, s)
        for (x, z) in ((-62, 72), (-108, 88), (-24, 90), (-46, 86), (-84, 92), (-18, 46)):
            pine(x, z, 0, R.uniform(8, 11), s)
        light(-18, 4, 20, 14, 0.6, s); light(-50, 4, 78, 18, 0.5, s); light(-100, 4, 84, 16, 0.5, s)

    # free for all: eight spawns (mirrored pairs)
    for (x, y, z) in ((-60, 0, 80), (-22, 0, 58), (-27, CREST, -2.4), (-110, 0, 88)):
        for s in (-1, 1):
            k.spawn("any", X(x, s), y + 0.05, z, yaw_to(X(x, s), z, 0, 40))
    m.save("dam", dress=False)
