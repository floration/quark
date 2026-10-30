"""SOLAR SYSTEM: surf, a tour from the Sun out to Pluto in eleven stages (1.19.6 plan, section 2). Each stage is a
closed box (dark walls with thin stripes of its planet's material, a clip lid under the black sky) with ridges over a
deadly floor; a teleporter covers its end wall and puts you, at your speed, on the next stage's arrival ledge (its
checkpoint) facing on down the course. A ridge is two steep faces back to back with a glow cap on the line between them
(cut boxes; the cap is part of the faces' slope, nothing flat to stand on).

Transfers: the next ridge runs beside the one you ride for its last stretch (the overlap), a gap G out from its foot and
with its own foot under the plane of your face across that gap, so you let go, slide down your face, fly the gap and
land on the next ridge's near face. A surf bot running the game's own movement (pmove, run-only, strafing into the face
while sinking, steering in the air) rode every stage from a standing start and from portal entries at 25, 40 and 55 m/s
(see the commit). Difficulty climbs stage by stage: the Sun's faces are 28 m slopes at 50 degrees with 4 m gaps and
65 m overlaps; Neptune's are 12 m at 60 degrees with 6 m gaps and 30 m overlaps. Each stage starts with a drop-in (a
steeper first 36 m of the first ridge, under the ledge), so a standing start gets going and a fast entry lands on it.

The twists: Mercury's kicker over the crater, Venus's three haze curtains (small smokewall sheets: sight only), the
Moon's low gravity field, Mars's 90 degree bend (a ridge in six mitred pieces), the asteroid belt's zig-zag of short
ridges, Jupiter's long downhill into a kicker that throws you up into its portal (only its top part teleports),
Saturn's rings (two bands of four pieces round the planet: transfer outward from one to the other), Uranus's ridges
running steeply down and back up, Neptune's narrow 60 degree ridges and Pluto's finish kicker onto the last ledge. The
planets hang outside the end walls (solid, out of reach), each its own look; Saturn's sits in its rings as scenery."""
import math, os
from .prefabs import *
from .m4kit import Kit, MAT, num

Y = 200.0            # every arrival ledge's top
LF = 166.0           # the arrival ledges' front edge (they run back to the +z wall at 176: 10 m deep)
LEDGE_W = 16.0
SPACING = 380.0      # stages side by side in x
PLAN = []            # for the surf bot (SOLAR_PLAN env: tools' ride check)


def build():
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

    def emit_ridge(r, face, accent, cap, cap_h=0.5, band=1.5):
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
        def piece(mat, extra):
            k.cut(mat, bb, [fp, fm, cap0, cap1] + extra)
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
        def __init__(st, idx, name, mats, kill, light):
            st.idx, st.name, st.mats, st.kill, st.light = idx, name, mats, kill, light
            st.ox = (idx - 5) * SPACING
            st.ridges, st.seq, st.extra_pts, st.after = [], [], [], []
            st.end = None
            st.portal_lo = None
            st.lowgrav = None
            st.finish = None
            st.trim = None
            st.extra_lo = []

        def first(st, L, W, theta, slope=-0.06, lat=0.0, drop_in=36.0, drop_slope=-0.28):
            """the drop-in: a steep first stretch under the ledge (speed from a standing start), then the ridge on"""
            r0 = Ridge((st.ox + lat, LF + 3.0), (0.0, -1.0), drop_in, Y - 2.5, drop_slope, W, theta, 0)
            st.ridges.append(r0); st.seq.append(r0)
            r = Ridge(r0.E, (0.0, -1.0), L, r0.top1, slope, W, theta, 0)
            st.ridges.append(r); st.seq.append(r); return r

        def beside(st, W, theta, side, G, d, O, u=None):
            """where the next ridge starts: O m before the last one's end (negative: a gap after it), G m out from its
            foot on `side`, its own near foot d m under the last one's foot there"""
            p = st.ridges[-1]
            u = u or p.u
            ex, ez = p.E
            px, pz = -u[1], u[0]
            off = (p.W + W + G) * side
            S = (ex - u[0] * O + px * off, ez - u[1] * O + pz * off)
            foot = p.top1 - p.slope * min(O, p.L) - p.rise if O > 0 else p.top1 - p.rise
            return S, u, foot - d + W * math.tan(math.radians(theta))

        def kick(st, gap, top, bottom, angle=50.0, span=None, mat=None):
            """a kicker across the way after the last ridge (its way axis-aligned): a face rising toward the way that
            throws a fast rider up off its lip"""
            p = st.ridges[-1]; u = p.u; ex, ez = p.E
            run = (top - bottom) / math.tan(math.radians(angle))
            span = span or (p.W + 14)
            px, pz = -u[1], u[0]
            sx, sz = ex + u[0] * gap, ez + u[1] * gap
            if abs(u[1]) > 0.99:
                za, zb = sorted((sz, sz + u[1] * run))
                k.wedge(mat or st.mats["kick"], 3 if u[1] < 0 else 2, sx - span, bottom, za, sx + span, top, zb)
            else:
                xa, xb = sorted((sx, sx + u[0] * run))
                k.wedge(mat or st.mats["kick"], 1 if u[0] < 0 else 0, xa, bottom, sz - span, xb, top, sz + span)
            lipx, lipz = sx + u[0] * run, sz + u[1] * run
            st.extra_pts += [(sx + px * span, sz + pz * span), (sx - px * span, sz - pz * span), (lipx, lipz)]
            st.seq.append(f"kicker {sx:.3f} {sz:.3f} {u[0]:.5f} {u[1]:.5f} {run:.3f} {top:.3f} 0 {span:.3f} {900 + len(st.seq)}")
            st.lip = (lipx, lipz, top, u)

        def after_kick(st, L, W, theta, gap, drop, lat=0.0, slope=-0.06):
            lipx, lipz, top, u = st.lip
            px, pz = -u[1], u[0]
            S = (lipx + u[0] * gap + px * lat, lipz + u[1] * gap + pz * lat)
            r = Ridge(S, u, L, top - drop, slope, W, theta, len(st.ridges) + 500)
            st.ridges.append(r); st.seq.append(r); return r

        def then(st, L, W, theta, side, G, O, d=None, slope=-0.06, u=None):
            if d is None: d = G * math.tan(math.radians(st.ridges[-1].theta)) + 3.0   # (under the last face's plane across the gap)
            S, u, top = st.beside(W, theta, side, G, d, O, u)
            r = Ridge(S, u, L, top, slope, W, theta, len(st.ridges) + 1)
            st.ridges.append(r); st.seq.append(r); return r

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
                # radial planes: the start cap's outward normal points back along the arc, the end's on along it
                n0 = (math.sin(t0) * sg, -math.cos(t0) * sg); n1 = (-math.sin(t1) * sg, math.cos(t1) * sg)
                r = Ridge(A, u, L, h, slope, W, theta, chain, caps=(n0, n1))
                r.a0, r.a1 = math.degrees(t0), math.degrees(t1)
                h = r.top1
                st.ridges.append(r); st.seq.append(r); out.append(r)
            return out

    def stage_box(st):
        pts = [(st.ox - LEDGE_W / 2, LF), (st.ox + LEDGE_W / 2, 175)] + [(p[0], p[2]) for r in st.ridges for p in r.pts()] + st.extra_pts
        x0 = min(p[0] for p in pts) - 14; x1 = max(p[0] for p in pts) + 14
        z0 = min(p[1] for p in pts) - 14; z1 = 176.0
        lo = min([p[1] for r in st.ridges for p in r.pts()] + st.extra_lo)
        return x0, z0, x1, z1, lo

    def emit_stage(st, link_in, link_out):
        mats = st.mats
        x0, z0, x1, z1, lo = stage_box(st)
        if st.end:
            # the end side: the wall the course runs into ('-z' or '-x' or '+x'); pull the box in to 14 m past the last ridge
            pass
        killY = lo - 14
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
        # the floor: the deadly liquid (the void line is under it)
        # (no solid floor under it: nothing to stand on down there, and the map checks have no acres of it to walk)
        k.box(st.kill, x0, killY - 2, z0, x1, killY, z1)
        # the arrival ledge: back against the +z wall
        lx0, lx1 = st.ox - LEDGE_W / 2, st.ox + LEDGE_W / 2
        k.box(mats["ledge"], lx0, Y - 1.5, LF, lx1, Y, z1)
        k.box(mats["accent"], lx0 + 1, Y - 3, LF + 1, lx1 - 1, Y - 1.5, z1)
        for gx in (lx0, lx1 - 0.15): k.box("glow", gx, Y - 0.35, LF, gx + 0.15, Y - 0.05, z1)   # (flush on its sides)
        k.box("glow", lx0, Y - 0.35, LF, lx1, Y - 0.05, LF + 0.15)
        if link_in is not None:
            k.line(f"teleporter {num(st.ox - 4)} {num(Y + 1)} {num(z1 - 0.5)} {num(st.ox + 4)} {num(Y + 6)} {num(z1)} tp {link_in} 5")
            cps.append((lx0 + 0.5, Y, LF + 0.5, lx1 - 0.5, Y + 9, z1 - 0.6))
        # the portal's frame (flush with the wall behind: nothing to catch a foot on)
        k.box(mats["ledge"], st.ox - 5, Y, z1 - 0.5, st.ox - 4, Y + 7, z1); k.box(mats["ledge"], st.ox + 4, Y, z1 - 0.5, st.ox + 5, Y + 7, z1)
        k.box(mats["ledge"], st.ox - 4, Y + 6, z1 - 0.5, st.ox + 4, Y + 7, z1); k.box(mats["ledge"], st.ox - 4, Y, z1 - 0.5, st.ox + 4, Y + 1, z1)
        # the ridges
        for r in st.ridges:
            emit_ridge(r, mats["face"], mats["accent"], mats.get("cap", "glow"))
        # the end: a teleporter over the whole end wall (or its top part, over a kicker)
        if link_out is not None:
            last = st.ridges[-1]
            d = st.end or ("-z" if abs(last.u[1]) > abs(last.u[0]) and last.u[1] < 0 else "-x" if last.u[0] < 0 else "+x")
            ylo = st.portal_lo if st.portal_lo is not None else killY
            if d == "-z": k.line(f"teleporter {num(x0)} {num(ylo)} {num(z0)} {num(x1)} {num(lid)} {num(z0 + 1)} tp {link_out} 4")
            elif d == "-x": k.line(f"teleporter {num(x0)} {num(ylo)} {num(z0)} {num(x0 + 1)} {num(lid)} {num(z1)} tp {link_out} 0")
            else: k.line(f"teleporter {num(x1 - 1)} {num(ylo)} {num(z0)} {num(x1)} {num(lid)} {num(z1)} tp {link_out} 1")
        for f in st.after: f(st)
        # the plan for the ride check
        PLAN.append(f"stage {killY:g} {st.ox:g} {Y + 2.6:g} {z1 - 0.97:g} 0")
        for r in st.seq:
            PLAN.append(r if isinstance(r, str) else f"ridge {r.S[0]:.3f} {r.S[1]:.3f} {r.u[0]:.5f} {r.u[1]:.5f} {r.L:.3f} {r.top0:.3f} {r.slope:.4f} {r.W:.3f} {r.chain} {r.tan:.4f}")
        if st.finish: PLAN.append("finish " + " ".join(f"{v:g}" for v in st.finish))

    def mats(face, accent, wall, wall2, ledge=None, floor=None, cap="glow", bands=(20, 1.5), kick=None):
        return dict(face=face, accent=accent, wall=wall, wall2=wall2, ledge=ledge or accent, floor=floor or "box", cap=cap, bands=bands, kick=kick or face)

    def zig(st, n, L, W, theta, G, O, first_side=+1, slope=-0.06):
        for i in range(n): st.then(L, W, theta, first_side * (1 if i % 2 == 0 else -1), G, O, slope=slope)

    def top_at(ridges, a):
        """the apex height of a chain of arc pieces at angle a (between their joints)"""
        for r in ridges:
            if r.a0 >= a >= r.a1 or r.a0 <= a <= r.a1:
                f = (a - r.a0) / (r.a1 - r.a0); return r.top0 + r.slope * r.L * f
        return ridges[-1].top1

    # ================================================================== 1 the Sun: three wide ridges, the tutorial
    s = Stage(0, "sun", mats("moltenslag", "emberbed", "box", "furnaceglow", ledge="cooledmagma"), "magmariver", (1, 0.55, 0.25))
    s.first(115, 18, 50)
    zig(s, 2, 125, 18, 50, 4, 65)
    stages.append(s)

    # ================================================================== 2 Mercury: four ridges, a kicker over a crater
    s = Stage(1, "mercury", mats("cooledmagma", "scoria", "box", "pumice", kick="scoria"), "liquidvoid", (0.85, 0.8, 0.75))
    s.first(95, 16.5, 50)
    r = s.then(110, 16.5, 50, +1, 5, 58)
    s.kick(6, r.top1 - r.rise + 6, r.top1 - r.rise - 3, angle=47)       # (over the crater: it opens under the lip)
    s.after_kick(90, 16.5, 50, 3, 1, slope=-0.15)
    s.then(105, 16.5, 50, -1, 5, 55)
    stages.append(s)

    # ================================================================== 3 Venus: four ridges in a yellow haze
    s = Stage(2, "venus", mats("sulfurcrust", "amber", "box", "fumarolecrust"), "plasmapool", (1, 0.85, 0.4))
    s.first(90, 15, 52)
    zig(s, 3, 100, 15, 52, 5, 52)
    stages.append(s)

    # ================================================================== 4 Earth and the Moon
    s = Stage(3, "earth", mats("grass", "glacierice", "box", "cloud"), "deepocean", (0.6, 0.8, 1))
    s.first(70, 14, 52)
    zig(s, 4, 85, 14, 52, 5, 48)
    s.moon0 = len(s.ridges)
    zig(s, 3, 70, 9, 54, 6, 45, first_side=-1, slope=-0.04)
    def moon_field(st):   # the Moon: half gravity over its three ridges (from the gap before the first)
        P = [p for r in st.ridges[st.moon0:] for p in r.pts()]
        a = st.ridges[st.moon0 - 1].E
        # (as tight as the flights allow: the field draws as a see-through volume)
        k.box("lowgravityfield", min(p[0] for p in P) - 3, min(p[1] for p in P) - 8, min(p[2] for p in P) - 2, max(p[0] for p in P) + 3, max(p[1] for p in P) + 8, a[1] + 8)
    s.after.append(moon_field)
    stages.append(s)

    # ================================================================== 5 Mars: down a canyon, a 90 degree bend
    s = Stage(4, "mars", mats("redcanyonrock", "ochreearth", "box", "reddunes"), "magmariver", (1, 0.6, 0.45))
    s.first(75, 13, 53)
    s.then(85, 13, 53, -1, 6, 45)
    S3, u3, top3 = s.beside(13, 53, +1, 6, 6 * math.tan(math.radians(53)) + 3, 45)
    s.arc((S3[0] - 70, S3[1]), 70, 0, -90, 6, 13, 53, top3, -0.06, chain=77)
    s.then(85, 13, 53, -1, 6, 0, u=(-1.0, 0.0))
    s.then(80, 13, 53, +1, 6, 44)
    stages.append(s)

    # ================================================================== 6 the asteroid belt: short ridges, zig-zag
    s = Stage(5, "asteroids", mats("boulderfield", "bandediron", "box", "bandediron", ledge="boulderfield"), "liquidvoid", (0.75, 0.7, 0.65))
    s.first(75, 10, 54)
    zig(s, 5, 62, 10, 54, 6, 36)
    stages.append(s)

    # ================================================================== 7 Jupiter: the biggest ridges, a long downhill, a kicker up into the portal
    s = Stage(6, "jupiter", mats("sandstonestrata", "crossbeddedsandstone", "box", "sandstonestrata", ledge="crossbeddedsandstone", bands=(7, 3, 5, 2)), "moltengold", (1, 0.78, 0.55))
    s.first(150, 19, 50, slope=-0.15)
    r = s.then(150, 19, 50, +1, 4, 72, slope=-0.15)
    s.kick(8, r.top1 - r.rise + 10, r.top1 - r.rise - 3, angle=47)
    s.portal_lo = r.top1 - r.rise + 11
    stages.append(s)

    # ================================================================== 8 Saturn: the rings, ridges bent round the planet
    s = Stage(7, "saturn", mats("glacierice", "icecave", "box", "icecave", ledge="icecave"), "liquidvoid", (0.95, 0.88, 0.7))
    s.first(70, 12, 54)
    S1, u1, top1 = s.beside(12, 54, +1, 5, 5 * math.tan(math.radians(54)) + 3, 45)
    R1 = 80.0
    C = (S1[0] - R1, S1[1])
    ring1 = s.arc(C, R1, 0, -64, 4, 12, 54, top1, -0.06, chain=81)
    R2 = R1 + 12 + 12 + 6
    a2 = -30.0
    s.arc(C, R2, a2, -124, 4, 12, 54, top_at(ring1, a2) - 6 * math.tan(math.radians(54)) - 3, -0.06, chain=82)
    s.planet_c = C
    s.end = "-x"
    stages.append(s)

    # ================================================================== 9 Uranus: steep ridges that run down and back up
    s = Stage(8, "uranus", mats("frost", "icewall", "box", "glacierice", ledge="glacierice"), "liquidvoid", (0.6, 0.95, 0.95))
    s.first(70, 7.5, 58, slope=-0.3)
    s.then(62, 7.5, 58, +1, 6, 32, slope=0.1)
    s.then(66, 7.5, 58, -1, 6, 32, slope=-0.3)
    s.then(62, 7.5, 58, +1, 6, 32, slope=0.1)
    s.then(62, 7.5, 58, -1, 6, 32, slope=-0.25)
    stages.append(s)

    # ================================================================== 10 Neptune: narrow 60 degree ridges, the hardest transfers
    s = Stage(9, "neptune", mats("windpackedsnow", "bluetintedglass", "box", "frost", ledge="frost"), "deepocean", (0.35, 0.5, 1))
    s.first(70, 6, 60)
    zig(s, 5, 58, 6, 60, 6, 30)
    stages.append(s)

    # ================================================================== 11 Pluto: three short ridges, the finish kicker
    s = Stage(10, "pluto", mats("frost", "tundra", "box", "snow", ledge="tundra", kick="snow"), "liquidvoid", (0.85, 0.85, 0.95))
    s.first(70, 9, 55)
    s.then(62, 9, 55, +1, 6, 34)
    r = s.then(58, 9, 55, -1, 6, 34)
    s.kick(6, r.top1 - r.rise + 7, r.top1 - r.rise - 3, angle=47)
    lipx, lipz, top, u = s.lip
    fx0, fx1, fz0, fz1 = lipx - 10, lipx + 10, lipz - 16, lipz
    s.extra_pts += [(fx0, fz0), (fx1, fz1)]
    s.after.append(lambda st, b=(fx0, top, fz0, fx1, fz1): (k.box("tundra", b[0], b[1] - 2, b[2], b[3], b[1], b[4]),
                                                           k.box("glow", b[0], b[1] - 0.35, b[2], b[3], b[1] - 0.05, b[2] + 0.15)))
    s.finish = (fx0 + 1, top - 0.1, fz0 + 1, fx1 - 1, top + 8, fz1 - 0.5)
    cps_finish = (fx0 + 1, top, fz0 + 1, fx1 - 1, top + 8, fz1 - 0.5)
    stages.append(s)

    for i, st in enumerate(stages):
        emit_stage(st, i if i > 0 else None, i + 1 if i + 1 < len(stages) else None)

    # ------------------------------------------------------------------ dressing (off the lines: outside the walls, on
    # them flush, or non-solid scenery)
    PLANETS = {   # bands of materials (bottom to top), radius, height over the wall tops
        "sun": (["furnaceglow", "glow", "furnaceglow", "glow", "furnaceglow", "glow", "furnaceglow"], 95, 20),
        "mercury": (["pumice", "scoria", "pumice", "cooledmagma", "pumice", "scoria", "pumice"], 30, 18),
        "venus": (["sulfurcrust", "amber", "sulfurcrust", "fumarolecrust", "amber", "sulfurcrust", "amber"], 45, 16),
        "earth": (["glacierice", "deepocean", "grass", "deepocean", "cloud", "deepocean", "glacierice"], 48, 14),
        "mars": (["redcanyonrock", "reddunes", "ochreearth", "reddunes", "redcanyonrock", "reddunes", "glacierice"], 34, 16),
        "jupiter": (["sandstonestrata", "ochreearth", "crossbeddedsandstone", "sandstonestrata", "ochreearth", "crossbeddedsandstone", "sandstonestrata"], 110, 30),
        "uranus": (["glacierice", "frost", "glacierice", "frost", "glacierice", "frost", "glacierice"], 55, 20),
        "neptune": (["deepocean", "windpackedsnow", "deepocean", "deepocean", "windpackedsnow", "deepocean", "deepocean"], 52, 20),
        "pluto": (["tundra", "snow", "tundra", "snow", "frost", "snow", "tundra"], 20, 14),
    }
    for st in stages:
        x0, z0, x1, z1 = st.box
        wall_top = Y + 18
        last = st.ridges[-1]
        d = st.end or "-z"
        if st.name in PLANETS:
            mats_, rad, up = PLANETS[st.name]
            cy = wall_top + up
            if d == "-z": cx, cz = (x0 + x1) / 2, z0 - 30 - rad
            else: cx, cz = x0 - 30 - rad, (z0 + z1) / 2
            globe(mats_, cx, cy, cz, rad)
            if st.name == "jupiter":   # the Great Red Spot: a glow patch on the face toward the course
                k.box("glow", cx - 22, cy - 22, cz + rad * 0.86, cx + 2, cy - 10, cz + rad * 0.9)
            if st.name == "earth":     # the Moon beside it
                globe(["pumice", "volcanicash"], cx + rad + 40, cy + 30, cz + 20, 12, layers=5)
            if st.name == "uranus":    # its rings stand on end
                for i in range(12):
                    a0 = 2 * math.pi * i / 12; a1 = 2 * math.pi * (i + 1) / 12
                    R0, R1 = rad + 12, rad + 16
                    pts = [(cx + R0 * math.cos(a0), cy + R0 * math.sin(a0)), (cx + R1 * math.cos(a0), cy + R1 * math.sin(a0)),
                           (cx + R1 * math.cos(a1), cy + R1 * math.sin(a1)), (cx + R0 * math.cos(a1), cy + R0 * math.sin(a1))]
                    k.cut("far", (min(p[0] for p in pts), min(p[1] for p in pts), cz - 1, max(p[0] for p in pts), max(p[1] for p in pts), cz + 1),
                          [plane_xy(pts, j) for j in range(4)])
        # a light over the ledge and along the course
        k.light(st.ox, Y + 8, 170, 30, 0.7)
        for r in st.ridges[1::2]:
            mx, mz = r.S[0] + r.u[0] * r.L / 2, r.S[1] + r.u[1] * r.L / 2
            k.light(mx, r.top0 + 12, mz, 60, 0.6)

    # Saturn: the planet in the rings' middle and its own ring (scenery: you fly through nothing)
    st = stages[7]
    C = st.planet_c
    base = min(p[1] for r in st.ridges for p in r.pts())
    globe(["far"], C[0], base + 10, C[1], 36, layers=7)
    k.ring("far", C[0], C[1], 46, 62, base + 9.6, base + 10.4, n=16)

    # Venus: three curtains of haze across the way (sight only: you and your shots pass), each the width of two ridges
    st = stages[2]
    for r in st.ridges[2:5]:
        z = r.S[1] - 6
        k.box("smokewall", r.S[0] - 2 * r.W - 6, r.top0 - r.rise - 8, z - 0.25, r.S[0] + 2 * r.W + 6, r.top0 + 4, z + 0.25)

    # the asteroid belt: rocks drifting beside and under the lanes (non-solid scenery)
    st = stages[5]
    import random
    R = random.Random(56)
    x0, z0, x1, z1 = st.box
    for i in range(16):
        side = (-1, 1)[i % 2]
        x = (x0 + 4 + R.random() * 6) if side < 0 else (x1 - 4 - R.random() * 6)
        z = z0 + 20 + R.random() * (z1 - z0 - 60)
        y = st.killY + 10 + R.random() * (Y - st.killY - 10)
        rr = 1.5 + R.random() * 3
        k.prism("far", [(x + rr * math.cos(a), z + rr * math.sin(a)) for a in [2 * math.pi * j / 6 + R.random() * 0.3 for j in range(6)]], y - rr, y + rr * 0.8)

    # the start grid: eight surfers on the Sun's ledge, facing down the course
    o = stages[0].ox
    for z in (166.5, 170.5):
        for x in (-5.4, -1.8, 1.8, 5.4): m.spawn("race", o + x, Y, z, 0)

    cps.append(cps_finish)
    for c in cps: m.raw("checkpoint " + " ".join(num(v) for v in c))
    if os.environ.get("SOLAR_PLAN"):
        with open(os.environ["SOLAR_PLAN"], "w") as f: f.write("\n".join(PLAN) + "\n")
    m.save("solar", dress=False)
