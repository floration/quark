"""MIDTOWN: multi point. A city centre the size of four hamlets: three avenues each way, sixteen blocks of
two-storey offices you can climb through to the roofs, an elevated railway on its own deck across the middle,
a park with a fountain, the city hall on its plaza, and traffic everywhere. The attack runs from the north edge
to city hall: A the intersection, B an office roof, C the park, D the railway station, E city hall plaza."""
from .prefabs import *


def build():
    m = M("Midtown", "points", sun=(0.4, 1.0, 0.25))
    MHX = MHZ = 112; MH = 30
    POND = (14, 36, 26, 48)                                # the park pond (fall in and you drown)
    m.room(MHX, MHZ, MH, pits=[POND + ("water",)], style="yard", skyline="city", tile=16)
    AVES = (-64, 0, 64)                                  # avenue centre lines (16 m wide, both ways)
    BLOCKS = [(-110, -72), (-56, -8), (8, 56), (72, 110)]
    PARK = (8, 8, 56, 56)
    HALL_BLOCK = (-56, 8, -8, 56)
    def road_lines():   # lane markings and crossings (trim, flush) and kerbs round every block
        for c in AVES:
            for (a0, a1) in ((-MHX, -72), (-56, -8), (8, 56), (72, MHX)):
                for k in range(int(a0) + 2, int(a1) - 2, 6):
                    m.box(k, 0.0, c - 0.12, k + 3, 0.02, c + 0.12, mat="trim"); m.box(c - 0.12, 0.0, k, c + 0.12, 0.02, k + 3, mat="trim")
            for c2 in AVES:   # zebra crossings on the four arms of each intersection
                for j in range(-6, 7, 2):
                    for (dx, dz) in ((9.5, 0), (-9.5, 0)): m.box(c + dx - 1, 0.0, c2 + j - 0.4, c + dx + 1, 0.02, c2 + j + 0.4, mat="trim")
                    for (dx, dz) in ((0, 9.5), (0, -9.5)): m.box(c + j - 0.4, 0.0, c2 + dz - 1, c + j + 0.4, 0.02, c2 + dz + 1, mat="trim")
        for (x0, x1) in BLOCKS:
            for (z0, z1) in BLOCKS:
                m.box(x0, 0, z0, x1, 0.15, z1, accent=True)                       # the pavement (a kerb all round)
    road_lines()

    def office(x0, z0, x1, z1, doors, roof_holes=()):
        """a two-storey office: doors on the ground floor, stairs up the west wall to the first floor and up the east
        wall to the flat roof (a parapet round it, gaps where skybridges land), windows on the long sides"""
        m.building(x0, z0, x1, z1, 6, doors, roof=False, windows=True)
        # each doorway kept clear 1.5 m in and out (nothing dressed in front of it), and the facade bands cut round it
        dgaps = []
        for (sd, c, w) in doors:
            if sd == "n": g = (c - w / 2, z0 - 1.5, c + w / 2, z0 + 1.5)
            elif sd == "s": g = (c - w / 2, z1 - 1.5, c + w / 2, z1 + 1.5)
            elif sd == "w": g = (x0 - 1.5, c - w / 2, x0 + 1.5, c + w / 2)
            else: g = (x1 - 1.5, c - w / 2, x1 + 1.5, c + w / 2)
            m.keep_clear(*g, y=2.6); dgaps.append(g)
        for (fy, n) in ((4.6, 3), (10.6, 3)):                                  # ceiling lights inside, both floors
            for k in range(n):
                m.light((x0 + x1) / 2, fy, z0 + (z1 - z0) * (k + 0.5) / n, 12, 1.25)
        L = (x0 + 0.4, z0 + 2, x0 + 2.6, z0 + 14); U = (x1 - 2.6, z1 - 14, x1 - 0.4, z1 - 2)
        for r in minus((x0 + 0.4, z0 + 0.4, x1 - 0.4, z1 - 0.4), [L]): m.box(r[0], 5.6, r[1], r[2], 6, r[3])
        m.stairs(*L, 6, "z", 1, base=0.15)   # (from the pavement the offices stand on)
        fx = x0 + 3.5                                         # (the fire escape's x: the roof edge is notched over it)
        FE = (fx - 0.75, z1 - 0.05, fx + 0.75, z1 + 0.45)
        m.building(x0, z0, x1, z1, 6, [], roof=True, windows=True, base=6, notch=[U, FE])
        m.stairs(*U, 12, "z", -1, base=6, solid=False)
        # the fire escape: a ladder up the south face to the roof (a gap in the parapet over it)
        m.ladder(fx, z1 + 0.12, 0.15, 12.35, "n", width=0.8)
        gaps = [U, FE] + list(roof_holes)
        # facade: floor-line bands and pilasters every 3 m (the building reads as storeys from the street)
        for y in (1.0, 5.9, 7.0, 11.9):   # (the bands below the door lintels stop at the doorways)
            for r in minus((x0 - 0.06, z0 - 0.06, x1 + 0.06, z1 + 0.06), [(x0, z0, x1, z1)] + (dgaps if y < 2.6 else [])):
                m.box(r[0], y, r[1], r[2], y + 0.12, r[3], mat="trim")
        inDoor = lambda a, b, c, d: any(a < g[2] and c > g[0] and b < g[3] and d > g[1] for g in dgaps)
        for k in range(1, int((x1 - x0) / 3)):
            x = x0 + k * 3
            for (za, zb) in ((z0 - 0.08, z0), (z1, z1 + 0.08)):
                if not inDoor(x - 0.12, za, x + 0.12, zb): m.box(x - 0.12, 0.15, za, x + 0.12, 12, zb, mat="trim")
        for k in range(1, int((z1 - z0) / 3)):
            z = z0 + k * 3
            for (xa, xb) in ((x0 - 0.08, x0), (x1, x1 + 0.08)):
                if not inDoor(xa, z - 0.12, xb, z + 0.12): m.box(xa, 0.15, z - 0.12, xb, 12, z + 0.12, mat="trim")
        for (a0, b0, a1, b1) in ((x0 - 0.3, z0 - 0.3, x1 + 0.3, z0), (x0 - 0.3, z1, x1 + 0.3, z1 + 0.3), (x0 - 0.3, z0, x0, z1), (x1, z0, x1 + 0.3, z1)):
            for r in minus((a0, b0, a1, b1), gaps): m.box(r[0], 12.35, r[1], r[2], 12.95, r[3], accent=True)
        R = random.Random(int(x0 * 13 + z0))
        # the offices: rows of desks with cubicle screens on both floors (cover indoors), clear of the stairs and doors
        for (fy, sx0, sx1) in ((0.15, x0 + 4.0, x1 - 3.0), (6.0, x0 + 3.0, x1 - 4.0)):
            for zz in range(int(z0 + 5), int(z1 - 4), 5):
                for xx in range(int(sx0), int(sx1 - 2), 5):
                    if abs(zz - (z0 + z1) / 2) < 3: continue                  # (the aisle between the side doors)
                    m.box(xx, fy, zz, xx + 2.2, fy + 0.8, zz + 1.0, accent=True)                 # the desk
                    m.box(xx, fy, zz + 1.0, xx + 2.2, fy + 1.45, zz + 1.12, mat="metal")         # its screen
        # on the roof: a billboard on legs down the middle and a water tank (they break the lines from roof to roof)
        bx0 = x0 + 5; bz = (z0 + z1) / 2 - 2
        for xx in (bx0, bx0 + 7.6): m.metal(xx, 12.35, bz - 0.2, xx + 0.4, 16, bz + 0.2)
        m.box(bx0 - 0.5, 14, bz - 0.15, bx0 + 8.5, 18, bz + 0.15, accent=True); m.glow(bx0 - 0.5, 13.85, bz - 0.25, bx0 + 8.5, 14, bz + 0.25)
        tx, tz = x0 + 5, z1 - 6
        for (dx, dz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)): m.metal(tx + dx * 1.1 - 0.15, 12.35, tz + dz * 1.1 - 0.15, tx + dx * 1.1 + 0.15, 14.5, tz + dz * 1.1 + 0.15)
        m.box(tx - 1.6, 14.5, tz - 1.6, tx + 1.6, 18, tz + 1.6); m.box(tx - 1.8, 18, tz - 1.8, tx + 1.8, 18.4, tz + 1.8, accent=True)
        for k in range(7):                                                    # roof plant: air handlers (cover up there)
            ax, az = R.uniform(x0 + 4, x1 - 6), R.uniform(z0 + 3, z1 - 16)
            m.metal(ax - 1.2, 12.35, az - 0.9, ax + 1.2, 13.7, az + 0.9); m.glow(ax - 1.25, 13.0, az - 0.95, ax + 1.25, 13.1, az + 0.95)
    OFFICES = []
    for (bx0, bx1) in BLOCKS:
        for (bz0, bz1) in BLOCKS:
            if (bx0, bz0, bx1, bz1) in (PARK, HALL_BLOCK): continue
            w = bx1 - bx0
            if w < 44: OFFICES.append((bx0 + 2, bz0 + 2, bx0 + 20, bz0 + 28))
            else: OFFICES += [(bx0 + 2, bz0 + 2, bx0 + 20, bz0 + 28), (bx1 - 20, bz1 - 28, bx1 - 2, bz1 - 2)]
    for (x0, z0, x1, z1) in OFFICES:
        office(x0, z0, x1, z1, [("n", (x0 + x1) / 2, 2.4), ("s", (x0 + x1) / 2, 2.4), ("w", (z0 + z1) / 2 + 3, 2.4), ("e", (z0 + z1) / 2 - 3, 2.4)])

    # ---- the elevated railway: a deck at 9 m down the middle avenue, on columns, with two stations and their stairs
    for r in tiles([(-MHX, -3, MHX, 3)], 16): m.box(r[0], 8.6, r[1], r[2], 9, r[3], mat="metal")
    for x in range(-104, MHX, 16):
        m.box(x - 0.5, 0, -0.5, x + 0.5, 8.6, 0.5)
    m.box(-MHX, 9, -3.15, MHX, 9.9, -3, accent=True); m.box(-MHX, 9, 3, MHX, 9.9, 3.15, accent=True)   # the deck's railings
    STATIONS = [(-40, -24), (24, 40)]
    for (s0, s1) in STATIONS:
        for r in tiles([(s0, -6, s1, -3), (s0, 3, s1, 6)], 16): m.box(r[0], 8.6, r[1], r[2], 9, r[3])   # platforms
        m.box(s0, 13, -6.3, s1, 13.3, 6.3, accent=True)                       # the canopy
        for x in (s0 + 0.5, s1 - 0.8):
            for z in (-5.8, 5.5): m.metal(x, 9, z, x + 0.3, 13, z + 0.3)
        m.light((s0 + s1) / 2, 12, 0, 12, 0.8)
    m.stairs(-24, 3, -6, 6, 9, "x", -1); m.stairs(6, -6, 24, -3, 9, "x", 1)   # (up from the avenue onto each platform)
    for (x, n) in ((-104, 2), (60, 2), (-4, 2)):                               # stopped trains: carriages on the deck
        for k in range(n): m.metal(x + k * 13, 9, -2.4, x + k * 13 + 12, 12.2, 2.4); m.glow(x + k * 13, 10.6, -2.45, x + k * 13 + 12, 11.4, 2.45)

    # ---- the park (C): lawns, trees, hedges, the fountain and a pond (cover everywhere, sightlines broken)
    px0, pz0, px1, pz1 = PARK
    m.box(px0, 0.15, pz0, px1, 0.2, pz1, accent=True)
    for (x, z) in ((44, 14), (50, 26), (36, 44), (32, 50), (48, 48), (40, 30)): m.tree(x, z, 1.0)
    m.box(POND[0] - 0.4, -0.05, POND[1] - 0.4, POND[2] + 0.4, 0.45, POND[1], accent=True); m.box(POND[0] - 0.4, -0.05, POND[3], POND[2] + 0.4, 0.45, POND[3] + 0.4, accent=True)
    m.box(POND[0] - 0.4, -0.05, POND[1], POND[0], 0.45, POND[3], accent=True); m.box(POND[2], -0.05, POND[1], POND[2] + 0.4, 0.45, POND[3], accent=True)   # the pond's kerb
    for (x0, z0, x1, z1) in ((30, 12, 31, 24), (12, 32, 24, 33), (34, 34, 46, 35), (52, 34, 53, 46)): m.box(x0, 0.2, z0, x1, 1.3, z1, accent=True)   # hedges
    m.box(40, 0.2, 18, 46, 0.9, 24); m.glow(40.6, 0.9, 18.6, 45.4, 1.0, 23.4); m.box(42.4, 0.9, 20.4, 43.6, 2.6, 21.6, mat="metal")  # the fountain
    m.light(43, 2, 21, 9, 0.6, fixture=False)
    for (x, z) in ((20, 40), (44, 40), (14, 22)): m.box(x - 1.5, 0.2, z - 0.4, x + 1.5, 0.7, z + 0.4)   # benches
    C_Z = (13, 13, 27, 27); m.dais("C", C_Z, y=0.2)
    m.ring_cover(20, 20, 0.7, seed=31)
    # ---- city hall (E) on its plaza
    m.building(-54, 40, -12, 55, 8, [("n", -33, 4.0), ("w", 47, 2.4), ("e", 47, 2.4)], windows=True)
    for x in range(-52, -12, 5): m.box(x, 0, 38.6, x + 1.2, 8, 39.8)                                # the colonnade
    m.box(-55, 8, 38, -11, 8.6, 56, accent=True); m.box(-40, 8.6, 42, -26, 11, 52, accent=True)    # roof, the clock tower base
    m.box(-36, 11, 45, -30, 17, 49); m.glow(-35.6, 14, 44.9, -30.4, 16, 45)                          # the clock tower
    E_Z = (-40, 18, -26, 32); m.dais("E", E_Z, y=0.15)
    m.ring_cover(-33, 25, 0.65, seed=32)
    for (x, z) in ((-50, 14), (-16, 14), (-50, 30), (-16, 30)): m.box(x - 1.2, 0.15, z - 1.2, x + 1.2, 1.4, z + 1.2, accent=True); m.light(x, 3, z, 7, 0.5)   # planters
    # ---- A at the north intersection, B on an office roof, D on the west station's platform
    A_Z = (-7, -71, 7, -57); m.dais("A", A_Z)
    m.ring_cover(0, -64, 0.5, seed=33)
    B_OFF = (10, -54, 28, -28)
    B_Z = (12, -52, 22, -42); m.dais("B", B_Z, y=12.35)
    m.ring_cover(17, -47, 12.85, r=3.0, n=5, seed=34)
    D_Z = (-38, -5, -26, 5); m.dais("D", D_Z, y=9)
    m.ring_cover(-32, 0, 9.5, r=3.2, n=5, seed=35)

    # ---- traffic: cars and buses in the lanes (cover on the streets), street lamps and traffic lights
    def car(x, z, along, R):
        hx, hz = (2.2, 0.95) if along == "x" else (0.95, 2.2)
        col = R.random() < 0.5
        m.box(x - hx, 0.35, z - hz, x + hx, 1.15, z + hz, accent=col, mat=None if col else "metal")
        cx, cz = (hx * 0.55, hz) if along == "x" else (hx, hz * 0.55)
        m.box(x - cx, 1.15, z - cz + 0.05, x + cx, 1.75, z + cz - 0.05) if along == "x" else m.box(x - cx + 0.05, 1.15, z - cz, x + cx - 0.05, 1.75, z + cz)
        for (wx, wz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            m.metal(x + wx * (hx - 0.6) - 0.3, 0, z + wz * (hz - 0.05) - 0.06, x + wx * (hx - 0.6) + 0.3, 0.6, z + wz * (hz - 0.05) + 0.06)
        m.glow(x - hx - 0.02, 0.8, z - hz + 0.2, x - hx, 0.95, z + hz - 0.2) if along == "x" else m.glow(x - hx + 0.2, 0.8, z - hz - 0.02, x + hx - 0.2, 0.95, z - hz)
    def bus(x, z, along):
        hx, hz = (5.5, 1.3) if along == "x" else (1.3, 5.5)
        m.metal(x - hx, 0.4, z - hz, x + hx, 3.2, z + hz)
        m.glow(x - hx - 0.02, 1.8, z - hz - 0.02, x + hx + 0.02, 2.5, z + hz + 0.02)
        m.box(x - hx + 0.1, 3.2, z - hz + 0.1, x + hx - 0.1, 3.4, z + hz - 0.1, accent=True)
    RC = random.Random("midtown-traffic")
    for c in AVES:
        for t in range(-104, 105, 9):
            if any(abs(t - c2) < 12 for c2 in AVES): continue                # (intersections clear)
            lane = c + RC.choice((-4.5, 4.5))
            if RC.random() < 0.8:
                if RC.random() < 0.15: bus(t, lane, "x") if abs(lane) > 3.5 or True else None
                else: car(t, lane, "x", RC)
            lane = c + RC.choice((-4.5, 4.5))
            if RC.random() < 0.8 and not (c == 0 and abs(lane) < 4):
                car(lane, t, "z", RC)
    for c in AVES:
        for t in range(-100, 101, 24):
            for sd in (-1, 1):
                x, z = t, c + sd * 7.4
                if any(abs(t - c2) < 10 for c2 in AVES): continue
                m.metal(x - 0.1, 0.15, z - 0.1, x + 0.1, 6, z + 0.1); m.box(x - 0.6, 6, z - 0.15, x + 0.1, 6.15, z + 0.15, mat="metal")
                m.glow(x - 0.6, 5.9, z - 0.12, x - 0.2, 6.0, z + 0.12); m.light(x - 0.4, 5.5, z, 12, 0.6, fixture=False)
    for c in AVES:
        for c2 in AVES:
            for (dx, dz) in ((8.5, 8.5), (-8.5, -8.5)):
                x, z = c + dx, c2 + dz
                m.metal(x - 0.1, 0.15, z - 0.1, x + 0.1, 4.2, z + 0.1); m.box(x - 0.2, 3.2, z - 0.2, x + 0.2, 4.2, z + 0.2, accent=True); m.glow(x - 0.22, 3.9, z - 0.1, x - 0.2, 4.1, z + 0.1)
    # trees down the medians of the avenues (shade, and they break the long lines down the streets)
    for c in (-64, 64):
        for t in range(-100, 101, 16):
            if any(abs(t - c2) < 12 for c2 in AVES): continue
            if (c, t) in ((-64, -100), (64, -52), (64, -36), (64, 28)): continue      # (the spawn lines down the middle)
            m.tree(c, t, 0.75); m.tree(t, c, 0.75)
    # street trees on the pavement at the corners of every block
    for (bx0, bx1) in BLOCKS:
        for (bz0, bz1) in BLOCKS:
            if (bx0, bz0, bx1, bz1) == PARK: continue
            for (x, z) in ((bx0 + 1.2, bz0 + 1.2), (bx1 - 1.2, bz1 - 1.2), (bx0 + 1.2, bz1 - 1.2), (bx1 - 1.2, bz0 + 1.2)):
                if any(o[0] - 2 < x < o[2] + 2 and o[1] - 2 < z < o[3] + 2 for o in OFFICES): continue
                m.tree(x, z, 0.7)
    # bus shelters and kiosks on the pavements
    for (x, z) in ((-90, -74.5), (47, -74.5), (-30, 74.5), (90, 9.5), (-90, 57.5)):
        m.box(x - 3, 2.6, z - 1, x + 3, 2.8, z + 1, accent=True); m.metal(x - 3, 0.15, z - 1, x - 2.8, 2.6, z + 1); m.metal(x + 2.8, 0.15, z - 1, x + 3, 2.6, z + 1)
        m.wallbang(x - 2.8, 0.15, z + 0.8 if z > 0 else z - 1, x + 2.8, 2.4, z + 1 if z > 0 else z - 0.8)   # (glass: shoot through it)

    # ---- spawns per stage, then cover scattered on the streets and in the yards
    MSP = {
        "att0": (0, [(-65, -104), (-63, -104), (-65, -100), (-63, -100), (-65, -96), (-63, -96)], 135),   # (the avenue's centre line: clear of parked cars)
        "def0": (0, [(36, -66), (40, -66), (44, -66), (36, -62), (40, -62), (44, -62)], 90),
        "att1": (0, [(-4, -104), (0, -104), (4, -104), (-4, -98), (0, -98), (-6.5, -98)], 180),
        "def1": (0.15, [(16, -36), (20, -36), (24, -36), (16, -32), (20, -32), (24, -32)], 0),
        "att2": (0, [(61, -50), (65, -50), (63, -54), (65, -46), (63, -42), (65, -42)], 135),
        "def2": (0.2, [(30, 50), (34, 50), (38, 50), (30, 46), (38, 46), (42, 50)], 0),
        "att3": (0, [(63, 28), (65, 28), (61, 40), (65, 40), (63, 36), (65, 36)], 90),
        "def3": (9, [(-60, -1.5), (-56, -1.5), (-52, -1.5), (-60, 1.5), (-56, 1.5), (-52, 1.5)], -90),
        "att4": (0, [(-20, -1.5), (-16, -1.5), (-12, -1.5), (-20, 1.5), (-16, 1.5), (-12, 1.5)], 180),   # (under the railway)
        "def4": (0.15, [(-46, 44), (-42, 44), (-38, 44), (-28, 44), (-24, 44), (-20, 44)], 0),
    }
    for k in "ABCDE": m.raw(m.points[k])
    for k, (y, pts, yaw) in MSP.items():
        for (x, z) in pts: m.spawn(k, x, y, z, yaw)
    CITY = [(1.6, 1.2, 1.2, "box"), (2.4, 1.2, 1.1, "bin"), (4.4, 1.9, 1.6, "box"), (1.2, 1.2, 1.0, "box"), (3.0, 1.0, 1.2, "box")]
    LANES = [(-MHX, c - 1, MHX, c + 1) for c in AVES] + [(c - 1, -MHZ, c + 1, MHZ) for c in AVES]
    m.scatter((-MHX, -MHZ, MHX, MHZ), 0, 4, CITY, "midtown", avoid=LANES + [(-MHX, -6.5, MHX, 6.5)] + [(o[0] - 1.5, o[1] - 1.5, o[2] + 1.5, o[3] + 1.5) for o in OFFICES] + [PARK, (-56, 36, -8, 56)])
    m.raw("setting city centre")
    m.save("midtown")
