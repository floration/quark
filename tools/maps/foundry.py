"""FOUNDRY: built for tag (every mode works). A huge indoor factory hall under a sawtooth roof: a grid of steel columns, overhead
crane rails with travelling cranes and hanging hooks (grapple anchors everywhere), the casting pit in the
middle with the giant ladle pouring into it, conveyor lines and presses on the floor, tanks in the corners,
and a catwalk ring at 6 m with bridges across. Ribbed metal walls, loading bay shutters, skylights."""
from .prefabs import *


def build():
    m = M("Foundry", "tag")
    HX, HZ, H = 34, 26, 16
    m.raw(f"indoor {H}")
    m.room(HX, HZ, H, pits=[(-3, -3, 3, 3, "molten"), (-11, -0.8, -3, 0.8, "molten"), (3, -0.8, 11, 0.8, "molten")], style="factory", walls=False)
    # shell: thick walls, roof slab, sawtooth roof lights (glow strips under each tooth), ribbed wall panels
    m.box(-HX - 2, -1, -HZ - 2, -HX, H, HZ + 2); m.box(HX, -1, -HZ - 2, HX + 2, H, HZ + 2)
    m.box(-HX, -1, -HZ - 2, HX, H, -HZ); m.box(-HX, -1, HZ, HX, H, HZ + 2)
    m.box(-HX - 2, H, -HZ - 2, HX + 2, H + 0.8, HZ + 2)
    for x in range(-HX + 4, HX, 8):                                            # the sawtooth: a tooth per bay, a skylight under each
        m.box(x - 4, H + 0.8, -HZ - 2, x, H + 3.0, HZ + 2, mat="far")
        m.glow(x - 0.6, H - 0.1, -HZ + 1, x + 0.6, H, HZ - 1)
        for z in (-16, -6, 6, 16): m.light(x, H - 3.5, z, 18, 1.3, fixture=False)   # high-bay lamps: the hall's only light
    for x in range(-HX + 2, HX - 1, 2):                                        # ribs on the long walls
        for side in (-1, 1):
            z0, z1 = (HZ - 0.25, HZ) if side > 0 else (-HZ, -HZ + 0.25)
            m.metal(x - 0.12, 1.2, z0, x + 0.12, H - 1.0, z1)
    for z in range(-HZ + 2, HZ - 1, 2):                                        # ...and the end walls
        for side in (-1, 1):
            x0, x1 = (HX - 0.25, HX) if side > 0 else (-HX, -HX + 0.25)
            if abs(z) <= 5: continue                                           # the loading bay shutters go here
            m.metal(x0, 1.2, z - 0.12, x1, H - 1.0, z + 0.12)
    for side in (-1, 1):
        zf0, zf1 = (HZ - 0.35, HZ) if side > 0 else (-HZ, -HZ + 0.35)
        m.box(-HX, 0, zf0, HX, 1.2, zf1, accent=True)                          # dado
        xf0, xf1 = (HX - 0.35, HX) if side > 0 else (-HX, -HX + 0.35)
        m.box(xf0, 0, -HZ, xf1, 1.2, HZ, accent=True)
        # loading bay shutter in each end wall: slatted, hazard-striped frame
        for y in [i * 0.5 for i in range(1, 12)]: m.metal(xf0 - 0.05, y, -5, xf1, y + 0.08, 5)
        m.glow(xf0 - 0.08, 0, -5.3, xf1, 6.2, -5.0); m.glow(xf0 - 0.08, 0, 5.0, xf1, 6.2, 5.3); m.glow(xf0 - 0.08, 6.0, -5.3, xf1, 6.3, 5.3)
    # columns: a grid of steel columns holding the crane rails
    for x in (-24, -12, 12, 24):
        for z in (-18, 18):
            m.metal(x - 0.5, 0, z - 0.5, x + 0.5, H - 3.0, z + 0.5)
            m.box(x - 0.8, 0, z - 0.8, x + 0.8, 0.5, z + 0.8, accent=True)
            m.glow(x - 0.55, 2.2, z - 0.55, x + 0.55, 2.35, z + 0.55)
    for z in (-18, 18):                                                        # crane rails along the hall and two cranes on them
        m.metal(-HX, H - 3.0, z - 0.4, HX, H - 2.4, z + 0.4)
    for (cx, w) in ((-16, 2.4), (16, 2.4)):
        m.box(cx - w / 2, H - 4.2, -18, cx + w / 2, H - 3.0, 18, accent=True)   # crane bridge
        m.metal(cx - 1.4, H - 5.4, -1.2 + (6 if cx < 0 else -6), cx + 1.4, H - 4.2, 1.2 + (6 if cx < 0 else -6))   # trolley
        hz_ = 6 if cx < 0 else -6
        m.metal(cx - 0.06, 7.2, hz_ - 0.06, cx + 0.06, H - 5.4, hz_ + 0.06)  # cable
        m.box(cx - 0.6, 6.2, hz_ - 0.4, cx + 0.6, 7.2, hz_ + 0.4, accent=True)  # hook block (grapple it)
        m.glow(cx - 0.62, 6.6, hz_ - 0.42, cx + 0.62, 6.7, hz_ + 0.42)
    # the ladle over the casting pit (the centrepiece)
    m.metal(-HX, 11.6, -0.6, HX, 12.4, 0.6)
    m.metal(-2.2, 7.4, -2.2, 2.2, 7.8, 2.2)
    for (a, b, c, d) in ((-2.2, -2.2, 2.2, -1.8), (-2.2, 1.8, 2.2, 2.2), (-2.2, -1.8, -1.8, 1.8), (1.8, -1.8, 2.2, 1.8)): m.metal(a, 7.8, b, c, 9.4, d)
    m.glow(-1.8, 9.0, -1.8, 1.8, 9.2, 1.8); m.box(2.2, 8.8, -0.5, 3.0, 9.2, 0.5, accent=True)
    for x in (-2.0, 2.0): m.metal(x - 0.08, 9.4, -0.08, x + 0.08, 11.6, 0.08)
    m.glow(2.55, -3.6, -0.15, 2.85, 8.8, 0.15)
    m.light(0, 5.5, 0, 11, 0.8, fixture=False)
    for (x, z) in ((0, -5.5), (0, 5.5)): m.metal(x - 0.7, 0, z - 0.7, x + 0.7, 1.2, z + 0.7); m.glow(x - 0.5, 1.2, z - 0.5, x + 0.5, 1.25, z + 0.5)
    # conveyor lines: long low belts with rollers and glow edges (cover you can jump on), point symmetric
    def conveyor(x0, x1, z, mirror=True):
        m.metal(x0, 0, z - 0.9, x1, 0.6, z + 0.9, mirror=mirror)
        m.box(x0, 0.6, z - 0.75, x1, 0.75, z + 0.75, accent=True, mirror=mirror)          # the belt
        for x in range(int(x0) + 1, int(x1), 2): m.metal(x - 0.06, 0.6, z - 0.95, x + 0.06, 0.8, z + 0.95, mirror=mirror)
        m.glow(x0, 0.45, z - 0.93, x1, 0.52, z - 0.9, mirror=mirror); m.glow(x0, 0.45, z + 0.9, x1, 0.52, z + 0.93, mirror=mirror)
    conveyor(-28, -14, -9)
    conveyor(14, 26, -12)
    # presses: tall machines with a head over a bed, a gap you can run through
    def press(cx, cz):
        for dx in (-1.6, 1.6): m.metal(cx + dx - 0.4, 0, cz - 1.2, cx + dx + 0.4, 6.5, cz + 1.2, mirror=True)
        m.box(cx - 2.0, 6.5, cz - 1.4, cx + 2.0, 8.0, cz + 1.4, accent=True, mirror=True)
        m.metal(cx - 1.2, 4.2, cz - 1.0, cx + 1.2, 6.5, cz + 1.0, mirror=True)           # the ram
        m.box(cx - 1.6, 0, cz - 1.2, cx + 1.6, 1.0, cz + 1.2, mirror=True)                 # the bed
        m.glow(cx - 1.65, 1.0, cz - 1.25, cx + 1.65, 1.1, cz + 1.25, mirror=True)
    press(-20, 6); press(-8, 14)
    # tanks in the corners: stacked rings, ladders of pipe, and a valve platform
    for (cx, cz) in ((-29, -21), (29, -21)):
        for k, r in enumerate((3.0, 3.2, 3.0)):
            m.box(cx - r, k * 3.5, cz - r * 0.8, cx + r, (k + 1) * 3.5, cz + r * 0.8, mat=("metal" if k == 1 else None), mirror=True)
        m.box(cx - 3.3, 10.5, cz - 2.6, cx + 3.3, 10.8, cz + 2.6, accent=True, mirror=True)
        m.glow(cx - 3.25, 5.2, cz - 2.62, cx + 3.25, 5.35, cz + 2.62, mirror=True)
    # pipe racks along the long walls, high up (anchors, and they read as a factory)
    for y in (9.0, 9.6):
        m.metal(-HX, y, -HZ + 0.4, HX, y + 0.35, -HZ + 0.8, mirror=True)
    for x in (-30, -18, -6, 6, 18, 30): m.metal(x - 0.15, 9.0, -HZ + 0.25, x + 0.15, 10.2, -HZ + 1.0, mirror=True)
    # the catwalk ring at 6 m along both long walls, bridges across at the ends, stairs up
    m.catwalk(-30, -25.8, 30, -23.6, 6.0, rails="z", mirror=True)
    m.catwalk(-33.8, -23.6, -31.6, 23.6, 6.0, rails="x", mirror=True)
    m.stairs(-31.6, -2, -28.6, 10, 6.0, "z", 1, mirror=True)                 # (clear of the corner offices)
    m.stairs(6, -23.6, 18, -21.0, 6.0, "x", 1, mirror=True)
    m.catwalk(-9, -23.6, -7, -10, 6.0, rails="x", mirror=True)                 # a spur out over the floor
    # raised offices in two corners (the old corner platforms), with windows over the floor
    m.box(-34, 0, 14, -22, 3.0, 26, mirror=True); m.stairs(-22, 14, -19, 20, 3.0, "x", -1, mirror=True)
    m.windowwall(-34, 13.6, -24, 14.0, 6.0, 1.0, 2.6, every=3.0, width=1.8, wallbang=True, mirror=True, base=3.0)
    m.box(-34, 6.0, 13.6, -22, 6.4, 26, accent=True, mirror=True); m.light(-28, 5.2, 20, 7, 0.6, mirror=True)
    # floor markings: walkway lanes in hazard yellow (flush)
    for z in (-4.5, 4.5): m.box(-30, -0.03, z - 0.12, 30, 0, z + 0.12, mat="glow")
    # spawns and every mode's pieces
    for (x, z) in ((-26, -16), (26, 16), (-10, 16), (10, -16), (0, -14), (0, 14), (-26, 4), (26, -4), (-8, 6), (8, -6)):
        m.spawn("any", x, 0, z, round(math.degrees(math.atan2(x, z))))
    for x in (-4, 0, 4): m.spawn("a", x, 0, 19, 0, "b")
    m.raw("site -26 0 -6 -20 3 0"); m.raw("site 20 0 0 26 3 6")
    m.raw("hill -7 0 -7 7 3 7")
    for p in ((0, 22), (13.5, 11), (13.5, -11), (0, -22)): m.raw(f"path {p[0]:g} 0 {p[1]:g}")
    m.raw("checkpoint 26 0 -24 32 6 -18"); m.raw("checkpoint -32 0 18 -26 6 24")
    m.raw("checkpoint -1.5 0 7 1.5 4 10"); m.raw("checkpoint -3 0 18 3 4 22")
    for x in (-4, 0, 4): m.spawn("race", x, 0, -17, 45)
    # tool cribs along the walls: little rooms with one door (somewhere to duck out of a chase, and to hide)
    m.cribs([(-29, -6, "e"), (-29, 6, "e"), (-16, -21, "n"), (16, -21, "n"), (-6, -21, "n"), (6, 21, "s"), (-20, 10, "s"), (24, 6, "w"), (-24, -12, "e")], mirror=True, count=3)
    m.raw("setting factory floor")
    m.save("foundry")
