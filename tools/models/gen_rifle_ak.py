#!/usr/bin/env python3
"""The outlined set's rifle as a low-poly AK (owner, 2026-11-06: the box AK "looks ugly"; "you cant make a low poly
version?"). A mesh override, not a box model: real angled and curved shapes (the curved magazine, the dropping stock,
the raked grip, the slant brake, the tall front sight) that axis-aligned boxes can't make.

Every piece is a closed low-poly solid: a side profile extruded across the gun, or a cross-section extruded along it.
Flat-shaded faces. The outline is an inverted hull: each solid again, grown by OUTLINE mm, its faces turned inward,
in a dark colour; the game culls back faces, so only the rim around the silhouette shows (the box weapons' look).

Colours follow the game like the box weapons: materials metal_* (dark = metal x 0.55, mid = metal), team_* (the
wood: team colour), glow_* and the outline "ink" (drawn as authored). The charging handle is the anim_bolt node
(docs/ASSETS.md "Animated parts"): it kicks back each shot. The muzzle is at kMuzzle (0, 8, -290) mm.

  python3 tools/models/gen_rifle_ak.py [--out FILE]
"""
import argparse
import base64
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

REF = {
    "metal": tuple(c ** 2.2 * 1.6 + 0.05 for c in (0.30, 0.33, 0.37)),
    "base": tuple(c ** 2.2 * 1.1 for c in (0.62, 0.62, 0.64)),
    "team": (1.0, 1.0, 1.0),
    "glow": (1.0, 0.55, 0.15),
}
TONES = {   # material -> (slot reference, factor)
    "metal_dark": ("metal", 0.55), "metal_mid": ("metal", 1.0), "metal_bore": ("metal", 0.25), "base_light": ("base", 1.0),
    "team_wood": ("team", 1.0), "team_dark": ("team", 0.62), "glow": ("glow", 1.0),
}
INK = (0.012, 0.013, 0.016)
OUTLINE = 1.1   # mm the hull grows


# ---- 2D helpers --------------------------------------------------------------------------------------------------

def area(poly):
    return 0.5 * sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))


def ccw(poly):
    return poly if area(poly) > 0 else poly[::-1]


def offset(poly, t):
    """grow a CCW polygon by t (mitred, the miter capped)"""
    n = len(poly)
    out = []
    for i in range(n):
        p0, p1, p2 = poly[i - 1], poly[i], poly[(i + 1) % n]
        def nrm(a, b):
            dx, dy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(dx, dy) or 1
            return (dy / L, -dx / L)   # outward for CCW
        n1, n2 = nrm(p0, p1), nrm(p1, p2)
        mx, my = n1[0] + n2[0], n1[1] + n2[1]
        d = 1 + n1[0] * n2[0] + n1[1] * n2[1]
        k = t / max(d, 0.35)
        out.append((p1[0] + mx * k, p1[1] + my * k))
    return out


def triangulate(poly):
    """ear clipping; poly CCW; returns index triples"""
    idx = list(range(len(poly)))
    tris = []

    def inside(p, a, b, c):
        def s(p1, p2, p3):
            return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
        d1, d2, d3 = s(p, a, b), s(p, b, c), s(p, c, a)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))

    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
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
            raise RuntimeError("triangulation failed")
    tris.append(tuple(idx))
    return tris


# ---- solids ------------------------------------------------------------------------------------------------------

class Mesh:
    def __init__(self):
        self.tris = {}   # material -> list of (p0, p1, p2) (mm)

    def add(self, mat, a, b, c):
        self.tris.setdefault(mat, []).append((a, b, c))


def prism(mesh, mat, poly, axis, lo, hi, inward=False):
    """extrude a 2D polygon. axis 'x': poly is (z, y), extruded over x in [lo, hi]. axis 'z': poly is (x, y), over z.
    inward: the faces turned inside (the outline hull)"""
    poly = ccw(poly)
    def P(u, v, w):
        return (w, v, u) if axis == "x" else (u, v, w)
    tris = triangulate(poly)
    faces = []
    # caps: for axis x the poly plane is (z, y): +x face must wind CCW seen from +x
    for (i, j, k) in tris:
        a, b, c = poly[i], poly[j], poly[k]
        faces.append((P(*a, hi), P(*b, hi), P(*c, hi)))
        faces.append((P(*a, lo), P(*c, lo), P(*b, lo)))
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        q = [P(*a, lo), P(*b, lo), P(*b, hi), P(*a, hi)]
        faces.append((q[0], q[1], q[2]))
        faces.append((q[0], q[2], q[3]))
    # (u, v, w) -> (z, y, x) for axis x is a mirror: every face winds the other way
    flip = (axis == "x") != inward
    for a, b, c in faces:
        mesh.add(mat, *((a, c, b) if flip else (a, b, c)))
    return mesh


def solid(mesh, mat, poly, axis, lo, hi, shell=True):
    poly = ccw(poly)
    prism(mesh, mat, poly, axis, lo, hi)
    if shell:
        prism(mesh, "ink", offset(poly, OUTLINE), axis, lo - OUTLINE, hi + OUTLINE, inward=True)


def ngon(cx, cy, r, k=8, phase=None, sx=1.0, sy=1.0):
    ph = math.pi / k if phase is None else phase
    return [(cx + r * sx * math.cos(ph + 2 * math.pi * i / k), cy + r * sy * math.sin(ph + 2 * math.pi * i / k)) for i in range(k)]


# ---- the AK ------------------------------------------------------------------------------------------------------
# side profiles are (z, y) mm: -z toward the muzzle, +y up. Cross-sections are (x, y).

def build():
    body, bolt = Mesh(), Mesh()
    B = body

    # the receiver group sits D mm further back than the first cut, so the front (handguard, gas tube, sights) has
    # an AK's length ahead of the receiver; the stock is shorter (it runs off the screen's corner anyway)
    D = 36
    def S(poly):
        return [(z + D, y) for z, y in poly]
    # receiver: a flat-sided slab, the magwell dip at the bottom front, the rear tang
    solid(B, "metal_dark", S([(-100, 16), (72, 16), (76, 10), (76, -18), (58, -22), (-26, -22), (-34, -28),
                               (-74, -28), (-80, -22), (-100, -22)]), "x", -18, 18)
    # dust cover: an arched section along the receiver, its rear button
    arch = [(-16, 15), (16, 15), (16, 22), (13, 27), (7, 30), (-7, 30), (-13, 27), (-16, 22)]
    solid(B, "metal_mid", arch, "z", -92 + D, 66 + D)
    solid(B, "metal_mid", ngon(0, 23, 5, 8), "z", 66 + D, 74 + D)
    # rear sight block (tapered toward the front, on the trunnion) and its leaf
    solid(B, "metal_dark", S([(-124, 14), (-100, 14), (-100, 31), (-110, 31), (-124, 22)]), "x", -12, 12)
    solid(B, "metal_mid", S([(-120, 31), (-104, 31), (-104, 35), (-118, 34)]), "x", -7, 7)
    # selector lever on the right: a long plate, its tab
    solid(B, "metal_mid", S([(-74, 9), (6, 9), (10, 4), (6, 1), (-74, 5)]), "x", 18, 20.5)
    # ejection port (a dark recess plate on the right)
    solid(B, "metal_bore", S([(-64, 2), (-10, 2), (-10, 13), (-64, 13)]), "x", 17.6, 18.6, shell=False)
    # ammo counter on the left
    solid(B, "glow", S([(-72, 3), (-36, 3), (-36, 11), (-72, 11)]), "x", -18.8, -17.6, shell=False)

    # lower handguard (wood): a rounded section; upper handguard around the gas tube
    low = [(-17, 10), (17, 10), (19, -4), (15, -16), (7, -22), (-7, -22), (-15, -16), (-19, -4)]
    solid(B, "team_wood", low, "z", -212, -96)
    solid(B, "metal_dark", offset(ccw(low), 1.5), "z", -98, -90)   # retainer
    solid(B, "metal_dark", offset(ccw(low), 1.0), "z", -218, -212)  # front cap
    up = [(-12, 11), (12, 11), (13, 22), (9, 29), (-9, 29), (-13, 22)]
    solid(B, "team_wood", up, "z", -200, -90)
    # barrel, gas tube, gas block, front sight block, brake
    solid(B, "metal_mid", ngon(0, 8, 7, 8), "z", -276, -212)
    solid(B, "metal_mid", ngon(0, 24, 6, 8), "z", -230, -198)
    solid(B, "metal_dark", [(-240, -1), (-226, -1), (-226, 31), (-232, 33), (-240, 30)], "x", -10, 10)
    solid(B, "metal_dark", [(-272, -2), (-250, -2), (-250, 15), (-255, 20), (-256, 40), (-262, 40), (-264, 20), (-272, 15)], "x", -8, 8)
    solid(B, "metal_dark", [(-266, 18), (-254, 18), (-254, 38), (-266, 38)], "x", 6, 9.5)   # sight ears
    solid(B, "metal_dark", [(-266, 18), (-254, 18), (-254, 38), (-266, 38)], "x", -9.5, -6)
    solid(B, "metal_dark", [(-290, -1), (-272, -1), (-272, 18), (-280, 18), (-290, 11)], "x", -9, 9)   # slant brake
    solid(B, "metal_bore", ngon(0, 8, 4.5, 8), "z", -290.6, -289.9, shell=False)

    # the magazine: long, curving forward as it goes down; a raised rib down each side; the floor plate
    def mag(t):
        e = t ** 1.5
        return (-72 + D - 50 * e, -22 - 118 * t), (-30 + D - 58 * e, -22 - 124 * t)
    steps = 8
    front = [mag(i / steps)[0] for i in range(steps + 1)]
    back = [mag(i / steps)[1] for i in range(steps + 1)]
    solid(B, "metal_mid", front + back[::-1], "x", -12, 12)
    ribF = [(f[0] + 0.32 * (b[0] - f[0]), f[1]) for f, b in zip(front, back)][1:-1]
    ribB = [(f[0] + 0.62 * (b[0] - f[0]), b[1] + 0.3 * (f[1] - b[1])) for f, b in zip(front, back)][1:-1]
    solid(B, "metal_dark", ribF + ribB[::-1], "x", -13.5, 13.5)
    fb, bb = front[-1], back[-1]
    solid(B, "team_dark", [(fb[0] - 3, fb[1] + 1), (fb[0] - 1, fb[1] - 7), (bb[0] + 3, bb[1] - 7), (bb[0] + 3, bb[1] + 1)], "x", -14, 14)

    # trigger guard and trigger
    solid(B, "metal_mid", S([(-32, -22), (-27, -22), (-22, -36), (14, -36), (14, -40), (-25, -40)]), "x", -3.5, 3.5)
    solid(B, "metal_mid", S([(10, -22), (14, -22), (14, -38), (10, -38)]), "x", -3.5, 3.5)
    solid(B, "base_light", S([(-8, -22), (-2, -22), (0, -28), (-3, -34), (-6, -33), (-5, -27)]), "x", -2, 2)
    # pistol grip: raked back, a little flared at the bottom
    solid(B, "team_wood", S([(12, -22), (40, -22), (70, -84), (70, -92), (44, -94), (36, -82), (16, -36)]), "x", -12, 12)
    # stock: the top line drops gently, the bottom line more; a dark butt plate
    solid(B, "team_wood", [(72 + D, 14), (232, 0), (232, -62), (214, -62), (170, -42), (80 + D, -22), (72 + D, -16)], "x", -14, 14)
    solid(B, "metal_dark", [(232, 2), (242, 2), (242, -64), (232, -64)], "x", -15, 15)

    # moving: the charging handle on the right, riding the carrier (anim_bolt: back 2.5 cm and home)
    pivot = (20, 9, -24 + D)
    def rel(poly_zy):
        return [(z - pivot[2], y - pivot[1]) for z, y in poly_zy]
    solid(bolt, "metal_mid", rel(S([(-44, 4), (-2, 4), (-2, 13), (-44, 13)])), "x", 18 - pivot[0], 20.5 - pivot[0])
    # the handle: a short faceted stem out to the right (x 20..37), at the carrier's front
    solid(bolt, "metal_mid", [(0, -4.5), (14, -4.5), (17, -2), (17, 2), (14, 4.5), (0, 4.5)], "z", -40 + D - pivot[2], -32 + D - pivot[2])
    return body, bolt, pivot


# ---- glTF ----------------------------------------------------------------------------------------------------------

def colour(mat):
    if mat == "ink":
        return INK
    slot, f = TONES[mat]
    return tuple(c * f for c in REF[slot])


def write(path):
    body, bolt, pivot = build()
    bin_ = bytearray()
    views, accs, mats, meshes = [], [], [], []
    mat_index = {}

    def put(data, target):
        off = len(bin_)
        bin_.extend(data)
        while len(bin_) % 4:
            bin_.append(0)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
        return len(views) - 1

    def prims(mesh):
        out = []
        for mat in sorted(mesh.tris):
            if mat not in mat_index:
                mat_index[mat] = len(mats)
                mats.append({"name": mat, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": 1}})
            P, N, C = [], [], []
            col = colour(mat)
            for a, b, c in mesh.tris[mat]:
                e1 = [b[t] - a[t] for t in range(3)]
                e2 = [c[t] - a[t] for t in range(3)]
                n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                L = math.sqrt(sum(v * v for v in n)) or 1
                n = [v / L for v in n]
                for p in (a, b, c):
                    P.append([v / 1000.0 for v in p]); N.append(n); C.append(col)
            pb = b"".join(struct.pack("<3f", *p) for p in P)
            a0 = len(accs)
            accs.append({"bufferView": put(pb, 34962), "componentType": 5126, "count": len(P), "type": "VEC3",
                         "min": [min(p[i] for p in P) for i in range(3)], "max": [max(p[i] for p in P) for i in range(3)]})
            accs.append({"bufferView": put(b"".join(struct.pack("<3f", *n) for n in N), 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
            accs.append({"bufferView": put(b"".join(struct.pack("<3f", *c) for c in C), 34962), "componentType": 5126, "count": len(C), "type": "VEC3"})
            out.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "COLOR_0": a0 + 2}, "material": mat_index[mat], "mode": 4})
        return out

    meshes.append({"name": "RIFLE", "primitives": prims(body)})
    meshes.append({"name": "anim_bolt", "primitives": prims(bolt)})
    tris = sum(len(v) for v in body.tris.values()) + sum(len(v) for v in bolt.tris.values())
    doc = {
        "asset": {"version": "2.0", "generator": "quark tools/models/gen_rifle_ak.py"},
        "extras": {"style": "outlined", "weapon": "RIFLE", "description": "low-poly AK: flat-shaded solids with an inverted-hull outline",
                   "provenance": "AI-made placeholder"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": "RIFLE", "mesh": 0}, {"name": "anim_bolt", "mesh": 1, "translation": [v / 1000.0 for v in pivot]}],
        "meshes": meshes, "materials": mats, "accessors": accs, "bufferViews": views,
        "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(bin_)).decode("ascii")}],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(json.dumps(doc, separators=(",", ":")) + "\n")
    return tris


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "assets", "overrides", "models", "weapon", "RIFLE.gltf"))
    a = ap.parse_args()
    print("RIFLE (AK): %d triangles -> %s" % (write(a.out), a.out))


if __name__ == "__main__":
    sys.exit(main())
