"""JUNGLE RUINS (1.19.6): a one-way race through jungle temple ruins, 12 checkpoints, about 1.4 km (a minute), in six
sections, each its own skill:
  1 the explorers' camp (8 race spawns by the tents) and a jungle path between fallen trees;
  2 the causeway: a broken stone causeway rising from 0 to 6 m over a swamp (swamp water: 0.7x speed), six pieces
    with 7-8 m gaps, each gap crossed by rotten planks that give way 0.3 s after you stand on them (stone steps up out
    of the swamp at every piece);
  3 the step pyramid: its 78-step stair up to 45 m (or grapple the vines on the tiers), through the top temple and
    west over a stone bridge onto the cliff plateau;
  4 the waterfall cave: through a water curtain into a cave of six stacked galleries, 7 m drops between them at
    alternate ends (a hole in the third gallery's floor is a 1 s shortcut), out behind the falls at 10 m;
  5 the river: rapids along +x (12 m/s current: free speed) down five rock steps from 10 to -10 m, rocks to dodge,
    roots across it to grapple;
  6 the sunken temple: out up a ramp, through a collapsed colonnade, south past the ruined gate and west into the
    temple court (the finish).
The jungle round the course is a solid mass 20 m high with clip on top (nobody leaves the course or cuts a corner
over it); the plateau and the pyramid stand above it, inside the clip walls. A miss in the swamp only costs time;
nothing on this course kills except the void far below (-30)."""
import random
from .prefabs import M, minus
from .bigkit import Kit, num

LID = 100.0
MT, MB = 20.0, -14.0          # the jungle mass: its top (clip above it) and bottom
SITE = (-264, -202, 264, 192)
MASSIF = (-262, -200, -148, -20)   # the cliff plateau (top 45) with the cave in it
PYR = (-148, -198, -52, -102)      # the pyramid's base
PC = (-100, -150)                  # its centre
CORR = {   # the open course: (x0, z0, x1, z1)
    "camp": (220, -185, 262, -125), "path": (160, -162, 220, -138), "swamp": (-52, -175, 160, -125),
    "river": (-215, -20, 175, -2), "f1": (175, -20, 255, 0), "f2": (235, 0, 255, 170), "f3": (150, 150, 255, 170),
    "court": (100, 120, 160, 190)}
CAVE = (-215, -32, -165, -26)      # the galleries' footprint
EXIT = (-171, -26, -165, -20)      # the way out behind the falls (y 10..15)
LEG = [45 - 7 * i for i in range(6)]


def h_cause(x):
    """the causeway's height: 0 at the path (x 160), 6 at the pyramid (x -52)"""
    return 6.0 * (160.0 - x) / 212.0


def build():
    m = M("Jungle Ruins", "race", sun=(0.25, 1.0, 0.35))
    m.no_ffa = True
    m.raw("setting jungle temple ruins")
    m.raw("palette base 0.36 0.42 0.3 accent 0.55 0.46 0.22 metal 0.3 0.3 0.26 glow 1.0 0.78 0.42 sky 0.62 0.76 0.68")
    m.raw("void -30")
    k = Kit(m)
    R = random.Random(1197)

    def cp(x0, y, z0, x1, z1, h=4.0):
        m.raw(f"checkpoint {num(x0)} {num(y)} {num(z0)} {num(x1)} {num(y + h)} {num(z1)}")

    def trunk(x, z, y0, h, w=1.4, mat="treebark", crown=True):
        k.box(mat, x - w / 2, y0, z - w / 2, x + w / 2, y0 + h, z + w / 2)
        if crown:
            r = R.uniform(3.5, 5.5)
            k.box("jungleleaves", x - r, y0 + h, z - r, x + r, y0 + h + R.uniform(2.5, 4), z + r)

    # ================= the jungle mass and the site's clip =================
    opens = list(CORR.values()) + [MASSIF, PYR]
    for n, (a, b, c, d) in enumerate(minus(SITE, opens)):
        k.box(("jungleleaves", "jungleleaves", "lichenrock", "jungleleaves", "vinesonstone")[n % 5], a, MB, b, c, MT, d)
        # the canopy's top: uneven mounds of leaves inside the clip (only seen, never reached)
        if c - a > 8 and d - b > 8:
            for _ in range(max(1, int((c - a) * (d - b) / 900))):
                w, dd = R.uniform(6, 16), R.uniform(6, 16)
                x0 = R.uniform(a + 1, max(a + 1, c - 1 - w)); z0 = R.uniform(b + 1, max(b + 1, d - 1 - dd))
                k.box("jungleleaves", x0, MT, z0, min(c - 1, x0 + w), MT + R.uniform(3, 12), min(d - 1, z0 + dd))
    # clip over the mass (not over the plateau or the pyramid: their own edges are clipped below)
    for (a, b, c, d) in minus(SITE, opens):
        k.box("clip", a, MT, b, c, LID, d)
    X0, Z0, X1, Z1 = SITE
    for (a, b, c, d) in ((X0 - 2, Z0 - 2, X0, Z1 + 2), (X1, Z0 - 2, X1 + 2, Z1 + 2), (X0, Z0 - 2, X1, Z0), (X0, Z1, X1, Z1 + 2)):
        k.box("clip", a, MB, b, c, LID, d)
    k.box("clip", X0 - 2, LID, Z0 - 2, X1 + 2, LID + 2, Z1 + 2)

    # ================= 1. the camp and the jungle path (y 0) =================
    for key, mat in (("camp", "forestfloor"), ("path", "rootyloam")):
        a, b, c, d = CORR[key]
        k.box(mat, a, MB, b, c, 0, d)
    for i in range(8): k.spawn("race", 250, 0, -155.25 + 1.5 * i, 90)
    for (x0, z0, z1) in ((226, -182, -174), (240, -182, -174), (226, -136, -128), (240, -136, -128)):   # tents (off the lane)
        k.wedge("hazmattarp", 2, x0, 0, z0, x0 + 8, 3.2, (z0 + z1) / 2)
        k.wedge("hazmattarp", 3, x0, 0, (z0 + z1) / 2, x0 + 8, 3.2, z1)
    k.box("woodencrate", 256, 0, -182, 258, 1.6, -179); k.box("woodencrate", 253, 0, -181.5, 255.4, 1.2, -179.5)
    k.box("lightblock", 233, 0, -170, 234, 0.6, -169)                                   # the campfire
    m.lines.append("light 233.5 2.5 -169.5 12 0.9")
    for z in (-140, -170): k.box("bamboo", 246, 0, z, 246.3, 2.6, z + 0.3); k.box("lightblock", 245.9, 2.6, z - 0.1, 246.4, 3.0, z + 0.4)
    cp(196, 0, -162, 200, -138)                                                            # 1
    # fallen trees along the path's edges, one across it (a hop)
    k.box("treebark", 165, 0, -161.5, 190, 1.4, -160); k.box("treebark", 200, 0, -140, 218, 1.6, -138.4)
    k.box("treebark", 184.5, 0, -162, 185.5, 0.9, -138)
    for (x, z) in ((172, -139), (206, -161), (214, -139.5), (162, -140)): trunk(x, z, 0, R.uniform(21, 25))   # (crowns over the clip line)

    # ================= 2. the causeway over the swamp =================
    a, b, c, d = CORR["swamp"]
    SEG = [(128, 160), (96, 121), (62, 89), (28, 54), (-8, 20), (-52, -16)]
    CZ0, CZ1 = -153, -147
    k.box("rootyloam", a, MB, b, c, -2, d)
    for (p, q, r_, s_) in minus((a, b, c, d), [(x0, CZ0, x1, CZ1) for (x0, x1) in SEG]):
        k.box("swampwater", p, -2, q, r_, -0.4, s_)
    for i, (x0, x1) in enumerate(SEG):
        lo, hi = h_cause(x1), h_cause(x0)
        mat = ("largemossystoneblocks", "mossystoneblocks")[i % 2]
        k.box(mat, x0, -2, CZ0, x1, lo, CZ1)
        if hi > lo + 1e-3: k.ramp(mat, x0, CZ0, x1, CZ1, lo, hi, "-x")
        if i > 0:   # stone steps up out of the swamp at its east end (south flank)
            k.stairs("mossystone", x1 - 6, CZ0 - 3, x1, CZ0, h_cause(x1 - 6), "x", -1, base=-2)
        if i + 1 < len(SEG):   # the rotten planks over the next gap
            g0, g1 = SEG[i + 1][1], x0
            k.box("rottenplanks", g0, h_cause((g0 + g1) / 2) - 0.15, -151, g1, h_cause((g0 + g1) / 2), -149)
        for xx in (x0 + 1, x1 - 2):   # broken kerb stones on the edges (low: no cover, no snag)
            k.box("lichenrock", xx, h_cause(xx), CZ0, xx + 1, h_cause(xx) + 0.4, CZ0 + 0.5)
    cp(112, h_cause(114), CZ0, 116, CZ1)                                                     # 2
    cp(38, h_cause(40), CZ0, 42, CZ1)                                                        # 3
    cp(-36, h_cause(-34), CZ0, -32, CZ1)                                                     # 4
    for (x, z, s) in ((140, -168, 1.0), (100, -132, 1.2), (70, -170, 0.9), (10, -131, 1.1), (-30, -168, 1.0), (40, -168, 1.3)):
        k.box("mosscarpet", x - 5 * s, -2, z - 4 * s, x + 5 * s, 0.4, z + 4 * s)                    # islands, a tree on each
        trunk(x, z, 0.4, R.uniform(10, 16), w=1.6)
    for (x, z) in ((118, -158), (80, -142), (5, -158), (-24, -142)):                            # ruined pillars in the water
        k.box("vinesonstone", x, -2, z, x + 1.6, R.uniform(3, 6), z + 1.6)
    for x in (140, 75, 10, -40): m.lines.append(f"light {x} 6 -150 22 0.6")

    # ================= 3. the step pyramid =================
    k.box("forestfloor", PYR[0], MB, PYR[1], PYR[2], 0, PYR[3])
    LANE = (-91, -154, -52, -146)
    prev = 0.0
    for t in range(8):
        half = 48 - 37 * t / 7
        top = 6 + 4.875 * (t + 1)
        rect = (PC[0] - half, PC[1] - half, PC[0] + half, PC[1] + half)
        mat = ("largetemplestone", "largemossystoneblocks")[t % 2]
        for (p, q, r_, s_) in minus(rect, [LANE]): k.box(mat, p, prev, q, r_, top, s_)
        # vines down the tier's east riser, either side of the stair (grapple anchors)
        ex = PC[0] + half
        if t < 7:
            for z in (-162, -138, -170, -130):
                if abs(z - PC[1]) < half - 2: k.box("vinesonstone", ex, prev + 0.5, z - 0.8, ex + 0.15, top, z + 0.8)
        prev = top
    k.stairs("templestone", LANE[0], LANE[1], LANE[2], LANE[3], 45, "x", -1, base=6)
    # the temple on top: doors east and west, a stepped roof, the shrine
    TX0, TZ0, TX1, TZ1 = -109, -159, -91, -141
    for (p, q, r_, s_) in ((TX0, TZ0, TX1, TZ0 + 0.8), (TX0, TZ1 - 0.8, TX1, TZ1)):
        k.box("templestone", p, 45, q, r_, 51, s_)
    for x in (TX0, TX1 - 0.8):
        k.box("templestone", x, 45, TZ0 + 0.8, x + 0.8, 51, -152); k.box("templestone", x, 45, -148, x + 0.8, 51, TZ1 - 0.8)
        k.box("templestone", x, 49.5, -152, x + 0.8, 51, -148)                              # lintels
        k.box("gold", x - 0.05, 49.3, -152.2, x + 0.85, 49.5, -147.8)
    k.box("templestone", TX0 - 1, 51, TZ0 - 1, TX1 + 1, 51.8, TZ1 + 1)
    k.box("templestone", TX0 + 3, 51.8, TZ0 + 3, TX1 - 3, 53.4, TZ1 - 3)
    k.box("jade", TX0 + 6, 53.4, TZ0 + 6, TX1 - 6, 54.4, TZ1 - 6)
    k.box("templestone", -104, 45, -158.2, -96, 46.2, -156); k.box("jade", -101, 46.2, -157.6, -99, 47.4, -156.6)   # the shrine
    m.lines.append("light -100 48.5 -150 12 0.8")
    cp(-104, 45, -152, -96, -148)                                                           # 5
    # the bridge west to the plateau (deck 45, low parapets, piers down onto the tiers)
    k.box("largemossystoneblocks", MASSIF[2], 44.2, -152, -111, 45, -148)
    for z in (-152, -148.4): k.box("mossystone", MASSIF[2], 45, z, -111, 45.6, z + 0.4)
    for x in (-140, -126):
        k.box("mossystone", x, 0, -151.5, x + 2, 44.2, -148.5)
    m.lines.append("light -130 48 -150 18 0.6")

    # ================= 4. the plateau and the waterfall cave =================
    MX0, MZ0, MX1, MZ1 = MASSIF
    fill_holes = [CAVE, EXIT]
    for (p, q, r_, s_) in minus(MASSIF, fill_holes):
        k.box("lichenrock", p, MB, q, r_, 44.6, s_)
        k.box("mosscarpet", p, 44.6, q, r_, 45, s_)
    k.box("lichenrock", EXIT[0], MB, EXIT[1], EXIT[2], 8, EXIT[3]); k.box("wetcaverock", EXIT[0], 8, EXIT[1], EXIT[2], 10, EXIT[3])
    k.box("wetcaverock", EXIT[0], 15, EXIT[1], EXIT[2], 44.6, EXIT[3]); k.box("mosscarpet", EXIT[0], 44.6, EXIT[1], EXIT[2], 45, EXIT[3])
    # the galleries: leg i floor LEG[i], 5 m high; the slab under it 2 m with a hole at its far end
    cx0, cz0, cx1, cz1 = CAVE
    for i, y in enumerate(LEG):
        west = i % 2 == 0
        holes = []
        if i < 5: holes.append((cx0, cz0, cx0 + 6, cz1) if west else (cx1 - 6, cz0, cx1, cz1))
        if i == 2: holes.append((-205, cz0, -199, cz1))                                    # the shortcut
        bot = y - 2 if i < 5 else MB
        for (p, q, r_, s_) in minus(CAVE, holes):
            k.box("wetcaverock", p, bot + (0.1 if i < 5 else 0), q, r_, y, s_)
            if i < 5: k.box("glowwormcave", p, bot, q, r_, bot + 0.1, s_)               # the glow-worm ceiling of the gallery below
        for z in (cz0, cz1 - 0.1):
            for x in range(int(cx0) + 3, int(cx1) - 2, 9): k.box("glowingalgae", x, y + 0.3, z, x + 2.5, y + 0.6, z + 0.1)
        m.lines.append(f"light {num((cx0 + cx1) / 2 - 12)} {num(y + 3.8)} -29 14 0.55")
        m.lines.append(f"light {num((cx0 + cx1) / 2 + 12)} {num(y + 3.8)} -29 14 0.55")
    # leg 0 sits above the plateau: its walls and roof (a rock dome), its mouth east under the curtain
    k.box("wetcaverock", cx0 - 4, 45, cz0 - 4, cx1, 50, cz0); k.box("wetcaverock", cx0 - 4, 45, cz1, cx1, 50, cz1 + 4)
    k.box("wetcaverock", cx0 - 4, 45, cz0, cx0, 50, cz1)
    k.box("wetcaverock", cx0 - 4, 50, cz0 - 4, cx1 + 4, 54, cz1 + 4); k.box("lichenrock", cx0, 54, cz0 - 1, cx1 - 6, 57, cz1 + 1)
    k.box("glowwormcave", cx0, 49.9, cz0, cx1, 50, cz1)
    k.box("watercurtain", cx1, 45, cz0, cx1 + 0.2, 50, cz1)
    cp(cx1 - 6, 45, cz0, cx1 - 2, cz1)                                                      # 6 (the mouth)
    cp(-192, 24, cz0, -188, cz1)                                                           # 7 (gallery 4)
    # the falls on the south face (z -20), the curtain over the way out
    k.box("waterfall", -186, 15, -20.3, -150, 45, -20)
    k.box("waterfall", -186, 10, -20.3, EXIT[0], 15, -20); k.box("waterfall", EXIT[2], 10, -20.3, -150, 15, -20)
    k.box("watercurtain", EXIT[0], 10, -20.4, EXIT[2], 15, -20.3)
    # the plateau: trees and broken walls off the straight line from the bridge to the mouth
    for (x, z) in ((-160, -120), (-200, -150), (-230, -100), (-175, -70), (-240, -170), (-220, -55), (-185, -185), (-245, -130), (-205, -90)):
        trunk(x, z, 45, R.uniform(12, 20), w=1.8, mat=R.choice(["treebark", "palmtrunk"]))
    for (x0, z0, x1, z1) in ((-210, -140, -196, -139), (-238, -80, -237, -60), (-180, -110, -170, -109)):
        k.box("largemossystoneblocks", x0, 45, z0, x1, 47.5, z1)
    m.lines.append("light -160 49 -110 24 0.5"); m.lines.append("light -165 49 -60 24 0.5")

    # ================= 5. the river (rapids along +x) =================
    RZ0, RZ1 = CORR["river"][1], CORR["river"][3]
    SEGS = [(-215, -140, 8.5), (-140, -70, 4.5), (-70, 0, 0.5), (0, 70, -3.5), (70, 130, -7), (130, 175, -10.5)]
    for i, (x0, x1, bed) in enumerate(SEGS):
        k.box("wetcaverock" if i == 0 else "lichenrock", x0, MB, RZ0, x1, bed, RZ1)
        if i == 0:
            k.box("clearlagoon", x0, bed, RZ0, -185, bed + 1.0, RZ1)
            k.box("whiterapids", -185, bed, RZ0, x1, bed + 1.0, RZ1)
        else:
            k.box("whiterapids", x0, bed, RZ0, x1, bed + 1.0, RZ1)
        if i + 1 < len(SEGS):
            k.box("waterfall", x1, SEGS[i + 1][2], RZ0, x1 + 0.12, bed, RZ1)   # the rock step's sheet of water (flush with its lip)
    # rocks to dodge (eddies), clear of the checkpoints, alternating sides
    for j, (x, side) in enumerate(((-120, 0), (-95, 1), (-45, 0), (-15, 1), (15, 0), (55, 1), (90, 0), (115, 1), (150, 0))):
        bed = next(b for (x0, x1, b) in SEGS if x0 <= x < x1)
        z = RZ0 + 3 if side == 0 else RZ1 - 6
        k.box("lichenrock", x, bed, z, x + R.uniform(3, 5), bed + R.uniform(1.8, 2.6), z + 3)
    for x in (-160, -100, -40, 25, 85, 145):                                                # roots across (grapple anchors)
        bed = next(b for (x0, x1, b) in SEGS if x0 <= x < x1)
        k.box("treebark", x, bed + 7, RZ0, x + 0.6, bed + 7.6, RZ1)
        k.box("jungleleaves", x - 1, bed + 7.6, RZ0, x + 1.6, bed + 8.4, RZ0 + 5)
    cp(-200, 8.5, RZ0, -194, RZ1)                                                           # 8 (the pool under the falls)
    cp(-60, 0.5, RZ0, -56, RZ1)                                                             # 9
    cp(100, -7, RZ0, 104, RZ1)                                                              # 10
    for x in (-180, -110, -40, 30, 100, 160):
        bed = next(b for (x0, x1, b) in SEGS if x0 <= x < x1)
        m.lines.append(f"light {x} {num(bed + 6)} -11 20 0.6")

    # ================= 6. the sunken temple =================
    a, b, c, d = CORR["f1"]
    k.box("lichenrock", 175, MB, b, 200, -10.5, d)
    k.ramp("largeflagstonepaving", 175, b, 200, d, -10.5, 0, "+x")
    for key in ("f1", "f2", "f3", "court"):
        p, q, r_, s_ = CORR[key]
        if key == "f1": p = 200
        k.box("largemossystoneblocks" if key != "court" else "templestone", p, MB, q, r_, 0, s_)
    # the colonnade: columns along both sides, two fallen across half the lane each (a slalom), a lintel here and there
    for x in range(204, 254, 7):
        for z in (-19.6, -1.8):
            hgt = 9 if (x // 7) % 3 else R.uniform(2, 5)
            k.box("templestone", x, 0, z, x + 1.4, hgt, z + 1.4)
        if (x // 7) % 3 == 1: k.box("templestone", x, 9, -19.6, x + 1.4, 10, -0.4)
    k.box("templestone", 214, 0, -9, 215.4, 1.2, -0.4); k.box("templestone", 230, 0, -19.6, 231.4, 1.2, -11)
    cp(235, 0, 58, 255, 62)                                                                 # 11 (the run south)
    for z in range(10, 165, 12):                                                            # f2: broken walls on its edges
        k.box("mossystoneblocks", 235, 0, z, 236.4, R.uniform(1.5, 6), z + R.uniform(3, 7))
        k.box("mossystoneblocks", 253.6, 0, z + 5, 255, R.uniform(1.5, 6), z + 5 + R.uniform(3, 6))
    k.box("templestone", 235, 0, 78, 238, 9, 81); k.box("templestone", 252, 0, 78, 255, 9, 81)   # the ruined gate
    k.box("templestone", 235, 9, 78, 255, 10.5, 81); k.box("jade", 243, 10.5, 78.5, 247, 11.5, 80.5)
    k.box("mossystoneblocks", 236, 0, 120, 246, 1.0, 121.4)                                  # a fallen wall (hop it or go round)
    for x in range(160, 236, 15):
        k.box("mossystoneblocks", x, 0, 150, x + R.uniform(4, 8), R.uniform(2, 5), 151.4)
        k.box("mossystoneblocks", x + 6, 0, 168.6, x + 6 + R.uniform(4, 8), R.uniform(2, 5), 170)
    # the court: the temple half-sunk on its west side, columns round it, the finish line before its steps
    k.box("largemossystoneblocks", 100, 0, 135, 118, 7, 175)
    k.box("templestone", 118, 0, 140, 124, 3, 170)                                          # its porch
    k.stairs("templestone", 124, 145, 130, 165, 3, "x", -1, base=0)
    k.box("templestone", 102, 7, 137, 122, 10, 173); k.box("gold", 108, 10, 150, 116, 11.5, 160)
    for (x, z) in ((132, 124), (148, 124), (132, 186), (148, 186), (156, 124), (156, 186)):
        k.box("templestone", x, 0, z, x + 1.6, 8, z + 1.6)
    cp(136, 0, 140, 142, 170)                                                               # 12: the finish
    k.box("gold", 136, 0, 139.6, 142, 0.05, 140); k.box("gold", 136, 0, 170, 142, 0.05, 170.4)
    for (x, z) in ((150, 160), (245, 40), (245, 120), (215, 160), (220, -10), (140, 175)): m.lines.append(f"light {x} 6 {z} 22 0.6")
    # a few great trees beside the finish run (in the mass's edge, not the lane)
    for (x, z) in ((233, 30), (257, 60), (233, 100), (257, 140), (180, 148), (205, 172), (100, 118), (162, 192)):
        trunk(x, z, 0, R.uniform(21, 25), w=2.0)
    # great trees half in the jungle walls along the course (trunks only in the wall's edge, crowns over the clip line)
    for key in ("river", "f1", "f2", "f3", "swamp"):
        x0, z0, x1, z1 = CORR[key]
        floor = {"river": None, "swamp": -2}.get(key, 0)
        if x1 - x0 >= z1 - z0:
            for x in range(int(x0) + 12, int(x1) - 6, 26):
                for zz in (z0 - 0.4, z1 + 0.4):
                    y0 = next((bb for (p, q, bb) in SEGS if p <= x < q), 0) if key == "river" else floor
                    trunk(x + R.uniform(-4, 4), zz, y0, MT + 1 - y0, w=2.2, mat=R.choice(["treebark", "palmtrunk"]))
        else:
            for z in range(int(z0) + 12, int(z1) - 6, 26):
                for xx in (x0 - 0.4, x1 + 0.4):
                    trunk(xx, z + R.uniform(-4, 4), floor, MT + 1, w=2.2, mat=R.choice(["treebark", "palmtrunk"]))
    m.save("jungle", dress=False)
