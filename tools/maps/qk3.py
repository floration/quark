"""QK3 TWIN HALLS (1.19.6): ranked VANILLA and INSTAGIB, box and accent only. Two tall halls (38 x 38 m inside, 27 m
to the roof) face each other across a 20 m chasm (void). Three bridges cross it, stacked at 6, 14 and 22 m (the top
one without rails). In each hall: a bastion block at the chasm wall that the 6 m bridge lands on (steps down its
back), a ledge ring at 14 m (a long stair up from the floor), a balcony at 22 m over the ledge (ladder) where the top
bridge lands, and an alcove at 22 m on the back wall (a long ladder up from the floor). Under the bridges two cross
catwalks at -8 m join the halls' ground doors by ladders: the flank. Point symmetric, the sun overhead."""
from .prefabs import *

HX0, HX1 = 10, 50      # a hall's span in x (the east one; the west one is its mirror), walls 1 m
HZ = 20                # halls and chasm: z -20..20
TOP = 27.0             # the halls' roofs
LID = 32.0
LEDGE = 14.0
DOOR_H = 4.5           # (a duel giant is 3.6 m)
CW = -8.0              # the catwalks


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


def wall_x(m, x0, x1, z0, z1, y0, y1, holes):
    """a wall across z (thickness x0..x1) from z0 to z1 with holes [(c0, c1, h0, h1)] (west hall side; mirrored)"""
    pos = z0
    for (c0, c1, h0, h1) in sorted(holes):
        if c0 > pos: m.box(x0, y0, pos, x1, y1, c0, mirror=True)
        if h0 > y0: m.box(x0, y0, c0, x1, h0, c1, mirror=True)
        if h1 < y1: m.box(x0, h1, c0, x1, y1, c1, mirror=True)
        pos = c1
    if pos < z1: m.box(x0, y0, pos, x1, y1, z1, mirror=True)


def layout(m):
    # everything is built for the WEST hall (x -50..-10) and point-mirrored to the east one
    X0, X1 = -HX1, -HX0          # -50, -10
    I0, I1 = X0 + 1, X1 - 1      # inside: -49..-11
    iz = HZ - 1
    # ---- the hall's floor, walls and roof (the chasm wall: doors at 6 / 14 / 22 for the bridges, ground doors at
    # z +-12..16 onto the catwalk ladders)
    m.box(X0, -1, -HZ, X1, 0, HZ, mirror=True)
    m.box(X0, -1, -HZ, I0, TOP, HZ, mirror=True)                                     # back wall
    m.box(I0, -1, -HZ, X1, TOP, -iz, mirror=True); m.box(I0, -1, iz, X1, TOP, HZ, mirror=True)
    # the chasm wall (x -11..-10): ground doors at z +-12..16, the bridge doors stacked at z -2.5..2.5
    for (z0, z1) in ((-iz, -16), (-12, -2.5), (2.5, 12), (16, iz)):
        m.box(I1, -1, z0, X1, TOP, z1, mirror=True)
    for (z0, z1) in ((-16, -12), (12, 16)):
        m.box(I1, DOOR_H, z0, X1, TOP, z1, mirror=True)
    ys = [-1.0] + [v for y in (6.0, LEDGE, 22.0) for v in (y, y + DOOR_H)] + [TOP]
    for k in range(0, len(ys), 2):
        m.box(I1, ys[k], -2.5, X1, ys[k + 1], 2.5, mirror=True)
    m.box(X0, TOP, -HZ, X1, TOP + 0.6, HZ, mirror=True)                              # the roof
    m.box(X0, TOP + 0.6, -HZ, X1, LID, HZ, mat="clip", mirror=True)                   # (the roof is no floor)
    # ---- the chasm: end walls at z +-20 from the void up (the halls' sides continue across), a lid
    m.box(-HX0, -16, HZ - 1, HX0, TOP, HZ, mirror=True)
    m.box(X0, -16, -HZ, X1, -1, HZ, mirror=True)                                    # the halls' foundations (into the void)
    m.box(-HX0, TOP, -HZ, HX0, LID, -HZ + 1, mat="clip", mirror=True)
    m.box(-HX1, LID, -HZ, HX1, LID + 1, HZ, mat="clip")
    # ---- the three bridges, stacked at z 0: 6 and 14 with rails, 22 without
    for y in (6.0, LEDGE, 22.0):
        m.box(-HX0, y - 0.6, -2.5, HX0, y, 2.5, accent=(y == 22.0))
        if y < 22:
            m.box(-HX0, y, -2.5, HX0, y + 1.0, -2.2, accent=True); m.box(-HX0, y, 2.2, HX0, y + 1.0, 2.5, accent=True)
    # ---- the catwalks at -8: two across the chasm (z +-14), ladders up the foundations to the ground doors
    m.box(-HX0, CW - 0.5, 12.5, HX0, CW, 15.5, mirror=True)
    m.box(-HX0, CW, 12.5, HX0, CW + 0.9, 12.7, accent=True, mirror=True)
    m.box(-HX0, CW, 15.3, HX0, CW + 0.9, 15.5, accent=True, mirror=True)
    m.ladder(-HX0 + 0.12, 14, CW, 0, "w"); m.ladder(HX0 - 0.12, -14, CW, 0, "e")
    m.ladder(HX0 - 0.12, 14, CW, 0, "e"); m.ladder(-HX0 + 0.12, -14, CW, 0, "w")
    # ---- inside the hall: the bastion the 6 m bridge lands on (steps down its back), the ledge ring at 14 (a long
    # stair up the north side), the balcony at 22 (a ladder up its post from the ledge), the back alcove at 22
    m.box(-17, 0, -4, I1, 6.0, 4, mirror=True)
    m.box(-17.05, 5.7, -4.05, I1, 6.0, 4.05, accent=True, mirror=True)
    m.stairs(-29, -2, -17, 2, 6.0, "x", 1, mirror=True)
    for (x0, z0, x1, z1) in minus((I0, -iz, I1, iz), [(I0 + 4, -iz + 4, I1 - 4, iz - 4)]):
        m.box(x0, LEDGE - 0.5, z0, x1, LEDGE, z1, mirror=True)
    for (x0, z0, x1, z1) in ((I0 + 4, -iz + 4, I1 - 4, -iz + 4.3), (I0 + 4, iz - 4.3, I1 - 4, iz - 4)):
        m.box(x0, LEDGE - 0.5, z0, x1, LEDGE + 0.04, z1, accent=True, mirror=True)
    m.box(I0 + 4, LEDGE - 0.5, -iz + 4, I0 + 4.3, LEDGE + 0.04, iz - 4, accent=True, mirror=True)
    m.box(I1 - 4.3, LEDGE - 0.5, -iz + 4, I1 - 4, LEDGE + 0.04, iz - 4, accent=True, mirror=True)
    m.stairs(-41, iz - 7, -19, iz - 4, LEDGE, "x", -1, mirror=True, solid=False)
    m.box(-17, 21.5, -4, I1, 22.0, 4, mirror=True)                                    # the balcony at 22
    m.box(-15.5, LEDGE, 4, -13.5, 21.5, 5, mirror=True)                               # its post (the ladder's)
    m.ladder(-14.5, 5.12, LEDGE, 22.0, "n"); m.ladder(14.5, -5.12, LEDGE, 22.0, "s")
    m.box(I0, 21.5, -4, -43, 22.0, 4, mirror=True)                                    # the back alcove at 22
    m.box(-44, 0, -1, -43, 21.5, 1, mirror=True)                                      # its post
    m.ladder(-42.88, 0, 0, 22.0, "w"); m.ladder(42.88, 0, 0, 22.0, "e")
    m.box(I0, 22.0, -4, I0 + 0.3, 25.5, 4, accent=True, mirror=True)
    # cover on the floor: two blocks and low walls (the same in each hall)
    m.box(-36, 0, -10, -32, 3.0, -6, mirror=True)
    m.box(-26, 0, 6, -22, 1.2, 10, mirror=True)
    m.box(-40, 0, -2, -38.8, 1.2, 4, mirror=True)
    # ---- spawns: four a hall (two on the floor, two on the side ledge), facing the chasm
    for (x, y, z, yaw) in ((-30, 0, -8, -90), (-40, 0, 7, -90), (-44, LEDGE, -17, -90), (-38, LEDGE, -17, -90)):
        m.spawn("a", x, y, z, yaw, "b")
    for (x, y, z, yaw) in ((-20, 0, 9, 180), (-14, 6.0, 0, 90), (0, CW, 14, -90), (-46, 22.0, 0, -90)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: the halls (floor and ledge), the chasm has the sky
    for (lx, lz) in ((-40, -10), (-20, -10), (-40, 10), (-20, 10)):
        m.light(lx, 10, lz, 22, 1.4, fixture=False, mirror=True)
    for (lx, lz) in ((-46, -14), (-30, -16), (-15, 0)):
        m.light(lx, LEDGE + 5, lz, 16, 0.9, fixture=False, mirror=True)
    m.light(0, CW + 3, 14, 10, 0.8, fixture=False, mirror=True)


def decorate(m):
    """accent on the walls only (5 cm proud): pilasters up the back and side walls, a skirting band, frames round
    the bridge doors on the chasm side"""
    X0, X1 = -HX1, -HX0
    I0, I1 = X0 + 1, X1 - 1
    iz = HZ - 1
    for z in (-12, -4, 4, 12):
        m.box(I0, 0, z - 0.5, I0 + 0.05, TOP, z + 0.5, accent=True, mirror=True)
    for x in (-42, -30, -20):
        m.box(x - 0.5, 0, -iz, x + 0.5, TOP, -iz + 0.05, accent=True, mirror=True)
    m.box(I0, 0, -iz, I1, 0.25, -iz + 0.05, accent=True, mirror=True)
    m.box(I0, 0, -iz + 0.05, I0 + 0.05, 0.25, iz - 0.05, accent=True, mirror=True)
    for y in (6.0, LEDGE, 22.0):
        m.box(X1, y + DOOR_H, -3, X1 + 0.05, y + DOOR_H + 0.3, 3, accent=True, mirror=True)
        for (z0, z1) in ((-3, -2.5), (2.5, 3)):
            m.box(X1, y, z0, X1 + 0.05, y + DOOR_H, z1, accent=True, mirror=True)
