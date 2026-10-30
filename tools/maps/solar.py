"""SOLAR SYSTEM: surf, a tour from the Sun out to Pluto in fifteen stages (1.19.6 plan, section 2; reworked after q
played it: "really easy", "finished the whole thing in like 1:30", "they should have turns and obstacles ... and big
jumps"). Each stage is a closed box (dark walls with thin stripes of its body's material, clip over them, the black sky
over all) with ridges over a liquid that kills on touch; a teleporter covers its end wall and drops you onto the next
stage's ledge (its checkpoint) standing still: every stage starts from a standstill, as surf stages do (speed carried
through the portals snowballed to 180 m/s, and a fast rider flew whole stages straight into their end walls).

A ridge is two steep faces back to back with a cap on the line between them (cut boxes; nothing flat to stand on);
a wall ride is one face with a wall on the line behind it. Transfers: the next ridge runs beside the one you ride, a
gap G out from its foot and lower; you let go, fly the gap and land on its near face. Each stage brings something
new and asks more than the one before (half-widths 20 -> 5.5 m, faces 48 -> 62 degrees, gaps 4 -> 20 m):

   1 Sun        big 20 m faces at 48 degrees, a speed bowl (down hard, up again)
   2 Mercury    a flat kicker over the crater onto a steep landing ridge
   3 Venus      an S-bend (two 55 degree bends), two small haze curtains over the line
   4 Earth      two wall rides, then a kicker launch across 32 m of half gravity onto the Moon
   5 Mars       a 90 degree canyon bend that falls through the turn, a gap in the ridge to hop
   6 asteroids  five ridges turned 8-18 degrees off each other at alternating heights, big rocks past their ends
   7 Jupiter    a long steep downhill into a launch over the Great Red Spot, 38 m onto a catch ridge
   8 Io         two half helixes, one each way (the course folds back on itself)
   9 Europa     wall rides with gaps to hop between them
  10 Saturn     round the inner ring, out across a 14 m gap to the outer ring, off its end the other way
  11 Titan      a climbing wall ride that spends your speed, a hard drop, a launch, 34 m to the catch
  12 Uranus     ridges down and up, then a fork round Miranda (either side), back onto the line
  13 Neptune    6 m ridges at 60 degrees, 16-18 m transfers, a hairpin, a 14 m deeper last drop
  14 Kuiper     short 61 degree ridges turning and hopping, 16-18 m transfers
  15 Pluto      5.5 m ridges at 62 degrees, a gap, a kicker that only a fast rider clears to the finish deck

A surf bot running the game's own movement (pmove, run-only) rides every stage from the ledge at a standstill (the
commit has the times); its course total is about 200 s. The planets hang outside the end walls (solid, out of reach),
each with its own colours; Saturn's sits in its rings; Miranda splits Uranus's line."""
import math, os
from .prefabs import *
from .m4kit import Kit, MAT, num

Y = 200.0            # every arrival ledge's top
LF = 166.0           # the arrival ledges' front edge (they run back to the +z wall at 176: 10 m deep)
LEDGE_W = 16.0
SPACING = 380.0      # stages side by side in x
PLAN = []            # for the surf bot (SOLAR_PLAN env: tools' ride check)


def build():
    """two passes: the first lays every stage out at a wide spacing to measure it, the second packs them side by side"""
    ext = course(None)
    ox, cur = [], 0.0
    for (a, b) in ext:
        ox.append(cur - a); cur = ox[-1] + b + 8.0
    mid = cur / 2
    course([o - mid for o in ox])


def course(OX):
    m = M("Solar System", "surf", sun=(-0.3, 0.55, 0.78))
    m.top_up_spawns = lambda offered: None   # (eight race spawns of its own on the Sun's ledge)
    k = Kit(m)
    del PLAN[:]
    m.raw("palette base 0.14 0.14 0.17 accent 0.42 0.48 0.62 metal 0.55 0.56 0.6 glow 1 0.86 0.6 sky 0.008 0.008 0.02")
    m.raw("setting the solar system")
    cps = []

    # ------------------------------------------------------------------ geometry helpers
    def planes_ridge(S, u, L, top0, slope, W, tan):
        px, pz = -u[1], u[0]
        sx, sz = S
        uS = u[0] * sx + u[1] * sz
        pS = px * sx + pz * sz
        fp = (tan * px - slope * u[0], 1.0, tan * pz - slope * u[1], top0 + tan * pS - slope * uS)
        fm = (-tan * px - slope * u[0], 1.0, -tan * pz - slope * u[1], top0 - tan * pS - slope * uS)
        def level(h, below):   # y <= (apex line - h) (below) or y >= it
            if below: return (-slope * u[0], 1.0, -slope * u[1], top0 - h - slope * uS)
            return (slope * u[0], -1.0, slope * u[1], -(top0 - h) + slope * uS)
        return fp, fm, level

    class Ridge:
        def __init__(r, S, u, L, top0, slope, W, theta, chain, caps=None):
            r.S, r.u, r.L, r.top0, r.slope, r.W, r.theta, r.chain = S, u, L, top0, slope, W, theta, chain
            r.tan = math.tan(math.radians(theta)); r.rise = W * r.tan
            r.caps = caps
            r.wall = 0
        @property
        def E(r): return (r.S[0] + r.u[0] * r.L, r.S[1] + r.u[1] * r.L)
        @property
        def top1(r): return r.top0 + r.slope * r.L
        def pts(r):
            px, pz = -r.u[1], r.u[0]
            out = []
            ext = 0.0
            if r.caps: ext = r.W * max(abs(math.tan(math.acos(max(-1, min(1, -(c[0] * r.u[0] + c[1] * r.u[1]) if i == 0 else (c[0] * r.u[0] + c[1] * r.u[1])))))) for i, c in enumerate(r.caps)) + 0.3
            for (a, top) in ((-ext, r.top0 - r.slope * ext), (r.L + ext, r.top0 + r.slope * (r.L + ext))):
                cx, cz = r.S[0] + r.u[0] * a, r.S[1] + r.u[1] * a
                out.append((cx, top, cz))
                for sgn in (1, -1): out.append((cx + px * r.W * sgn, top - r.rise, cz + pz * r.W * sgn))
            return out

    def emit_ridge(r, face, accent, cap, cap_h=0.5, band=1.5, wall_mat=None):
        fp, fm, level = planes_ridge(r.S, r.u, r.L, r.top0, r.slope, r.W, r.tan)
        sx, sz = r.S; ex, ez = r.E
        if r.caps:
            c0, c1 = r.caps
        else:
            c0, c1 = (-r.u[0], -r.u[1]), r.u
        cap0 = (c0[0], 0.0, c0[1], c0[0] * sx + c0[1] * sz)
        cap1 = (c1[0], 0.0, c1[1], c1[0] * ex + c1[1] * ez)
        P = r.pts()
        xs = [p[0] for p in P]; ys = [p[1] for p in P]; zs = [p[2] for p in P]
        bb = (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))
        sides = [fp, fm]
        px, pz = -r.u[1], r.u[0]; pS = px * sx + pz * sz
        if r.wall:   # a wall ride: one face (side r.wall), a wall standing on the ridge line behind it
            sg = r.wall
            sides = [fp if sg > 0 else fm, (-sg * px, 0.0, -sg * pz, -sg * pS)]
            H, T = 9.0, 1.5
            wb = (bb[0] - 2, bb[1] - 3, bb[2] - 2, bb[3] + 2, bb[4] + H, bb[5] + 2)
            k.cut(wall_mat or face, wb, [(sg * px, 0.0, sg * pz, sg * pS), (-sg * px, 0.0, -sg * pz, T - sg * pS),
                                        level(-H, True), level(r.rise + 3, False), cap0, cap1])
        def piece(mat, extra):
            k.cut(mat, bb, sides + [cap0, cap1] + extra)
        piece(cap, [level(cap_h, False)])                                # the glow cap on the ridge line
        piece(face, [level(cap_h, True), level(r.rise - band, False)])  # the faces
        piece(accent, [level(r.rise - band, True)])                    # the foot band (the faces' last 1.5 m)

    def globe(mats, cx, cy, cz, rad, n=8, layers=7, tilt_ring=None):
        """a planet: `layers` bands of an n-sided faceted ball (a band's segments are 3-plane cut boxes); mats: a
        material per band (cycled)"""
        for j in range(layers):
            a0 = -math.pi / 2 + math.pi * j / layers; a1 = -math.pi / 2 + math.pi * (j + 1) / layers
            y0, y1 = cy + rad * math.sin(a0), cy + rad * math.sin(a1)
            r0, r1 = rad * math.cos(a0), rad * math.cos(a1)
            rm = max(r0, r1)
            mat = mats[j % len(mats)]
            for i in range(n):
                t0 = 2 * math.pi * i / n; t1 = 2 * math.pi * (i + 1) / n; tm = (t0 + t1) / 2
                # the outer face: through the band's two rings at this segment's middle direction
                dx, dz = math.cos(tm), math.sin(tm)
                c = math.cos(math.pi / n)
                ra, rb = r0 * c, r1 * c
                # plane: radial distance (along d) <= ra + (rb - ra) * (y - y0) / (y1 - y0)
                gy = (rb - ra) / (y1 - y0)
                pl = (dx, -gy, dz, dx * cx + dz * cz + ra - gy * y0)
                s0 = (-math.sin(t0), 0, math.cos(t0)); s1 = (math.sin(t1), 0, -math.cos(t1))
                side0 = (-s0[0], 0, -s0[2], -(s0[0] * cx + s0[2] * cz)); side1 = (-s1[0], 0, -s1[2], -(s1[0] * cx + s1[2] * cz))
                xsx = [cx, cx + rm * math.cos(t0), cx + rm * math.cos(t1)]
                zsz = [cz, cz + rm * math.sin(t0), cz + rm * math.sin(t1)]
                k.cut(mat, (min(xsx), y0, min(zsz), max(xsx), y1, max(zsz)), [pl, side0, side1])

    def plane_xy(pts, j):
        (ax, ay), (bx, by) = pts[j], pts[(j + 1) % len(pts)]
        cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
        nx, ny = by - ay, -(bx - ax)
        if nx * (cx - ax) + ny * (cy - ay) > 0: nx, ny = -nx, -ny
        L = math.hypot(nx, ny); nx, ny = nx / L, ny / L
        return (nx, ny, 0.0, nx * ax + ny * ay)

    # ------------------------------------------------------------------ a stage
    stages = []

    class Stage:
        def __init__(st, name, mats, kill):
            idx = len(stages)
            st.idx, st.name, st.mats, st.kill = idx, name, mats, kill
            st.ox = OX[idx] if OX else idx * 3000.0
            st.ridges, st.seq, st.extra_pts, st.after = [], [], [], []
            st.end = None
            st.finish = None
            st.extra_lo = []
            st.rocks = []

        def first(st, L, W, theta, slope=-0.06, lat=0.0, drop_in=36.0, drop_slope=-0.28):
            """the drop-in: a steep first stretch under the ledge (speed from a standing start), then the ridge on"""
            r0 = Ridge((st.ox + lat, LF + 3.0), (0.0, -1.0), drop_in, Y - 2.5, drop_slope, W, theta, 0)
            st.ridges.append(r0); st.seq.append(r0)
            r = Ridge(r0.E, (0.0, -1.0), L, r0.top1, slope, W, theta, 0)
            st.ridges.append(r); st.seq.append(r); return r

        def beside(st, W, theta, side, G, d, O, u=None, p=None):
            """where the next ridge starts: O m before the last one's end (negative: a gap after it), G m out from its
            foot on `side`, its own near foot d m under the last one's foot there"""
            p = p or st.ridges[-1]
            u = u or p.u
            ex, ez = p.E
            px, pz = -u[1], u[0]
            off = (p.W + W + G) * side
            S = (ex - u[0] * O + px * off, ez - u[1] * O + pz * off)
            foot = p.top1 - p.slope * min(O, p.L) - p.rise if O > 0 else p.top1 - p.rise
            return S, u, foot - d + W * math.tan(math.radians(theta))

        def then(st, L, W, theta, side, G, O, d=None, slope=-0.06, u=None, wall=False, extra_drop=0.0):
            """a transfer: the next ridge beside the last (side +1 / -1), G m of air between their feet"""
            if d is None: d = G * math.tan(math.radians(st.ridges[-1].theta)) + 3.0   # (under the last face's plane across the gap)
            S, u, top = st.beside(W, theta, side, G, d + extra_drop, O, u)
            r = Ridge(S, u, L, top, slope, W, theta, len(st.ridges) + 1)
            if wall: r.wall = -side          # (the face toward the last ridge; the wall behind it)
            st.ridges.append(r); st.seq.append(r); return r

        def cont(st, L, slope, wall=0):
            """the same ridge on, at another fall (a vertical curve's piece): edge to edge, the same chain"""
            p = st.ridges[-1]
            r = Ridge(p.E, p.u, L, p.top1, slope, p.W, p.theta, p.chain)
            r.wall = wall
            st.ridges.append(r); st.seq.append(r); return r

        def vcurve(st, L, s0, s1, n):
            for i in range(n):
                st.cont(L / n, s0 + (s1 - s0) * (i + 0.5) / n)

        def hop(st, gap, L, drop, slope=-0.06):
            """a gap in the ridge: it goes on `gap` m further, `drop` m lower (you fly the gap)"""
            p = st.ridges[-1]
            S = (p.E[0] + p.u[0] * gap, p.E[1] + p.u[1] * gap)
            r = Ridge(S, p.u, L, p.top1 + p.slope * gap - drop, slope, p.W, p.theta, len(st.ridges) + 300)
            st.ridges.append(r); st.seq.append(r); return r

        def bend(st, R, deg, nseg, slope, side):
            """the ridge bends on round a centre R m off to `side` (+1: toward +p), deg degrees, in mitred pieces"""
            p = st.ridges[-1]
            px, pz = -p.u[1], p.u[0]
            ex, ez = p.E
            C = (ex + side * R * px, ez + side * R * pz)
            a0 = math.degrees(math.atan2(ez - C[1], ex - C[0]))
            return st.arc(C, R, a0, a0 + side * deg, nseg, p.W, p.theta, p.top1, slope, p.chain)

        def arc(st, C, R, a0, a1, nseg, W, theta, top0, slope, chain):
            """a ridge bent round C (radius R of its line) from angle a0 to a1 (degrees, from +x toward +z) in nseg
            straight pieces mitred on the radial planes between them: the faces meet edge to edge"""
            out = []
            h = top0
            for i in range(nseg):
                t0 = math.radians(a0 + (a1 - a0) * i / nseg); t1 = math.radians(a0 + (a1 - a0) * (i + 1) / nseg)
                A = (C[0] + R * math.cos(t0), C[1] + R * math.sin(t0)); B = (C[0] + R * math.cos(t1), C[1] + R * math.sin(t1))
                L = math.hypot(B[0] - A[0], B[1] - A[1]); u = ((B[0] - A[0]) / L, (B[1] - A[1]) / L)
                sg = 1 if a1 > a0 else -1
                n0 = (math.sin(t0) * sg, -math.cos(t0) * sg); n1 = (-math.sin(t1) * sg, math.cos(t1) * sg)
                r = Ridge(A, u, L, h, slope, W, theta, chain, caps=(n0, n1))
                r.a0, r.a1 = math.degrees(t0), math.degrees(t1)
                h = r.top1
                st.ridges.append(r); st.seq.append(r); out.append(r)
            return out

        def kick(st, gap, top, bottom, angle=50.0, span=None, mat=None):
            """a kicker across the way after the last ridge (its way axis-aligned): a face rising toward the way that
            throws a fast rider up off its lip"""
            p = st.ridges[-1]; u = p.u; ex, ez = p.E
            run = (top - bottom) / math.tan(math.radians(angle))
            span = span or (p.W + 14)
            px, pz = -u[1], u[0]
            sx, sz = ex + u[0] * gap, ez + u[1] * gap
            st.after.append(lambda st_, a=(sx, sz, u, run, span, top, bottom, mat): emit_kick(st_, *a))
            lipx, lipz = sx + u[0] * run, sz + u[1] * run
            st.extra_pts += [(sx + px * span, sz + pz * span), (sx - px * span, sz - pz * span), (lipx, lipz)]
            st.extra_lo.append(bottom)
            st.seq.append(f"kicker {sx:.3f} {sz:.3f} {u[0]:.5f} {u[1]:.5f} {run:.3f} {top:.3f} 0 {span:.3f} {900 + len(st.seq)}")
            st.lip = (lipx, lipz, top, u)

        def after_kick(st, L, W, theta, gap, drop, lat=0.0, slope=-0.06):
            lipx, lipz, top, u = st.lip
            px, pz = -u[1], u[0]
            S = (lipx + u[0] * gap + px * lat, lipz + u[1] * gap + pz * lat)
            r = Ridge(S, u, L, top - drop, slope, W, theta, len(st.ridges) + 500)
            st.ridges.append(r); st.seq.append(r); return r

        def split(st, L, W, theta, G, O, globe_mats, choose=+1, slope=-0.06):
            """a fork: two ridges either side of the last (mirror images), a planet body on the line between them,
            and one ridge on the line again after (both forks transfer onto it)"""
            a = st.ridges[-1]
            d = G * math.tan(math.radians(a.theta)) + 3.0
            fork = {}
            for sd in (+1, -1):
                S, u, top = st.beside(W, theta, sd, G, d, O, p=a)
                fork[sd] = Ridge(S, u, L, top, slope, W, theta, len(st.ridges) + 1 + (sd > 0))
            st.ridges += [fork[-1], fork[+1]]; st.seq.append(fork[choose])
            b = fork[+1]
            S, u, top = st.beside(a.W, a.theta, -1, G, G * math.tan(math.radians(theta)) + 3.0, O, p=b)
            c = Ridge(S, u, a.L, top, a.slope, a.W, a.theta, len(st.ridges) + 1)
            st.ridges.append(c); st.seq.append(c)
            # the body between the forks: past the last ridge's end, before the next one starts (out of every face)
            gx, gz = (a.E[0] + c.S[0]) / 2, (a.E[1] + c.S[1]) / 2
            gap = math.hypot(c.S[0] - a.E[0], c.S[1] - a.E[1])
            rad = min(gap / 2 - 3, a.W + G - 2)
            st.after.append(lambda st_, g=(globe_mats, gx, a.top1 - a.rise * 0.6, gz, rad): globe(g[0], g[1], g[2], g[3], g[4], n=8, layers=5))
            return c

    def emit_kick(st, sx, sz, u, run, span, top, bottom, mat):
        if abs(u[1]) > 0.99:
            za, zb = sorted((sz, sz + u[1] * run))
            k.wedge(mat or st.mats["kick"], 3 if u[1] < 0 else 2, sx - span, bottom, za, sx + span, top, zb)
        else:
            xa, xb = sorted((sx, sx + u[0] * run))
            k.wedge(mat or st.mats["kick"], 1 if u[0] < 0 else 0, xa, bottom, sz - span, xb, top, sz + span)

    def stage_box(st):
        pts = [(st.ox - LEDGE_W / 2, LF), (st.ox + LEDGE_W / 2, 175)] + [(p[0], p[2]) for r in st.ridges for p in r.pts()] + st.extra_pts
        x0 = min(p[0] for p in pts) - 14; x1 = max(p[0] for p in pts) + 14
        z0 = min(p[1] for p in pts) - 14; z1 = 176.0
        lo = min([p[1] for r in st.ridges for p in r.pts()] + st.extra_lo)
        return x0, z0, x1, z1, lo

    def emit_stage(st, link_in, link_out):
        mats = st.mats
        x0, z0, x1, z1, lo = stage_box(st)
        killY = lo - 14
        assert killY > -97, (st.name, killY)          # (the engine holds feet at y -100: the floor must be over it)
        bot = killY - 4
        wall_top = Y + 18; lid = Y + 40
        st.box = (x0, z0, x1, z1); st.killY = killY
        T = 2.0
        wm, wb = mats["wall"], mats["wall2"]
        # the walls: banded (bands flush: one thickness), clip above them to the lid
        def wall(ax0, az0, ax1, az1):
            y = bot; band = 0
            hs = mats.get("bands", (20, 1.5))
            while y < wall_top:
                h = hs[band % len(hs)]
                k.box(wm if band % 2 == 0 else wb, ax0, y, az0, ax1, min(wall_top, y + h), az1)
                y += h; band += 1
            k.box("clip", ax0, wall_top, az0, ax1, lid, az1)
        wall(x0 - T, z0 - T, x0, z1 + T); wall(x1, z0 - T, x1 + T, z1 + T)
        wall(x0, z0 - T, x1, z0); wall(x0, z1, x1, z1 + T)
        # (no lid: nothing in surf climbs within 40 m of the clip tops; the map checks confirm it)
        # the floor: a liquid that kills on touch (review: three stages' floors didn't), some with a band of their
        # planet's own liquid over it (looks only: you fall through it into the killing one)
        k.box(st.kill, x0, killY - 2, z0, x1, killY, z1)
        if mats.get("over"): k.box(mats["over"], x0, killY, z0, x1, killY + 0.6, z1)
        # the arrival ledge: back against the +z wall
        lx0, lx1 = st.ox - LEDGE_W / 2, st.ox + LEDGE_W / 2
        k.box(mats["ledge"], lx0, Y - 1.5, LF, lx1, Y, z1)
        k.box(mats["accent"], lx0 + 1, Y - 3, LF + 1, lx1 - 1, Y - 1.5, z1)
        for gx in (lx0, lx1 - 0.15): k.box(mats["cap"], gx, Y - 0.35, LF, gx + 0.15, Y - 0.05, z1)   # (flush on its sides)
        k.box(mats["cap"], lx0, Y - 0.35, LF, lx1, Y - 0.05, LF + 0.15)
        # the arrival portal: a slab over the ledge, under a canopy; you come out of its underside straight down at your
        # speed and land on the ledge standing (every stage starts from a standstill, as surf stages do: carried speed
        # let a fast rider fly a whole stage into its end wall). Out of reach from the ledge (7 m up).
        PY = Y + 7.0
        if link_in is not None:
            k.line(f"teleporter {num(st.ox - 3)} {num(PY)} {num(LF + 2)} {num(st.ox + 3)} {num(PY + 0.5)} {num(z1 - 2)} tp {link_in} 3")
            cps.append((lx0 + 0.5, Y, LF + 0.5, lx1 - 0.5, Y + 6, z1 - 0.6))
        k.box(mats["ledge"], lx0, PY + 0.5, LF, lx1, PY + 1.2, z1)
        for gx in (lx0, lx1 - 0.6):
            k.box(mats["ledge"], gx, Y, z1 - 0.6, gx + 0.6, PY + 0.5, z1)
        # the ridges
        for r in st.ridges:
            emit_ridge(r, mats["face"], mats["accent"], mats["cap"], wall_mat=mats["wall2"])
        # the end: a teleporter over the whole end wall
        if link_out is not None:
            last = st.seq[-1] if not isinstance(st.seq[-1], str) else st.ridges[-1]
            d = st.end or ("-z" if abs(last.u[1]) > abs(last.u[0]) and last.u[1] < 0 else "-x" if last.u[0] < 0 else "+x")
            if d == "-z": k.line(f"teleporter {num(x0)} {num(killY)} {num(z0)} {num(x1)} {num(lid)} {num(z0 + 1)} tp {link_out} 4")
            elif d == "-x": k.line(f"teleporter {num(x0)} {num(killY)} {num(z0)} {num(x0 + 1)} {num(lid)} {num(z1)} tp {link_out} 0")
            else: k.line(f"teleporter {num(x1 - 1)} {num(killY)} {num(z0)} {num(x1)} {num(lid)} {num(z1)} tp {link_out} 1")
        for f in st.after: f(st)
        # the plan for the ride check
        PLAN.append(f"stage {killY:g} {st.ox:g} {Y + 0.05:g} {z1 - 5.05:g} 0")   # (where you land, and where R puts you)
        for r in st.seq:
            PLAN.append(r if isinstance(r, str) else f"ridge {r.S[0]:.3f} {r.S[1]:.3f} {r.u[0]:.5f} {r.u[1]:.5f} {r.L:.3f} {r.top0:.3f} {r.slope:.4f} {r.W:.3f} {r.chain} {r.tan:.4f}")
        if st.finish: PLAN.append("finish " + " ".join(f"{v:g}" for v in st.finish))

    def mats(face, accent, wall2, cap, ledge=None, kick=None, bands=(20, 1.5), over=None):
        return dict(face=face, accent=accent, wall="box", wall2=wall2, ledge=ledge or accent, cap=cap, bands=bands,
                    kick=kick or face, over=over)

    def zig(st, n, L, W, theta, G, O, first_side=+1, slope=-0.06):
        for i in range(n): st.then(L, W, theta, first_side * (1 if i % 2 == 0 else -1), G, O, slope=slope)

    def top_at(ridges, a):
        """the apex height of a chain of arc pieces at angle a (between their joints)"""
        for r in ridges:
            if r.a0 >= a >= r.a1 or r.a0 <= a <= r.a1:
                f = (a - r.a0) / (r.a1 - r.a0); return r.top0 + r.slope * r.L * f
        return ridges[-1].top1

    SPLIT = int(os.environ.get("SOLAR_SPLIT", "1"))

    # ================================================================== 1 the Sun: big faces, a speed bowl
    s = Stage("sun", mats("moltenslag", "emberbed", "furnaceglow", "furnaceglow", ledge="cooledmagma"), "magmariver")
    s.first(110, 20, 48)
    s.then(70, 20, 48, +1, 4, 60)
    s.vcurve(90, -0.35, 0.2, 4)                  # the bowl: down hard, then up (the climb spends what the fall gave)
    s.cont(40, -0.06)
    s.then(130, 20, 48, -1, 5, 60)
    stages.append(s)

    # ================================================================== 2 Mercury: a kicker over the crater
    s = Stage("mercury", mats("cooledmagma", "scoria", "pumice", "fumarolecrust", kick="scoria"), "liquidvoid")
    s.first(95, 17, 50)
    r = s.then(110, 17, 50, +1, 6, 55, slope=-0.12)
    s.kick(5, r.top1 - r.rise + 5, r.top1 - r.rise - 3, angle=38)      # (flat: it keeps your speed; review)
    s.after_kick(110, 17, 50, 6, 2, slope=-0.2)
    s.then(110, 17, 50, -1, 8, 55)
    stages.append(s)

    # ================================================================== 3 Venus: an S through the haze
    s = Stage("venus", mats("sulfurcrust", "amber", "fumarolecrust", "sulfurpitcrust"), "moltengold")
    s.first(80, 15, 52)
    s.then(60, 15, 52, +1, 7, 40)
    s.bend(85, 55, 4, -0.09, +1)
    s.bend(85, 55, 4, -0.09, -1)
    s.cont(50, -0.06)
    s.then(100, 15, 52, -1, 9, 45, u=(0.0, -1.0))
    stages.append(s)

    # ================================================================== 4 Earth and the Moon: wall rides, then the Moon jump
    s = Stage("earth", mats("grass", "glacierice", "cloud", "glowingalgae", over="deepocean"), "liquidvoid")
    s.first(75, 13, 54)
    s.then(95, 13, 54, +1, 9, 45, wall=True)
    s.then(95, 13, 54, -1, 9, 45, wall=True)
    r = s.ridges[-1]
    s.kick(5, r.top1 - r.rise + 6, r.top1 - r.rise - 3, angle=40, mat="pumice")
    lip = s.lip
    s.moon = s.after_kick(80, 10, 55, 32, 5, slope=-0.08)
    s.then(70, 10, 55, +1, 12, 35)
    def moon_field(st, lip=lip):   # half gravity over the jump to the Moon only (the field draws as a see-through volume)
        lx, lz, top, u = lip
        k.box("lowgravityfield", lx - 14, top - 18, lz - 30, lx + 14, top + 8, lz)
    s.after.append(moon_field)
    stages.append(s)

    # ================================================================== 5 Mars: down the canyon round a 90 degree bend, a gap to hop
    s = Stage("mars", mats("redcanyonrock", "ochreearth", "reddunes", "rocksalt"), "magmariver")
    s.first(70, 12, 55)
    s.then(70, 12, 55, -1, 9, 40)
    s.bend(60, 90, 4, -0.11, +1)                  # (review: steeper, four pieces: it gains through the bend)
    s.cont(30, -0.08)
    s.hop(8, 70, 2.5)
    s.then(80, 12, 55, +1, 11, 38)
    stages.append(s)

    # ================================================================== 6 the asteroid belt: turning ridges among the rocks
    s = Stage("asteroids", mats("boulderfield", "bandediron", "bandediron", "glowingfissures", ledge="boulderfield"), "liquidvoid")
    s.first(65, 10, 56)
    hd = [-18, 14, -14, 16, -8]
    for i, h in enumerate(hd):
        a = math.radians(h)
        s.then(62, 10, 56, (1 if i % 2 == 0 else -1), 12, 30, u=(math.sin(a), -math.cos(a)), extra_drop=(6 if i % 2 else 0))
    stages.append(s)

    # ================================================================== 7 Jupiter: the long downhill and the launch over the Red Spot
    s = Stage("jupiter", mats("sandstonestrata", "crossbeddedsandstone", "sandstonestrata", "amber", ledge="crossbeddedsandstone", bands=(7, 3, 5, 2)), "moltengold")
    s.first(130, 14, 56, slope=-0.18)
    r = s.then(140, 14, 56, +1, 10, 60, slope=-0.2)
    s.kick(6, r.top1 - r.rise + 6, r.top1 - r.rise - 3, angle=33)
    s.spot = s.lip
    s.after_kick(130, 14, 56, 38, 8, slope=-0.12)
    stages.append(s)

    # ================================================================== 8 Io: two half helixes, one each way (an S folded back)
    s = Stage("io", mats("sulfurpitcrust", "ropylavarock", "sulfurcrust", "furnaceglow", ledge="ropylavarock"), "magmariver")
    s.first(60, 11, 56)
    s.then(50, 11, 56, +1, 10, 30)
    s.bend(42, 180, 10, -0.1, -1)                 # (a half helix down: you come back past where you started)
    s.cont(20, -0.06)
    s.bend(42, 180, 10, -0.1, +1)                 # (and round the other way)
    s.then(70, 11, 56, +1, 13, 30)
    stages.append(s)

    # ================================================================== 9 Europa: the cracked ice, wall rides and gaps
    s = Stage("europa", mats("snowflakefrost", "jasper", "lakeice", "seaglass", ledge="jasper"), "liquidvoid")
    s.first(65, 9, 57)
    s.then(70, 9, 57, +1, 12, 32, wall=True)
    s.hop(10, 50, 2.5)
    s.then(70, 9, 57, -1, 14, 30, wall=True)
    s.hop(12, 50, 3)
    s.then(70, 9, 57, +1, 15, 30)
    stages.append(s)

    # ================================================================== 10 Saturn: round the rings, out across the gap, back
    s = Stage("saturn", mats("glacierice", "icecave", "icecave", "quartzite", ledge="icecave"), "liquidvoid")
    s.first(70, 10, 58)
    S1, u1, top1 = s.beside(10, 58, +1, 6, 6 * math.tan(math.radians(58)) + 3, 40)
    R1 = 80.0
    C = (S1[0] - R1, S1[1])
    ring1 = s.arc(C, R1, 0, -100, 6, 10, 58, top1, -0.07, chain=81)
    G2 = 14.0
    R2 = R1 + 10 + 10 + G2
    a2 = -55.0
    s.arc(C, R2, a2, -128, 5, 10, 58, top_at(ring1, a2) - G2 * math.tan(math.radians(58)) - 3, -0.08, chain=82)
    s.then(80, 10, 58, -1, 8, 30, u=(-1.0, 0.0))
    s.planet_c = C; s.R1 = R1
    stages.append(s)

    # ================================================================== 11 Titan: up a wall ride in the haze, then the launch
    s = Stage("titan", mats("desertcrust", "ochreearth", "desertcrust", "amber", ledge="ochreearth"), "liquidvoid")
    s.first(70, 9, 58, slope=-0.25)
    s.then(100, 9, 58, +1, 10, 35, slope=0.06, wall=True)    # (a long climb: it spends what you bring)
    r = s.then(90, 9, 58, -1, 12, 30, slope=-0.3)              # (then off it and down hard)
    s.kick(5, r.top1 - r.rise + 6, r.top1 - r.rise - 3, angle=36)
    s.after_kick(80, 9, 58, 34, 6, slope=-0.1)
    s.then(70, 9, 58, +1, 15, 30)
    stages.append(s)

    # ================================================================== 12 Uranus: up and down, a fork round Miranda
    s = Stage("uranus", mats("glacierice", "frost", "glacierice", "seaglass", ledge="glacierice"), "liquidvoid")
    s.first(70, 8, 58, slope=-0.3)
    s.then(70, 8, 58, +1, 7, 32, slope=0.06)
    s.split(90, 8, 58, 14, 30, ["frost", "glacierice", "frost", "windpackedsnow", "frost"], choose=SPLIT, slope=-0.12)
    s.then(70, 8, 58, -1, 14, 32, slope=0.08)
    s.then(70, 8, 58, +1, 14, 30, slope=-0.3)
    stages.append(s)

    # ================================================================== 13 Neptune: long transfers, a hairpin, the drop
    s = Stage("neptune", mats("icecave", "bluedsteel", "lakeice", "glowwormcave", ledge="bluedsteel", over="deepocean"), "liquidvoid")
    s.first(75, 6, 60, drop_in=50, drop_slope=-0.32)
    s.then(65, 6, 60, +1, 10, 30)
    s.then(65, 6, 60, -1, 16, 30)
    s.bend(40, 90, 6, -0.1, -1)
    s.then(70, 6, 60, +1, 18, 30)
    s.then(70, 6, 60, -1, 18, 30, extra_drop=14)
    stages.append(s)

    # ================================================================== 14 the Kuiper belt: short icy ridges, turning, with gaps
    s = Stage("kuiper", mats("snowflakefrost", "blackbasaltblocks", "frost", "glowwormcave", ledge="blackbasaltblocks"), "liquidvoid")
    s.first(65, 6, 61, drop_in=50, drop_slope=-0.32)
    s.then(45, 6, 61, +1, 10, 24, u=(math.sin(math.radians(-10)), -math.cos(math.radians(-10))))
    s.hop(9, 40, 2.5)
    s.then(45, 6, 61, -1, 16, 22, u=(math.sin(math.radians(12)), -math.cos(math.radians(12))))
    s.then(45, 6, 61, +1, 17, 22, u=(0.0, -1.0), extra_drop=6)
    s.hop(10, 40, 3)
    s.then(55, 6, 61, -1, 18, 24)
    stages.append(s)

    # ================================================================== 15 Pluto: narrow and fast, a gap, the kicker to the finish
    s = Stage("pluto", mats("tundra", "tuff", "tundra", "quartzcrystals", ledge="tuff", kick="tuff"), "liquidvoid")
    s.first(75, 5.5, 62, drop_in=50, drop_slope=-0.34)
    s.then(62, 5.5, 62, +1, 12, 30, slope=-0.12)
    s.hop(10, 60, 3, slope=-0.12)
    r = s.then(70, 5.5, 62, -1, 18, 34, slope=-0.22)
    s.kick(6, r.top1 - r.rise + 6, r.top1 - r.rise - 3, angle=40)
    lipx, lipz, top, u = s.lip
    fx0, fx1, fz0, fz1 = lipx - 18, lipx + 18, lipz - 44, lipz - 9      # (the finish 9 m past the lip: a slow kick falls short)
    s.extra_pts += [(fx0, fz0 - 2), (fx1, fz1)]
    top -= 6.0                                                            # (and 6 m under the lip)
    # the finish: a deck, a catch wall at its far end (a fast kick hits it and drops onto the deck), and the finish
    # line's box 30 m tall over the deck (flying through it counts)
    s.after.append(lambda st, b=(fx0, top, fz0, fx1, fz1): (k.box("tuff", b[0], b[1] - 2, b[2], b[3], b[1], b[4]),
                                                           k.box("quartzcrystals", b[0], b[1] - 0.35, b[4] - 0.15, b[3], b[1] - 0.05, b[4]),
                                                           k.box("tundra", b[0], b[1] - 2, b[2] - 2, b[3], b[1] + 40, b[2])))
    s.finish = (fx0 + 1, top - 0.1, fz0 + 1, fx1 - 1, top + 30, fz1 - 0.5)
    cps_finish = (fx0 + 1, top, fz0 + 1, fx1 - 1, top + 30, fz1 - 0.5)
    stages.append(s)

    if OX is None:   # (the first pass: only the stages' extents)
        out = []
        for st in stages:
            x0, z0, x1, z1, lo = stage_box(st)
            out.append((x0 - 2 - st.ox, x1 + 2 - st.ox))
        return out

    for i, st in enumerate(stages):
        emit_stage(st, i if i > 0 else None, i + 1 if i + 1 < len(stages) else None)

    # ------------------------------------------------------------------ dressing (off the lines: outside the walls, on
    # them flush, or between the ramps; nothing on a face)
    PLANETS = {   # bands of materials (bottom to top), radius, height over the wall tops
        "sun": (["furnaceglow", "moltenslag", "furnaceglow", "moltenslag", "furnaceglow", "moltenslag", "furnaceglow"], 95, 20),
        "mercury": (["pumice", "scoria", "pumice", "cooledmagma", "pumice", "scoria", "pumice"], 30, 18),
        "venus": (["sulfurcrust", "amber", "sulfurcrust", "fumarolecrust", "amber", "sulfurcrust", "amber"], 45, 16),
        "earth": (["glacierice", "malachite", "chippedpaint", "malachite", "cloud", "chippedpaint", "glacierice"], 48, 14),
        "mars": (["redcanyonrock", "reddunes", "ochreearth", "reddunes", "redcanyonrock", "reddunes", "glacierice"], 34, 16),
        "jupiter": (["sandstonestrata", "ochreearth", "crossbeddedsandstone", "sandstonestrata", "ochreearth", "crossbeddedsandstone", "sandstonestrata"], 110, 30),
        "uranus": (["glacierice", "seafoam", "glacierice", "seafoam", "glacierice", "seafoam", "glacierice"], 55, 20),
        "neptune": (["bluepooltile", "chippedpaint", "bluepooltile", "bluepooltile", "chippedpaint", "bluepooltile", "bluepooltile"], 52, 20),
        "pluto": (["tundra", "tuff", "desertcrust", "tuff", "tundra", "tuff", "tundra"], 24, 14),
        "io": (["sulfurcrust", "sulfurpitcrust", "ropylavarock", "sulfurcrust", "sulfurpitcrust", "sulfurcrust", "ropylavarock"], 26, 16),
        "europa": (["snowflakefrost", "jasper", "snowflakefrost", "saltflat", "jasper", "snowflakefrost", "saltflat"], 24, 16),
        "titan": (["ochreearth", "desertcrust", "ochreearth", "amber", "desertcrust", "ochreearth", "desertcrust"], 40, 16),
        "kuiper": (["blackbasaltblocks", "snowflakefrost", "scoria", "blackbasaltblocks", "snowflakefrost", "scoria", "blackbasaltblocks"], 14, 30),
    }
    for st in stages:
        x0, z0, x1, z1 = st.box
        wall_top = Y + 18
        if st.name in PLANETS:
            mats_, rad, up = PLANETS[st.name]
            cy = wall_top + up
            cx, cz = (x0 + x1) / 2, z0 - 30 - rad
            globe(mats_, cx, cy, cz, rad)
            if st.name == "jupiter":   # the Great Red Spot: a glow patch on the face toward the course
                k.box("glow", cx - 22, cy - 22, cz + rad * 0.86, cx + 2, cy - 10, cz + rad * 0.9)
            if st.name == "neptune":   # the Great Dark Spot
                k.box("bluedsteel", cx + 6, cy - 18, cz + rad * 0.86, cx + 24, cy - 8, cz + rad * 0.9)
            if st.name == "pluto":     # the pale heart
                k.box("saltflat", cx - 10, cy - 12, cz + rad * 0.84, cx + 6, cy + 2, cz + rad * 0.88)
            if st.name == "earth":     # the Moon beside it
                globe(["pumice", "volcanicash"], cx + rad + 40, cy + 30, cz + 20, 12, layers=5)
            if st.name == "uranus":    # its rings stand on end (solid, lit, out of reach)
                for i in range(12):
                    a0 = 2 * math.pi * i / 12; a1 = 2 * math.pi * (i + 1) / 12
                    R0, Rr = rad + 12, rad + 16
                    pts = [(cx + R0 * math.cos(a0), cy + R0 * math.sin(a0)), (cx + Rr * math.cos(a0), cy + Rr * math.sin(a0)),
                           (cx + Rr * math.cos(a1), cy + Rr * math.sin(a1)), (cx + R0 * math.cos(a1), cy + R0 * math.sin(a1))]
                    k.cut("seafoam", (min(p[0] for p in pts), min(p[1] for p in pts), cz - 1, max(p[0] for p in pts), max(p[1] for p in pts), cz + 1),
                          [plane_xy(pts, j) for j in range(4)])
        # a light over the ledge and along the course
        k.light(st.ox, Y + 8, 170, 30, 0.7)
        for r in st.ridges[1::2]:
            mx, mz = r.S[0] + r.u[0] * r.L / 2, r.S[1] + r.u[1] * r.L / 2
            k.light(mx, r.top0 + 12, mz, 60, 0.6)

    # Saturn: a solid sandy planet in the rings' middle, its own flat ring round it (inside the inner ring: you ride
    # its outer face, so neither is in reach)
    st = next(t for t in stages if t.name == "saturn")
    C = st.planet_c
    top = max(p[1] for r in st.ridges for p in r.pts())
    near = min(abs(C[0] - st.ridges[0].S[0]) - st.ridges[0].W, st.R1 - 10) - 12   # (clear of the first ridge and the inner ring)
    prad = near - 10
    globe(["ripplesand", "sandstoneslab", "ripplesand", "crossbeddedsandstone", "ripplesand", "sandstoneslab", "ripplesand"], C[0], top - 12, C[1], prad, layers=7)
    k.ring("tuff", C[0], C[1], prad + 2, near, top - 12.4, top - 11.6, n=16)

    # Venus: two small curtains of haze over the line (sight only: you and your shots pass), each one ridge wide and
    # only the top 10 m of it
    st = next(t for t in stages if t.name == "venus")
    for r in (st.ridges[1], st.ridges[-1]):
        mx, mz = r.S[0] + r.u[0] * r.L * 0.55, r.S[1] + r.u[1] * r.L * 0.55
        apex = r.top0 + r.slope * r.L * 0.55
        k.box("smokewall", mx - r.W - 2, apex - 10, mz - 0.25, mx + r.W + 2, apex + 2, mz + 0.25)

    # the asteroid belt: big rocks just past each ridge's end on its line (transfer before them) and a few out in the
    # gaps; solid, never on a face
    st = next(t for t in stages if t.name == "asteroids")
    def clear(x, z, rr):
        for r in st.ridges:
            px, pz = -r.u[1], r.u[0]
            a = (x - r.S[0]) * r.u[0] + (z - r.S[1]) * r.u[1]; l = (x - r.S[0]) * px + (z - r.S[1]) * pz
            if -rr - 3 < a < r.L + rr + 3 and abs(l) < r.W + rr + 3: return False
        return True
    import random
    R = random.Random(61)
    for r in st.ridges[1:]:
        rr = 6 + R.random() * 3
        x, z = r.E[0] + r.u[0] * (rr + 9), r.E[1] + r.u[1] * (rr + 9)
        if not clear(x, z, rr): continue
        y = r.top1 - r.rise * 0.5
        k.prism(R.choice(["boulderfield", "bandediron", "scoria"]), [(x + rr * math.cos(a), z + rr * math.sin(a)) for a in [2 * math.pi * j / 6 + R.random() * 0.3 for j in range(6)]], y - rr, y + rr * 0.8)

    # Jupiter's Great Red Spot again, far under the launch (a glow disc on the gold)
    st = next(t for t in stages if t.name == "jupiter")
    lx, lz, top, u = st.spot
    k.disc("glow", lx, lz - 20, 14, st.killY, st.killY + 0.3, n=12)

    # the start grid: eight surfers on the Sun's ledge, facing down the course
    o = stages[0].ox
    for z in (166.5, 170.5):
        for x in (-5.4, -1.8, 1.8, 5.4): m.spawn("race", o + x, Y, z, 0)

    cps.append(cps_finish)
    for c in cps: m.raw("checkpoint " + " ".join(num(v) for v in c))
    if os.environ.get("SOLAR_PLAN"):
        with open(os.environ["SOLAR_PLAN"], "w") as f: f.write("\n".join(PLAN) + "\n")
    m.save("solar", dress=False)
