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
