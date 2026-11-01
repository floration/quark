"""DAM (1.19.6): neutral payload at a hydroelectric dam in a pine valley, a morning in the hills, 140 x 116 m.
The cart starts in the gatehouse at the middle of the dam's crest (20 m up, the reservoir behind it). A (west) pushes
it east along the crest and down the east access road (a 45 m ramp) to B's depot on the hairpin pad (10 m up); B
pushes it west down the other side to A's depot. Each team spawns in the shed on its own pad.
Each half is the other's mirror (x -> -x), so every timing is the same for both teams; only the light differs.
Ways onto the crest from a pad, per half: the road (the cart's lane, with two bays cut in its rock wall for cover);
the stair path up a slot in the hill behind it (three flights, out onto the crest's far end); the valley stair down to
the floor and the stair tower on the dam's face up to the crest; the face itself, which stays hookable (a grapple from
the valley floor anywhere in its middle 64 m); and, for swimmers knocked into the reservoir, the gatehouse's stair out
of the water. The gatehouse's control room (26 m, windows down both halves of the crest) is the perch over the cart's
start and the hill in koth; a valve housing on its centreline (the cart runs through its arch) breaks the crest's
end-to-end line. The valley below the dam (powerhouse, spillways, river, pines) is the flank between the pads.
Fix round 2 (q's review answers, 2026-10-30): rebuilt smaller (the lower road and the big valley floor dropped, the
depots on the hairpin pads: track 2 x 99 m); the stair path and the face's stair tower added as walkable flanks; the
valve housing, stoplog stacks, 1.3 m parapets and the road's bays as cover; the sawtooth rail cutting replaced by one
bed of 0.5 m steps 0.27-0.38 m under the line with rails beside the cart; the crane's beams clipped (no grapple perch); water-stain streaks,
spillway chutes, penstock pipes, rock ledges, scree and tiered pines; a deeper sky (the haze is the sky's colour)."""
import random
from .prefabs import M, minus
from .bigkit import Kit, num, yaw_to

LID = 60.0
CREST = 20.0
PAD = 10.0
HX, Z0, Z1 = 70, -40, 76            # the play area (rock and clip round it)
# the track, west half (east = the mirror): A's depot on the pad, the road (rising north), the crest to the gatehouse
WEST = [(-37, PAD, 64), (-37, PAD, 50), (-37, CREST, 5), (-37, CREST, 0)]
PATH = WEST + [(0, CREST, 0)] + [(-x, y, z) for (x, y, z) in reversed(WEST)]


def build():
    m = M("Dam", "npayload", sun=(0.7, 0.62, 0.3))
    m.also = ["koth", "power"]
    m.raw("setting hydroelectric dam in a pine valley, morning")
    m.raw("palette base 0.55 0.56 0.56 accent 0.85 0.62 0.2 metal 0.4 0.42 0.44 glow 1.0 0.82 0.5 sky 0.5 0.62 0.76")
    m.raw("void -12")
    k = Kit(m)
    R = random.Random(1196)

    def X(x, s): return x if s < 0 else -x
    def B(mat, x0, y0, z0, x1, y1, z1, s):
        k.box(mat, X(x0, s), y0, z0, X(x1, s), y1, z1)
    def W(mat, d, x0, y0, z0, x1, y1, z1, s):   # a wedge (d 0 +x, 1 -x, 2 +z, 3 -z: the way it rises)
        if s > 0 and d in (0, 1): d = 1 - d
        k.wedge(mat, d, X(x0, s), y0, z0, X(x1, s), y1, z1)
    def stairs(mat, x0, z0, x1, z1, top, axis, rising, s, base=0.0):
        k.stairs(mat, min(X(x0, s), X(x1, s)), z0, max(X(x0, s), X(x1, s)), z1, top, axis,
                 rising if (s < 0 or axis == "z") else -rising, base=base)
    def light(x, y, z, r, i, s): k.light(X(x, s), y, z, r, i)
    def clip(x0, z0, x1, z1, top, s): B("clip", x0, top, z0, x1, LID, z1, s)
    def pine(x, z, y, h, s):
        """a pine from tiered octagons (a stepped cone) on a bark trunk"""
        xx = X(x, s)
        k.box("pinebark", xx - 0.3, y, z - 0.3, xx + 0.3, y + h * 0.45, z + 0.3)
        for j in range(5):
            r = 2.9 - j * 0.55; y0 = y + h * (0.22 + 0.15 * j)
            k.disc("pineboughs", xx, z, r, y0, y0 + h * 0.2, n=8)
    def road_y(z): return PAD + (50 - z) / 45 * (CREST - PAD)

    # ================= the ground, the river, the bounds =================
    for (x0, x1) in ((-32, -16), (-16, -5), (5, 16), (16, 32)):
        k.box("forestfloor", x0, -1, 40, x1, 0, 58); k.box("forestfloor", x0, -1, 58, x1, 0, Z1)
    for (x0, x1) in ((-32, -16), (-16, 0), (0, 16), (16, 32)):
        for (z0, z1) in ((12.3, 26), (26, 40)):
            k.box("forestfloor", x0, -1, z0, x1, 0, z1)
    for s in (-1, 1):
        B("graniterock", -5, -3.2, 40, 0, -2.2, Z1, s)                        # the river bed
        B("kerbstones", -5.4, -2.2, 40, -5, 0, Z1, s)                         # its banks
    k.box("clearlagoon", -5, -2.2, 40, 5, -0.6, Z1)
    k.box("wideplanks", -6, -0.35, 60, 6, 0, 66)                              # a footbridge
    k.box("wideplanks", -6, 0, 60, 6, 1.0, 60.12); k.box("wideplanks", -6, 0, 65.88, 6, 1.0, 66)
    # rock round the play area, clip over it
    k.box("graniterock", -HX - 12, 0, Z0 - 12, HX + 12, 34, Z0); k.box("clip", -HX - 12, 34, Z0 - 12, HX + 12, LID, Z0)
    k.box("graniterock", -HX - 12, 0, Z1, HX + 12, 24, Z1 + 12); k.box("clip", -HX - 12, 24, Z1, HX + 12, LID, Z1 + 12)
    for s in (-1, 1):
        B("graniterock", -HX - 12, 0, Z0, -HX, 30, Z1, s); clip(-HX - 12, Z0, -HX, Z1, 30, s)
    k.box("clip", -HX - 12, LID, Z0 - 12, HX + 12, LID + 2, Z1 + 12)

    # ================= the dam: the reservoir, the crest, the face =================
    k.box("graniterock", -44, 0, Z0, 44, 12, -5)
    k.box("deepocean", -44, 12, Z0, 44, 18, -5)
    k.box("waterstainedconcrete", -50, 0, -5, 50, CREST - 0.4, 5)
    for x in range(-50, 50, 10):   # (in 10 m pieces: the bots' nav costs a route from each surface's centre)
        for (a, b, c, d) in minus((x, -5, x + 10, 5), [(-38, 3.5, -36, 5), (36, 3.5, 38, 5)]):   # (the bed comes up through it)
            k.box("asphalt", a, CREST - 0.4, b, c, CREST, d)
    k.box("roadmarkings", -50, CREST, -0.1, -8, CREST + 0.01, 0.1); k.box("roadmarkings", 8, CREST, -0.1, 50, CREST + 0.01, 0.1)
    k.wedge("freshgreyconcrete", 3, -32, 0, 5, 32, CREST, 12.3)               # the downstream face (70 degrees)
    k.box("waterstainedconcrete", -32, 0, 11.2, 32, 3.0, 12.3)               # the toe (water-stained band)
    for s in (-1, 1):
        # the parapets (1.3 m: cover for a crouched player), gaps for the stairs, the road and the path
        for (x0, x1) in ((-50, -11.5), (-8.5, -8)):
            B("boardformedconcrete", x0, CREST, -5, x1, CREST + 1.3, -4.4, s)
        for (x0, x1) in ((-32, -24), (-16, -8)):
            B("boardformedconcrete", x0, CREST, 4.4, x1, CREST + 1.3, 5, s)
        B("boardformedconcrete", -50, CREST, -4.4, -49.4, CREST + 1.3, 5, s)  # the crest's far end
        # the gatehouse's reservoir stair: out of the water onto the crest (8 m of steps, most of them under water)
        stairs("boardformedconcrete", -11.5, -13, -8.5, -5, CREST, "z", 1, s, base=12)
        for xx in (-11.6, -8.5):
            B("powdercoatyellow", xx, 18, -13, xx + 0.1, CREST + 1.3, -5, s)
        # the spillway chutes between the piers (waterfall look, foam at the toe) and the piers themselves
        for x in (-30, -26):
            k.wedge("weatheredconcrete", 3, X(x, s) - 0.6, 0, 5, X(x, s) + 0.6, CREST + 0.6, 12.9)
        W("waterfall", 3, -29.4, 0, 5.08, -26.6, CREST - 1, 12.38, s)
        B("seafoam", -29.4, 0, 12.3, -26.6, 0.02, 20, s)
        # water-stain streaks down the face under each gate (thin, just proud of it)
        for x in (-22.8, -12, -6.5):
            W("waterstainedconcrete", 3, x, 0, 5.06, x + 1.4, CREST - 0.2, 12.36, s)
        # the penstock: a pipe down the face into the powerhouse
        W("rustedsheet", 3, -10.8, 0, 5.4, -9.2, 18, 13.9, s)
        B("rustedsheet", -10.8, 0.2, 12.3, -9.2, 1.8, 16, s)
        B("boardformedconcrete", -11.2, 0, 14.4, -8.8, 0.8, 15.2, s)          # its saddle

    # ================= the face's stair tower (per half): valley floor to the crest, two flights =================
    for s in (-1, 1):
        B("castinplacewalls", -24, 0, 5, -16, CREST - 0.4, 12.3, s)            # its core against the face
        B("galvanizedgrating", -24, CREST - 0.4, 5, -16, CREST, 15, s)         # the top landing (onto the crest)
        stairs("galvanizedgrating", -24, 15, -20, 25, PAD, "z", 1, s)          # flight 1: up away from the dam
        B("galvanizedgrating", -24, 0, 25, -16, PAD, 28, s)                    # the turn
        stairs("galvanizedgrating", -20, 15, -16, 25, CREST, "z", -1, s, base=PAD)   # flight 2: back up to the crest
        B("powdercoatyellow", -20.1, PAD, 15, -19.9, CREST, 25, s)             # the divider
        B("castinplacewalls", -16, 0, 12.3, -15.6, CREST + 1.3, 28.4, s)        # the walls (a door at the foot)
        B("castinplacewalls", -24.4, 0, 28, -15.6, PAD + 1.3, 28.4, s)
        B("castinplacewalls", -24.4, 4, 12.3, -24, CREST + 1.3, 28, s)
        B("castinplacewalls", -24.4, 0, 16, -24, 4, 28, s)                     # (the door: z 12.3..16, 4 m high)
        B("powdercoatyellow", -24, CREST, 14.9, -20, CREST + 1.3, 15, s)       # rail over flight 1's well
        light(-20, 6, 20, 12, 0.7, s); light(-20, CREST + 3, 10, 12, 0.6, s)

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
    # the valve housing on the centreline (solid; the cart runs through its arch, which is under eye height)
    k.box("rivetedplates", -1.2, CREST, -3, 1.2, CREST + 4, -1.1); k.box("rivetedplates", -1.2, CREST, 1.1, 1.2, CREST + 4, 3)
    k.box("rivetedplates", -1.2, CREST + 1.58, -1.1, 1.2, CREST + 4, 1.1)
    k.box("powdercoatyellow", -1.4, CREST + 4, -3.2, 1.4, CREST + 4.3, 3.2)
    k.box("gaugecluster", -1.26, CREST + 2.2, -2.6, -1.2, CREST + 3.4, -1.4)
    k.box("gaugecluster", 1.2, CREST + 2.2, 1.4, 1.26, CREST + 3.4, 2.6)
    for (z0, z1) in ((-7, -6.5), (6.5, 7)):
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

    # ================= the crest's cover: the maintenance truck, stoplogs, the gantry crane, lamps =================
    for s in (-1, 1):
        B("powdercoatyellow", -32, CREST, 1.9, -27.5, CREST + 2.6, 4.3, s)     # the truck's box body
        B("rivetedplates", -27.5, CREST, 1.9, -25.4, CREST + 2.0, 4.3, s)      # its cab
        B("dustyglass", -26.3, CREST + 1.2, 2.0, -25.4, CREST + 1.9, 4.2, s)
        B("castiron", -31.6, CREST, 1.8, -30.8, CREST + 0.6, 1.9, s); B("castiron", -26.8, CREST, 1.8, -26, CREST + 0.6, 1.9, s)
        for j in range(4):                                                     # a stack of stoplogs (2 m, solid)
            B("charredtimber" if j % 2 else "railwaysleepers", -16, CREST + 0.5 * j, -4.4, -12, CREST + 0.5 * (j + 1), -2.4, s)
        # the gantry crane over the intake gates: legs on the parapets, a girder each side, a bridge across;
        # its beams are skinned in clip (the hook lets go: no grapple perch over the crest)
        for z0, z1 in ((-5, -4.4), (4.4, 5)):
            B("bridgegirder", -34.3, CREST, z0, -33.7, CREST + 8, z1, s); B("bridgegirder", -14.3, CREST, z0, -13.7, CREST + 8, z1, s)
            B("bridgegirder", -34.3, CREST + 8, z0, -13.7, CREST + 8.8, z1, s)
            B("clip", -34.4, CREST + 7.9, z0 - 0.1, -13.6, CREST + 9.4, z1 + 0.1, s)
        B("powdercoatyellow", -21, CREST + 8, -4.4, -19, CREST + 8.8, 4.4, s)
        B("clip", -21.1, CREST + 7.9, -4.4, -18.9, CREST + 9.4, 4.4, s)
        B("hazard", -20.5, CREST + 6.4, -0.5, -19.5, CREST + 8, 0.5, s)          # its hook block
        for x in (-46, -40):
            B("castiron", x, CREST, -4.4, x + 0.2, CREST + 6, -4.2, s)
            B("lightblock", x - 0.4, CREST + 6, -4.8, x + 0.6, CREST + 6.3, -3.8, s)
            light(x, CREST + 5, -3, 16, 0.75, s)

    # ================= the access road and the pad (per half) =================
    for s in (-1, 1):
        # the road: from the crest down the hill's foot to the pad (10 m over 45 m)
        B("retainingwallblocks", -42, 0, 5, -32, PAD, 50, s)
        W("asphalt", 3, -42, PAD, 5, -38, CREST, 50, s); W("asphalt", 3, -36, PAD, 5, -32, CREST, 50, s)
        # the track bed in a 2 m cutting: 0.5 m sleeper steps, each topped at the line's lowest over the cart's reach
        # (the check sweeps the cart's box against boxes, so a single sloped wedge would count as in its way), so the
        # bed runs 0.27-0.38 m under the line: no sawtooth to see or snag on. The rails sit just outside the cart's
        # box at the line's height, so it reads as seated on them.
        z = 3.5
        while z < 50:
            z1 = min(50.0, z + 0.5)
            B("railwaysleepers", -38, 0, z, -36, min(CREST, road_y(z1 + 1.2)) - 0.01, z1, s); z = z1
        for xr in (-38.0, -36.08):
            W("castiron", 3, xr, PAD, 5, xr + 0.08, CREST, 50, s)
        B("retainingwallblocks", -32.6, 0, 5, -32, PAD + 1.3, 50, s)            # its guard wall over the valley
        W("retainingwallblocks", 3, -32.6, PAD + 1.3, 5, -32, CREST + 1.3, 50, s)
        # two bays cut into the rock wall (2 m deep, 4 m long): push cover
        for (z0, z1) in ((18, 22), (33, 37)):
            B("retainingwallblocks", -44, 0, z0, -42, road_y(z1), z1, s)
            W("asphalt", 3, -44, road_y(z1), z0, -42, road_y(z0), z1, s)
            B("lightblock", -44, road_y(z0) + 3.2, z0 + 1.6, -43.9, road_y(z0) + 3.5, z1 - 1.6, s)
            light(-43, road_y(z0) + 3, (z0 + z1) / 2, 10, 0.6, s)
        # the pad (the depot): the road's end, the shed, the valley stair's head
        B("asphalt", -42, 0, 50, -32, PAD, 63, s); B("asphalt", -42, 0, 63, -32, PAD, Z1, s)
        B("concretepavers", -64, 0, 50, -48, PAD, 62, s); B("concretepavers", -48, 0, 50, -42, PAD, Z1, s)
        B("concretepavers", -64, 0, 62, -48, PAD, Z1, s)
        B("roadmarkings", -42, PAD, 59, -38.6, PAD + 0.01, 59.2, s)
        B("castiron", -38.4, PAD, 65.6, -35.6, PAD + 1.0, 66.1, s)              # the buffer stop at the track's end
        B("hazard", -38.4, PAD + 1.0, 65.6, -35.6, PAD + 1.3, 66.1, s)
        B("retainingwallblocks", -32.6, PAD, 54, -32, PAD + 1.3, Z1, s)          # the pad's guard wall (valley side)
        # the spawn shed: open to the north (onto the pad), walled east (no sight across the valley)
        B("castinplacewalls", -48.4, PAD, 62, -48, PAD + 7, Z1, s)
        B("castinplacewalls", -64, PAD, 62, -63.6, PAD + 7, Z1, s)
        B("corrugated", -64.4, PAD + 7, 61.6, -47.6, PAD + 7.4, Z1, s)
        B("woodencrate", -63.5, PAD, 62.5, -61, PAD + 2, 65, s); B("woodencrate", -51, PAD, 72.5, -48.5, PAD + 1.4, 75.5, s)
        B("glow", -62, PAD + 6.7, 68, -50, PAD + 6.9, 69, s)
        light(-56, PAD + 5, 69, 14, 0.7, s); light(-45, PAD + 5, 56, 16, 0.7, s)
        for (x, z) in ((-60, 68), (-55, 68), (-60, 72), (-55, 72)):
            k.spawn("a" if s < 0 else "b", X(x, s), PAD + 0.05, z, 0)
        # the valley stair: from the floor up to the pad
        stairs("boardformedconcrete", -32, 54, -27, 74, PAD, "z", -1, s)
        B("boardformedconcrete", -32, 0, 50, -27, PAD, 54, s)
        B("rivetedplates", -27.1, 0, 50, -27, PAD + 1.3, 74, s)                 # its rail (thin: shoot-through)
        # road lamps
        for z in (12, 27, 42):
            y = road_y(z) + 1.3
            B("castiron", -32.4, y, z - 0.1, -32.2, y + 4.5, z + 0.1, s)
            B("lightblock", -32.7, y + 4.5, z - 0.4, -31.9, y + 4.8, z + 0.4, s)
            light(-33, y + 4, z, 16, 0.75, s)

    # ================= the hill behind the road, with the stair path up a slot in it =================
    for s in (-1, 1):
        B("graniterock", -HX, 0, Z0, -50, 30, 5, s); clip(-HX, Z0, -50, 5, 30, s)          # behind the crest
        B("graniterock", -50, 0, Z0, -44, 30, -5, s); clip(-50, Z0, -44, -5, 30, s)
        B("graniterock", -HX, 0, 5, -50, 24, 50, s); clip(-HX, 5, -50, 50, 24, s)          # west of the slot
        B("graniterock", -HX, 0, 50, -64, 24, Z1, s); clip(-HX, 50, -64, Z1, 24, s)        # behind the pad
        for (z0, z1) in ((5, 18), (22, 33), (37, 50)):                                     # between the slot and the road
            B("graniterock", -45, 0, z0, -42, 24, z1, s); clip(-45, z0, -42, z1, 24, s)
        for (z0, z1) in ((18, 22), (33, 37)):
            B("graniterock", -45, 0, z0, -44, 24, z1, s); clip(-45, z0, -44, z1, 24, s)
        # the path: a landing at the pad, three flights with landings, out onto the crest's far end
        B("cobbledsetts", -50, 0, 44, -45, PAD, 50, s)
        stairs("largecobbledsetts", -50, 36, -45, 44, 13.5, "z", -1, s, base=PAD)
        B("cobbledsetts", -50, 0, 30, -45, 13.5, 36, s)
        stairs("largecobbledsetts", -50, 22, -45, 30, 17, "z", -1, s, base=13.5)
        B("cobbledsetts", -50, 0, 16, -45, 17, 22, s)
        stairs("largecobbledsetts", -50, 12, -45, 16, CREST, "z", -1, s, base=17)
        B("cobbledsetts", -50, 0, 5, -45, CREST, 12, s)
        for (z, y) in ((47, PAD), (33, 13.5), (19, 17), (8, CREST)):
            B("lightblock", -49.95, y + 2.6, z - 1, -49.85, y + 2.9, z + 1, s)
            light(-47.5, y + 3, z, 10, 0.6, s)
        # the rock's faces broken up: ledges and scree aprons at the feet
        B("graniterock", -64, PAD + 6, 52, -62.8, PAD + 7.2, 60, s)
        W("scree", 2, -26, 0, 70, -10, 2.5, Z1, s)                          # (at the valley's south wall)
        W("scree", 1, -64, PAD, 50, -61.5, PAD + 2, 60, s)                   # (behind the pad)
        B("graniterock", -24, 6, 75.2, -14, 7, Z1, s); B("graniterock", -12, 9, 75.4, -8, 10, Z1, s)
        for (x, z, y) in ((-58, 28, 24), (-66, 40, 24), (-60, -12, 30), (-66, -30, 30), (-56, 10, 24)):
            pine(x, z, y, R.uniform(7, 10), s)

    # ================= the valley floor: the powerhouse, cover, pines =================
    k.box("castinplacewalls", -14, 0, 16, 14, 12, 34); k.box("clip", -14, 12, 16, 14, LID, 34)
    k.box("corrugated", -14.5, 12, 15.5, 14.5, 12.4, 34.5)
    k.box("waterstainedconcrete", -5, -0.4, 34, 5, 0, 40)                      # the tailrace apron
    k.box("rivetedplates", -3, 1, 33.9, 3, 5, 34)                             # the outlet's gate
    for s in (-1, 1):
        for x0 in (-13, -6):
            B("glow", x0, 7, 34, x0 + 3, 9, 34.05, s)                          # lit windows
        B("electricalcabinet", -29, 0, 34, -25, 3, 38, s)                      # a transformer
        B("gabionwall", -24, 0, 46, -18, 1.6, 48, s)
        B("graniterock", -20, 0, 66, -15, 2.2, 70, s); B("graniterock", -12, 0, 50, -9, 1.6, 53, s)
        for (x, z) in ((-26, 44), (-22, 74), (-13, 60), (-28, 30)):
            pine(x, z, 0, R.uniform(8, 11), s)
        light(-20, 4, 40, 14, 0.6, s); light(-18, 4, 62, 16, 0.5, s); light(-28, 3, 18, 12, 0.6, s)

    # free for all: eight spawns (mirrored pairs)
    for (x, y, z) in ((-20, 0, 56), (-47.5, CREST, 0), (-28, CREST, -2.4), (-10, 0, 64)):
        for s in (-1, 1):
            k.spawn("any", X(x, s), y + 0.05, z, yaw_to(X(x, s), z, 0, 40))
    m.save("dam", dress=False)
