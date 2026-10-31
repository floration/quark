"""QK3 PINWHEEL (1.19.6, rebuilt): ranked VANILLA and INSTAGIB 1v1, box and accent only. A sunken bowl in the middle
of a 52 x 52 m yard, ringed by four decks at 7 m, one on each side of the bowl and each pushed along its side like a
pinwheel's blade, so their ends leave 5 m gaps at the bowl's corners (a hop at speed). Each blade's ramp climbs to
its deck from the yard's corner behind it, and a straight stair climbs the pier that cuts the outer lane, so every
deck has two walkable ways up and the whole map is loops: up a ramp, round the deck ring, off into the bowl, up its
banks and out under a deck. A pillar in the bowl (its top at 12 m: the power spot) blocks the lines across the
bowl; two bridges reach it from the south and north decks and a ladder each climbs it.
Levels: the bowl -2, the yard 0, the decks 7, the pillar top 12. A 90-degree pinwheel (point symmetric overall), the
sun overhead. The idea is momentum: the ramps feed speed into the deck ring and the banks feed it back out.
(Replaces Twin Halls, which q found not fun: a void chasm, long hall-to-hall lines, a trap catwalk.)"""
from .prefabs import *
from .mkit import wedge
import math

R = 26.0             # the yard: x, z -28..28 inside the outer walls (1 m thick)
WALL = 15.0          # the outer walls' tops (clip from there to the lid)
LID = 22.0
DECK = 7.0           # the decks
SLAB = 0.6
BOWL = -2.0          # the bowl's floor (|x|, |z| <= 6.5), banks out to 10
B0, B1 = 6.5, 10.0
PIL = 2.5            # the pillar: +-2.5, from the bowl's floor to 12
PTOP = 12.0

# blade 0 (the south one), in its own frame; blades 1..3 are it turned 90, 180 and 270 degrees ((x, z) -> (-z, x))
D = (-10, -19, 5, -10)          # its deck (x0, z0, x1, z1) at 7
RAMP = (-24, -19, -10, -14)     # its ramp, rising toward +x from the yard (0) to the deck
PIER = (-4, -26, 3, -19)        # the pier across the outer lane, solid to the deck's height
STAIR = (3, -26, 12, -21.5)       # the stair up the pier's east face (low end at x 12)

_WD = {0: 2, 2: 1, 1: 3, 3: 0}   # a wedge's dir turned 90 degrees
_LF = {"e": "s", "s": "w", "w": "n", "n": "e"}


class Turn:
    """M's calls, turned k * 90 degrees about the middle"""
    def __init__(s, m, k): s.m, s.k = m, k

    def p(s, x, z):
        for _ in range(s.k): x, z = -z, x
        return x, z

    def rect(s, x0, z0, x1, z1):
        a, b = s.p(x0, z0); c, d = s.p(x1, z1)
        return min(a, c), min(b, d), max(a, c), max(b, d)

    def box(s, x0, y0, z0, x1, y1, z1, **kw):
        a, b, c, d = s.rect(x0, z0, x1, z1)
        s.m.box(a, y0, b, c, y1, d, **kw)

    def wedge(s, mat, d, box):
        x0, y0, z0, x1, y1, z1 = box
        for _ in range(s.k): d = _WD[d]
        a, b, c, e = s.rect(x0, z0, x1, z1)
        wedge(s.m, mat, d, (a, y0, b, c, y1, e))

    def stairs(s, x0, z0, x1, z1, top, axis, rising, base=0.0):
        for _ in range(s.k): axis, rising = ("z", rising) if axis == "x" else ("x", -rising)
        a, b, c, d = s.rect(x0, z0, x1, z1)
        s.m.stairs(a, b, c, d, top, axis, rising, base=base)

    def ladder(s, x, z, y0, y1, face):
        for _ in range(s.k): face = _LF[face]
        a, b = s.p(x, z)
        s.m.ladder(a, b, y0, y1, face, rail="accent")

    def light(s, x, y, z, r, i):
        a, b = s.p(x, z)
        s.m.light(a, y, b, r, i, fixture=False)

    def spawn(s, team, x, y, z, mirror_team=None):
        a, b = s.p(x, z)
        s.m.spawn(team, a, y, b, round(math.degrees(math.atan2(a, b))), mirror_team)   # (toward the middle)


def build():
    m = M("QK3 Pinwheel", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla vampire instagib")   # (q, 2026-10-31: every qk map on all three ladders)
    m.raw("palette base 0.62 0.62 0.6 accent 0.12 0.62 0.56 metal 0.3 0.3 0.32 glow 0.5 1 0.9 sky 0.6 0.7 0.82")
    layout(m)
    decorate(m)
    m.raw("setting four decks wound round a sunken bowl like a pinwheel, box and accent")
    m.save("qk3", dress=False)


def layout(m):
    # ---- the yard's floor round the bowl, the bowl, its banks (two wedges overlap in each corner: their union is
    # the inside corner)
    for (x0, z0, x1, z1) in minus((-R, -R, R, R), [(-B1, -B1, B1, B1)]):
        m.box(x0, -1, z0, x1, 0, z1)
    m.box(-B1, BOWL - 1, -B1, B1, BOWL, B1)
    for k in range(4):
        Turn(m, k).wedge("box", 3, (-B1, BOWL, -B1, B1, 0, -B0))
    # ---- the outer walls, clip over them to the lid, the lid
    m.box(-R - 1, -1, -R - 1, R + 1, WALL, -R, mirror=True)
    m.box(-R - 1, -1, -R, -R, WALL, R, mirror=True)
    m.box(-R - 1, WALL, -R - 1, R + 1, LID, -R, mat="clip", mirror=True)
    m.box(-R - 1, WALL, -R, -R, LID, R, mat="clip", mirror=True)
    m.box(-R - 1, LID, -R - 1, R + 1, LID + 1, R + 1, mat="clip")
    # ---- the four blades: deck, ramp, pier, stair
    for k in range(4):
        t = Turn(m, k)
        x0, z0, x1, z1 = D
        t.box(x0, DECK - SLAB, z0, x1, DECK, z1)
        x0, z0, x1, z1 = RAMP
        t.wedge("box", 0, (x0, 0, z0, x1, DECK, z1))
        x0, z0, x1, z1 = PIER
        t.box(x0, 0, z0, x1, DECK, z1)
        x0, z0, x1, z1 = STAIR
        t.stairs(x0, z0, x1, z1, DECK, "x", -1)
    # ---- the pillar in the bowl, the bridges from the south and north decks, a ladder up the pillar from each
    for (y0, y1, acc) in ((BOWL, 2.6, False), (2.6, 3.0, True), (3.0, PTOP - 0.4, False), (PTOP - 0.4, PTOP, True)):
        m.box(-PIL, y0, -PIL, PIL, y1, PIL, accent=acc)                 # (its bands flush: nothing for a climber to snag)
    m.box(-2, DECK - SLAB, -B1, 2, DECK, -PIL, mirror=True)
    # (off the middle, so climbing over the top doesn't carry you into the other ladder)
    m.ladder(-1.2, -PIL - 0.12, DECK, PTOP, "s", rail="accent")
    m.ladder(1.2, PIL + 0.12, DECK, PTOP, "n", rail="accent")
    # ---- spawns: team a in blades 0 and 1 (south and east), b their mirror (blades 2 and 3); eight any
    for k in (0, 1):
        t = Turn(m, k)
        t.spawn("a", -22, 0, -22, "b")
        t.spawn("a", -14, 0, -22.5, "b")
    for k in range(4):
        t = Turn(m, k)
        t.spawn("any", -6, 0, -15)
        t.spawn("any", 0, DECK, -23)
    # ---- light: under the decks (the yard and the bowl have the sun)
    for k in range(4):
        t = Turn(m, k)
        for x in (-7, -1, 4):
            t.light(x, DECK - 1.5, -14.5, 9, 0.9)


def decorate(m):
    """accent only, flush or 5 cm proud, all on faces off the lanes: a fascia along every deck and pier edge (each
    blade reads as one piece), the bridges' sides, a rim round the bowl; one wide band on the south and north walls,
    two thin ones on the east and west walls (which side you're on)"""
    for k in range(4):
        t = Turn(m, k)
        x0, z0, x1, z1 = D
        y0, y1 = DECK - SLAB, DECK
        t.box(x0, y0, z1, x1, y1, z1 + 0.05, accent=True)                  # the bowl side
        t.box(x1, y0, z0, x1 + 0.05, y1, z1, accent=True)                  # the corner gap end
        t.box(x0 - 0.05, y0, RAMP[3], x0, y1, z1, accent=True)            # beside the ramp's top
        px0, pz0, px1, pz1 = PIER
        t.box(px0 - 0.05, y0, pz0, px0, y1, pz1, accent=True)             # the pier's west face
        t.box(-B1, 0, -B1 - 0.3, B1, 0.02, -B1, accent=True)              # the bowl's rim
        if k % 2 == 0:
            t.box(-R, 4.0, -R, R, 4.6, -R + 0.05, accent=True)
        else:
            for (a, b) in ((3.9, 4.1), (4.4, 4.6)):
                t.box(-R, a, -R, R, b, -R + 0.05, accent=True)
    for (a, b) in ((-2.05, -2), (2, 2.05)):                                 # the bridges' sides
        m.box(a, DECK - SLAB, -B1, b, DECK, -PIL, accent=True, mirror=True)
