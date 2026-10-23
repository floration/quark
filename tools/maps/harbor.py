"""HARBOR: free for all, the big one. A quay along the north with the sea beyond it (fall in and you're out),
a moored freighter you can board, three piers and a lighthouse; the yard with containers, a warehouse
and the dock crane in the south."""
from .prefabs import *


def build():
    m = M("Harbor", "ffa", sun=(0.6, 1.0, 0.15))
    m.room(30, 30, 12, pits=[(-30, 22, 30, 30, "water")], style="yard", skyline="cranes")
    m.box(-30, 0, 14, 30, 1.2, 22)                                             # quay
    m.stairs(-4, 11, 4, 14, 1.2, "z", -1); m.stairs(-24, 11, -18, 14, 1.2, "z", -1); m.stairs(18, 11, 24, 14, 1.2, "z", -1)
    for x in (-27, -3, 13, 21): m.box(x - 0.3, 1.2, 21.3, x + 0.3, 2.0, 21.9, accent=True)   # bollards
    for (x0, x1) in ((-30, -26.5), (-2, 2), (23, 30)): m.box(x0, -3.6, 22, x1, 1.2, 30)      # piers
    # the freighter: hull level with the quay, a bridge cabin you can walk through, mast and funnel
    m.metal(-24.5, -3.6, 22.4, -4.5, 1.2, 29.2)
    m.metal(-4.5, -3.6, 23.2, -3, 1.2, 28.4); m.metal(-3, -3.6, 24.2, -2.2, 1.2, 27.4)        # bow
    m.box(-24.8, 1.2, 22.2, -2.4, 1.6, 22.4, accent=True); m.box(-24.8, 1.2, 29.2, -4.5, 1.6, 29.4, accent=True)  # gunwales
    m.building(-22, 23.4, -15.5, 28.4, 2.9, [("s", -18.75, 2.4), ("n", -18.75, 2.0)], windows=True, base=1.2)
    m.box(-21, 4.1, 24.4, -16.5, 5.6, 27.4); m.glow(-20.9, 4.9, 24.3, -16.6, 5.2, 27.5)      # wheelhouse
    m.box(-13.5, 1.2, 24.7, -11.5, 6.5, 26.9, accent=True); m.glow(-13.6, 5.6, 24.6, -11.4, 5.9, 27.0)  # funnel
    m.metal(-8.2, 1.2, 25.6, -7.8, 10, 26); m.metal(-11, 8, 25.7, -5, 8.3, 25.9); m.glow(-8.3, 10, 25.5, -7.7, 10.4, 26.1)
    m.container(-11, 23.4, -9, 28.4, h=2.4)
    # the lighthouse on the east pier
    m.box(24.5, 1.2, 24.5, 28.5, 8, 28.5)
    for (a0, b0, a1, b1) in minus((24, 24, 29, 29), [(25.9, 24, 27.1, 24.6)]): m.box(a0, 8, b0, a1, 8.4, b1, accent=True)   # gallery (notched over the ladder)
    m.glow(25.6, 8.4, 25.6, 27.4, 10, 27.4); m.box(25.4, 10, 25.4, 27.6, 10.6, 27.6, accent=True)   # the lantern (a walkway round it)
    m.light(26.5, 9.2, 23.2, 10, 0.7, fixture=False)
    # the yard
    for (x0, z0, x1, z1) in ((-26, -10, -20, -4), (8, -24, 14, -18), (-6, -20, 0, -14)): m.container(x0, z0, x1, z1)
    m.wallbang(18, 0, -8, 24, 2.6, -2)
    m.wallbang(-8, 0, 2, -2, 2.4, 2.4); m.wallbang(4, 0, 4, 10, 2.4, 4.4)
    m.crates(-16, -24, 3); m.crates(20, 6, 2); m.crates(-2, 6, 2); m.crates(-24, 4, 1)
    m.box(-1, 0, -6, 1, 9, -4, accent=True); m.box(-1, 9, -6, 14, 9.8, -4, accent=True); m.box(13, 6.5, -5.5, 14, 9, -4.5)
    m.doorwall(-30, -16.2, -16, -15.8, 4.5, [(-27, 2.4, 2.8), (-22.5, 2.4, 2.8)], axis="x")
    m.doorwall(-16.2, -30, -15.8, -16.2, 4.5, [(-23, 2.6, 2.8)], axis="z")
    m.box(-30, 4.5, -30, -15.8, 4.8, -15.8, accent=True)
    m.stairs(-20, -15.8, -17.6, -11.3, 4.5, "z", -1)
    m.light(-26, 3.9, -26, 9, 0.9); m.light(-20, 3.9, -21, 9, 0.9)
    m.catwalk(-20, -8, 8, -6.6, 2.8, rails="z")
    for (x, z) in ((-22, 18), (22, 18), (6, 18), (-26, -26), (26, -26), (0, -26), (-12, -2), (10, 9), (-18, 8), (25, -14)):
        yaw = math.degrees(math.atan2(x, z))
        m.spawn("any", x, 1.2 if z > 14 else 0, z, round(yaw))
    m.spawn("a", -10, 0, -10, 0, "b")
    # the transit shed in the south yard: open sides, a roof on columns, pallets inside. Breaks the long yard
    # sightlines and gives the south an indoor fight
    for x in (2, 8, 14, 20, 26):
        for z in (-29.6, -20.4): m.metal(x - 0.3, 0, z - 0.3, x + 0.3, 6, z + 0.3)
    m.box(1.5, 6, -30, 26.5, 6.4, -20, accent=True)
    for x in (5, 11, 17, 23): m.glow(x - 0.4, 5.88, -29, x + 0.4, 6, -21); m.light(x, 5.2, -25, 8, 0.7, fixture=False)
    m.crates(16, -28, 2); m.crates(21, -24, 1); m.box(3, 0, -27, 5.4, 1.2, -25, accent=True)
    # the quay canopy: shade over the quay and the ship's side, posts along the quay edge
    for x in range(-28, 29, 8): m.metal(x - 0.25, 1.2, 14.2, x + 0.25, 5.5, 14.7)
    m.box(-30, 5.5, 14, 30, 5.9, 19, accent=True); m.glow(-30, 5.38, 14, 30, 5.5, 14.15)
    for x in (-20, -4, 12, 26): m.light(x, 4.8, 16.5, 9, 0.6, fixture=False)
    # the customs office in the middle: a room you can run through, stairs up the side to its roof (a mid level)
    m.building(-14, 6, -6, 12, 3.2, [("w", 9, 2.2), ("e", 9, 2.2), ("s", -10, 2.2)], windows=True)
    m.stairs(-14, 3.4, -8, 6, 3.55, "x", -1)                                   # along its north wall (clear of the doors and the quay steps)
    # quay clutter that breaks the 60 m quay into stretches: fish crate stacks, net racks, the harbourmaster's hut
    for (x, n) in ((-26, 2), (-12, 1), (9, 2), (16, 1)): m.crates(x, 15.2, n, size=1.1)
    for x in (-20, 3): m.metal(x, 1.2, 16.8, x + 4, 3.0, 17.0); m.box(x - 0.1, 1.2, 16.6, x + 0.1, 3.2, 17.2, accent=True); m.box(x + 3.9, 1.2, 16.6, x + 4.1, 3.2, 17.2, accent=True)
    m.building(-4, 15, 2, 19.5, 2.8, [("s", -1, 2.0), ("e", 17.2, 1.8)], base=1.2, windows=True, lights=True)
    # the east yard: two container rows make lanes between the shed and the quay
    m.container(18, -16, 24, -12); m.container(18, -16, 24, -12, h=5.2); m.container(10, -12, 15, -9)
    m.container(14, 0, 19, 3); m.box(19.5, 0, 0.5, 21.5, 1.4, 2.5)
    # a ladder up the lighthouse to its gallery: the classic harbour perch, exposed from every side
    m.ladder(26.5, 24.5, 1.2, 8.4, "s", width=0.9)
    # dusk lighting: lamp posts round the yard walls and along the quay edge, the freighter's deck lights and its mast
    # head (the yard was lit only under its roofs: 24 lights over 4600 m2)
    for (x, z) in ((-29.2, -8), (-29.2, 6), (29.2, -6), (29.2, 8), (-8, -29.2), (-2, 11), (6, -16), (-14, -12)):
        m.metal(x - 0.1, 0, z - 0.1, x + 0.1, 5.2, z + 0.1); m.glow(x - 0.3, 5.2, z - 0.3, x + 0.3, 5.45, z + 0.3); m.light(x, 4.8, z, 10, 0.6, fixture=False)
    for x in (-16, 6, 18):
        m.metal(x - 0.1, 1.2, 20.9, x + 0.1, 5.4, 21.1); m.glow(x - 0.3, 5.4, 20.7, x + 0.3, 5.6, 21.3); m.light(x, 5.0, 20.5, 9, 0.6, fixture=False)
    for x in (-23, -6): m.glow(x - 0.2, 1.6, 22.5, x + 0.2, 1.9, 22.7); m.light(x, 2.2, 24, 7, 0.6, fixture=False)   # deck lights on the freighter
    m.light(-8, 9.0, 24.5, 10, 0.6, fixture=False)                             # its mast head
    m.light(-23, 3.6, -23, 8, 0.7); m.light(21, 4.2, -14, 8, 0.6, fixture=False); m.light(12, 3.0, -21, 7, 0.6, fixture=False)   # the warehouse, the container rows
    m.raw("setting harbour")
    m.save("harbor")
