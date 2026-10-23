"""SUMMIT: king of the hill. Outdoors on a mountain top: the hill is the peak, three terraces up with the
stairs turning a corner each tier, a beacon mast on top. Crevasses in the corners, two mountain huts."""
from .prefabs import *


def build():
    m = M("Summit", "koth")
    cre = [(13, 13, 19, 19, "water"), (-19, -19, -13, -13, "water"), (13, -19, 19, -13, "water"), (-19, 13, -13, 19, "water")]
    m.room(26, 26, 10, pits=cre, style="yard", skyline="mountains")
    m.box(-11, 0, -11, 11, 1.5, 11)                                            # terraces
    m.box(-8, 1.5, -8, 8, 3.0, 8, accent=True)
    m.box(-5, 3.0, -5, 5, 4.5, 5)
    m.raw("hill -5 4.5 -5 5 7.5 5")
    m.stairs(-2, 11, 2, 14, 1.5, "z", -1); m.stairs(-2, -14, 2, -11, 1.5, "z", 1)
    m.stairs(8, -2, 11, 2, 3.0, "x", -1, base=1.5); m.stairs(-11, -2, -8, 2, 3.0, "x", 1, base=1.5)
    m.stairs(-2, 5, 2, 8, 4.5, "z", -1, base=3.0); m.stairs(-2, -8, 2, -5, 4.5, "z", 1, base=3.0)
    m.metal(-0.35, 4.5, -0.35, 0.35, 11, 0.35)                                 # the beacon
    for (dx, dz) in ((-1, 0), (1, 0), (0, -1)): m.metal(dx * 1.4 - 0.08, 4.5, dz * 1.4 - 0.08, dx * 1.4 + 0.08, 7, dz * 1.4 + 0.08)   # (the fourth side is the ladder)
    m.box(-0.9, 11, -0.9, 0.9, 11.3, 0.9, accent=True); m.glow(-0.6, 11.3, -0.6, 0.6, 12.3, 0.6)
    m.light(0, 10.5, 0, 10, 0.6, fixture=False)
    m.box(-4, 4.5, -1, -2.6, 5.5, 1, mirror=True)                              # rocks on the peak
    m.box(-10.5, 1.5, 5, -9, 2.6, 7.5, mirror=True); m.box(6, 3.0, -7.5, 7.5, 3.9, -6, mirror=True)
    m.building(15, -5, 23, 5, 3.6, [("w", 0, 2.4), ("n", 19, 2.2), ("s", 19, 2.2)], mirror=True, notch=(14.4, 2.4, 15.6, 3.6))   # mountain huts
    for (a0, b0, a1, b1) in minus((14.4, -5.6, 23.6, 5.6), [(14.4, 2.4, 15.6, 3.6)]): m.box(a0, 3.95, b0, a1, 4.3, b1, mirror=True)   # snowy second roof tier
    for (x, z, w, hh) in ((-24.5, -4, 2.4, 1.6), (-17, 8, 3, 2.2), (-6, 18, 2.6, 1.4), (6, 21, 2, 1.2), (-23, 21, 2.4, 2.6)):
        m.box(x - w / 2, 0, z - w / 2, x + w / 2, hh, z + w / 2, mirror=True)   # boulders
    for x in (-8, -4, 0, 4, 8): m.spawn("a", x, 0, 23, 0, "b")
    m.spawn("any", -12.5, 0, 0, 90, "any")
    # the cliff ledge round the rim: an overhanging rock shelf at 4 m on rock pillars. A second ring to fight on,
    # shelter from the peak's sightlines underneath, ramps up at four points (point symmetric)
    L, LY = 3.5, 4.0
    for side in (-1, 1):
        zi, zo = (26 - L, 26) if side > 0 else (-26, -26 + L)
        m.box(-26, LY, zi, 26, LY + 0.6, zo); m.box(-26, LY - 0.25, zi - 0.15 if side > 0 else zo, 26, LY, zi if side > 0 else zo + 0.15, accent=True)
        xi, xo = (26 - L, 26) if side > 0 else (-26, -26 + L)
        m.box(xi, LY, -26 + L, xo, LY + 0.6, 26 - L); m.box(xi - 0.15 if side > 0 else xo, LY - 0.25, -26 + L, xi if side > 0 else xo + 0.15, LY, 26 - L, accent=True)
    for c in (-18, -6, 6, 18):                                                 # rock pillars under the shelf
        m.box(c - 0.6, 0, 26 - L + 0.3, c + 0.6, LY, 26 - L + 1.5, mirror=True); m.box(26 - L + 0.3, 0, c - 0.6, 26 - L + 1.5, LY, c + 0.6, mirror=True)
    m.stairs(-10, 26 - L - 3.2, -6, 26 - L, LY, "z", 1, mirror=True)          # ramps up onto the ledge
    m.stairs(26 - L - 3.2, 8, 26 - L, 12, LY, "x", 1, mirror=True)
    for (x, z) in ((-12, 24), (0, 24), (12, 24), (24, 0)):                    # lamps under the overhang
        m.glow(x - 0.5, LY - 0.4, z - 0.2, x + 0.5, LY - 0.25, z + 0.2, mirror=True); m.light(x, LY - 0.8, z, 8, 0.6, fixture=False, mirror=True)
    # ladders up the mountain huts: their roofs are flank perches over the terraces
    m.ladder(15, 3, 0, 4.3, "e", width=0.9); m.ladder(-15, -3, 0, 4.3, "w", width=0.9)
    # rock strata on the cliff walls (staggered bands, not solid)
    R_ = random.Random("summit-strata")
    for y in (1.4, 2.9, 6.2, 7.8):
        for side in (-1, 1):
            x = -26.0
            while x < 26:
                ln = R_.uniform(3, 9)
                if R_.random() < 0.7: m.box(x, y, side * 26 - (0.06 if side > 0 else -0.0), min(26, x + ln), y + 0.18, side * 26 + (0 if side > 0 else 0.06), mat="trim", mirror=True)
                x += ln + R_.uniform(0.5, 2.5)
    # the beacon's platform at 7.5 m (a ladder up the mast): the top of the mountain, over the hill
    m.box(-1.7, 7.2, -1.7, 1.7, 7.5, 1.7, accent=True)
    m.box(-0.45, 4.5, 1.7, 0.45, 7.2, 2.0, mat="metal")                            # the ladder's post at the platform's edge
    m.ladder(0, 2.0, 4.5, 7.5, "n", width=0.8)
    # cairns on the peak (cover to hold the hill from), firewood stacks for the beacon on the terraces (timber: it
    # stops you, not a bullet), and lanterns round the terraces
    for (x, z) in ((-3.2, 3.4), (3.4, 3.2), (-1.0, 3.8), (3.8, -0.6), (-3.9, 0.6), (1.2, -3.9)): m.box(x - 0.55, 4.5, z - 0.55, x + 0.55, 5.6, z + 0.55, mirror=True)
    m.box(-2.6, 4.5, -2.6, -1.6, 5.2, -1.2, mirror=True)                                 # a toppled boulder by the beacon
    for (x0, z0, x1, z1) in ((-9.6, -9.8, -7.2, -9.2), (6.8, 8.4, 9.4, 9.0)): m.wallbang(x0, 1.5, z0, x1, 2.7, z1, mirror=True)
    for (x0, z0, x1, z1) in ((-6.8, 5.6, -5.4, 6.4),): m.wallbang(x0, 3.0, z0, x1, 4.1, z1, mirror=True)
    for (x, z, y) in ((-10.5, 0, 1.5), (0, -10.5, 1.5), (-7.5, 7.5, 3.0), (7.5, 7.5, 3.0), (-4.5, -4.5, 4.5), (4.5, -4.5, 4.5), (-18, -18, 0), (18, -18, 0)):
        m.metal(x - 0.08, y, z - 0.08, x + 0.08, y + 2.2, z + 0.08, mirror=True); m.light(x, y + 2.0, z, 7, 0.6, mirror=True)
    m.raw("setting mountain peak")
    m.save("summit")
