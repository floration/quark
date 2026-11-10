"""Low-poly outlined model builder (the shipped weapons and the grapple launcher: tools/models/gen_weapons_lowpoly.py).

Every piece is a closed low-poly solid, flat-shaded: a 2D polygon extruded along an axis (`solid`), or a loft between
two polygons with the same number of corners (`loft`: frustums, tapers). The outline is an inverted hull: each solid
again, grown by OUTLINE mm, its faces turned inward, near-black ("ink", drawn as authored). The game culls back faces,
so only the rim around the silhouette shows, like the box weapons' 1 px outlines.

Units are millimetres (x right, y up, -z forward); the files are metres. Materials follow the game's colours by name
(docs/ASSETS.md): metal_* (the map's metal), base_* (the map's base), team_* (team colour), glow_* (the map's glow);
`ink` and any other name are drawn as authored. A part named anim_<part> is a moving part (docs/ASSETS.md "Animated
parts"): its node's translation is the pivot and its vertices are relative to it.
"""
import base64
import json
import math
import os
import struct

REF = {
    "metal": tuple(c ** 2.2 * 1.6 + 0.05 for c in (0.30, 0.33, 0.37)),
    "base": tuple(c ** 2.2 * 1.1 for c in (0.62, 0.62, 0.64)),
    "team": (1.0, 1.0, 1.0),
    "glow": (1.0, 0.55, 0.15),
}
TONES = {   # material -> (slot reference, factor) or a fixed colour
    "metal_dark": ("metal", 0.55), "metal_mid": ("metal", 1.0), "metal_bore": ("metal", 0.25),
    "metal_steel": ("metal", 1.6), "base_light": ("base", 1.0), "base_glass": ("base", 1.15),
    "team_wood": ("team", 1.0), "team_dark": ("team", 0.62), "glow": ("glow", 1.0), "glow_dim": ("glow", 0.55),
    "glow_hot": ("glow", 1.4), "sleeve": (0.12, 0.12, 0.14), "ink": (0.012, 0.013, 0.016),
}
ALPHA = {"base_glass": 0.45}
OUTLINE = 2.6   # mm the hull grows (owner, 2026-11-06: "it should still have some outlines": 1.1 mm was under a pixel in first person)


# ---- 2D -----------------------------------------------------------------------------------------------------------

def area(poly):
    return 0.5 * sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))


def ccw(poly):
    return list(poly) if area(poly) > 0 else list(poly)[::-1]


def offset(poly, t):
    """grow a CCW polygon by t (mitred, the miter capped)"""
    n, out = len(poly), []
    for i in range(n):
        p0, p1, p2 = poly[i - 1], poly[i], poly[(i + 1) % n]
        def nrm(a, b):
            dx, dy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(dx, dy) or 1
            return (dy / L, -dx / L)
        n1, n2 = nrm(p0, p1), nrm(p1, p2)
        d = 1 + n1[0] * n2[0] + n1[1] * n2[1]
        k = t / max(d, 0.35)
        out.append((p1[0] + (n1[0] + n2[0]) * k, p1[1] + (n1[1] + n2[1]) * k))
    return out


def triangulate(poly):
    idx, tris = list(range(len(poly))), []

    def inside(p, a, b, c):
        def s(p1, p2, p3):
            return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
        d1, d2, d3 = s(p, a, b), s(p, b, c), s(p, c, a)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))

    while len(idx) > 3:
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = poly[i0], poly[i1], poly[i2]
            if (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) <= 1e-9:
                continue
            if any(inside(poly[j], a, b, c) for j in idx if j not in (i0, i1, i2)):
                continue
            tris.append((i0, i1, i2))
            idx.pop(k)
            break
        else:
            raise RuntimeError("triangulation failed: %r" % (poly,))
    tris.append(tuple(idx))
    return tris


def ngon(cx, cy, r, k=8, phase=None, sx=1.0, sy=1.0):
    ph = math.pi / k if phase is None else phase
    return [(cx + r * sx * math.cos(ph + 2 * math.pi * i / k), cy + r * sy * math.sin(ph + 2 * math.pi * i / k)) for i in range(k)]


def rrect(u0, v0, u1, v1, c):
    """a rectangle with its corners cut by c"""
    c = min(c, (u1 - u0) / 2 - 0.01, (v1 - v0) / 2 - 0.01)
    return [(u0 + c, v0), (u1 - c, v0), (u1, v0 + c), (u1, v1 - c), (u1 - c, v1), (u0 + c, v1), (u0, v1 - c), (u0, v0 + c)]


# ---- 3D -----------------------------------------------------------------------------------------------------------

# axis: which axis the polygon is extruded along. The polygon's (u, v) are, per axis:
#   "x": (z, y)   a side profile, extruded across the gun
#   "z": (x, y)   a cross-section, extruded along the gun
#   "y": (x, z)   a top-down outline, extruded up
def _P(axis, u, v, w):
    if axis == "x":
        return (w, v, u)
    if axis == "z":
        return (u, v, w)
    return (u, w, v)


_MIRROR = {"x": True, "z": False, "y": True}   # (u, v, w) -> xyz is a mirror for x and y: windings flip


def rotx(a):
    c, s = math.cos(a), math.sin(a)
    return ((1, 0, 0), (0, c, -s), (0, s, c))


def roty(a):
    c, s = math.cos(a), math.sin(a)
    return ((c, 0, s), (0, 1, 0), (-s, 0, c))


def rotz(a):
    c, s = math.cos(a), math.sin(a)
    return ((c, -s, 0), (s, c, 0), (0, 0, 1))


def matmul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


class Mesh:
    def __init__(self):
        self.tris = {}       # material -> [(p0, p1, p2)]
        self.xf = None       # (R, about, t): points p -> R (p - about) + about + t, for the next solids

    def place(self, R=None, about=(0, 0, 0), t=(0, 0, 0)):
        self.xf = (R or ((1, 0, 0), (0, 1, 0), (0, 0, 1)), about, t)
        return self

    def unplace(self):
        self.xf = None
        return self

    def _x(self, p):
        if not self.xf:
            return p
        R, a, t = self.xf
        q = [p[i] - a[i] for i in range(3)]
        return tuple(sum(R[i][k] * q[k] for k in range(3)) + a[i] + t[i] for i in range(3))

    def add(self, mat, a, b, c):
        self.tris.setdefault(mat, []).append((self._x(a), self._x(b), self._x(c)))

    def count(self):
        return sum(len(v) for v in self.tris.values())


def _prism(mesh, mat, poly, axis, lo, hi, inward):
    poly = ccw(poly)
    faces = []
    for i, j, k in triangulate(poly):
        a, b, c = poly[i], poly[j], poly[k]
        faces.append((_P(axis, *a, hi), _P(axis, *b, hi), _P(axis, *c, hi)))
        faces.append((_P(axis, *a, lo), _P(axis, *c, lo), _P(axis, *b, lo)))
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        q = [_P(axis, *a, lo), _P(axis, *b, lo), _P(axis, *b, hi), _P(axis, *a, hi)]
        faces += [(q[0], q[1], q[2]), (q[0], q[2], q[3])]
    flip = _MIRROR[axis] != inward
    for a, b, c in faces:
        mesh.add(mat, *((a, c, b) if flip else (a, b, c)))


def solid(mesh, mat, poly, axis, lo, hi, shell=True, outline=None):
    """an extruded polygon, with its outline hull (outline: the hull's growth in mm, default OUTLINE)"""
    t = OUTLINE if outline is None else outline
    poly = ccw(poly)
    _prism(mesh, mat, poly, axis, lo, hi, False)
    if shell:
        _prism(mesh, "ink", offset(poly, t), axis, lo - t, hi + t, True)


def _loft(mesh, mat, pa, wa, pb, wb, axis, inward):
    """two polygons with the same corner count at w = wa and w = wb (wa < wb), joined by quads"""
    pa, pb = ccw(pa), ccw(pb)
    faces = []
    for i, j, k in triangulate(pb):
        faces.append((_P(axis, *pb[i], wb), _P(axis, *pb[j], wb), _P(axis, *pb[k], wb)))
    for i, j, k in triangulate(pa):
        faces.append((_P(axis, *pa[i], wa), _P(axis, *pa[k], wa), _P(axis, *pa[j], wa)))
    n = len(pa)
    for i in range(n):
        a0, a1, b1, b0 = _P(axis, *pa[i], wa), _P(axis, *pa[(i + 1) % n], wa), _P(axis, *pb[(i + 1) % n], wb), _P(axis, *pb[i], wb)
        faces += [(a0, a1, b1), (a0, b1, b0)]
    flip = _MIRROR[axis] != inward
    for a, b, c in faces:
        mesh.add(mat, *((a, c, b) if flip else (a, b, c)))


def loft(mesh, mat, pa, wa, pb, wb, axis="z", shell=True, outline=None):
    t = OUTLINE if outline is None else outline
    if wa > wb:
        pa, wa, pb, wb = pb, wb, pa, wa
    pa, pb = ccw(pa), ccw(pb)
    _loft(mesh, mat, pa, wa, pb, wb, axis, False)
    if shell:
        _loft(mesh, "ink", offset(pa, t), wa - t, offset(pb, t), wb + t, axis, True)


def _sweep(mesh, mat, rings, axis, inward):
    """rings [(polygon, w)] with w rising, each polygon CCW with the same corner count: end caps + side quads"""
    faces = []
    p0, w0 = rings[0]
    p1, w1 = rings[-1]
    for i, j, k in triangulate(p1):
        faces.append((_P(axis, *p1[i], w1), _P(axis, *p1[j], w1), _P(axis, *p1[k], w1)))
    for i, j, k in triangulate(p0):
        faces.append((_P(axis, *p0[i], w0), _P(axis, *p0[k], w0), _P(axis, *p0[j], w0)))
    n = len(p0)
    for (pa, wa), (pb, wb) in zip(rings, rings[1:]):
        for i in range(n):
            a0, a1, b1, b0 = _P(axis, *pa[i], wa), _P(axis, *pa[(i + 1) % n], wa), _P(axis, *pb[(i + 1) % n], wb), _P(axis, *pb[i], wb)
            faces += [(a0, a1, b1), (a0, b1, b0)]
    flip = _MIRROR[axis] != inward
    for a, b, c in faces:
        mesh.add(mat, *((a, c, b) if flip else (a, b, c)))


def sweep(mesh, mat, rings, axis="y", shell=True, outline=None):
    """a loft through several polygons [(polygon, w)] (same corner count, w strictly rising; a polygon is (u, v) as
    for solid), only its two ends capped: a tapered limb, a torso, a faceted head in one closed solid"""
    t = OUTLINE if outline is None else outline
    rings = sorted(((ccw(p), w) for p, w in rings), key=lambda r: r[1])
    _sweep(mesh, mat, rings, axis, False)
    if shell:
        hull = [(offset(p, t), w) for p, w in rings]
        hull[0], hull[-1] = (hull[0][0], hull[0][1] - t), (hull[-1][0], hull[-1][1] + t)
        _sweep(mesh, "ink", hull, axis, True)


def rod(mesh, mat, p0, p1, r, k=6, shell=True, outline=None):
    """a k-sided rod between two points (any direction)"""
    d = [p1[i] - p0[i] for i in range(3)]
    L = math.sqrt(sum(v * v for v in d))
    yaw = math.atan2(-d[0], -d[2])
    pitch = math.atan2(d[1], math.hypot(d[0], d[2]))
    R = matmul(roty(yaw), rotx(pitch))   # -z rotated onto d
    saved = mesh.xf
    if saved:
        raise RuntimeError("rod inside place()")
    mesh.place(R, (0, 0, 0), p0)
    solid(mesh, mat, ngon(0, 0, r, k), "z", -L, 0, shell, outline)
    mesh.unplace()


# ---- glTF -----------------------------------------------------------------------------------------------------------

def colour(mat, tones=None):
    t = (tones or TONES)[mat]
    if isinstance(t[0], str):
        return tuple(c * t[1] for c in REF[t[0]])
    return t


def write_gltf(path, name, parts, extras, tones=None, generator="gen_weapons_lowpoly.py"):
    """parts: [(node name, Mesh, pivot mm or None)]; the first is the body (pivot None). tones: a TONES of the
    caller's (default TONES); generator: the script named in asset.generator"""
    bin_ = bytearray()
    views, accs, mats, meshes, nodes = [], [], [], [], []
    mat_index = {}

    def put(data, target):
        off = len(bin_)
        bin_.extend(data)
        while len(bin_) % 4:
            bin_.append(0)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
        return len(views) - 1

    def prims(mesh, pivot):
        out = []
        pv = pivot or (0, 0, 0)
        for mat in sorted(mesh.tris):
            if mat not in mat_index:
                mat_index[mat] = len(mats)
                m = {"name": mat, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, ALPHA.get(mat, 1)], "metallicFactor": 0, "roughnessFactor": 1}}
                if mat in ALPHA:
                    m["alphaMode"] = "BLEND"
                mats.append(m)
            P, N = [], []
            for a, b, c in mesh.tris[mat]:
                e1 = [b[t] - a[t] for t in range(3)]
                e2 = [c[t] - a[t] for t in range(3)]
                n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                L = math.sqrt(sum(v * v for v in n)) or 1
                n = [v / L for v in n]
                for p in (a, b, c):
                    P.append([(p[i] - pv[i]) / 1000.0 for i in range(3)])
                    N.append(n)
            col = colour(mat, tones)
            a0 = len(accs)
            accs.append({"bufferView": put(b"".join(struct.pack("<3f", *p) for p in P), 34962), "componentType": 5126, "count": len(P), "type": "VEC3",
                         "min": [min(p[i] for p in P) for i in range(3)], "max": [max(p[i] for p in P) for i in range(3)]})
            accs.append({"bufferView": put(b"".join(struct.pack("<3f", *n) for n in N), 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
            accs.append({"bufferView": put(struct.pack("<3f", *col) * len(P), 34962), "componentType": 5126, "count": len(P), "type": "VEC3"})
            out.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "COLOR_0": a0 + 2}, "material": mat_index[mat], "mode": 4})
        return out

    for nm, mesh, pivot in parts:
        if not mesh.tris:
            continue
        meshes.append({"name": nm, "primitives": prims(mesh, pivot)})
        node = {"name": nm, "mesh": len(meshes) - 1}
        if pivot is not None:
            node["translation"] = [v / 1000.0 for v in pivot]
        nodes.append(node)
    doc = {
        "asset": {"version": "2.0", "generator": "quark tools/models/" + generator},
        "extras": dict(extras, provenance="AI-made placeholder"),
        "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes, "meshes": meshes, "materials": mats, "accessors": accs, "bufferViews": views,
        "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(bin_)).decode("ascii")}],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(json.dumps(doc, separators=(",", ":")) + "\n")
    return sum(m.count() for _, m, _ in parts)
