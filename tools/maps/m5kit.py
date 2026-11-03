"""Building kit for the 1.19.6 maps Lighthouse, Terraces, Glacier and Spire: bigkit (its Kit, materials and helpers,
imported from there) plus their own helpers: rock fills with tunnels cut in them, floor slabs with holes, walls with
doors, beams, ramp slabs and boulders."""
import math
from .bigkit import Kit, MAT, num, mat_id, clip_poly, yaw_to   # (the shared kit: m5kit began as a copy of it)


# ---- maps-m5 helpers

def fill(k, mat, bounds, air):
    """fill bounds (x0 y0 z0 x1 y1 z1) with mat wherever no air box is (rock with tunnels and stairwells cut in it):
    a greedy merge of the grid the boxes cut, so no hidden hollow is left"""
    import bisect
    bx0, by0, bz0, bx1, by1, bz1 = bounds
    cl = []
    for a in air:
        c = (max(a[0], bx0), max(a[1], by0), max(a[2], bz0), min(a[3], bx1), min(a[4], by1), min(a[5], bz1))
        if c[3] - c[0] > 1e-4 and c[4] - c[1] > 1e-4 and c[5] - c[2] > 1e-4: cl.append(c)
    xs = sorted({bx0, bx1} | {v for c in cl for v in (c[0], c[3])})
    ys = sorted({by0, by1} | {v for c in cl for v in (c[1], c[4])})
    zs = sorted({bz0, bz1} | {v for c in cl for v in (c[2], c[5])})
    nx, ny, nz = len(xs) - 1, len(ys) - 1, len(zs) - 1
    full = [[[True] * nz for _ in range(ny)] for _ in range(nx)]
    for c in cl:
        i0, i1 = bisect.bisect_left(xs, c[0]), bisect.bisect_left(xs, c[3])
        j0, j1 = bisect.bisect_left(ys, c[1]), bisect.bisect_left(ys, c[4])
        k0, k1 = bisect.bisect_left(zs, c[2]), bisect.bisect_left(zs, c[5])
        for i in range(i0, i1):
            for j in range(j0, j1):
                for kk in range(k0, k1): full[i][j][kk] = False
    for i in range(nx):
        for j in range(ny):
            for kk in range(nz):
                if not full[i][j][kk]: continue
                k2 = kk
                while k2 + 1 < nz and full[i][j][k2 + 1]: k2 += 1
                i2 = i
                while i2 + 1 < nx and all(full[i2 + 1][j][q] for q in range(kk, k2 + 1)): i2 += 1
                j2 = j
                while j2 + 1 < ny and all(full[a][j2 + 1][q] for a in range(i, i2 + 1) for q in range(kk, k2 + 1)): j2 += 1
                for a in range(i, i2 + 1):
                    for b in range(j, j2 + 1):
                        for q in range(kk, k2 + 1): full[a][b][q] = False
                k.box(mat, xs[i], ys[j], zs[kk], xs[i2 + 1], ys[j2 + 1], zs[k2 + 1])


def rects_minus(rect, holes):
    """rect (x0, z0, x1, z1) less the holes, as rectangles"""
    out = [rect]
    for (hx0, hz0, hx1, hz1) in holes:
        nxt = []
        for (x0, z0, x1, z1) in out:
            if hx1 <= x0 or hx0 >= x1 or hz1 <= z0 or hz0 >= z1: nxt.append((x0, z0, x1, z1)); continue
            if hz0 > z0: nxt.append((x0, z0, x1, hz0))
            if hz1 < z1: nxt.append((x0, hz1, x1, z1))
            a, b = max(z0, hz0), min(z1, hz1)
            if hx0 > x0: nxt.append((x0, a, hx0, b))
            if hx1 < x1: nxt.append((hx1, a, x1, b))
        out = nxt
    return out


def slab(k, mat, x0, z0, x1, z1, y0, y1, holes=(), tile=None):
    """a floor slab with holes, cut into tiles of at most `tile` metres (smaller bake pieces)"""
    for (a, b, c, d) in rects_minus((x0, z0, x1, z1), holes):
        if tile:
            nx_, nz_ = max(1, math.ceil((c - a) / tile - 1e-6)), max(1, math.ceil((d - b) / tile - 1e-6))
            for i in range(nx_):
                for j in range(nz_):
                    k.box(mat, a + (c - a) * i / nx_, y0, b + (d - b) * j / nz_, a + (c - a) * (i + 1) / nx_, y1, b + (d - b) * (j + 1) / nz_)
        else: k.box(mat, a, y0, b, c, y1, d)


def walls(k, mat, x0, z0, x1, z1, y0, y1, doors=(), t=0.4, glass=None):
    """four walls round x0..x1, z0..z1 (outer faces), y0..y1; doors = [(side, centre, width, height[, sill])], side
    n (z0), s (z1), w (x0), e (x1); with a sill it is a window (wall below it; `glass` fills it if given)"""
    for side in "nswe":
        ds = sorted((c - w / 2, c + w / 2, h, (rest[0] if rest else 0)) for (sd, c, w, h, *rest) in doors if sd == side)
        if side in "ns":
            za, zb = (z0, z0 + t) if side == "n" else (z1 - t, z1)
            a, b = x0, x1
        else:
            za, zb = (x0, x0 + t) if side == "w" else (x1 - t, x1)
            a, b = z0 + t, z1 - t
        def put(m, p0, q0, p1, q1):
            if side in "ns": k.box(m, p0, q0, za, p1, q1, zb)
            else: k.box(m, za, q0, p0, zb, q1, p1)
        pos = a
        for (c0, c1, h, sill) in ds + [(b, b, 0, 0)]:
            if c0 > pos: put(mat, pos, y0, c0, y1)
            if c1 > c0:
                if y0 + h < y1: put(mat, c0, y0 + h, c1, y1)
                if sill > 0:
                    put(mat, c0, y0, c1, y0 + sill)
                    if glass: put(glass, c0, y0 + sill, c1, y0 + h)
            pos = max(pos, c1)


def beam(k, mat, x0, x1, a, b, w):
    """a straight beam along the y-z plane from a = (y, z) to b = (y, z), w thick, spanning x0..x1 (masts, leaning
    poles): a cut box of two planes"""
    (ay, az), (by, bz) = a, b
    dy, dz = by - ay, bz - az
    L = math.hypot(dy, dz)
    ny, nz = -dz / L, dy / L   # unit normal in y-z
    h = w / 2
    pts = [(ay + ny * h, az + nz * h), (ay - ny * h, az - nz * h), (by + ny * h, bz + nz * h), (by - ny * h, bz - nz * h)]
    ys = [p[0] for p in pts]; zs = [p[1] for p in pts]
    c = ny * ay + nz * az
    k.cut(mat, (x0, min(ys), min(zs), x1, max(ys), max(zs)), [(0, ny, nz, c + h), (0, -ny, -nz, -c + h)])


def ramp_slab(k, mat, x0, z0, x1, z1, y0, y1, rising, thick=0.6):
    """a sloped slab (hollow under it): its top from y0 to y1 rising toward rising ('+x' '-x' '+z' '-z'), thick deep"""
    a0, a1 = (x0, x1) if rising in ("+x", "-x") else (z0, z1)
    L = a1 - a0
    s = (y1 - y0) / L
    if rising in ("+x", "+z"):   # top y = y0 + s (a - a0)
        n = (-s, 1.0); d = y0 - s * a0
    else:                        # top y = y0 + s (a1 - a)
        n = (s, 1.0); d = y0 + s * a1
    nl = math.hypot(*n)
    nn = (n[0] / nl, n[1] / nl); dd = d / nl
    if rising in ("+x", "-x"): top = (nn[0], nn[1], 0, dd); bot = (-nn[0], -nn[1], 0, -dd + thick)
    else: top = (0, nn[1], nn[0], dd); bot = (0, -nn[1], -nn[0], -dd + thick)
    k.cut(mat, (x0, min(y0, y1) - thick * 1.2, z0, x1, max(y0, y1), z1), [top, bot])


def rock(k, mat, cx, cz, r, y0, h, seed=0, tilt=(0.2, 0.12)):
    """a boulder / outcrop: an irregular pentagon (radius r, wobbled by seed) from y0 up to about y0 + h with a tilted
    top (one brush)"""
    import random
    R = random.Random(f"rock{seed}{cx}{cz}")
    a0 = R.uniform(0, 2 * math.pi)
    pts = []
    for i in range(5):
        t = a0 + 2 * math.pi * (i + R.uniform(-0.15, 0.15)) / 5
        rr = r * R.uniform(0.8, 1.15)
        pts.append((cx + rr * math.cos(t), cz + rr * math.sin(t)))
    tx, tz = tilt
    L = math.sqrt(tx * tx + 1 + tz * tz)
    top = (tx / L, 1 / L, tz / L, (tx * cx + (y0 + h) + tz * cz) / L)
    k.prism(mat, pts, y0, y0 + h + r * (abs(tx) + abs(tz)) * 1.2, top=top)
