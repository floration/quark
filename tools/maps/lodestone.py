"""LODESTONE: multi point. A working mine the size of four hamlets, two storeys deep. The north half is the
surface (14 m, open sky) over the room-and-pillar workings (0 m), with an open pit cut through both; the south
half is two great caverns under the mountain, lit by a sun shaft and fissures, by lamps and by the crystals
the mine is after. The attack goes down the mine, point by point: A the pithead (surface), B the pit floor,
C the workings, D the crystal grotto, E the mother lode. Every point that falls moves both sides' spawns on."""
from .prefabs import *


def build():
    m = M("Lodestone", "points", sun=(0.35, 1.0, 0.45))
    LHX = LHZ = 112; LH = 32; SURF = 14
    LAKE = (-96, 62, -64, 96)
    m.room(LHX, LHZ, LH, pits=[LAKE + ("water",)], style="mine", skyline="mountains", tile=16)

    def lrails(pts, y=0.0):
        """mine rails flush in the floor at height y along axis-aligned legs: two rails, timber sleepers"""
        for (ax, az), (bx, bz) in zip(pts, pts[1:]):
            if ax == bx:
                z0, z1 = min(az, bz), max(az, bz)
                for o in (-0.45, 0.45): m.metal(ax + o - 0.05, y - 0.04, z0, ax + o + 0.05, y, z1)
                z = z0 + 0.4
                while z < z1 - 0.3: m.box(ax - 0.7, y - 0.05, z - 0.1, ax + 0.7, y - 0.01, z + 0.1, accent=True); z += 1.2
            else:
                x0, x1 = min(ax, bx), max(ax, bx)
                for o in (-0.45, 0.45): m.metal(x0, y - 0.04, az + o - 0.05, x1, y, az + o + 0.05)
                x = x0 + 0.4
                while x < x1 - 0.3: m.box(x - 0.1, y - 0.05, az - 0.7, x + 0.1, y - 0.01, az + 0.7, accent=True); x += 1.2

    def cart(x, z, y=0.0, along="x", ore=True):
        """an ore tub on the rails: steel body, a timber rim band, four wheels, a heap of glowing ore"""
        hx, hz = (0.95, 0.6) if along == "x" else (0.6, 0.95)
        for (wx, wz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            m.metal(x + wx * (hx - 0.35) - 0.15, y, z + wz * (hz - 0.05) - 0.05, x + wx * (hx - 0.35) + 0.15, y + 0.3, z + wz * (hz - 0.05) + 0.05)
        m.metal(x - hx, y + 0.25, z - hz, x + hx, y + 1.15, z + hz)
        m.box(x - hx - 0.04, y + 0.95, z - hz - 0.04, x + hx + 0.04, y + 1.05, z + hz + 0.04, accent=True)
        if ore:
            m.box(x - hx + 0.2, y + 1.15, z - hz + 0.15, x + hx - 0.2, y + 1.35, z + hz - 0.15)
            m.glow(x - 0.2, y + 1.35, z - 0.15, x + 0.1, y + 1.5, z + 0.1)

    def crystal(x, z, h, y=0.0, r=0.55, lean=(1, 0), shards=3, seed=0):
        """a crystal: a slanted prism of shrinking glow blocks, with smaller shards round its foot"""
        R = random.Random(seed or int(x * 31 + z * 17 + h))
        n = max(3, int(h / 0.7))
        for i in range(n):
            f = 1 - i / n
            w = r * (0.3 + 0.7 * f)
            cx, cz = x + lean[0] * i * 0.16, z + lean[1] * i * 0.16
            m.glow(cx - w, y + h * i / n, cz - w, cx + w, y + h * (i + 1) / n, cz + w)
        for k in range(shards):
            a = R.uniform(0, 6.283); d = r + R.uniform(0.3, 0.8); sh = h * R.uniform(0.2, 0.45); sw = r * R.uniform(0.25, 0.45)
            sx, sz = x + math.cos(a) * d, z + math.sin(a) * d
            m.glow(sx - sw, y, sz - sw, sx + sw, y + sh * 0.6, sz + sw); m.glow(sx - sw * 0.5, y + sh * 0.6, sz - sw * 0.5, sx + sw * 0.5, y + sh, sz + sw * 0.5)

    def lamp(x, z, y_ceil, drop=1.4, radius=13, intensity=0.85):
        """an overhead lamp hung from the rock on a chain: shade, bulb, its light"""
        yb = y_ceil - drop
        m.metal(x - 0.03, yb + 0.3, z - 0.03, x + 0.03, y_ceil, z + 0.03)
        m.box(x - 0.35, yb + 0.15, z - 0.35, x + 0.35, yb + 0.3, z + 0.35, accent=True)
        m.glow(x - 0.18, yb, z - 0.18, x + 0.18, yb + 0.15, z + 0.18)
        m.light(x, yb - 0.4, z, radius, intensity, fixture=False)

    def timber_set(x0, z0, x1, z1, y0, y1):
        """mine timbering along a rock face: posts every 3 m with a cap beam (decor on the walls, not solid)"""
        if abs(x1 - x0) >= abs(z1 - z0):
            x = x0 + 1.5
            while x < x1 - 0.5: m.box(x - 0.15, y0, z0, x + 0.15, y1, z1, mat="trim"); x += 3
            m.box(x0, y1 - 0.3, z0, x1, y1, z1, mat="trim")
        else:
            z = z0 + 1.5
            while z < z1 - 0.5: m.box(x0, y0, z - 0.15, x1, y1, z + 0.15, mat="trim"); z += 3
            m.box(x0, y1 - 0.3, z0, x1, y1, z1, mat="trim")

    # ---- the boundary rock: strata bands all round, timbering along the workings and the caverns
    for y in range(2, LH, 3):
        m.box(-LHX, y, -LHZ - 0.08, LHX, y + 0.2, -LHZ, mat="trim"); m.box(-LHX, y, LHZ, LHX, y + 0.2, LHZ + 0.08, mat="trim")
        m.box(-LHX - 0.08, y, -LHZ, -LHX, y + 0.2, LHZ, mat="trim"); m.box(LHX, y, -LHZ, LHX + 0.08, y + 0.2, LHZ, mat="trim")
    for (x0, z0, x1, z1) in ((-LHX, -LHZ, LHX, -LHZ + 0.25), (-LHX, LHZ - 0.25, LHX, LHZ)):
        timber_set(x0, z0, x1, z1, 0, 4.5)
    for (x0, z0, x1, z1) in ((-LHX, -LHZ, -LHX + 0.25, LHZ), (LHX - 0.25, -LHZ, LHX, LHZ)):
        timber_set(x0, z0, x1, z1, 0, 4.5)

    # ---- the open pit: three benches down from the surface (14) to the pit floor (0), haul ramps between them,
    # adits (tunnels) out of the pit floor into the workings on three sides
    PIT = (-96, -104, -16, -36)
    B1 = (-90, -98, -22, -42)     # inside the upper bench (9.5)
    B2 = (-84, -92, -28, -48)     # inside the lower bench (5): the pit floor
    ADITS = [(-44, -48, -38, -34), (-28, -74, -14, -68), (-98, -74, -84, -68)]   # south, east, west (through the pit wall)
    R1 = (-74, -96, -62, -92); R4 = (-28, -62, -24, -50); R8 = (-70, -48, -58, -44)   # floor -> lower bench
    R2 = (-50, -102, -38, -98); R5 = (-22, -58, -18, -46)                             # lower -> upper bench
    R3 = (-100, -60, -90, -56); R6 = (-22, -66, -12, -62); R7 = (-62, -42, -58, -32)   # upper bench -> surface (on out
    RIMHOLES = [(-100, -60, -96, -56), (-16, -66, -12, -62), (-62, -36, -58, -32)]      # through the pit wall: holes in the slab)
    def bench(outer, inner, top, slots):
        for r in tiles(minus(outer, [inner] + slots + ADITS), 16): m.box(r[0], 0, r[1], r[2], top, r[3])
        for a in ADITS:                                   # the adit roof (the bench goes on over the tunnel)
            for r in minus(a, [inner]):
                x0, z0 = max(r[0], outer[0]), max(r[1], outer[1]); x1, z1 = min(r[2], outer[2]), min(r[3], outer[3])
                if x1 > x0 and z1 > z0: m.box(x0, 4, z0, x1, top, z1)
    # the pit wall: rock round the pit under the slab (the workings stop here), the adits cut through it
    for r in minus((PIT[0] - 2, PIT[1] - 2, PIT[2] + 2, PIT[3] + 2), [PIT] + ADITS + RIMHOLES): m.box(r[0], 0, r[1], r[2], 13, r[3])
    for a in ADITS:
        for r in minus(a, [PIT]): m.box(r[0], 4, r[1], r[2], 13, r[3])
    bench(PIT, B1, 9.5, [R2, R5] + [(max(r[0], PIT[0]), max(r[1], PIT[1]), min(r[2], PIT[2]), min(r[3], PIT[3])) for r in (R3, R6, R7)])
    bench(B1, B2, 5, [R1, R4, R8])
    m.stairs(*R1, 5, "x", 1); m.stairs(*R4, 5, "z", -1); m.stairs(*R8, 5, "x", -1)
    m.stairs(*R2, 9.5, "x", -1, base=5); m.stairs(*R5, 9.5, "z", -1, base=5)
    m.stairs(*R3, 14, "x", -1, base=9.5); m.stairs(*R6, 14, "x", 1, base=9.5); m.stairs(*R7, 14, "z", 1, base=9.5)
    for (x0, z0, x1, z1) in ADITS:                        # timbered adit mouths, lamp in each
        ax = (x1 - x0) < (z1 - z0)
        for k in (0, 1):
            if ax: m.box(x0 + (0 if k == 0 else x1 - x0 - 0.3), 0, z0, x0 + (0.3 if k == 0 else x1 - x0), 4, z1, mat="trim")
            else: m.box(x0, 0, z0 + (0 if k == 0 else z1 - z0 - 0.3), x1, 4, z0 + (0.3 if k == 0 else z1 - z0), mat="trim")
        m.light((x0 + x1) / 2, 3.2, (z0 + z1) / 2, 8, 0.7)
    # strata on the cut faces: the pit wall above the upper bench, the upper bench's face, the lower bench's face
    for (rect, ys) in ((PIT, (10.5, 12.0)), (B1, (6.3, 8.0)), (B2, (1.5, 3.2))):
        inner = (rect[0] + 0.06, rect[1] + 0.06, rect[2] - 0.06, rect[3] - 0.06)
        for y in ys:
            for r in minus(rect, [inner]): m.box(r[0], y, r[1], r[2], y + 0.15, r[3], mat="trim")
    B_ZONE = (-50, -68, -38, -56)
    m.dais("B", B_ZONE)
    m.ring_cover(-44, -62, 0.5, seed=2)
    for (x, z) in ((-53, -54), (-35, -70), (-53, -71)): cart(x, z, 0, "x")
    lrails([(-84, -71), (-28, -71)]); lrails([(-41, -92), (-41, -48)])
    m.heap(-72, -60, 4, 3, tiers=3); m.heap(-36, -86, 3.5, 2.6, tiers=3); m.heap(-74, -82, 3, 2.2, tiers=2)
    for (x, z) in ((-78, -54), (-34, -52), (-66, -86)): crystal(x, z, 2.2, r=0.4, seed=int(x * z))
    # the pit crane: a lattice mast on the floor with a jib over the benches (grapple anchor, landmark)
    m.metal(-61, 0, -79, -59, 22, -77); m.metal(-61, 22, -79, -36, 23, -77); m.metal(-62, 22, -80, -58, 24, -76, )
    for y in range(2, 22, 4): m.box(-61.3, y, -79.3, -58.7, y + 0.2, -76.7, accent=True)
    m.light(-60, 20.5, -78, 9, 0.6, fixture=False)

    # ---- the surface slab (14): the floor of the north half, the roof of the workings under it
    DECL_E = (40, -24, 64, -20); DECL_W = (-60, -28, -36, -24)
    SHAFT = (10, -66, 14, -62)
    LADW = [(74.6, -88.7, 76, -87.3), (-104.7, -40.7, -103.3, -39.3)]   # holes over the two extra ladders
    for r in tiles(minus((-LHX, -LHZ, LHX, -8), [PIT, DECL_E, DECL_W, SHAFT] + LADW + RIMHOLES), 16): m.box(r[0], 13, r[1], r[2], 14, r[3])
    m.stairs(*DECL_E, 14, "x", 1); m.stairs(*DECL_W, 14, "x", -1)
    for (x0, z0, x1, z1) in (DECL_E, DECL_W):              # rails round the decline mouths on the surface
        m.box(x0, 14, z0 - 0.15, x1, 15, z0, accent=True); m.box(x0, 14, z1, x1, 15, z1 + 0.15, accent=True)
    # the pit rim: a low timber fence round the hole (gaps where the haul ramps come up)
    for (x0, z0, x1, z1) in minus((-96.3, -104.3, -15.7, -35.7), [PIT, (-101, -60.5, -95, -55.5), (-17, -66.5, -11, -61.5), (-62.5, -37, -57.5, -31)]):
        m.box(x0, 14, z0, x1, 14.9, z1, accent=True)
    # the cliffs above the surface and the cavern wall that closes the north half off from the mountain's inside
    for r in minus((-LHX, -8, LHX, -6), [(-80, -8, -70, -6), (-30, -8, -20, -6), (20, -8, 30, -6), (70, -8, 80, -6)]): m.box(r[0], 0, r[1], r[2], 14, r[3])
    for (x0, x1) in ((-80, -70), (-30, -20), (20, 30), (70, 80)):
        m.box(x0, 6, -8, x1, 14, -6)                                         # portal lintels (the portals are 6 m high)
        # (jambs inside the opening, flush with the wall's faces: posts standing proud of it stopped bots sliding
        # along the wall to the portal - F10)
        m.box(x0, 0, -8, x0 + 0.3, 6, -6, mat="metal"); m.box(x1 - 0.3, 0, -8, x1, 6, -6, mat="metal")
        m.box(x0 - 0.3, 6, -8.3, x1 + 0.3, 6.6, -5.7, accent=True); m.light((x0 + x1) / 2, 5, -7, 9, 0.7, fixture=False)
    m.box(-LHX, 14, -8, LHX, LH, -6)
    for y in (1.2, 2.6, 8.0): m.box(-LHX, y, -8.06, LHX, y + 0.15, -8, mat="trim"); m.box(-LHX, y, -6, LHX, y + 0.15, -5.94, mat="trim")
    timber_set(-LHX, -8.25, LHX, -8, 0, 4.5); timber_set(-LHX, -6, LHX, -5.75, 0, 4.5)
    for y in (1.2, 2.6, 7.0):                                    # strata on the pit wall's outer face (seen from the workings)
        for r in minus((PIT[0] - 2.06, PIT[1] - 2.06, PIT[2] + 2.06, PIT[3] + 2.06), [(PIT[0] - 2, PIT[1] - 2, PIT[2] + 2, PIT[3] + 2)]):
            m.box(r[0], y, r[1], r[2], y + 0.15, r[3], mat="trim")
    for y in range(16, LH, 3): m.box(-LHX, y, -8.08, LHX, y + 0.2, -8, mat="trim")

    # ---- the workings under the slab: rock pillars left standing on a 16 m grid (room-and-pillar mining)
    A_ZONE = (-7, -71, 7, -57)
    C_ZONE = (-7, -35, 7, -21)
    # spawn pads per stage: (y, [(x, z)], yaw). Each stage's two sides walk the same distance to its point, within a
    # second at a run (owner decision B2: the attackers' arrival minus the defenders' within -1..+1 s at every point,
    # walked on the bots' nav graph)
    SP = {
        "att0": (14, [(50, -110), (55, -110), (60, -110), (50, -105), (55, -105), (60, -105)], 90),     # (the surface, east of the pithead)
        "def0": (14, [(-45, -110), (-40, -110), (-35, -110), (-45, -105), (-40, -105), (-35, -105)], -90),   # (the surface, west of it)
        "att1": (14, [(-8, -104), (-2, -104), (4, -104), (-8, -98), (-2, -98), (4, -98)], 180),
        "def1": (0, [(-102, -72.5), (-100.5, -72.5), (-99, -72.5), (-102, -69.5), (-100.5, -69.5), (-99, -69.5)], -90),   # (the west adit, behind the pit wall)
        "att2": (0, [(-44, -76), (-42, -76), (-40, -76), (-44, -78), (-42, -78), (-40, -78)], 180),    # (the pit floor, by B, off the rails)
        "def2": (0, [(54, -26), (58, -26), (62, -26), (54, -29), (58, -29), (62, -29)], 90),             # (the workings east of C)
        "att3": (0, [(-30, -30), (-25, -30), (-20, -30), (-30, -25), (-25, -25), (-20, -25)], 180),    # (the workings by C)
        "def3": (12, [(-108, 30), (-102, 30), (-108, 38), (-102, 38), (-108, 46), (-102, 46)], -90),
        "att4": (0, [(-30, 36), (-24, 36), (-18, 36), (-30, 42), (-24, 42), (-18, 42)], 90),             # (the grotto, east of D)
        "def4": (12, [(50, 101), (55, 101), (60, 101), (50, 106), (55, 106), (60, 106)], 0),             # (the shaft station's ledge)
    }
    keep = [PIT, DECL_E, DECL_W] + [(r[0] - 2, r[1] - 2, r[2] + 2, r[3] + 2) for r in RIMHOLES] + [(6, -70, 18, -58), A_ZONE, C_ZONE, (-28, -76, -16, -66), (-96, -76, -84, -66), (-46, -50, -36, -36)]
    keep += [(x0 - 2, -10, x1 + 2, -4) for (x0, x1) in ((-80, -70), (-30, -20), (20, 30), (70, 80))]   # in front of the portals
    keep += [(-LHX, -74, LHX, -68), (22, -LHZ, 28, -6)]                                             # the rail lines
    for k, (y, pts, yaw) in SP.items():
        if y == 0 and k[3] in "0123" and all(z < -8 for (x, z) in pts):
            xs = [x for (x, z) in pts]; zs = [z for (x, z) in pts]
            keep.append((min(xs) - 3, min(zs) - 3, max(xs) + 3, max(zs) + 3))
    keep += [(72, -91, 79, -85), (-107, -43, -101, -37), (-LHX, -LHZ, -100, -56)]
    def free(r): return not any(r[0] < k[2] and r[2] > k[0] and r[1] < k[3] and r[3] > k[1] for k in keep)
    pillars = 0
    for row, pz in enumerate(range(-100, -8, 14)):          # (alternate rows offset by half a bay: no long lines)
        for px in range(-104 + (8 if row % 2 else 0), LHX - 3, 16):
            r = (px - 3.5, pz - 3.5, px + 3.5, pz + 3.5)
            if r[3] > -10: continue
            if not free(r): continue
            m.box(r[0], 0, r[1], r[2], 13, r[3]); pillars += 1
            m.box(r[0] - 0.3, 11.8, r[1] - 0.3, r[2] + 0.3, 13, r[3] + 0.3, accent=True)     # timber cap where it meets the roof
    # the two pillars the extra ladders climb (a rock face each, with a hole in the roof over the ladder)
    m.box(76, 0, -91, 79, 13, -85); m.ladder(75.88, -88, 0, 14, "e", width=0.8)
    m.box(-107, 0, -43, -104.6, 13, -37); m.ladder(-104.48, -40, 0, 14, "w", width=0.8)
    # lamps under the roof between the pillars, every other bay
    for lx in range(-96, LHX - 8, 32):
        for lz in range(-92, -8, 32):
            if PIT[0] - 2 < lx < PIT[2] + 2 and PIT[1] - 2 < lz < PIT[3] + 2: continue
            lamp(lx, lz, 13)
    for lx in range(-80, LHX - 8, 32):
        for lz in range(-76, -8, 32):
            if PIT[0] - 2 < lx < PIT[2] + 2 and PIT[1] - 2 < lz < PIT[3] + 2: continue
            lamp(lx, lz, 13)
    # rails through the workings: the east-west haulage from the pit's adit, and the main line south to the caverns
    lrails([(-16, -71), (108, -71)]); lrails([(25, -108), (25, -8)])
    for (x, z, al) in ((8, -71, "x"), (60, -71, "x"), (25, -90, "z"), (25, -40, "z"), (95, -71, "x")): cart(x, z, 0, al)
    # C: the workings' loading bay (ore bins, carts on a siding, timber cribs)
    m.dais("C", C_ZONE)
    m.ring_cover(0, -28, 0.5, seed=3)
    lrails([(-12, -28), (-8, -28)])
    for (x, z, al) in ((-10, -24, "z"), (10, -32, "z"), (-10, -33, "z")): cart(x, z, 0, al)
    for (x, z) in ((12, -24), (-14, -40), (14, -40)):                               # timber cribs (stacked sleepers)
        for k in range(4): m.box(x - 1, k * 0.35, z - 1 + (0.2 if k % 2 else 0), x + 1, k * 0.35 + 0.35, z + 1 - (0.2 if k % 2 else 0), accent=(k % 2 == 0))
    # the pithead shaft: a timber shaft from the workings up to the headframe, ladder inside
    sx0, sz0, sx1, sz1 = SHAFT
    m.box(sx0 - 0.3, 0, sz0 - 0.3, sx1 + 0.3, 13, sz0, accent=True)                 # north wall (the ladder's)
    m.box(sx0 - 0.3, 0, sz0, sx0, 13, sz1 + 0.3, accent=True); m.box(sx1, 0, sz0, sx1 + 0.3, 13, sz1 + 0.3, accent=True)
    m.box(sx0, 2.8, sz1, sx1, 13, sz1 + 0.3, accent=True)                          # south wall over the shaft door
    m.ladder(12, sz0 + 0.12, 0, 14, "n", width=0.9)
    m.light(12, 2.5, -64, 6, 0.6)

    # ---- the surface (14): the pithead (A), the winding house, the mill, spoil heaps, a rail line between them
    m.dais("A", A_ZONE, y=SURF)
    m.ring_cover(0, -64, SURF + 0.5, seed=1)
    for (x, z, al) in ((-10, -64, "z"), (10, -74, "x"), (-4, -54, "x")): cart(x, z, SURF, al)
    # the headframe over the shaft: two legs, a back stay, the sheave wheel on top
    for (x, z) in ((9.6, -66), (14.4, -66), (9.6, -62), (14.4, -62)): m.metal(x - 0.25, SURF, z - 0.25, x + 0.25, SURF + 12, z + 0.25)
    m.box(9.2, SURF + 12, -66.4, 14.8, SURF + 12.8, -61.6, accent=True)
    m.metal(11.6, SURF + 12.8, -64.6, 12.4, SURF + 15.2, -63.4); m.glow(11.5, SURF + 13.8, -64.7, 12.5, SURF + 14.2, -63.3)
    for y in (SURF + 4, SURF + 8): m.box(9.4, y, -66.2, 14.6, y + 0.25, -65.8, accent=True); m.box(9.4, y, -62.2, 14.6, y + 0.25, -61.8, accent=True)
    m.metal(14.3, SURF, -64.3, 22, SURF + 0.4, -63.7)                              # the cable run to the winding house
    # the winding house (west of A) and the mill (east): walk-through sheds with lamps
    m.building(-34, -80, -18, -66, 6, [("e", -73, 2.4), ("s", -26, 2.4), ("n", -26, 2.4)], windows=True, base=SURF)
    m.metal(-30, SURF, -76, -24, SURF + 2.6, -70); m.glow(-30.05, SURF + 1.0, -76.05, -23.95, SURF + 1.15, -69.95)   # the winding drum
    m.building(30, -96, 66, -72, 8, [("w", -84, 3.0), ("e", -84, 3.0), ("s", 40, 3.0), ("s", 56, 3.0), ("n", 48, 3.0)], windows=True, base=SURF)
    for (x0, z0) in ((34, -92), (34, -80), (58, -92), (58, -80)):                   # ore bins inside the mill
        m.box(x0, SURF, z0, x0 + 4, SURF + 2.4, z0 + 4, accent=True)
    m.metal(40, SURF + 3.2, -86, 56, SURF + 3.6, -82); m.box(46, SURF, -88, 50, SURF + 3.2, -80)   # the crusher and its conveyor
    lrails([(-14, -84), (100, -84)], y=SURF)
    for (x, z) in ((20, -84), (76, -84)): cart(x, z, SURF, "x")
    for (x, z, r, h) in ((40, -44, 6, 4.5), (78, -32, 5, 3.6), (-4, -24, 4.5, 3.2), (96, -56, 4, 3.0), (60, -54, 3.5, 2.6)):
        m.heap(x, z, r, h, y=SURF, tiers=3)
    for (x, z) in ((20, -40), (70, -100), (-80, -24), (100, -20)): lamp(x, z, SURF + 6, drop=0.1, radius=10, intensity=0.6); m.metal(x - 0.12, SURF, z - 0.12, x + 0.12, SURF + 5.9, z + 0.12)
    # sheds along the cliff (cover and a break in the long surface lines)
    for (x0, z0, x1, z1, door) in ((-110, -100, -100, -90, "e"), (-110, -24, -100, -12, "e"), (100, -40, 110, -28, "w")):
        m.building(x0, z0, x1, z1, 4, [(door, (z0 + z1) / 2, 2.0)], base=SURF)

    # ---- the caverns (south half): rock roof at 32, the spine between them, ledges at 12 round the edges
    SUN = [(50, 20, 58, 28), (-70, 20, -66, 40), (-40, 70, -36, 90)]
    for r in minus((-LHX, -6, LHX, LHZ), SUN): m.box(r[0], LH, r[1], r[2], LH + 1, r[3])
    for (x0, z0, x1, z1) in SUN:                                                    # the shafts up through the rock
        for (a, b, c, d) in ((x0 - 1, z0 - 1, x1 + 1, z0), (x0 - 1, z1, x1 + 1, z1 + 1), (x0 - 1, z0, x0, z1), (x1, z0, x1 + 1, z1)):
            m.box(a, LH, b, c, LH + 14, d)
    SPINE_HOLES = [(14, 22, 0, 5), (46, 54, 0, 5), (86, 94, 12, 17)]
    def spine():
        zs = sorted({-6, LHZ} | {v for (a, b, y0, y1) in SPINE_HOLES for v in (a, b)})
        for (za, zb) in zip(zs, zs[1:]):
            hole = [h for h in SPINE_HOLES if h[0] <= za and zb <= h[1]]
            if not hole: m.box(-4, 0, za, 4, LH, zb); continue
            (_, _, y0, y1) = hole[0]
            if y0 > 0: m.box(-4, 0, za, 4, y0, zb)
            m.box(-4, y1, za, 4, LH, zb)
            m.box(-4.3, y1, za, 4.3, y1 + 0.5, zb, accent=True)
    spine()
    m.box(-0.3, 0, 13.8, 0.3, 5, 14.45, mat="metal")                            # a pit prop in the tunnel mouth (blocks the spawns' sightline)
    for y in [1.2, 2.6] + list(range(5, LH, 4)): m.box(-4.08, y, -6, 4.08, y + 0.15, LHZ, mat="trim")
    timber_set(-4.25, -6, -4, LHZ, 0, 4.5); timber_set(4, -6, 4.25, LHZ, 0, 4.5)
    # ledges (rock shelves at 12): east and south in the shaft cavern, west, south and spine-side in the grotto
    LEDGES = [(96, 8, LHX, 104), (4, 100, LHX, LHZ), (4, 80, 16, 100), (-LHX, 0, -96, 60), (-LHX, 60, -100, 100), (-LHX, 100, -4, LHZ), (-16, 80, -4, 100)]
    for (x0, z0, x1, z1) in LEDGES:
        for (a, b, c, d) in tiles([(x0, z0, x1, z1)], 16): m.box(a, 0, b, c, 12, d)
        for y in (1.2, 2.6, 5.0, 8.0, 11.8):
            m.box(x0 - 0.06, y, z0 - 0.06, x1 + 0.06, y + 0.15, z1 + 0.06, mat="trim")
    m.ladder(-95.88, 10, 0, 12, "w", width=0.8); m.ladder(95.88, 10, 0, 12, "e", width=0.8)   # the ledge ends: a ladder up,
    m.box(-96, 0, 0, -90, 6, 6); m.box(90, 0, 13, 96, 6, 18)                                   # and a rock step to drop down by
    for (x0, z0, x1, z1) in ((-96, 0, -90, 6), (90, 13, 96, 18)): m.box(x0 - 0.06, 2.6, z0 - 0.06, x1 + 0.06, 2.75, z1 + 0.06, mat="trim")
    m.stairs(84, 40, 96, 44, 12, "x", 1); m.stairs(40, 88, 44, 100, 12, "z", 1); m.stairs(8, 68, 12, 80, 12, "z", 1)
    m.stairs(-96, 20, -92, 32, 12, "z", -1); m.stairs(-40, 88, -36, 100, 12, "z", 1); m.stairs(-12, 68, -8, 80, 12, "z", 1)
    for (x0, z0, x1, z1) in LEDGES:                                                  # ledge lamps on posts
        for (x, z) in (((x0 + x1) / 2, (z0 + z1) / 2),):
            m.metal(x - 0.12, 12, z - 0.12, x + 0.12, 15, z + 0.12); m.light(x, 15.2, z, 12, 0.7)
    # ---- the shaft station (south-east cavern): the cage landing under the sun shaft, the catwalk to the east
    # ledge, sidings with tubs, and E, the mother lode, where the richest vein breaks the floor
    m.box(48, 0, 18, 60, 0.5, 30, accent=True)                                      # the landing
    for (x, z) in ((49, 19), (59, 19), (49, 29), (59, 29)):
        m.metal(x - 0.4, 0, z - 0.4, x + 0.4, LH, z + 0.4)
    for y in range(6, LH, 6):
        for (a, b, c, d) in ((48.6, 18.6, 59.4, 19.4), (48.6, 28.6, 59.4, 29.4), (48.6, 19.4, 49.4, 28.6), (58.6, 19.4, 59.4, 28.6)):
            m.box(a, y, b, c, y + 0.4, d, mat="metal")
    for r in minus((44, 14, 64, 34), [(48, 18, 60, 30)]): m.box(r[0], 11.7, r[1], r[2], 12, r[3], mat="metal")   # the mid-shaft platform
    m.metal(64, 0, 25, 65, 12, 26); m.ladder(65.12, 25.5, 0, 12, "w", width=0.8)
    m.catwalk(64, 22, 96, 25, 12, rails="z")
    m.light(54, 10, 24, 12, 0.9, fixture=False)
    lrails([(25, -6), (25, 50)]); lrails([(25, 24), (44, 24)])
    for (x, z, al) in ((25, 10, "z"), (25, 36, "z"), (38, 24, "x")): cart(x, z, 0, al)
    E_ZONE = (18, 57, 32, 71)
    m.dais("E", E_ZONE)
    m.ring_cover(25, 64, 0.5, seed=5)
    for (x, z, h) in ((14, 60, 6), (36, 68, 5), (22, 75, 7), (30, 54, 4)): crystal(x, z, h, r=0.7, seed=int(x + z))
    for (x, z) in ((70, 60), (88, 80), (40, 6), (80, 10)): m.heap(x, z, 3.5, 2.8)
    for (x, z) in ((70, 40), (40, 80)): crystal(x, z, 4.5, r=0.6)
    for (x, z) in ((36, 40), (76, 76), (60, 90), (88, 30)): lamp(x, z, LH, drop=14, radius=14, intensity=0.8)
    # stalagmites: stepped rock columns on the cavern floors (cover, anchors), their stalactites over them
    for (x, z, h) in ((70, 6, 7), (40, 48, 5), (84, 58, 6), (-30, 6, 7), (-80, 6, 6), (-20, 40, 5), (-56, 80, 6), (-84, 40, 5)):
        for k in range(3): r = 1.6 - k * 0.45; m.box(x - r, h * k / 3, z - r, x + r, h * (k + 1) / 3, z + r)
        for k in range(3): r = 1.4 - k * 0.4; m.box(x - r, LH - 4 * (k + 1) / 3, z - r, x + r, LH - 4 * k / 3, z + r)
    # ---- the crystal grotto (south-west cavern): the lake, crystal forests, D on the shore
    D_ZONE = (-58, 30, -44, 44)
    m.dais("D", D_ZONE)
    m.ring_cover(-51, 37, 0.5, seed=4)
    R = random.Random("lodestone-crystals")
    for (x, z, h) in ((-62, 46, 8), (-40, 28, 6), (-46, 52, 5), (-64, 26, 5), (-78, 58, 10), (-56, 98, 9), (-24, 76, 7), (-90, 14, 6), (-26, 22, 5),
                      (-70, 100, 6), (-30, 56, 4), (-84, 88, 7), (-50, 64, 3.5), (-36, 104, 5)):
        crystal(x, z, h, r=0.5 + h * 0.06, lean=(R.choice((-1, 1)), R.choice((-1, 0, 1))), seed=int(x * 7 + z))
    for (x, z) in ((-72, 60), (-60, 72), (-88, 60), (-62, 92)): crystal(x, z, 2.0, r=0.35)
    m.box(-97, -0.05, 61, -63, 0.05, 63, accent=True); m.box(-63, -0.05, 61, -61, 0.05, 97, accent=True)   # the lake's rim
    for (x, z) in ((-50, 10), (-80, 30), (-30, 90), (-60, 110 - 14)): lamp(x, z, LH, drop=14, radius=14, intensity=0.75)
    m.light(-80, 2, 80, 14, 0.8, fixture=False)                                       # glow off the crystal lake
    for k in "ABCDE": m.raw(m.points[k])
    # ---- every other mode (Lodestone is the map built for all of them):
    # team spawns in the two caverns, mirrored either side of the spine (the portals into the workings are symmetric)
    for (x, z) in ((-64, 10), (-60, 10), (-64, 14), (-60, 14), (-52, 10), (-52, 14)):
        m.spawn("a", x, 0, z, 0); m.spawn("b", -x, 0, z, 0)
    # free for all: twelve spawns spread over every part of the mine (a free for all wants 12+, and it had the 8 the
    # generator tops a pool up to)
    for (x, y, z) in ((-61, 12, 109), (109, 0, -109), (-109, SURF, -105), (109, 12, 67), (-1, 0, -17), (-109, 12, 7),
                      (13, SURF, -109), (89, 0, -21), (-80, 0, 40), (60, 0, 90), (60, SURF, -60), (-64, 0, -20)):
        m.spawn("any", x, y, z, 0)
    # king of the hill on the workings' loading bay (C): as far from either cavern. The bomb sites are the two places
    # in the workings both caverns reach in the same second (B2: within -1..+1 s): the loading bay, and the pillar
    # hall under the pithead (D and E sat in one cavern each: one side was there 10 s before the other, REVIEW MAP-6)
    m.raw("hill %g 0 %g %g 4 %g" % (C_ZONE[0] - 1, C_ZONE[1] - 1, C_ZONE[2] + 1, C_ZONE[3] + 1))
    for z in (C_ZONE, (-14, -62, -2, -50)): m.raw("site %g 0 %g %g 3 %g" % (z[0] - 1, z[1] - 1, z[2] + 1, z[3] + 1))
    # the payload: an ore cart from the grotto, through the spine tunnel, to the shaft station (checkpoints at 1/3, 2/3)
    CART = [(-70, 15.5), (-30, 15.5), (30, 15.5), (70, 15.5)]   # (beside the station's landing, z 18..30: not through it)
    for (x, z) in CART: m.raw(f"path {x:g} 0 {z:g}")
    m.raw("cartcheck 0.33"); m.raw("cartcheck 0.66")
    # race: from the surface down the mine to the shaft station's cage landing (not a ranked course: Viaduct is that).
    # The last leg used to end on the station's ledge at 12 m: 179 m round by the stairs, 66 m for a good player who
    # leaps and hooks up to it (a 2.7x shortcut); it ends on the landing now
    for x in (-4, 0, 4): m.spawn("race", x, SURF, -104, 180)
    for (x0, y, z0, x1, z1) in ((-7, SURF, -71, 7, -57), (-50, 0, -68, -38, -56), (-7, 0, -35, 7, -21), (-58, 0, 30, -44, 44), (18, 0, 57, 32, 71), (48, 0, 18, 60, 30)):
        m.raw(f"checkpoint {x0:g} {y:g} {z0:g} {x1:g} {y + 4:g} {z1:g}")
    m.box(30, SURF, -111, 32, SURF + 3, -100, accent=True)                       # a spoil wall: no sightline between the stage-0 spawns
    for k, (y, pts, yaw) in SP.items():                       # spawns (stage by stage): before the scatter, which avoids them
        for (x, z) in pts: m.spawn(k, x, y, z, yaw)
    # ---- rock ribs across the caverns: they split each cavern into bays (fights at rifle range, not across 100 m)
    # (the two by the cavern wall stop 2.5 m short of it: built against it, each made a dead-end corner that bots
    # walking along the rib to the portals stuck in, every defender in bomb - F10)
    RIBS = [(30, 36, 46, 38), (66, 36, 96, 38), (44, 80, 84, 82), (40, -3.5, 42, 12), (78, 50, 80, 72),
            (-92, 20, -70, 22), (-30, 58, -28, 96), (-40, 50, -10, 52), (-88, 100 - 2, -50, 100), (-24, -3.5, -22, 14)]
    for (x0, z0, x1, z1) in RIBS:
        m.box(x0, 0, z0, x1, 7, z1)
        m.box(x0 - 0.1, 7, z0 - 0.1, x1 + 0.1, 7.4, z1 + 0.1, accent=True)
        for y in (1.2, 2.6, 5.0): m.box(x0 - 0.06, y, z0 - 0.06, x1 + 0.06, y + 0.15, z1 + 0.06, mat="trim")
    # stacks of sleepers, crates and cribs both sides of the ore-cart line (payload: something to push behind and to
    # hold the cart from; the line ran through bare cavern, cover on 5% of the floor beside it), clear of its sweep
    def trackside_free(x0, z0, x1, z1):   # nothing standing within 0.6 m (rails and sleepers flush with the floor aside), no spawn within 2 m
        for ln in m.lines:
            w = ln.split()
            if len(w) == 7 and w[0] in ("box", "accent", "metal", "glow", "wallbang", "ladder"):
                b = list(map(float, w[1:]))
                if b[1] < 1.6 and b[4] > 0.25 and b[0] < x1 + 0.6 and b[3] > x0 - 0.6 and b[2] < z1 + 0.6 and b[5] > z0 - 0.6: return False
            elif w and w[0] == "spawn" and abs(float(w[3])) < 1 and x0 - 2 < float(w[2]) < x1 + 2 and z0 - 2 < float(w[4]) < z1 + 2: return False
        return True
    with m.generated():
        for k, x in enumerate(range(-66, 67, 5)):
            if abs(x) < 9: continue                                                  # (the spine tunnel's mouths)
            for (z0, z1) in ((12.3, 13.3), (17.7, 18.7)):                            # (a step off the cart's sweep, 14.5..16.5)
                if z0 > 15 and 44 < x < 64: continue                                 # (the station's landing)
                if trackside_free(x - 1.2, z0, x + 1.2, z1): m.box(x - 1.2, 0, z0, x + 1.2, 1.3 if k % 2 else 1.5, z1, accent=(k % 3 == 0))
    # crystal veins in the cavern walls (the mine follows them)
    RV = random.Random("lodestone-veins")
    for i in range(260):
        side = RV.choice("wes"); y = RV.uniform(0.6, 11); L = RV.uniform(0.4, 1.6); t = RV.uniform(0.08, 0.2)
        if side == "s": x = RV.uniform(-LHX + 1, LHX - 1); m.glow(x, y, LHZ - 0.05, x + L, y + t, LHZ + 0.05)
        else:
            z = RV.uniform(-4, LHZ - 1); x = -LHX if side == "w" else LHX
            m.glow(x - 0.05, y, z, x + 0.05, y + t, z + L)
    for i in range(120):                                         # and in the spine and the ribs
        z = RV.uniform(-4, LHZ - 2); y = RV.uniform(0.6, 10); L = RV.uniform(0.4, 1.4); xf = RV.choice((-4.05, 3.95))
        m.glow(xf, y, z, xf + 0.1, y + RV.uniform(0.08, 0.18), z + L)
    # lamp posts through both caverns (timber post, a lamp on an arm): the caverns' working light
    for lx in range(-100, LHX - 4, 22):
        for lz in range(6, LHZ - 6, 22):
            if -8 < lx < 8: continue
            if any(x0 - 2 < lx < x1 + 2 and z0 - 2 < lz < z1 + 2 for (x0, z0, x1, z1) in LEDGES + RIBS + [LAKE, (46, 16, 62, 32)]): continue
            if any(x0 - 2 < lx < x1 + 2 and z0 - 2 < lz < z1 + 2 for (x0, z0, x1, z1) in (A_ZONE, B_ZONE, C_ZONE, D_ZONE, E_ZONE)): continue
            flights = [tuple(map(float, l.split()[1:5])) for l in m.lines if l.startswith("stairs ")]
            if any(x0 - 1 < lx < x1 + 1 and z0 - 1 < lz < z1 + 1 for (x0, z0, x1, z1) in flights): continue   # (never on a staircase)
            m.box(lx - 0.15, 0, lz - 0.15, lx + 0.15, 6.2, lz + 0.15, accent=True)
            m.box(lx - 0.1, 6.0, lz - 0.1, lx + 0.9, 6.2, lz + 0.1, accent=True)
            lamp(lx + 0.8, lz, 6.0, drop=0.5, radius=16, intensity=0.9)
    for (x, z) in ((-62, 46), (-78, 58), (-56, 98), (-24, 76), (22, 75), (14, 60)): m.light(x, 3, z, 10, 0.7, fixture=False)   # crystal glow
    # cover scattered over every floor: crates, ore bins, rubble, timber cribs (none on rails, spawns, points or stairs);
    # 3.5 m apart (4 on the surface) and 0.9 m off everything else since 1.17, was 6 (7) and 1.4: the bays were bare
    # (cover on 28% of the floor)
    RAILS = [(-72, 13, 72, 21), (-LHX, -72.5, LHX, -69.5), (23.5, -LHZ, 26.5, 52), (-LHX, -85.5, LHX, -82.5), (23, 22.5, 46, 25.5), (-43, -94, -39, -34)]
    SHAPES = [(1.4, 1.4, 1.3, "box"), (2.2, 1.2, 1.5, "box"), (1.8, 1.8, 1.4, "crib"), (2.4, 1.6, 1.3, "bin"), (3.0, 3.0, 1.6, "heap"), (1.2, 2.4, 1.1, "box")]
    BIG = [(4.5, 4.5, 2.4, "box"), (5.0, 2.4, 2.2, "bin")]   # (rock falls and ore wagons on the cavern floors: things to go round)
    m.scatter((-LHX, -LHZ, LHX, -8), 0, 3.5, SHAPES, "workings", avoid=RAILS + [PIT, (-14, -40, 14, -16)], clear=0.9)
    m.scatter((PIT[0] + 12, PIT[1] + 12, PIT[2] - 12, PIT[3] - 12), 0, 3.5, SHAPES, "pitfloor", avoid=RAILS, clear=0.9)
    m.scatter((-LHX, -LHZ, LHX, -8), SURF, 4, SHAPES + [(4, 3, 2.6, "box"), (5, 3, 2.2, "bin")], "surface",
            avoid=RAILS + [(PIT[0] - 1, PIT[1] - 1, PIT[2] + 1, PIT[3] + 1), DECL_E, DECL_W, SHAFT] + LADW + RIMHOLES, clear=0.9)
    m.scatter((4, -6, LHX, LHZ), 0, 3.5, SHAPES + BIG, "station", avoid=RAILS + [(46, 16, 62, 32)], clear=0.9)
    m.scatter((-LHX, -6, -4, LHZ), 0, 3.5, SHAPES + BIG, "grotto", avoid=RAILS + [LAKE], clear=0.9)
    # ...and on the 12 m ledges round the caverns (bare rock shelves: cover on 10% of them)
    for k, r in enumerate(LEDGES): m.scatter(r, 12, 4.5, SHAPES, "ledge %d" % k, clear=0.9)

    # ---- spawns (stage by stage) and the setting
    def trim_to_rock():
        """the strata bands and timbering are long strips laid along the rock: cut them wherever nothing solid is
        behind them (a tunnel mouth, a stair slot, a portal), so no band runs across an opening in mid-air"""
        solid, grid = [], {}
        for ln in m.lines:
            w = ln.split()
            if len(w) == 7 and w[0] in ("box", "accent", "metal"):
                b = tuple(map(float, w[1:])); solid.append(b)
                for gx in range(int(b[0] // 8), int(b[3] // 8) + 1):
                    for gz in range(int(b[2] // 8), int(b[5] // 8) + 1): grid.setdefault((gx, gz), []).append(b)
        def backed(q):   # does a solid box touch this piece (within 0.15 m)?
            e = 0.15
            for gx in range(int((q[0] - e) // 8), int((q[3] + e) // 8) + 1):
                for gz in range(int((q[2] - e) // 8), int((q[5] + e) // 8) + 1):
                    for b in grid.get((gx, gz), ()):
                        if b[0] < q[3] + e and b[3] > q[0] - e and b[1] < q[4] and b[4] > q[1] and b[2] < q[5] + e and b[5] > q[2] - e: return True
            return False
        out = []
        for ln in m.lines:
            w = ln.split()
            if not (len(w) == 7 and w[0] == "trim"): out.append(ln); continue
            b = list(map(float, w[1:]))
            ext = [b[3] - b[0], b[4] - b[1], b[5] - b[2]]
            ax = max(range(3), key=lambda i: ext[i])
            if ext[ax] < 2: out.append(ln); continue
            lo, hi = b[ax], b[ax + 3]
            n = max(1, int(math.ceil((hi - lo) / 0.5)))
            keep, start = [], None
            for i in range(n + 1):
                ok = False
                if i < n:
                    q = list(b); q[ax] = lo + (hi - lo) * i / n; q[ax + 3] = lo + (hi - lo) * (i + 1) / n
                    ok = backed(q)
                if ok and start is None: start = i
                if not ok and start is not None: keep.append((start, i)); start = None
            for (i0, i1) in keep:
                q = list(b); q[ax] = lo + (hi - lo) * i0 / n; q[ax + 3] = lo + (hi - lo) * i1 / n
                out.append("trim " + " ".join(f"{v:g}" for v in q))
        m.lines = out
    m.raw("setting mine")
    m.props(); m.dress("lodestone"); m.detail_pass(); m.skyline_pass("lodestone")
    with m.generated(): trim_to_rock()   # (its cut pieces are generated: kept off keep-clear volumes)
    # every mode but the four that want a compact arena (team vs, duel, 1v1, tourney: 300-3000 m2 against the mine's
    # ~83,000; owner's call, 1.17): they can still be started here, with the menus' "may not work" warning
    m.also = ["bomb", "race", "koth", "payload", "npayload", "hide", "tag"]
    m.save("lodestone", dress=False)
