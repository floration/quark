"""The map build's tidy pass (M.save runs it on every generated map before writing it): the map editor's clean-ups
(Quarkology map_editor/js: fixers.js Fix z-fighting / Fix spawns, fixmap.js Fix map / Fix gaps, csg.js carve), so a
generated map ships already fixed. It works on the map's own box lines (box / accent / <material> lines and the
wedge / corner / tetra / cut lines); markers, zones, ladders, lights, stairs and groups are never touched, and a
slope or cut box is never changed. In order, again until nothing changes:
  1. z-fighting: two pieces that overlap and share a face plane facing the same way (both min, or both max, equal on
     some axis) flicker there. The smaller one (by volume; equal: the lower index) stays and its box is carved out of
     the bigger one (up to 6 slabs, exact). Two identical pieces: the later one goes. Where that would change what a
     shot goes through (map.h wallbangBrush: a piece 1/8 m thin or less is shot through; a thin trim strip run into a
     thick wall would leave a slot of only the strip through it, or a slab of the wall that thin), the smaller one
     loses what is inside the bigger one instead ("trimmed"), as it does where that makes fewer pieces and keeps
     some of the smaller one (a rung across a post: carving would cut the post up at every rung).
  2. merge: plain boxes of the same material whose union is exactly one box (flush, a whole face shared) become one.
  3. hidden: a plain box whose every face is covered all over, just outside it, by solid opaque boxes goes, when
     those boxes fill it too (no sealed void is left).
  4. gaps: two plain boxes with facing faces less than 1/8 m apart: the face off the 1/8 m grid moves out to the
     one on it (both off: both to the grid line between them, or the one farther off to the other), when the face
     that moves lies within the other one (it only fills the slot between them: a floor's top never rises where it
     is walked on, a wall never moves out into the open) and nothing else, a staircase or a ladder is in the way. The
     only step that adds solid space: slots no player fits in and shots slipped through.
  5. spawns: a spawn whose player hull is in something solid goes up 1/8 m at a time until it is clear (none is
     today; one still stuck after 64 m stops the build).
Steps 1-3 keep the solid space, what stops a shot and every gameplay material exactly where they were. So a pair is
left as it is (counted by reason in the report) when either piece is a material that does something (water, lava,
the liquids, acid, ice, rubber, asphalt, mud, nograpple, wallbang, clip, the ball wall, glass, breakables, the
see-through looks, the feature blocks, teleporters, ...; see cosmetic and per_point) or a slope / cut box. The
editor's other reasons are kept (two pieces of the same material: the flicker barely shows and cutting adds pieces;
one solid and one not). Deterministic: the same input gives the same output, byte for byte."""
import os, re, math

_HERE = os.path.dirname(os.path.abspath(__file__))
_MAP_H = os.path.join(_HERE, "..", "..", "src", "game", "map.h")


def _load():
    t = open(_MAP_H, encoding="utf-8").read()
    i = t.index("kMapMaterialWords[] = {")
    words = re.findall(r'"([a-z0-9]+)"', t[i:t.index("};", i)])
    i = t.index("kSemi[kSemiCount] = {")
    semi = [tuple(v == "true" for v in m) for m in re.findall(r"\{(true|false), (true|false), (true|false)\}", t[i:t.index("};", i)])]
    return words, semi


WORDS, _SEMI = _load()
INDEX = {w: n for n, w in enumerate(WORDS)}
N_MATS = len(WORDS)
LIQ0, LIQN = 472, 25
LARGE0, LARGEN = 497, 40
SEMI0, SEMIN = 537, 25
BREAK0, BREAKN = 562, 24
FEAT0 = 586
HEAL, SPIKES, LOWGRAV, SLOWMO, GRAVFLIP, GATE, LIGHTBLOCK, SHIELD, CRUMBLE, TELEPORT, BALLWALL = range(FEAT0, FEAT0 + 11)
assert N_MATS == FEAT0 + 11 and len(_SEMI) == SEMIN and WORDS[TELEPORT] == "teleporter" and WORDS[BALLWALL] == "ballwall"

EPS, PLANE, M_EPS = 1e-4, 1e-6, 1e-6
GRID = 0.125
GAP_MAX = 0.125
THIN = 0.125 + 1e-3          # map.h kWallbangThin
SPAWN_STEP, SPAWN_MAX_STEPS = 0.125, 512


# ---- materials ----
def liquid(m): return LIQ0 <= m < LIQ0 + LIQN
def semi(m): return SEMI0 <= m < SEMI0 + SEMIN
def breakm(m): return BREAK0 <= m < BREAK0 + BREAKN
def glass(m): return m in (21, 37)
def hidden_mat(m): return m == 9 or m == BALLWALL   # map.h hiddenMaterial: never drawn


def cosmetic(m):
    """a material that only looks: solid and plain (or the non-solid looks far / trim), nothing in pmove, the server
    or the ball reads it (map.h: box, accent, metal, glow, far, trim, the plain looks 13-35 but asphalt and mud, the
    100 blocks and the picks 39-471, the large patterns 497-536; glass and the breakable kinds are not)"""
    return (m in (0, 1, 3, 4, 5, 8) or (13 <= m <= 35 and m not in (17, 21, 22, 23)) or 39 <= m < LIQ0
            or LARGE0 <= m < LARGE0 + LARGEN)


def per_point(m):
    """a material whose effect is the same at every point of it, so two flush boxes of it that become one play the
    same (FIX MAP's merge): the cosmetic ones, the plain gameplay floors / walls (wallbang, clip, nograpple, ice,
    rubber, asphalt, mud), and water, lava, acid and the liquids (map.cpp rebuildFluids: each box is only asked
    whether you touch it). Not glass or anything that breaks, crumbles, links, counts, lights or pushes by the piece
    (teleporter, checkpoint gate, crumble platform, light block, gravity flip, the fields, the see-through looks)."""
    return grows_ok(m) or m in (6, 7, 11) or liquid(m)


def grows_ok(m):
    """a material a face of which may move out to close a gap under 1/8 m (FIX GAPS): the cosmetic ones and the
    solid plain gameplay floors / walls; never a liquid, lava or acid (it would reach further)"""
    return cosmetic(m) or m in (2, 9, 10, 12, 17, 22, 23)


def solid(m):
    """validate.js solid (what the editor's spawn check counts): players collide with it"""
    if m < 5 or m in (9, 10): return True
    if m < 12 or m >= N_MATS or liquid(m): return False
    if semi(m): return _SEMI[m - SEMI0][0]
    return m not in (LOWGRAV, SLOWMO, GATE, SHIELD, BALLWALL)


def solid_material(m):   # map.h solidMaterial
    if semi(m): return any(_SEMI[m - SEMI0])
    return m < 5 or m == 10 or (12 <= m < N_MATS and not liquid(m) and m not in (LOWGRAV, SLOWMO, GATE, BALLWALL))


def stops_shots(m):   # map.h stopsShots (of what is in the grid)
    return m != 9 and (not semi(m) or _SEMI[m - SEMI0][1])


class B:
    __slots__ = ("mat", "mn", "mx", "shape", "kw", "tail", "line", "origin", "uid")
    _n = [0]

    def __init__(s, mat, mn, mx, shape, kw, tail, line, origin):
        s.mat, s.mn, s.mx, s.shape, s.kw, s.tail, s.line, s.origin = mat, mn, mx, shape, kw, tail, line, origin
        B._n[0] += 1; s.uid = B._n[0]   # (a serial: the pair index's order, the same every run)

    def copy(s, mn, mx):   # a plain box of the same material and line, a new box (its line rewritten)
        return B(s.mat, [tidy(v) for v in mn], [tidy(v) for v in mx], s.shape, s.kw, s.tail, None, s.origin)


def tidy(v): return math.floor(v * 1e5 + 0.5) / 1e5
def vol(b): return (b.mx[0] - b.mn[0]) * (b.mx[1] - b.mn[1]) * (b.mx[2] - b.mn[2])
def thinnest(b): return min(b.mx[0] - b.mn[0], b.mx[1] - b.mn[1], b.mx[2] - b.mn[2])


def hard(b):
    """does b stop a hitscan shot (in the grid, stops shots, not wallbang: map.h wallbangBrush)"""
    m = b.mat
    return (solid_material(m) or m == 9) and stops_shots(m) and m != 2 and thinnest(b) > THIN


def fmt(v):
    if v == 0: return "0"
    s = ("%.5f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


# ---- parsing ----
def parse(lines):
    """the map's own pieces (B, in file order) and its custom groups' parts (world boxes, as covers)"""
    out, parts, group = [], [], None
    for n, l in enumerate(lines):
        h = l.find("#")
        t = (l[:h] if h >= 0 else l).split()
        if not t: continue
        kw = t[0]
        try:
            if kw in INDEX and len(t) >= 7:
                v = [float(x) for x in t[1:7]]
                out.append(B(INDEX[kw], v[:3], v[3:], None, kw, t[7:], l, n))
            elif kw in ("wedge", "corner", "tetra"):
                mat = INDEX[t[1]] if t[1] in INDEX else int(t[1])
                v = [float(x) for x in t[3:9]]
                out.append(B(mat, v[:3], v[3:], (kw, int(t[2])), kw, None, l, n))
            elif kw == "cut":
                c = int(t[2]); v = [float(x) for x in t[3 + 4 * c:9 + 4 * c]]
                out.append(B(int(t[1]), v[:3], v[3:], ("cut", [[float(x) for x in t[3 + 4 * k:7 + 4 * k]] for k in range(c)]), kw, None, l, n))
            elif kw == "group":
                v = [float(x) for x in t[4:10]]
                group = (t[1], int(t[2]), v[:3], v[3:])
            elif kw == "part" and group and group[0] == "custom" and len(t) == 8:   # (a box part: a cover)
                v = [float(x) for x in t[2:8]]; g0, g1 = group[2], group[3]
                sz = [g1[k] - g0[k] for k in range(3)]
                parts.append(B(int(t[1]), [g0[k] + v[k] * sz[k] for k in range(3)], [g0[k] + v[3 + k] * sz[k] for k in range(3)], None, "part", None, None, -1))
        except (ValueError, IndexError, KeyError):
            continue   # (a line the game would refuse: left as it is)
    return out, parts


# ---- 1. z-fighting (fixers.js zfightPlan) ----
def zfights(a, b):
    for k in (0, 1, 2):
        if min(a.mx[k], b.mx[k]) - max(a.mn[k], b.mn[k]) <= EPS: return False
    for k in (0, 1, 2):
        if abs(a.mn[k] - b.mn[k]) < PLANE or abs(a.mx[k] - b.mx[k]) < PLANE: return True
    return False


def same_box(a, b):
    return all(abs(a.mn[k] - b.mn[k]) < PLANE and abs(a.mx[k] - b.mx[k]) < PLANE for k in (0, 1, 2))


def skip_reason(small, big):
    # (the editor's reasons first, in its order; then this pass's own: gameplay materials, what is shot through)
    if hidden_mat(small.mat) or hidden_mat(big.mat): return "hidden"
    if big.shape: return "shapedBig"
    if small.shape: return "shapedSmall"
    if glass(big.mat) or big.mat in (36, 38) or breakm(big.mat): return "fragile"
    if solid(small.mat) != solid(big.mat): return "solidity"
    if not cosmetic(small.mat) or not cosmetic(big.mat): return "gameplay"
    if small.mat == big.mat and not same_box(small, big): return "same"
    return None


def carve(b, c):
    """box b less box c: [b] when they don't overlap, else the up to 6 boxes of b outside c (csg.js carve)"""
    lo = [max(b.mn[k], c.mn[k]) for k in (0, 1, 2)]
    hi = [min(b.mx[k], c.mx[k]) for k in (0, 1, 2)]
    if not all(hi[k] - lo[k] > EPS for k in (0, 1, 2)): return [b]
    out, mn, mx = [], b.mn[:], b.mx[:]
    for k in (0, 1, 2):
        if lo[k] - mn[k] > EPS:
            t = mx[:]; t[k] = lo[k]; out.append(b.copy(mn, t))
        if mx[k] - hi[k] > EPS:
            f = mn[:]; f[k] = hi[k]; out.append(b.copy(f, mx))
        mn[k] = lo[k]; mx[k] = hi[k]
    return out


class HGrid:
    """boxes (grown by pad) in a hierarchical grid: a box goes in the level whose cells (C x 2^level m) are at least
    its size, so it is in 8 cells at most. add(obj); near(box) -> the objects whose grown boxes may overlap it"""
    def __init__(s, pad, C=2.0):
        s.pad, s.C, s.levels = pad, C, {}

    def add(s, b):
        fl, p, C = math.floor, s.pad, s.C
        a = (b.mn[0] - p, b.mn[1] - p, b.mn[2] - p, b.mx[0] + p, b.mx[1] + p, b.mx[2] + p)
        size, L = max(a[3] - a[0], a[4] - a[1], a[5] - a[2]), 0
        while C * (1 << L) < size and L < 40: L += 1
        g, w = s.levels.setdefault(L, {}), C * (1 << L)
        for x in range(fl(a[0] / w), fl(a[3] / w) + 1):
            for y in range(fl(a[1] / w), fl(a[4] / w) + 1):
                for z in range(fl(a[2] / w), fl(a[5] / w) + 1):
                    l = g.get((x, y, z))
                    if l is None: g[(x, y, z)] = [b]
                    else: l.append(b)
        return L

    def near(s, b, grow=0.0, lo=0, hi=99):
        """the objects in the cells of b (grown by grow) at the levels lo <= level < hi"""
        out, fl = set(), math.floor
        bx0, by0, bz0 = b.mn[0] - grow, b.mn[1] - grow, b.mn[2] - grow
        bx1, by1, bz1 = b.mx[0] + grow, b.mx[1] + grow, b.mx[2] + grow
        for L, g in s.levels.items():
            if L < lo or L >= hi: continue
            r = 1.0 / (s.C * (1 << L))
            x0, y0, z0, x1, y1, z1 = fl(bx0 * r), fl(by0 * r), fl(bz0 * r), fl(bx1 * r), fl(by1 * r), fl(bz1 * r)
            if x0 == x1 and y0 == y1 and z0 == z1:
                l = g.get((x0, y0, z0))
                if l: out.update(l)
                continue
            if (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > len(g):   # (a big box over a fine level: its cells)
                for (x, y, z), l in g.items():
                    if x0 <= x <= x1 and y0 <= y <= y1 and z0 <= z <= z1: out.update(l)
                continue
            for x in range(x0, x1 + 1):
                for y in range(y0, y1 + 1):
                    for z in range(z0, z1 + 1):
                        l = g.get((x, y, z))
                        if l: out.update(l)
        return out


class PairIndex:
    """the pairs of pieces whose boxes, grown by pad, overlap on every axis (but two slopes / cut boxes: nothing here
    changes those), kept up to date as the list changes: pieces are never changed in place (a change is a new
    piece), so only new ones are looked up. pairs(lst) -> index pairs (i < j), sorted."""
    def __init__(s, pad):
        s.pad, s.every, s.plain, s.known, s.found, s.by, s.prev = pad, HGrid(pad), HGrid(pad), set(), {}, {}, None   # (known keeps every piece seen)

    def pairs(s, lst):
        if s.prev is not None and len(lst) == len(s.prev) and all(a is b for a, b in zip(lst, s.prev)):
            return [p for p, _ in s.last]   # (nothing changed since the last look-up)
        at = {b: i for i, b in enumerate(lst)}
        found, by = s.found, s.by
        if s.prev is not None:   # (the pairs of pieces that have gone, out)
            for b in s.prev:
                if b not in at:
                    for k in by.pop(b, ()): found.pop(k, None)
        new = [b for b in lst if b not in s.known]
        lv = {}
        for b in new:
            s.known.add(b); lv[b] = s.every.add(b)
            if b.shape is None: s.plain.add(b)
        p2, first = 2 * s.pad, len(new) == len(lst)
        for b in new:
            # (its level and the coarser ones; the finer ones only for pieces from before: a new finer one finds b.
            # A slope looks up only the plain boxes)
            L0 = lv[b]
            x0, y0, z0 = b.mn[0] - p2, b.mn[1] - p2, b.mn[2] - p2
            x1, y1, z1 = b.mx[0] + p2, b.mx[1] + p2, b.mx[2] + p2
            g = s.every if b.shape is None else s.plain
            for fine in ((False,) if first else (False, True)):
                for o in (g.near(b, p2, 0, L0) if fine else g.near(b, p2, L0)):
                    if o is b or (fine and o in lv) or o not in at: continue
                    omn, omx = o.mn, o.mx
                    if omn[0] < x1 and x0 < omx[0] and omn[1] < y1 and y0 < omx[1] and omn[2] < z1 and z0 < omx[2]:
                        k = (b, o) if b.uid < o.uid else (o, b)
                        if k not in found:
                            found[k] = zfights(b, o)
                            by.setdefault(b, []).append(k); by.setdefault(o, []).append(k)
        s.prev = lst[:]
        s.last = [((at[a], at[b]) if at[a] < at[b] else (at[b], at[a]), v) for (a, b), v in found.items()]
        s.last.sort()
        return [p for p, _ in s.last]

    def zpairs(s, lst):   # (the z-fighting ones)
        s.pairs(lst)   # (brings s.last up to date)
        return [p for p, v in s.last if v]


def pairs_of(lst, index):
    """the z-fighting pairs (i < j)"""
    return index.zpairs(lst)


def zfight_pass(brushes, skipped, index):
    """-> the list, pairs fixed, pieces removed. A pair is fixed as the editor does (the smaller piece's box carved
    out of the bigger one), or, where that would change what is shot through (a thin, wallbang trim strip run into
    a thick wall: the wall would be only the strip there; or a slab of the bigger one left 1/8 m thin), the other way
    round: the smaller piece loses what is inside the bigger one ("trimmed": the bigger one's face shows there). What
    stops a shot is the same either way (map.h wallbangBrush); the union of the pieces is too."""
    lst, fixed, removed, ntrim, seen = brushes[:], 0, 0, 0, set()
    for _ in range(64):
        pairs = []
        for (i, j) in pairs_of(lst, index):
            vi, vj = vol(lst[i]), vol(lst[j])
            pairs.append((vj, j, i) if vj < vi - 1e-12 else (vi, i, j))
        pairs.sort(key=lambda p: (p[0], p[1]))
        done = {}               # (a piece's parts so far: a bigger one carved by every smaller one in turn)
        trimmed, kept = set(), set()   # (smaller pieces trimmed this pass, and the bigger ones that trimmed them)
        for (_, s, b) in pairs:
            small, big = lst[s], lst[b]
            why = skip_reason(small, big)
            if not why:
                hs, hb = hard(small), hard(big)
                was = done.get(b, [big])
                parts = [x for w in was for x in carve(w, small)]
                if len(parts) == len(was) and all(x is y for x, y in zip(parts, was)): continue
                cut = carve(small, big)   # (the other way round: the smaller one less the bigger one)
                carve_ok = (hs or not hb) and all(hard(x) == hb for x in parts if x not in was)
                trim_ok = (hb or not hs) and all(hard(x) == hs for x in cut)
                # (the trim where carving can't keep what stops a shot, or where it makes fewer pieces and leaves
                # some of the smaller one: a rung across a post, a floor patch run into a wall)
                if trim_ok and (not carve_ok or (cut and len(cut) - 1 < len(parts) - len(was))):
                    if s in done or s in trimmed or b in done or b in trimmed: continue   # (next pass, against the pieces as they are then)
                    if not cut: removed += 1
                    else: fixed += 1
                    done[s] = cut; trimmed.add(s); kept.add(b); ntrim += 1
                    continue
                if carve_ok:
                    if s in trimmed or b in kept: continue
                    if not parts: removed += 1
                    else: fixed += 1
                    done[b] = parts
                    continue
                why = "wallbang"
            key = (tuple(small.mn), tuple(small.mx), small.mat, tuple(big.mn), tuple(big.mx), big.mat)
            if key not in seen: seen.add(key); skipped[why] = skipped.get(why, 0) + 1
        if not done:
            break
        lst = [x for i, b in enumerate(lst) for x in (done[i] if i in done else (b,))]
    return lst, fixed, removed, ntrim


# ---- 2. merge (fixmap.js mergeBoxes) ----
def merge_pass(brushes, skipped):
    lst, merged, frm, seen = brushes[:], set(), 0, set()
    can = lambda b: not b.shape and not glass(b.mat) and b.mat not in (36, 38)   # (as the editor: never glass, breakable brick)

    def skip(why, a, b):   # (counted once a pair)
        k = (why, tuple(a.mn), tuple(a.mx), tuple(b.mn), tuple(b.mx))
        if k not in seen: seen.add(k); skipped[why] = skipped.get(why, 0) + 1
    for _ in range(64):
        changed = False
        for k in (0, 1, 2):
            u, v = (k + 1) % 3, (k + 2) % 3
            key = lambda b, end: (b.mat, b.mn[u], b.mx[u], b.mn[v], b.mx[v], end)
            by_min = {}
            for i, b in enumerate(lst):
                if can(b): by_min.setdefault(key(b, b.mn[k]), []).append(i)
            gone = set()
            for i in range(len(lst)):
                a = lst[i]
                if i in gone or not can(a): continue
                while True:
                    l = by_min.get(key(a, a.mx[k]))
                    if not l: break
                    j, m = None, None
                    for jj in l:
                        if jj == i or jj in gone: continue
                        b = lst[jj]
                        if not per_point(a.mat):   # (a material that acts by the piece: a teleporter, a gate, ...)
                            skip("mergeGameplay", a, b); continue
                        mm = B(a.mat, a.mn[:], a.mx[:], None, a.kw, a.tail, None, a.origin)
                        mm.mx[k] = b.mx[k]
                        if hard(mm) != hard(a) or hard(mm) != hard(b):   # (thin pieces stacked along their thin side: not wallbang as one)
                            skip("mergeThin", a, b); continue
                        j, m = jj, mm; break
                    if j is None: break
                    b = lst[j]
                    if a.uid not in merged: frm += 1
                    if b.uid not in merged: frm += 1
                    merged.discard(a.uid); merged.discard(b.uid); merged.add(m.uid)
                    lst[i] = a = m; gone.add(j); changed = True
            if gone: lst = [b for i, b in enumerate(lst) if i not in gone]
        if not changed: break
    into = sum(1 for b in lst if b.uid in merged)
    return lst, frm, into


# ---- 3. hidden (fixmap.js hiddenBoxes) ----
def covers(b):
    """hides what is behind it: solid, opaque, not shot through (not glass, wallbang, thin, breakable; map.h)"""
    m = b.mat
    if b.shape: return False
    ok = (m < 5 and m != 2) or m == 10 or 12 <= m < 139 or LARGE0 <= m < LARGE0 + LARGEN
    return ok and not glass(m) and m not in (36, 37, 38) and hard(b)


def _grid(boxes, C=4.0):
    cells, big = {}, []
    for i, b in enumerate(boxes):
        x0, x1, z0, z1 = math.floor(b.mn[0] / C), math.floor(b.mx[0] / C), math.floor(b.mn[2] / C), math.floor(b.mx[2] / C)
        if (x1 - x0 + 1) * (z1 - z0 + 1) > 400: big.append(i); continue
        for x in range(x0, x1 + 1):
            for z in range(z0, z1 + 1): cells.setdefault((x, z), []).append(i)

    def near(mn, mx):
        s = set(big)
        for x in range(math.floor(mn[0] / C), math.floor(mx[0] / C) + 1):
            for z in range(math.floor(mn[2] / C), math.floor(mx[2] / C) + 1): s.update(cells.get((x, z), ()))
        return s
    return near


def _buried(b, rs_all):
    for a in (0, 1, 2):
        u, v = (a + 1) % 3, (a + 2) % 3
        if b.mx[u] - b.mn[u] < M_EPS or b.mx[v] - b.mn[v] < M_EPS: continue
        for sgn in (-1, 1):
            xo = (b.mx[a] if sgn > 0 else b.mn[a]) + sgn * 0.01
            rs = [o for o in rs_all if o.mn[a] < xo < o.mx[a] and o.mn[u] < b.mx[u] and o.mx[u] > b.mn[u] and o.mn[v] < b.mx[v] and o.mx[v] > b.mn[v]]
            if not rs: return False
            cu, cv = {b.mn[u], b.mx[u]}, {b.mn[v], b.mx[v]}
            for o in rs:
                for w in (o.mn[u], o.mx[u]):
                    if b.mn[u] < w < b.mx[u]: cu.add(w)
                for w in (o.mn[v], o.mx[v]):
                    if b.mn[v] < w < b.mx[v]: cv.add(w)
            cu, cv = sorted(cu), sorted(cv)
            for i in range(len(cu) - 1):
                if cu[i + 1] - cu[i] < M_EPS: continue
                mu = (cu[i] + cu[i + 1]) / 2
                for j in range(len(cv) - 1):
                    if cv[j + 1] - cv[j] < M_EPS: continue
                    mv = (cv[j] + cv[j + 1]) / 2
                    if not any(o.mn[u] < mu < o.mx[u] and o.mn[v] < mv < o.mx[v] for o in rs): return False
    return True


def _filled(b, cand):
    """is all of b inside the boxes cand (exact: b cut at their faces, every cell's centre inside one)"""
    rs = [o for o in cand if all(o.mn[k] < b.mx[k] and o.mx[k] > b.mn[k] for k in (0, 1, 2))]
    if not rs: return False
    cuts = []
    for k in (0, 1, 2):
        c = {b.mn[k], b.mx[k]}
        for o in rs:
            for w in (o.mn[k], o.mx[k]):
                if b.mn[k] < w < b.mx[k]: c.add(w)
        c = sorted(c)
        cuts.append([(c[i] + c[i + 1]) / 2 for i in range(len(c) - 1) if c[i + 1] - c[i] >= M_EPS])
    for x in cuts[0]:
        rx = [o for o in rs if o.mn[0] < x < o.mx[0]]
        for y in cuts[1]:
            ry = [o for o in rx if o.mn[1] < y < o.mx[1]]
            for z in cuts[2]:
                if not any(o.mn[2] < z < o.mx[2] for o in ry): return False
    return True


def hidden_pass(lst, parts, skipped, index):
    nb = [[] for _ in lst]   # (each plain box's neighbours within 1/4 m that cover: the pair index's)
    cov = [covers(b) for b in lst]
    for (i, j) in index.pairs(lst):
        if cov[j]: nb[i].append(j)
        if cov[i]: nb[j].append(i)
    pc = [b for b in parts if covers(b)]
    near = _grid(pc) if pc else None
    removed = set()
    order = sorted((i for i, b in enumerate(lst) if not b.shape), key=lambda i: (-vol(lst[i]), i))
    for i in order:
        b = lst[i]
        cand = [lst[j] for j in nb[i] if j not in removed]
        if near: cand += [pc[j] for j in sorted(near([v - 0.02 for v in b.mn], [v + 0.02 for v in b.mx]))]
        if not _buried(b, cand): continue
        if not _filled(b, cand):   # (buried, but it would leave a sealed void: the solid space would change)
            skipped["hiddenVoid"] = skipped.get("hiddenVoid", 0) + 1; continue
        if not cosmetic(b.mat):   # (buried, but a material that acts: a teleporter's link, a gate's count, ... stay)
            skipped["hiddenGameplay"] = skipped.get("hiddenGameplay", 0) + 1; continue
        removed.add(i)
    return [b for i, b in enumerate(lst) if i not in removed], len(removed)


# ---- 4. gaps (fixmap.js gapPlan, plain boxes as the game's FIX GAPS) ----
def gap_pass(brushes, skipped, index, keep=()):
    lst = brushes[:]
    on = lambda v: abs(v / GRID - round(v / GRID)) < 1e-6
    off_by = lambda v: abs(v - round(v / GRID) * GRID)
    at0 = {b: n for n, b in enumerate(lst)}   # (the pieces as the pass found them: the pair index's grid has those)

    def hits(x, i, j):   # would x overlap a third piece, a staircase or a ladder (pieces only grow here, by less than 1/8 m a face)
        for o in keep:
            if all(min(x.mx[q], o.mx[q]) - max(x.mn[q], o.mn[q]) > 1e-6 for q in (0, 1, 2)): return True
        for o in index.every.near(x, 4 * GAP_MAX):
            n = at0.get(o)
            if n is None or n == i or n == j: continue
            o = lst[n]
            if all(min(x.mx[q], o.mx[q]) - max(x.mn[q], o.mn[q]) > 1e-6 for q in (0, 1, 2)): return True
        return False

    def within(a, b, k):   # (a's face across axis k inside b's: moved, it only fills the slot between them)
        return all(q == k or (a.mn[q] >= b.mn[q] - 1e-6 and a.mx[q] <= b.mx[q] + 1e-6) for q in (0, 1, 2))

    def overlap(a, b, k):
        return all(q == k or min(a.mx[q], b.mx[q]) - max(a.mn[q], b.mn[q]) > 1e-6 for q in (0, 1, 2))
    closed = 0
    for (i, j) in index.pairs(lst):
        for k in (0, 1, 2):
            for (p, q) in ((i, j), (j, i)):
                A, Bb = lst[p], lst[q]
                gap = Bb.mn[k] - A.mx[k]
                if not (1e-6 < gap < GAP_MAX - 1e-9) or not overlap(A, Bb, k): continue
                ta, tb = A.mx[k], Bb.mn[k]
                oa, ob = on(ta), on(tb)
                if oa and ob: continue
                if A.shape or Bb.shape: skipped["gapShaped"] = skipped.get("gapShaped", 0) + 1; continue
                if not oa and ob: ta = tb
                elif oa and not ob: tb = ta
                else:
                    L = math.ceil(ta / GRID - 1e-9) * GRID
                    if L <= tb + 1e-9: ta = tb = L
                    elif off_by(ta) > off_by(tb): ta = tb
                    else: tb = ta
                na, nb = A, Bb
                if tidy(ta) != A.mx[k]:
                    na = A.copy(A.mn, A.mx); na.mx[k] = tidy(ta)
                if tidy(tb) != Bb.mn[k]:
                    nb = Bb.copy(Bb.mn, Bb.mx); nb.mn[k] = tidy(tb)
                why = None
                if (na is not A and not within(A, Bb, k)) or (nb is not Bb and not within(Bb, A, k)): why = "gapExposed"
                elif (na is not A and not grows_ok(A.mat)) or (nb is not Bb and not grows_ok(Bb.mat)): why = "gapGameplay"
                elif hard(na) != hard(A) or hard(nb) != hard(Bb): why = "gapWallbang"
                elif (na is not A and hits(na, p, q)) or (nb is not Bb and hits(nb, p, q)): why = "gapBlocked"
                if why: skipped[why] = skipped.get(why, 0) + 1; continue
                lst[p], lst[q] = na, nb
                closed += 1
    return lst, closed


# ---- 5. spawns (fixers.js spawnPlan, validate.js hullHits) ----
_FRAMES = [(0, 1, 2, False), (0, 1, 2, True), (1, 0, 2, False), (1, 0, 2, True), (0, 2, 1, False), (0, 2, 1, True)]


def _frac(v, a, c): return (v - a) / (c - a) if c > a else 0


def _inside(b, p):   # shapes.js insideShape
    kind, d = b.shape
    if kind == "cut":
        f = [_frac(p[k], b.mn[k], b.mx[k]) for k in (0, 1, 2)]
        return all(0 < v < 1 for v in f) and all(c[0] * f[0] + c[1] * f[1] + c[2] * f[2] < c[3] for c in d)
    up = d >> 2 if 4 <= d < 24 else 0
    ua, ha, va, mir = _FRAMES[up]
    gu, gv = _frac(p[ua], b.mn[ua], b.mx[ua]), _frac(p[va], b.mn[va], b.mx[va])
    if gu < 0 or gu > 1 or gv < 0 or gv > 1: return False
    h = b.mx[ha] - p[ha] if mir else p[ha] - b.mn[ha]
    dd = d & 3
    if kind == "corner": g = min(gu if dd in (1, 2) else 1 - gu, gv if dd >= 2 else 1 - gv)
    elif kind == "tetra": g = (gu if dd in (1, 2) else 1 - gu) + (gv if dd >= 2 else 1 - gv) - 1
    else: g = (gu, 1 - gu, gv, 1 - gv)[dd]
    return 0 <= h < (b.mx[ha] - b.mn[ha]) * g


def hull_hits(pos, b, E=0.02, N=5):
    x, y, z = pos
    lo, hi = (x - 0.4 + E, y + E, z - 0.4 + E), (x + 0.4 - E, y + 1.8 - E, z + 0.4 - E)
    if not all(hi[k] > b.mn[k] and lo[k] < b.mx[k] for k in (0, 1, 2)): return False
    if not b.shape: return True
    a = [max(lo[k], b.mn[k]) for k in (0, 1, 2)]; c = [min(hi[k], b.mx[k]) for k in (0, 1, 2)]
    at = lambda k, i: a[k] + (c[k] - a[k]) * i / (N - 1)
    return any(_inside(b, (at(0, i), at(1, j), at(2, l))) for i in range(N) for j in range(N) for l in range(N))


def spawn_pass(lines, lst, parts):
    solids = [b for b in lst + parts if solid(b.mat)]
    moves, stuck = [], []
    for n, l in enumerate(lines):
        t = l.split()
        if len(t) < 5 or t[0] != "spawn": continue
        x, y0, z = float(t[2]), float(t[3]), float(t[4])
        col = [b for b in solids if x + 0.4 > b.mn[0] and x - 0.4 < b.mx[0] and z + 0.4 > b.mn[2] and z - 0.4 < b.mx[2]]
        hit = lambda y: any(hull_hits((x, y, z), b) for b in col)
        if not hit(y0): continue
        y, k = y0, 0
        while k < SPAWN_MAX_STEPS and y <= 4096 - 1.8 and hit(y): y = round((y + SPAWN_STEP) * 1e6) / 1e6; k += 1
        if hit(y): stuck.append(l); continue
        t[3] = fmt(y); lines[n] = " ".join(t); moves.append((l, y - y0))
    return moves, stuck


def volumes(lines):
    """the staircases' and ladders' boxes (the game makes the steps; a ladder is climbed in its box): no gap fix
    grows a piece into one"""
    out = []
    for l in lines:
        t = l.split()
        try:
            if t and t[0] == "stairs" and len(t) >= 7:
                x0, z0, x1, z1, y0, y1 = map(float, t[1:7])
                out.append(B(0, [min(x0, x1), min(y0, y1), min(z0, z1)], [max(x0, x1), max(y0, y1), max(z0, z1)], None, "", None, None, -1))
            elif t and t[0] == "ladder" and len(t) >= 7:
                v = [float(x) for x in t[1:7]]
                out.append(B(0, v[:3], v[3:], None, "", None, None, -1))
        except ValueError:
            continue
    return out


# ---- the pass ----
def line_of(b):
    if b.line is not None: return b.line
    return " ".join([b.kw] + [fmt(v) for v in b.mn + b.mx] + (b.tail or []))


def run(lines, name=""):
    """tidies the map's lines in place; -> its report (counts, the pairs left by reason)"""
    brushes, parts = parse(lines)
    keep = volumes(lines)
    before = len(brushes)
    skipped = {}
    rep = {"map": name, "before": before, "zfight": [0, 0], "trimmed": 0, "merged": [0, 0], "hidden": 0, "gaps": 0, "rounds": 0}
    lst = brushes
    pi = PairIndex(GAP_MAX)   # (one for every step: pieces within 1/4 m of each other)
    for r in range(8):
        sk = {}
        lst, f, rm, tr = zfight_pass(lst, sk, pi)
        rep["trimmed"] += tr
        lst, frm, into = merge_pass(lst, sk)
        lst, hid = hidden_pass(lst, parts, sk, pi)
        lst, closed = gap_pass(lst, sk, pi, keep)
        rep["zfight"][0] += f; rep["zfight"][1] += rm; rep["merged"][0] += frm; rep["merged"][1] += into
        rep["hidden"] += hid; rep["gaps"] += closed; rep["rounds"] = r + 1
        skipped = sk   # (the last round's: what is left)
        if not (f or rm or frm or hid or closed): break
    rep["skipped"] = dict(sorted(skipped.items()))
    rep["after"] = len(lst)
    moves, stuck = spawn_pass(lines, lst, parts)
    if stuck:
        raise SystemExit("make_maps.py: %s: spawn still in something solid after %g m up:\n  %s" % (name, SPAWN_MAX_STEPS * SPAWN_STEP, "\n  ".join(stuck)))
    rep["spawns"] = len(moves)
    # the lines back: each piece where its line was (carved slabs and merged boxes where their first piece was)
    at = {}
    for b in lst: at.setdefault(b.origin, []).append(b)
    own = {b.origin for b in brushes}
    out = []
    for n, l in enumerate(lines):
        if n in own:
            out.extend(line_of(b) for b in at.get(n, ()))
        else: out.append(l)
    lines[:] = out
    return rep
