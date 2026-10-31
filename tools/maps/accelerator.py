"""PARTICLE ACCELERATOR (1.19.6): race through a particle physics lab: about 1,550 m of running plus 100 m of drops and a
120 m climb, about 75 s at 24 m/s (the ring tunnel is 390 m of it, a quarter).
  1 the surface campus: the start grid, an S between two office blocks (hedges make the road the line), the wooden globe by its last
    bends, the glass foyer;
  2 the facilities: the magnet test hall (four rows of dipole magnets on stands to weave through, an invisible
    ceiling 4.5 m up over them so nobody hooks or hops over a row), gantry stairs up to the control room on the
    mezzanine, the enclosed skybridge, the cryogenics yard (tanks, pipe racks, a coolant trench: x1.3 speed);
  3 the access shaft: 100 m down in three drops onto ledges offset west / east, so nobody falls straight through;
  4 the detector cavern: the detector split open for repairs, its two 12-sided halves 30 m apart along the beam pipe.
    Run through half A, cross the gap (hook the beam pipe's supports, or drop into the service pit and climb the
    scaffold tower's ladders), run through half B and out of its open end cap;
  5 the ring tunnel: 8.2 m wide, 5 m tall, a long left-hand bend in straight runs turned 7.5 degrees at each joint:
    magnets along the outer wall, carts, magnet stands to hop, cable trays to slide under (or swing round), half-closed
    sector doors;
  6 the ventilation shaft up: a 60 degree kicker into an octagonal shaft 120 m tall with a low-gravity updraft, a fan
    cage to thread; the walkable way is four ladders of 20-30 m with landings; the low gravity ends 10 m under the
    top, an open canopy on four posts, so the climb carries straight out onto the roof;
  7 the car park: out of the vent stack on the roof, down three ramps between the parked cars, the finish in the
    ground floor's north bay.
Checkpoints stand on their floors (R puts you back on the floor at the box's middle) and are at most 10.5 m tall;
under open sky or a tall roof a clip pane over each one stops anybody flying over it. The underground floor is at
y -90 and the void line at -99 (the engine holds feet at -100 or over). There is no fall damage, so the shaft's drops
are the walkable way down."""
import math
from .prefabs import M, minus
from .m4kit import Kit, num, MAT

VOID = -99.0          # (the engine holds feet at y -100 or over: the void line must be above that to ever fire)
CAV_Y = -90.0          # the underground floor (shaft bottom, cavern, tunnel, vent room)


def build():
    m = M("Particle Accelerator", "race", sun=(0.45, 1.0, 0.3))
    k = Kit(m)
    m.raw(f"void {VOID:g}")
    m.raw("palette base 0.62 0.63 0.66 accent 0.16 0.34 0.62 metal 0.30 0.32 0.36 glow 0.70 0.90 1.00 sky 0.58 0.70 0.86")
    cps = []   # checkpoints in race order (written at the end, in this order)

    def cp(x0, floor, z0, x1, top, z1, seal=False):
        """a checkpoint across a section, its bottom on the floor (R respawns you at the box's bottom, x/z centre) and at
        most 10.5 m tall (the bots and the route checks head for its centre: within 6 m over a floor); seal: under open
        sky or a tall roof, a clip pane over it up to `top`, so nobody hooks or flies over it"""
        h = min(top - floor, 10.5)
        cps.append((x0, floor, z0, x1, floor + h, z1))
        if seal and top > floor + h: k.box("clip", x0, floor + h, z0, x1, top, z1)

    def poly_yz(mat, pts, x0, x1):
        """a prism along x over convex polygon pts [(z, y)]"""
        zs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        cz, cy = sum(zs) / len(zs), sum(ys) / len(ys)
        planes = []
        for i in range(len(pts)):
            (az, ay), (bz, by) = pts[i], pts[(i + 1) % len(pts)]
            nz, ny = by - ay, -(bz - az)
            if nz * (cz - az) + ny * (cy - ay) > 0: nz, ny = -nz, -ny
            L = math.hypot(nz, ny)
            if L < 1e-9: continue
            nz, ny = nz / L, ny / L
            if abs(nz) < 1e-6 or abs(ny) < 1e-6: continue
            planes.append((0, ny, nz, ny * ay + nz * az))
        k.cut(mat, (x0, min(ys), min(zs), x1, max(ys), max(zs)), planes)

    def barrel(mat, x0, x1, cy, cz, r0, r1, n=12, skip=()):
        """a horizontal n-sided ring along x (flat faces top and bottom), radii as circumradii"""
        for i in range(n):
            if i in skip: continue
            a = math.radians(360.0 / n * i + 180.0 / n); b = math.radians(360.0 / n * (i + 1) + 180.0 / n)
            pts = [(cz + r0 * math.cos(a), cy + r0 * math.sin(a)), (cz + r1 * math.cos(a), cy + r1 * math.sin(a)),
                   (cz + r1 * math.cos(b), cy + r1 * math.sin(b)), (cz + r0 * math.cos(b), cy + r0 * math.sin(b))]
            poly_yz(mat, pts, x0, x1)

    def yard_walls(x0, z0, x1, z1, h, mat="precastconcrete", gaps=()):
        """a yard's four walls 1 m thick outside x0..x1 / z0..z1, with clip from their top to 120 m: gaps = [(side, a, b)]
        left open (side n/s/w/e, a..b along it)"""
        for side in "nswe":
            if side in "ns":
                z = z0 - 1 if side == "n" else z1
                runs = minus((x0 - 1, 0, x1 + 1, 1), [(a, 0, b, 1) for (sd, a, b) in gaps if sd == side])
                for (a, _, b, _) in runs:
                    k.box(mat, a, 0, z, b, h, z + 1); k.box("clip", a, h, z, b, 120, z + 1)
            else:
                x = x0 - 1 if side == "w" else x1
                runs = minus((0, z0, 1, z1), [(0, a, 1, b) for (sd, a, b) in gaps if sd == side])
                for (_, a, _, b) in runs:
                    k.box(mat, x, 0, a, x + 1, h, b); k.box("clip", x, h, a, x + 1, 120, b)

    # ================================================================== 1. the surface campus (x -400..-110, z -64..64)
    CX0, CX1, CZ = -400, -110, 64
    ROADS = [(-372, 22, -270, 36), (-270, -36, -256, 36), (-256, -36, -186, -22), (-200, -22, -186, 10), (-186, -8, CX1, 8)]
    WALKS = [(-372, 19, -270, 22), (-372, 36, -270, 39), (-186, -11, CX1, -8), (-186, 8, CX1, 11)]
    for (a, b, c, d) in minus((CX0, -CZ, CX1, CZ), ROADS + WALKS):
        k.box("grass", a, -1, b, c, 0, d)
    for r in ROADS: k.box("asphalt", r[0], -1, r[1], r[2], 0, r[3])
    for r in WALKS: k.box("sidewalkslabs", r[0], -1, r[1], r[2], 0, r[3])
    yard_walls(CX0, -CZ, CX1, CZ, 4, mat="sidewalkslabs", gaps=[("e", -40, 40)])
    # the two office blocks that make the S: A on the north side, B on the south (solid: you run round them)
    k.box("precastconcrete", -320, 0, -CZ, -280, 18, 18)
    k.box("precastconcrete", -240, 0, -18, -200, 18, CZ)
    k.box("clip", -320, 18, -CZ, -280, 120, 18); k.box("clip", -240, 18, -18, -200, 120, CZ)
    # the start grid: eight racers facing east down the first straight
    for i, z in enumerate((25, 28, 31, 34)):
        for x in (-352, -348):
            k.spawn("race", x, 0, z, -90)
    # the S as the line: hedges (clip over them) fill the yard round a 22-24 m road that turns 90 degrees at each
    # block's corner (review: the racing line cut across an empty 128 m lawn)
    LANES = [(CX0, 18, -256, 40), (-280, -42, -256, 40), (-280, -42, -176, -18), (-200, -42, -176, 12), (-200, -12, CX1, 12)]
    BLOCKS = [(-320, -CZ, -280, 18), (-240, -18, -200, CZ)]
    for (a, b, c, d) in minus((CX0, -CZ, CX1, CZ), LANES + BLOCKS):
        k.box("hedge", a, 0, b, c, 2.6, d); k.box("clip", a, 2.6, b, c, 120, d)
    cp(-262, 0, -42, -258, 120, 40, seal=True)                                    # CP1: between the office blocks
    # the main building's front (x -110): concrete with the glass foyer front round a 16 m entrance
    MB0, MB1 = -110, -70
    k.box("precastconcrete", MB0, 0, -40, MB0 + 1, 14, -8); k.box("precastconcrete", MB0, 0, 8, MB0 + 1, 14, 40)
    k.box("precastconcrete", MB0, 7, -8, MB0 + 1, 14, 8)
    k.box("precastconcrete", MB0, 0, -40, MB1, 14, -39); k.box("precastconcrete", MB0, 0, 39, MB1, 14, 40)
    k.box("polishedconcrete", MB0, -1, -39, MB1, 0, 39)
    k.box("nograpple", MB0, 14, -40, MB1, 15, 40)
    k.box("clip", MB0, 15, -40, MB1, 120, 40)
    cp(-94, 0, -39, -90, 14, 39)                                      # CP2: the foyer
    # the foyer's back wall into the magnet hall: a 16 m opening
    k.box("precastconcrete", MB1 - 1, 0, -39, MB1, 16, -8); k.box("precastconcrete", MB1 - 1, 0, 8, MB1, 16, 39)
    k.box("precastconcrete", MB1 - 1, 8, -8, MB1, 16, 8)

    # ================================================================== 2. the facilities
    # the magnet test hall x -70..110, z -24..24, 16 m tall
    H0, H1, HZ, HH = -70, 110, 24, 16
    k.box("epoxyfloor", H0, -1, -HZ, H1, 0, HZ)
    k.box("insulatedpanel", H0, 0, -HZ - 1, H1, HH, -HZ); k.box("insulatedpanel", H0, 0, HZ, H1, HH, HZ + 1)
    k.box("insulatedpanel", H1, 0, -HZ - 1, H1 + 1, HH, -3); k.box("insulatedpanel", H1, 0, 3, H1 + 1, HH, HZ + 1)
    k.box("insulatedpanel", H1, 0, -3, H1 + 1, 11.5, 3); k.box("insulatedpanel", H1, 16, -3, H1 + 1, HH, 3)
    k.box("nograpple", H0 - 1, HH, -HZ - 1, H1 + 1, HH + 1, HZ + 1)
    # four rows of magnets, each row a wall across the hall with one 8 m way through, alternately south and north
    ROWS = [(-55, (12, 20)), (-25, (-20, -12)), (5, (12, 20)), (35, (-20, -12))]
    for (rx, (g0, g1)) in ROWS:
        z = -HZ + 0.5
        spans = [(-HZ + 0.5, g0), (g1, HZ - 0.5)]
        for (a, b) in spans:
            n = max(1, int(round((b - a + 0.5) / 3.0)))
            w = (b - a + 0.5) / n
            for i in range(n):
                z0 = a + i * w; z1 = z0 + w - 0.5
                if z1 - z0 < 0.6: continue
                k.box("bluedsteel", rx, 1.0, z0, rx + 15, 3.0, z1)   # the dipole
                k.box("copperandpatina", rx - 0.4, 1.1, z0 + 0.15, rx, 2.9, z1 - 0.15)                   # coil ends
                k.box("copperandpatina", rx + 15, 1.1, z0 + 0.15, rx + 15.4, 2.9, z1 - 0.15)
                for sx in (rx + 1.5, rx + 12.5):
                    k.box("galvanizedsteel", sx, 0, z0 + 0.2, sx + 1.0, 1.0, z1 - 0.2)                     # stands
    k.box("clip", -57, 4.5, -HZ, 52, HH, HZ)   # (nobody over the rows: no hopping, no hooking)
    cp(-5, 0, -HZ, 0, HH, HZ)                                          # CP3: mid-hall
    # gantry stairs up to the mezzanine (y 12) and the control room on it
    k.stairs("steelcheckerfloor", 60, -4, 84, 4, 12, "x", 1, solid=False)
    MZ0 = 84
    k.box("steelcheckerfloor", MZ0, 11.4, -HZ, 92, 12, HZ)
    k.box("steelcheckerfloor", 92, 11.4, -12.4, H1, 12, 12.4)   # (only under the control room: no dead corners beside it)
    for (px, pz) in ((MZ0, -HZ + 0.5), (MZ0, HZ - 1.5), (MZ0, -5.5), (MZ0, 4.5)):
        k.box("galvanizedsteel", px, 0, pz, px + 1, 11.4, pz + 1)
    k.box("galvanizedsteel", MZ0, 12, -HZ, MZ0 + 0.2, 13.1, -4); k.box("galvanizedsteel", MZ0, 12, 4, MZ0 + 0.2, 13.1, HZ)   # railing
    # the control room: x 92..110, z -12..12, 4 m; glass to the hall, a 6 m door west, the bridge door east
    CR0 = 92
    k.box("insulatedpanel", CR0, 12, -12, CR0 + 0.4, 16, -3); k.box("insulatedpanel", CR0, 12, 3, CR0 + 0.4, 16, 12)
    k.box("insulatedpanel", CR0, 15.4, -3, CR0 + 0.4, 16, 3)
    k.box("insulatedpanel", CR0, 12, -12.4, H1, 16, -12); k.box("insulatedpanel", CR0, 12, 12, H1, 16, 12.4)
    k.box("nograpple", CR0, 16, -12.4, H1, 16.4, 12.4)
    cp(98, 12, -12, 102, 16.4, 12)                                       # CP4: the control room
    # the skybridge: x 110..180, enclosed (slot windows nobody fits through), 6 m wide
    B0, B1 = H1 + 1, 180
    k.box("steelcheckerfloor", B0, 11, -3, B1, 12, 3)
    for z0, z1 in ((-4, -3), (3, 4)):
        k.box("insulatedpanel", B0, 11, z0, B1, 13.2, z1); k.box("insulatedpanel", B0, 13.9, z0, B1, 16.4, z1)
        x = B0
        while x < B1 - 0.1:
            k.box("insulatedpanel", x, 13.2, z0, x + 0.4, 13.9, z1); x += 3.5
    k.box("nograpple", B0, 16, -4, B1, 16.4, 4)
    k.box("bridgegirder", B0, 10, -3.5, B1, 11, 3.5)
    # the cryogenics yard x 180..262, z -36..36: the cold box (its roof the deck the bridge lands on), stairs down
    Y0, Y1, YZ = 180, 262, 36
    TR0, TR1 = 226, 256      # the coolant trench along the yard's middle: 1 m deep, 4 m wide (x1.3 speed in it)
    for (a, b, c, d) in minus((Y0, -YZ, Y1, YZ), [(TR0, -2, TR1, 2)]):
        k.box("polishedconcrete", a, -1, b, c, 0, d)
    k.box("polishedconcrete", TR0, -2, -2, TR1, -1, 2)
    k.box("polishedconcrete", TR1 - 1.5, -1, -2, TR1, -0.5, 2)              # (the step back up)
    k.box("coolant", TR0, -1, -2, TR1 - 1.5, -0.2, 2)
    k.box("clip", Y0 - 1, 16.4, -4, Y0, 120, 4)                             # (over the bridge where it comes in)
    yard_walls(Y0, -YZ, Y1, YZ, 8, gaps=[("w", -4, 4), ("e", -6, 6)])
    k.box("insulatedpanel", Y0, 0, -10, 198, 12, 10)                    # the cold box
    k.box("steelcheckerfloor", Y0, 11.6, -10, 198, 12, 10)
    k.stairs("steelcheckerfloor", 198, -4, 222, 4, 12, "x", -1, solid=False)
    # the bridge's wall stub over the yard's west gap (the bridge comes in there, high)
    k.box("precastconcrete", Y0 - 1, 0, -4, Y0, 11, 4)
    # tanks (12-sided, 18 m) off the line
    for (tx, tz, r) in ((214, -24, 5), (214, 24, 5), (240, -26, 4), (244, 25, 4), (230, 14, 3)):
        k.disc("rivetedplates", tx, tz, r, 0, 18, n=12)
    cp(222.5, 0, -YZ, 225.5, 120, YZ, seal=True)                                   # CP5: the cryo yard, off the stairs (clear of the trench)
    # the shaft head x 262..292, z -16..16, door west 12 m
    SH0, SH1, SHZ, SHS = 262, 292, 16, 20     # (north z -16, south z 20: the stairwell beside the shaft)
    S0, S1, SZ = 265, 285, 12      # the shaft x 265..285, z -12..12
    SW = (264, 13, 286, 19)        # the stairwell's inside (x 264..286, z 13..19), 100 m of switchback stairs
    k.box("precastconcrete", SH0, 0, -SHZ, SH0 + 1, 14, -6); k.box("precastconcrete", SH0, 0, 6, SH0 + 1, 14, SHS)
    k.box("precastconcrete", SH0, 7, -6, SH0 + 1, 14, 6)
    k.box("precastconcrete", SH0, 0, -SHZ - 1, SH1, 14, -SHZ); k.box("precastconcrete", SH0, 0, SHS, SH1, 14, SHS + 1)
    k.box("precastconcrete", SH1, 0, -SHZ - 1, SH1 + 1, 14, SHS + 1)
    k.box("nograpple", SH0, 14, -SHZ - 1, SH1 + 1, 15, SHS + 1)
    for (a, b, c, d) in minus((SH0, -SHZ, SH1, SHS), [(S0, -SZ, S1, SZ), (267, SW[1], SW[2], SW[3])]):
        k.box("steelcheckerfloor", a, -1, b, c, 0, d)
    # the stairwell: railings round its opening (in at the west landing), 13 flights of 16 steps, landings at the ends
    k.box("galvanizedsteel", 267, 0, SW[1] - 0.1, SW[2], 1.1, SW[1]); k.box("galvanizedsteel", 267, 0, SW[3], SW[2], 1.1, SW[3] + 0.1)
    k.box("galvanizedsteel", SW[2], 0, SW[1], SW[2] + 0.1, 1.1, SW[3])
    k.box("precastconcrete", SW[0] - 1, CAV_Y - 1, SW[1], SW[0], -1, SW[3] + 1); k.box("precastconcrete", SW[2], CAV_Y - 1, SW[1], SW[2] + 1, -1, SW[3] + 1)
    k.box("precastconcrete", SW[0], CAV_Y - 1, SW[3], SW[2], -1, SW[3] + 1)
    k.box("precastconcrete", SW[0], CAV_Y - 1, SW[1], SW[2], CAV_Y, SW[3])
    k.box("precastconcrete", 267, CAV_Y, 15.85, 283, -1, 16.15)                  # (between the two lanes)
    NF = 13; dy = -CAV_Y / NF
    for f in range(NF):
        top = -f * dy; bot = top - dy
        if f % 2 == 0:
            k.stairs("steelcheckerfloor", 267, SW[1], 283, 15.85, top, "x", -1, base=bot, solid=False)
            k.box("steelcheckerfloor", 283, bot - 0.5, SW[1], SW[2], bot, SW[3])
        else:
            k.stairs("steelcheckerfloor", 267, 16.15, 283, SW[3], top, "x", 1, base=bot, solid=False)
            k.box("steelcheckerfloor", SW[0], bot - 0.5, SW[1], 267, bot, SW[3])
    cp(SH0 + 1, 0, -SZ, S0 - 0.4, 14, SZ)                            # CP6: in the shaft head's door (on its floor, short of the shaft)

    # ================================================================== 3. the shaft: 100 m down, three drops
    k.box("nograpple", S0 - 1, CAV_Y - 1, -SZ - 1, S0, -1, SZ + 1)
    k.box("nograpple", S1, CAV_Y + 6, -SZ - 1, S1 + 1, -1, SZ + 1)
    k.box("nograpple", S1, CAV_Y - 1, -SZ - 1, S1 + 1, CAV_Y + 6, -4); k.box("nograpple", S1, CAV_Y - 1, 4, S1 + 1, CAV_Y + 6, SZ + 1)
    k.box("nograpple", S0, CAV_Y - 1, -SZ - 1, S1, -1, -SZ)
    k.box("nograpple", S0, CAV_Y - 1, SZ, 280, -1, SZ + 1); k.box("nograpple", 280, CAV_Y + 4, SZ, S1, -1, SZ + 1)   # (the stairwell's door)
    k.box("steelcheckerfloor", 271, -36, -SZ, S1, -35, SZ)             # ledge 1 (east), y -35
    k.box("steelcheckerfloor", S0, -71, -SZ, 279, -70, SZ)             # ledge 2 (west), y -70
    k.box("tunnellining", S0, CAV_Y - 1, -SZ, S1, CAV_Y, SZ)           # the bottom
    k.box("galvanizedsteel", 271, -38, -2, 272, -36, 2)                # hook points under the lips
    k.box("galvanizedsteel", 278, -73, -2, 279, -71, 2)
    cp(279, CAV_Y, -SZ, 300, -40, SZ)                                  # CP7: the shaft bottom
    # the corridor east to the cavern
    k.box("tunnellining", S1 + 1, CAV_Y - 1, -4, 300, CAV_Y, 4)
    k.box("tunnellining", S1 + 1, CAV_Y, -5, 300, CAV_Y + 6, -4); k.box("tunnellining", S1 + 1, CAV_Y, 4, 300, CAV_Y + 6, 5)
    k.box("tunnellining", S1 + 1, CAV_Y + 6, -5, 300, CAV_Y + 7, 5)

    # ================================================================== 4. the detector cavern x 300..520
    V0, V1, VZ, VTOP = 300, 520, 30, -55.0
    DY = CAV_Y + 12      # the beam axis, y -88
    A0, A1, B0d, B1d = 330, 365, 395, 430
    PIT0, TOW0 = A1, 386
    PITY = CAV_Y - 1.5      # the service pit's floor: 7.5 m under the decks (the bots' longest drop is 8 m)
    k.box("precastconcrete", V0, CAV_Y - 6, -VZ, PIT0, CAV_Y, VZ)                 # floor west of the pit
    k.box("precastconcrete", PIT0, CAV_Y - 7, -VZ, TOW0, PITY, VZ)               # the service pit's floor
    k.box("scaffoldboards", TOW0, PITY, -VZ, B0d, CAV_Y + 6, VZ)                 # the scaffold tower, its top y -84
    k.box("precastconcrete", B0d, CAV_Y - 1, -VZ, V1, CAV_Y, VZ)
    k.box("tunnellining", V0 - 1, CAV_Y + 6, -VZ, V0, VTOP, VZ)
    k.box("tunnellining", V0 - 1, CAV_Y - 7, -VZ, V0, CAV_Y + 6, -5); k.box("tunnellining", V0 - 1, CAV_Y - 7, 5, V0, CAV_Y + 6, VZ)
    k.box("tunnellining", V0 - 1, CAV_Y - 7, -VZ - 1, V1 + 1, VTOP + 1, -VZ); k.box("tunnellining", V0 - 1, CAV_Y - 7, VZ, V1 + 1, VTOP + 1, VZ + 1)
    k.box("tunnellining", V0 - 1, VTOP, -VZ, V1 + 1, VTOP + 1, VZ)
    # the east wall, the tunnel's portal in it (8.2 x 5 m)
    k.box("tunnellining", V1, CAV_Y - 1, -VZ, V1 + 2, VTOP, -4.1); k.box("tunnellining", V1, CAV_Y - 1, 4.1, V1 + 2, VTOP, VZ)
    k.box("tunnellining", V1, CAV_Y + 5, -4.1, V1 + 2, VTOP, 4.1)
    # stairs up to half A's deck (y -84), the gangway, the west shielding wall with its way through
    k.stairs("steelcheckerfloor", 304, -5, 316, 5, CAV_Y + 6, "x", 1, base=CAV_Y, solid=True)
    k.box("steelcheckerfloor", 316, CAV_Y, -6, A0, CAV_Y + 6, 6)
    k.box("rivetedplates", 328, CAV_Y, -VZ, A0, VTOP, -5); k.box("rivetedplates", 328, CAV_Y, 5, A0, VTOP, VZ)
    k.box("rivetedplates", 328, CAV_Y + 14, -5, A0, VTOP, 5)
    # the two halves: 12 sides, 22 m across (circumradius 11.4), coil layer inside, a deck through each
    for (x0, x1) in ((A0, A1), (B0d, B1d)):
        barrel("rivetedplates", x0, x1, DY, 0, 9.6, 11.4)
        # the coil layer inside, in coloured windings along the barrel (what you see running through it)
        cols = ("hammeredcopper", "powdercoatyellow", "bluedsteel", "powdercoatgreen", "hammeredcopper")
        for j, c in enumerate(cols):
            a = x0 + 0.5 + (x1 - x0 - 1) * j / len(cols); b = x0 + 0.5 + (x1 - x0 - 1) * (j + 1) / len(cols)
            barrel(c, a, b, DY, 0, 8.3, 9.6)
        k.box("steelcheckerfloor", x0, CAV_Y + 4, -5, x1, CAV_Y + 6, 5)
    k.box("galvanizedsteel", A0 + 2, CAV_Y, -4, A1 - 2, CAV_Y + 1.6, 4)                 # half A's cradle
    # the beam pipe along the axis, and its supports in the pit (the hook line across the gap)
    k.tube_x("brass", A0 - 2, B1d + 2, DY, 0, 0.7)
    for px in (371, 379):
        k.box("galvanizedsteel", px, PITY, -0.5, px + 1, DY - 0.6, 0.5)
    # the walkable way: two ladders up the tower's west face (12 m)
    for lz in (-9, 9):
        k.ladder(TOW0, lz, PITY, CAV_Y + 6, "e")
    cp(410, CAV_Y + 6, -VZ, 414, -72, VZ)
    k.box("clip", B0d, CAV_Y + 28, -VZ, B1d + 2, VTOP, VZ)                    # (nobody over half B: through it or beside it)                              # CP8: through half B
    # the east shielding wall: half B's open end (10 x 10 m) and two doors at floor level beside it
    k.box("rivetedplates", B1d + 2, CAV_Y, -VZ, B1d + 4, VTOP, -24); k.box("rivetedplates", B1d + 2, CAV_Y, 24, B1d + 4, VTOP, VZ)
    k.box("rivetedplates", B1d + 2, CAV_Y, -14, B1d + 4, VTOP, -5); k.box("rivetedplates", B1d + 2, CAV_Y, 5, B1d + 4, VTOP, 14)
    k.box("rivetedplates", B1d + 2, CAV_Y + 6, -24, B1d + 4, VTOP, -14); k.box("rivetedplates", B1d + 2, CAV_Y + 6, 14, B1d + 4, VTOP, 24)
    k.box("rivetedplates", B1d + 2, CAV_Y + 16, -5, B1d + 4, VTOP, 5)
    k.box("steelcheckerfloor", B1d, CAV_Y, -6, 444, CAV_Y + 6, 6)
    k.stairs("steelcheckerfloor", 444, -6, 456, 6, CAV_Y + 6, "x", -1, base=CAV_Y, solid=True)
    cp(505, CAV_Y, -VZ, 510, VTOP, VZ, seal=True)                                  # CP9: the tunnel portal

    # ================================================================== 5. the ring tunnel
    W = 4.1                                         # half the clear width
    heads = [0.0] + [-(3.75 + 7.5 * i) for i in range(12)] + [-90.0]
    lens = [30.0] + [28.0] * 12 + [24.0]
    pts = [(V1 + 2.0, 0.0)]
    for (h, L) in zip(heads, lens):
        a = math.radians(h)
        pts.append((pts[-1][0] + L * math.cos(a), pts[-1][1] + L * math.sin(a)))
    dirs = [(math.cos(math.radians(h)), math.sin(math.radians(h))) for h in heads]
    left = [(d[1], -d[0]) for d in dirs]            # (the inside of the bend: toward -z at the start)
    def miter(j):
        """joint j's offset vector for a unit offset to the left"""
        if j == 0: return left[0]
        if j == len(heads): return left[-1]
        a, b = left[j - 1], left[j]
        mx, mz = a[0] + b[0], a[1] + b[1]
        L = math.hypot(mx, mz); mx, mz = mx / L, mz / L
        c = mx * a[0] + mz * a[1]
        return (mx / c, mz / c)
    def piece_len(i):
        """how long a turned piece may be: the walk checks see a brush as its box, so a piece's box may stand out of
        it by 0.6 m at most (short pieces where the run is turned most)"""
        a = math.radians(heads[i]); sn = min(abs(math.sin(a)), abs(math.cos(a)))
        return 1e9 if sn < 0.02 else 0.6 / sn
    def band(i, o0, o1, y0, y1, mat, chunk=True):
        """segment i's strip between left offsets o0 and o1 (the clear width is -W..W), y0..y1, in pieces"""
        (ax, az), (bx, bz) = pts[i], pts[i + 1]
        ma, mb = miter(i), miter(i + 1)
        P = lambda f, o: (ax + ma[0] * o + (bx + mb[0] * o - ax - ma[0] * o) * f, az + ma[1] * o + (bz + mb[1] * o - az - ma[1] * o) * f)
        n = max(1, math.ceil(lens[i] / piece_len(i))) if chunk else 1
        for j in range(n):
            f0, f1 = j / n, (j + 1) / n
            k.prism(mat, [P(f0, o0), P(f1, o0), P(f1, o1), P(f0, o1)], y0, y1)
    TY0, TY1 = CAV_Y, CAV_Y + 5
    for i in range(len(heads)):
        band(i, -W - 1, W + 1, TY0 - 1, TY0, "epoxyfloor", chunk=False)
        band(i, -W - 1, W + 1, TY1, TY1 + 1, "nograpple", chunk=False)
        band(i, W, W + 1, TY0, TY1, "tunnellining")
        band(i, -W - 1, -W, TY0, TY1, "tunnellining")
        if 0 < i < len(heads) - 1:
            band(i, -W, -W + 1.2, TY0, TY0 + 1.4, "accent")   # the magnets
    def at(i, f, off):
        """a point f of the way along segment i, off to the left"""
        (ax, az), (bx, bz) = pts[i], pts[i + 1]
        x = ax + (bx - ax) * f + left[i][0] * off; z = az + (bz - az) * f + left[i][1] * off
        return x, z
    def across(i, f, o0, o1, y0, y1, mat, depth=0.6):
        """a slab across segment i at f, from left offset o0 to o1, depth along the run"""
        (ax, az), (bx, bz) = pts[i], pts[i + 1]
        d = dirs[i]; L = lens[i]
        cx, cz = ax + (bx - ax) * f, az + (bz - az) * f
        n = max(1, math.ceil((o1 - o0) / piece_len(i)))
        for j in range(n):
            a0, a1 = o0 + (o1 - o0) * j / n, o0 + (o1 - o0) * (j + 1) / n
            poly = []
            for (sg, o) in ((-1, a0), (1, a0), (1, a1), (-1, a1)):
                poly.append((cx + d[0] * sg * depth / 2 + left[i][0] * o, cz + d[1] * sg * depth / 2 + left[i][1] * o))
            k.prism(mat, poly, y0, y1)
    # obstacles in two sectors (review: one every 35 m was mostly reacting): sector 1 on runs 2-4, sector 2 on runs
    # 9-11, each a hurdle (open magnet stand, 0.9 m), a half-closed sector door and a cart; clean runs between them
    across(2, 0.5, -W + 1.2, W, TY0, TY0 + 0.9, "galvanizedsteel")
    across(3, 0.4, -W + 1.2, 0.4, TY0, TY1, "rivetedplates", depth=1.0)          # sector door, half closed (outer half)
    across(4, 0.6, 1.8, W - 0.2, TY0, TY0 + 1.6, "galvanizedsteel", depth=3.0)    # a cart (inner side)
    across(9, 0.4, -W + 1.2, W, TY0, TY0 + 0.9, "galvanizedsteel")
    across(10, 0.4, -0.4, W, TY0, TY1, "rivetedplates", depth=1.0)               # sector door, inner half
    across(11, 0.5, 1.8, W - 0.2, TY0, TY0 + 1.6, "galvanizedsteel", depth=3.0)
    # the walls read as progress: copper ends on every magnet, a cable tray high on the inner wall, and each
    # sector's colour on the inner wall (yellow, then orange) as a flush panel at every run's start
    for i in range(1, len(heads) - 1):
        for f in (0.03, 0.97):
            across(i, f, -W, -W + 1.25, TY0, TY0 + 1.45, "hammeredcopper", depth=0.5)
        band(i, W - 0.35, W, TY1 - 1.0, TY1 - 0.85, "galvanizedgrating")
        across(i, 0.12, W - 0.03, W, TY0 + 1.6, TY0 + 3.2, "powdercoatyellow" if i < 7 else "orangewirecut", depth=2.0)
    jm = 7
    jx, jz = pts[jm]
    cp(jx - 6, TY0, jz - 6, jx + 6, TY1, jz + 6)                         # CP10: halfway round
    EX, EZ = pts[-1]
    # ================================================================== 6. the vent: room, kicker, octagonal shaft
    SX, SZc = EX, EZ - 14.0          # the shaft's centre
    A = 5.5                          # its inner half-width (11 m across the flats)
    RZ0, RZ1 = EZ - 22, EZ           # the room z (x EX-8..EX+8), y -90..-80
    k.box("tunnellining", EX - 8, TY0 - 1, RZ0, EX + 8, TY0, RZ1)
    k.box("tunnellining", EX - 9, TY0 - 1, RZ0 - 1, EX + 9, TY0 + 11, RZ0); k.box("tunnellining", EX - 9, TY0 - 1, RZ0, EX - 8, TY0 + 11, RZ1 + 1)
    k.box("tunnellining", EX + 8, TY0 - 1, RZ0, EX + 9, TY0 + 11, RZ1 + 1)
    k.box("tunnellining", EX - 8, TY0 - 1, RZ1, EX - W, TY0 + 11, RZ1 + 1); k.box("tunnellining", EX + W, TY0 - 1, RZ1, EX + 8, TY0 + 11, RZ1 + 1)
    k.box("tunnellining", EX - W, TY1, RZ1, EX + W, TY0 + 11, RZ1 + 1)
    for (a, b, c, d) in minus((EX - 8, RZ0, EX + 8, RZ1), [(SX - A, SZc - A, SX + A, SZc + A)]):
        k.box("nograpple", a, TY0 + 10, b, c, TY0 + 11, d)
    cp(EX - 8, TY0, EZ - 6, EX + 8, TY0 + 10, EZ)                             # CP11: the vent room
    # the kicker: 60 degrees, rising north into the shaft
    k.wedge("rivetedplates", 3, EX - 4, TY0, EZ - 4 - 15 / math.tan(math.radians(60)), EX + 4, TY0 + 15, EZ - 4)
    # the shaft: flat walls + chamfered corners, from the room's ceiling (y -90) to the stack's cap (y 27)
    def octa(y0, y1, mat, doors=()):
        """the octagon's walls y0..y1; doors = [(side, y0, y1)] cut 5 m wide in a flat side (n s w e)"""
        for side in "nswe":
            cuts = [(dy0, dy1) for (sd, dy0, dy1) in doors if sd == side]
            if side in "ns":
                z0 = SZc - A - 1 if side == "n" else SZc + A
                box = (SX - A - 1, z0, SX + A + 1, z0 + 1); hole = (SX - 2.5, SX + 2.5)
            else:
                x0 = SX - A - 1 if side == "w" else SX + A
                box = (x0, SZc - A - 1, x0 + 1, SZc + A + 1); hole = (SZc - 2.5, SZc + 2.5)
            y = y0
            for (c0, c1) in sorted(cuts) + [(y1, y1)]:
                if c0 > y:
                    k.box(mat, box[0], y, box[1], box[2], c0, box[3])
                if c1 > c0:   # the door's sides and lintel
                    if side in "ns":
                        k.box(mat, box[0], c0, box[1], hole[0], c1, box[3]); k.box(mat, hole[1], c0, box[1], box[2], c1, box[3])
                    else:
                        k.box(mat, box[0], c0, box[1], box[2], c1, hole[0]); k.box(mat, box[0], c0, hole[1], box[2], c1, box[3])
                y = max(y, c1)
        c = 3.22
        for (sx, sz) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            cx, cz = SX + sx * A, SZc + sz * A
            k.prism(mat, [(cx, cz), (cx - sx * c, cz), (cx, cz - sz * c)], y0, y1)
    octa(CAV_Y + 10, 20, "ventilationduct")
    # the top: an open canopy, four corner posts under the cap (the climb carries straight out onto the roof)
    for (sx, sz) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        cx, cz = SX + sx * A, SZc + sz * A
        k.box("ventilationduct", min(cx, cx + sx * 1.0), 20, min(cz, cz + sz * 1.0), max(cx, cx + sx * 1.0), 27, max(cz, cz + sz * 1.0))
    k.box("nograpple", SX - A - 1, 27, SZc - A - 1, SX + A + 1, 28, SZc + A + 1)     # the stack's cap
    # the walkable way up: three 40 m ladders, each wall stepping back to leave a landing on the last one's top
    # (each ladder 1.2 m to the other side of the last, so stepping off one onto its landing never catches the next)
    for j, (y0, y1) in enumerate(((TY0, -70), (-70, -40), (-40, -10), (-10, 20))):
        d = 1.5 * (3 - j)
        if d: k.box("galvanizedsteel", SX - 2.5, y0, SZc - A, SX + 2.5, y1, SZc - A + d)
        k.ladder(SX + (1.2 if j % 2 == 0 else -1.2), SZc - A + d, y0, y1, "n")
    # the grating at the top (a 4 m hole in the middle to fly up through; open over the top ladder)
    for (a, b, c, d) in minus((SX - A, SZc - A, SX + A, SZc + A), [(SX - 2, SZc - 2, SX + 2, SZc + 2), (SX - 2, SZc - A, SX - 0.4, SZc - A + 2)]):
        k.box("galvanizedgrating", a, 19.6, b, c, 20, d)
    # the updraft: low gravity up the middle (off the ladders)
    k.box("lowgravityfield", SX - A, CAV_Y + 11, SZc - A + 5, SX + A, 9.5, SZc + A)   # (ends 10 m under the top: the climb carries out)
    # the fan cage at y -25: two diagonal bars and a hub to thread past
    k.bar("galvanizedsteel", (SX - A, SZc + A), (SX + A, SZc - A + 5), 1.0, -25.5, -24.5)
    k.bar("galvanizedsteel", (SX - A, SZc - A + 5), (SX + A, SZc + A), 1.0, -25.5, -24.5)
    k.disc("galvanizedsteel", SX, SZc, 1.6, -26, -24, n=8)
    cp(SX - 8, 20, SZc - 13.5, SX + 28, 120, SZc - A - 1.5, seal=True)   # CP12: across the roof between the stack and ramp 3

    # ================================================================== 7. the car park (y 0, 6.67, 13.33, 20)
    PX0, PX1 = SX - 8, SX + 28
    PZ0, PZ1 = SZc - 56, SZc + 7
    LV = [0.0, 20 / 3, 40 / 3, 20.0]
    WL = (SX - 8, SX + 4)       # the west lane (R3 roof -> deck 2, R1 deck 1 -> ground)
    EL = (SX + 16, SX + 28)     # the east lane (R2 deck 2 -> deck 1)
    RZa, RZb = SZc - 44, SZc - 14
    stack = (SX - A - 1, SZc - A - 1, SX + A + 1, SZc + A + 1)
    holes = {0: [], 1: [(WL[0], RZa, WL[1], RZb)], 2: [(EL[0], RZa, EL[1], RZb)], 3: [(WL[0], RZa, WL[1], RZb)]}
    for lv, y in enumerate(LV):
        for (a, b, c, d) in minus((PX0, PZ0, PX1, PZ1), holes[lv] + [stack]):
            k.box("asphalt", a, y - 0.5 if lv else y - 1, b, c, y, d)
    # the ramps (12.4 degrees): R3 roof -> deck 2 heading north, R2 deck 2 -> deck 1 heading south, R1 deck 1 -> ground north
    # (shallow steps, 0.25 m: the walk checks see a slope as its box, so a sloped ramp would read as a wall)
    k.stairs("asphalt", WL[0], RZa, WL[1], RZb, LV[3], "z", 1, base=LV[2], rise=0.25, floor=LV[2] - 0.5)
    k.stairs("asphalt", EL[0], RZa, EL[1], RZb, LV[2], "z", -1, base=LV[1], rise=0.25, floor=LV[1] - 0.5)
    k.stairs("asphalt", WL[0], RZa, WL[1], RZb, LV[1], "z", 1, base=LV[0], rise=0.25, floor=LV[0] - 1)
    # kerb walls along each ramp's side toward the middle lane (nobody steps off a ramp halfway down)
    k.box("precastconcrete", WL[1], LV[2], RZa, WL[1] + 0.4, LV[3] + 1.1, RZb)
    k.box("precastconcrete", EL[0] - 0.4, LV[1], RZa, EL[0], LV[2], RZb)
    k.box("precastconcrete", WL[1], LV[0], RZa, WL[1] + 0.4, LV[1], RZb)
    # the shaft's casing through the decks (below the roof: a square column)
    k.box("precastconcrete", stack[0], 0, stack[1], stack[2], 20, stack[1] + 1); k.box("precastconcrete", stack[0], 0, stack[3] - 1, stack[2], 20, stack[3])
    k.box("precastconcrete", stack[0], 0, stack[1], stack[0] + 1, 20, stack[3]); k.box("precastconcrete", stack[2] - 1, 0, stack[1], stack[2], 20, stack[3])
    # the facade: a parapet, a 0.7 m slot (nobody fits), a band up to the next deck; on the roof a parapet and clip
    for lv in range(3):
        y = LV[lv]; top = LV[lv + 1] - 0.5
        for (x0, z0, x1, z1) in ((PX0 - 1, PZ0 - 1, PX1 + 1, PZ0), (PX0 - 1, PZ1, PX1 + 1, PZ1 + 1), (PX0 - 1, PZ0, PX0, PZ1), (PX1, PZ0, PX1 + 1, PZ1)):
            k.box("precastconcrete", x0, y, z0, x1, y + 1.1, z1); k.box("precastconcrete", x0, y + 1.8, z0, x1, top, z1)
            n = 0
    for (x0, z0, x1, z1) in ((PX0 - 1, PZ0 - 1, PX1 + 1, PZ0), (PX0 - 1, PZ1, PX1 + 1, PZ1 + 1), (PX0 - 1, PZ0, PX0, PZ1), (PX1, PZ0, PX1 + 1, PZ1)):
        k.box("precastconcrete", x0, 20, z0, x1, 21.2, z1); k.box("clip", x0, 21.2, z0, x1, 120, z1)
    k.box("clip", PX0 - 1, 120, PZ0 - 1, PX1 + 1, 121, PZ1 + 1)
    cp(EL[0], LV[1], SZc - 14, EL[1], LV[2] - 0.5, SZc + 7)              # CP13: off ramp 2 (deck 1's south bay)
    cp(PX0, 0, PZ0, PX1, LV[1] - 0.5, PZ0 + 6)                            # CP14: the finish, the ground floor's north bay

    # ================================================================== the globe (beside the S's last bends), the campus look
    for i in range(10):
        y0 = i * 2.0; y1 = y0 + 2.0
        r = 20 * math.cos(math.asin(min(1.0, (y0 + 1.0) / 20.5)))
        k.disc("logcabin" if i % 2 == 0 else "wideplanks", -145, 40, r, y0, y1, n=12)   # (beside the last bends: in view)

    # ================================================================== dressing (walls, ceilings, edges: the lanes stay clear)
    # campus: road markings (flush), crossings, lamp posts and trees on the lawns' far edges, the office blocks' windows
    for (x0, z0, x1, z1) in ((-340, 28.8, -280, 29.2), (-170, -0.2, -120, 0.2)):
        k.box("roadmarkings", x0, -0.01, z0, x1, 0.005, z1)
    for (x0, z0, x1, z1) in ((-300, 22, -294, 36), (-150, -8, -144, 8)):
        k.box("zebracrossing", x0, -0.01, z0, x1, 0.005, z1)
    k.box("roadmarkings", -337.5, -0.01, 22, -337, 0.005, 36)                      # the start line
    for (x, z) in ((-330, 40.5), (-300, 40.5), (-270, 40.5), (-170, 12.5), (-140, 12.5), (-170, -12.5), (-140, -12.5)):
        k.box("galvanizedsteel", x - 0.1, 0, z - 0.1, x + 0.1, 6, z + 0.1); k.box("glow", x - 0.3, 6, z - 0.3, x + 0.3, 6.2, z + 0.3)
    for (x, z, sz) in ((-390, -50, 1.4), (-372, -55, 1.1), (-350, -48, 1.3), (-390, 58, 1.2), (-160, 50, 1.4), (-130, 56, 1.2),
                       (-150, -52, 1.3), (-180, -56, 1.1), (-232, -56, 1.2)):
        m.tree(x, z, sz)
    for (x0, z0, x1, z1) in ((-320, -CZ, -280, 18), (-240, -18, -200, CZ)):
        for y in (3, 6.6, 10.2, 13.8):
            k.box("steeldoorskin", x0 - 0.06, y, z0, x0, y + 1.6, z1)
            k.box("steeldoorskin", x1, y, z0, x1 + 0.06, y + 1.6, z1)
        k.box("glow", x0 - 0.1, 17.6, z0, x1 + 0.1, 18, z1)
    # the foyer: glass either side of the entrance, a reception desk and a sign off to the side
    k.box("bluetintedglass", MB0 - 0.2, 1, -20, MB0, 6, -8); k.box("bluetintedglass", MB0 - 0.2, 1, 8, MB0, 6, 20)
    for z in (-20, -14, 8, 14, 20): k.box("galvanizedsteel", MB0 - 0.3, 0, z - 0.15, MB0, 7, z + 0.15)
    k.box("terrazzo", MB0 + 1, -0.01, -39, MB1 - 1, 0.005, 39)
    k.box("polishedgranite", -100, 0, 26, -84, 1.1, 30); k.box("glow", -100, 1.1, 26, -84, 1.15, 30)
    k.box("controlpanel", -98, 4, 38.8, -76, 9, 39)
    # the hall: hazard lines at the stairs, warning signs and a crane rail high on the walls, gauges by the door
    k.box("hazard", 57, -0.01, -6, 60, 0.005, 6)
    for x in range(-50, 100, 30):
        k.box("radiationwarning", x, 6, -HZ + 0.0, x + 2.4, 8.4, -HZ + 0.06); k.box("radiationwarning", x + 12, 6, HZ - 0.06, x + 14.4, 8.4, HZ)
    for z in (-HZ, HZ - 1): k.box("bridgegirder", H0, 13, z, H1, 14, z + 1)
    k.box("bridgegirder", 20, 14, -HZ, 23, 15, HZ)                              # the gantry crane, parked over row 3
    # the control room: consoles along its side walls, screens over the hall windows
    for z0, z1 in ((-11.9, -10.6), (10.6, 11.9)):
        k.box("controlpanel", CR0 + 2, 12, z0, H1 - 1, 13.1, z1)
        k.box("gaugecluster", CR0 + 2, 13.6, z0 if z0 < 0 else z1 - 0.06, H1 - 1, 15.2, (z0 + 0.06) if z0 < 0 else z1)
    k.box("bluetintedglass", CR0 - 0.05, 13.2, -10, CR0, 15.2, -4); k.box("bluetintedglass", CR0 - 0.05, 13.2, 4, CR0, 15.2, 10)
    # the cryo yard: pipe racks over its sides (hook points), pipes on them, caps on the tanks
    for z in (-14, 14):
        for x in range(200, 261, 12):
            k.box("galvanizedsteel", x, 0, z - 0.25, x + 0.5, 9, z + 0.25)
        k.box("bridgegirder", 200, 9, z - 1.5, 260.5, 9.6, z + 1.5)
        k.tube_x("galvanizedsteel", 200, 260.5, 10.2, z - 0.7, 0.55); k.tube_x("copperandpatina", 200, 260.5, 10.2, z + 0.7, 0.45)
    for (tx, tz, r) in ((214, -24, 5), (214, 24, 5), (240, -26, 4), (244, 25, 4), (230, 14, 3)):
        k.disc("galvanizedsteel", tx, tz, r * 0.7, 18, 19.2, n=8)
        k.disc("hazard", tx, tz, r + 0.05, 1.5, 1.9, n=12)
    k.box("hazard", TR0 - 0.4, -0.01, -2.4, TR1, 0.005, -2); k.box("hazard", TR0 - 0.4, -0.01, 2, TR1, 0.005, 2.4)
    # the shaft head: hazard lip, the crane's hook over the shaft
    k.box("hazard", S0 - 0.6, -0.01, -SZ, S0, 0.005, SZ)
    k.box("bridgegirder", S0, 12.5, -1, S1, 13.5, 1)
    k.box("galvanizedsteel", 274.95, 6, -0.05, 275.05, 12.5, 0.05)
    k.box("castiron", 274.4, 5, -0.6, 275.6, 6, 0.6)
    # the cavern: floodlight heads on the walls, a gantry crane overhead, cable trays along the walls, glow rings on the
    # detector's open faces
    for x in (320, 380, 445, 495):
        for z0, z1 in ((-VZ, -VZ + 1.2), (VZ - 1.2, VZ)):
            k.box("powdercoatyellow", x - 1.2, VTOP - 7, z0, x + 1.2, VTOP - 5.2, z1)   # (yellow work floods)
            k.box("glow", x - 1.0, VTOP - 7.1, z0 + 0.1, x + 1.0, VTOP - 7, z1 - 0.1)
    k.box("bridgegirder", 470, VTOP - 4, -VZ, 474, VTOP - 2, VZ)
    # hazard banding: flush along the cavern's walls at the floor, and round the service pit's edges
    for (z0, z1) in ((-VZ, -VZ + 0.03), (VZ - 0.03, VZ)):
        k.box("hazard", V0, CAV_Y, z0, V1, CAV_Y + 1.2, z1)
    k.box("hazard", PIT0 - 0.6, CAV_Y - 0.01, -VZ, PIT0, CAV_Y + 0.005, VZ)
    for z0, z1 in ((-VZ, -VZ + 1.0), (VZ - 1.0, VZ)):
        k.box("galvanizedgrating", V0, CAV_Y + 9, z0, V1, CAV_Y + 9.3, z1)
    for xf in (A1, B0d):
        barrel("glow", xf - 0.05, xf + 0.05, DY, 0, 11.4, 11.7)
    for xb in (A0 + 8, A0 + 26, B0d + 8, B0d + 26):
        barrel("tankarmour", xb, xb + 1.2, DY, 0, 11.4, 11.9)                    # the yoke bands
    # the measuring hall (q: "more decor, especially for the measuring room"); all of it flush on the walls or high up,
    # nothing on the floor of the line: the detector's end-cap rings on both shielding walls (open at the bottom, clear
    # of the ways through), flanges on the beam pipe, readout screens and instrument racks high on the side walls,
    # vertical cable drops, accent strips under the ceiling, and catwalks with railings at the hall's two ends (they
    # bridge nothing: the west one ends at the shielding wall, the east one starts past half B)
    for (xa, xb) in ((A0, A0 + 0.15), (B1d + 1.85, B1d + 2)):
        barrel("bluedsteel", xa, xb, DY, 0, 12.4, 13.4, skip=(7, 8, 9))
        barrel("hammeredcopper", xa, xb, DY, 0, 13.4, 13.9, skip=(7, 8, 9))
        barrel("glow", xa, xb + (0.03 if xa == A0 else 0), DY, 0, 12.2, 12.4, skip=(7, 8, 9))
    for fx in range(A0 + 4, B1d, 8):
        k.tube_x("electroplatednickel", fx, fx + 0.3, DY, 0, 0.95)
    RY0 = CAV_Y + 10.5                     # the high instrument band: 10.5-14 m up the side walls
    for x in range(306, V1 - 6, 14):
        if 326 <= x <= 336 or 428 <= x <= 438: continue    # (not on the shielding walls)
        for (z0, z1, zs) in ((-VZ, -VZ + 0.5, -VZ + 0.5), (VZ - 0.5, VZ, VZ - 0.55)):
            k.box("electricalcabinet", x, RY0, z0, x + 4, RY0 + 2.4, z1)                 # a rack of instruments
            k.box("controlpanel", x + 5, RY0 + 0.4, z0 if z0 < 0 else z1 - 0.12, x + 9, RY0 + 2.6, (z0 + 0.12) if z0 < 0 else z1)
            k.box("neonmaze", x + 5.3, RY0 + 0.7, zs if z0 < 0 else zs - 0.01, x + 8.7, RY0 + 2.3, (zs + 0.01) if z0 < 0 else zs + 0.05)   # the readout
            k.box("galvanizedgrating", x + 10.2, CAV_Y + 9.3, z0 if z0 < 0 else z1 - 0.3, x + 10.8, VTOP, (z0 + 0.3) if z0 < 0 else z1)   # a cable drop
    for (z0, z1) in ((-VZ, -VZ + 0.05), (VZ - 0.05, VZ)):
        k.box("glow", V0, VTOP - 1.2, z0, V1, VTOP - 0.9, z1)                              # the accent strip
        k.box("radiationwarning", 345, CAV_Y + 2.5, z0, 349, CAV_Y + 5.5, z1)
        k.box("radiationwarning", 470, CAV_Y + 2.5, z0, 474, CAV_Y + 5.5, z1)
    CWY = CAV_Y + 16                       # the catwalks (16 m up)
    for (x0, x1) in ((V0 + 0.5, 327.5), (B1d + 6, V1 - 0.5)):
        for (z0, z1, rz) in ((-VZ, -VZ + 2, -VZ + 2), (VZ - 2, VZ, VZ - 2.06)):
            k.box("steeldeckplate", x0, CWY - 0.2, z0, x1, CWY, z1)
            k.box("galvanizedsteel", x0, CWY + 1.0, rz, x1, CWY + 1.1, rz + 0.06)       # the rails
            k.box("galvanizedsteel", x0, CWY + 0.5, rz, x1, CWY + 0.56, rz + 0.06)
            for x in [x0 + 0.1 + 3.0 * i for i in range(int((x1 - x0) // 3.0) + 1)]:
                if x + 0.08 <= x1: k.box("galvanizedsteel", x, CWY, rz, x + 0.08, CWY + 1.0, rz + 0.06)
            k.box("powdercoatyellow", x0, CWY - 0.2, rz - 0.02 if z0 < 0 else rz, x1, CWY - 0.05, rz + 0.04 if z0 < 0 else rz + 0.06)
            for x in range(int(x0) + 2, int(x1) - 3, 9):
                k.box("electricalcabinet", x, CWY, z0 if z0 < 0 else z1 - 0.7, x + 1.4, CWY + 2.2, (z0 + 0.7) if z0 < 0 else z1)
    # the ring tunnel: a pipe run and a cable bundle along the outer wall's top, a light strip down the ceiling,
    # radiation signs and a hazard stripe at the outer wall's foot (flush), magnet tops on the inner wall (high)
    for i in range(len(heads)):
        band(i, W - 0.9, W - 0.35, TY1 - 0.75, TY1 - 0.25, "copperandpatina")
        band(i, W - 0.35, W, TY1 - 0.6, TY1 - 0.15, "galvanizedsteel")
        band(i, -0.25, 0.25, TY1 - 0.04, TY1, "glow")
        band(i, W - 0.03, W, TY0, TY0 + 0.45, "hazard")
        if 0 < i < len(heads) - 1:
            band(i, -W, -W + 0.45, TY1 - 1.3, TY1 - 0.5, "bluedsteel")
            if i % 2 == 1:
                across(i, 0.5, W - 0.03, W, TY0 + 1.7, TY0 + 2.9, "radiationwarning", depth=1.2)
    # the vent: glowing fan-housing bands every 30 m on its walls
    for y in (-60, -30, 0):
        k.box("glow", SX + A - 0.05, y, SZc - A + 4.5, SX + A, y + 0.4, SZc + A)
        k.box("glow", SX - A, y, SZc - A + 4.5, SX - A + 0.05, y + 0.4, SZc + A)
        k.box("glow", SX - A, y, SZc + A - 0.05, SX + A, y + 0.4, SZc + A)
    # the car park: bay lines in the middle lane, parked cars in the bays, columns between the lanes, the finish line
    MIDL = (SX + 4.4, SX + 15.6)
    for lv in range(3):
        y = LV[lv]
        k.box("parkingbaylines", MIDL[0], y - 0.01, RZa, MIDL[1], y + 0.005, RZb)
        for z in range(int(RZa) + 4, int(RZb) - 2, 8):
            k.box("precastconcrete", MIDL[0] - 0.2, y, z, MIDL[0] + 0.4, LV[lv + 1] - 0.5, z + 0.6)
            k.box("precastconcrete", MIDL[1] - 0.4, y, z, MIDL[1] + 0.2, LV[lv + 1] - 0.5, z + 0.6)
        for j, z in enumerate((RZa + 2, RZa + 10, RZa + 18, RZa + 26)):
            if (j + lv) % 3 == 2: continue
            for (cx0, cx1) in ((MIDL[0] + 0.8, MIDL[0] + 5.2), (MIDL[1] - 5.2, MIDL[1] - 0.8)):
                if (j * 7 + lv * 3 + int(cx0)) % 4 == 0: continue
                k.box("aircraftskin" if (j + lv) % 2 else "powdercoatyellow", cx0, y + 0.3, z, cx1, y + 1.2, z + 1.9)
                k.box("bluetintedglass", cx0 + 1.1, y + 1.2, z + 0.15, cx1 - 1.0, y + 1.75, z + 1.75)
                k.box("rubber", cx0 + 0.6, y, z - 0.05, cx0 + 1.3, y + 0.6, z + 1.95); k.box("rubber", cx1 - 1.3, y, z - 0.05, cx1 - 0.6, y + 0.6, z + 1.95)
    k.box("checkerboardtile", PX0, -0.01, PZ0 + 2.5, PX1, 0.005, PZ0 + 3.5)
    k.box("glow", PX0, 5.2, PZ0 + 2.8, PX1, 5.6, PZ0 + 3.2)
    for (x0, x1) in ((PX0, PX0 + 0.3), (PX1 - 0.3, PX1)):
        k.box("glow", x0, 0, PZ0 + 2.8, x1, 5.6, PZ0 + 3.2)
    # the world round it: the ground (unlit, open over the shafts)
    for (a, b, c, d) in minus((-480, -420, 920, 140), [(SH0, -SHZ, SH1, SHS), (SX - A - 1, SZc - A - 1, SX + A + 1, SZc + A + 1)]):
        k.box("far", a, -1.6, b, c, -1.1, d)

    # ================================================================== lights (the bake): cold white inside, yellow
    # floods in the cavern, a lamp every 25 m in the tunnel, the vent dark but for its fan housings
    for x in (-100, -80): k.light(x, 11, -20, 22, 0.9); k.light(x, 11, 20, 22, 0.9)
    for x in range(-60, 111, 20):
        for z in (-12, 12): k.light(x, 13.5, z, 20, 0.9)
    k.light(101, 15.2, 0, 12, 1.0)
    for x in (125, 145, 165): k.light(x, 15.2, 0, 12, 0.8)
    k.light(277, 12, 0, 24, 0.9); k.light(275, 10, 16, 12, 0.8)
    for y in (-20, -52, -86): k.light(275, y, 0, 26, 0.8)
    for f in range(0, NF, 2): k.light(275, -f * dy - 2, 16, 14, 0.7)
    k.light(293, CAV_Y + 4, 0, 10, 0.8)
    for x in (320, 380, 445, 495):
        for z in (-22, 22): k.light(x, VTOP - 6, z, 45, 1.2)
    k.light(380, CAV_Y + 2, 0, 16, 0.9)
    for i in range(len(heads)):
        n = max(1, int(lens[i] // 25))
        for j in range(n):
            f = (j + 0.5) / n
            x, z = at(i, f, 0)
            k.light(x, TY1 - 0.4, z, 13, 0.9)
            (ax, az), (bx, bz) = pts[i], pts[i + 1]
            k.box("lightblock", x - 0.4, TY1 - 0.08, z - 0.4, x + 0.4, TY1, z + 0.4)
    k.light(EX, CAV_Y + 8, EZ - 8, 16, 0.9)
    for y in (-70, -40, -10, 15): k.light(SX, y, SZc + 3, 14, 0.6)
    for lv in range(3):
        for x in (SX - 2, SX + 22):
            for z in (SZc - 48, SZc - 28, SZc - 8):
                k.light(x, LV[lv + 1] - 1.2, z, 16, 0.8)

    for c in cps:
        m.raw("checkpoint " + " ".join(num(v) for v in c))
    m.raw("setting particle physics lab")
    m.raw("racedefault 225")   # the ranked race total counts this for anyone with no run here: PLACEHOLDER (3x a good run, about 75 s) until q sets it
    # bomb (q, 2026-10-30: "playable in bomb too"): the cryogenics yard is the arena. Attackers climb out of the shaft
    # head's stairwell (one flight) and in at its west door; defenders come along the skybridge onto the cold box and
    # down its stairs. The two sites are the yard's west corners (north and south of the cold box),
    # each beside a tank: attackers reach the north one in 99 m and the south one in 118 m (4.1 / 4.9 s at 24 m/s),
    # defenders either in 109 m (maptool metrics). Defenders any further back lost about two rounds in three to 8
    # expert bots (seeds 1-6); from here the rounds split 43-38 with plants on both sites. The race spawns and checkpoints are untouched; nothing was built for it but the sites' flush markings.
    m.also = ["bomb"]
    LAND = -CAV_Y / 13               # (the stairwell's first landing, 6.9 m down: the attackers climb one flight out)
    BA = [(283.8, 14.2), (285.2, 15.9), (283.8, 17.6), (285.2, 18.6), (285.2, 13.6)]
    BD = [(150, -1.5), (150, 1.5), (153, -1.5), (153, 1.5), (156, 0)]
    for (x, z) in BA: m.raw(f"spawn bomb-a {x:g} {-LAND:.3f} {z:g} 90")
    for (x, z) in BD: m.raw(f"spawn bomb-b {x} 12 {z} -90")
    SITES = [(198, 0, 20, 208, 3, 31), (198, 0, -31, 208, 3, -20)]
    for st in SITES:
        m.raw("site " + " ".join(f"{v:g}" for v in st))
        for (z0, z1) in ((st[2], st[2] + 0.4), (st[5] - 0.4, st[5])):
            k.box("hazard", st[0], -0.01, z0, st[3], 0.004, z1)
    m.top_up_spawns = lambda offered: None   # (race and bomb: their own spawns; no free for all pool)
    m.save("accelerator", dress=False)
    import os
    from . import prefabs
    path = os.path.join(prefabs.OUT, "accelerator.qmap")
    text = open(path).read().replace("modes race bomb ffa\n", "modes race bomb\n")
    open(path, "w", newline="\n").write(text)
