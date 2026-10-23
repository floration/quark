"""FURNACE: ranked LASER VAMPIRE. Compact (laser range is 24 m), close corners; the furnace in the middle
glows through its grates, molten trenches behind the inner walls (bridged in the middle) punish a careless
pull from the tractor beam. Point symmetric."""
from .prefabs import *


def build():
    m = M("Furnace", "1v1", sun=(0, 1, 0))   # (ranked: the sun overhead, so both halves are lit alike, MAP-17)
    m.raw("ranked vampire")
    m.room(18, 18, 10, pits=[(-9, 9.5, -2, 11, "molten"), (2, 9.5, 9, 11, "molten"), (-9, -11, -2, -9.5, "molten"), (2, -11, 9, -9.5, "molten")], style="hall", skyline="stacks")
    m.box(-3, 0, -3, 3, 4.5, 3, accent=True)
    for (a0, b0, a1, b1) in minus((-3.4, -3.4, 3.4, 3.4), [(3.0, 1.5, 3.4, 3.4), (-3.4, -3.4, -3.0, -1.5)]): m.box(a0, 4.5, b0, a1, 5.0, b1)   # the charging deck (notched over the ladders)
    m.box(-1.6, 5.0, -1.6, 1.6, 10, 1.6)                                          # the stack (its top: the highest perch)
    for y in (7.0, 8.8): m.glow(-1.65, y, -1.65, 1.65, y + 0.15, 1.65)
    for (a, b) in ((-3.04, -3.0), (3.0, 3.04)):                                 # grates glowing on all four faces
        for c in (-1.5, -0.5, 0.5, 1.5):
            m.glow(a, 2.2, c - 0.15, b, 3.6, c + 0.15); m.glow(c - 0.15, 2.2, a, c + 0.15, 3.6, b)
    m.light(0, 1.2, -3.6, 6, 0.9, fixture=False, mirror=True)
    m.box(-1.0, 0.4, 2.9, 1.0, 1.6, 3.1); m.box(-1.0, 0.4, -3.1, 1.0, 1.6, -2.9)
    m.box(-5.2, 0, 3.5, -3.8, 0.7, 5.5, mirror=True); m.box(-5.6, 0, -1, -4.2, 0.5, 1, mirror=True)   # coal heaps
    m.doorwall(-12, 6.8, 12, 7.2, 4.0, [(-7, 2.4, 2.8), (5, 2.4, 2.8)], axis="x", mirror=True)
    m.box(-12, 4.0, 6.8, 12, 4.4, 7.4, accent=True, mirror=True)               # the wall's cap, flush on the court side (a ladder goes up it)
    m.box(-18, 0, -4, -14, 2.5, 4, mirror=True); m.stairs(-14, -4, -11, 4, 2.5, "x", -1, mirror=True)
    m.box(-18, 2.5, -4.2, -14, 3.4, -4, accent=True, mirror=True); m.box(-18, 2.5, 4, -14, 3.4, 4.2, accent=True, mirror=True)
    for (x, z) in ((-10, 13), (10, 13)): m.pillar(x, z, 10.0, r=0.6, mirror=True)
    m.box(-6, 0, 12, -3, 1.3, 13, mirror=True); m.box(3, 0, 13.5, 5, 1.6, 15.5, mirror=True)
    m.wallbang(-1.5, 0, 12.5, 1.5, 2.4, 12.8, mirror=True)
    m.crates(-16, -14, 1, mirror=True)
    for x in (-4, 4): m.spawn("a", x, 0, 16, 0, "b")
    m.spawn("any", -16, 2.5, 0, 90, "any")
    # the stokers' ladders up two opposite sides of the furnace to its charging deck (5 m): a hot perch in the middle
    m.ladder(3.0, 2.4, 0, 5.0, "w", width=0.8); m.ladder(-3.0, -2.4, 0, 5.0, "e", width=0.8)
    # the side bays behind the inner walls are roofed sheds (where each player starts): indoor fights either side
    m.box(-18, 4.0, 7.4, 18, 4.4, 18, mirror=True)
    for x in (-12, 0, 12): m.glow(x - 1.2, 3.85, 12, x + 1.2, 4.0, 13, mirror=True); m.light(x, 3.3, 12.5, 8, 0.7, fixture=False, mirror=True)
    m.ladder(1.6, 0.6, 5.0, 10.0, "w", width=0.8); m.ladder(-1.6, -0.6, 5.0, 10.0, "e", width=0.8)   # up the stack from the deck
    # a damper housing on the stack's far edge from each ladder: at 6.75 m/s you come over the top with a hop that
    # would carry you off the 3.2 m top onto the deck; this stops you on it (1.17)
    m.box(-1.6, 10.0, -0.1, -1.2, 12.8, 1.3, accent=True, mirror=True)
    # a ladder up each inner wall onto the side bay's roof (4.4 m): a roof walk over the spawns
    m.ladder(-10, 6.8, 0, 4.4, "s", width=0.8); m.ladder(10, -6.8, 0, 4.4, "n", width=0.8)
    m.ladder(10, 6.8, 0, 4.4, "s", width=0.8); m.ladder(-10, -6.8, 0, 4.4, "n", width=0.8)   # (two each: the roof walk is a way round)
    m.raw("setting blast furnace")
    m.save("furnace")
