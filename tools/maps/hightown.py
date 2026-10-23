"""HIGHTOWN: built for hide and seek (tag too). The top five floors of a skyscraper and its roof, high over a
city far below: offices round an open atrium, corner rooms and storerooms to hide in, and two big switchback
staircases (2.6 m flights with landings) hugging the west and east facades from the lobby to the roof, so
there are always two separate ways between floors on opposite sides of the building; windows on every side (the panes are invisible but solid) and
the roof terrace with its pool, plant and helipad. Step off the roof and it's a long way down."""
from .prefabs import *


def build():
    m = M("Hightown", "hide", sun=(0.5, 1.0, 0.35))
    m.also = ["tag"]
    m.raw("void -12")
    HT, FH = 20, 4.5                        # half size, floor to floor
    FLOORS = [0, 4.5, 9, 13.5, 18]
    ROOF = 22.5
    ATRIUM = (-5, -5, 5, 5)
    STAIR_Z = 4.5                           # flights run z -4.5..4.5 beside the facade, landings beyond both ends
    def flights(k):  # the two flights up from floor k: (rect, axis rise along z); west lane A (by the windows) on even
                     # floors rising +z, lane B (inboard) on odd ones rising back -z; the east stair is the same turned 180
        w = (-19.6, -STAIR_Z, -17.0, STAIR_Z, 1) if k % 2 == 0 else (-17.0, -STAIR_Z, -14.4, STAIR_Z, -1)
        return [w, (-w[2], -w[3], -w[0], -w[1], -w[4])]
    for k, y in enumerate(FLOORS + [ROOF]):
        holes = [f[:4] for f in flights(k - 1)] if k > 0 else []   # (the stairwell over the flight below)
        if 0 < k < len(FLOORS): holes.append(ATRIUM)
        for r in tiles(minus((-HT, -HT, HT, HT), holes), 10): m.box(r[0], y - 0.4, r[1], r[2], y, r[3])
    for k, y in enumerate(FLOORS):
        top = y + FH - 0.4                  # (the ceiling: the next slab's underside)
        # flights to the floor above, a rail along their open side
        for (x0, z0, x1, z1, rise) in flights(k):
            m.stairs(x0, z0, x1, z1, y + FH, "z", rise, base=y, solid=k == 0)   # (upper flights: slabs, not blocks to the lobby)
        for sx in (-1, 1):   # rails: the inboard side of the staircase, and the drop end of the stairwell from below
            x_in = sorted((sx * 14.4, sx * 14.2)); m.box(x_in[0], y, -STAIR_Z, x_in[1], y + 1.0, STAIR_Z, accent=True)
            if k > 0:
                (hx0, hz0, hx1, hz1, rise) = flights(k - 1)[(sx + 1) // 2]
                ze = hz0 if rise > 0 else hz1   # the foot of the flight below: the open end
                m.box(hx0, y, ze - 0.2 if ze == hz0 else ze, hx1, y + 1.0, ze if ze == hz0 else ze + 0.2, accent=True)
        # the facade: sill, lintel and piers every 4 m on all four sides; the window openings are clip (you can see
        # and shoot out, nobody falls out)
        for (a0, a1, side, axis) in ((-HT, HT, -HT, "x"), (-HT, HT, HT, "x"), (-HT, HT, -HT, "z"), (-HT, HT, HT, "z")):
            t0, t1 = (side - 0.4, side) if side < 0 else (side, side + 0.4)
            def wbox(p0, y0, p1, y1, mat=None):
                if axis == "x": m.box(p0, y0, t0, p1, y1, t1, mat=mat)
                else: m.box(t0, y0, p0, t1, y1, p1, mat=mat)
            wbox(a0 - (0.4 if axis == "x" else 0), y, a1 + (0.4 if axis == "x" else 0), y + 0.9)
            wbox(a0, y + 3.3, a1, y + FH)
            for p in range(int(a0), int(a1) + 1, 4): wbox(p - 0.3 if p > a0 else p, y + 0.9, p + 0.3 if p < a1 else p, y + 3.3)
            wbox(a0, y + 0.9, a1, y + 3.3, mat="clip")
            wbox(a0, y + 0.85, a1, y + 0.9, mat="trim")
        # the atrium rail (floors above the lobby), gaps where nothing is
        if k > 0:
            for (a, b, c, d) in ((-5.15, -5.15, 5.15, -5), (-5.15, 5, 5.15, 5.15), (-5.15, -5, -5, 5), (5, -5, 5.15, 5)):
                m.box(a, y, b, c, y + 1.0, d, accent=True)
        # corner rooms (|x|, |z| > 12) behind partitions with a door each, the wings between them open plan
        for sx in (-1, 1):
            for sz in (-1, 1):
                zs = sorted((sz * 12, sz * HT)); xs = sorted((sx * 12, sx * HT))
                wx0, wx1 = sorted((sx * 12, sx * 12 + sx * 0.2))
                wz0, wz1 = sorted((sz * 12, sz * 12 + sz * 0.2))
                dz = sz * 16; dx = sx * 16
                for (q0, q1) in ((zs[0], dz - 0.8), (dz + 0.8, zs[1])): m.box(wx0, y, q0, wx1, top, q1)
                m.box(wx0, y + 2.4, dz - 0.8, wx1, top, dz + 0.8)
                for (q0, q1) in ((xs[0], dx - 0.8), (dx + 0.8, xs[1])): m.box(q0, y, wz0, q1, top, wz1)
                m.box(dx - 0.8, y + 2.4, wz0, dx + 0.8, top, wz1)
        # lights: a grid of ceiling panels
        # ((+-12, 0): beside the stairwells, not hanging in them at head height over the flights)
        for (lx, lz) in ((-12, 0), (12, 0), (0, -16), (0, 16), (-16, -16), (16, 16), (16, -16), (-16, 16), (-8, -8), (8, 8)):
            m.light(lx, top - 0.5, lz, 10, 0.9)
    # spawns: hiders anywhere in the building, every floor and the roof (the furniture keeps clear of them)
    HT_SPAWNS = ((-10, 0, 0, 90), (15, 0, 6, -90), (0, 4.5, -15, 0), (-8, 4.5, 15, 180), (15, 9, -6, -90), (-15, 9, 6, 90),
                 (6, 13.5, 15, 180), (-6, 13.5, -15, 0), (-15, 18, -6, 90), (15, 18, 6, -90), (0, ROOF, -10, 0), (-2, ROOF, 15, 180))
    R = random.Random("hightown")
    def free(x0, z0, x1, z1, k):
        keep = [ATRIUM] + [(-19.6, -STAIR_Z - 4, -14.0, STAIR_Z + 4), (14.0, -STAIR_Z - 4, 19.6, STAIR_Z + 4)]   # the stairs and their landings
        keep += [(-12.4, 15, -11.8, 17), (11.8, -17, 12.4, -15), (-17, -12.4, -15, -11.8), (15, 11.8, 17, 12.4),
                 (-12.4, -17, -11.8, -15), (11.8, 15, 12.4, 17), (-17, 11.8, -15, 12.4), (15, -12.4, 17, -11.8)]   # partition doors
        keep += [(sx - 0.8, sz - 0.8, sx + 0.8, sz + 0.8) for (sx, sy, sz, _) in HT_SPAWNS if sy == FLOORS[k]]
        keep += [(-5.6, -5.6, 5.6, 5.6)]
        keep += [(-20, -20, 20, -19.2), (-20, 19.2, 20, 20), (-20, -20, -19.2, 20), (19.2, -20, 20, 20)]   # (the facade)
        return not any(x0 < b[2] + 0.6 and x1 > b[0] - 0.6 and z0 < b[3] + 0.6 and z1 > b[1] - 0.6 for b in keep)
    for k, y in enumerate(FLOORS):
        # office furniture: desks with screens in the wings, cabinets, sofas, plants (cover and things to hide behind)
        for (x0, z0, x1, z1) in ((-11, -19, 11, -12.6), (-11, 12.6, 11, 19), (-19, -11, -12.6, 11), (12.6, -11, 19, 11)):
            for xx in range(int(x0) + 1, int(x1) - 2, 4):
                for zz in range(int(z0) + 1, int(z1) - 1, 3):
                    if R.random() < 0.3 or not free(xx, zz, xx + 2.2, zz + 1.1, k): continue
                    m.box(xx, y, zz, xx + 2.2, y + 0.8, zz + 1.0, accent=True); m.metal(xx, y, zz + 1.0, xx + 2.2, y + 1.5, zz + 1.1)
        for (x0, z0, x1, z1) in ((-19, -19, -12.6, -12.6), (12.6, 12.6, 19, 19), (12.6, -19, 19, -12.6), (-19, 12.6, -12.6, 19)):
            for _ in range(6):   # the corner rooms: storerooms of crates, cabinets and boxes to crawl among
                w, d, h = R.choice([(1.2, 1.2, 1.2), (2.0, 0.6, 2.0), (0.8, 0.8, 1.6), (1.6, 1.0, 1.0)])
                cx, cz = R.uniform(x0, x1 - w), R.uniform(z0, z1 - d)
                if free(cx, cz, cx + w, cz + d, k): m.box(cx, y, cz, cx + w, y + h, cz + d, accent=R.random() < 0.5)
        for (px, pz) in ((-6.5, -9), (6.5, 9), (-9, 6.5), (9, -6.5)):    # plants by the atrium
            if free(px - 0.4, pz - 0.4, px + 0.4, pz + 0.4, k):
                m.box(px - 0.45, y, pz - 0.45, px + 0.45, y + 0.6, pz + 0.45, accent=True); m.glow(px - 0.3, y + 0.6, pz - 0.3, px + 0.3, y + 1.8, pz + 0.3)
    # the lobby (bottom floor): reception desk and a fountain under the atrium
    m.box(-3, 0, -3, 3, 0.5, 3, accent=True); m.box(-2.4, 0.5, -2.4, 2.4, 0.6, 2.4, mat="water"); m.glow(-0.3, 0.5, -0.3, 0.3, 2.0, 0.3)
    m.box(-8, 0, 6, -3, 1.1, 7, accent=True); m.box(-8, 1.1, 6, -3, 1.2, 7, mat="trim")
    for y in FLOORS[1:]: m.glow(-5.1, y - 0.45, -5.1, 5.1, y - 0.4, -5)   # light strips under the atrium edges
    m.light(0, 3.0, 0, 12, 1.0, fixture=False); m.light(0, 12, 0, 14, 0.9, fixture=False)
    # the roof: parapet, plant room, water tanks, a pool and the helipad
    for (a, b, c, d) in ((-17.0, -STAIR_Z, -16.8, STAIR_Z), (16.8, -STAIR_Z, 17.0, STAIR_Z), (-19.6, -STAIR_Z - 0.2, -17.0, -STAIR_Z), (17.0, STAIR_Z, 19.6, STAIR_Z + 0.2)):   # stairwell rails
        m.box(a, ROOF, b, c, ROOF + 1.0, d, accent=True)
    for (a, b, c, d) in ((-HT, -HT, HT, -HT + 0.4), (-HT, HT - 0.4, HT, HT), (-HT, -HT, -HT + 0.4, HT), (HT - 0.4, -HT, HT, HT)):
        m.box(a, ROOF, b, c, ROOF + 1.2, d, accent=True)
    m.box(-11, ROOF, -3, -6, ROOF + 3.2, 4)                                    # plant room (a door in its side)
    m.box(-11.2, ROOF, -1, -11, ROOF + 2.4, 2, mat="trim")
    for (tx, tz) in ((6, -15), (12, -15)):
        for (dx, dz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)): m.metal(tx + dx * 1.1 - 0.15, ROOF, tz + dz * 1.1 - 0.15, tx + dx * 1.1 + 0.15, ROOF + 2.0, tz + dz * 1.1 + 0.15)
        m.box(tx - 1.6, ROOF + 2.0, tz - 1.6, tx + 1.6, ROOF + 5.0, tz + 1.6); m.box(tx - 1.8, ROOF + 5.0, tz - 1.8, tx + 1.8, ROOF + 5.3, tz + 1.8, accent=True)
    for (a, b, c, d) in ((6, 8, 18, 8.4), (6, 17.6, 18, 18), (6, 8, 6.4, 18), (17.6, 8, 18, 18)): m.box(a, ROOF, b, c, ROOF + 0.6, d, accent=True)
    m.box(6.4, ROOF + 0.35, 8.4, 17.6, ROOF + 0.4, 17.6, mat="water")           # the pool (wade in it)
    m.box(-19.2, ROOF, 12.2, -12.2, ROOF + 0.3, 19.2, mat="metal")   # helipad (clear of the stairwell), its "H"
    for hx in (-17.4, -14.0): m.glow(hx, ROOF + 0.3, 13.6, hx + 0.6, ROOF + 0.32, 17.8)
    m.glow(-16.8, ROOF + 0.3, 15.4, -14.0, ROOF + 0.32, 16.0)
    for (ax, az) in ((-2, -12), (2, -16), (-6, -17)):
        m.metal(ax - 1.2, ROOF, az - 0.9, ax + 1.2, ROOF + 1.4, az + 0.9); m.glow(ax - 1.25, ROOF + 0.7, az - 0.95, ax + 1.25, ROOF + 0.8, az + 0.95)
    for (lx, lz) in ((-15, 15), (15, -15), (0, 0), (12, 13)): m.light(lx, ROOF + 2.5, lz, 12, 0.7)
    # the tower's body below the playable floors, and the city far below (unlit 'far' scenery, beyond reach)
    m.box(-20.4, -160, -20.4, 20.4, -0.4, 20.4, mat="far")
    CR = random.Random("hightown/city")
    for _ in range(90):
        cx, cz = CR.uniform(-320, 320), CR.uniform(-320, 320)
        if abs(cx) < 45 and abs(cz) < 45: continue
        w, d = CR.uniform(12, 34), CR.uniform(12, 34)
        top = CR.uniform(-130, -30) if CR.random() < 0.85 else CR.uniform(-20, 30)   # a few neighbours nearly as tall
        m.box(cx, -160, cz, cx + w, top, cz + d, mat="far")
    m.box(-400, -161, -400, 400, -160, 400, mat="far")                        # the streets, 160 m down
    for (x, y, z, yaw) in HT_SPAWNS: m.spawn("any", x, y, z, yaw)
    m.raw("setting skyscraper over a city")
    m.save("hightown")
