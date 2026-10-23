"""DOJO: ranked LASER VAMPIRE, eye to eye. Keep it simple: one open square platform (24 x 24 m, nothing on it)
in a lava moat, on a mountain top. Both fighters start on it facing each other and bow (no firing for the
first 5 s); a careless pull off the edge is a swim in the lava. The ring path outside the moat (jump or hook
back) carries all the decoration: pillars, lanterns, shrines. Point symmetric."""
from .prefabs import *


def build():
    m = M("Dojo", "1v1", sun=(0, 1, 0))   # (ranked: the sun overhead, so both halves are lit alike, MAP-17)
    m.raw("ranked vampire")
    m.raw("holdfire 5")
    P, W = 12, 4.5                                                                 # platform half size, moat width
    moat = [(-P - W, -P - W, P + W, -P, "molten"), (-P - W, P, P + W, P + W, "molten"),
            (-P - W, -P, -P, P, "molten"), (P, -P, P + W, P, "molten")]
    m.room(23, 23, 3.5, pits=moat, style="temple", skyline="mountains")
    m.box(-P, -0.02, -P, P, 0.02, P, mat="trim")                                   # the stone of the platform
    m.box(-P + 0.6, 0.02, -P + 0.6, P - 0.6, 0.04, P - 0.6, accent=True)            # the mat (flush: nothing to trip on)
    m.box(-1.6, 0.04, -1.6, 1.6, 0.06, 1.6, mat="glow")                            # the centre mark
    for (x0, z0, x1, z1) in ((-P, -P, P, -P + 0.3), (-P, P - 0.3, P, P), (-P, -P, -P + 0.3, P), (P - 0.3, -P, P, P)):
        m.box(x0, 0.0, z0, x1, 0.08, z1, mat="glow")                               # glowing edge line: here be lava
    m.light(0, 4, 0, 14, 0.5, fixture=False)
    for (x, z) in ((-P - W / 2, 0), (P + W / 2, 0), (0, -P - W / 2), (0, P + W / 2)):
        m.light(x, -0.6, z, 12, 0.9, fixture=False)                                # the moat glows up at the walls
    # the fight stays on the platform: a running jump clears the 4.5 m moat (a dash-jump 13 m), so an invisible wall
    # stands at its far edge, up out of reach (the ring path beyond is scenery)
    E = P + W
    for (x0, z0, x1, z1) in ((-E - 0.6, -E - 0.6, E + 0.6, -E), (-E - 0.6, E, E + 0.6, E + 0.6), (-E - 0.6, -E, -E, E), (E, -E, E + 0.6, E)):
        m.box(x0, -4, z0, x1, 80, z1, mat="clip")
    # outside the moat only: pillars, lanterns, shrines on the ring path
    R = P + W + 0.5
    for t in (-14, -7, 0, 7, 14):
        m.pillar(-22, t, 6.0, r=0.6, mirror=True); m.pillar(t, -22, 6.0, r=0.6, mirror=True)
    for (x, z) in ((-R - 1, -R - 1), (R + 1, R + 1), (-R - 1, R + 1), (R + 1, -R - 1)):
        m.box(x - 0.6, 0, z - 0.6, x + 0.6, 1.0, z + 0.6, accent=True)             # stone lanterns at the corners
        m.glow(x - 0.35, 1.0, z - 0.35, x + 0.35, 1.7, z + 0.35)
        m.box(x - 0.7, 1.7, z - 0.7, x + 0.7, 1.9, z + 0.7, mat="trim")
        m.light(x, 1.4, z, 6, 0.6, fixture=False)
    for (x, z) in ((-22.5, 0), (22.5, 0)):
        m.box(x - 0.5, 0, z - 2, x + 0.5, 1.0, z + 2, accent=True)                 # the shrines (against the wall)
        m.glow(x - 0.1, 1.0, z - 1.2, x + 0.1, 3.0, z + 1.2)
    for (x, z) in ((0, -22.5), (0, 22.5)):
        m.box(x - 3, 0, z - 0.5, x + 3, 0.5, z + 0.5, mat="trim")                  # bamboo racks
        for k in range(-2, 3): m.metal(x + k * 1.0 - 0.05, 0.5, z - 0.05, x + k * 1.0 + 0.05, 2.6, z + 0.05)
    m.spawn("a", -8, 0.1, 0, -90, "b")   # eye to eye across the platform
    m.raw("setting dojo")
    m.save("dojo")
