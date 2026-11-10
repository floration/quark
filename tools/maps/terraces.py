"""RICE TERRACES (1.19.6): team vs (and power, king of the hill, free for all) on a sunny hillside of flooded rice
terraces, 128 x 120 m. North is -z: the hill climbs 27.3 m from the valley stream (south, 0 m) to the temple (north).
  The terraces: twelve bands follow the contour, which sweeps across the hill (each band's front edge an S of up to 7 m
  either way, cut in 8 m pieces); the bands are 4.8-8 m deep and their stone risers 1.8-2.4 m (too high to jump: you
  cross at set places). Each band is cut by its cross dykes (every 32 m, staggered: the fast lanes, dry) into fields:
  two in three are flooded (clear lagoon, 0.35 m deep, x0.9 speed; half of them planted with rice, rows of non-solid
  tufts), the third is a drained mud field (solid mud: x0.6 speed, a weak jump). A dyke 1.2 m wide runs along each band's
  front edge (on every second band an embankment 1.2 m over the water: cover).
  Ways up a riser: stone stairs, timber steps, rammed earth ramps, bamboo ladders and stone flights that turn along the
  riser, staggered so no line runs straight up the hill.
  Team A: the farmhouse (west) on its threshing yard at 10.5 m (the terraces' fifth band, levelled).
  Team B: the water mill (east) on its platform at 6.3 m by the stream, with a covered stair up to 10.5 m.
  The temple (north, 27.3 m): a walled court 30 x 24 m with a gate south, east and west, a bell tower (ladder to 12 m),
  the shrine at the back under a two-tier roof. The hill (king of the hill) is 20 x 16 m in the court. A parapet runs
  along the platform's front edge, open at the three stairs, so the court's approaches are fought on the platform.
  Its wings: the west one a pine grove with the monks' dormitory and a stupa, the east one pines, bamboo and a pond
  with its bridge; on each, behind a stone screen, king of the hill's spawn pad, a gravel path from it to the court's
  side gate.
  The valley (south, 0 m): the stream (clear lagoon, east-west: no current) with A's west stair and B's mill race
  steps down to it: the low lane.
Field huts with woven (shoot-through) walls and haystacks break the long lines along the dykes."""
import math
from .prefabs import M
from .m5kit import Kit, slab, walls, rock, yaw_to

HX, HZ = 64, 60
NB = 12                            # the bands
VALLEY = 44.0                      # band 1's front edge (the valley south of it)
PADDY = 0.4                        # a paddy's floor under its band's dyke tops
MUD = 0.1                          # a mud field's mud on that floor
# riser k (k = 1..13) lifts band k - 1 to band k (13: the temple platform); every one over the 1.22 m jump
RISERS = [2.1, 2.3, 1.9, 2.2, 2.0, 1.9, 1.8, 2.0, 1.9, 2.4, 2.2, 2.3, 2.3]   # (the low ones over the middle bands, the shallowest)
H = [0.0]
for _r in RISERS: H.append(round(H[-1] + _r, 4))
TEMPLE = H[NB + 1]                 # 27.3: the temple platform (one riser over band 12)
# band k's depth at the contour's mean (k = 1..12; they sum to the 72 m from the valley's edge to the temple's front)
# and each front edge's sweep (edge k; the valley's and the temple's are straight): every band is 4.8-8 m deep
DEPTH = [None, 6.8, 6.3, 6.3, 5.9, 5.6, 5.6, 5.6, 5.6, 6.3, 6.2, 6.0, 5.8]
SWEEP = [0, 0, 2.0, 3.5, 5.0, 6.0, 6.5, 7.0, 6.5, 6.0, 4.5, 3.0, 1.5, 0]
COLS = [(-HX + 8 * i, -HX + 8 + 8 * i) for i in range(16)]
FARM_COLS = (0, 1, 2)              # x -64..-40: bands 1-5 levelled (the farm's threshing yard)
MILL_COLS = (13, 14, 15)           # x 40..64: bands 1-3 levelled (the mill platform)
STAIR_COL = 13                     # x 40..48: bands 4-5 merged, the mill's covered stair in front of them


def h(k): return H[k]                                        # band k's dyke tops (k = 0: the valley)
def zf(k, x):                                                # band k's front edge (k = NB + 1: the temple platform's)
    if k <= 1: return VALLEY
    if k > NB: return -28.0
    return VALLEY - sum(DEPTH[1:k]) + SWEEP[k] * math.sin(x / 40.0 + 0.1 * k + 0.3)


def yard(ci, kk):   # is column ci of band kk part of a levelled yard (no paddy)?
    return ci in FARM_COLS and kk <= 5 or ci in MILL_COLS and kk <= 3 or ci == STAIR_COL and kk in (4, 5)


def col(x): return min(15, max(0, int((x + HX) // 8)))


def cross_dykes(kk): return [c for c in range(-HX + 16 * (kk % 2), HX + 1, 32) if abs(c) < HX - 2]


def kept_cross(kk):
    """band kk's cross dykes as built: each moved up to 8 m along the band to stay off the yards and clear of the
    crossings up from the band"""
    out = []
    for c0 in cross_dykes(kk):
        for c in (c0, c0 + 4, c0 - 4, c0 + 8, c0 - 8):
            if abs(c) < HX - 2 and not yard(col(c), kk) and not any(cr["span"][0] - 2 < c < cr["span"][1] + 2 for cr in CROSS.get(kk + 1, [])):
                out.append(c); break
    return out


def step_n(k): return math.ceil(RISERS[k - 1] / 0.5 - 1e-6)


# the crossings of each riser (the front of band k, from band k - 1): dicts with the dyke gap's centre x, the kind and
# the x span they take in band k - 1. Kinds: stone stairs, timber steps, a rammed earth ramp, a bamboo ladder, and a
# stone flight turned along the riser (where band k - 1 is deep enough to keep a lane beside it); 24 m apart and
# staggered from riser to riser; and from band 6 up two chains, from the yards' corners in toward the grand stair (the
# quick way to the temple: about 120 m from either spawn)
KINDS = ["stone", "ramp", "timber", "ladder", "turn"]


def flight_fits(k, x):
    """does a straight 3 m flight up riser k at x keep its steps 0.85 m deep (the flight's foot clear of band
    k - 1's front dyke)?"""
    if k <= 2: return True
    zs = [zf(k, x + d) for d in (-1.5, 0, 1.5)]
    room = min(zf(k - 1, x + d) for d in (-1.5, 0, 1.5)) - 1.6 - max(zs)
    n = step_n(k)
    return max(zs) - min(zs) + 0.6 + min(n * 0.8, room) >= 0.85 * n


def ramp_fits(k, x):   # is a 4 m ramp up riser k at x no steeper than 1 in 1.6?
    if k <= 2: return True
    zs = [zf(k, x + d) for d in (-2, 0, 2)]
    room = min(zf(k - 1, x + d) for d in (-2, 0, 2)) - 1.6 - max(zs)
    return min(5.2, room) + 0.4 >= 1.6 * RISERS[k - 1]


def crossings(k):
    if k == NB + 1:   # up to the temple platform: the grand stair and two side stairs
        return [dict(x=0, kind="grand", span=(-3, 3)), dict(x=-34, kind="stone", span=(-35.5, -32.5)), dict(x=34, kind="stone", span=(32.5, 35.5))]
    def straight(x, kind):   # (a flight where its steps fit, moved up to 6 m along the riser; else a ramp, a ladder)
        for dx in (0, 3, -3, 6, -6):
            if flight_fits(k, x + dx): return dict(x=x + dx, kind=kind, span=(x + dx - 1.5, x + dx + 1.5))
        if ramp_fits(k, x): return dict(x=x, kind="ramp", span=(x - 2.0, x + 2.0))
        return dict(x=x, kind="ladder", span=(x - 1.0, x + 1.0))
    out, x, i = [], -36 + ((k * 11) % 18), k
    while x <= 36:
        kind = KINDS[i % 5]
        if kind == "turn":
            s = 1 if i % 2 else -1
            L = step_n(k) * 0.8
            xe = x + s * 1.5; xb = xe - s * L
            xs = [xb + (xe - xb) * j / 8 for j in range(9)]
            z1 = max(zf(k, v) for v in xs) + 3.0
            if k > 2 and min(zf(k - 1, v) for v in xs) - 1.2 - z1 >= 0.9:
                out.append(dict(x=x, kind="turn", s=s, span=tuple(sorted((xb, xe)))))
            else: out.append(straight(x, "stone"))
        elif kind in ("stone", "timber"): out.append(straight(x, kind))
        else:
            w = 1.0 if kind == "ladder" else 2.0
            out.append(dict(x=x, kind=kind, span=(x - w, x + w)))
        x += 24; i += 1
    if k > 5:   # the two chains: from the yards' corners up and in toward the grand stair, 7 m in a riser
        chain = [(side * (52 - 7 * (k - 6)), ("stone", "ramp", "timber")[(k + (side > 0)) % 3]) for side in (-1, 1)]
        out = [c for c in out if all(abs(c["x"] - xx) > 9 for (xx, _) in chain)]
        for (xx, kind) in chain:
            out.append(straight(xx, kind) if kind != "ramp" or not ramp_fits(k, xx) else dict(x=xx, kind=kind, span=(xx - 2.0, xx + 2.0)))
    return out


CROSS = {kk: crossings(kk) for kk in range(2, NB + 2)}


def fields(kk):
    """band kk's paddy fields: (x0, x1, kind) between its cross dykes and the yards, kind mud / rice / lagoon"""
    runs, cur = [], None
    for (ci, (x0, x1)) in enumerate(COLS):
        if yard(ci, kk):
            if cur: runs.append(cur); cur = None
            continue
        cur = (cur[0], x1) if cur else (x0, x1)
    if cur: runs.append(cur)
    out, j = [], 0
    for (a, b) in runs:
        cuts = [a] + [c for c in kept_cross(kk) if a < c < b] + [b]
        for (p, q) in zip(cuts, cuts[1:]):
            out.append((p, q, ("mud", "rice", "lagoon")[(kk + j) % 3])); j += 1
    return out


def floor_at(kk, x):
    """band kk's floor at x: the paddy floor, or the mud's top in a mud field"""
    for (a, b, kind) in fields(kk):
        if a <= x <= b: return h(kk) - PADDY + (MUD if kind == "mud" else 0)
    return h(kk) - PADDY


def chord(f, a, b):
    """the straight line from (a, f(a)) to (b, f(b)), and how far the curve f lies past it on each side over a..b:
    (line, most the curve is south of it (larger z), most north)"""
    xs = [a + (b - a) * i / 16 for i in range(17)]
    line = lambda x: f(a) + (f(b) - f(a)) * (x - a) / (b - a)
    d = [f(x) - line(x) for x in xs]
    return line, max(0.0, max(d)), max(0.0, -min(d))


def paddy_piece(kk, a, b, water):
    """the polygon of band kk's paddy over a..b (behind its front dyke, up to the next riser), or None when it can't
    be one piece. A liquid acts as its box (pmove asks the liquids' AABBs), so a water piece's box must not reach past
    the riser below it (it would wet whoever jumps by that riser in the band below): its front edge is drawn back
    behind the dyke for that, by at most 0.35 m. Mud is solid (its exact shape): its edges only reach into the dyke
    and the riser."""
    fl, fs, fn = chord(lambda x: zf(kk, x) - 1.2, a, b)
    bl, bs, bn = chord(lambda x: zf(kk + 1, x), a, b)
    back = lambda x: bl(x) - bn - 0.02   # (into the riser above: the water / mud reaches it everywhere)
    if water:
        lo = min(zf(kk, a + (b - a) * i / 16) for i in range(17)) - 0.02
        pull = max(0.0, max(fl(a), fl(b)) - lo)
        if pull + fn > 0.35: return None
        front = lambda x: fl(x) - pull
    else:
        if fs > 0.6: return None
        front = lambda x: fl(x) + fs + 0.02   # (into the dyke)
    return [(a, front(a)), (b, front(b)), (b, back(b)), (a, back(a))]


def pieces(kk, a, b, water):
    """a..b split on the 4 m grid into as few paddy pieces as fit (at most 32 m each)"""
    out, p = [], a
    while p < b - 1e-6:
        q = min(b, (math.floor(p / 4 + 1e-6) + 1) * 4)
        poly = paddy_piece(kk, p, q, water)
        if poly is None: raise SystemExit(f"terraces: band {kk}'s paddy at x {p:g}..{q:g} is too curved for one piece")
        while q < b - 1e-6 and q + 4 - p <= 32 + 1e-6:
            pp = paddy_piece(kk, p, min(b, q + 4), water)
            if pp is None: break
            q, poly = min(b, q + 4), pp
        out.append(poly); p = q
    return out


def build():
    m = M("Rice Terraces", "vs", sun=(0.2, 1.0, 0.55))
    m.also = ["power", "koth"]
    k = Kit(m)

    gaps = {kk: [c["x"] for c in CROSS[kk]] for kk in range(2, NB + 2)}

    # ---- the valley: banks, the stream's bed and water
    slab(k, "rootyloam", -HX, VALLEY, HX, 50, -1, 0, tile=32)
    slab(k, "rootyloam", -HX, 58, HX, HZ, -1, 0, tile=32)
    slab(k, "riverbankclay", -HX, 50, HX, 58, -1.6, -0.6, tile=32)
    for (x0, x1) in ((-64, -32), (-32, 0), (0, 32), (32, 64)):
        k.box("clearlagoon", x0, -0.6, 50, x1, -0.15, 58)

    # ---- the bands: one prism per 8 m column between this band's front and the next's, solid down to -1, the
    # paddy floor 0.4 m under the dykes
    for kk in range(1, NB + 1):
        y = h(kk) - PADDY
        for (ci, (x0, x1)) in enumerate(COLS):
            if yard(ci, kk): continue
            poly = [(x0, zf(kk, x0)), (x1, zf(kk, x1)), (x1, zf(kk + 1, x1)), (x0, zf(kk + 1, x0))]
            k.prism("drystonewall", poly, -1, y)
        # its fields: water (in as few pieces as its box allows) or 0.1 m of mud on the floor (one piece to 32 m)
        for (a, b, kind) in fields(kk):
            for poly in pieces(kk, a, b, kind != "mud"):
                if kind == "mud": k.prism("mud", poly, y, y + MUD)
                else: k.prism("clearlagoon", poly, y, h(kk) - 0.05)
    # the levelled yards: the farm (bands 1-5 at 10.5) and the mill platform (bands 1-3 at 6.3), and bands 4-5 by the
    # mill's covered stair (one block at 10.5)
    for (cols, kmax, mat) in ((FARM_COLS, 5, "hardpan"), (MILL_COLS, 3, "riverstonewall"), ((STAIR_COL,), 5, "drystonewall")):
        for ci in cols:
            x0, x1 = COLS[ci]
            kmin = 4 if cols == (STAIR_COL,) else 1
            poly = [(x0, zf(kmin, x0)), (x1, zf(kmin, x1)), (x1, zf(kmax + 1, x1)), (x0, zf(kmax + 1, x0))]
            k.prism("drystonewall", poly, -1, h(kmax) - 0.5)
            k.prism(mat, poly, h(kmax) - 0.5, h(kmax))
    # the front dykes (embankments 1.2 m over the water on every second band), gapped at the crossings
    for kk in range(2, NB + 1):
        hi = h(kk) + (0.8 if kk % 2 == 0 else 0.0)
        xs = list(range(-HX, HX + 1, 4))
        for (a, b) in zip(xs, xs[1:]):
            mid = (a + b) / 2
            if yard(col(mid), kk): continue
            if any(abs(mid - g) < 4.5 for g in gaps[kk]): continue
            k.bar("dirt", (a - 0.05, zf(kk, a) - 0.6), (b + 0.05, zf(kk, b) - 0.6), 1.2, h(kk) - PADDY - 0.01, hi)
        # cross dykes every 32 m (dry lanes up and down the band), clear of the crossings up from it
        for c in kept_cross(kk):
            za = min(zf(kk, c - 0.6), zf(kk, c + 0.6)) - 1.0
            zb = max(zf(kk + 1, c - 0.6), zf(kk + 1, c + 0.6))
            k.box("dirt", c - 0.6, h(kk) - PADDY - 0.01, zb - 0.3, c + 0.6, h(kk), za)

    # ---- the crossings: stone stairs, timber steps, earth ramps, bamboo ladders, turned flights against each riser
    for kk in range(2, NB + 2):
        top = h(kk) - PADDY if kk <= NB else TEMPLE   # (onto the paddy floor, through the dyke's gap)
        for cr in CROSS[kk]:
            x, kind = cr["x"], cr["kind"]
            base = h(kk - 1) if yard(col(x), kk - 1) else floor_at(kk - 1, x if kind != "turn" else (cr["span"][0] if cr["s"] > 0 else cr["span"][1]))
            n = math.ceil((top - base) / 0.5 - 1e-6)
            if kind == "turn":   # along the riser, rising to its top step by the dyke's gap
                xa, xb = cr["span"]
                zs = [zf(kk, xa + (xb - xa) * j / 8) for j in range(9)]
                k.stairs("graniteblocks", xa, min(zs) - 0.6, xb, max(zs) + 3.0, top, "x", cr["s"], base=base)
                continue
            w = 6.0 if kind == "grand" else 3.0
            zs = [zf(kk, x + d) for d in (-w / 2, 0, w / 2)] if kk <= NB else [-28.0]
            zr0, zr1 = min(zs), max(zs)
            # (the band below's depth, its front dyke and a step in front of the flight left clear)
            room = min(zf(kk - 1, x + d) for d in (-w / 2, 0, w / 2)) - 1.6 - zr1 if kk > 2 else 6
            if kind in ("stone", "grand", "timber"):
                mat = "templestone" if kk > NB else "wideplanks" if kind == "timber" else "graniteblocks"
                k.stairs(mat, x - w / 2, zr0 - 0.6, x + w / 2, zr1 + min(n * 0.8, room), top, "z", -1, base=base)
            elif kind == "ramp":   # rammed earth
                k.ramp("mudandstrawdaub", x - 2, zr0 - 0.4, x + 2, zr1 + min(5.2, room), base, top, "-z")
            else:   # a bamboo ladder on the riser
                k.box("bamboo", x - 0.9, base, zr0 - 0.1, x + 0.9, h(kk), zr1 + 0.25)
                k.ladder(x, zr1 + 0.25, base, h(kk) - 0.2, "n", mat="bamboo")   # (its climb ends 0.8 m over the riser)

    # ---- the valley stairs: A's west stair down from the farm, B's mill race steps
    k.stairs("graniteblocks", -64, VALLEY, -40, 48, h(5), "x", -1, base=0)
    k.stairs("riverstonewall", 38, VALLEY, 50, 48, h(3), "x", 1, base=0)
    # B's covered stair: from the mill platform (6.3) up to the fifth band (10.5), a roof over it
    zz, z0 = max(zf(4, 41), zf(4, 44), zf(4, 47)), min(zf(4, 41), zf(4, 44), zf(4, 47))
    k.stairs("wideplanks", 41, z0 - 0.6, 47, zz + 9, h(5), "z", -1, base=h(3))
    for (x, z) in ((40.6, zz + 9), (47.4, zz + 9), (40.6, zz + 2), (47.4, zz + 2)):
        k.box("bamboo", x - 0.2, h(3), z - 0.2, x + 0.2, h(5) + 3.0, z + 0.2)
    k.wedge("thatch", 0, 39.5, h(5) + 3.0, zz - 1, 44, h(5) + 4.2, zz + 10)
    k.wedge("thatch", 1, 44, h(5) + 3.0, zz - 1, 48.5, h(5) + 4.2, zz + 10)

    platform(k)
    temple(k)
    wings(k)
    decorate(k)
    farm(k)
    mill(k)
    outer(k)

    # ---- spawns
    for (x, z) in ((-55.5, 36), (-51.5, 36), (-55.5, 40), (-51.5, 40)):
        k.spawn("a", x, h(5), z, yaw_to(x, z, 0, -34))
    for (x, z) in ((54, 38), (58, 38), (54, 41), (58, 41)):
        k.spawn("b", x, h(3), z, yaw_to(x, z, 0, -34))
    # free for all: in the barn and the granary (out of sight: the yards see far), the valley's bank and its east end
    # under the mill (the race steps between them), two cross dykes, the temple platform
    for (x, y, z, tx, tz) in ((-44.5, h(5), 39, -44.5, 0), (56, h(3), 30.5, 56, 60), (-30, 0, 46, 0, 0), (58, 0, 46.5, 0, 46),
                              (-30, TEMPLE, -36, 0, 0), (30, TEMPLE, -36, 0, 0)):
        k.spawn("any", x, y, z, yaw_to(x, z, tx, tz))
    for (kk, c) in ((6, 0), (8, 32)):
        kk = next(b for b in (kk, kk + 1, kk - 1) if kept_cross(b))
        c = min(kept_cross(kk), key=lambda v: abs(v - c))
        k.spawn("any", c, h(kk), (zf(kk, c) - 1.2 + zf(kk + 1, c)) / 2, 90 if c > 0 else -90)
    m.raw(f"hill -10 {TEMPLE:g} -54 10 {TEMPLE + 6:g} -38")
    # king of the hill starts on the temple's wings (map format 11 spawn sets) behind the stone screens: out of the
    # hillside's sight, a gravel path (about 35 m) to the court's side gate
    for side, team in ((-1, "koth-a"), (1, "koth-b")):
        for (x, z) in ((side * 49, -55), (side * 45, -55), (side * 49, -51.5), (side * 45, -51.5)):
            k.spawn(team, x, TEMPLE, z, yaw_to(x, z, side * 15, -46))

    m.raw("setting a sunny hillside of flooded rice terraces, a farm, a mill and a temple")
    # (accent: the rice and the loose bamboo culms, both trim)
    m.raw("palette base 0.62 0.58 0.5 accent 0.36 0.52 0.16 metal 0.3 0.3 0.3 glow 1 0.85 0.55 sky 0.55 0.75 0.8")
    m.save("terraces", dress=False)


# the platform's top: gravel paths from the koth pads to the side gates, and the east wing's pond, let into it
PATHS = [(-41, -54.5, -24, -51.5), (-27, -51.5, -24, -44.5), (-24, -47.5, -15, -44.5)]
PATHS += [(-x1, z0, -x0, z1) for (x0, z0, x1, z1) in PATHS]
POND = (29, -50.6, 38, -41)


def platform(k):
    """the temple platform (27.3 m): its stone, the paths and the pond let into its top, the parapet along its front"""
    y = TEMPLE
    k.box("drystonewall", -HX, -1, -HZ, HX, y - 0.5, -28)
    slab(k, "largetemplestone", -HX, -HZ, HX, -28, y - 0.5, y, holes=PATHS + [POND])
    for (x0, z0, x1, z1) in PATHS:
        k.box("gravel", x0, y - 0.5, z0, x1, y, z1)
    px0, pz0, px1, pz1 = POND
    k.box("riverpebbles", px0, y - 0.5, pz0, px1, y - 0.45, pz1)
    k.box("clearlagoon", px0, y - 0.45, pz0, px1, y - 0.05, pz1)
    # the bridge: a plank deck across the pond from the path toward the east side stair, rising 0.4 m to its middle;
    # posts and rails beside it
    bx0, bx1 = 32, 35
    k.ramp("wideplanks", bx0, pz0 - 0.3, bx1, pz0 + 3, y, y + 0.4, "+z")
    k.box("wideplanks", bx0, y, pz0 + 3, bx1, y + 0.4, pz1 - 3)
    k.ramp("wideplanks", bx0, pz1 - 3, bx1, pz1 + 0.3, y, y + 0.4, "-z")
    for xx in (bx0 - 0.15, bx1 + 0.15):
        for zz in (pz0 + 2.5, pz1 - 2.5):
            k.box("terracotta", xx - 0.12, y - 0.45, zz - 0.12, xx + 0.12, y + 1.4, zz + 0.12)
        k.box("terracotta", xx - 0.06, y + 1.2, pz0 + 2.62, xx + 0.06, y + 1.32, pz1 - 2.62)
    # the parapet along the front edge (1.2 m), open at the grand stair and the two side stairs
    cuts = sorted((cr["span"][0] - 0.5, cr["span"][1] + 0.5) for cr in CROSS[NB + 1])
    p = -HX
    for (a, b) in cuts + [(HX, HX)]:
        if a > p: k.box("templestone", p, y, -28.6, a, y + 1.2, -28)
        p = b


def roof(k, mat, x0, z0, x1, z1, y0, y1, tips=0.5):
    """a roof on x0..x1, z0..z1: two slopes meeting at the ridge (along x), the eaves' four corners turned up (corner
    pieces)"""
    zm = (z0 + z1) / 2
    k.wedge(mat, 3, x0, y0, zm, x1, y1, z1)   # (the south slope, rising north)
    k.wedge(mat, 2, x0, y0, z0, x1, y1, zm)
    for (cx, cz, d) in ((x0, z0, 0), (x1, z0, 1), (x1, z1, 2), (x0, z1, 3)):
        sx = 1 if d in (0, 3) else -1; sz = 1 if d in (0, 1) else -1
        k.wedge(mat, d, cx, y0, cz, cx + sx * 1.0, y0 + tips, cz + sz * 1.0, kind="corner")


def temple(k):
    """the walled court on the platform: gates south, east and west, a bell tower (ladder), the shrine at the back"""
    y = TEMPLE
    walls(k, "templestone", -15, -58, 15, -34, y, y + 4,
          [("s", 0, 6, 4.5), ("w", -46, 5, 4.5), ("e", -46, 5, 4.5)], t=0.8)
    # the pagoda gate over the south opening: posts, a beam and its roof with upturned eaves (above the opening)
    for x in (-4, 3.2):
        k.box("terracotta", x, y, -35, x + 0.8, y + 5, -33.4)
    k.box("terracotta", -5, y + 5, -34.8, 5, y + 5.5, -33.6)
    k.box("jade", -2, y + 5.05, -33.6, 2, y + 5.45, -33.45)
    roof(k, "roofshingles", -6.5, -36.4, 6.5, -32, y + 5.5, y + 6.8)
    # the shrine: a small hall at the back of the court (cover on the hill's north edge) under a two-tier roof
    walls(k, "templestone", -8, -58, 8, -54.5, y, y + 4.5, [("s", 0, 3, 3.2)])
    roof(k, "roofshingles", -9.5, -59.4, 9.5, -53.4, y + 4.5, y + 5.6)
    k.box("templestone", -5, y + 5.2, -57.6, 5, y + 6.4, -55.2)
    roof(k, "roofshingles", -6.5, -58.4, 6.5, -54.4, y + 6.4, y + 7.4, tips=0.4)
    k.disc("bronze", 0, -56.4, 0.25, y + 7.0, y + 8.6, n=6)
    # the bell tower (west corner of the court): a 4 m square on posts, a ladder up to its 12 m platform
    for (x, z) in ((-14, -57), (-10.6, -57), (-14, -53.6), (-10.6, -53.6)):
        k.box("largetemplestone", x, y, z, x + 0.6, y + 12, z + 0.6)
    k.box("wideplanks", -14.2, y + 11.6, -57.2, -9.8, y + 12, -53.2)
    roof(k, "roofshingles", -15, -58, -9, -52.4, y + 15, y + 16.2, tips=0.4)
    for (x, z) in ((-14, -57), (-10.6, -57), (-14, -53.6), (-10.6, -53.6)):
        k.box("largetemplestone", x, y + 12, z, x + 0.6, y + 15, z + 0.6)
    k.box("bamboo", -13.4, y, -53.2, -10.6, y + 11.6, -53.0)
    k.ladder(-12, -53.0, y, y + 12, "n", mat="bamboo")
    k.box("lightblock", -12.3, y + 13.5, -55.3, -11.7, y + 14.1, -54.7)
    k.light(0, y + 3.5, -50, 12, 0.5)


def pine(k, x, z, s=1.0):
    """a pine on the platform: its trunk and three tiers of boughs (the lowest 2.6 m up: you run under it)"""
    y = TEMPLE
    k.box("pinebark", x - 0.25 * s, y, z - 0.25 * s, x + 0.25 * s, y + 7.5 * s, z + 0.25 * s)
    for (r, y0, y1) in ((2.4, 2.6, 4.0), (1.8, 4.0, 5.4), (1.0, 5.4, 7.0)):
        k.disc("pineboughs", x, z, r * s, y + y0 * s, y + y1 * s, n=6)


def wings(k):
    """the temple's wings: groves either side of the court, the koth spawn pads behind their screens, the west
    wing's dormitory and stupa, the east wing's bamboo and its pond (let into the platform: its bridge leads from the path toward the side stair)"""
    y = TEMPLE
    for side in (-1, 1):
        # the screen: 3 m of stone on the pad's south side (facing the hillside), a coping on it
        x0, x1 = sorted((side * 41, side * 55))
        k.box("templestone", x0, y, -49.4, x1, y + 3.0, -48.6)
        k.box("largetemplestone", x0 - 0.2, y + 3.0, -49.6, x1 + 0.2, y + 3.4, -48.4)
        k.light(side * 47, y + 3.5, -53, 9, 0.5)
    # the groves: pines off the paths, the pads and the gates' line
    for (x, z, s) in ((-58, -36, 1.0), (-49, -33, 0.9), (-36, -32.5, 1.1), (-56, -44, 0.85), (-31, -42, 0.95),
                      (-20, -33, 0.8), (60, -38, 1.0), (50, -34, 1.1), (42, -40, 0.9), (22, -33, 0.85), (58, -45, 0.8)):
        pine(k, x, z, s)
    # the monks' dormitory (west, against the back wall): a long daub hall under a thatch roof, its veranda posts
    k.box("mudandstrawdaub", -40, y, -59.8, -27, y + 3.4, -56.2)
    roof(k, "thatch", -40.8, -60, -26.2, -54.8, y + 3.4, y + 5.2)
    for x in (-39.5, -33.5, -27.5):
        k.box("wideplanks", x - 0.15, y, -55.6, x + 0.15, y + 3.4, -55.3)
    k.light(-33.5, y + 2.8, -54, 8, 0.5)
    # the stupa (west, by the court's corner): a stepped base, the dome, its spire
    sx, sz = -24, -38
    k.disc("templestone", sx, sz, 2.0, y, y + 0.8, n=8)
    k.disc("templestone", sx, sz, 1.6, y + 0.8, y + 2.4, n=8)
    k.disc("largetemplestone", sx, sz, 1.0, y + 2.4, y + 3.0, n=8)
    k.disc("bronze", sx, sz, 0.3, y + 3.0, y + 5.0, n=6)
    # bamboo on the east wing, against the back wall (out of the path)
    for (x, z) in ((44, -59), (46.5, -58.6), (49, -59.2), (52, -58.8), (55, -59.3), (58, -58.9)):
        k.box("bamboo", x - 0.15, y, z - 0.15, x + 0.15, y + 8, z + 0.15)
    # stone lanterns along the paths (beside them, every 8 m or so)
    for side in (-1, 1):
        for (x, z) in ((-37, -55.4), (-30.5, -55.4), (-28, -43.6), (-20, -43.6), (-23, -55.4)):
            lx, lz = side * x, z
            k.box("largetemplestone", lx - 0.3, y, lz - 0.3, lx + 0.3, y + 0.9, lz + 0.3)
            k.box("templestone", lx - 0.4, y + 0.9, lz - 0.4, lx + 0.4, y + 1.0, lz + 0.4)
            k.box("lightblock", lx - 0.22, y + 1.0, lz - 0.22, lx + 0.22, y + 1.4, lz + 0.22)
            k.box("roofshingles", lx - 0.45, y + 1.4, lz - 0.45, lx + 0.45, y + 1.55, lz + 0.45)
    # a stone lantern by the pond
    k.box("largetemplestone", 39.4, y, -46.6, 40.6, y + 1.0, -45.4)
    k.box("lightblock", 39.55, y + 1.0, -46.45, 40.45, y + 1.6, -45.55)


def farm(k):
    """A's side: the two-storey farmhouse (spawns in its yard behind it), the barn"""
    y = h(5)
    walls(k, "logcabin", -63, 22, -47, 30, y, y + 7, [("e", 26, 3.4, 3.2), ("s", -55, 3.4, 3.2), ("n", -55, 2.0, 2.2, 1.0)])
    slab(k, "wideplanks", -62.6, 22.4, -47.4, 29.6, y + 3.0, y + 3.4, holes=[(-58, 22.4, -50, 24.4)])
    k.wedge("thatch", 2, -63.6, y + 7, 21.4, -46.4, y + 9.5, 26)
    k.wedge("thatch", 3, -63.6, y + 7, 26, -46.4, y + 9.5, 30.6)
    k.box("shojiscreen", -55.06, y + 3.4, 24.6, -54.94, y + 6.4, 29.6)   # (upstairs: a paper wall)
    k.stairs("wideplanks", -58, 22.4, -50, 24.4, y + 3.4, "x", -1, base=y)
    walls(k, "logcabin", -48, 35, -41, 43, y, y + 5, [("n", -44.5, 4, 3.6)])
    k.wedge("thatch", 0, -48.6, y + 5, 34.4, -44.5, y + 7, 43.6)
    k.wedge("thatch", 1, -44.5, y + 5, 34.4, -40.4, y + 7, 43.6)
    k.light(-55, y + 3, 26, 10, 0.6)


def xy_prism(k, mat, poly, z0, z1):
    """a prism along z over the convex polygon poly [(x, y)...] (the wheel's pieces)"""
    xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
    cx, cy = sum(xs) / len(xs), sum(ys) / len(ys)
    planes = []
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
        nx, ny = by - ay, -(bx - ax)
        if nx * (cx - ax) + ny * (cy - ay) > 0: nx, ny = -nx, -ny
        L = math.hypot(nx, ny)
        if L < 1e-9: continue
        nx, ny = nx / L, ny / L
        if abs(nx) < 1e-6 or abs(ny) < 1e-6: continue   # (an axis edge: the box's own face)
        planes.append((nx, ny, 0, nx * ax + ny * ay))
    k.cut(mat, (min(xs), min(ys), z0, max(xs), max(ys), z1), planes)


def wheel(k, cx, cy, z0, z1, r0=3.6, r1=4.2, n=12, paddle=0.9):
    """a water wheel standing in the x-y plane (its axle along z): a ring of n plank segments r0..r1, a paddle on each,
    three spokes through the hub"""
    for i in range(n):
        t0, t1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        xy_prism(k, "wideplanks", [(cx + r * math.cos(t), cy + r * math.sin(t)) for (r, t) in ((r0, t0), (r1, t0), (r1, t1), (r0, t1))], z0, z1)
        tm = (t0 + t1) / 2
        ux, uy = math.cos(tm), math.sin(tm)
        px, py = -uy * 0.06, ux * 0.06
        a, b = r1 - 0.05, r1 + paddle
        xy_prism(k, "wideplanks", [(cx + a * ux + px, cy + a * uy + py), (cx + b * ux + px, cy + b * uy + py),
                                   (cx + b * ux - px, cy + b * uy - py), (cx + a * ux - px, cy + a * uy - py)], z0 - 0.1, z1 + 0.1)
    for t in (math.pi / 6, math.pi / 2, 5 * math.pi / 6):
        ux, uy = math.cos(t), math.sin(t)
        px, py = -uy * 0.1, ux * 0.1
        r = r0 + 0.05
        xy_prism(k, "wideplanks", [(cx + r * ux + px, cy + r * uy + py), (cx + r * ux - px, cy + r * uy - py),
                                   (cx - r * ux - px, cy - r * uy - py), (cx - r * ux + px, cy - r * uy + py)], (z0 + z1) / 2 - 0.2, (z0 + z1) / 2 + 0.2)


def mill(k):
    """B's side: the stone mill (spawns on its floor), its water wheel in the stream, the granary"""
    y = h(3)
    walls(k, "riverstonewall", 50, 34, 62, 44, y, y + 6, [("w", 35.6, 2.2, 3.4), ("n", 56, 3.4, 3.4), ("n", 52, 2.2, 3.4)])
    k.box("wideplanks", 50.4, y - 0.05, 34.4, 61.6, y, 43.6)
    k.wedge("roofshingles", 2, 49.4, y + 6, 33.4, 62.6, y + 8.5, 39)
    k.wedge("roofshingles", 3, 49.4, y + 6, 39, 62.6, y + 8.5, 44.6)
    # the wheel: a ring of planks (r 3.6-4.2, 12 segments) with its paddles, upright in the stream (dipping into it),
    # its iron axle back over the bank into the mill's wall (overhead: you walk under it)
    cx, cy = 56.0, 3.4
    wheel(k, cx, cy, 50.6, 51.8)
    k.box("castiron", cx - 0.3, cy - 0.3, 44, cx + 0.3, cy + 0.3, 52.1)
    walls(k, "riverstonewall", 52, 28, 60, 32.5, y, y + 4, [("s", 56, 3, 3)])
    k.box("roofshingles", 51.6, y + 4, 27.6, 60.4, y + 4.4, 32.9)
    k.light(56, y + 4, 39, 10, 0.6)


def outer(k):
    """the map's edge: walls stepped up the hill on the west and east, the valley's south bank, the temple's back
    wall; clip over them up to 85 m (the bell tower's roof is 43.5 m)"""
    for x0, x1 in ((-HX - 2, -HX), (HX, HX + 2)):   # (a bamboo grove: its top climbs the hill with the bands)
        k.box("bamboo", x0, -1, -28, x1, 5, HZ + 2)
        k.ramp("bamboo", x0, -28, x1, HZ + 2, 5, TEMPLE + 6, "-z")
        k.box("bamboo", x0, -1, -HZ - 2, x1, TEMPLE + 6, -28)
    k.box("bamboo", -HX - 2, -1, HZ, HX + 2, 5, HZ + 2)
    k.box("templestone", -HX - 2, -1, -HZ - 2, HX + 2, TEMPLE + 6, -HZ)
    k.box("clip", -HX - 2, -1, -HZ - 2, -HX, 85, HZ + 2)
    k.box("clip", HX, -1, -HZ - 2, HX + 2, 85, HZ + 2)
    k.box("clip", -HX, 5, HZ, HX, 85, HZ + 2)
    k.box("clip", -HX, TEMPLE + 6, -HZ - 2, HX, 85, -HZ)
    k.box("clip", -HX, 85, -HZ, HX, 86, HZ)


HAYSTACKS = ((3, -20), (4, 22), (6, -30), (7, 8), (8, 34), (9, -10), (10, 20), (11, -36), (11, 14), (11, -10, "back"), (12, -20), (12, 44))


def decorate(k):
    """sparingly, off the dykes and crossings: field huts (woven walls: shoot-through) and haystacks on a few paddies
    (the long lines along the bands broken every 30-40 m), the rice (non-solid rows in half the flooded fields), stone
    lanterns and an incense burner in the court (cover on the hill), lanterns on the gate, stepping stones along the
    stream's edges, bamboo clumps at the bands' ends and loose culms in front of the grove walls"""
    import random
    R = random.Random("terraces")
    def spans(kk): return [cr["span"] for cr in CROSS.get(kk, []) + CROSS.get(kk + 1, [])]
    def free(kk, x, r, back=False):   # (back: against the riser, clear of the flights up it only)
        if back: return all(x + r + 2 < a or x - r - 2 > b for (a, b) in (cr["span"] for cr in CROSS.get(kk + 1, []))) and all(abs(x - c) > r + 1 for c in kept_cross(kk))
        return all(x + r + 2 < a or x - r - 2 > b for (a, b) in spans(kk)) and all(abs(x - c) > r + 1 for c in kept_cross(kk))
    huts, stacks = 0, []
    for (kk, x, *where) in HAYSTACKS:
        x = next((x + d for d in sorted(range(-20, 21, 2), key=abs) if abs(x + d) < HX - 6 and free(kk, x + d, 2.5, bool(where))), None)
        if x is None: continue
        zc = (zf(kk, x) - 1.2 + zf(kk + 1, x)) / 2 if not where else zf(kk + 1, x) + 1.1   # (back: against the riser)
        y = h(kk) - PADDY
        stacks.append((kk, x))
        if huts < 4 and kk % 2 and not where:   # a hut: woven walls (1/8 m: shoot-through), a door each side, a thatch roof
            for (x0, z0, x1, z1) in ((x - 2, zc - 1.5, x + 2, zc - 1.44), (x - 2, zc + 1.44, x + 2, zc + 1.5)):
                k.box("bamboo", x0, y, z0, x1, y + 2.6, z1)
            for xx in (x - 2, x + 1.94):
                k.box("bamboo", xx, y, zc - 1.44, xx + 0.06, y + 0.9, zc + 1.44)
            k.wedge("thatch", 2, x - 2.4, y + 2.6, zc - 1.9, x + 2.4, y + 3.8, zc)
            k.wedge("thatch", 3, x - 2.4, y + 2.6, zc, x + 2.4, y + 3.8, zc + 1.9)
            huts += 1
        else:                     # haystacks (hay bales: only blasts break them)
            k.box("haybales", x - 1.2, y, zc - 1.0, x + 1.2, y + 1.6, zc + 1.0)
            k.box("haybales", x - 0.6, y + 1.6, zc - 0.6, x + 0.6, y + 2.4, zc + 0.6)
    # the rice: rows 0.9 m apart along the band in the planted fields, in clumps of 3 m (the gaps between: tufts),
    # 0.45 m over the water, non-solid (trim: nothing to snag on, no cover), clear of the flights and the haystacks
    for kk in range(1, NB + 1):
        for (a, b, kind) in fields(kk):
            if kind != "rice": continue
            xa = a + 1.0
            while xa + 3 <= b - 1.0:
                xb = xa + 3.0
                if all(xb + 1 < sa or xa - 1 > sb for (sa, sb) in spans(kk)) and not any(kk == sk and xa - 4.5 < sx < xb + 4.5 for (sk, sx) in stacks) and abs(xa) < HX - 4 and abs(xb) < HX - 4:
                    d = 0.7
                    while True:
                        za, zb = zf(kk, xa) - 1.2 - d, zf(kk, xb) - 1.2 - d
                        if min(za - zf(kk + 1, xa), zb - zf(kk + 1, xb)) < 0.6: break
                        k.bar("trim", (xa, za), (xb, zb), 0.16, h(kk) - 0.2, h(kk) + 0.45)
                        d += 0.9
                xa = xb + 0.8
    # the irrigation spouts: a bamboo pipe out of each riser's face (one a column, under its top) over the flooded
    # field below it; on the wall, non-solid
    for kk in range(2, NB + 2):
        for (ci, (x0, x1), t) in [(ci, c, t) for (ci, c) in enumerate(COLS) for t in ((0.5,) if kk < 6 else (0.25, 0.75))]:
            x = x0 + (x1 - x0) * t + R.uniform(-1.2, 1.2)
            if yard(ci, kk - 1) or (kk <= NB and yard(ci, kk)): continue
            if any(sa - 1.5 < x < sb + 1.5 for (sa, sb) in (cr["span"] for cr in CROSS[kk])): continue
            if not any(a <= x <= b and kind != "mud" for (a, b, kind) in fields(kk - 1)): continue
            z = zf(kk, x); y = (h(kk) if kk <= NB else TEMPLE) - 0.75
            k.box("trim", x - 0.07, y, z - 0.2, x + 0.07, y + 0.14, z + 0.55)
    # the court: two stone lanterns and the incense burner (cover on the hill), lanterns on the gate
    y = TEMPLE
    for (x, z) in ((-6, -42), (6, -50)):
        k.box("largetemplestone", x - 0.6, y, z - 0.6, x + 0.6, y + 1.0, z + 0.6)
        k.box("templestone", x - 0.3, y + 1.0, z - 0.3, x + 0.3, y + 1.8, z + 0.3)
        k.box("lightblock", x - 0.45, y + 1.8, z - 0.45, x + 0.45, y + 2.4, z + 0.45)
    k.disc("bronze", 4.5, -42.5, 1.3, y, y + 1.1, n=8)   # (off the hill's centre: the bots' goal)
    k.disc("bronze", 4.5, -42.5, 0.9, y + 1.1, y + 1.5, n=8)
    for x in (-5.2, 5.2):
        k.box("lightblock", x - 0.3, y + 3.2, -33.2, x + 0.3, y + 4.0, -32.6)
    # stepping stones along the stream's edges (out of its middle)
    for i, x in enumerate(range(-56, 57, 16)):
        rock(k, "riverstonewall", x + R.uniform(-3, 3), 51.2 if i % 2 else 56.8, 0.9, -0.6, 0.7, seed=i)
    # bamboo clumps at the ends of the bands (against the outer walls)
    for kk in range(5, NB + 1, 2):
        for side in (-1, 1):
            x = side * (HX - 2.5)
            z = (zf(kk, x) - 1.6 + zf(kk + 1, x)) / 2
            for (dx, dz) in ((0, 0), (1.1, 0.8), (-0.4, 1.3)):
                k.box("bamboo", x + side * -abs(dx) - 0.15, h(kk) - PADDY, z + dz - 0.15, x + side * -abs(dx) + 0.15, h(kk) + 7, z + dz + 0.15)
    # loose culms in front of the grove walls (non-solid: hugging the wall, out of the band ends' walk line)
    for kk in range(1, NB + 1):
        for side in (-1, 1):
            zfr, zbk = zf(kk, side * HX) - (1.2 if kk > 1 else 0), zf(kk + 1, side * HX)
            for (t, dx) in ((0.15, 0.25), (0.4, 0.45), (0.6, 0.3), (0.85, 0.4)):
                if kk % 2 and kk >= 5 and 0.3 < t < 0.7: continue   # (the clumps' place)
                if side < 0 and kk <= 5 or side > 0 and kk <= 3: continue   # (the yards' corners: the farm, the mill)
                x, z = side * (HX - dx), zfr - t * (zfr - zbk)
                k.box("trim", x - 0.07, h(kk) - PADDY, z - 0.07, x + 0.07, h(kk) + 5.5 + 2.5 * t, z + 0.07)
