"""JUNGLE RUINS (1.19.6): a one-way race through jungle temple ruins, 12 checkpoints, about 1.25 km (a minute at full
speed, longer for anyone who falls), in six sections, each its own skill and each with a way to fail:
  1 the explorers' camp (8 race spawns by the tents) and a jungle path between fallen trees;
  2 the causeway: a broken stone causeway rising from 0 to 6 m over a mud swamp (0.6x speed, a weak jump). Its 8 m gaps
    have rotten planks (they give way 0.3 s after you stand on them: fall in and wade to the next stone steps); two
    gaps are 13 m with no plank over black sinkholes (ink, no bottom): a run-up or a dash, or a wade round the
    sinkhole's edge;
  3 the step pyramid: its stair up the east face to 45 m, or a faster two-hook vine line up the south-east corner
    (two vine-wrapped stelae on the tiers); through the top temple and down the back: a crumbling stair of ten stone
    pads (each falls 1 s after you land) zig-zagging down to 38 m over the lower tiers and a gorge with no bottom
    (two stelae there are the grapple line over it);
  4 the ruined city on the plateau (38 m): broken walls to vault, two crevasses to the void crossed by 1.5 m stone
    beams; then down a trench through a water curtain into the cave: five galleries side by side, each 5 m lower,
    joined by drops at alternate ends, their floors ledges over a chasm (a 3 m ledge, a broken 2 m ledge, a chasm
    with a natural bridge, ledges that swap sides, a floor with two pits); the void under all of it;
  5 the river: rapids along +x (12 m/s current: free speed) down five rock steps from 8.5 to -10.5 m, big rocks
    staggered across it every 16 m so the fast line weaves, roots across it to grapple;
  6 the sunken temple: the river pours into a flooded hall (deep water, no bottom); hop a line of broken column tops
    rising out of it (or take the side ledge and its ladder, about 3 s slower), up a fallen roof slab to the gallery
    and drop into the temple court: the finish.
The jungle round the course is a solid mass 20 m high with clip on top (nobody leaves the course or cuts a corner
over it), its faces dressed with leaning trunks, fern and bamboo clumps, ruins half in the wall and leaf ledges
overhead; a canopy of big leaf boxes over the open lanes (clip skins under them: nograpple). The plateau and the
pyramid stand above it, inside the clip walls. The void is at -30.
Fix round 2 (q's review answers, 2026-10-30): the old course had no way to fail and a flat 466 m sprint at the end.
Now: sinkholes and 13 m gaps on the causeway, a crumbling back stair over a bottomless gorge, crevasses on the
plateau, a cave of ledges over a chasm, staggered river rocks, a flooded hall of column tops; the finish 120 m (was
466); the swamp narrowed to 30 m; the canopy, the dressed mass faces, flowstone / glowing algae / clear pools in the
cave; the pyramid moved 24 m east so it shows from the causeway through the haze."""
import random
from .prefabs import M, minus
from .bigkit import Kit, num

LID = 100.0
MT, MB = 20.0, -14.0          # the jungle mass: its top (clip above it) and bottom
SITE = (-264, -202, 264, 192)
MASSIF = (-262, -200, -148, -20)   # the plateau (top 38) with the cave in it
GORGE = (-148, -200, -124, -102)   # between the plateau and the pyramid: no bottom
PYR = (-124, -198, -28, -102)      # the pyramid's base
PC = (-76, -150)                   # its centre
PTOP = 38.0                        # the plateau's top
CORR = {   # the open course: (x0, z0, x1, z1)
    "camp": (220, -185, 262, -125), "path": (160, -162, 220, -138), "swamp": (-28, -165, 160, -135),
    "river": (-225, -20, 175, -2), "hall": (175, -26, 245, 22), "court": (245, -40, 262, 30)}
CZ0, CZ1 = -153, -147                                       # the causeway's lane
SINKS = [(97, -160, 110, -140), (31, -160, 44, -140)]       # the bottomless sinkholes under the two wide gaps
# the cave: five galleries side by side (z), each 7 m wide and 5 m lower than the last, x -225..-175 inside
GX0, GX1 = -225.0, -175.0
GAL = [(-76 + 8.5 * i, -69 + 8.5 * i, 32 - 5 * i) for i in range(5)]   # (z0, z1, floor)
CAVEBOX = (-226, -77, -174, -34)
TRENCH = (-175, -76, -160, -69)    # the way down into gallery 0 from the plateau
TUNNEL = (-225, -35, -219, -20)    # the way out of gallery 4 to the river
CREVS = [(-200, -125, -148, -118), (-190, -100, -148, -93)]   # crevasses across the plateau (to the void)
RIVER_SEGS = [(-225, -150, 8.5), (-150, -80, 4.5), (-80, -10, 0.5), (-10, 60, -3.5), (60, 120, -7), (120, 175, -10.5)]
HALL_WATER = -13.0


def h_cause(x):
    """the causeway's height: 0 at the path (x 160), 6 at the pyramid (x -28)"""
    return 6.0 * (160.0 - x) / 188.0


def bed_at(x):
    return next((b for (x0, x1, b) in RIVER_SEGS if x0 <= x < x1), RIVER_SEGS[-1][2])


def build():
    m = M("Jungle Ruins", "race", sun=(0.25, 1.0, 0.35))
    m.no_ffa = True
    m.raw("setting jungle temple ruins")
    m.raw("palette base 0.36 0.42 0.3 accent 0.55 0.46 0.22 metal 0.3 0.3 0.26 glow 1.0 0.78 0.42 sky 0.5 0.64 0.58")
    m.raw("void -30")
    k = Kit(m)
    R = random.Random(1197)

    def cp(x0, y, z0, x1, z1, h=4.0):
        m.raw(f"checkpoint {num(x0)} {num(y)} {num(z0)} {num(x1)} {num(y + h)} {num(z1)}")

    def trunk(x, z, y0, h, w=1.4, mat="treebark", crown=True):
        k.box(mat, x - w / 2, y0, z - w / 2, x + w / 2, y0 + h, z + w / 2)
        if crown:   # a crown in three tiers (wide and low, then narrower), each a little off the trunk
            r = R.uniform(4, 6); y = y0 + h - 1.5
            for (f, t) in ((1.0, 1.6), (0.7, 1.8), (0.4, 1.4)):
                ox, oz = R.uniform(-0.8, 0.8), R.uniform(-0.8, 0.8)
                k.box("jungleleaves", x - r * f + ox, y, z - r * f + oz, x + r * f + ox, y + t, z + r * f + oz)
                y += t

    def canopy(x0, z0, x1, z1, y):
        """a canopy leaf mass overhead, a clip skin under it (the hook hits it and lets go: no swinging over the course)"""
        k.box("jungleleaves", x0, y, z0, x1, y + R.uniform(3, 5), z1)
        k.box("clip", x0, y - 0.05, z0, x1, y, z1)

    def dress(a0, a1, w, s, axis, floor, step=9.0, seed=0, skip=()):
        """dress a mass face (the lane's edge): along `axis` ('x' or 'z') from a0 to a1, the face at w, the lane on
        side s (+1 / -1). floor: the lane's floor (a number or a function of the position along). A cycle of leaning
        trunks, fern clumps, ruins half in the wall, leaf ledges at 8-12 m with vines, and bamboo; nothing reaches
        more than 1.2 m into the lane under 8 m."""
        r = random.Random(seed)
        p = a0 + r.uniform(1, step)
        n = r.randrange(5)
        while p < a1 - 3:
            y = floor(p) if callable(floor) else floor
            if any(q0 <= p <= q1 for (q0, q1) in skip):
                p += step; continue

            def bx(mat, u0, u1, d0, d1, y0, y1):   # (u along the face, d out from the face into the lane)
                if axis == "x": k.box(mat, u0, y0, w + s * d0, u1, y1, w + s * d1)
                else: k.box(mat, w + s * d0, y0, u0, w + s * d1, y1, u1)
            kind = n % 5
            if kind == 0:     # a great trunk half in the wall, a root flare at its foot, its crown over the clip line
                bx(r.choice(["treebark", "palmtrunk"]), p, p + 2.0, -1.0, 1.0, y, MT + 2)
                bx("treebark", p - 0.6, p + 2.6, -0.5, 1.2, y, y + 0.7)
                bx("jungleleaves", p - 3, p + 5, -4, 3, MT + 2, MT + 5)
            elif kind == 1:   # a fern clump at the foot
                bx("ferns", p, p + r.uniform(2.5, 4), 0, r.uniform(0.8, 1.2), y, y + r.uniform(0.8, 1.3))
            elif kind == 2:   # a ruin half in the wall: a block wall stub and a carved lintel stone
                h = r.uniform(2.5, 5)
                bx("mossystoneblocks", p, p + 3.5, -1.5, 0.8, y, y + h)
                bx("largemossystoneblocks", p - 0.5, p + 4, -1.5, 1.0, y + h, y + h + 0.8)
            elif kind == 3:   # a leaf ledge overhead with vines down the wall under it
                yy = y + r.uniform(8, 12)
                bx("jungleleaves", p - 1, p + 5, -0.5, 2.5, yy, yy + 1.2)
                bx("vinesonstone", p + 0.5, p + 1.5, 0, 0.12, y + 3, yy)
                bx("vinesonstone", p + 3, p + 3.8, 0, 0.12, y + 5, yy)
            else:             # a bamboo clump
                for (u, h) in ((0, 7), (0.8, 9), (1.6, 6), (0.4, 8)):
                    bx("bamboo", p + u, p + u + 0.25, 0.2 + (u % 0.8) * 0.4, 0.45 + (u % 0.8) * 0.4, y, y + h + r.uniform(-1, 1))
            n += 1
            p += step + r.uniform(-2, 3)

    # ================= the jungle mass and the site's clip =================
    opens = list(CORR.values()) + [MASSIF, PYR, GORGE]
    for n, (a, b, c, d) in enumerate(minus(SITE, opens)):
        k.box(("jungleleaves", "jungleleaves", "lichenrock", "jungleleaves", "vinesonstone")[n % 5], a, MB, b, c, MT, d)
        if c - a > 8 and d - b > 8:   # the canopy's top: uneven mounds of leaves inside the clip (only seen, never reached)
            for _ in range(max(1, int((c - a) * (d - b) / 900))):
                w, dd = R.uniform(6, 16), R.uniform(6, 16)
                x0 = R.uniform(a + 1, max(a + 1, c - 1 - w)); z0 = R.uniform(b + 1, max(b + 1, d - 1 - dd))
                k.box("jungleleaves", x0, MT, z0, min(c - 1, x0 + w), MT + R.uniform(3, 12), min(d - 1, z0 + dd))
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
    for (z0, z1) in ((-184.6, -184), (-126, -125.4)): k.box("bamboofence", 222, 0, z0, 260, 1.2, z1)   # the camp's fences
    cp(196, 0, -162, 200, -138)                                                            # 1
    k.box("treebark", 165, 0, -161.5, 190, 1.4, -160); k.box("treebark", 200, 0, -140, 218, 1.6, -138.4)
    k.box("treebark", 184.5, 0, -162, 185.5, 0.9, -138)                                    # a fallen tree across (a hop)
    dress(160, 220, -162, 1, "x", 0, seed=1); dress(160, 220, -138, -1, "x", 0, seed=2)
    canopy(168, -172, 200, -156, 29); canopy(190, -146, 222, -130, 31)

    # ================= 2. the causeway over the swamp =================
    a, b, c, d = CORR["swamp"]
    SEG = [(136, 160), (110, 128), (74, 97), (44, 66), (8, 31), (-28, 0)]   # gaps: 8, 13 (sink), 8, 13 (sink), 8
    for (p, q, r_, s_) in minus((a, b, c, d), SINKS):
        k.box("rootyloam", p, MB, q, r_, -2, s_)
    for (p, q, r_, s_) in minus((a, b, c, d), [(x0, CZ0, x1, CZ1) for (x0, x1) in SEG] + SINKS):
        k.box("mudbath", p, -2, q, r_, -0.4, s_)
    for (p, q, r_, s_) in SINKS:   # black water with no bottom: miss the jump and you sink into the void
        k.box("ink", p, -2, q, r_, -0.4, s_)
        for (z0, z1) in ((q, q + 0.6), (s_ - 0.6, s_)):   # a rim of drowned stones round it (a hint, under the surface)
            k.box("lichenrock", p, -2.4, z0, r_, -0.5, z1)
    for i, (x0, x1) in enumerate(SEG):
        lo, hi = h_cause(x1), h_cause(x0)
        mat = ("largemossystoneblocks", "mossystoneblocks")[i % 2]
        k.box(mat, x0, -2, CZ0, x1, lo, CZ1)
        if hi > lo + 1e-3: k.ramp(mat, x0, CZ0, x1, CZ1, lo, hi, "-x")
        if i > 0:   # stone steps up out of the swamp near its east end (south flank, climbing north onto it)
            k.stairs("mossystone", x1 - 9, CZ0 - 8, x1 - 4, CZ0, h_cause(x1 - 4), "z", 1, base=-2)
        if i + 1 < len(SEG):
            g0, g1 = SEG[i + 1][1], x0
            if g1 - g0 < 10:   # the rotten planks over an 8 m gap
                k.box("rottenplanks", g0, h_cause((g0 + g1) / 2) - 0.15, -151, g1, h_cause((g0 + g1) / 2), -149)
        for xx in (x0 + 1, x1 - 2):   # broken kerb stones on the edges (low: no cover, no snag)
            k.box("lichenrock", xx, h_cause(xx), CZ0, xx + 1, h_cause(xx) + 0.4, CZ0 + 0.5)
    cp(116, h_cause(118), CZ0, 120, CZ1)                                                     # 2
    cp(16, h_cause(18), CZ0, 20, CZ1)                                                        # 3
    for (x, z, s) in ((146, -161, 0.6), (82, -139, 0.7), (49, -162, 0.6), (-12, -139, 0.7)):   # islands, a tree on each
        k.box("mosscarpet", x - 5 * s, -2, z - 3 * s, x + 5 * s, 0.4, z + 3 * s)
        trunk(x, z, 0.4, R.uniform(10, 16), w=1.6)
    for (x, z) in ((122, -159), (60, -142), (20, -159), (-20, -142)):                        # ruined pillars in the water
        k.box("vinesonstone", x, -2, z, x + 1.6, R.uniform(3, 6), z + 1.6)
    for x in (140, 75, 10): m.lines.append(f"light {x} 7 -150 22 0.6")
    dress(-28, 160, -165, 1, "x", -2, step=10, seed=3); dress(-28, 160, -135, -1, "x", -2, step=10, seed=4)
    for (x0, x1, side) in ((128, 160, -1), (60, 100, 1), (8, 40, -1), (-28, 6, 1)):
        canopy(x0, -176 if side < 0 else -144, x1, -158 if side < 0 else -124, R.uniform(29, 33))

    # ================= 3. the step pyramid =================
    k.box("forestfloor", PYR[0], MB, PYR[1], PYR[2], 0, PYR[3])
    LANE = (PC[0] + 9, -154, PYR[2], -146)
    prev = 0.0
    tiers = []
    for t in range(8):
        half = 48 - 37 * t / 7
        top = 6 + 4.875 * (t + 1)
        rect = (PC[0] - half, PC[1] - half, PC[0] + half, PC[1] + half)
        tiers.append((rect, top))
        mat = ("largetemplestone", "largemossystoneblocks")[t % 2]
        for (p, q, r_, s_) in minus(rect, [LANE]): k.box(mat, p, prev, q, r_, top, s_)
        # a carved band on the south riser (flush trim: the faces read as stepped masonry, not slabs)
        k.box("gold" if t == 6 else "jade" if t == 3 else "mossystone", rect[0], top - 0.4, rect[1] - 0.05, rect[2], top - 0.2, rect[1])
        prev = top
    k.stairs("templestone", LANE[0], LANE[1], LANE[2], LANE[3], 45, "x", -1, base=6)
    # the vine line: two vine-wrapped stelae on the south-east corner's tiers (fewer, higher anchors: two hooks)
    (r2, t2), (r5, t5) = tiers[2], tiers[5]
    for (x, z, y0, y1) in ((r2[2] - 3.5, r2[1] + 2, t2, t2 + 8), (r5[2] - 3.5, r5[1] + 2, t5, t5 + 8)):
        k.box("vinesonstone", x, y0, z, x + 1.6, y1, z + 1.6)
        k.box("gold", x - 0.2, y1, z - 0.2, x + 1.8, y1 + 0.3, z + 1.8)
        k.box("witheredvines", x - 0.06, y0 + 1, z + 0.3, x, y1 - 0.5, z + 1.3)
    # the temple on top: doors east and west, a stepped roof, the shrine
    TX0, TZ0, TX1, TZ1 = PC[0] - 9, -159, PC[0] + 9, -141
    for (p, q, r_, s_) in ((TX0, TZ0, TX1, TZ0 + 0.8), (TX0, TZ1 - 0.8, TX1, TZ1)):
        k.box("templestone", p, 45, q, r_, 51, s_)
    for x in (TX0, TX1 - 0.8):
        k.box("templestone", x, 45, TZ0 + 0.8, x + 0.8, 51, -152); k.box("templestone", x, 45, -148, x + 0.8, 51, TZ1 - 0.8)
        k.box("templestone", x, 49.5, -152, x + 0.8, 51, -148)                              # lintels
        k.box("gold", x - 0.05, 49.3, -152.2, x + 0.85, 49.5, -147.8)
    k.box("templestone", TX0 - 1, 51, TZ0 - 1, TX1 + 1, 51.8, TZ1 + 1)
    k.box("templestone", TX0 + 3, 51.8, TZ0 + 3, TX1 - 3, 53.4, TZ1 - 3)
    k.box("jade", TX0 + 6, 53.4, TZ0 + 6, TX1 - 6, 54.4, TZ1 - 6)
    k.box("lightblock", PC[0] - 1, 54.4, -151, PC[0] + 1, 55.2, -149)                      # the beacon (reads through the haze)
    k.box("templestone", PC[0] - 4, 45, -158.2, PC[0] + 4, 46.2, -156); k.box("jade", PC[0] - 1, 46.2, -157.6, PC[0] + 1, 47.4, -156.6)
    m.lines.append(f"light {PC[0]} 48.5 -150 12 0.8"); m.lines.append(f"light {PC[0]} 57 -150 30 0.7")
    cp(PC[0] - 4, 45, -152, PC[0] + 4, -148)                                               # 4
    # the back: a short deck out of the west door, then ten crumbling pads zig-zagging down to the plateau (45 -> 38)
    k.box("largemossystoneblocks", -98.2, 44.4, -152, TX0, 45, -148)
    k.box("templestone", -97, tiers[5][1], -151.5, -95.4, 44.4, -148.5)                   # its pier
    for i in range(10):
        x1 = -100 - 4.8 * i; x0 = x1 - 3
        zc = -150 + (0.8 if i % 2 else -0.8)
        top = 45 - 0.7 * (i + 1)
        k.box("crumbleplatform", x0, top - 0.5, zc - 1.2, x1, top, zc + 1.2)
    # the grapple line over the back: two stelae, one on the tiers, one rising out of the gorge
    for (x, y0) in ((-112, tiers[2][1]), (-137, MB)):
        k.box("vinesonstone", x, y0, -158, x + 1.6, 52, -156.4)
        k.box("gold", x - 0.2, 52, -158.2, x + 1.8, 52.3, -156.2)
    m.lines.append("light -120 48 -150 18 0.5")

    # ================= 4. the plateau (ruined city) and the cave =================
    holes = [CAVEBOX, TRENCH, TUNNEL] + CREVS
    for (p, q, r_, s_) in minus(MASSIF, holes):
        k.box("lichenrock", p, MB, q, r_, PTOP - 0.4, s_)
        k.box("largemossystoneblocks", p, PTOP - 0.4, q, r_, PTOP, s_)
    for (p, q, r_, s_) in CREVS:   # the crevasses: the void at the bottom; a 1.5 m stone beam over each
        bx = -168 if q < -110 else -180
        k.box("largemossystoneblocks", bx, PTOP - 0.8, q, bx + 1.5, PTOP, s_)
        k.box("vinesonstone", p, PTOP - 6, q - 0.1, r_, PTOP - 0.4, q)
    cp(-152, PTOP, -154, -148, -146)                                                        # 5
    # broken walls across the line (vault them: 1 m), ruins and trees off it
    for (x0, z0, x1, z1) in ((-172, -141, -150, -140), (-178, -109, -156, -108), (-180, -84, -160, -83)):
        k.box("largemossystoneblocks", x0, PTOP, z0, x1, PTOP + 1.0, z1)
        k.box("mossystoneblocks", x0, PTOP + 1.0, z0, x0 + 3, PTOP + 3.5, z1)                 # a taller stub at one end
    RUINS = [(-200, -160, 10, 8, 6), (-230, -130, 12, 10, 9), (-215, -105, 8, 6, 5), (-240, -175, 9, 9, 7),
             (-195, -60, 6, 10, 4), (-245, -60, 10, 8, 8), (-232, -88, 6, 6, 11)]
    rr = random.Random(77)
    for gx in range(-256, -186, 15):   # the ruined city's blocks west of the line (off it), streets between them
        for gz in range(-196, -84, 15):
            if rr.random() < 0.3 or any(q - 3 < gz + 12 and gz < s_ + 3 for (p, q, r_, s_) in CREVS): continue
            RUINS.append((gx + rr.uniform(0, 3), gz + rr.uniform(0, 3), rr.uniform(6, 11), rr.uniform(6, 11), rr.uniform(3, 10)))
    for (x, z) in ((-200, -150), (-215, -175), (-240, -120), (-225, -95)):                 # moss over the old squares
        k.box("mosscarpet", x, PTOP, z, x + rr.uniform(8, 14), PTOP + 0.05, z + rr.uniform(8, 14))
    for (x, z, w, d, h) in RUINS:   # roofless house shells: three walls each, one side open
        k.box("mossystoneblocks", x, PTOP, z, x + w, PTOP + h, z + 0.8)
        k.box("mossystoneblocks", x, PTOP, z, x + 0.8, PTOP + h * 0.7, z + d)
        k.box("mossystoneblocks", x + w - 0.8, PTOP, z, x + w, PTOP + h * 0.5, z + d)
        k.box("templestone", x - 0.3, PTOP + h, z - 0.3, x + w * 0.6, PTOP + h + 0.6, z + 1.1)
    for (x, z) in ((-160, -175), (-205, -140), (-235, -110), (-190, -82), (-250, -150), (-220, -45), (-200, -185), (-158, -60)):
        trunk(x, z, PTOP, R.uniform(12, 20), w=1.8, mat=R.choice(["treebark", "palmtrunk"]))
    for (x, z) in ((-163, -130), (-176, -98), (-171, -78), (-157, -112)):
        k.box("ferns", x, PTOP, z, x + 2.5, PTOP + 1, z + 2)
    for (x, z, h) in ((-158, -132, 2.2), (-174, -116, 4.5), (-151, -104, 1.6), (-178, -90, 3.2), (-155, -88, 2.6)):   # column stumps
        k.box("templestone", x, PTOP, z, x + 1.4, PTOP + h, z + 1.4)
    k.wedge("templestone", 0, -184, PTOP, -138, -179, PTOP + 1.6, -134)                   # a fallen statue's head (off the line)
    k.box("jade", -183.5, PTOP, -137.5, -181.5, PTOP + 1.2, -135.5)
    m.lines.append("light -160 42 -110 24 0.5"); m.lines.append("light -170 42 -80 24 0.5")
    # the trench down into the cave (38 -> 32), a water curtain over the cave's mouth
    k.ramp("wetcaverock", TRENCH[0] + 1, TRENCH[1], TRENCH[2], TRENCH[3], GAL[0][2], PTOP, "+x")
    k.box("wetcaverock", TRENCH[0], GAL[0][2] - 0.6, TRENCH[1], TRENCH[0] + 1, GAL[0][2], TRENCH[3])   # the mouth's sill
    k.box("watercurtain", GX1 - 0.2, GAL[0][2], TRENCH[1], GX1, GAL[0][2] + 4.5, TRENCH[3])
    cp(-160, PTOP, TRENCH[1], -156, TRENCH[3])                                               # 6
    # the cave: rock round and over every gallery, dividers between them, the galleries' floors over the void
    cx0, cz0, cx1, cz1 = CAVEBOX
    k.box("wetcaverock", cx0, MB, cz0, GX0, PTOP, cz1)                                      # west end wall
    for (p, q, r_, s_) in minus((GX1, cz0, cx1, cz1), [(GX1, GAL[0][0], cx1, GAL[0][1])]):
        k.box("wetcaverock", p, MB, q, r_, PTOP, s_)                                         # east end wall (the mouth in it)
    k.box("wetcaverock", GX1, GAL[0][2] + 4.5, GAL[0][0], cx1, PTOP, GAL[0][1])
    k.box("wetcaverock", GX0, MB, cz0, GX1, PTOP, GAL[0][0])                                # south of gallery 0
    for (p, q, r_, s_) in minus((GX0, GAL[4][1], GX1, cz1), [TUNNEL]):
        k.box("wetcaverock", p, MB, q, r_, PTOP, s_)                                         # north of gallery 4 (the way out in it)
    exits = {0: (GX0, GX0 + 6), 1: (GX1 - 6, GX1), 2: (GX0, GX0 + 6), 3: (GX1 - 6, GX1)}   # each gallery's drop into the next
    ROCK = ["wetcaverock", "flowstone", "lichenrock", "wetcaverock", "flowstone"]
    for i, (z0, z1, f) in enumerate(GAL):
        roof = f + (5 if i < 4 else 4)
        k.box(ROCK[i], GX0, roof, z0, GX1, PTOP, z1)                                         # the rock over it
        k.box("glowwormcave", GX0, roof - 0.1, z0, GX1, roof, z1)
        if i < 4:   # the divider to the next gallery, with the drop at this gallery's far end
            dz0, dz1 = z1, GAL[i + 1][0]
            e0, e1 = exits[i]
            for (p, q) in ((GX0, e0), (e1, GX1)):
                k.box(ROCK[i], p, MB, dz0, q, PTOP, dz1)
            k.box(ROCK[i], e0, MB, dz0, e1, GAL[i + 1][2] - 0.6, dz1)
            k.box(ROCK[i], e0, f + 3.5, dz0, e1, PTOP, dz1)

    def fl(mat, x0, z0, x1, z1, y):
        k.box(mat, x0, y - 0.6, z0, x1, y, z1)
    # gallery 0 (west): a 3 m ledge on its north side, the chasm south; a full pad at each end
    z0, z1, f = GAL[0]
    fl("wetcaverock", GX0, z1 - 3, GX1, z1, f); fl("wetcaverock", GX0, z0, GX0 + 6, z1 - 3, f); fl("wetcaverock", GX1 - 5, z0, GX1, z1 - 3, f)
    # gallery 1 (east): a 2 m ledge on its south side, broken three times (2 m gaps); pads at both ends
    z0, z1, f = GAL[1]
    fl("flowstone", GX0, z0, GX0 + 6, z1, f); fl("flowstone", GX1 - 6, z0, GX1 - 1, z1, f)   # (1 m short of the end wall)
    for (a0, a1) in ((GX0 + 6, -207), (-205, -195), (-193, -185), (-183, GX1 - 6)):
        fl("flowstone", a0, z0, a1, z0 + 2, f)
    # gallery 2 (west): a full floor with a 4 m chasm across it (a 1.5 m natural bridge), clear pools on its edges
    z0, z1, f = GAL[2]
    for (a0, a1) in ((GX0, -200), (-196, GX1)): fl("lichenrock", a0, z0, a1, z1, f)
    fl("lichenrock", -200, z0 + 3, -196, z0 + 4.5, f)
    for (a0, a1, side) in ((-190, -184, 0), (-215, -209, 1)):
        zz = z0 if side == 0 else z1 - 1.6
        k.box("clearlagoon", a0, f - 0.5, zz, a1, f + 0.3, zz + 1.6)
        k.box("glowingalgae", a0, f - 0.55, zz, a1, f - 0.5, zz + 1.6)
    # gallery 3 (east): a 2 m ledge on the north side, then on the south side, a plate across where they meet
    z0, z1, f = GAL[3]
    fl("wetcaverock", GX0, z0, GX0 + 6, z1, f); fl("wetcaverock", GX1 - 6, z0, GX1, z1, f)
    fl("wetcaverock", GX0 + 6, z1 - 2, -198, z1, f); fl("wetcaverock", -202, z0, GX1 - 6, z0 + 2, f)
    fl("wetcaverock", -202, z0 + 2, -198, z1 - 2, f)
    # gallery 4 (west, a low roof): a full floor with two pits to hop (3 m of floor beside each)
    z0, z1, f = GAL[4]
    for (p, q, r_, s_) in minus((GX0, z0, GX1, z1), [(-197, z0, -193, z0 + 4), (-210, z1 - 4, -206, z1)]):
        fl("flowstone", p, q, r_, s_, f)
    cp(-190, GAL[2][2], GAL[2][0], -186, GAL[2][1])                                         # 7 (gallery 2)
    # the cave's dressing: glowing algae strips and flowstone columns on the walls, lights
    for i, (z0, z1, f) in enumerate(GAL):
        for x in range(int(GX0) + 8, int(GX1) - 6, 11):
            k.box("glowingalgae", x, f + 0.4, z1 - 0.08, x + 2.5, f + 0.7, z1)
            roof = f + (5 if i < 4 else 4)
            k.box("flowstone", x + 5, roof - R.uniform(0.8, 1.8), z0, x + 5.8, roof, z0 + 0.5)   # flowstone hanging from the roof
        for lx in (GX0 + 8, (GX0 + GX1) / 2, GX1 - 8):
            m.lines.append(f"light {num(lx)} {num(f + 3.5)} {num((z0 + z1) / 2)} 16 1.0")
    # the way out: a tunnel north from gallery 4's west end down to the river's pool (12 -> 10)
    tx0, tz0, tx1, tz1 = TUNNEL
    k.box("wetcaverock", tx0, MB, tz0, tx1, 10, tz1)
    k.ramp("wetcaverock", tx0, tz0, tx1, tz1, 10, GAL[4][2], "-z")
    k.box("wetcaverock", tx0, GAL[4][2] + 4, tz0, tx1, PTOP, tz1)
    k.box("watercurtain", tx0, 10, tz1 - 0.2, tx1, 15, tz1)
    # the falls on the plateau's north face (z -20) beside the way out
    k.box("waterfall", -219, 15, -20.3, -150, PTOP, -20)
    k.box("waterfall", -219, 9.5, -20.3, -200, 15, -20)

    # ================= 5. the river (rapids along +x) =================
    RZ0, RZ1 = CORR["river"][1], CORR["river"][3]
    for i, (x0, x1, bed) in enumerate(RIVER_SEGS):
        k.box("wetcaverock" if i == 0 else "lichenrock", x0, MB, RZ0, x1, bed, RZ1)
        if i == 0:
            k.box("clearlagoon", x0, bed, RZ0, -195, bed + 1.0, RZ1)
            k.box("whiterapids", -195, bed, RZ0, x1, bed + 1.0, RZ1)
        else:
            k.box("whiterapids", x0, bed, RZ0, x1, bed + 1.0, RZ1)
        if i + 1 < len(RIVER_SEGS):
            k.box("waterfall", x1, RIVER_SEGS[i + 1][2], RZ0, x1 + 0.12, bed, RZ1)
    # big rocks staggered across it, every 16 m, from alternate banks, each over half the channel: the line weaves
    j = 0
    for x in range(-180, 168, 16):
        if any(abs(x - cx) < 6 for cx in (-58, 82)) or any(abs(x - x1) < 5 for (x0, x1, b) in RIVER_SEGS): continue
        bed = bed_at(x); side = j % 2; j += 1
        reach = R.uniform(8, 10)
        z0, z1 = (RZ0, RZ0 + reach) if side == 0 else (RZ1 - reach, RZ1)
        w = R.uniform(4, 6)
        top = bed + R.uniform(2.6, 3.6)
        k.box("lichenrock", x, bed, z0, x + w, top, z1)
        if R.random() < 0.5: k.box("mosscarpet", x + 0.5, top, z0 + 0.5, x + w - 0.5, top + 0.3, z1 - 0.5)
    for x in (-160, -100, -40, 25, 95, 150):                                                # roots across (grapple anchors)
        bed = bed_at(x)
        k.box("treebark", x, bed + 7, RZ0, x + 0.6, bed + 7.6, RZ1)
        k.box("jungleleaves", x - 1, bed + 7.6, RZ0, x + 1.6, bed + 8.4, RZ0 + 5)
    cp(-214, 8.5, RZ0, -208, RZ1)                                                           # 8 (the pool under the falls)
    cp(-60, 0.5, RZ0, -56, RZ1)                                                             # 9
    cp(80, -7, RZ0, 84, RZ1)                                                                # 10
    for x in (-200, -130, -60, 10, 80, 150):
        m.lines.append(f"light {x} {num(bed_at(x) + 6)} -11 20 0.6")

    def riverfloor(x): return bed_at(x) + 1.0
    roots = [(x - 1, x + 2) for x in (-160, -100, -40, 25, 95, 150)]
    dress(-150, 175, RZ0, 1, "x", riverfloor, step=12, seed=5, skip=roots)
    dress(-195, 175, RZ1, -1, "x", riverfloor, step=12, seed=6, skip=roots)
    for (x0, x1, side) in ((-140, -100, 1), (-60, -20, -1), (10, 50, 1), (100, 140, -1)):
        canopy(x0, -28 if side < 0 else -10, x1, -12 if side < 0 else 6, R.uniform(26, 30))

    # ================= 6. the sunken temple: the flooded hall and the court =================
    HX0, HZ0, HX1, HZ1 = CORR["hall"]
    k.box("waterfall", HX0, HALL_WATER, RZ0, HX0 + 0.12, -10.5, RZ1)                       # the river pours in
    k.box("deepocean", HX0, HALL_WATER - 3, HZ0, 235, HALL_WATER, HZ1)                     # deep water, no bottom
    # the side ledge (the slow way: walk it, then a ladder up to the platform)
    k.box("largemossystoneblocks", HX0, MB, HZ0, 235, -10.5, HZ0 + 2.5)
    k.box("largemossystoneblocks", HX0, MB, HZ0 + 2.5, HX0 + 4, -10.5, RZ0 + 5)
    # the broken colonnade: column tops rising out of the water, 6.5 m apart, zig-zagging (the fast way)
    for i in range(9):
        cx = 180 + 6.5 * i; cz = -11 + (1.5 if i % 2 else -1.5); top = -10.2 + 0.85 * i
        k.box("templestone", cx - 1.2, MB, cz - 1.2, cx + 1.2, top - 0.4, cz + 1.2)
        k.box("largemossystoneblocks", cx - 1.3, top - 0.4, cz - 1.3, cx + 1.3, top, cz + 1.3)
    for (cx, cz) in ((190, 8), (204, 12), (222, 6), (198, -20), (214, -19)):               # whole columns in the water (off the line)
        k.box("templestone", cx - 0.9, MB, cz - 0.9, cx + 0.9, 12, cz + 0.9)
        k.box("templestone", cx - 1.3, 12, cz - 1.3, cx + 1.3, 13, cz + 1.3)
    # the platform at the hall's east end, the ladder up from the ledge, the fallen roof slab up to the gallery
    k.box("largemossystoneblocks", 235, MB, HZ0, HX1, -2.5, HZ1)
    k.ladder(235, HZ0 + 1.25, -10.5, -2.5, "e", mat="treebark")
    cp(236, -2.5, -16, 240, -6)                                                             # 11
    k.ramp("templestone", 236, -8, 243, 12, -2.5, 6, "+z")
    k.box("templestone", 236, MB, 12, HX1, 6, HZ1)                                          # the gallery (6 m)
    # the hall's walls (an opening onto the court from the gallery), broken roof beams
    k.box("largemossystoneblocks", HX0, MB, HZ1, HX1 + 1, 14, HZ1 + 1)
    k.box("largemossystoneblocks", HX0, MB, HZ0 - 1, HX1 + 1, 14, HZ0)
    for (z0, z1) in ((HZ0, 12.5), (19.5, HZ1)): k.box("largemossystoneblocks", HX1, MB, z0, HX1 + 1, 14, z1)
    k.box("largemossystoneblocks", HX1, MB, 12.5, HX1 + 1, 6, 19.5)
    k.box("templestone", HX1, 10, 12.5, HX1 + 1, 14, 19.5)
    for x in range(180, 245, 13):
        k.box("templestone", x, 13, HZ0, x + 1.2, 14.2, HZ0 + R.uniform(6, 20))              # broken roof beams from the walls
        k.box("templestone", x + 6, 13, HZ1 - R.uniform(6, 18), x + 7.2, 14.2, HZ1)
    for x in (185, 210, 232):
        k.box("gold", x, 9, HZ0, x + 3, 9.3, HZ0 + 0.1); k.box("gold", x, 9, HZ1 - 0.1, x + 3, 9.3, HZ1)
    for (x, z) in ((200, -11), (225, -11)): m.lines.append(f"light {x} 2 {z} 20 0.7")
    # the court (y 0): the finish line before the shrine
    cx0, cz0, cx1, cz1 = CORR["court"]
    k.box("templestone", cx0, MB, cz0, cx1, 0, cz1)
    k.box("gold", 253, 0, cz0, 253.4, 0.05, cz1)
    cp(253.4, 0, cz0, 257.4, cz1)                                                           # 12: the finish
    k.box("largemossystoneblocks", 258, 0, -12, 262, 3, 12)                                 # the shrine against the east wall
    k.box("templestone", 259, 3, -8, 262, 8, 8); k.box("gold", 259.5, 8, -4, 262, 9.5, 4)
    for z in (-36, -22, 22, 28):
        k.box("templestone", 249, 0, z, 250.6, 8, z + 1.6)
    m.lines.append("light 254 6 -5 22 0.7")
    m.save("jungle", dress=False)
