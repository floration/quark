"""RAILYARD: payload. A pushes from the south to the north along a winding track: through a roofed tunnel,
past the station platform, under a footbridge. A steam locomotive waits on the siding."""
from .prefabs import *


def build():
    m = M("Railyard", "payload")
    m.room(24, 46, 10, style="yard", skyline="city")
    path = [(0, 40), (0, 26), (-14, 26), (-14, 6), (0, 6), (0, -10), (14, -10), (14, -28), (0, -28), (0, -40)]
    for p in path: m.raw(f"path {p[0]:g} 0 {p[1]:g}")
    m.track(path)
    m.signal_gantry(36, 24); m.signal_gantry(-36, 24)
    m.container(-23, 32, -19, 38); m.container(19, -38, 23, -32)
    m.container(-23, -40, -17, -37); m.container(17, 37, 23, 40)
    m.container(-23, -40, -17, -37, h=5.2); m.container(17, 37, 23, 40, h=5.2)
    m.wallbang(21.5, 0, 33, 24, 2.4, 33.3); m.wallbang(-23, 0, -33.3, -20, 2.4, -33)
    for (x0, z0, x1, z1, h) in [(4, 30, 8, 34, 1.6), (-8, 30, -4, 34, 1.6), (4, -2, 7, 2, 1.4), (-6, -4, -3, 0, 2.2),
                               (18, -20, 21, -14, 1.6), (9.4, -25, 12.4, -21, 1.4), (-8, -34, -4, -30, 1.6), (4, -34, 8, -30, 1.6)]:
        m.box(x0, 0, z0, x1, h, z1)
    # the tunnel over the west leg of the track (doors in both walls half-way)
    m.doorwall(-18.4, 9, -18, 23, 4.6, [(16, 2.4, 2.6)], axis="z"); m.doorwall(-10, 9, -9.6, 23, 4.6, [(16, 2.4, 2.6)], axis="z")
    m.box(-18.6, 4.6, 8.6, -9.4, 5.1, 23.4, accent=True)
    m.glow(-18.0, 4.3, 8.6, -17.9, 4.45, 23.4); m.glow(-10.1, 4.3, 8.6, -10.0, 4.45, 23.4)
    m.light(-14, 3.8, 12, 7, 0.8); m.light(-14, 3.8, 20, 7, 0.8)
    # the station: a raised platform with a canopy on posts and a little ticket office
    m.box(-24, 0, -26, -17, 0.9, -10, accent=True); m.stairs(-17, -20, -15, -16, 0.9, "x", -1)
    for z in (-25, -18, -11): m.metal(-17.6, 0.9, z - 0.2, -17.2, 4.4, z + 0.2)
    m.box(-24, 4.4, -26.5, -16.5, 4.7, -9.5, accent=True); m.glow(-16.6, 4.2, -26.5, -16.5, 4.4, -9.5)
    m.building(-24, -24, -20, -19, 2.8, [("e", -21.5, 1.8)], base=0.9)
    m.light(-20, 3.8, -18, 8, 0.8)
    # footbridge over the yard
    m.catwalk(-9, -19, 9, -17, 4.5, rails="z")
    m.stairs(-12, -19, -9, -17, 4.5, "x", 1); m.stairs(9, -19, 12, -17, 4.5, "x", -1)
    # the locomotive on its siding (east), facing north
    for o in (-0.7, 0.7): m.metal(19 + o - 0.08, -0.05, 12, 19 + o + 0.08, 0, 32)
    m.metal(17.4, 0.4, 15, 20.6, 1.2, 29.5)                                    # frame
    m.box(17.7, 1.2, 19, 20.3, 3.4, 29, accent=True)                           # boiler
    for z in (21, 24, 27): m.metal(17.6, 1.2, z - 0.2, 20.4, 3.5, z + 0.2)     # boiler bands
    m.metal(18.5, 3.4, 26.8, 19.5, 5.4, 27.8); m.glow(18.4, 5.2, 26.7, 19.6, 5.4, 27.9)  # chimney
    m.box(17.4, 1.2, 15, 20.6, 4.4, 19, accent=True); m.metal(17.2, 4.4, 14.8, 20.8, 4.7, 19.2)   # cab
    m.wallbang(17.6, 2.6, 18.9, 20.4, 3.6, 19.05)
    for z in (16.5, 19.5, 22.5, 25.5, 28.5): m.metal(17.2, 0, z - 0.7, 17.5, 1.4, z + 0.7); m.metal(20.5, 0, z - 0.7, 20.8, 1.4, z + 0.7)
    m.glow(18.6, 2.2, 29, 19.4, 2.8, 29.2)                                     # headlamp
    m.box(17.4, 0, 29.5, 20.6, 0.9, 30.3, accent=True)                         # cow-catcher
    m.box(-24, 0, -6, -18, 2.5, 2, accent=True); m.stairs(-18, -6, -15, 2, 2.5, "x", -1)
    m.box(18, 0, 4, 24, 2.5, 10, accent=True); m.stairs(15, 4, 18, 10, 2.5, "x", 1)
    # second ways onto the raised platforms and the station (each reachable from two sides: no dead ends)
    m.stairs(-23, 2, -19, 5, 2.5, "z", -1)
    m.stairs(19, 1, 23, 4, 2.5, "z", 1)
    m.stairs(-24, -29, -20, -26, 0.9, "z", 1)
    # the engine house (north, where A starts) and the terminus shed (south, where B makes its last stand): the
    # line runs from one roof to the other, so both ends of the push are indoor fights
    def shed(z0, z1, roof_y, doors_z):
        for x in (-11, 11):
            for z in (z0 + 1, (z0 + z1) / 2, z1 - 1): m.metal(x - 0.35, 0, z - 0.35, x + 0.35, roof_y, z + 0.35)
        m.box(-12, roof_y, z0, 12, roof_y + 0.5, z1, accent=True)
        for x in range(-9, 10, 6): m.glow(x - 0.4, roof_y - 0.12, z0 + 1, x + 0.4, roof_y, z1 - 1)   # roof lights
        for z in (z0 + 4, z1 - 4): m.light(0, roof_y - 1.5, z, 12, 0.8, fixture=False)
        m.box(-12, roof_y - 1.4, doors_z - 0.25, 12, roof_y, doors_z + 0.25)    # the gable beam over the open end
    shed(-46, -30, 8.0, -30); shed(32, 46, 8.0, 32)
    # the water tower: four legs, a tank you can stand on (a lookout over the yard), a ladder up one leg
    for (dx, dz) in ((-1.6, -1.6), (1.6, -1.6), (-1.6, 1.6), (1.6, 1.6)): m.metal(21 + dx - 0.2, 0, -3 + dz - 0.2, 21 + dx + 0.2, 9, -3 + dz + 0.2)
    m.box(18.8, 9, -5.2, 23.2, 12, -0.8, accent=True)
    for (a0, b0, a1, b1) in minus((18.6, -5.4, 23.4, -0.6), [(20.4, -5.4, 21.6, -4.9)]): m.metal(a0, 12, b0, a1, 12.25, b1)   # notched over the ladder
    for (a0, a1) in ((18.75, 20.4), (21.6, 23.25)): m.glow(a0, 10.4, -5.25, a1, 10.55, -0.75)   # (gap over the ladder); m.metal(20.8, 12.25, -3.2, 21.2, 13.4, -2.8)
    m.metal(20.3, 0, -5.2, 21.7, 9, -4.6)                                      # the down-pipe the ladder runs up
    m.ladder(21, -5.2, 0, 12.25, "s", width=0.9)
    # the filler pipe on the tank, in line with the ladder: at 6.75 m/s you come over the top with a hop that would
    # carry you over the 4.8 m tank; this stops you on it (1.17)
    m.metal(20.7, 12.25, -3.3, 21.3, 15.2, -2.7)
    # freight wagons on two sidings: cover in the open middle of the yard (off the cart's track)
    def wagon(x0, z0, x1, z1):
        m.box(x0, 0.6, z0, x1, 3.0, z1, accent=True); m.metal(x0 - 0.05, 3.0, z0, x1 + 0.05, 3.2, z1)
        for z in (z0 + 0.8, z1 - 0.8): m.metal(x0 + 0.2, 0, z - 0.4, x1 - 0.2, 0.6, z + 0.4)
        m.box(x0 - 0.04, 1.0, (z0 + z1) / 2 - 1.2, x1 + 0.04, 2.6, (z0 + z1) / 2 + 1.2)   # the sliding door
    wagon(7, 10, 9.4, 17); wagon(-9.4, -27, -7, -20)
    # brick yard walls across the yard (gaps for the cart's track, the siding and a door in the middle): they cut
    # the long sightlines up the yard and split it into sections you fight through
    for (x0, x1) in ((-24, -17), (-11, -2), (2, 12)):
        m.box(x0, 0, 15.6, x1, 2.8, 16.4); m.box(x0 - 0.1, 2.8, 15.5, x1 + 0.1, 3.0, 16.5, accent=True)
    # sleeper stacks and buffer stops along the line: cover close to the track, where the cart fight is
    for (x, z) in ((-6, 22), (5, 20), (-19, 12), (-8, 2), (6, -6), (19, -22), (-4, -22), (5, -36), (-17, 34)):
        m.box(x - 1.3, 0, z - 0.6, x + 1.3, 0.5, z + 0.6, accent=True); m.box(x - 1.3, 0.5, z - 0.6, x + 1.3, 1.0, z + 0.6); m.box(x - 1.0, 1.0, z - 0.5, x + 1.0, 1.4, z + 0.5, accent=True)
    for (x0, x1) in ((-16, -2), (2, 11), (17, 24)):
        m.box(x0, 0, -15.4, x1, 2.8, -14.6); m.box(x0 - 0.1, 2.8, -15.5, x1 + 0.1, 3.0, -14.5, accent=True)
    for x in (-6, -2, 2, 6): m.spawn("a", x, 0, 44.2, 0); m.spawn("b", x, 0, -44, 180)
    m.box(-6.6, 0, 41.4, 6.6, 2.6, 41.8, accent=True)                           # a screen: no sightline down the track to B's spawn
    m.spawn("any", -20, 0, 30, 90); m.spawn("any", 20, 0, -30, -90)
    # crates, cable drums and parts stacked around the yard, away from the track (cover for the defenders'
    # fallbacks and the attackers' pushes)
    m.clutter(-30, -60, 30, 60, 5, [(1.4, 1.4, 1.4), (2.6, 1.2, 1.2), (1.2, 2.6, 1.2), (1.6, 1.6, 2.2)], seed=7)   # (denser in 1.17: cover on 38% of the floor was thin)
    m.raw("setting rail yard")
    m.raw("cartcheck 0.33"); m.raw("cartcheck 0.66")   # checkpoints a third and two thirds along: time, and no rolling back past them
    m.save("railyard")
