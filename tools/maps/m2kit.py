"""Small geometry kit for the 1.19.6 maps (datacenter, mine, qk5-qk8): boxes in any material by name, slopes, convex
prisms (cut boxes), walls with openings (straight or along any line), and the material numbers slopes and cuts need.
Everything here only appends lines to an M (prefabs.py); output stays deterministic."""
import os, re, math

_MAPH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "game", "map.h")


def _material_words():
    src = open(_MAPH, encoding="utf-8").read()
    i = src.index("kMapMaterialWords[] = {")
    return re.findall(r'"([a-z0-9]+)"', src[i:src.index("};", i)])


WORDS = _material_words()
MAT = {w: k for k, w in enumerate(WORDS)}


def f(v):
    """a coordinate as the files write it (shortest, no -0)"""
    v = round(float(v), 4)
    return "%g" % (0.0 if v == 0 else v)


def B(m, mat, x0, y0, z0, x1, y1, z1):
    """a box of any material (by its file word), corners in any order; nothing for an empty one"""
    assert mat in MAT, mat
    x0, x1 = sorted((x0, x1)); y0, y1 = sorted((y0, y1)); z0, z1 = sorted((z0, z1))
    if x1 - x0 < 1e-4 or y1 - y0 < 1e-4 or z1 - z0 < 1e-4: return
    m.lines.append(f"{mat} {f(x0)} {f(y0)} {f(z0)} {f(x1)} {f(y1)} {f(z1)}")


def wedge(m, mat, rise, x0, y0, z0, x1, y1, z1, up=0):
    """a ramp in the box rising toward rise: '+x' '-x' '+z' '-z' (up 1: hung from the ceiling)"""
    d = {"+x": 0, "-x": 1, "+z": 2, "-z": 3}[rise]
    x0, x1 = sorted((x0, x1)); y0, y1 = sorted((y0, y1)); z0, z1 = sorted((z0, z1))
    m.lines.append(f"wedge {MAT[mat]} {d + 4 * up} {f(x0)} {f(y0)} {f(z0)} {f(x1)} {f(y1)} {f(z1)}")


def prism(m, mat, poly, y0, y1):
    """a convex polygon (x, z corners, either way round) from y0 to y1, as one box (an axis-aligned rectangle) or a
    cut box whose cuts are the polygon's edges that aren't on its bounding box (at most 6)"""
    xs = [p[0] for p in poly]; zs = [p[1] for p in poly]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    W, D = x1 - x0, z1 - z0
    if W < 1e-4 or D < 1e-4 or y1 - y0 < 1e-4: return
    cx, cz = sum(xs) / len(xs), sum(zs) / len(zs)
    cuts = []
    n = len(poly)
    for i in range(n):
        (ax, az), (bx, bz) = poly[i], poly[(i + 1) % n]
        if (abs(ax - bx) < 1e-6 and (abs(ax - x0) < 1e-6 or abs(ax - x1) < 1e-6)) or (abs(az - bz) < 1e-6 and (abs(az - z0) < 1e-6 or abs(az - z1) < 1e-6)):
            continue   # (on the bounding box: the box itself is that face)
        nx, nz = bz - az, -(bx - ax)
        L = math.hypot(nx, nz)
        if L < 1e-9: continue
        nx, nz = nx / L, nz / L
        d = nx * ax + nz * az
        if nx * cx + nz * cz > d: nx, nz, d = -nx, -nz, -d   # (outward)
        cuts.append((nx * W, 0.0, nz * D, d - nx * x0 - nz * z0))
    if not cuts:
        B(m, mat, x0, y0, z0, x1, y1, z1); return
    assert len(cuts) <= 6, poly
    txt = " ".join("%s %s %s %s" % tuple("%.6g" % (0.0 if abs(c) < 1e-9 else c) for c in cut) for cut in cuts)
    m.lines.append(f"cut {MAT[mat]} {len(cuts)} {txt} {f(x0)} {f(y0)} {f(z0)} {f(x1)} {f(y1)} {f(z1)}")


def solid_spans(y0, y1, holes):
    """[y0, y1] less the (h0, h1) ranges: the solid pieces left"""
    out, y = [], y0
    for (h0, h1) in sorted(holes):
        if h0 > y: out.append((y, min(h0, y1)))
        y = max(y, h1)
    if y < y1: out.append((y, y1))
    return [(a, b) for (a, b) in out if b - a > 1e-4]


def wall(m, mat, x0, z0, x1, z1, y0, y1, holes=()):
    """an axis-aligned wall (the longer side is its run) with openings holes = [(a0, a1, h0, h1)] along the run"""
    along_x = (x1 - x0) >= (z1 - z0)
    a0, a1 = (x0, x1) if along_x else (z0, z1)
    cuts = sorted({a0, a1} | {min(max(v, a0), a1) for h in holes for v in h[:2]})
    for (s, e) in zip(cuts, cuts[1:]):
        if e - s < 1e-4: continue
        mid = (s + e) / 2
        over = [(h[2], h[3]) for h in holes if h[0] < mid < h[1]]
        for (ya, yb) in solid_spans(y0, y1, over):
            if along_x: B(m, mat, s, ya, z0, e, yb, z1)
            else: B(m, mat, x0, ya, s, x1, yb, e)


def door(c, w=4.5, h=4.0, y=0.0):
    """an opening for wall(): centred at c along the run, w wide, h tall from y"""
    return (c - w / 2, c + w / 2, y, y + h)


def line_wall(m, mat, p0, p1, t, y0, y1, holes=(), inward=None):
    """a wall t thick along the segment p0 -> p1 (on its side toward inward, a point; default: centred), with openings
    holes = [(s0, s1, h0, h1)] in metres along it from p0"""
    (ax, az), (bx, bz) = p0, p1
    L = math.hypot(bx - ax, bz - az)
    ux, uz = (bx - ax) / L, (bz - az) / L
    nx, nz = -uz, ux
    if inward is not None and (inward[0] - ax) * nx + (inward[1] - az) * nz < 0: nx, nz = -nx, -nz
    o0, o1 = (0.0, t) if inward is not None else (-t / 2, t / 2)
    cuts = sorted({0.0, L} | {min(max(v, 0.0), L) for h in holes for v in h[:2]})
    for (s, e) in zip(cuts, cuts[1:]):
        if e - s < 1e-4: continue
        mid = (s + e) / 2
        over = [(h[2], h[3]) for h in holes if h[0] < mid < h[1]]
        q = [(ax + ux * s + nx * o0, az + uz * s + nz * o0), (ax + ux * e + nx * o0, az + uz * e + nz * o0),
             (ax + ux * e + nx * o1, az + uz * e + nz * o1), (ax + ux * s + nx * o1, az + uz * s + nz * o1)]
        for (ya, yb) in solid_spans(y0, y1, over): prism(m, mat, q, ya, yb)


def octagon(cx, cz, apothem):
    """the corners of a regular octagon with flat faces facing the axes, from the north face's west end, clockwise
    seen from above (N, NE, E, SE, S, SW, W, NW faces in that order: corner k to k + 1 is face k)"""
    s = apothem * math.tan(math.pi / 8)
    a = apothem
    return [(cx - s, cz - a), (cx + s, cz - a), (cx + a, cz - s), (cx + a, cz + s), (cx + s, cz + a), (cx - s, cz + a),
            (cx - a, cz + s), (cx - a, cz - s)]


def light(m, x, y, z, r=12, i=0.8):
    m.lines.append(f"light {f(x)} {f(y)} {f(z)} {f(r)} {f(i)}")


def clip(poly, nx, nz, d):
    """the convex polygon's part where nx x + nz z <= d"""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        da, db = nx * a[0] + nz * a[1] - d, nx * b[0] + nz * b[1] - d
        if da <= 1e-9: out.append(a)
        if (da < -1e-9 and db > 1e-9) or (da > 1e-9 and db < -1e-9):
            t = da / (da - db); out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    # (drop repeated corners)
    res = []
    for p in out:
        if not res or abs(p[0] - res[-1][0]) > 1e-7 or abs(p[1] - res[-1][1]) > 1e-7: res.append(p)
    if len(res) > 1 and abs(res[0][0] - res[-1][0]) < 1e-7 and abs(res[0][1] - res[-1][1]) < 1e-7: res.pop()
    return res if len(res) >= 3 else []


def clip_rect(poly, x0, z0, x1, z1):
    for (nx, nz, d) in ((-1, 0, -x0), (1, 0, x1), (0, -1, -z0), (0, 1, z1)):
        poly = clip(poly, nx, nz, d)
        if not poly: return []
    return poly


def area(poly):
    return abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))) / 2


def poly_wall(m, mat, quad, axis_u, holes, y0, y1):
    """a wall piece (a convex polygon, quad) with openings holes = [(s0, s1, h0, h1)] measured along the unit vector
    axis_u (from the origin): split across the run at each opening's edges"""
    ux, uz = axis_u
    us = [ux * p[0] + uz * p[1] for p in quad]
    lo, hi = min(us), max(us)
    cuts = sorted({lo, hi} | {min(max(v, lo), hi) for h in holes for v in h[:2]})
    for (s, e) in zip(cuts, cuts[1:]):
        if e - s < 1e-4: continue
        piece = clip(clip(quad, -ux, -uz, -s), ux, uz, e)
        if not piece or area(piece) < 1e-4: continue
        mid = (s + e) / 2
        over = [(h[2], h[3]) for h in holes if h[0] < mid < h[1]]
        for (ya, yb) in solid_spans(y0, y1, over): prism(m, mat, piece, ya, yb)


def cut3d(m, mat, planes, box):
    """a cut box: box (x0 y0 z0 x1 y1 z1) less what lies past the world planes [(nx, ny, nz, d)]: n . p <= d inside"""
    x0, y0, z0, x1, y1, z1 = box
    W, H, D = x1 - x0, y1 - y0, z1 - z0
    cuts = []
    for (nx, ny, nz, d) in planes:
        cuts.append((nx * W, ny * H, nz * D, d - (nx * x0 + ny * y0 + nz * z0)))
    assert 1 <= len(cuts) <= 6
    txt = " ".join("%s %s %s %s" % tuple("%.6g" % (0.0 if abs(c) < 1e-9 else c) for c in cut) for cut in cuts)
    m.lines.append(f"cut {MAT[mat]} {len(cuts)} {txt} {f(x0)} {f(y0)} {f(z0)} {f(x1)} {f(y1)} {f(z1)}")


def sphere(m, mat, cx, cy, cz, r, bands=6, sectors=8):
    """a faceted ball: bands x sectors pieces, each a wedge from the axis out to a flat face on the sphere"""
    for i in range(bands):
        p0, p1 = -math.pi / 2 + math.pi * i / bands, -math.pi / 2 + math.pi * (i + 1) / bands
        ya, yb = cy + r * math.sin(p0), cy + r * math.sin(p1)
        for j in range(sectors):
            t0, t1 = 2 * math.pi * j / sectors, 2 * math.pi * (j + 1) / sectors
            pts = [(cx + r * math.cos(p) * math.cos(t), cy + r * math.sin(p), cz + r * math.cos(p) * math.sin(t)) for p in (p0, p1) for t in (t0, t1)]
            xs = [p[0] for p in pts] + [cx]; zs = [p[2] for p in pts] + [cz]
            box = (min(xs), ya, min(zs), max(xs), yb, max(zs))
            if box[3] - box[0] < 1e-3 or box[5] - box[2] < 1e-3: continue
            planes = []
            for t, sgn in ((t0, -1), (t1, 1)):   # the sector's sides: vertical planes through the axis
                nx, nz = -math.sin(t) * sgn, math.cos(t) * sgn
                planes.append((nx, 0.0, nz, nx * cx + nz * cz))
            # the outer face: through three distinct corners
            uniq = []
            for p in pts:
                if all(math.dist(p, q) > 1e-6 for q in uniq): uniq.append(p)
            a, b, c = uniq[0], uniq[1], uniq[2]
            u = [b[k] - a[k] for k in range(3)]; v = [c[k] - a[k] for k in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            ln = math.sqrt(sum(x * x for x in n)); n = [x / ln for x in n]
            d = sum(n[k] * a[k] for k in range(3))
            if n[0] * cx + n[1] * ((ya + yb) / 2) + n[2] * cz > d: n = [-x for x in n]; d = -d
            planes.append((n[0], n[1], n[2], d))
            cut3d(m, mat, planes, box)


# ---- point symmetry (ranked maps: every solid, spawn, light and ladder has a twin through the centre) ----
_BOXLIKE = set(WORDS) | {"ladder", "site", "hill", "point", "checkpoint"}


def _mirror_line(l):
    w = l.split()
    if not w: return None
    k = w[0]
    try:
        if k in ("wedge", "corner", "tetra"):
            d = int(w[2]); dr, up = d % 4, d // 4
            assert up in (0, 1), "mirror_all: only floor / ceiling slopes"
            dr = ({0: 1, 1: 0, 2: 3, 3: 2} if k == "wedge" else {0: 2, 2: 0, 1: 3, 3: 1})[dr] if k != "tetra" else {0: 2, 2: 0, 1: 3, 3: 1}[dr]
            x0, y0, z0, x1, y1, z1 = map(float, w[3:9])
            return f"{k} {w[1]} {dr + 4 * up} {f(-x1)} {f(y0)} {f(-z1)} {f(-x0)} {f(y1)} {f(-z0)}"
        if k == "cut":
            n = int(w[2]); pl = [list(map(float, w[3 + 4 * i:7 + 4 * i])) for i in range(n)]
            x0, y0, z0, x1, y1, z1 = map(float, w[3 + 4 * n:9 + 4 * n])
            txt = " ".join("%s %s %s %s" % tuple("%.6g" % (0.0 if abs(c) < 1e-9 else c) for c in (-a, b, -c, d - a - c)) for (a, b, c, d) in pl)
            return f"cut {w[1]} {n} {txt} {f(-x1)} {f(y0)} {f(-z1)} {f(-x0)} {f(y1)} {f(-z0)}"
        if k == "spawn":
            t = {"a": "b", "b": "a"}.get(w[1], w[1])
            yaw = (float(w[5]) if len(w) > 5 else 0.0) + 180
            yaw = (yaw + 180) % 360 - 180
            return f"spawn {t} {f(-float(w[2]))} {w[3]} {f(-float(w[4]))} {f(yaw)}"
        if k == "light":
            return f"light {f(-float(w[1]))} {w[2]} {f(-float(w[3]))} " + " ".join(w[4:])
        if k == "stairs":
            x0, z0, x1, z1 = map(float, w[1:5])
            return f"stairs {f(-x1)} {f(-z1)} {f(-x0)} {f(-z0)} {w[5]} {w[6]} {w[7]} {f(-float(w[8]))} {w[9]}"
        if k in _BOXLIKE and len(w) >= 7:
            x0, y0, z0, x1, y1, z1 = map(float, w[1:7])
            return " ".join([k, f(-x1), f(y0), f(-z1), f(-x0), f(y1), f(-z0)] + w[7:])
    except ValueError:
        return None
    return None


def mirror_all(m, since):
    """append the point mirror (x, z) -> (-x, -z) of every line from index since on (a twin that's already there,
    a self-symmetric piece, is not added again)"""
    have = set(m.lines)
    for l in list(m.lines[since:]):
        t = _mirror_line(l)
        if t and t not in have:
            m.lines.append(t); have.add(t)


def cutbox(m, mat, planes, box):
    """cut3d (world planes) under its old name for the qk maps"""
    cut3d(m, mat, planes, box)


def ramp(m, mat, x0, z0, x1, z1, axis, ya, yb, thick=0.8):
    """a sloped slab thick metres deep from ya (at the low-axis end) to yb (at the high end), along axis 'x' or 'z'"""
    u0, u1 = (x0, x1) if axis == "x" else (z0, z1)
    s = (yb - ya) / (u1 - u0)
    ax, az = (1.0, 0.0) if axis == "x" else (0.0, 1.0)
    top = (-s * ax, 1.0, -s * az, ya - s * u0)                 # y <= ya + s (u - u0)
    bot = (s * ax, -1.0, s * az, -(ya - thick) + s * u0)       # y >= ya - thick + s (u - u0)
    cut3d(m, mat, [top, bot], (x0, min(ya, yb) - thick, z0, x1, max(ya, yb), z1))


def island(m, cx, cz, w, d, y, depth=None, top="box", under="accent"):
    """a floating platform w x d with its top at y: a 1.2 m slab and a pointed rock under it"""
    hx, hz = w / 2, d / 2
    B(m, top, cx - hx, y - 1.2, cz - hz, cx + hx, y, cz + hz)
    h = depth if depth is not None else min(w, d) * 0.6
    yb = y - 1.2 - h
    kx, kz = hx / h, hz / h
    planes = [(1, -kx, 0, cx + hx - kx * (y - 1.2)), (-1, -kx, 0, -cx + hx - kx * (y - 1.2)),
              (0, -kz, 1, cz + hz - kz * (y - 1.2)), (0, -kz, -1, -cz + hz - kz * (y - 1.2))]
    cut3d(m, under, planes, (cx - hx, yb, cz - hz, cx + hx, y - 1.2, cz + hz))


class Rooms:
    """rooms (floor, ceiling, four walls 1 m thick outside the interior) and door volumes: every wall a door volume
    crosses is opened there. Rooms meant to connect leave a 1 m gap (the wall) between them."""
    def __init__(s, m): s.m, s.doors, s.rooms = m, [], []
    def door(s, x0, y0, z0, x1, y1, z1): s.doors.append((min(x0, x1), min(y0, y1), min(z0, z1), max(x0, x1), max(y0, y1), max(z0, z1)))
    def room(s, x0, z0, x1, z1, yf, yc, floor="box", wall="box", ceil="box", fholes=(), choles=(), nofloor=False, noceil=False, walls="nsew"):
        s.rooms.append(dict(r=(x0, z0, x1, z1), yf=yf, yc=yc, floor=floor, wall=wall, ceil=ceil, fholes=list(fholes), choles=list(choles), nofloor=nofloor, noceil=noceil, walls=walls))
    def slab_wall(s, mat, x0, y0, z0, x1, y1, z1):
        along_x = (x1 - x0) >= (z1 - z0)
        hs = [((d[0], d[3], d[1], d[4]) if along_x else (d[2], d[5], d[1], d[4])) for d in s.doors
              if d[0] < x1 and d[3] > x0 and d[2] < z1 and d[5] > z0 and d[1] < y1 and d[4] > y0]
        a0, a1 = (x0, x1) if along_x else (z0, z1)
        cuts = sorted({a0, a1} | {min(max(v, a0), a1) for h in hs for v in h[:2]})
        for (p, q) in zip(cuts, cuts[1:]):
            if q - p < 1e-4: continue
            mid = (p + q) / 2
            for (ya, yb) in solid_spans(y0, y1, [(h[2], h[3]) for h in hs if h[0] < mid < h[1]]):
                if along_x: B(s.m, mat, p, ya, z0, q, yb, z1)
                else: B(s.m, mat, x0, ya, p, x1, yb, q)
    def build(s, tile=16):
        from .prefabs import minus, tiles
        for R in s.rooms:
            x0, z0, x1, z1 = R["r"]; yf, yc = R["yf"], R["yc"]
            if not R["nofloor"]:
                for t in tiles(minus((x0 - 0.5, z0 - 0.5, x1 + 0.5, z1 + 0.5), R["fholes"]), tile): B(s.m, R["floor"], t[0], yf - 1, t[1], t[2], yf, t[3])
            if not R["noceil"]:
                for t in tiles(minus((x0 - 0.5, z0 - 0.5, x1 + 0.5, z1 + 0.5), R["choles"]), tile): B(s.m, R["ceil"], t[0], yc, t[1], t[2], yc + 1, t[3])
            w, yb, yt = R["wall"], yf - 1, yc + (0 if R["noceil"] else 1)
            if "n" in R["walls"]: s.slab_wall(w, x0 - 1, yb, z0 - 1, x1 + 1, yt, z0)
            if "s" in R["walls"]: s.slab_wall(w, x0 - 1, yb, z1, x1 + 1, yt, z1 + 1)
            if "w" in R["walls"]: s.slab_wall(w, x0 - 1, yb, z0, x0, yt, z1)
            if "e" in R["walls"]: s.slab_wall(w, x1, yb, z0, x1 + 1, yt, z1)
