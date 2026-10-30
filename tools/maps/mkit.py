"""Helpers for the 1.19.6 maps (dojo, castle, qk1..qk4): slopes and cut boxes by material name, point-mirrored
correctly (a mirrored wedge also turns its slope round, which M.box's mirror can't do), and a few shapes built from
them (cylinders, ring walls). Material numbers come from src/game/map.h's kMapMaterialWords, so they follow the game."""
import os, re, math

_MATS = None


def mat_index(name):
    """a material's number (the slope and cut lines take numbers, box lines take words)"""
    global _MATS
    if _MATS is None:
        h = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "game", "map.h")
        t = open(h, encoding="utf-8").read()
        i = t.index("kMapMaterialWords[] = {")
        _MATS = re.findall(r'"([a-z0-9]+)"', t[i:t.index("};", i)])
    return _MATS.index(name)


# a point mirror (x, z) -> (-x, -z) turns a floor / ceiling slope's high side round: wedge dir 0 <-> 1, 2 <-> 3;
# corner / tetra corner 0 (-u-v) <-> 2 (+u+v), 1 <-> 3. Sideways ones (up 2..5) swap their up too.
_WEDGE_FLIP = {0: 1, 1: 0, 2: 3, 3: 2}
_CORNER_FLIP = {0: 2, 2: 0, 1: 3, 3: 1}


def _fmt(*v): return " ".join(f"{x:g}" for x in v)


def slope(m, kind, mat, d, box, up=0, mirror=False):
    """kind wedge / corner / tetra; d its dir (wedge: 0 +x, 1 -x, 2 +z, 3 -z: the side it rises toward); box (x0 y0 z0
    x1 y1 z1). up 0 on the floor, 1 hanging from the ceiling."""
    x0, y0, z0, x1, y1, z1 = box
    k = mat_index(mat) if isinstance(mat, str) else mat
    m.raw(f"{kind} {k} {d + 4 * up} {_fmt(x0, y0, z0, x1, y1, z1)}")
    if mirror:
        fd = (_WEDGE_FLIP if kind == "wedge" else _CORNER_FLIP)[d]
        m.raw(f"{kind} {k} {fd + 4 * up} {_fmt(-x1, y0, -z1, -x0, y1, -z0)}")


def wedge(m, mat, d, box, up=0, mirror=False): slope(m, "wedge", mat, d, box, up, mirror)


def cut(m, mat, planes, box, mirror=False):
    """a box less what lies past each plane (a, b, c, d: a x + b y + c z <= d over the box's own 0..1 fractions)"""
    x0, y0, z0, x1, y1, z1 = box
    k = mat_index(mat) if isinstance(mat, str) else mat
    m.raw(f"cut {k} {len(planes)} " + " ".join(_fmt(*p) for p in planes) + " " + _fmt(x0, y0, z0, x1, y1, z1))
    if mirror:   # fractions flip in x and z: a(1 - x) + b y + c(1 - z) <= d
        fl = [(-a, b, -c, d - a - c) for (a, b, c, d) in planes]
        m.raw(f"cut {k} {len(fl)} " + " ".join(_fmt(*p) for p in fl) + " " + _fmt(-x1, y0, -z1, -x0, y1, -z0))


def cylinder(m, mat, cx, cz, r, y0, y1, sides=12, mirror=False):
    """an upright cylinder (a cut box: up to 6 planes, so 12+ sides is two or more pieces): `sides` faces round it,
    built as the bounding box cut by the faces that cross each quarter"""
    # each quarter of the circle's bounding square, cut by the polygon faces whose normals fall in that quarter
    n = sides
    for q in range(4):
        qa0, qa1 = q * math.pi / 2, (q + 1) * math.pi / 2
        sx, sz = (1 if q in (0, 3) else -1), (1 if q in (0, 1) else -1)
        bx0, bx1 = (cx, cx + r) if sx > 0 else (cx - r, cx)
        bz0, bz1 = (cz, cz + r) if sz > 0 else (cz - r, cz)
        planes = []
        for i in range(n):
            a = (i + 0.5) * 2 * math.pi / n   # face normal angle (faces centred between corners)
            if not (qa0 < a < qa1): continue
            nx, nz = math.cos(a), math.sin(a)
            rr = r * math.cos(math.pi / n)    # apothem
            # n . (p - c) <= rr in world; p = b0 + f * size  ->  nx*sx_size*fx + nz*sz_size*fz <= rr - n.(b0 - c)
            W, D = bx1 - bx0, bz1 - bz0
            planes.append((nx * W, 0, nz * D, rr - (nx * (bx0 - cx) + nz * (bz0 - cz))))
        planes = [tuple(round(v, 5) for v in p) for p in planes]
        if planes: cut(m, mat, planes[:6], (bx0, y0, bz0, bx1, y1, bz1), mirror=mirror)
        else: m.box(bx0, y0, bz0, bx1, y1, bz1, mat=mat, mirror=mirror)


def ring(m, mat, cx, cz, r0, r1, y0, y1, sides=16, skip=(), mirror=False):
    """a round wall between radii r0 and r1, as `sides` straight segments (boxes along x or z where they line up,
    cut boxes otherwise); skip: the segment numbers left open (gates). Segment i spans angles i..i+1 of 2 pi / sides."""
    for i in range(sides):
        if i in skip: continue
        a0, a1 = i * 2 * math.pi / sides, (i + 1) * 2 * math.pi / sides
        pts = [(cx + r * math.cos(a), cz + r * math.sin(a)) for r in (r0, r1) for a in (a0, a1)]
        x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
        z0, z1 = min(p[1] for p in pts), max(p[1] for p in pts)
        W, D = x1 - x0, z1 - z0
        am = (a0 + a1) / 2
        nx, nz = math.cos(am), math.sin(am)
        ro, ri = r1 * math.cos(math.pi / sides), r0
        planes = []
        # outer face: n . (p - c) <= ro ; inner face: n . (p - c) >= ri ; the two radial ends
        planes.append((nx * W, 0, nz * D, ro - (nx * (x0 - cx) + nz * (z0 - cz))))
        planes.append((-nx * W, 0, -nz * D, -ri + (nx * (x0 - cx) + nz * (z0 - cz))))
        for (a, sgn) in ((a0, -1), (a1, 1)):   # radial planes through the centre: keep the side toward the middle angle
            tx, tz = -math.sin(a) * sgn, math.cos(a) * sgn   # normal pointing out of the segment
            planes.append((tx * W, 0, tz * D, -(tx * (x0 - cx) + tz * (z0 - cz))))
        planes = [tuple(round(v, 5) for v in p) for p in planes]
        cut(m, mat, planes, (round(x0, 4), y0, round(z0, 4), round(x1, 4), y1, round(z1, 4)), mirror=mirror)


def peak(m, mat, cx, cz, r, y0, h, mirror=False):
    """a pyramid (a far mountain, a roof's spire): four corner slopes meeting over (cx, cz)"""
    for (qx, qz, d) in ((1, 1, 0), (-1, 1, 1), (-1, -1, 2), (1, -1, 3)):
        x0, x1 = sorted((cx, cx + qx * r)); z0, z1 = sorted((cz, cz + qz * r))
        slope(m, "corner", mat, d, (x0, y0, z0, x1, y0 + h, z1), mirror=mirror)
