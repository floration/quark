"""FAIRGROUND (1.19.6): tag (hide and seek, free for all) at a funfair at night, 120 x 100 m and mostly flat. Nothing
moves: every ride is a static prop. Loops at every size for the chase:
  the carousel in the middle (a drum to run round, horses on poles, a canopy at 6 m: the tightest loop),
  the Ferris wheel to the north (a 28 m wheel on A-frames, gondolas at many heights, a ladder up a mast behind it to
  the hub platform at 17 m; its spokes and rim are grapple anchors),
  the big top to the north-west (tiered seats round the ring, the trapeze rig with a ladder up one mast),
  the ghost train to the west (a dark tunnel loop, in one door and out the other, static cars on the rails),
  the stall avenues west and east of the carousel (a counter to vault at the front, a back door, some boarded up with
  plywood you can dash through),
  the hall of mirrors to the east (a braided maze of 1.7 m corridors with loops, three doors, solid mirror tiles and
  seven breakable mirrors),
  the dodgems rink to the north-east (a roof on posts, rubber barriers),
  the helter-skelter to the south-east (a square spiral stair up inside the tower, the railed slide down round it),
  the bouncy castle to the south-west (rubber all over: bounce out over its wall),
  the duck pond (pink slime throws you out) and the drinks-stand spill (fizzing soda: a big jump) to the south.
Painted hoarding (a chain-link fence at the gate's end) and invisible walls ring the fair.
(1.19.6 fix round, all review notes YES: bulb strings (glow) on the carousel, the wheel's rim and spokes, the big top's
eave and ridges, every stall's canopy, the slide and the gate; neon boards on the ghost train, the mirror maze, the
dodgems and the gate; light-block strips under the dodgems' and the marquee's roofs; five canopy colours and prize
shelves on the stalls; a coloured bouncy castle; darker ground, brighter rides; the maze braided (six more openings,
a third door, seven breakable mirrors); the helter-skelter's slide a railed, skirted trough with cut corners and its
ladder a spiral stair; hide: a crawl under the big top's raked seating, doors on the ghost train's alcoves, five boarded
stalls shuttered as cupboards; hoarding instead of see-through mesh on three sides; the far city as ten lit blocks.)"""
import math
from .prefabs import M
from .m6kit import Kit, wall_x, wall_z, slab, shell, yaw_to, minus, tiles, num

HX, HZ = 60, 50
LID = 78.0
WHEEL = (0.0, 17.0, -36.0, 14.0)        # the wheel: centre x, y, z (its plane), radius


def vbar(k, mat, a, b, w, z0, z1):
    """a straight bar in a vertical x-y plane from a (x, y) to b, w wide, from z0 to z1 (a cut box)"""
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    L = math.hypot(dx, dy)
    nx, ny = -dy / L, dx / L
    d0 = nx * ax + ny * ay
    box = (min(ax, bx) - w, min(ay, by) - w, z0, max(ax, bx) + w, max(ay, by) + w, z1)
    ux, uy = dx / L, dy / L   # (end caps square to the bar)
    k.cut(mat, box, [(nx, ny, 0, d0 + w / 2), (-nx, -ny, 0, -d0 + w / 2), (ux, uy, 0, ux * bx + uy * by), (-ux, -uy, 0, -(ux * ax + uy * ay))])


def cone(k, mat, cx, cz, r, ye, ya, n=8, t=0.3, a0=-22.5):
    """a conical roof (n faces) over a polygon of circumradius r at the eave ye, apex ya: thin plates"""
    a = r * math.cos(math.pi / n)
    kk = (ya - ye) / a
    for i in range(n):
        t0 = math.radians(a0 + 360.0 * i / n); t1 = math.radians(a0 + 360.0 * (i + 1) / n)
        tm = (t0 + t1) / 2
        ux, uz = math.cos(tm), math.sin(tm)
        v0 = (cx + r * math.cos(t0), cz + r * math.sin(t0)); v1 = (cx + r * math.cos(t1), cz + r * math.sin(t1))
        top = (kk * ux, 1, kk * uz, ye + kk * a + kk * (cx * ux + cz * uz))
        L = math.sqrt(kk * kk + 1)
        bot = (-kk * ux, -1, -kk * uz, -(top[3] - t * L))
        planes = [top, bot]
        for (vx, vz), sgn in ((v0, -1), (v1, 1)):     # the sector's radial sides
            ex, ez = vx - cx, vz - cz
            nx, nz = -ez * sgn, ex * sgn               # (pointing out of the sector)
            if nx * (math.cos(tm)) + nz * (math.sin(tm)) > 0: nx, nz = -nx, -nz
            Ln = math.hypot(nx, nz)
            planes.append((nx / Ln, 0, nz / Ln, (nx * cx + nz * cz) / Ln))
        xs = [cx, v0[0], v1[0]]; zs = [cz, v0[1], v1[1]]
        k.cut(mat, (min(xs), ye - 0.6, min(zs), max(xs), ya + 0.05, max(zs)), planes)


def bulbs(k, pts, r=0.075):
    """a string of glow bulbs (0.15 m boxes) at the points [(x, y, z)]"""
    for (x, y, z) in pts:
        k.box("glow", x - r, y - r, z - r, x + r, y + r, z + r)


def along(a, b, step=1.4, ends=True):
    """points from a to b (x, y, z) about step apart"""
    L = math.dist(a, b)
    n = max(1, int(round(L / step)))
    r = range(n + 1) if ends else range(1, n)
    return [tuple(a[i] + (b[i] - a[i]) * j / n for i in range(3)) for j in r]


def circle(cx, y, cz, r, step=1.4, a0=0.0):
    n = max(8, int(round(2 * math.pi * r / step)))
    return [(cx + r * math.cos(a0 + 2 * math.pi * i / n), y, cz + r * math.sin(a0 + 2 * math.pi * i / n)) for i in range(n)]


CANOPIES = ["stripedwallpaper", "powdercoatyellow", "pinkbrick", "powdercoatgreen", "paintedredbrick"]
PRIZES = ["pinkbrick", "powdercoatyellow", "powdercoatgreen", "bluedsteel", "paintedredbrick", "whiteconcrete"]


def stall(k, x0, z0, face, mat, boarded, n=0, cupboard=False):
    """a stall 4 x 4 m: x0..x0+4, z0..z0+4; its counter (1.1 m) on the side facing `face` (+1: +x, -1: -x), a back
    door opposite (boarded up with plywood when boarded: dash through), thin side walls, a sloped canopy"""
    xf, xb = (x0 + 4, x0) if face > 0 else (x0, x0 + 4)
    sgn = 1 if face > 0 else -1
    for z in (z0, z0 + 3.88):
        k.box(mat, x0, 0, z, x0 + 4, 3.0, z + 0.12)
    wall_z(k, mat, z0, z0 + 4, xb - sgn * 0.06, 0, 3.0, [(z0 + 2, 1.4, 2.4)], t=0.12)
    if boarded: k.box("plywoodboardup", xb - sgn * 0.12 - 0.06, 0, z0 + 1.3, xb - sgn * 0.12 + 0.06, 2.4, z0 + 2.7)
    k.box("cratewood", min(xf, xf - sgn * 0.5), 0, z0 + 0.12, max(xf, xf - sgn * 0.5), 1.1, z0 + 3.88)
    shell(k, CANOPIES[n % len(CANOPIES)], (x0 - (0.6 if face < 0 else 0), 3.0, z0, x0 + 4 + (0.6 if face > 0 else 0), 3.8, z0 + 4),
          "-x" if face > 0 else "+x", t=0.15)
    # a prize shelf on the back wall (either side of the back door: 0.3 m deep at 1.5) with small coloured prizes
    xs = xb + sgn * 0.06
    for (za, zb) in ((z0 + 0.12, z0 + 1.3), (z0 + 2.7, z0 + 3.88)):
        k.box("wideplanks", min(xs, xs + sgn * 0.3), 1.5, za, max(xs, xs + sgn * 0.3), 1.58, zb)
        for j in range(2):
            pz = za + 0.2 + j * 0.5
            k.box(PRIZES[(n + j + (1 if za > z0 + 2 else 0)) % len(PRIZES)], min(xs + sgn * 0.05, xs + sgn * 0.27), 1.58, pz,
                  max(xs + sgn * 0.05, xs + sgn * 0.27), 1.58 + 0.25 + 0.1 * ((n + j) % 3), pz + 0.3)
    # bulbs along the canopy's front edge
    xe = xf + sgn * 0.6
    bulbs(k, along((xe, 2.92, z0 + 0.3), (xe, 2.92, z0 + 3.7), step=1.1))
    # a cupboard: the front shuttered above the counter, in only by dashing through the boarded back (hide)
    if cupboard:
        k.box("rollershutter", min(xf, xf - sgn * 0.08), 1.1, z0 + 0.12, max(xf, xf - sgn * 0.08), 3.0, z0 + 3.88)


def maze(seed, n):
    """a perfect maze on an n x n grid (deterministic): the set of open walls between cells"""
    import random
    rnd = random.Random(seed)
    seen, open_ = {(0, 0)}, set()
    stack = [(0, 0)]
    while stack:
        c = stack[-1]
        nb = [(c[0] + dx, c[1] + dz) for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1))
              if 0 <= c[0] + dx < n and 0 <= c[1] + dz < n and (c[0] + dx, c[1] + dz) not in seen]
        if not nb: stack.pop(); continue
        q = rnd.choice(nb)
        open_.add(frozenset((c, q))); seen.add(q); stack.append(q)
    return open_


def build():
    m = M("Fairground", "tag", sun=(0.3, 1.0, 0.5))
    m.also = ["hide"]
    k = Kit(m)
    m.raw("palette base 0.7 0.66 0.6 accent 0.6 0.2 0.3 metal 0.36 0.36 0.4 glow 1 0.82 0.45 sky 0.03 0.03 0.07")
    m.raw("setting a funfair at night")

    # ================================================================== ground, fence, bounds
    POND, SPILL = (-6, 30, 6, 36), (24, 28, 28, 32)
    AVE = [(-32, -18, -22, 26), (22, -18, 32, 26)]                      # the stall avenues (gravel)
    k.box("peat", -HX, -1.2, -HZ, HX, -0.8, HZ)                          # (under the pond and the spill)
    for (x0, z0, x1, z1) in tiles(minus((-HX, -HZ, HX, HZ), AVE + [POND, SPILL]), 30):
        k.box("peat", x0, -0.4, z0, x1, 0, z1)                           # (dark trodden earth: the lit rides stand out)
    for r in AVE:
        for (x0, z0, x1, z1) in tiles([r], 30): k.box("gravel", x0, -0.4, z0, x1, 0, z1)
    # the perimeter: painted hoarding (solid boards) on the north, east and west, a chain-link fence (players can't pass,
    # sight and shots can) only at the gate's end (south: less see-through to draw), on posts, clip above, the lid
    for (x0, z0, x1, z1) in [(-HX - 0.2, -HZ, -HX, HZ), (HX, -HZ, HX + 0.2, HZ), (-HX, -HZ - 0.2, HX, -HZ)]:
        k.box("chippedpaint", x0, 0, z0, x1, 3.0, z1)
    k.box("chainlinkmesh", -HX, 0, HZ, HX, 3.0, HZ + 0.1)
    for x in range(-HX, HX + 1, 10):
        for z in (-HZ, HZ):
            k.box("galvanizedsteel", x - 0.08, 0, z - 0.08, x + 0.08, 3.2, z + 0.08)
    for z in range(-HZ + 10, HZ, 10):
        for x in (-HX, HX):
            k.box("galvanizedsteel", x - 0.08, 0, z - 0.08, x + 0.08, 3.2, z + 0.08)
    m.clip_walls(-HX, -HZ, HX, HZ, 0, up=LID)
    k.box("clip", -HX - 2, LID, -HZ - 2, HX + 2, LID + 1, HZ + 2)
    # the far city and hills beyond the fence
    # (separate blocks of different heights with lit window rows, not one wall)
    for (x0, z0, x1, z1, h) in [(-170, -175, -120, -150, 26), (-80, -180, -30, -155, 40), (10, -170, 50, -150, 22),
                                (80, -185, 140, -155, 34), (155, -90, 180, -40, 18), (160, 10, 185, 70, 30),
                                (60, 150, 120, 170, 16), (-60, 155, 0, 175, 24), (-175, 60, -150, 110, 20), (-180, -60, -155, -10, 28)]:
        k.box("far", x0, -1, z0, x1, h, z1)
        for y in range(6, int(h) - 2, 6):
            if abs(z0) > 100:
                zf = z1 if z0 < 0 else z0
                k.box("glow", x0 + 3, y, min(zf, zf + (0.2 if z0 < 0 else -0.2)), x1 - 3, y + 1.0, max(zf, zf + (0.2 if z0 < 0 else -0.2)))
            else:
                xf = x1 if x0 < 0 else x0
                k.box("glow", min(xf, xf + (0.2 if x0 < 0 else -0.2)), y, z0 + 3, max(xf, xf + (0.2 if x0 < 0 else -0.2)), y + 1.0, z1 - 3)

    # ================================================================== the carousel (centre): platform r 8 at 0.5,
    # the drum r 2.4 to the canopy, horses on poles at r 5.6, the canopy (a cone, 6 -> 8.5) on posts at r 8
    k.ring("wideplanks", 0, 0, 0, 8.2, 0, 0.5, n=16)
    k.disc("stripedwallpaper", 0, 0, 2.4, 0.5, 6.0, n=8)
    for i in range(8):
        a = math.radians(22.5 + 45 * i)
        x, z = 5.6 * math.cos(a), 5.6 * math.sin(a)
        k.box("brass", x - 0.06, 0.5, z - 0.06, x + 0.06, 6.0, z + 0.06)
        ta = math.radians(45 * i)
        hx, hz = 5.6 * math.cos(ta), 5.6 * math.sin(ta)
        k.box("brass", hx - 0.06, 0.5, hz - 0.06, hx + 0.06, 6.0, hz + 0.06)
        k.box("powdercoatyellow" if i % 2 else "whiteconcrete", hx - 0.25, 1.3, hz - 0.25, hx + 0.25, 2.2, hz + 0.25)
        px, pz = 8.0 * math.cos(a), 8.0 * math.sin(a)
        k.box("brass", px - 0.1, 0.5, pz - 0.1, px + 0.1, 6.0, pz + 0.1)
    k.ring("stripedwallpaper", 0, 0, 0, 8.6, 6.0, 6.3, n=16)
    cone(k, "stripedwallpaper", 0, 0, 8.6, 6.3, 8.8, n=8)
    k.disc("gold", 0, 0, 0.3, 8.8, 9.6, n=8)
    bulbs(k, circle(0, 5.95, 0, 8.75, step=1.2))                                     # bulbs round the canopy's rim
    for i in range(8):                                                                # and up its ridges
        a = math.radians(-22.5 + 45 * i)
        bulbs(k, along((8.5 * math.cos(a), 6.45, 8.5 * math.sin(a)), (0.6 * math.cos(a), 8.7, 0.6 * math.sin(a)), step=1.4, ends=False))

    # ================================================================== the Ferris wheel (north): A-frames at the
    # wheel's front and back, the rim (16 bars), 8 spokes, gondolas (platforms with a seat) under 8 rim points, the
    # axle; the mast behind it with a ladder up to the hub platform
    cx, cy, cz, R = WHEEL
    for zf in (cz - 2.2, cz + 2.2):
        for sx in (-1, 1):
            vbar(k, "galvanizedsteel", (cx + sx * 9.0, 0.0), (cx + sx * 0.6, cy), 0.6, zf - 0.3, zf + 0.3)
        k.box("galvanizedsteel", cx - 6.5, 4.6, zf - 0.2, cx + 6.5, 5.0, zf + 0.2)          # the A's crossbar
    k.box("castiron", cx - 0.7, cy - 0.7, cz - 2.6, cx + 0.7, cy + 0.7, cz + 2.6)            # the axle and hub
    pts = [(cx + R * math.cos(2 * math.pi * i / 16), cy + R * math.sin(2 * math.pi * i / 16)) for i in range(16)]
    for i in range(16):
        vbar(k, "chrome", pts[i], pts[(i + 1) % 16], 0.5, cz - 0.6, cz + 0.6)
    for i in range(0, 16, 2):
        vbar(k, "chrome", (cx, cy), pts[i], 0.3, cz - 0.2, cz + 0.2)
    for p in circle(cx, cz + 0.68, cy, R, step=1.3):                                # bulbs round the rim (front face) and up the spokes
        bulbs(k, [(p[0], p[2], p[1])])
    for i in range(0, 16, 2):
        bulbs(k, along((cx, cy, cz + 0.3), (pts[i][0], pts[i][1], cz + 0.3), step=1.4, ends=False))
    for i in range(1, 16, 2):   # gondolas: a floor 2.4 x 2.0 hung 1.6 under the rim point, a back and a seat
        gx, gy = pts[i]
        if gy - 1.6 < 1.0: continue
        k.box("powdercoatyellow", gx - 1.2, gy - 1.8, cz - 1.0, gx + 1.2, gy - 1.6, cz + 1.0)
        k.box("powdercoatyellow", gx - 1.2, gy - 1.6, cz + 0.88, gx + 1.2, gy - 0.6, cz + 1.0)
        k.box("galvanizedsteel", gx - 0.05, gy - 1.6, cz - 0.05, gx + 0.05, gy - 0.25, cz + 0.05)
    # the mast (x -0.5..0.5, z -41.4..-40.4) up to the hub platform (z -43..-40.4 at 17), its ladder on the south face
    k.box("galvanizedsteel", -0.5, 0, -41.4, 0.5, cy, -40.4)
    k.box("steeldeckplate", -1.6, cy - 0.3, -43.4, 1.6, cy, -40.4)
    for (x0, z0, x1, z1) in [(-1.6, -43.4, 1.6, -43.28), (-1.6, -43.4, -1.48, -40.4), (1.48, -43.4, 1.6, -40.4)]:
        k.box("galvanizedsteel", x0, cy, z0, x1, cy + 1.1, z1)
    k.ladder(0, -40.28, 0, cy, "n", mat="galvanizedsteel")
    k.box("chippedpaint", -1.6, cy, -43.4, 1.6, cy + 2.6, -43.2)                       # the wheel's sign board
    # the queue: a picket fence round the boarding platform at the wheel's foot (dash through it)
    k.box("wideplanks", -4, 0, cz - 3, 4, 0.4, cz + 3)
    for (x0, z0, x1, z1) in [(-7, cz + 4.5, -2, cz + 4.62), (2, cz + 4.5, 7, cz + 4.62)]:
        k.box("picketfence", x0, 0, z0, x1, 1.0, z1)

    # ================================================================== the big top (north-west, centre -36 -30, r 12)
    BX, BZ = -36.0, -30.0
    k.ring("leantocanvas", BX, BZ, 11.6, 12.0, 0, 3.6, n=8, a0=-22.5, skip=(0, 2))
    k.ring("leantocanvas", BX, BZ, 11.6, 12.0, 3.6, 5.0, n=8, a0=-22.5)
    cone(k, "leantocanvas", BX, BZ, 12.4, 5.0, 13.0, n=8)
    k.disc("gold", BX, BZ, 0.25, 13.0, 15.0, n=8)
    # the seating (fix round): three raked tiers on a dark under-stand (a 1.2 m crawl beneath, in through two gaps in its
    # front skirt: 1.3 m under the first tier), a step up at the front
    for (r0, r1, top) in ((7.4, 8.6, 1.6), (8.6, 9.8, 2.1), (9.8, 11.0, 2.6)):
        k.ring("cratewood", BX, BZ, r0, r1, top - 0.3, top, n=8, a0=-22.5, skip=(0, 2))
    k.ring("cratewood", BX, BZ, 8.6, 8.75, 1.6, 2.1, n=8, a0=-22.5, skip=(0, 2))     # (the risers)
    k.ring("cratewood", BX, BZ, 9.8, 9.95, 2.1, 2.6, n=8, a0=-22.5, skip=(0, 2))
    k.ring("charredtimber", BX, BZ, 7.3, 7.45, 0, 1.3, n=8, a0=-22.5, skip=(0, 2, 4, 6))   # the skirt (gaps: 4, 6)
    k.ring("charredtimber", BX, BZ, 11.0, 11.15, 0, 2.6, n=8, a0=-22.5, skip=(0, 2))
    for sk in (0, 2):                                                                 # (the under-stand's ends at the aisles)
        for a in (-22.5 + 45 * sk, 22.5 + 45 * sk):
            t = math.radians(a)
            k.bar("charredtimber", (BX + 7.3 * math.cos(t), BZ + 7.3 * math.sin(t)), (BX + 11.1 * math.cos(t), BZ + 11.1 * math.sin(t)), 0.15, 0, 1.3)
    k.ring("cratewood", BX, BZ, 6.8, 7.3, 0, 0.6, n=8, a0=-22.5, skip=(0, 2, 4, 6))   # the step up
    bulbs(k, circle(BX, 4.95, BZ, 12.5, step=1.4, a0=math.radians(10)))              # bulbs round the eave
    for i in range(8):                                                                # and down the ridges
        a = math.radians(-22.5 + 45 * i)
        bulbs(k, along((BX + 12.2 * math.cos(a), 5.3, BZ + 12.2 * math.sin(a)), (BX + 0.6 * math.cos(a), 12.75, BZ + 0.6 * math.sin(a)), step=1.5, ends=False))
    k.disc("sand", BX, BZ, 5.0, 0, 0.1, n=8)                                        # the ring
    k.ring("paintedredbrick", BX, BZ, 5.0, 5.3, 0, 0.5, n=8, a0=-22.5, skip=(0,))
    # the trapeze rig: two masts (8 m, a platform each, a ladder up the west one), the bar between them at 9.5
    for x in (BX - 3.4, BX + 3.4):
        k.box("galvanizedsteel", x - 0.3, 0, BZ - 0.3, x + 0.3, 8.0, BZ + 0.3)
        k.box("steeldeckplate", x - 1.0, 7.7, BZ - 1.8, x + 1.0, 8.0, BZ + 0.3)
        k.box("galvanizedsteel", x - 1.0, 8.0, BZ - 1.8, x + 1.0, 9.6, BZ - 1.68)
        for dx in (-0.9, 0.9):
            k.box("galvanizedsteel", x + dx - 0.05, 8.0, BZ - 1.65, x + dx + 0.05, 11.0, BZ - 1.55)
    k.box("galvanizedsteel", BX - 4.3, 11.0, BZ - 1.66, BX + 4.3, 11.12, BZ - 1.54)
    k.box("galvanizedsteel", BX - 1.0, 9.5, BZ - 0.06, BX + 1.0, 9.62, BZ + 0.06)
    k.ladder(BX - 3.4, BZ + 0.42, 0.1, 8.0, "n", mat="galvanizedsteel")

    # ================================================================== the ghost train (west, x -58..-42, z -12..14):
    # a loop corridor (2.6 m) round a solid core with three scare alcoves, in at the north door, out at the south;
    # a wall across the east corridor between the doors makes it one loop. Static cars on the rails.
    GX0, GZ0, GX1, GZ1 = -58.0, -12.0, -42.0, 14.0
    wall_z(k, "rottingwood", GZ0, GZ1, GX1, 0, 4.5, [(-8, 2.6, 3.2), (10, 2.6, 3.2)], t=0.4)
    wall_z(k, "rottingwood", GZ0, GZ1, GX0, 0, 4.5, t=0.4)
    wall_x(k, "rottingwood", GX0, GX1, GZ0, 0, 4.5, t=0.4)
    wall_x(k, "rottingwood", GX0, GX1, GZ1, 0, 4.5, t=0.4)
    k.box("charredtimber", GX0 - 0.2, 4.5, GZ0 - 0.2, GX1 + 0.2, 4.9, GZ1 + 0.2)
    core = (-55.2, -9.2, -45.0, 11.2)
    alcoves = [(-55.2, -2, -53.0, 2.0), (-49, -9.2, -46, -7.2), (-50, 9.2, -47, 11.2)]
    for (x0, z0, x1, z1) in minus(core, alcoves):
        k.box("peelingwallpaper", x0, 0, z0, x1, 4.5, z1)
    k.box("peelingwallpaper", -45.0, 0, 0.6, -42.2, 4.5, 1.4)                       # (the east corridor's block)
    # a door into each scare alcove (a front wall with a 1 m doorway: closets to hide in)
    wall_z(k, "rottingwood", -2.0, 2.0, -55.3, 0, 4.5, [(0, 1.0, 2.4)], t=0.2)
    wall_x(k, "rottingwood", -49.0, -46.0, -9.3, 0, 4.5, [(-47.5, 1.0, 2.4)], t=0.2)
    wall_x(k, "rottingwood", -50.0, -47.0, 11.3, 0, 4.5, [(-48.5, 1.0, 2.4)], t=0.2)
    k.box("neonmaze", GX1 + 0.4, 8.4, -5.0, GX1 + 0.48, 10.2, 7.0)                    # its neon sign on the pediment
    for (x0, z0, x1, z1) in [(-56.6, -6, -56.2, 8), (-43.9, -11, -43.5, -0.2), (-43.9, 2, -43.5, 13)]:
        k.box("railtrack", x0, 0, z0, x1, 0.06, z1)
    for (x, z) in ((-56.4, 4.0), (-50.0, -10.6)):                                   # the cars (1.6 x 2.2, 1.2 high)
        if abs(z) > 10: k.box("paintedredbrick", x - 1.1, 0, z - 0.8, x + 1.1, 1.2, z + 0.8)
        else: k.box("paintedredbrick", x - 0.8, 0, z - 1.1, x + 0.8, 1.2, z + 1.1)
    # the facade (east): a false front to 9 m with a pediment
    k.box("rottingwood", GX1, 4.9, GZ0, GX1 + 0.4, 8.0, GZ1)
    k.cut("rottingwood", (GX1, 8.0, GZ0, GX1 + 0.4, 11.0, GZ1), [(0, 1, 3.0 / 13.0, 8.0 + 3.0 / 13.0 * 1.0 + 3.0), (0, 1, -3.0 / 13.0, 8.0 - 3.0 / 13.0 * 1.0 + 3.0)])

    # ================================================================== the stall avenues: 8 stalls on each side of
    # each avenue (z -16..24, every 5 m), counters facing the avenue, back doors, one in three boarded up
    i = 0
    CUP = {0, 7, 12, 19, 30}                         # five of the boarded stalls are shuttered cupboards (hide)
    for (xw, xe) in ((-36, -22), (18, 32)):
        for j in range(8):
            z0 = -16 + 5 * j
            stall(k, xw, z0, 1, "chipboard", i % 3 == 0, n=i, cupboard=i in CUP); i += 1
            stall(k, xe, z0, -1, "chipboard", i % 3 == 1, n=i, cupboard=i in CUP); i += 1

    # ================================================================== the hall of mirrors (east, x 40..54, z -12..2):
    # a 7 x 7 maze of 2 m cells (1.7 m corridors), mirror-tile walls (solid), a few breakable mirrors; in from the west,
    # out to the south; a roof at 3.3
    MX, MZ, C, N = 40.0, -12.0, 2.0, 7
    open_ = maze(19, N)
    # braided (fix round): every inner wall where (gx * 7 + gz * 3) % 9 == 4 opens too, so the maze has cycles a runner
    # can loop a tagger round; a third door east
    for gx in range(N):
        for gz in range(N):
            for (dx, dz) in ((1, 0), (0, 1)):
                q = (gx + dx, gz + dz)
                if q[0] < N and q[1] < N and (gx * 7 + gz * 3 + dx) % 9 == 4:
                    open_.add(frozenset(((gx, gz), q)))
    open_ |= {frozenset(((0, 3), (-1, 3))), frozenset(((3, N - 1), (3, N))), frozenset(((N - 1, 2), (N, 2)))}   # the doors
    brk = 0
    for gx in range(N + 1):
        for gz in range(N):                                                       # walls along z (x = const)
            if frozenset(((gx - 1, gz), (gx, gz))) in open_: continue
            x = MX + gx * C
            mat = "mirror" if (gx * 7 + gz * 3) % 5 == 2 and 0 < gx < N else "mirrortile"
            t = 0.12 if mat == "mirror" else 0.3
            k.box(mat, x - t / 2, 0, MZ + gz * C - 0.15, x + t / 2, 3.0, MZ + (gz + 1) * C + 0.15)
    for gz in range(N + 1):
        for gx in range(N):                                                       # walls along x (z = const)
            if frozenset(((gx, gz - 1), (gx, gz))) in open_: continue
            z = MZ + gz * C
            mat = "mirror" if (gx * 5 + gz * 7) % 5 == 3 and 0 < gz < N else "mirrortile"
            t = 0.12 if mat == "mirror" else 0.3
            k.box(mat, MX + gx * C - 0.15, 0, z - t / 2, MX + (gx + 1) * C + 0.15, 3.0, z + t / 2)
    k.box("chippedpaint", MX - 0.3, 3.0, MZ - 0.3, MX + N * C + 0.3, 3.3, MZ + N * C + 0.3)
    k.box("chippedpaint", MX - 0.3, 3.3, MZ - 0.3, MX + N * C + 0.3, 5.0, MZ - 0.1)    # (its sign board)
    k.box("neonmaze", MX + 1.0, 3.6, MZ - 0.38, MX + N * C - 1.0, 4.8, MZ - 0.3)

    # ================================================================== the dodgems (north-east, x 24..48, z -44..-26):
    # a floor 0.3 up, rubber barriers (gaps at each side), a roof on six posts at 5, the cars
    k.box("steelcheckerfloor", 24, 0, -44, 48, 0.3, -26)
    for (x0, z0, x1, z1) in [(24, -44, 33, -43.7), (39, -44, 48, -43.7), (24, -26.3, 33, -26), (39, -26.3, 48, -26),
                             (24, -43.7, 24.3, -38), (24, -32, 24.3, -26.3), (47.7, -43.7, 48, -38), (47.7, -32, 48, -26.3)]:
        k.box("rubber", x0, 0.3, z0, x1, 1.0, z1)
    for x in (24.2, 36, 47.8):
        for z in (-43.8, -26.2):
            k.box("galvanizedsteel", x - 0.2, 0.3, z - 0.2, x + 0.2, 5.0, z + 0.2)
    k.box("corrugated", 23.6, 5.0, -44.4, 48.4, 5.3, -25.6)
    for (z0, z1) in ((-44.5, -44.4), (-25.6, -25.5)):
        k.box("neonmaze", 23.6, 5.0, z0, 48.4, 5.6, z1)                               # neon along the roof's edges
    for z in (-39.5, -30.5):
        k.box("lightblock", 26, 4.7, z - 0.15, 46, 5.0, z + 0.15)                      # light strips under the roof
    for (x, z, ax) in ((29, -40, "x"), (35, -31, "z"), (42, -38, "x"), (30, -30, "x"), (43, -30, "z"), (37, -40, "z")):
        if ax == "x": k.box("powdercoatgreen", x - 1.1, 0.3, z - 0.8, x + 1.1, 1.3, z + 0.8)
        else: k.box("powdercoatgreen", x - 0.8, 0.3, z - 1.1, x + 0.8, 1.3, z + 1.1)

    # ================================================================== the helter-skelter (south-east): the tower
    # (x 43..49, z 31..37, 12 m: a door west, a ladder up inside to the top platform), the slide round it: two laps
    # of four ramps (1.5 m down each) with flat corners, from the top down to the ground
    HX0, HZ0, HX1, HZ1, W = 43.0, 31.0, 49.0, 37.0, 2.4
    wall_x(k, "stripedwallpaper", HX0, HX1, HZ0 + 0.15, 0, 12.0, [(46, 1.6, 2.6)], t=0.3)      # (the door: north)
    for (x0, z0, x1, z1) in [(HX0, HZ1 - 0.3, HX1, HZ1), (HX1 - 0.3, HZ0 + 0.3, HX1, HZ1 - 0.3)]:
        k.box("stripedwallpaper", x0, 0, z0, x1, 12.0, z1)
    k.box("stripedwallpaper", HX0, 0, HZ0 + 0.3, HX0 + 0.3, 12.0, HZ1 - 0.3)
    # the stair inside (fix round: was a ladder): a square spiral of eight 2 m flights round a 2 m core, 1.5 m a flight,
    # landings in the corners; in at the north door, out at the top onto the platform's north-east corner
    k.box("stripedwallpaper", 45.0, 0, 33.0, 47.0, 12.0, 35.0)                       # the core
    I0, J0, I1, J1 = HX0 + 0.3, HZ0 + 0.3, HX1 - 0.3, HZ1 - 0.3                      # (inside: 43.3..48.7, 31.3..36.7)
    sides = [("E", (45.0 + 2.0, 33.0, I1, 35.0), "z", 1), ("S", (45.0, 35.0, 47.0, J1), "x", -1),
             ("W", (I0, 33.0, 45.0, 35.0), "z", -1), ("N", (45.0, J0, 47.0, 33.0), "x", 1)]
    lands = {"E": (47.0, 35.0, I1, J1), "S": (I0, 35.0, 45.0, J1), "W": (I0, J0, 45.0, 33.0), "N": (47.0, J0, I1, 33.0)}
    y = 0.0
    for f in range(8):
        name, (x0, z0, x1, z1), ax, rising = sides[f % 4]
        k.stairs("steeldeckplate", x0, z0, x1, z1, y + 1.5, ax, rising, base=y, solid=(f < 3))
        if f in (1, 2): k.box("steeldeckplate", x0, 0, z0, x1, y, z1)                 # (the first lap solid to the ground)
        y += 1.5
        if f < 7:
            lx0, lz0, lx1, lz1 = lands[name]
            k.box("steeldeckplate", lx0, y - 0.3 if f >= 3 else 0, lz0, lx1, y, lz1)
    # (the top: a platform over the tower and the first corner, open over the last two flights)
    for (x0, z0, x1, z1) in minus((HX0, HZ0, HX1, HZ1), [(I0, J0, 45.0, 35.6), (45.0, J0, 47.0, 33.0), (45.0, 33.0, 47.0, 35.0)]):
        k.box("steeldeckplate", x0, 11.7, z0, x1, 12.0, z1)
    for (x0, z0, x1, z1) in [(I0, 35.6, 45.0, 35.72), (45.0, 33.0, 45.12, 35.6), (45.0, 33.0, 47.0, 33.12)]:
        k.box("chrome", x0, 12.0, z0, x1, 13.0, z1)                                 # rails round the stairwell
    k.box("steeldeckplate", HX1, 11.7, HZ1, HX1 + W, 12.0, HZ1 + W)                   # (the slide's top corner)
    cone(k, "stripedwallpaper", (HX0 + HX1) / 2, (HZ0 + HZ1) / 2, 5.0, 14.0, 17.0, n=4, a0=-45)
    for (x, z) in ((HX0, HZ0), (HX1, HZ0), (HX0, HZ1), (HX1, HZ1)):
        k.box("galvanizedsteel", x - 0.1, 12.0, z - 0.1, x + 0.1, 14.0, z + 0.1)

    def sloped(mat, rect, rising, ylo, yhi, top, bot):
        """the slab between (the slope + top) and (the slope + bot): the slope from ylo on the low side of rect to yhi
        on its rising side ('+x' '-x' '+z' '-z')"""
        x0, z0, x1, z1 = rect
        L = (x1 - x0) if rising in ("+x", "-x") else (z1 - z0)
        sl = (yhi - ylo) / L
        if rising == "+x": n, c = (-sl, 1, 0), ylo - sl * x0
        elif rising == "-x": n, c = (sl, 1, 0), ylo + sl * x1
        elif rising == "+z": n, c = (0, 1, -sl), ylo - sl * z0
        else: n, c = (0, 1, sl), ylo + sl * z1
        k.cut(mat, (x0, ylo + bot - 0.05, z0, x1, yhi + top + 0.05, z1),
              [(n[0], n[1], n[2], c + top), (-n[0], -n[1], -n[2], -(c + bot))])

    # the slide: corners (squares W x W at the tower's corners, their outer corner cut round, railed on the outer
    # edges) and ramps along the faces (a trough: the deck, a chrome rail on the outer edge, a 0.6 m skirt under it),
    # going round counter-clockwise seen from above (east face north, north face west, west face south, south face east)
    corners = [(HX1, HZ1), (HX1, HZ0 - W), (HX0 - W, HZ0 - W), (HX0 - W, HZ1)]      # SE, NE, NW, SW (x0, z0 of each)
    outer = [(1, 1), (1, -1), (-1, -1), (-1, 1)]                                     # their outer corner's side
    ch = 1.0
    y = 12.0
    seg = 0
    edge_pts = []
    while y > 0.1:
        c = corners[seg % 4]; ox, oz = outer[seg % 4]
        cx0, cz0, cx1, cz1 = c[0], c[1], c[0] + W, c[1] + W
        xo, zo = (cx1 if ox > 0 else cx0), (cz1 if oz > 0 else cz0)                   # the outer corner
        xi, zi = (cx0 if ox > 0 else cx1), (cz0 if oz > 0 else cz1)
        if seg > 0:
            k.prism("powdercoatyellow", [(xi, zi), (xo, zi), (xo, zo - oz * ch), (xo - ox * ch, zo), (xi, zo)], y - 0.3, y)
        # its rails: the two outer edges and the cut between them; a skirt under them
        for (mat, ya, yb) in (("chrome", y, y + 1.0), ("powdercoatyellow", y - 0.9, y - 0.3)):
            k.box(mat, min(xo, xo - ox * 0.12), ya, min(zi, zo - oz * ch), max(xo, xo - ox * 0.12), yb, max(zi, zo - oz * ch))
            k.box(mat, min(xi, xo - ox * ch), ya, min(zo, zo - oz * 0.12), max(xi, xo - ox * ch), yb, max(zo, zo - oz * 0.12))
            k.bar(mat, (xo - ox * 0.06, zo - oz * ch), (xo - ox * ch, zo - oz * 0.06), 0.12, ya, yb)
        edge_pts += [(xo - ox * 0.06, y + 1.08, zi), (xo - ox * 0.06, y + 1.08, zo - oz * ch), (xo - ox * ch, y + 1.08, zo - oz * 0.06),
                     (xi, y + 1.08, zo - oz * 0.06)]
        y1 = max(0.0, y - 1.5)
        nxt = (seg + 1) % 4
        if nxt == 1:   # east face, going north: x HX1..HX1+W, z HZ0..HZ1 (rising south)
            deck, rail, rising = (HX1, HZ0, HX1 + W, HZ1), (HX1 + W - 0.12, HZ0, HX1 + W, HZ1), "+z"
        elif nxt == 2:  # north face, going west: z HZ0-W..HZ0, x HX0..HX1 (rising east)
            deck, rail, rising = (HX0, HZ0 - W, HX1, HZ0), (HX0, HZ0 - W, HX1, HZ0 - W + 0.12), "+x"
        elif nxt == 3:  # west face, going south: x HX0-W..HX0, z HZ0..HZ1 (rising north)
            deck, rail, rising = (HX0 - W, HZ0, HX0, HZ1), (HX0 - W, HZ0, HX0 - W + 0.12, HZ1), "-z"
        else:           # south face, going east: z HZ1..HZ1+W, x HX0..HX1 (rising west)
            deck, rail, rising = (HX0, HZ1, HX1, HZ1 + W), (HX0, HZ1 + W - 0.12, HX1, HZ1 + W), "-x"
        sloped("powdercoatyellow", deck, rising, y1, y, 0.0, -0.3)
        sloped("chrome", rail, rising, y1, y, 1.0, 0.0)
        sloped("powdercoatyellow", rail, rising, y1, y, -0.3, -0.9)
        # bulbs along the rail's top
        rx0, rz0, rx1, rz1 = rail
        mx, mz = (rx0 + rx1) / 2, (rz0 + rz1) / 2
        if rising in ("+z", "-z"):
            lo_z, hi_z = (rz0, rz1) if rising == "+z" else (rz1, rz0)
            edge_pts += along((mx, y1 + 1.08, lo_z), (mx, y + 1.08, hi_z), step=1.5, ends=False)
        else:
            lo_x, hi_x = (rx0, rx1) if rising == "+x" else (rx1, rx0)
            edge_pts += along((lo_x, y1 + 1.08, mz), (hi_x, y + 1.08, mz), step=1.5, ends=False)
        y = y1; seg += 1
    bulbs(k, edge_pts)

    # ================================================================== the bouncy castle (south-west, x -46..-34,
    # z 30..42): rubber floor (0.5), walls (2 m) with a way in on the east, rubber towers at the corners
    BC = (-46.0, 30.0, -34.0, 42.0)
    k.box("rubber", BC[0], 0, BC[1], BC[2], 0.5, BC[3])
    for (x0, z0, x1, z1) in [(BC[0], BC[1], BC[2], BC[1] + 0.6), (BC[0], BC[3] - 0.6, BC[2], BC[3]), (BC[0], BC[1], BC[0] + 0.6, BC[3]),
                             (BC[2] - 0.6, BC[1], BC[2], 34.5), (BC[2] - 0.6, 37.5, BC[2], BC[3])]:
        k.box("rubber", x0, 0.5, z0, x1, 2.5, z1)
    for (j, (x, z)) in enumerate(((BC[0], BC[1]), (BC[2] - 1.6, BC[1]), (BC[0], BC[3] - 1.6), (BC[2] - 1.6, BC[3] - 1.6))):
        k.box("rubber", x, 0.5, z, x + 1.6, 4.0, z + 1.6)
        k.box("powdercoatyellow" if j % 2 else "pinkbrick", x - 0.05, 4.0, z - 0.05, x + 1.65, 4.12, z + 1.65)   # coloured caps
        k.cut("pinkbrick" if j % 2 else "powdercoatyellow", (x + 0.3, 4.12, z + 0.3, x + 1.3, 5.0, z + 1.3),
              [(1, 0.6, 0, x + 1.3 + 0.6 * 4.12), (-1, 0.6, 0, -(x + 0.3) + 0.6 * 4.12), (0, 0.6, 1, z + 1.3 + 0.6 * 4.12), (0, 0.6, -1, -(z + 0.3) + 0.6 * 4.12)])
    # coloured bands on the walls' outer faces and caps on their tops (the rubber stays where players touch it)
    for (x0, z0, x1, z1, mat) in [(BC[0], BC[1] - 0.05, BC[2], BC[1], "powdercoatyellow"), (BC[0], BC[3], BC[2], BC[3] + 0.05, "pinkbrick"),
                                  (BC[0] - 0.05, BC[1], BC[0], BC[3], "powdercoatgreen")]:
        k.box(mat, x0, 1.0, z0, x1, 1.6, z1)
    for (x0, z0, x1, z1) in [(BC[0], BC[1], BC[2], BC[1] + 0.6), (BC[0], BC[3] - 0.6, BC[2], BC[3]), (BC[0], BC[1], BC[0] + 0.6, BC[3])]:
        k.box("pinkbrick", x0, 2.5, z0, x1, 2.56, z1)

    # ================================================================== the duck pond (pink slime: throws you out)
    # and the drinks stand with its spill (fizzing soda: a big jump), the strength tester (a hook), the gate
    k.box("bluepooltile", POND[0], -1.2, POND[1], POND[2], -0.8, POND[3])
    for (x0, z0, x1, z1) in [(POND[0] - 0.3, POND[1] - 0.3, POND[2] + 0.3, POND[1]), (POND[0] - 0.3, POND[3], POND[2] + 0.3, POND[3] + 0.3),
                             (POND[0] - 0.3, POND[1], POND[0], POND[3]), (POND[2], POND[1], POND[2] + 0.3, POND[3])]:
        k.box("bluepooltile", x0, -1.2, z0, x1, 0.4, z1)
    k.box("pinkslime", POND[0], -0.8, POND[1], POND[2], -0.2, POND[3])
    k.box("hardpan", SPILL[0], -1.2, SPILL[1], SPILL[2], -0.3, SPILL[3])
    k.box("fizzingsoda", SPILL[0], -0.3, SPILL[1], SPILL[2], -0.05, SPILL[3])
    wall_x(k, "chipboard", 24, 30, 33.5, 0, 3.0, t=0.12)
    k.box("cratewood", 24, 0, 32.4, 30, 1.1, 33.4)
    k.box("stripedwallpaper", 23.6, 3.0, 32.0, 30.4, 3.2, 34.0)
    k.box("galvanizedsteel", 13.6, 0, 40.6, 14.4, 10.0, 41.4)                          # the strength tester
    k.box("powdercoatyellow", 13.4, 9.0, 40.4, 14.6, 10.2, 41.6)
    for x in (-6, 6):                                                                 # the gate's posts and arch
        k.box("stripedwallpaper", x - 0.6, 0, 46.8, x + 0.6, 7.0, 48.0)
    k.box("stripedwallpaper", -6.6, 7.0, 46.8, 6.6, 8.4, 48.0)
    k.box("neonmaze", -5.6, 7.15, 46.72, 5.6, 8.25, 46.8)                             # the gate's neon name board
    bulbs(k, along((-6.6, 8.5, 46.75), (6.6, 8.5, 46.75), step=1.1))

    # ================================================================== the open ground broken up (cover for the chase,
    # few large pieces: momentum stays): food trucks and picnic tables east, a hay-bale maze west, the games marquee
    # and the teacups south, generator trucks north-east of the wheel
    for (x0, z0) in ((44, 8), (44, 16), (50, 24)):
        k.box("powdercoatgreen" if x0 == 44 else "powdercoatyellow", x0, 0.4, z0, x0 + 7, 3.2, z0 + 2.6)
        k.box("rubber", x0 + 0.6, 0, z0 + 0.1, x0 + 1.6, 0.6, z0 + 2.5); k.box("rubber", x0 + 5.4, 0, z0 + 0.1, x0 + 6.4, 0.6, z0 + 2.5)
        k.box("chipboard", x0 + 1.5, 3.2, z0 - 0.8, x0 + 5.5, 3.3, z0)                 # the serving hatch's awning
    for (x, z) in ((40, 22), (40, 29), (54, 3)):
        k.box("wideplanks", x - 1.5, 0.7, z - 0.8, x + 1.5, 0.8, z + 0.8)
        k.box("wideplanks", x - 1.4, 0, z - 0.1, x + 1.4, 0.7, z + 0.1)
    HAY = [(-58, 18, -54, 19.2), (-52, 18, -46, 19.2), (-58, 22, -50, 23.2), (-48, 21, -46.8, 27), (-55, 26, -51, 27.2), (-58, 18, -56.8, 30)]
    for (x0, z0, x1, z1) in HAY:
        k.box("haybales", x0, 0, z0, x1, 1.4, z1)
    k.box("haybales", -53, 1.4, 22, -51, 2.4, 23.2)
    # the games marquee (x -26..-12, z 32..42): a striped roof on posts, open sides, trestle tables
    for x in (-26, -19, -12):
        for z in (32, 42):
            k.box("galvanizedsteel", x - 0.1, 0, z - 0.1, x + 0.1, 3.4, z + 0.1)
    shell(k, "stripedwallpaper", (-26.4, 3.4, 31.6, -12, 5.2, 37.0), "+z", t=0.15)
    shell(k, "stripedwallpaper", (-26.4, 3.4, 37.0, -12, 5.2, 42.4), "-z", t=0.15)
    for z in (35, 39):
        k.box("cratewood", -24, 0, z - 0.5, -14, 0.9, z + 0.5)
    for z in (34.0, 40.0):
        k.box("lightblock", -25, 3.6, z - 0.15, -13, 3.9, z + 0.15)                    # light strips under the roof
    # the teacups (centre 22 40): a turntable 0.3 up, five cups (1.1 m: vault them)
    k.ring("wideplanks", 22, 40, 0, 6.0, 0, 0.3, n=12)
    for i in range(5):
        a = math.radians(72 * i + 10)
        k.ring("pinkbrick" if i % 2 else "powdercoatgreen", 22 + 3.6 * math.cos(a), 40 + 3.6 * math.sin(a), 0.9, 1.1, 0.3, 1.4, n=8)
    # generator trucks north-east of the wheel (x 14..21, z -46..-38)
    for z0 in (-46, -41.5):
        k.box("shippingcontainer", 14, 0, z0, 21, 2.8, z0 + 2.6)

    # ================================================================== lights (night: warm lamps, a few colours)
    # (fix round: the rides brighter, the open ground dimmer, so the lit rides stand out)
    L = [(0, 4.5, 0, 14, 0.85), (0, 10, -30, 20, 0.85), (-27, 4, -14, 12, 0.65), (-27, 4, 4, 12, 0.65), (-27, 4, 22, 12, 0.65),
         (27, 4, -14, 12, 0.65), (27, 4, 4, 12, 0.65), (27, 4, 22, 12, 0.65), (BX, 8, BZ, 16, 0.8), (-50, 3.5, -10.5, 6, 0.35),
         (-56.4, 3.5, 0, 6, 0.35), (-50, 3.5, 12.5, 6, 0.35), (-43.7, 3.5, -5, 5, 0.3), (47, 2.6, -5, 9, 0.6), (36, 4.5, -35, 14, 0.8),
         (46, 9, 34, 12, 0.8), (-40, 5, 36, 12, 0.75), (0, 4, 33, 10, 0.7), (27, 2.8, 30, 8, 0.6), (0, 7, 46, 10, 0.7),
         (-12, 5, -20, 12, 0.4), (14, 5, -20, 12, 0.4), (-12, 5, 20, 12, 0.4), (14, 5, 20, 12, 0.4), (-50, 5, 26, 10, 0.3),
         (50, 5, 12, 10, 0.3), (50, 5, -24, 10, 0.3), (-50, 5, -46, 10, 0.3), (14, 5, 44, 10, 0.3), (-20, 5, 44, 10, 0.3),
         (12, 16, -36, 10, 0.5), (-12, 16, -36, 10, 0.5), (54, 5, 46, 10, 0.3), (-56, 5, 46, 10, 0.3), (56, 5, -46, 10, 0.3),
         (46, 6, 34, 10, 0.6), (22, 3, 40, 9, 0.6)]
    for (x, y, z, r, i) in L:
        k.light(x, y, z, r, i)

    # ================================================================== spawns (8, any): each near two loops
    for (x, z, tx, tz) in [(-12, -12, 0, 0), (12, 12, 0, 0), (-12, 14, -27, 14), (12, -14, 27, -14), (-42, -16, -36, -30),
                           (38, 10, 46, 34), (-20, 40, -40, 36), (24, -20, 36, -35)]:
        k.spawn("any", x, 0, z, yaw_to(x, z, tx, tz))
    m.save("fairground", dress=False)
