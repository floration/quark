"""CLOCKWORK: duel (1v1 / 2v2), inside a giant clock. The great wheel turns in the middle on its arbor, ringed by
the works (fall in and you're out) and reached by four narrow spokes; a pinion above it is a second level up
ladders on the arbor. Pinion stacks in the corners, mainspring barrels on the flanks, pendulums hanging in
front of each spawn, the clock face glowing on the end walls. Compact and point symmetric: a duel map."""
from .prefabs import *


def build():
    m = M("Clockwork", "duel")
    works = [(-8, -8, -1.2, -3.5, "abyss"), (1.2, -8, 8, -3.5, "abyss"), (-8, 3.5, -1.2, 8, "abyss"), (1.2, 3.5, 8, 8, "abyss"),
             (-8, -3.5, -3.5, -1.2, "abyss"), (-8, 1.2, -3.5, 3.5, "abyss"), (3.5, -3.5, 8, -1.2, "abyss"), (3.5, 1.2, 8, 3.5, "abyss")]
    m.room(18, 18, 12, pits=works, style="factory", skyline=None)
    m.raw("indoor 12")
    m.box(-20, 12, -20, 20, 12.6, 20)                                           # the case lid
    # the great wheel: a brass disc you stand on, teeth round its rim, the arbor (axle) up to the lid
    m.box(-3, 0, -3, 3, 0.5, 3, accent=True)
    for k in range(16):
        a = k * math.pi / 8; x, z = 3.25 * math.cos(a), 3.25 * math.sin(a)
        if abs(x) > 1.4 and abs(z) > 1.4: m.box(x - 0.3, 0, z - 0.3, x + 0.3, 0.5, z + 0.3, accent=True)
    m.metal(-0.5, 0.5, -0.5, 0.5, 12, 0.5)
    # the pinion above it: a second level at 3.6 m, one ladder up each side of the arbor (notched over them)
    m.metal(-2.4, 3.6, -2.4, 2.4, 3.9, 2.4)
    for k in range(12):
        a = k * math.pi / 6 + 0.26; x, z = 2.55 * math.cos(a), 2.55 * math.sin(a)
        m.box(x - 0.18, 3.6, z - 0.18, x + 0.18, 3.9, z + 0.18, mat="trim")
    m.box(-0.5, 0.5, 2.4, 0.5, 3.6, 2.6, accent=True, mirror=True)                # a leaf under the pinion's rim with a ladder on it
    m.ladder(0, 2.6, 0.5, 3.9, "n", width=0.8); m.ladder(0, -2.6, 0.5, 3.9, "s", width=0.8)
    m.light(0, 3.2, 2.0, 7, 0.8, fixture=False, mirror=True)
    # the spokes: brass strips along the four ways over the works
    for (a0, b0, a1, b1) in ((-1.2, 3.5, -1.0, 8), (1.0, 3.5, 1.2, 8), (3.5, -1.2, 8, -1.0), (3.5, 1.0, 8, 1.2)): m.box(a0, 0, b0, a1, 0.12, b1, accent=True, mirror=True)
    # the works below the floor: gear teeth turning in the dark (scenery under the lethal drop)
    for (x, z, r) in ((-5.5, -5.5, 2.0), (5.5, -5.5, 1.6), (-5.5, 5.5, 1.6), (5.5, 5.5, 2.0), (-5.5, 0, 1.2), (5.5, 0, 1.2)):
        m.metal(x - r, -4.2, z - r, x + r, -3.8, z + r)
        m.light(x, -3.0, z, 4, 0.5, fixture=False)
    # pinion stacks in the corners: tall columns of gears (two with ladders to their 9 m tops: perches over the works)
    for (cx, cz) in ((-11, -11), (11, -11)):
        m.box(cx - 1.2, 0, cz - 1.2, cx + 1.2, 9, cz + 1.2, accent=True, mirror=True)
        for y in (3.0, 6.0): m.box(cx - 1.55, y, cz - 1.55, cx + 1.55, y + 0.3, cz + 1.55, mat="trim", mirror=True)
        m.glow(cx - 1.1, 9.0, cz - 1.1, cx + 1.1, 9.08, cz + 1.1, mirror=True)
    m.ladder(-11, 9.8, 0, 9, "s", width=0.8); m.ladder(11, -9.8, 0, 9, "n", width=0.8)
    # mainspring barrels on the flanks: a drum on its side with bands round it (cover, and a low perch)
    m.box(-14, 0, -3, -12, 3.5, 3, mirror=True)
    for z in (-2.2, 0, 2.2): m.box(-14.06, 0.2, z - 0.15, -11.94, 3.56, z + 0.15, mat="trim", mirror=True)
    m.glow(-13.6, 3.5, -2.6, -12.4, 3.6, 2.6, mirror=True)
    # the escapement decks: raised corners with steps (a low second level on each side)
    m.box(-18, 0, -18, -14, 2, -14, mirror=True); m.stairs(-14, -18, -12, -14, 2, "x", -1, mirror=True)
    m.box(14, 0, -18, 18, 2, -14, mirror=True); m.stairs(12, -18, 14, -14, 2, "x", 1, mirror=True)
    # the ratchet block and the gear guards (perforated brass sheet: it hides you, it doesn't stop a shot) in front of
    # the spawns, and a pendulum hanging between them (walk under the bob, hook the rod)
    m.box(-3, 0, 13, 3, 1.2, 14, mirror=True)
    m.wallbang(-6, 0, 10, -2, 2.2, 11, mirror=True); m.wallbang(2, 0, 10, 6, 2.2, 11, mirror=True)
    m.metal(-0.15, 4.0, 11.85, 0.15, 12, 12.15, mirror=True)
    m.box(-1.2, 2.4, 11.7, 1.2, 4.0, 12.3, accent=True, mirror=True); m.glow(-1.25, 3.1, 11.65, 1.25, 3.3, 12.35, mirror=True)
    # the clock face on each end wall, seen from behind: numerals glowing through, the hands, a bright centre
    for k in range(12):
        a = k * math.pi / 6; x, y = 4.4 * math.sin(a), 6.6 + 4.4 * math.cos(a)
        w = 0.5 if k % 3 == 0 else 0.25
        m.glow(x - w, y - w, -17.96, x + w, y + w, -17.9, mirror=True)
    m.box(-0.2, 6.6, -17.96, 0.2, 10.2, -17.8, accent=True, mirror=True); m.box(0, 6.4, -17.96, 2.9, 6.8, -17.8, accent=True, mirror=True)
    m.glow(-0.4, 6.2, -17.98, 0.4, 7.0, -17.85, mirror=True)
    m.light(0, 6.6, -16, 12, 0.9, fixture=False, mirror=True)
    # the escapement wheel on each side wall: a ring of teeth (detail, and hooks for the grapple)
    for k in range(20):
        a = k * math.pi / 10; z, y = 4.6 * math.cos(a), 6.6 + 4.6 * math.sin(a)
        m.box(17.6, y - 0.3, z - 0.3, 18, y + 0.3, z + 0.3, accent=True, mirror=True)
    m.metal(17.7, 6.1, -0.5, 18, 7.1, 0.5, mirror=True)
    # the great gear under the lid (a hook point over the middle) and lamps round the case
    for k in range(16):
        a = k * math.pi / 8; x, z = 5.0 * math.cos(a), 5.0 * math.sin(a)
        m.box(x - 0.5, 10.6, z - 0.5, x + 0.5, 11.0, z + 0.5, accent=True)
    m.metal(-4.5, 11.0, -4.5, 4.5, 11.4, 4.5)
    for (x, z) in ((-16, -8), (-16, 8), (-8, -16), (8, -16), (-16, 0), (0, -16)): m.light(x, 6.0, z, 9, 0.7, mirror=True)
    for (x, z) in ((-9, -9), (9, -9), (-14, -14), (14, -14), (0, -9)): m.light(x, 2.8, z, 6, 0.6, fixture=False, mirror=True)
    # the balconies: a walkway 6 m up along each side wall (where the escapement wheel turns), up a ladder from the
    # mainspring barrel's top at its middle and stairs at its far end: the upper level of the duel, a loop over it all
    m.box(-18, 5.7, -10, -15.5, 6.0, 14, mirror=True)
    for (b0, b1) in ((-10, -0.8), (0.8, 14)): m.box(-15.6, 6.0, b0, -15.5, 7.0, b1, accent=True, mirror=True)   # rail (open at the ladder)
    m.box(-15.5, 3.5, -0.8, -14.5, 6.0, 0.8, mirror=True)                       # a post up from the barrel to the balcony
    m.ladder(-14.5, 0, 3.6, 6.0, "w", width=0.8); m.ladder(14.5, 0, 3.6, 6.0, "e", width=0.8)   # (from the barrel's cap)
    m.stairs(-18, -14, -15.5, -10, 6.0, "z", 1, mirror=True, base=2.0)         # up from the escapement deck at the far end
    for z in (-10, 10): m.light(-16.8, 5.2, z, 6, 0.6, fixture=False, mirror=True)
    for x in (-4, 4): m.spawn("a", x, 0, 16, 0, "b")
    m.spawn("any", -15, 0, 7, 90, "any")
    m.raw("setting inside a giant clock")
    m.save("clockwork")
