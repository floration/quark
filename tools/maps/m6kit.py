"""Helpers for mapper 6's 1.19.6 maps (museum, cruiseship, fairground, rooftops): straight walls with door and
window openings, floor slabs with holes, thin sloped shells (glass roofs, slides) and a few small shapes, all drawn
through bigkit's Kit (named materials straight from src/game/map.h)."""
import math
from .prefabs import minus, tiles
from .bigkit import Kit, num, yaw_to

__all__ = ["Kit", "num", "yaw_to", "wall_x", "wall_z", "slab", "shell", "rails_x", "rails_z", "cage_ladder", "minus", "tiles"]


def _spans(a0, a1, holes):
    """a0..a1 split round holes [(centre, width, ...)]: the solid runs and each hole's (lo, hi, rest...)"""
    hs = sorted((c - w / 2, c + w / 2) + tuple(rest) for (c, w, *rest) in holes)
    out, pos = [], a0
    for h in hs:
        if h[0] > pos: out.append(("solid", pos, h[0]))
        out.append(("hole",) + h)
        pos = max(pos, h[1])
    if pos < a1: out.append(("solid", pos, a1))
    return out


def _wall(k, mat, axis, a0, a1, c0, c1, y0, y1, holes, glass):
    def put(m, p0, q0, p1, q1):
        if p1 - p0 < 1e-4 or q1 - q0 < 1e-4: return
        if axis == "x": k.box(m, p0, q0, c0, p1, q1, c1)
        else: k.box(m, c0, q0, p0, c1, q1, p1)
    for sp in _spans(a0, a1, holes):
        if sp[0] == "solid": put(mat, sp[1], y0, sp[2], y1); continue
        lo, hi = sp[1], sp[2]
        h = sp[3] if len(sp) > 3 else 3.2
        sill = sp[4] if len(sp) > 4 else 0.0
        put(mat, lo, y0 + h, hi, y1)
        if sill > 0:
            put(mat, lo, y0, hi, y0 + sill)
            if glass: put(glass, lo, y0 + sill, hi, y0 + h)


def wall_x(k, mat, x0, x1, z, y0, y1, holes=(), t=0.4, glass=None, side=0):
    """a wall along x from x0 to x1 centred on z (side -1 / 1: its face on z, the wall on that side of it), y0..y1;
    holes [(x centre, width, height[, sill])] are doors (a sill: a window, glass filling it when glass names one)"""
    c0, c1 = (z - t / 2, z + t / 2) if side == 0 else ((z - t, z) if side < 0 else (z, z + t))
    _wall(k, mat, "x", min(x0, x1), max(x0, x1), c0, c1, y0, y1, holes, glass)


def wall_z(k, mat, z0, z1, x, y0, y1, holes=(), t=0.4, glass=None, side=0):
    """wall_x along z (holes by their z centre)"""
    c0, c1 = (x - t / 2, x + t / 2) if side == 0 else ((x - t, x) if side < 0 else (x, x + t))
    _wall(k, mat, "z", min(z0, z1), max(z0, z1), c0, c1, y0, y1, holes, glass)


def slab(k, mat, rect, y, holes=(), t=0.4, size=20):
    """a floor slab over rect (x0, z0, x1, z1), its top at y, less the holes, tiled (bots' routes follow surfaces)"""
    for (a, b, c, d) in tiles(minus(rect, holes), size): k.box(mat, a, y - t, b, c, y, d)


def shell(k, mat, box, rising, t=0.3, extra=()):
    """a thin sloped plate filling box's top slope: from the box's low side at y0 + t... to y1 at the rising side
    ('+x' '-x' '+z' '-z'): the plate between the slope and the slope t below it (a glass roof face, a slide);
    extra: more world planes (n . p <= d)"""
    x0, y0, z0, x1, y1, z1 = box
    H = y1 - y0
    if rising in ("+x", "-x"):
        L = x1 - x0; s = H / L
        # y <= y0 + s (x - x0)  (+x)   or   y <= y0 + s (x1 - x)  (-x)
        if rising == "+x": top = (-s, 1, 0, y0 - s * x0)
        else: top = (s, 1, 0, y0 + s * x1)
    else:
        L = z1 - z0; s = H / L
        if rising == "+z": top = (0, 1, -s, y0 - s * z0)
        else: top = (0, 1, s, y0 + s * z1)
    n = math.sqrt(top[0] ** 2 + 1 + top[2] ** 2)
    bot = (-top[0], -1, -top[2], -(top[3] - t * n))   # y >= slope - t (measured square to the face)
    k.cut(mat, box, [top, bot] + list(extra))


def rails_x(k, mat, x0, x1, z, y, h=1.1, t=0.12):
    k.box(mat, min(x0, x1), y, z - t / 2, max(x0, x1), y + h, z + t / 2)


def rails_z(k, mat, z0, z1, x, y, h=1.1, t=0.12):
    k.box(mat, x - t / 2, y, min(z0, z1), x + t / 2, y + h, max(z0, z1))


def cage_ladder(k, x, z, y0, y1, face, mat="wroughtiron"):
    """a ladder (bigkit's: rails, rungs, the climb volume) against a wall at (x, z) facing `face`"""
    k.ladder(x, z, y0, y1, face, mat=mat)
