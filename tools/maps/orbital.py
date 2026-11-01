"""ORBITAL: team vs on a space station. Each team starts in its docking bay beside its shuttle (stairs up to a
landing and the upper deck), through the bulkhead into the station: a cargo hall in the middle, a corridor
along each side with the upper deck on its roof, and the hub with the reactor core, a catwalk ring round it and
two observation windows open to space (fall out and you're gone). Indoors, two levels, point symmetric."""
from .prefabs import *


def build():
    m = M("Orbital", "vs", sun=(0.3, 1.0, 0.2))
    m.room(30, 16, 12, pits=[(-6, -13, 6, -9, "abyss"), (-6, 9, 6, 13, "abyss")], sunken=[(-17, -8, -13, -4, 1.5, "metal"), (13, 4, 17, 8, 1.5, "metal")], style="factory", skyline=None)
    m.raw("indoor 12")
    m.box(-32, 12, -18, 32, 12.5, 18)                                          # the hull above
    # the long hull walls (z 16..18) rebuilt with windows out to space: clip glass (see and shoot out, nobody falls
    # out), metal frames and a mullion down each. Tall ones in the docking bays (behind the stairs), one per corridor,
    # low ones each side of the hub walkway and a long band at the observation gallery (8 m deck, eye at 9.6 m)
    m.lines = [l for l in m.lines if l not in ("box -30 -4 -18 30 12 -16", "box -30 -4 16 30 12 18")]
    OWIN = [(-27.5, 1.4, -22.5, 9.0), (-16.5, 1.4, -12.0, 3.6), (-5.6, 1.6, -1.0, 3.6), (1.0, 1.6, 5.6, 3.6), (-5.6, 8.9, 5.6, 11.2)]
    for (a0, y0, a1, y1) in minus((-30, -4, 30, 12), OWIN): m.box(a0, y0, -18, a1, y1, -16, mirror=True)
    for (a0, y0, a1, y1) in OWIN:
        m.box(a0, y0, -17.2, a1, y1, -16.8, mat="clip", mirror=True)                      # the glass
        for (b0, c0, b1, c1) in ((a0 - 0.15, y0 - 0.15, a1 + 0.15, y0), (a0 - 0.15, y1, a1 + 0.15, y1 + 0.15), (a0 - 0.15, y0, a0, y1), (a1, y0, a1 + 0.15, y1)):
            m.metal(b0, c0, -16.15, b1, c1, -15.95, mirror=True)                          # frame, proud of the wall
        if a1 - a0 > 4: m.metal((a0 + a1) / 2 - 0.08, y0, -17.0, (a0 + a1) / 2 + 0.08, y1, -16.6, mirror=True)   # mullion
        m.glow(a0, y0 - 0.15, -16.0, a1, y0 - 0.1, -15.9, mirror=True)                    # sill light strip
    # the view: unlit 'far' scenery well outside the hull (no collision, no lightmap; stars are drawn by the sky
    # shader for a dark sky, so no brushes for them). South: a ringed gas giant above the station; north: a moon
    # and a smaller one further off.
    def far_ball(cx, cy, cz, r, n=10):
        """a blocky sphere: n slabs up it, each a square plus a cross (an octagon in plan)"""
        for i in range(n):
            ya, yb = cy - r + 2 * r * i / n, cy - r + 2 * r * (i + 1) / n
            ym = max(abs(ya - cy), abs(yb - cy)) if (ya - cy) * (yb - cy) > 0 else 0
            w = (r * r - ym * ym) ** 0.5
            if w < 0.5: w = r * 0.3
            m.box(cx - w * 0.72, ya, cz - w * 0.72, cx + w * 0.72, yb, cz + w * 0.72, mat="far")
            m.box(cx - w, ya, cz - w * 0.4, cx + w, yb, cz + w * 0.4, mat="far")
            m.box(cx - w * 0.4, ya, cz - w, cx + w * 0.4, yb, cz + w, mat="far")
    far_ball(-30, 26, -230, 66, n=18)                                            # the gas giant, high over the hub
    for (r0, r1) in ((82, 96), (100, 106)):                                      # its rings, flat at its equator
        for (a, b, c, d) in ((-r1, -r1, r1, -r0), (-r1, r0, r1, r1), (-r1, -r0, -r0, r0), (r0, -r0, r1, r0)):
            m.box(-30 + a, 26, -230 + b, -30 + c, 26.6, -230 + d, mat="far")
    far_ball(40, 24, 140, 20, n=10)                                              # the moon
    far_ball(-70, -20, 240, 9, n=6)                                              # a far moonlet
    for x in range(-27, 28, 6):
        for z in (-12, 0, 12): m.glow(x - 1.2, 11.9, z - 0.4, x + 1.2, 12, z + 0.4)    # ceiling light panels
    # stars below the observation windows, and glowing frames round them
    for (x, z) in ((-4, -12), (-1, -10), (3, -11.5), (5, -9.6), (-5, -9.5), (1, -12.6), (-2.5, -11), (4, -10.4)):
        m.glow(x - 0.06, -13.0, z - 0.06, x + 0.06, -12.9, z + 0.06, mirror=True)
    for (a0, b0, a1, b1) in ((-6.1, -13.1, 6.1, -13), (-6.1, -9, 6.1, -8.9), (-6.1, -13, -6, -9), (6, -13, 6.1, -9)): m.glow(a0, -0.05, b0, a1, 0.02, b1, mirror=True)
    m.box(-6, 0, -9.1, 6, 1.0, -8.95, accent=True, mirror=True)                # rail on the hub side of each window
    # ---- the docking bays (x -30..-18 and mirrored): the shuttle, the bay door's force field, stairs to the upper deck
    m.metal(-27, 0.3, -2.5, -20, 2.6, 2.5, mirror=True)                       # hull
    m.box(-20, 0.8, -1.6, -18.8, 2.3, 1.6, accent=True, mirror=True); m.glow(-19.0, 1.4, -1.2, -18.75, 2.1, 1.2, mirror=True)   # cockpit
    m.box(-26, 0.8, 2.5, -24, 1.2, 4.5, accent=True, mirror=True); m.box(-26, 0.8, -4.5, -24, 1.2, -2.5, accent=True, mirror=True)   # wings
    m.glow(-27.3, 0.8, -1.8, -27, 2.1, -0.6, mirror=True); m.glow(-27.3, 0.8, 0.6, -27, 2.1, 1.8, mirror=True)   # engines
    for (x, z) in ((-25.5, -2.4), (-21.5, -2.4), (-25.5, 2.0), (-21.5, 2.0)): m.metal(x - 0.15, 0, z, x + 0.15, 0.3, z + 0.4, mirror=True)   # landing struts
    m.stairs(-22, 2.5, -20, 6.5, 2.6, "z", -1, mirror=True)                     # boarding steps onto the shuttle's back
    m.glow(-29.95, 1.0, -8, -29.9, 9.0, 8, mirror=True)                         # the bay door's force field
    for z in (-8.2, 8.0): m.box(-30, 0, z, -29.6, 9.2, z + 0.2, accent=True, mirror=True)
    m.box(-30, 9.0, -8.2, -29.6, 9.4, 8.2, accent=True, mirror=True)
    m.stairs(-29, -16, -21, -14, 4.5, "x", 1, mirror=True)                      # up to the bay's landing
    m.box(-21, 4.2, -16, -18.4, 4.5, -11, mirror=True)                          # the landing
    for (x, z) in ((-27, -10), (-27, 10), (-22, -10), (-22, 10)): m.light(x, 3.0, z, 8, 0.7, mirror=True)
    m.crates(-28, -7, 2, size=1.2, mirror=True); m.crates(-23, 9, 1, size=1.2, mirror=True)
    # ---- the bulkheads between bays and station: doors on the floor (both corridors and the cargo hall) and one
    # up on the deck from the landing
    for (a0, y0, a1, y1) in minus((-16, 0, 16, 12), [(-14.5, 0, -11.5, 3.0), (-2.5, 0, 2.5, 3.2), (11.5, 0, 14.5, 3.0), (-14.5, 4.5, -11.5, 7.5)]):
        m.box(-18.4, y0, a0, -18, y1, a1, mirror=True)
    for (z0, z1, y0, y1) in ((-14.6, -11.4, 3.0, 3.15), (-2.6, 2.6, 3.2, 3.35), (11.4, 14.6, 3.0, 3.15), (-14.6, -11.4, 7.5, 7.65)): m.glow(-18.45, y0, z0, -17.95, y1, z1, mirror=True)
    # ---- the side corridors (x -18..-8): windows into the cargo hall, the upper deck on their roofs, a maintenance
    # ladder up from each
    m.windowwall(-18, -10, -8, -9.6, 4.2, 1.0, 2.6, every=2.5, width=1.4, wallbang=True, mirror=True)   # glass: shoot through it
    m.box(-18, 4.5, -10, -8, 5.4, -9.9, accent=True, mirror=True)               # deck rail over the hall
    for x in (-16, -12): m.light(x, 3.6, -13, 6, 0.6, mirror=True); m.glow(x - 0.8, 4.15, -13.4, x + 0.8, 4.2, -12.6, mirror=True)
    m.ladder(-10, -15.96, 0, 4.5, "n", width=0.8)
    m.ladder(10, 15.96, 0, 4.5, "s", width=0.8)
    for (a0, b0, a1, b1) in minus((-18, -16, -8, -10), [(-10.6, -16, -9.4, -15.0)]): m.box(a0, 4.2, b0, a1, 4.5, b1, mirror=True)   # (notched over the ladder)
    # ---- the cargo hall (middle, x -18..-8): crate stacks, a cargo lift platform, pipes overhead
    m.crates(-15, -5, 2, size=1.4, mirror=True); m.crates(-11, 4, 2, size=1.4, mirror=True)
    m.box(-14, 0, -1.5, -11, 0.6, 1.5, accent=True, mirror=True)               # the lift platform
    for z in (-1.4, 1.2): m.metal(-14, 0.6, z, -11, 4.0, z + 0.2, mirror=True)
    m.box(-14.2, 4.0, -1.6, -10.8, 4.3, 1.6, accent=True, mirror=True)
    m.metal(-18, 8.0, -0.3, 18, 8.4, 0.3)                                        # the spine pipe down the middle (a hook)
    # ---- the hub: the reactor core, consoles round it, the catwalk ring on the upper deck with bridges to the decks
    m.metal(-1.8, 0, -1.8, 1.8, 12, 1.8)
    for y in (2.0, 5.5, 9.0): m.glow(-1.9, y, -1.9, 1.9, y + 0.25, 1.9)
    m.light(0, 3.0, 3.2, 8, 0.9, fixture=False, mirror=True); m.light(3.2, 3.0, 0, 8, 0.9, fixture=False, mirror=True)
    for (x, z) in ((-3.2, -3.2), (3.2, -3.2)): m.box(x - 0.7, 0, z - 0.5, x + 0.7, 1.1, z + 0.5, accent=True, mirror=True); m.glow(x - 0.6, 1.1, z - 0.4, x + 0.6, 1.15, z + 0.4, mirror=True)
    for (a0, b0, a1, b1) in ((-6, -6, 6, -4), (-6, -4, -4, 4)): m.box(a0, 4.2, b0, a1, 4.5, b1, mirror=True)
    m.box(-6, 4.5, -6, 6, 5.4, -5.9, accent=True, mirror=True)
    m.box(-8, 4.2, -10, -6, 4.5, -6, mirror=True)                                # bridge from the corridor deck to the ring
    for (x, z) in ((-5, 5), (5, -5)): m.light(x, 6.5, z, 7, 0.6, mirror=True)
    # the loading pits in the cargo hall (sunk 1.5 m, steps out at one end): cover from the hall, a low level
    m.stairs(-15, -8, -13, -4, 0, "x", 1, mirror=True, base=-1.5)
    # the observation gallery at 8 m along the hull over each window, up a ladder from the corridor deck: the high
    # perch over the hub (and in plain sight of the other gallery)
    # (1.17: the ladder climbs the gallery's front, a strut under its edge, facing the hull: at 6.75 m/s you come
    # out over the top with a hop that carries you a few metres, and the hull stops you on the gallery. Up the hull
    # through a hole in its floor, stepping back off the top threw you over the rail into the hub.)
    m.box(-8, 7.7, -16, 8, 8.0, -13.6, mirror=True)
    for (a0, a1) in ((-8, -7.5), (-6.5, 8)): m.box(a0, 8.0, -13.7, a1, 8.9, -13.6, accent=True, mirror=True)   # (open over the ladder)
    m.box(-7.6, 4.5, -14.0, -6.4, 7.7, -13.6, mat="metal", mirror=True)        # the strut the ladder runs up
    m.ladder(-7, -13.6, 4.5, 8.0, "n", width=0.8)
    m.ladder(7, 13.6, 4.5, 8.0, "s", width=0.8)
    m.box(-8, 4.2, -16, -6, 4.5, -10, mirror=True)                                # the deck runs on to the foot of the gallery ladder
    for x in (-4, 0, 4): m.light(x, 7.2, -14.8, 6, 0.6, mirror=True)
    # ---- the outer strips behind the windows: walkways along the hull to flank the hub
    for x in (-4, 4): m.box(x - 0.6, 0, -15.4, x + 0.6, 1.4, -14.6, mirror=True)
    for x in (-24, -20): m.spawn("a", x, 0, -9, -90, "b"); m.spawn("a", x, 0, 9, -90, "b")
    for (x, z, yaw) in ((-28, 0, -90), (-13, -12.5, -90), (-12, 7, -90)): m.spawn("any", x, 0, z, yaw, "any")
    m.raw("setting space station")
    m.also = ["power"]   # (power: team vs with team health pools)
    m.save("orbital")
