"""COLOSSEUM: every mode (built for tourney). The arena ring with tiered stands and arched inner walls; a
giant gladiator statue in the middle and two open beast pits in the sand."""
from .prefabs import *


def build():
    m = M("Colosseum", "tourney", sun=(0.5, 1.0, 0.2))
    m.room(26, 26, 14, pits=[(-11, 5, -7.5, 8.5, "abyss"), (7.5, 5, 11, 8.5, "abyss")], style="temple", skyline="ruins")
    for t in range(3):
        o0, o1, h = 26 - (t + 1) * 1.6, 26 - t * 1.6, 0.9 * (3 - t)
        for (a0, a1) in ((-26, -2), (2, 26)):   # (an aisle cut through the north and south stands)
            m.box(a0, 0, -o1, a1, h, -o0, accent=(t == 1)); m.box(a0, 0, o0, a1, h, o1, accent=(t == 1))
        m.box(-o1, 0, -o0, -o0, h, o0, accent=(t == 1)); m.box(o0, 0, -o0, o1, h, o0, accent=(t == 1))
    m.stairs(-2, 21.2, 2, 26, 2.7, "z", 1, mirror=True)                        # the aisle up to the top tier
    m.box(-22, 0, -22, -18, 3.6, -18, accent=True, mirror=True); m.box(18, 0, -22, 22, 3.6, -18, accent=True, mirror=True)
    m.box(-6, -0.05, -6, 6, 0, 6, accent=True)
    # the gladiator: pedestal, legs, kilt, torso, arms, helmet with a crest, shield and spear
    m.box(-1.6, 0, -1.6, 1.6, 1.2, 1.6, accent=True)
    m.box(-0.9, 1.2, -0.4, -0.25, 3.4, 0.4); m.box(0.25, 1.2, -0.4, 0.9, 3.4, 0.4)
    m.box(-1.0, 3.4, -0.55, 1.0, 4.2, 0.55, accent=True); m.box(-1.0, 4.2, -0.5, 1.0, 6.0, 0.5)
    m.box(-1.6, 4.4, -0.3, -1.0, 5.9, 0.3); m.box(1.0, 4.4, -0.3, 1.6, 5.9, 0.3)
    m.box(-0.45, 6.0, -0.45, 0.45, 6.9, 0.45, accent=True); m.glow(-0.08, 6.9, -0.5, 0.08, 7.4, 0.5)
    m.metal(-2.3, 3.6, -1.0, -1.7, 6.0, 1.0)                                    # shield
    m.metal(1.7, 1.2, -0.08, 1.85, 9.5, 0.08); m.glow(1.65, 9.5, -0.12, 1.9, 10.2, 0.12)  # spear (grapple anchor)
    m.wallbang(-9, 0, -1, -8, 2.6, 1, mirror=True); m.wallbang(-1, 0, 8, 1, 2.6, 9, mirror=True)
    m.crates(-15, -3, 1, mirror=True)
    for (x0, x1) in ((-11, -7.5), (7.5, 11)):                                   # grates around the pits
        m.metal(x0, 0, 4.7, x1, 0.5, 5); m.metal(x0, 0, 8.5, x1, 0.5, 8.8)
    for side in (-1, 1):
        m.doorwall(-15, side * 15 - 0.25, 15, side * 15 + 0.25, 4.0, [(-9, 2.6, 3.0), (0, 3.4, 3.2), (9, 2.6, 3.0)], axis="x")
        m.doorwall(side * 15 - 0.25, -15, side * 15 + 0.25, 15, 4.0, [(-9, 2.6, 3.0), (0, 3.4, 3.2), (9, 2.6, 3.0)], axis="z")
    for (x, z) in ((-15, -15), (15, -15), (-15, 15), (15, 15)): m.pillar(x, z, 5.0, r=0.7)
    for (x, z) in ((0, -15), (-15, 0)): m.light(x, 2.6, z, 6, 0.5, mirror=True)
    for x in (-4, 4): m.spawn("a", x, 0, 17, 0, "b")
    for (x, z) in ((-16, 0), (16, 0), (0, -14), (0, 14), (-12, -12), (12, 12)): m.spawn("any", x, 0, z, 0)
    for x in (-8, 8): m.spawn("a", x, 0, 18, 0, "b")
    m.raw("site -12 0 -13 -6 3 -7"); m.raw("site 6 0 -13 12 3 -7")
    m.raw("hill -4 0 -4 4 3 4")
    for p in ((0, 19), (0, 8), (-5, 4), (-5, -4), (0, -8), (0, -19)): m.raw(f"path {p[0]:g} 0 {p[1]:g}")
    m.raw("checkpoint 14 0 -1.5 16 4 1.5"); m.raw("checkpoint -1.5 0 -16 1.5 4 -14"); m.raw("checkpoint -16 0 -1.5 -14 4 1.5"); m.raw("checkpoint -1.5 0 14 1.5 4 16")
    for x in (4, 7): m.spawn("race", x, 0, 18, -90)
    # the arcade roof: the ring between the arched inner walls and the stands is covered (the classic ambulatory):
    # an indoor ring underneath, and a walkable upper ring on top of it
    for (a0, b0, a1, b1) in minus((-21.2, -21.2, 21.2, 21.2), [(-14.75, -14.75, 14.75, 14.75)]):
        m.box(a0, 4.0, b0, a1, 4.3, b1)
    for side in (-1, 1):
        for c in (-10, 0, 10):
            m.glow(c - 0.5, 3.85, side * 18 - 0.4, c + 0.5, 4.0, side * 18 + 0.4); m.glow(side * 18 - 0.4, 3.85, c - 0.5, side * 18 + 0.4, 4.0, c + 0.5)
            m.light(c, 3.2, side * 18, 7, 0.5, fixture=False); m.light(side * 18, 3.2, c, 7, 0.5, fixture=False)
    # ladders from the sand up the arched walls onto the arcade roof (point symmetric): one on every wall, so the roof
    # ring is a way round, not a perch with one way up
    m.ladder(5, -14.75, 0, 4.3, "n"); m.ladder(-5, 14.75, 0, 4.3, "s")
    m.ladder(-5, -14.75, 0, 4.3, "n"); m.ladder(5, 14.75, 0, 4.3, "s")
    m.ladder(14.75, -5, 0, 4.3, "e"); m.ladder(-14.75, 5, 0, 4.3, "w")
    # the ambulatory's piers: a row down the middle of the covered ring holds the roof up and splits it into two
    # aisles (cover in the ring, which was a bare 6 m corridor)
    for c in (-12, 0, 12):   # (clear of the spawns in the ring)
        for (x, z) in ((c, -18.1), (c, 18.1), (-18.1, c), (18.1, c)): m.box(x - 0.45, 0, z - 0.45, x + 0.45, 4.0, z + 0.45, accent=True)
    for (x, z) in ((-18.1, -18.1), (18.1, -18.1), (-18.1, 18.1), (18.1, 18.1)): m.box(x - 0.6, 0, z - 0.6, x + 0.6, 4.0, z + 0.6, accent=True)
    # the emperor's box on the east and west stands: a platform at 6.5 m under a canopy, up a ladder from the arcade
    # roof - the best seat in the house, and the highest point of the arena
    m.box(21.5, 2.7, -3, 22, 6.2, 3, accent=True, mirror=True)
    m.box(21.5, 6.2, -3, 25.5, 6.5, 3, mirror=True)
    m.ladder(21.5, 0, 4.3, 6.5, "e", width=0.8); m.ladder(-21.5, 0, 4.3, 6.5, "w", width=0.8)
    for (x, z) in ((25.2, -2.8), (25.2, 2.6)): m.metal(x - 0.1, 6.5, z - 0.1, x + 0.1, 9.0, z + 0.1, mirror=True)
    m.box(22.5, 9.0, -3.2, 25.8, 9.25, 3.2, accent=True, mirror=True); m.light(24, 8.2, 0, 7, 0.7, fixture=False, mirror=True)
    # wreckage in the sand from the last games: broken chariots and weapon racks (cover between the arches)
    m.clutter(-14, -14, 14, 14, 5, [(2.2, 1.0, 1.1), (1.0, 2.2, 1.1), (1.4, 1.4, 1.4)], seed=21, mirror=True, accent=0.4)
    m.raw("setting roman arena")
    m.save("colosseum")
