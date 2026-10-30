"""Building kit for the 1.19.6 large maps (railyard, sewer, riverside, war): named materials, slopes and cut boxes
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
    if mat not in MAT: raise SystemExit(f"bigkit: no material '{mat}' in map.h")
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
        if mat not in MAT: raise SystemExit(f"bigkit: no material '{mat}' in map.h")
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
        if len(out) > 6: raise SystemExit("bigkit: a cut box has at most 6 planes")
        p = " ".join(f"{num(a)} {num(b)} {num(c)} {num(dd)}" for (a, b, c, dd) in out)
        s.line(f"cut {mat_id(mat)} {len(out)} {p} {num(x0)} {num(y0)} {num(z0)} {num(x1)} {num(y1)} {num(z1)}")

    def prism(s, mat, poly, y0, y1, top=None):
        """a vertical prism over convex polygon poly [(x, z)...] (either winding) from y0 to y1; top = (nx, ny, nz, d)
        an extra plane (a sloped top)"""
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

    def ring(s, mat, cx, cz, r0, r1, y0, y1, n=16, a0=0.0, arc=(0.0, 360.0), skip=()):
        """a vertical ring wall (or annular floor) from radius r0 to r1 in n segments (outer corners on r1's
        polygon, the inner chord at r0): arc limits it (degrees, from +x toward +z), skip lists segments left out"""
        out = []
        for i in range(n):
            t0 = math.radians(a0 + 360.0 * i / n); t1 = math.radians(a0 + 360.0 * (i + 1) / n)
            mid = math.degrees((t0 + t1) / 2) % 360
            lo, hi = arc
            if not (lo <= mid <= hi or lo <= mid + 360 <= hi): continue
            if i in skip: continue
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

    def stairs(s, mat, x0, z0, x1, z1, top, axis, rising, base=0.0, solid=True, rise=0.5):
        """prefabs' stairs (recorded for the stairs check) in a named material"""
        x0, x1 = sorted((x0, x1)); z0, z1 = sorted((z0, z1))
        s.line(f"stairs {num(x0)} {num(z0)} {num(x1)} {num(z1)} {num(base)} {num(top)} {axis} {rising:g} {int(solid)}")
        n = max(1, math.ceil((top - base) / rise - 1e-6)); length = (x1 - x0) if axis == "x" else (z1 - z0)
        for i in range(n):
            a = length * i / n; b = length * (i + 1) / n
            h = base + (top - base) * ((i + 1) if rising > 0 else (n - i)) / n
            y0 = (min(base, 0) - (1 if base < 0 else 0)) if solid else h - 0.5
            if axis == "x": s.box(mat, x0 + a, y0, z0, x0 + b, h, z1)
            else: s.box(mat, x0, y0, z0 + a, x1, h, z0 + b)

    def light(s, x, y, z, r=12, i=0.8):
        s.line(f"light {num(x)} {num(y)} {num(z)} {num(r)} {num(i)}")

    def spawn(s, team, x, y, z, yaw):
        s.line(f"spawn {team} {num(x)} {num(y)} {num(z)} {num(yaw)}")


def yaw_to(x, z, tx, tz):
    """the spawn yaw that faces from (x, z) toward (tx, tz) (yaw 0 faces -z, 90 faces -x)"""
    return round(math.degrees(math.atan2(-(tx - x), -(tz - z))))
