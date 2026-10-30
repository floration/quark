"""QK3 TWIN HALLS (1.19.6): ranked VANILLA and INSTAGIB, box and accent only. Two tall halls (38 x 38 m inside, 27 m
to the roof) face each other across a 20 m chasm (void). Above the ledge ring (14 m) each hall's chasm wall is three
big openings between piers, so the halls look at each other across the chasm; below it, the bastion door (6 m) and
two ground doors. The bridges are staggered: one at 6 m in the middle (bastion to bastion), a pair at 14 m off to
the sides (ledge to ledge), one at 22 m in the middle without rails (balcony to balcony), so they see each other
diagonally. In each hall: the bastion, a ledge ring at 14 (a long stair up from the floor), the balcony at 22 over
the ledge (a ladder), a corner alcove at 22 on the back wall (a ladder up from the ledge), two tall pillars
and a dais on the floor. Under the 14 m bridges two catwalks at -8 join the halls' ground doors by ladders: the
flank. Point symmetric, the sun overhead.
(1.19.6 fix round 2, the review's rework: open chasm walls, staggered bridges, alcove off the axis, floor cover,
lights over the tops, the ledge stair's low end solid, ladders all box and accent.)"""
from .prefabs import *
from .mkit import wedge

HX0, HX1 = 10, 50      # a hall's span in x (the east one; the west one is its mirror), walls 1 m
HZ = 20                # halls and chasm: z -20..20
TOP = 27.0             # the halls' roofs
LID = 32.0
LEDGE = 14.0
HIGH = 22.0            # the balcony, the alcove and the top bridge
LINTEL = 26.4          # the openings above the ledge end here (4.4 m over the balcony: a duel giant fits)
DOOR_H = 4.5           # (a duel giant is 3.6 m)
CW = -8.0              # the catwalks
B14 = (-12.5, -7.5)    # the 14 m bridge on the west hall's south half (its mirror: z 7.5..12.5)
# the chasm wall above the ledge: piers (z ranges, west hall) between three openings
PIERS = ((-19, -15), (-5.5, -3.5), (3.5, 5.5), (15, 19))


def build():
    m = M("QK3 Twin Halls", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla instagib")
    m.raw("void -16")
    m.raw("palette base 0.56 0.6 0.66 accent 0.85 0.2 0.25 metal 0.3 0.3 0.34 glow 1 0.5 0.5 sky 0.52 0.6 0.74")
    layout(m)
    decorate(m)
    m.raw("setting twin halls across a chasm, box and accent")
    m.save("qk3", dress=False)


def layout(m):
    # everything is built for the WEST hall (x -50..-10) and point-mirrored to the east one
    X0, X1 = -HX1, -HX0          # -50, -10
    I0, I1 = X0 + 1, X1 - 1      # inside: -49..-11
    iz = HZ - 1
    # ---- the hall's floor, walls and roof
    m.box(X0, -1, -HZ, X1, 0, HZ, mirror=True)
    m.box(X0, -1, -HZ, I0, TOP, HZ, mirror=True)                                     # back wall
    m.box(I0, -1, -HZ, X1, TOP, -iz, mirror=True); m.box(I0, -1, iz, X1, TOP, HZ, mirror=True)
    # the chasm wall (x -11..-10). Below the ledge: solid but for the ground doors (z +-12..16) and the bastion's
    # door (z -2.5..2.5, y 6..10.5)
    for (z0, z1) in ((-iz, -16), (-12, -2.5), (2.5, 12), (16, iz)):
        m.box(I1, -1, z0, X1, LEDGE, z1, mirror=True)
    for (z0, z1) in ((-16, -12), (12, 16)):
        m.box(I1, DOOR_H, z0, X1, LEDGE, z1, mirror=True)
    m.box(I1, -1, -2.5, X1, 6.0, 2.5, mirror=True)
    m.box(I1, 6.0 + DOOR_H, -2.5, X1, LEDGE, 2.5, mirror=True)
    # above it: piers between three openings (y 14..26.4), a lintel to the roof; the middle one starts at 18
    # (the balcony's window: the ledges don't see each other through it)
    for (z0, z1) in PIERS:
        m.box(I1, LEDGE, z0, X1, LINTEL, z1, mirror=True)
    m.box(I1, LEDGE, PIERS[1][1], X1, 18.0, PIERS[2][0], mirror=True)
    m.box(I1, LINTEL, -iz, X1, TOP, iz, mirror=True)
    m.box(X0, TOP, -HZ, X1, TOP + 0.6, HZ, mirror=True)                              # the roof
    m.box(X0, TOP + 0.6, -HZ, X1, LID, HZ, mat="clip", mirror=True)                   # (the roof is no floor)
    # ---- the chasm: end walls at z +-20 from the void up (the halls' sides continue across), a lid
    m.box(-HX0, -16, HZ - 1, HX0, TOP, HZ, mirror=True)
    m.box(X0, -16, -HZ, X1, -1, HZ, mirror=True)                                    # the halls' foundations (into the void)
    m.box(-HX0, TOP, -HZ, HX0, LID, -HZ + 1, mat="clip", mirror=True)
    m.box(-HX1, LID, -HZ, HX1, LID + 1, HZ, mat="clip")
    # ---- the bridges: 6 m in the middle (rails), a pair at 14 m to the sides (rails), 22 m in the middle (none)
    def bridge(y, z0, z1, rails, mirror):
        m.box(-HX0, y - 0.6, z0, HX0, y, z1, accent=not rails, mirror=mirror)
        if rails:
            m.box(-HX0, y, z0, HX0, y + 1.0, z0 + 0.3, accent=True, mirror=mirror)
            m.box(-HX0, y, z1 - 0.3, HX0, y + 1.0, z1, accent=True, mirror=mirror)
    bridge(6.0, -2.5, 2.5, True, False)
    bridge(LEDGE, B14[0], B14[1], True, True)
    bridge(HIGH, -2.0, 2.0, False, False)
    # ---- the catwalks at -8: two across the chasm (z +-14, beside the 14 m bridges' outer edges, 22 m under
    # them), ladders up the foundations to the ground doors
    m.box(-HX0, CW - 0.5, 12.5, HX0, CW, 15.5, mirror=True)
    m.box(-HX0, CW, 12.5, HX0, CW + 0.9, 12.7, accent=True, mirror=True)
    m.box(-HX0, CW, 15.3, HX0, CW + 0.9, 15.5, accent=True, mirror=True)
    for (x, z, f) in ((-HX0 + 0.12, 14, "w"), (HX0 - 0.12, -14, "e"), (HX0 - 0.12, 14, "e"), (-HX0 + 0.12, -14, "w")):
        m.ladder(x, z, CW, 0, f, rail="accent")
    # ---- inside the hall: the bastion the 6 m bridge lands on (steps down its back)
    m.box(-17, 0, -4, I1, 6.0, 4, mirror=True)
    m.box(-17.05, 5.7, -4.05, I1, 6.03, 4.05, accent=True, mirror=True)               # (3 cm proud of the top)
    m.stairs(-29, -2, -17, 2, 6.0, "x", 1, mirror=True)
    # the ledge ring at 14 (4 m wide), accent edges round its hole
    for (x0, z0, x1, z1) in minus((I0, -iz, I1, iz), [(I0 + 4, -iz + 4, I1 - 4, iz - 4)]):
        m.box(x0, LEDGE - 0.5, z0, x1, LEDGE, z1, mirror=True)
    for (x0, z0, x1, z1) in ((I0 + 4, -iz + 4, I1 - 4, -iz + 4.3), (I0 + 4, iz - 4.3, I1 - 4, iz - 4)):
        m.box(x0, LEDGE - 0.5, z0, x1, LEDGE + 0.04, z1, accent=True, mirror=True)
    m.box(I0 + 4, LEDGE - 0.5, -iz + 4, I0 + 4.3, LEDGE + 0.04, iz - 4, accent=True, mirror=True)
    m.box(I1 - 4.3, LEDGE - 0.5, -iz + 4, I1 - 4, LEDGE + 0.04, iz - 4, accent=True, mirror=True)
    # a 1 m rail along the openings (crouch cover over the chasm), open where the 14 m bridges land
    lands = [B14, (-B14[1], -B14[0])]
    for (x0, z0, x1, z1) in minus((I1 - 0.3, -15, I1, 15), [(I1 - 1, a, I1 + 1, b) for (a, b) in lands + list(PIERS) + [(PIERS[1][1], PIERS[2][0])]]):
        m.box(x0, LEDGE, z0, x1, LEDGE + 1.0, z1, accent=True, mirror=True)
    # the stair up to the ledge (north side, rising west): its low end solid (no head-bump pocket under the
    # slabs), the rest a flight in the air
    m.stairs(-24.5, iz - 7, -19, iz - 4, 3.5, "x", -1, mirror=True)
    m.stairs(-41, iz - 7, -24.5, iz - 4, LEDGE, "x", -1, mirror=True, base=3.5, solid=False)
    # the balcony at 22 over the ledge's chasm strip, its post with the ladder up from the ledge
    m.box(-17, HIGH - 0.5, -4, I1, HIGH, 4, mirror=True)
    m.box(-15.5, LEDGE, 4, -13.5, HIGH - 0.5, 5, mirror=True)
    m.ladder(-14.5, 5.12, LEDGE, HIGH, "n", rail="accent"); m.ladder(14.5, -5.12, LEDGE, HIGH, "s", rail="accent")
    # the alcove at 22 in the back wall's south corner (off the hall's axis: it no longer looks down the bridges
    # into the other alcove), over the ledge, a ladder up its post from the ledge
    m.box(I0, HIGH - 0.5, -iz, -43, HIGH, -11, mirror=True)
    m.box(-43.6, HIGH - 0.5, -iz, -43, TOP, -12.5, mirror=True)                          # (its chasm side closed: it looks north over its own hall)
    m.box(-48, LEDGE, -11, -46, HIGH - 0.5, -10, mirror=True)                          # its post
    m.ladder(-47, -9.88, LEDGE, HIGH, "n", rail="accent"); m.ladder(47, 9.88, LEDGE, HIGH, "s", rail="accent")
    # ---- the floor: two tall pillars (they cut the ledge-to-floor lines), a 1.5 m dais to hold (ramps on two
    # sides, a lip on its back), the 3 m block
    m.box(-38, 0, -7, -35, TOP, -4, mirror=True)
    m.box(-24, 0, 5, -21, TOP, 8, mirror=True)
    m.box(-40, 0, 2, -32, 1.5, 9, mirror=True)
    wedge(m, "box", 2, (-40, 0, -1, -32, 1.5, 2), mirror=True)                         # (up from the south)
    wedge(m, "box", 1, (-32, 0, 2, -28, 1.5, 9), mirror=True)                          # (up from the east)
    m.box(-40, 1.5, 8.6, -32, 2.7, 9, accent=True, mirror=True)
    m.box(-30, 0, -14, -26, 3.0, -10, mirror=True)
    # ---- spawns: four a hall (two on the floor, two on the side ledge), facing the chasm
    for (x, y, z, yaw) in ((-30, 0, -5, -90), (-44, 0, 4, -90), (-44, LEDGE, -17, -90), (-36, LEDGE, -17, -90)):
        m.spawn("a", x, y, z, yaw, "b")
    for (x, y, z, yaw) in ((-17, 0, 9, 180), (-14, 6.03, 0, 90), (0, CW, 14, -90), (-47, LEDGE, 8, -90)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: the halls (floor and ledge), over the tops at 22, the catwalks; the chasm has the sky
    for (lx, lz) in ((-40, -10), (-20, -10), (-40, 10), (-20, 10)):
        m.light(lx, 10, lz, 22, 1.4, fixture=False, mirror=True)
    for (lx, lz) in ((-46, -14), (-30, -16), (-15, 0)):
        m.light(lx, LEDGE + 5, lz, 16, 0.9, fixture=False, mirror=True)
    for (lx, lz) in ((-46, -15), (-14, 0)):
        m.light(lx, 25.5, lz, 12, 1.2, fixture=False, mirror=True)
    m.light(0, CW + 3, 14, 10, 0.8, fixture=False, mirror=True)


def decorate(m):
    """accent on the walls only (5 cm proud), a different pattern per storey so you know your height: a skirting
    band on the ground floor, a band under the ledge, pilasters up the back and side walls, frames on the chasm
    face"""
    X0, X1 = -HX1, -HX0
    I0, I1 = X0 + 1, X1 - 1
    iz = HZ - 1
    for z in (-4, 4):
        m.box(I0, 0, z - 0.5, I0 + 0.05, TOP, z + 0.5, accent=True, mirror=True)
    for x in (-42, -30, -20):
        m.box(x - 0.5, 0, -iz, x + 0.5, TOP, -iz + 0.05, accent=True, mirror=True)
    m.box(I0, 0, -iz, I1, 0.25, -iz + 0.05, accent=True, mirror=True)
    m.box(I0, 0, -iz + 0.05, I0 + 0.05, 0.25, iz - 0.05, accent=True, mirror=True)
    m.box(I0, LEDGE - 2.4, -iz + 0.05, I0 + 0.05, LEDGE - 2.0, iz - 0.05, accent=True, mirror=True)
    m.box(I0 + 0.05, LEDGE - 2.4, -iz, I1, LEDGE - 2.0, -iz + 0.05, accent=True, mirror=True)
    m.box(X1, 6.0 + DOOR_H, -3, X1 + 0.05, 6.0 + DOOR_H + 0.3, 3, accent=True, mirror=True)
    for (z0, z1) in ((-3, -2.5), (2.5, 3)):
        m.box(X1, 6.0, z0, X1 + 0.05, 6.0 + DOOR_H, z1, accent=True, mirror=True)
    m.box(X1, LINTEL, -15, X1 + 0.05, LINTEL + 0.3, 15, accent=True, mirror=True)
