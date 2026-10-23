"""ANVIL: ranked map for ALL THREE ladders. The anvil itself in the middle (horn at both ends, you stand on
it), two giant hammers standing on their heads, four corner towers, window walls and wallbang screens."""
from .prefabs import *


def build():
    m = M("Anvil", "1v1", sun=(0, 1, 0))   # (ranked: the sun overhead, so both halves are lit alike, MAP-17)
    m.raw("ranked vanilla vampire instagib")
    m.room(22, 22, 12, pits=[(-21.5, -7, -19, 7, "molten"), (19, -7, 21.5, 7, "molten")], style="hall", skyline="stacks")
    m.box(-3, 0, -2, 3, 1.5, 2); m.box(-2, 1.5, -1.5, 2, 3, 1.5)
    m.box(-5, 3, -2.5, 5, 4, 2.5, accent=True)
    m.stairs(2, 2.5, 4, 7.5, 4, "z", -1, mirror=True)                           # up the anvil's flank (clear of the horns)
    m.box(5, 3, -1.25, 7, 4, 1.25, mirror=True); m.box(7, 3.4, -0.6, 8.5, 4, 0.6, mirror=True)   # horns
    for (x, z) in ((-2, -2.6), (2, 2.6), (3.5, -2.6), (-3.5, 2.6)): m.glow(x - 0.1, 4.0, z - 0.1, x + 0.1, 4.2, z + 0.1)   # sparks
    m.light(0, 2.4, -2.2, 6, 0.6, fixture=False, mirror=True)
    m.metal(-1.5, 0, 5, 1.5, 1.6, 7, mirror=True); m.box(-1.55, 0.6, 4.95, 1.55, 0.9, 7.05, accent=True, mirror=True)  # hammers
    m.box(-0.25, 1.6, 5.75, 0.25, 6.5, 6.25, mirror=True); m.glow(-0.3, 6.5, 5.7, 0.3, 6.7, 6.3, mirror=True)
    m.box(-20, 0, -20, -15, 3.5, -15, accent=True, mirror=True); m.box(15, 0, -20, 20, 3.5, -15, accent=True, mirror=True)
    m.stairs(-15, -20, -12, -17, 3.5, "x", -1, mirror=True); m.stairs(12, -20, 15, -17, 3.5, "x", 1, mirror=True)
    m.windowwall(-12, -9.2, -4, -8.8, 3.0, 1.0, 2.2, every=2.7, width=1.4, mirror=True)
    m.windowwall(4, -9.2, 12, -8.8, 3.0, 1.0, 2.2, every=2.7, width=1.4, mirror=True)
    m.box(-14, 0, -2, -11, 1.4, 2, mirror=True)
    m.box(-8, 0, 12, -5, 2.2, 14, mirror=True); m.box(5, 0, 12, 8, 1.2, 14, mirror=True)
    m.wallbang(-1.5, 0, 9.5, 1.5, 2.6, 9.9, mirror=True)
    m.pillar(-9, 4, 12.0, r=0.5, mirror=True); m.pillar(9, 4, 12.0, r=0.5, mirror=True)
    for x in (-4, 4): m.spawn("a", x, 0, 19, 0, "b")
    m.spawn("any", -17.5, 3.5, -17.5, 135, "any")
    # the smithy galleries: a walkway at 3.5 m along the north and south walls joining the corner towers' tops
    # (an upper ring with loops; a roof over the spawns below), on posts, with a ladder up the middle
    for (a0, b0, a1, b1) in minus((-15, 17, 15, 22), [(-0.6, 17, 0.6, 17.9), (-15, 17, -12, 20), (12, 17, 15, 20)]): m.box(a0, 3.2, b0, a1, 3.5, b1, mirror=True)   # (notched over the ladder and the tower stairs)
    for (a0, a1) in ((-15, -0.6), (0.6, 15)): m.box(a0, 3.5, 16.85, a1, 3.6, 17, accent=True, mirror=True)
    for x in (-12, -6, 6, 12): m.metal(x - 0.25, 0, 16.9, x + 0.25, 3.2, 17.4, mirror=True)
    for x in (-9, 9): m.light(x, 2.6, 19.5, 7, 0.6, fixture=False, mirror=True); m.glow(x - 0.6, 3.05, 19, x + 0.6, 3.2, 20, mirror=True)
    m.ladder(0, 16.85, 0, 3.5, "s", width=0.9); m.ladder(0, -16.85, 0, 3.5, "n", width=0.9)
    # smelting troughs of molten metal along the west and east walls: a hazard to dodge (or knock people into)
    m.light(-20.2, 0.6, 0, 8, 0.9, fixture=False, mirror=True)
    # the forge hoods over the lava troughs: a flue walk at 5 m on the hood (a ladder up its front post), and the
    # chimney above it with a ladder to its top at 8.5 m: the smithy's high ground, one per side
    m.box(17.4, 4.7, -6, 22, 5.0, 6, mirror=True)
    m.box(17.0, 0, -0.8, 17.4, 5.0, 0.8, mirror=True)                           # the hood's front post
    m.ladder(17.0, 0, 0, 5.0, "e", width=0.8); m.ladder(-17.0, 0, 0, 5.0, "w", width=0.8)
    m.box(19.6, 5.0, -3, 22, 8.5, 3, accent=True, mirror=True)                  # the chimney
    m.ladder(19.6, 2.0, 5.0, 8.5, "e", width=0.8); m.ladder(-19.6, -2.0, 5.0, 8.5, "w", width=0.8)
    for z in (-4, 4): m.light(19, 4.2, z, 6, 0.7, fixture=False, mirror=True)
    m.glow(19.55, 6.5, -2.5, 19.6, 6.7, -0.8, mirror=True); m.glow(19.55, 6.5, 0.8, 19.6, 6.7, 2.5, mirror=True)
    m.raw("setting smithy")
    m.save("anvil")
