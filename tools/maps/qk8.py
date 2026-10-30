"""QK8 CANYON: ranked 1v1 (vanilla) and ranked instagib free for all, box and accent only, point-symmetric. A long
narrow trench, 170 x 50 m and 28 m deep, with a two-storey room at each end (the spawns). Long lines for instagib, but
no floor line over about 60 m: two full-height rock blocks per half stand on the trench floor, staggered side to side
(and the block under the middle bridge). Each wall carries a path: a shelf at 8 m from the end room's upper door, a
flight up to a shelf at 14 m, the middle bridge across at 14 m (the power spot), alcoves along the shelves and two
more at 20 m over the middle (ladders up). Two low bridges cross at 8 m; each comes down the far wall as a stair to the
floor (a solid arch on that side), so neither is a dead end. Climbing the walls by grapple is the vertical game;
overhanging rock shelves near the top are the anchors. The end rooms' roofs are clipped up to the lid: the trench ends
in a wall. Inside each end room a screen stands behind its 12 m door, so the trench doesn't see into the spawn."""
from .prefabs import M
from .m2kit import B, wall, wedge, light, mirror_all

TX, TZ, TH = 64.0, 12.0, 28.0      # the trench: x -64..64, z -12..12, walls 28 m
SW = 4.0                           # the shelves' and flights' width (and the end rooms' upper doors)


def build():
    m = M("QK8 Canyon", "1v1", sun=(0, 1, 0))
    m.raw("ranked vanilla instagib")
    m.also = ["duel", "vs", "tourney"]
    start = len(m.lines)
    # ---- the trench floor (its own twin) and the walls: the north wall here, the south one is its mirror
    B(m, "box", -TX, -1, -TZ, TX, 0, TZ)
    # the wall's inner layer (4 m) has the alcoves cut through it; the outer layer (9 m) is solid
    alcoves = [(-60, -54, 8, 11.5), (-48, -40, 8, 11.5),                            # along the low shelf
               (-10, -4, 14, 17.5), (8, 16, 14, 17.5), (22, 28, 14, 17.5),          # along the high shelf
               (32, 38, 8, 11.5),                                                    # the far end of the other wall's low bridge
               (-4, 4, 20, 23.5), (18, 24, 20, 23.5)]                               # over the middle (ladders up)
    wall(m, "box", -TX, TZ, TX, TZ + 4, 0, TH, alcoves)
    B(m, "box", -TX, 0, TZ + 4, TX, TH, TZ + 13)
    for (a0, a1, y0, y1) in alcoves: B(m, "accent", a0, y0 - 0.05, TZ, a1, y0, TZ + 4)   # (their floors marked)
    # the low shelf (8 m) from the end room's upper door, the flight up, the high shelf (14 m)
    B(m, "accent", -TX, 7.4, TZ - SW, -26, 8, TZ)
    m.stairs(-26, TZ - SW, -14, TZ, 14, "x", 1, base=8, solid=False)
    B(m, "accent", -14, 13.4, TZ - SW, 30, 14, TZ)
    # ladders from the high shelf into the alcoves at 20 m
    m.ladder(1.5, TZ - 0.4, 14, 20, "s"); m.ladder(21, TZ - 0.4, 14, 20, "s")
    # the bridges: the middle one at 14 m (its own twin), the low one at 8 m from this wall's low shelf across to the
    # south wall, where a stair takes it down to the floor (solid: an arch down on that side, under the high shelf)
    B(m, "accent", -3, 13.4, -TZ, 3, 14, TZ)
    B(m, "accent", -38, 7.4, -TZ, -32, 8, TZ - SW)
    m.stairs(-32, -TZ, -20, -TZ + 3, 8, "x", -1, solid=True)
    # the trench floor: the block under the middle bridge, two full-height rock blocks per half staggered side to side
    # (each closes one lane: no floor line over ~60 m), cover blocks a standing player fits behind (2.2 m)
    B(m, "box", -4, 0, -4, 4, 6, 4)
    B(m, "box", -47, 0, 2, -43, 8, TZ); B(m, "box", -16, 0, -TZ, -12, 7, -2)   # (A is flush with the low shelf it stands under)
    wedge(m, "accent", "+x", -49, 0, 2, -47, 3, TZ - SW); wedge(m, "accent", "-x", -12, 0, -TZ, -10, 3, -2)   # (their feet: rock spills)
    for (x0, z0, x1, z1, h) in ((-56, -8, -52, -4, 2.4), (-28, 3, -24, 7, 2.4), (36, 5, 40, 8, 2.2)):
        B(m, "accent", x0, 0, z0, x1, h, z1)
    # overhanging rock shelves near the top (grapple anchors, perches over the trench): a slab over a tapering underside
    for (x0, x1, y) in ((-56, -48, 24), (-30, -24, 22), (6, 14, 25), (40, 48, 23)):
        B(m, "accent", x0, y - 1, TZ - 2.5, x1, y, TZ)
        wedge(m, "accent", "+z", x0 + 0.5, y - 3.5, TZ - 2.5, x1 - 0.5, y - 1, TZ, up=1)
    # ---- the west end room (team a's spawn): two storeys, its upper floor round three sides
    RX0, RX1, RZ = -85.0, -65.0, 16.0
    B(m, "box", RX0, -1, -RZ, RX1 + 1, 0, RZ)
    B(m, "box", RX0 - 1, 0, -RZ - 1, RX1, 16, -RZ); B(m, "box", RX0 - 1, 0, RZ, RX1, 16, RZ + 1)
    B(m, "box", RX0 - 1, 0, -RZ, RX0, 16, RZ)
    wall(m, "box", RX1, -RZ, RX1 + 1, RZ, 0, 16, [(-6, 6, 0, 6), (TZ - SW, TZ, 8, 12)])
    B(m, "box", RX0 - 1, 16, -RZ - 1, RX1 + 1, 16.5, RZ + 1)
    # its roof is clipped up to the lid (nobody stands on it: no line from it into the enemy spawn)
    B(m, "clip", RX0 - 1, 16.5, -RZ - 10, RX1 + 1, 28.8, RZ + 10)
    # upper floor: the west strip, the south strip with the stairs, the north strip out to the door
    B(m, "accent", RX0, 7.6, -RZ, RX0 + 4, 8, RZ)
    B(m, "accent", RX0 + 4, 7.6, TZ - SW, RX1, 8, RZ)
    m.stairs(RX0 + 4, -RZ, RX1 - 2, -RZ + 4, 8, "x", -1, solid=True)
    # a screen inside the 12 m door (the trench can't see into the room), two pillars to the roof
    B(m, "accent", -70, 0, -5, -69, 5, 5)
    for z in (-7.5, 6.5): B(m, "box", -77, 0, z, -76, 16, z + 1)
    # spawns (team a here; b is the mirror); the free-for-all ones in alcoves, facing the trench
    for (x, z) in ((-82, -8), (-82, 6), (-74, -9), (-74, 2)): m.spawn("a", x, 0, z, -90)
    for (x, y, z) in ((-57, 8, 14), (-44, 8, 14), (12, 14, 14), (25, 14, 14)):
        m.spawn("any", x, y, z, 0)
    # lights along the trench, on the shelves and in the room
    for x in range(-56, 1, 14): light(m, x, 6, -6, 16, 0.8)
    for x in (-50, -20, 10): light(m, x, 17, 8, 14, 0.7)
    for (x, y, z) in ((-75, 6, 0), (-80, 6, -10), (-80, 6, 10), (-72, 12, 12), (-83, 12, -6)): light(m, x, y, z, 16, 1.0)
    mirror_all(m, start)
    # ---- bounds: a lid just over the walls (nobody stands on their tops)
    B(m, "clip", -87, 28.8, -26, 87, 29.8, 26)
    m.lines = [("accent" + l[5:]) if l.startswith("metal ") else l for l in m.lines]   # (box and accent only: the ladder rails too)
    m.raw("palette base 0.74 0.58 0.42 accent 0.38 0.2 0.12 metal 0.3 0.28 0.26 glow 1 0.7 0.4 sky 0.62 0.52 0.42")
    m.raw("setting a desert canyon")
    m.save("qk8", dress=False)
