"""DEPOT: bomb. Attackers (A) come in at the south gate, defenders (B) from the north yard, and the two sites lie
between them, halfway down each flank: the west site among the racks inside the warehouse, the east site on the
raised dock between the containers. Both sides reach either site within a second of each other (owner decision
B2: whoever takes a site first holds it, so the race to it is even). Three very different lanes: the west runs
through the roofed warehouse, the middle crosses an open yard around the freight truck (climb the crates onto its
roof), the east drops into a sunken loading trench beside the dock."""
from .prefabs import *


def build():
    m = M("Depot", "bomb")
    m.room(26, 34, 9, pits=[(12, 24, 26, 30, "water")], sunken=[(12, -21, 18, 21, 2.5, "metal")], style="yard", skyline="city")
    # the sites, halfway down: west among the warehouse racks, east on the dock between the containers (each the
    # same walk from both gates: the warehouse has a door at each end, the dock a stair at each end)
    m.box(-22, -0.05, -4, -14, 0, 4, accent=True); m.raw("site -22 0 -4 -14 3 4")
    m.box(19, 1.2, -2, 25, 1.25, 6, mat="metal"); m.raw("site 19 1.2 -2 25 4.2 6")
    # the defenders' yard in the north: crate stacks, the wall down the middle and the perch they stack up on
    for (x, z) in ((-20, -24), (-16, -28), (16, -24), (20, -28)): m.box(x - 1, 0, z - 1, x + 1, 1.4, z + 1)
    m.box(-24, 0, -20, -18, 2, -19)
    m.box(-1, 0, -22, 1, 4, -16, accent=True)                               # wall down the middle of the yard
    m.box(-4, 0, -33, 4, 2, -29); m.stairs(-4, -29, 4, -26, 2, "z", -1)       # defender perch
    m.box(-9.5, 0, -24.4, 9.5, 4.05, -24.0, accent=True)                     # the yard's screen wall: out round the sides (no sightline to A's spawn)
    # west: the warehouse (doors north and south, a side door to the yard, racks inside)
    m.building(-24, -8, -9, 10, 5.0, [("n", -16.5, 3.2), ("s", -16.5, 3.2), ("e", 1, 2.6), ("w", -3, 2.6)], windows=True)
    for z in (-3.5, 4):
        m.metal(-22, 0, z - 0.5, -18, 2.6, z + 0.5); m.metal(-15, 0, z - 0.5, -11, 2.6, z + 0.5)
        for zz in (z - 0.55, z + 0.45): m.box(-22, 1.3, zz, -18, 1.4, zz + 0.1, accent=True); m.box(-15, 1.3, zz, -11, 1.4, zz + 0.1, accent=True)
    m.crates(-21, -6.5, 2); m.crates(-12.5, 7.5, 1)
    # middle: the freight truck (trailer + cab, wheels, headlights) with a crate stair onto its roof
    m.metal(-1.6, 0.6, -7, 1.6, 3.4, 3)                                       # trailer
    m.box(-1.7, 3.4, -7.1, 1.7, 3.55, 3.1, accent=True)
    for z in (-6, -4.6, 1.6): m.box(-1.75, 0, z - 0.5, -1.45, 1.0, z + 0.5, accent=True); m.box(1.45, 0, z - 0.5, 1.75, 1.0, z + 0.5, accent=True)
    m.box(-1.5, 0.6, 3.3, 1.5, 2.9, 6.2); m.box(-1.5, 2.9, 3.3, 1.5, 3.1, 5.4, accent=True)   # cab
    m.wallbang(-1.3, 1.8, 6.2, 1.3, 2.7, 6.3)                                  # windscreen
    m.glow(-1.3, 1.0, 6.2, -0.7, 1.4, 6.3); m.glow(0.7, 1.0, 6.2, 1.3, 1.4, 6.3)
    m.box(-1.75, 0, 4.3, -1.45, 1.0, 5.3, accent=True); m.box(1.45, 0, 4.3, 1.75, 1.0, 5.3, accent=True)
    m.box(2.2, 0, -8, 3.4, 1.1, -6.8); m.box(2.2, 0, -6.8, 3.4, 2.4, -5.6)    # crates up onto the trailer
    m.box(-5, 0, 10, -2, 1.4, 12); m.box(3, 0, 12, 6, 1.4, 14)                 # yard cover
    # east: raised dock beside the sunken trench, steps down into it at both ends
    m.box(18, 0, -16, 26, 1.2, 16, accent=True)
    m.stairs(18, 16, 21, 19, 1.2, "z", -1); m.stairs(18, -19, 21, -16, 1.2, "z", 1)   # (a stair up at each end)
    m.container(20, -10, 25, -4); m.container(20, 6, 25, 12); m.container(20, 6, 25, 12, h=5.2)   # (the east site between them)
    m.stairs(12, 18, 18, 21, 0, "z", 1, base=-2.5); m.stairs(12, -21, 18, -18, 0, "z", -1, base=-2.5)
    m.box(14, -2.5, -4, 16, -1.1, 0)                                           # cover down in the trench
    m.metal(11.6, 0, -14, 12, 1.0, 14); m.metal(18, 1.2, -14, 18.4, 2.2, 14)   # trench railings
    for x in (-6, -2, 2, 6): m.spawn("a", x, 0, 28.5, 0)
    for (x, z) in ((-16, -31), (-14, -31), (14, -31), (16, -31), (-15, -33), (15, -33)): m.spawn("b", x, 0, z, 180)   # (the yard's far corners)
    m.spawn("a", 0, 0, 31, 0)
    m.spawn("any", -20, 0, 16, 90); m.spawn("any", 8, 0, 16, -90)
    # a ladder up the warehouse's south wall: its roof is an overwatch perch above the west lane (and exposed)
    m.ladder(-11, 10.4, 0, 5.35, "n"); m.ladder(-11, -8.4, 0, 5.35, "s")   # (one up each end: the roof is a way over, not a dead end)
    # the loading canopy over the east dock and the trench: the east lane fights under a roof, lit from above
    for z in (-15, -5, 5, 15):
        m.metal(11.4, 0, z - 0.25, 11.9, 6.5, z + 0.25); m.metal(25.4, 1.2, z - 0.25, 25.9, 6.5, z + 0.25)
    m.box(11, 6.5, -17, 26, 6.9, 17, accent=True)
    for z in range(-14, 15, 7): m.glow(13, 6.38, z - 0.4, 24, 6.5, z + 0.4); m.light(18.5, 5.6, z, 9, 0.7, fixture=False)
    # the gatehouse at the attackers' entry: a roofed shelter to stack up under before the push
    for (x, z) in ((-8, 26), (8, 26), (-8, 33.5), (8, 33.5)): m.metal(x - 0.3, 0, z - 0.3, x + 0.3, 4.2, z + 0.3)
    m.box(-9, 4.2, 25, 9, 4.6, 34, accent=True); m.glow(-9, 4.05, 25, 9, 4.2, 25.15); m.light(0, 3.6, 29.5, 9, 0.7, fixture=False)
    m.box(-8.4, 0, 24.6, 8.4, 4.05, 25.0, accent=True)                          # its front wall: out round the sides (no sightline to B's spawn)
    # the yard: a corrugated perimeter fence and floodlights on the corner columns
    m.wall_ribs(step=1.0)
    for (x, z) in ((-24, -32), (24, -32), (-24, 32), (24, 32), (-24, 0), (24, 0)):
        m.glow(x - 0.6 if x > 0 else x, 7.4, z - 0.6, x if x > 0 else x + 0.6, 7.9, z + 0.6); m.light(x * 0.92, 7.0, z * 0.95, 14, 0.8, fixture=False)
    # the barge basin by the attackers' gate (fall in and you're out): a moored barge to cross on
    m.metal(15, -1.2, 25, 23, 0.3, 29)
    m.box(15, 0.3, 25, 15.3, 0.9, 29, accent=True); m.box(22.7, 0.3, 25, 23, 0.9, 29, accent=True)
    m.light(19, 2.0, 27, 7, 0.6)
    # the yard crane by the defenders' sites: a ladder up its mast to the cab platform 9.6 m up (the defenders'
    # overwatch over both sites, exposed to everything)
    m.metal(21.5, 0, -32.5, 22.5, 9.3, -31.5)
    for (a0, b0, a1, b1) in minus((19.5, -33.5, 24.5, -30.5), [(21.6, -31.5, 22.4, -30.6)]): m.box(a0, 9.3, b0, a1, 9.6, b1, accent=True)
    m.ladder(22, -31.5, 0, 9.6, "n", width=0.8)
    m.metal(16, 9.6, -32.3, 24.5, 10.0, -31.7); m.light(22, 8.6, -32, 8, 0.7, fixture=False)
    # pallet stacks and drums around the yard (cover on the way to the sites; never on a spawn or a lane's stairs)
    m.clutter(-24, -34, 24, 26, 5, [(1.4, 1.4, 1.3), (2.2, 1.2, 1.1), (1.2, 2.2, 1.1), (1.0, 1.0, 1.6)], seed=17)   # (the defenders' yard too)
    m.raw("setting freight depot")
    m.save("depot")
