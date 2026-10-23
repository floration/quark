"""VIADUCT: race, two full loops over a chasm, each leg higher than the last. Leg 1 (north): a bridge on
stone arches, stepping stones, a covered tunnel, a climb, a beam (or a grapple swing) across the big gap, a
slalom, stones down. Leg 2 (south) climbs the east edge, leg 3 (north) runs the west edge higher still, and
leg 4 (south) is a skyway down the middle to the finish above the start. Falls send you to your checkpoint."""
from .prefabs import *


def build():
    m = M("Viaduct", "race", sun=(0.5, 1.0, 0.35))
    m.raw("modes race")
    m.room(16, 82, 40, pits=[(-16, -82, 16, 82, "chasm")], style="yard", skyline="mountains")
    def deck(x0, z0, x1, z1, top, mat=None, piers=True):
        m.box(x0, top - 1, z0, x1, top, z1, mat=mat)
        m.glow(x0 - 0.04, top - 0.25, z0, x0, top - 0.1, z1); m.glow(x1, top - 0.25, z0, x1 + 0.04, top - 0.1, z1)
        if piers and (z1 - z0) > 6:
            for z in (z0 + 1, z1 - 2):
                m.box(x0 + 0.4, -30, z, x1 - 0.4, top - 1, z + 1, accent=True)
    def pads(cs, top, size=0.9):
        """stepping pads (size x size) centred on cs = [(x, z, top)]: small landings that need a placed jump. Each
        zig-zag pad gets a post on the opposite side, so a dash-jump can't sail down the line over three of them."""
        for i, (x, z, t) in enumerate(cs):
            deck(x - size / 2, z - size / 2, x + size / 2, z + size / 2, t, piers=False)
            if abs(x - sum(c[0] for c in cs) / len(cs)) > 0.3:   # (a pad on the centre line gets none: it would stand on it)
                xp = -x + 2 * (sum(c[0] for c in cs) / len(cs))
                m.box(xp - 0.18, t - 3, z - 0.18, xp + 0.18, t + (4.0 if i % 2 else 8.5), z + 0.18, mat="metal")
    def weave(xc, z0, z1, top, dz=3.5, off=1.0, w=0.7, gap=0.0, rise=0.0):
        """a beam that snakes down z: 0.7 m segments alternating either side of xc, joined by short cross beams
        (or a jump of `gap` m across). Posts stand in every empty stretch, so no straight dash-jump or grapple
        line skips a bend: you run the whole thing. rise = height gained per segment."""
        n = max(2, int(round(abs(z1 - z0) / dz))); dz = (z1 - z0) / n; h = w / 2
        for k in range(n):
            xa = xc + (off if k % 2 == 0 else -off); za, zb = sorted((z0 + k * dz, z0 + (k + 1) * dz)); t = top + rise * k
            deck(xa - h, za, xa + h, zb, t, mat="metal", piers=False)
            if k + 1 < n:                                   # the cross beam to the next segment
                xb = xc + (-off if k % 2 == 0 else off); zc = z0 + (k + 1) * dz
                if gap <= 0: deck(min(xa, xb) - h, zc - h, max(xa, xb) + h, zc + h, t + rise, mat="metal", piers=False)
            xo = xc - (off if k % 2 == 0 else -off)         # posts where there is no beam, and down the middle between the
            for j, px in enumerate((xo, xc)):               # cross beams: no straight line along the weave stays clear.
                ph = 4.0 if (k + j) % 2 == 0 else 8.5       # (thin, staggered: hopping the rod tops is harder than the beam)
                m.box(px - 0.18, t - 3, (za + zb) / 2 - 0.18, px + 0.18, t + ph, (za + zb) / 2 + 0.18, mat="metal")
    def slalom(walls, X0, X1, top):
        """walls across a lane (X0..X1 wide, its floor at top), each leaving a 0.9 m opening at one side. The openings
        are kept clear: a wall pillar of the dressing beside one left a hull-width slot nobody could get through."""
        for (z, x0, x1) in walls:
            m.box(x0, top, z, x1, top + 1.6, z + 0.6, accent=True)
            o0, o1 = (x1, X1) if x0 <= X0 else (X0, x0)
            m.keep_clear_box(o0, top, z - 1.0, o1, top + 2.2, z + 1.6, "a slalom opening")
    # ---- leg 1, ground level, north to south
    deck(-6, 66, 6, 78, 0)                                                     # start
    deck(-2, 50, 2, 66, 0)                                                     # bridge on arches
    for z in (54, 60): m.box(-2.2, -6, z, 2.2, -1, z + 2, accent=True); m.box(-2.2, -30, z + 0.4, 2.2, -6, z + 1.6)
    pads([(0.8, 47.0, 0.4), (-0.8, 43.9, 0.8), (0.8, 40.8, 1.2), (-0.8, 37.7, 1.5), (0.0, 35.2, 1.5)], 0)   # pads up, zig-zag
    m.raw("checkpoint -1.5 1.5 34.4 1.5 4.5 36")
    deck(-12, 28, 2, 34, 1.5)                                                  # turn west
    for x in (-9, -3): m.spawn("any", x, 1.5, 30.5, 0)                         # free for all: two on the turn (the start has room for six)
    # the covered lane: the roof stops any hooking or jumping over it, and its floor has fallen in in four
    # places (2 m gaps you jump in the dark)
    for (z0, z1) in ((24, 28), (18.5, 22), (13, 16.5), (8, 11), (4, 6)): deck(-12, z0, -8, z1, 1.5, piers=False)
    for x in (-12, -8.4): m.box(x, 1.5, 8, x + 0.4, 5.5, 22)
    m.box(-12.4, 5.5, 7.6, -7.4, 35, 22.4)   # the lane tunnels through a rock spur (no roof to run along)
    m.glow(-12.6, 5.3, 7.6, -7.4, 5.5, 7.8); m.glow(-12.6, 5.3, 22.2, -7.4, 5.5, 22.4)
    m.light(-10, 4.6, 11, 7, 0.8); m.light(-10, 4.6, 19, 7, 0.8)
    m.raw("checkpoint -12 1.5 4 -8 5 6")
    m.stairs(-12, -4, -8, 4, 4.5, "z", -1, base=1.5, solid=False)            # the climb
    deck(-12, -10, 4, -4, 4.5)
    weave(0.7, -10, -16, 4.5, dz=2.0, off=0.9)                                # the beam across the gap, weaving
    m.metal(-4.2, 4.5, -13.2, -3.8, 12, -12.8); m.box(-5, 12.5, -14, 5, 13, -12, accent=True)
    deck(-2, -22, 12, -16, 4.5)
    m.raw("checkpoint 4 4.5 -22 10 7.5 -16")
    deck(8, -40, 12, -22, 3.0)                                                 # slalom lane: 0.9 m openings
    slalom(((-26, 8, 11.1), (-30, 8.9, 12), (-34, 8, 11.1), (-38, 8.9, 12)), 8, 12, 3.0)
    pads([(10.8, -42.5, 2.6), (9.2, -45.4, 2.2), (10.8, -48.3, 1.8)], 0)     # pads down
    deck(2, -60, 12, -51, 1.0)
    m.raw("checkpoint 2 1 -60 12 4 -52")
    weave(4, -60, -70, 1.0)                                                   # the run home, weaving
    deck(-8, -79, 8, -70, 1.0)
    m.box(-8, 1, -79.6, 8, 7, -79, accent=True); m.glow(-6, 5, -79.05, 6, 6, -78.95)   # lap gate
    m.metal(-8, 1, -79, -7.4, 7, -78.4); m.metal(7.4, 1, -79, 8, 7, -78.4)
    m.raw("checkpoint -6 1 -79 6 5 -72")
    # ---- leg 2, south along the east edge, 8 to 14 m
    deck(8, -79, 15.5, -73, 1.0)
    m.stairs(12.5, -73, 15.5, -57, 8, "z", 1, base=1.0, solid=False)
    deck(12.5, -57, 15.5, -20, 8)
    slalom(((-50, 12.5, 14.6), (-44, 13.4, 15.5), (-38, 12.5, 14.6), (-32, 13.4, 15.5)), 12.5, 15.5, 8)
    m.raw("checkpoint 12.5 8 -28 15.5 11 -20")
    m.stairs(12.5, -20, 15.5, -12, 10, "z", 1, base=8, solid=False)
    # the aqueduct gallery: roofed (nothing to hook, nothing to jump over), its floor broken in three places
    for (z0, z1) in ((-12, -7), (-5, 0), (2, 6), (8, 10)): deck(12.5, z0, 15.5, z1, 10, piers=False)
    m.box(12.5, 13.6, -12.4, 16, 14.1, 10.4, accent=True)
    for z in range(-12, 11, 4): m.box(12.5, 10, z - 0.25, 12.9, 13.6, z + 0.25)
    m.light(14, 12.8, -6, 7, 0.8); m.light(14, 12.8, 4, 7, 0.8)
    pads([(13.2, 12.4, 10.0), (14.8, 15.4, 10.4), (13.2, 18.4, 10.8), (14.8, 21.4, 11.2), (13.2, 24.4, 10.8), (14.8, 27.4, 10.4)], 0)   # the collapse
    deck(12.5, 28, 15.5, 30, 10, piers=False)
    weave(14, 30, 44, 10)                                                     # east beam, weaving
    deck(12.5, 44, 15.5, 70, 10)
    m.raw("checkpoint 12.5 10 44 15.5 13 62")
    m.box(12.5, 10, 70, 15.5, 13, 72, accent=True)                            # the corner tower and its ladder
    m.ladder(14, 70, 10, 14, "s", width=0.8)
    deck(12.5, 70, 15.5, 81, 14)
    deck(-15.5, 77, 12.5, 81, 14)
    m.raw("checkpoint -6 14 77 6 17 81")
    # ---- leg 3, north along the west edge, 18 to 22 m
    m.stairs(-15.5, 65, -12.5, 77, 18, "z", -1, base=14, solid=False)
    weave(-14, 65, 30, 18, rise=0.0)                                          # the west ridge: a long weave
    m.stairs(-15.5, 22, -12.5, 30, 20, "z", -1, base=18, solid=False)
    weave(-14.4, 22, -10, 20, off=0.9, dz=4.0)                                # the cliff path: weaving round rock posts under an overhang
    m.box(-16, 23.2, -10.4, -12.4, 35, 22.4, accent=True)                    # (solid rock above: no running along the top)
    for z in range(-8, 22, 6): m.light(-15, 22.6, z, 6, 0.7, fixture=False); m.glow(-15.9, 22.9, z - 0.3, -15.6, 23.2, z + 0.3)
    deck(-15.5, -30, -12.5, -10, 20)
    m.raw("checkpoint -15.5 20 -30 -12.5 23 -10")
    weave(-14, -30, -44, 20)                                                  # west beam, weaving
    deck(-15.5, -60, -12.5, -44, 20)
    m.stairs(-15.5, -70, -12.5, -60, 22, "z", -1, base=20, solid=False)
    deck(-15.5, -81, -12.5, -70, 22)
    deck(-12.5, -81, 1.5, -77, 22)
    m.raw("checkpoint -12.5 22 -81 1.5 25 -77")
    # ---- leg 4, the skyway south down the middle, 22 to 31 m
    m.stairs(-1.5, -77, 1.5, -65, 28, "z", 1, base=22, solid=False)
    deck(-1.5, -65, 1.5, -20, 28, piers=False)
    slalom(((-55, -1.5, 0.6), (-47, -0.6, 1.5), (-39, -1.5, 0.6), (-31, -0.6, 1.5)), -1.5, 1.5, 28)
    m.raw("checkpoint -1.5 28 -28 1.5 31 -20")
    m.stairs(-1.5, -20, 1.5, -12, 30, "z", 1, base=28, solid=False)
    # stepping pads in the sky: 0.9 m landings, 2-2.4 m gaps, zig-zag, rising and falling up to a metre
    sky, z, k = [], -10.8, 0
    while z < 36.5:
        sky.append(((-1.0, 1.0)[k % 2], z, 30 + (0.0, 0.5, 1.0, 0.6, 0.2, 0.8)[k % 6]))
        z += 0.9 + (1.6, 2.0, 1.8)[k % 3]; k += 1
    pads(sky, 0)
    deck(-1.5, 38, 1.5, 40, 30, piers=False)
    weave(0, 40, 52, 30, w=0.62)                                              # last beam, weaving at 0.62 m
    deck(-4, 52, 4, 64, 30, piers=False)
    m.box(-4, 30, 63.4, 4, 36, 64, accent=True); m.glow(-3, 34, 63.35, 3, 35, 63.45)   # finish arch
    m.metal(-4, 30, 62.8, -3.4, 36, 63.4); m.metal(3.4, 30, 62.8, 4, 36, 63.4)
    m.light(0, 33, 58, 9, 0.8)
    m.raw("checkpoint -3 30 56 3 34 63")
    # lanterns on posts along the wide decks (you can see the line of the course at a glance), and rock strata on
    # the gorge walls
    for (x, z, y) in ((-6, 72, 0), (6, 72, 0), (-12, 31, 1.5), (-2, -7, 4.5), (10, -19, 4.5), (12, -56, 1.0), (-8, -74, 1.0),
                      (15.2, -24, 8), (15.2, 50, 10), (-15.2, -20, 20), (-15.2, -52, 20), (0, 58, 30)):
        m.metal(x - 0.08, y, z - 0.08, x + 0.08, y + 2.6, z + 0.08); m.light(x, y + 2.4, z, 7, 0.7)
    for y in range(-10, 38, 2):
        m.box(-16, y, -82, -15.9, y + 0.25, 82, mat="trim"); m.box(15.9, y + 1, -82, 16, y + 1.25, 82, mat="trim")
    # the viaduct's spandrel walls: four stone walls right across the gorge that carry the whole structure, with
    # an arch cut wherever a leg passes through. They split the gorge into bays (you see the next stretch, not the
    # whole course) and nothing goes over them: each leg has to thread its own arch.
    # Buttress fins stand out 5 m from both faces beside every arch, the whole height of the wall (except where
    # they'd cross another leg's arch): each leg threads its arch down a walled channel, so hooking the open middle
    # of a wall and dashing along its face to an arch goes nowhere - you reach an arch only by running your own leg.
    for (z, holes) in ((50, [(-2.5, 0, 2.5, 4), (12.5, 10, 16, 14), (-16, 18, -12.4, 22), (-2, 30, 2, 34)]),
                       (20, [(-12.6, 1.5, -7.4, 6.2), (12.5, 10, 16, 14.5), (-16, 20, -12.4, 23.2), (-2.4, 29.5, 2.4, 34)]),
                       (-2.1, [(-12.6, 1.5, -7.4, 8.5), (12.5, 10, 16, 13.6), (-16, 20, -12.4, 23.2), (-2.4, 29.5, 2.4, 34)]),
                       (-29.5, [(7.5, 3, 12.5, 7), (12.5, 8, 16, 12), (-16, 20, -12.4, 24), (-2, 28, 2, 32)]),
                       (-62.5, [(1.5, 1, 6.5, 5), (12.5, 4, 16, 11), (-16, 19, -12.4, 26), (-2, 27, 2, 32)])):
        for (a0, y0, a1, y1) in minus((-16, -12, 16, 35), holes):
            m.box(a0, y0, z, a1, y1, z + 1, accent=(y1 - y0) < 1.5)
        for (h0, hy0, h1, hy1) in holes:
            for (f0, f1) in ((h0 - 0.6, h0), (h1, h1 + 0.6)):
                if f0 < -15.9 or f1 > 15.9: continue                  # (the gorge wall is that side already)
                cuts = [(hy0b - 0.5, hy1b + 0.5) for (g0, hy0b, g1, hy1b) in holes if g0 < f1 and g1 > f0]
                ys, y = [], -12.0
                for (c0, c1) in sorted(cuts):
                    if c0 > y: ys.append((y, c0))
                    y = max(y, c1)
                if y < 35: ys.append((y, 35))
                for (y0, y1) in ys: m.box(f0, y0, z - 5, f1, y1, z + 6, mat="metal")
    # nothing goes over the walls: an invisible ceiling level with their tops, right across the gorge
    m.box(-16, 35, -82, 16, 40, 82, mat="clip")
    # lava falls down the middle of every bay between two walls: a curtain of lava (touch it and you're back at your
    # checkpoint) everywhere but the four legs' own ways through, so nobody hooks or dashes round a leg across the
    # open middle of the gorge. Each leg's way through a bay runs from its arch in one wall to its arch in the next.
    WALLS = [(50, [(-2.5, 0, 2.5, 4), (12.5, 10, 16, 14), (-16, 18, -12.4, 22), (-2, 30, 2, 34)]),
             (20, [(-12.6, 1.5, -7.4, 6.2), (12.5, 10, 16, 14.5), (-16, 20, -12.4, 23.2), (-2.4, 29.5, 2.4, 34)]),
             (-2.1, [(-12.6, 1.5, -7.4, 8.5), (12.5, 10, 16, 13.6), (-16, 20, -12.4, 23.2), (-2.4, 29.5, 2.4, 34)]),
             (-29.5, [(7.5, 3, 12.5, 7), (12.5, 8, 16, 12), (-16, 20, -12.4, 24), (-2, 28, 2, 32)]),
             (-62.5, [(1.5, 1, 6.5, 5), (12.5, 4, 16, 11), (-16, 19, -12.4, 26), (-2, 27, 2, 32)])]
    for (za, ha), (zb, hb) in zip(WALLS, WALLS[1:]):
        zm = (za + zb + 1) / 2
        ways = [(min(p[0], q[0]) - 2.0, min(p[1], q[1]) - 2.5, max(p[2], q[2]) + 2.0, max(p[3], q[3]) + 3.0) for p, q in zip(ha, hb)]
        for (x0, y0, x1, y1) in minus((-16, -12, 16, 35), ways):
            if x1 - x0 < 0.5 or y1 - y0 < 0.5: continue
            m.box(x0, y0, zm - 0.2, x1, y1, zm + 0.2, mat="lava")
            m.glow(x0, y0, zm - 0.02, x1, y0 + 0.1, zm + 0.02)
        m.light(0, 18, zm, 22, 0.8, fixture=False)
    # the river that cut the gorge, far below (fall and you're back at your checkpoint long before you reach it)
    m.box(-16, -20.3, -82, 16, -20, 82, mat="water")
    # pavilions over the start grid and the lap gate: shade for the crowd, posts to weave past, the roof reads as the
    # place the lap begins and ends
    for (z0, z1) in ((66, 78), (-79, -70)):
        y = 0 if z0 > 0 else 1.0
        m.box(-6.4, y + 4.6, z0 - 0.4, 6.4, y + 5.0, z1 + 0.4, accent=True)
        for x in (-5.6, -2, 2, 5.6):
            for z in (z0 + 0.6, z1 - 0.6): m.metal(x - 0.2, y, z - 0.2, x + 0.2, y + 4.6, z + 0.2)
        m.light(-3, y + 4.0, (z0 + z1) / 2, 7, 0.7, fixture=False); m.light(3, y + 4.0, (z0 + z1) / 2, 7, 0.7, fixture=False)
    # crowd barriers along the open sides of the start grid and the turn west (cover for the free-for-all fights there,
    # and the line the racers are held behind)
    for (x0, x1, z0, z1, y) in ((-6, -5.75, 66, 78, 0), (5.75, 6, 66, 78, 0), (-12, -2.5, 33.75, 34, 1.5)):   # (and along the turn deck, clear of the pads)
        m.box(x0, y, z0, x1, y + 1.25, z1, accent=True)
        for z in range(int(z0) + 1, int(z1), 3): m.metal(x0 - 0.05, y, z - 0.1, x1 + 0.05, y + 1.3, z + 0.1)
    for z in (74, 76.5):                                                       # the start grid: eight racers (a full server)
        for x in (-4.5, -1.5, 1.5, 4.5): m.spawn("race", x, 0, z, 0)
    m.raw("setting mountain gorge")
    m.save("viaduct")
