"""Building kit for the 1.19.6 maps Lighthouse, Terraces, Glacier, Amphitheatre and Spire (a copy of m4kit, maps-m5): named materials
(convex prisms, rotated bars, rings, octagonal tubes) written straight into an M builder's lines. Named materials
are the file keywords of src/game/map.h (kMapMaterialWords); slopes and cut boxes take the material's number, read
from the same list, so the two can never disagree."""
import math, os, re

_HERE = os.path.dirname(os.path.abspath(__file__))
_MAP_H = os.path.join(_HERE, "..", "..", "src", "game", "map.h")


def _materials():
    text = open(_MAP_H, encoding="utf-8").read()
    i = text.index("kMapMaterialWords[] = {")
    j = text.index("};", i)
    words = re.findall(r'"([a-z0-9]+)"', text[i:j])
    return {w: k for k, w in enumerate(words)}


MAT = _materials()


def num(v):
    """a coordinate or plane number, short and exact enough (no float noise in the file)"""
    v = round(float(v), 4)
    if v == 0: v = 0.0
    return f"{v:g}"


def mat_id(mat):
    if isinstance(mat, int): return mat
    if mat not in MAT: raise SystemExit(f"m5kit: no material '{mat}' in map.h")
    return MAT[mat]


class Kit:
    """drawing calls onto an M (prefabs.M): every call appends map lines"""
    def __init__(s, m):
        s.m = m
        s.count = 0

    def line(s, l):
        s.m.lines.append(l); s.count += 1

    # ---- plain boxes in any named material
    def box(s, mat, x0, y0, z0, x1, y1, z1):
        if isinstance(mat, int): mat = [k for k, v in MAT.items() if v == mat][0]
        if mat not in MAT: raise SystemExit(f"m5kit: no material '{mat}' in map.h")
        a, b = sorted((x0, x1)), sorted((z0, z1)); c = sorted((y0, y1))
        if a[1] - a[0] < 1e-4 or b[1] - b[0] < 1e-4 or c[1] - c[0] < 1e-4: return
        s.line(f"{mat} {num(a[0])} {num(c[0])} {num(b[0])} {num(a[1])} {num(c[1])} {num(b[1])}")

    # ---- slopes: wedge / corner / tetra (dir 0 +x, 1 -x, 2 +z, 3 -z; up 0 floor, 1 ceiling, 2..5 on a side)
    def wedge(s, mat, dirn, x0, y0, z0, x1, y1, z1, up=0, kind="wedge"):
        a, b, c = sorted((x0, x1)), sorted((y0, y1)), sorted((z0, z1))
        s.line(f"{kind} {mat_id(mat)} {dirn + 4 * up} {num(a[0])} {num(b[0])} {num(c[0])} {num(a[1])} {num(b[1])} {num(c[1])}")

    def ramp(s, mat, x0, z0, x1, z1, y0, y1, rising):
        """a walkable slope from y0 to y1 rising toward rising ('+x' '-x' '+z' '-z'); solid under it down to y0"""
        d = {"+x": 0, "-x": 1, "+z": 2, "-z": 3}[rising]
        s.wedge(mat, d, x0, y0, z0, x1, y1, z1)

    # ---- cut boxes: the box less what lies past planes (world space: n . p <= d inside)
    def cut(s, mat, box, planes):
        x0, y0, z0, x1, y1, z1 = box
        sx, sy, sz = x1 - x0, y1 - y0, z1 - z0
        if min(sx, sy, sz) < 1e-4: return
        out = []
        for (nx, ny, nz, d) in planes:
            a, b, c = nx * sx, ny * sy, nz * sz
            dd = d - (nx * x0 + ny * y0 + nz * z0)
            L = math.sqrt(a * a + b * b + c * c)
            if L < 1e-9: continue
            a, b, c, dd = a / L, b / L, c / L, dd / L
            # (a plane that leaves the whole box in: dropped)
            corners = [a * i + b * j + c * k for i in (0, 1) for j in (0, 1) for k in (0, 1)]
            if max(corners) <= dd + 1e-6: continue
            if min(corners) >= dd - 1e-6: return   # (nothing left)
            out.append((a, b, c, dd))
        if not out:
            s.box(mat, x0, y0, z0, x1, y1, z1); return
        if len(out) > 6: raise SystemExit("m5kit: a cut box has at most 6 planes")
        p = " ".join(f"{num(a)} {num(b)} {num(c)} {num(dd)}" for (a, b, c, dd) in out)
        s.line(f"cut {mat_id(mat)} {len(out)} {p} {num(x0)} {num(y0)} {num(z0)} {num(x1)} {num(y1)} {num(z1)}")

    def prism(s, mat, poly, y0, y1, top=None, clip=()):
        """a vertical prism over convex polygon poly [(x, z)...] (either winding) from y0 to y1; top = (nx, ny, nz, d)
        an extra plane (a sloped top); clip: more planes (n . p <= d kept)"""
        xs = [p[0] for p in poly]; zs = [p[1] for p in poly]
        cx, cz = sum(xs) / len(xs), sum(zs) / len(zs)
        planes = []
        for i in range(len(poly)):
            (ax, az), (bx, bz) = poly[i], poly[(i + 1) % len(poly)]
            ex, ez = bx - ax, bz - az
            nx, nz = ez, -ex
            if nx * (cx - ax) + nz * (cz - az) > 0: nx, nz = -nx, -nz   # (outward)
            L = math.hypot(nx, nz)
            if L < 1e-9: continue
            nx, nz = nx / L, nz / L
            if abs(nx) < 1e-6 or abs(nz) < 1e-6: continue   # (an axis edge: the box's own face)
            planes.append((nx, 0, nz, nx * ax + nz * az))
        if top: planes.append(top)
        planes += list(clip)
        s.cut(mat, (min(xs), y0, min(zs), max(xs), y1, max(zs)), planes)

    def bar(s, mat, a, b, w, y0, y1):
        """a straight bar from a to b (x, z) of width w, y0..y1: a rotated rectangle (a plain box when axis-aligned)"""
        (ax, az), (bx, bz) = a, b
        dx, dz = bx - ax, bz - az
        L = math.hypot(dx, dz)
        if L < 1e-6: return
        ux, uz = dx / L, dz / L
        px, pz = -uz * w / 2, ux * w / 2
        if abs(ux) < 1e-6 or abs(uz) < 1e-6:
            xs = [ax + px, ax - px, bx + px, bx - px]; zs = [az + pz, az - pz, bz + pz, bz - pz]
            s.box(mat, min(xs), y0, min(zs), max(xs), y1, max(zs)); return
        s.prism(mat, [(ax + px, az + pz), (bx + px, bz + pz), (bx - px, bz - pz), (ax - px, az - pz)], y0, y1)

    def ring(s, mat, cx, cz, r0, r1, y0, y1, n=16, a0=0.0, arc=(0.0, 360.0), skip=(), only=None):
        """a vertical ring wall (or annular floor) from radius r0 to r1 in n segments (outer corners on r1's
        polygon, the inner chord at r0): arc limits it (degrees, from +x toward +z), skip lists segments left out"""
        out = []
        for i in range(n):
            t0 = math.radians(a0 + 360.0 * i / n); t1 = math.radians(a0 + 360.0 * (i + 1) / n)
            mid = math.degrees((t0 + t1) / 2) % 360
            lo, hi = arc
            if not (lo <= mid <= hi or lo <= mid + 360 <= hi): continue
            if i in skip or (only is not None and i not in only): continue
            poly = [(cx + r0 * math.cos(t0), cz + r0 * math.sin(t0)), (cx + r1 * math.cos(t0), cz + r1 * math.sin(t0)),
                    (cx + r1 * math.cos(t1), cz + r1 * math.sin(t1)), (cx + r0 * math.cos(t1), cz + r0 * math.sin(t1))]
            if r0 <= 1e-6: poly = [(cx, cz)] + poly[1:3]
            s.prism(mat, poly, y0, y1)
            out.append(i)
        return out

    def disc(s, mat, cx, cz, r, y0, y1, n=12):
        """a vertical n-gon column / disc (n <= 10: the box's four faces plus six cuts; 8 or fewer sides is one brush,
        more are split into wedges)"""
        pts = [(cx + r * math.cos(2 * math.pi * (i + 0.5) / n), cz + r * math.sin(2 * math.pi * (i + 0.5) / n)) for i in range(n)]
        if n <= 8:
            s.prism(mat, pts, y0, y1)
        else:   # (two halves, each convex)
            h = n // 2
            s.prism(mat, pts[:h + 1], y0, y1); s.prism(mat, pts[h:] + pts[:1], y0, y1)

    def tube_x(s, mat, x0, x1, cy, cz, r):
        """a horizontal octagonal tube (solid) along x: one brush"""
        planes = []
        for (sy, sz) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            ny, nz = sy / math.sqrt(2), sz / math.sqrt(2)
            # the chamfer plane through the octagon's edge at distance r * cos(22.5) from the centre
            d = r * math.cos(math.pi / 8)
            planes.append((0, ny, nz, ny * cy + nz * cz + d))
        s.cut(mat, (x0, cy - r * math.cos(math.pi / 8), cz - r * math.cos(math.pi / 8), x1, cy + r * math.cos(math.pi / 8), cz + r * math.cos(math.pi / 8)), planes)

    def tube_z(s, mat, z0, z1, cx, cy, r):
        planes = []
        for (sx, sy) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            nx, ny = sx / math.sqrt(2), sy / math.sqrt(2)
            planes.append((nx, ny, 0, nx * cx + ny * cy + r * math.cos(math.pi / 8)))
        k = r * math.cos(math.pi / 8)
        s.cut(mat, (cx - k, cy - k, z0, cx + k, cy + k, z1), planes)

    def ladder(s, x, z, y0, y1, face, width=0.9, mat="galvanizedsteel"):
        """prefabs' ladder (rails and rungs flush on a wall, the climb volume a metre above y1) in a named material"""
        w = width / 2
        d = {"n": (0, -1), "s": (0, 1), "w": (-1, 0), "e": (1, 0)}[face]
        rx, rz = x + d[0] * 0.09, z + d[1] * 0.09
        if d[0] == 0:
            for sx in (-w, w): s.box(mat, x + sx - 0.04, y0, rz - 0.03, x + sx + 0.04, y1, rz + 0.03)
            y = y0 + 0.35
            while y < y1: s.box(mat, x - w, y, rz - 0.03, x + w, y + 0.06, rz + 0.03); y += 0.4
            z0, z1 = sorted((z, z - d[1] * 0.7))
            s.line(f"ladder {num(x - w)} {num(y0)} {num(z0)} {num(x + w)} {num(y1 + 1.0)} {num(z1)}")
        else:
            for sz in (-w, w): s.box(mat, rx - 0.03, y0, z + sz - 0.04, rx + 0.03, y1, z + sz + 0.04)
            y = y0 + 0.35
            while y < y1: s.box(mat, rx - 0.03, y, z - w, rx + 0.03, y + 0.06, z + w); y += 0.4
            x0, x1 = sorted((x, x - d[0] * 0.7))
            s.line(f"ladder {num(x0)} {num(y0)} {num(z - w)} {num(x1)} {num(y1 + 1.0)} {num(z + w)}")

    def stairs(s, mat, x0, z0, x1, z1, top, axis, rising, base=0.0, solid=True, rise=0.5, floor=None):
        """prefabs' stairs (recorded for the stairs check) in a named material; floor: solid steps reach down to it
        (a flight between two decks, recorded as a flight in the air: nothing under it is walkable)"""
        x0, x1 = sorted((x0, x1)); z0, z1 = sorted((z0, z1))
        s.line(f"stairs {num(x0)} {num(z0)} {num(x1)} {num(z1)} {num(base)} {num(top)} {axis} {rising:g} {int(solid and floor is None)}")
        n = max(1, math.ceil((top - base) / rise - 1e-6)); length = (x1 - x0) if axis == "x" else (z1 - z0)
        for i in range(n):
            a = length * i / n; b = length * (i + 1) / n
            h = base + (top - base) * ((i + 1) if rising > 0 else (n - i)) / n
            y0 = floor if floor is not None else (min(base, 0) - (1 if base < 0 else 0)) if solid else h - 0.5
            if axis == "x": s.box(mat, x0 + a, y0, z0, x0 + b, h, z1)
            else: s.box(mat, x0, y0, z0 + a, x1, h, z0 + b)

    def light(s, x, y, z, r=12, i=0.8):
        s.line(f"light {num(x)} {num(y)} {num(z)} {num(r)} {num(i)}")

    def spawn(s, team, x, y, z, yaw):
        s.line(f"spawn {team} {num(x)} {num(y)} {num(z)} {num(yaw)}")


def clip_poly(poly, a, b, c):
    """convex polygon [(x, z)] cut to a x + b z <= c (a tight polygon: its box is what the walk checks see)"""
    out = []
    for i in range(len(poly)):
        p, q = poly[i], poly[(i + 1) % len(poly)]
        dp, dq = a * p[0] + b * p[1] - c, a * q[0] + b * q[1] - c
        if dp <= 0: out.append(p)
        if (dp <= 0) != (dq <= 0):
            t = dp / (dp - dq); out.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t))
    return out


def yaw_to(x, z, tx, tz):
    """the spawn yaw that faces from (x, z) toward (tx, tz) (yaw 0 faces -z, 90 faces -x)"""
    return round(math.degrees(math.atan2(-(tx - x), -(tz - z))))


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
