"""TEMPLE: ranked VANILLA 1v1. Halls of columns around a sanctum; reflecting pools either side, a bridge with
a pagoda shrine over the altar, a golden idol under it. Point symmetric."""
from .prefabs import *


def build():
    m = M("Temple", "1v1", sun=(0, 1, 0))   # (ranked: the sun overhead, so both halves are lit alike, MAP-17)
    m.raw("ranked vanilla")
    # (1.13: open on a mountain top, no outer walls: the courts end at the cliff edge; built at the old size, then
    # spread out 1.4x across the ground at the end)
    m.room(22, 30, 12, sunken=[(-7, 9, 7, 12.5, 0.8, "accent"), (-7, -12.5, 7, -9, 0.8, "accent")], style="temple", skyline="pagodas", walls=False)
    m.raw("void -6")
    m.box(-24, -40, -32, 24, -1, 32, mat="far")   # the mountain top under the courts: rock falling away to the void
    m.box(-8, 0, -6, 8, 0.6, 6, accent=True)
    m.stairs(-4, 6, 4, 8, 0.6, "z", -1); m.stairs(-4, -8, 4, -6, 0.6, "z", 1)
    m.box(-2, 0.6, -2, 2, 1.6, 2); m.box(-1.2, 1.6, -1.2, 1.2, 1.9, 1.2, accent=True)   # altar and the idol on it
    m.box(-0.45, 1.9, -0.45, 0.45, 2.6, 0.45); m.glow(-0.3, 2.6, -0.3, 0.3, 2.95, 0.3)
    for (x, z) in ((-12, -18), (-12, -8), (-12, 8), (-12, 18)): m.pillar(x, z, 11.0, mirror=True)
    m.wallbang(-7, 0.6, -0.3, -3, 3.2, 0.3, mirror=True)
    # the flights up to the side platforms, either side of the catwalk (stone under it: nobody runs up the middle
    # and cracks their head on it)
    m.box(-22, 0, -4, -17, 3.5, 4); m.stairs(-17, -4, -14, -1, 3.5, "x", -1); m.stairs(-17, 1, -14, 4, 3.5, "x", -1)
    m.box(17, 0, -4, 22, 3.5, 4); m.stairs(14, -4, 17, -1, 3.5, "x", 1); m.stairs(14, 1, 17, 4, 3.5, "x", 1)
    m.box(-17, 0, -1, -14, 3.5, 1, accent=True); m.box(14, 0, -1, 17, 3.5, 1, accent=True)
    m.box(-22, 3.5, -4.2, -17, 4.4, -4, accent=True); m.box(17, 3.5, 4, 22, 4.4, 4.2, accent=True)
    m.box(-6, 0, 18, -3, 2.4, 19, mirror=True); m.box(3, 0, 18, 6, 2.4, 19, mirror=True)
    m.crates(-19, 22, mirror=True); m.crates(15, 13, 1, mirror=True)
    m.catwalk(-17, -1, 17, 1, 3.5, rails="z")
    m.box(-1, 3.5, -1, 1, 6.5, 1, accent=True)                                  # the pagoda shrine on the bridge
    m.box(-1.8, 5.0, -1.8, 1.8, 5.3, 1.8); m.box(-1.4, 6.2, -1.4, 1.4, 6.5, 1.4)
    m.box(-0.9, 6.5, -0.9, 0.9, 7.3, 0.9, accent=True); m.box(-1.1, 7.3, -1.1, 1.1, 7.55, 1.1)
    m.glow(-0.25, 7.55, -0.25, 0.25, 8.3, 0.25)
    m.light(-10, 2.6, 0, 7, 0.6, mirror=True)
    for x in (-6, -2, 2, 6): m.glow(x - 0.2, -0.8, 10.6, x + 0.2, -0.75, 10.9, mirror=True)   # lanterns in the pools
    m.doorwall(-22, 13.8, 22, 14.2, 5.0, [(-10, 3.0, 3.2), (0, 4.0, 3.6), (10, 3.0, 3.2)], axis="x", mirror=True)
    for x in (-3, 3): m.spawn("a", x, 0, 23, 0, "b")   # (1.13: nearer the middle, the map grew)
    m.spawn("any", -19, 3.5, 0, 90, "any")
    # the reflecting pools hold water (a surface over the sunken floors: shallow, safe to stand in)
    m.box(-7, -0.35, 9, 7, -0.3, 12.5, mat="water", mirror=True)
    # roofs over the two pillared end halls (behind the arched walls): indoor fights at each spawn end
    m.box(-22, 5.0, 14.2, 22, 5.6, 30, mirror=True)
    for x in (-14, 0, 14):
        m.glow(x - 1.5, 4.85, 21.5, x + 1.5, 5.0, 22.5, mirror=True); m.light(x, 4.2, 22, 10, 0.8, fixture=False, mirror=True)
    # offering stones and toppled column drums across the courts (placed where the floor is open, mirrored): cover
    # to duck behind between the halls
    m.clutter(-22, -30, 22, 30, 6, [(1.6, 1.6, 1.0), (2.4, 1.0, 1.2), (1.0, 2.4, 1.2)], seed=5, mirror=True, accent=0.5)
    # the roof walk over each end hall: a ladder up the arched wall (its top capped flush with the roof), and a
    # pagoda on the roof with a ladder to its lookout 11 m up: the high ground, exposed on every side, one per end
    m.box(-22, 5.0, 13.8, 22, 5.6, 14.2, accent=True, mirror=True)
    m.ladder(-5, 13.8, 0, 5.6, "s", width=0.8); m.ladder(5, -13.8, 0, 5.6, "n", width=0.8)
    m.ladder(5, 13.8, 0, 5.6, "s", width=0.8); m.ladder(-5, -13.8, 0, 5.6, "n", width=0.8)   # (two each: up one, down the other)
    m.box(-1.7, 5.6, 22.3, 1.7, 11.1, 25.7, accent=True, mirror=True)                              # the pagoda's body
    for (y, r) in ((7.4, 2.4), (9.2, 2.1)): m.box(-r, y, 24 - r, r, y + 0.25, 24 + r, mat="trim", mirror=True)   # eaves
    for (a0, b0, a1, b1) in minus((-1.9, 22.1, 1.9, 25.9), [(-0.6, 22.1, 0.6, 22.3)]): m.box(a0, 11.1, b0, a1, 11.4, b1, mirror=True)
    m.ladder(0, 22.3, 5.6, 11.4, "s", width=0.8)
    m.ladder(0, -22.3, 5.6, 11.4, "n", width=0.8)
    # a balcony round each pagoda at 8 m, off the ladder up it: a mid perch between the roof walk and the lookout
    for (a0, b0, a1, b1) in minus((-3.2, 20.8, 3.2, 27.2), [(-1.7, 22.3, 1.7, 25.7), (-0.7, 20.8, 0.7, 22.3)]): m.box(a0, 8.0, b0, a1, 8.25, b1, accent=True, mirror=True)   # (open over the ladder)
    for (x, z) in ((-1.8, 22.2), (1.6, 22.2), (-1.8, 25.6), (1.6, 25.6)): m.box(x, 11.4, z, x + 0.2, 14.0, z + 0.2, mat="metal", mirror=True)
    for (a0, b0, a1, b1) in minus((-2.3, 21.7, 2.3, 26.3), [(-0.7, 21.7, 0.7, 22.4)]): m.box(a0, 14.0, b0, a1, 14.3, b1, accent=True, mirror=True)   # (open over the ladder)
    m.glow(-0.3, 14.3, 23.7, 0.3, 15.0, 24.3, mirror=True)
    m.light(0, 12.6, 24, 6, 0.6, fixture=False, mirror=True)
    # the cliff edge: a knee-high stone balustrade (posts with lanterns, rails with gaps to drop through), lantern
    # posts at the corners. Built on one side and point-mirrored (ranked maps are point symmetric).
    for (ks, fixed, axis) in ((range(-20, 21, 4), 30.6, "x"), (range(-28, 29, 4), 22.6, "z")):
        for k in ks:
            if axis == "x": m.box(k - 0.25, 0, fixed - 0.25, k + 0.25, 1.1, fixed + 0.25, accent=True, mirror=True)
            else: m.box(fixed - 0.25, 0, k - 0.25, fixed + 0.25, 1.1, k + 0.25, accent=True, mirror=True)
            gx, gz = (k, fixed) if axis == "x" else (fixed, k)
            m.glow(gx - 0.12, 1.1, gz - 0.12, gx + 0.12, 1.35, gz + 0.12, mirror=True)       # a lantern on every post
            if k % 8 == 0: m.light(gx, 1.6, gz, 6, 0.45, fixture=False, mirror=True)
        for k in list(ks)[:-1:2]:   # rails between pairs of posts, the next gap left open
            if axis == "x": m.box(k + 0.25, 0.85, fixed - 0.12, k + 3.75, 1.0, fixed + 0.12, mat="trim", mirror=True)
            else: m.box(fixed - 0.12, 0.85, k + 0.25, fixed + 0.12, 1.0, k + 3.75, mat="trim", mirror=True)
    for (x, z) in ((-21, -29), (21, -29), (-21, 29), (21, 29)):
        m.box(x - 0.4, 0, z - 0.4, x + 0.4, 3.2, z + 0.4, accent=True); m.glow(x - 0.3, 3.2, z - 0.3, x + 0.3, 3.8, z + 0.3)
        m.light(x, 3.4, z, 9, 0.7, fixture=False)
    # two ruined tower stubs (point-mirrored): a broken floor at 2.2 m and its standing half 7.2 m up, stairs up the side
    for (x, z) in ((-17.5, -12),):
        m.box(x - 2, 0, z - 2, x + 2, 2.2, z + 2, mirror=True)
        m.box(x - 2, 2.2, z - 2, x + 2, 7.2, z, mirror=True)                                   # the standing half, 7.2 m
        m.box(x - 2.1, 7.05, z - 2.1, x + 2.1, 7.2, z + 0.1, mat="trim", mirror=True)
        m.stairs(x + 2, z - 2, x + 6.5, z + 2, 2.2, "x", -1, mirror=True)                       # up to the broken floor
        m.ladder(x, z, 2.3, 7.2, "n", width=0.8)                                                # ...and up the standing half
        # a jagged piece of the back wall still standing on it, behind the ladder: at 6.75 m/s you come over the
        # top with a hop that would carry you off the 2 m top; this stops you on it (1.17)
        m.box(x - 1.0, 7.2, z - 2, x + 1.0, 10.2, z - 1.6, mirror=True)
        m.box(x - 1.0, 10.2, z - 2, x + 0.2, 10.8, z - 1.6, mirror=True)
        m.ladder(-x, -z, 2.3, 7.2, "s", width=0.8)
        m.light(x, 3.0, z, 7, 0.6, fixture=False, mirror=True)
    # more to fight around in the open courts: broken column drums and fallen lintels, waist and head high
    m.clutter(-20, -28, 20, 28, 7, [(1.2, 1.2, 1.4), (3.0, 0.8, 1.3), (0.8, 3.0, 1.3), (1.6, 1.6, 2.2)], seed=11, mirror=True, accent=0.4)   # (denser: the courts had cover on 19% of the floor)
    # ruins of the old outer cloister: broken wall runs, chest to head high, with fallen blocks at their feet (cover
    # and something to strafe around, without closing the courts in again)
    for (x0, z0, x1, z1, h) in ((-19, -25.6, -14, -25, 2.6), (-20.6, -20, -20, -15, 1.8), (13, -21, 13.6, -16.5, 2.2),
                                (-9.5, -26.4, -6.5, -25.8, 1.4), (17, -9, 17.6, -5, 2.0), (-16.8, 9, -16.2, 12.5, 1.6),
                                (-5, -4.2, -2, -3.6, 1.5), (5.5, 9.4, 9.5, 10, 1.3), (-20.2, -6, -19.6, -2.5, 1.7), (-14, 25, -10, 25.6, 2.2)):
        m.box(x0, 0, z0, x1, h, z1, mirror=True)
        m.box(x0, h, z0, x0 + (x1 - x0) * 0.55, h + 0.5, z1, mirror=True)                      # the broken top, stepped
        m.box(x0 - 0.2, h - 0.15, z0 - 0.1, x1 + 0.2, h, z1 + 0.1, mat="trim", mirror=True)     # its worn coping
        bx, bz = (x1 + 0.4, z0 - 0.9) if x1 - x0 > z1 - z0 else (x0 - 1.2, z1 + 0.4)
        m.box(bx, 0, bz, bx + 0.9, 0.7, bz + 0.8, accent=True, mirror=True)                    # a fallen block
    # two raised offering plinths (a step up, a lantern each): a little height to fight from in the open courts
    for (x, z) in ((-9, -22), (9, 22)):
        m.box(x - 2.5, 0, z - 2.5, x + 2.5, 1.0, z + 2.5, accent=True)
        m.stairs(x - 2.5, z + (2.5 if z < 0 else -3.5), x + 2.5, z + (3.5 if z < 0 else -2.5), 1.0, "z", 1 if z > 0 else -1)
        m.box(x - 0.3, 1.0, z - 0.3, x + 0.3, 2.4, z + 0.3, accent=True); m.glow(x - 0.25, 2.4, z - 0.25, x + 0.25, 2.9, z + 0.25)
        m.light(x, 2.6, z, 7, 0.6, fixture=False)
    # carved bands on every column: base, mid and capital rings
    for (x, z) in ((-12, -18), (-12, -8), (-12, 8), (-12, 18)):
        for (y, t) in ((0.6, 0.25), (5.2, 0.15), (10.2, 0.3)):
            m.box(x - 1.0, y, z - 1.0, x + 1.0, y + t, z + 1.0, mat="trim", mirror=True)
    m.scale_xz(1.15)
    m.raw("setting pagoda temple on a mountain top")
    m.save("temple")
