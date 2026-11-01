"""SOLAR SYSTEM: surf, a tour from the Sun out to Pluto in fifteen stages, built from q's own stage designs (2026-10-31:
q's piece list for each stage, in order, and the top-down layout he approved; nothing added).

Each stage is a list of q's pieces, laid out in plan exactly as his layout sketch lays them (`plan`: the same rules,
start at 0,0 heading -z), and given heights here: the course drops the whole way, every ramp lower than the last, and
every gap between two ramps is a flight (Quark: g 24 m/s^2) from the speed a run has there.

q's pieces:
   drop        the drop-in off the start ledge (or mid-stage: a steep drop ramp)
   straight, left, right, wave, narrow, helix, split
               surf ramps of their own (faces 56-62 degrees, 20 m wide): left / right are 90 degree banked curves,
               wave rolls up and down, narrow is half width, helix is a 270 degree spiral, split is two narrow ramps
               side by side (pick a line)
   jump        a kicker: the ramp flares up at its end and throws you into the next flight
   air         an extra-big gap
   leftwall / rightwall
               a kill wall standing alone in a flight (no ramp under it), death floor to the clip, over two thirds
               of the corridor: a left wall leaves the right third open, a right wall the left third
   speedcheck  a solid wall across the whole corridor in a flight, up to just under the flight of a run at the
               speed this stage is built for: slower and you hit it (q tunes its height in the editor)
   turnback    a ramp out, round the end of a dividing wall (death floor to the clip) and back down the other side

Every gap between two ramps is as long as q's layout draws it (about as long as the ramps: his Surfy). A flight lands
on the next ramp where a run at 85% of the speed it brings has fallen to: a faster run flies further and lands
further down that ramp, its line starting as steep as the flight falls there (at most 50 degrees), so you land along
the face. The stage's corridor: outer walls 24 m each side of the line (as drawn), dividing walls where two parts of a
stage come side by side, and where one passes under another (Jupiter's helix, Pluto's return) a roof of the stage's
kill liquid over the lower one, so no part is reachable from another. Each stage ends on a platform with a framed gate
(the walk teleporter onto the next stage's ledge: every stage starts from a standstill); Pluto's is the finish room.
SOLAR_TABLE=<file> writes each stage's numbers (lengths, heights, speeds, gaps, air times)."""
import math, os
from .prefabs import *
from .m4kit import Kit, MAT, num

G = 24.0             # gravity (m/s^2)
D = 24.0             # corridor half-width (q's layout: the outer walls 24 m each side of the line)
WR = 10.0            # a ramp's half-width (20 m wide, as drawn)
VS = 0.85            # flights are placed for a run at this fraction of the speed the ride brings
STEP = 4.0           # a straight ramp piece is at most this long
ARC = 3.0            # a curve's pieces: at most this long...
TURN = 5.0           # ...and at most this many degrees
LEDGE_W, LEDGE_D = 16.0, 10.0

# q's stages (2026-10-31): his pieces, in order; turnback sides as his layout has them
STAGES = [
    ("sun", ["drop", "straight", "air", "left", "straight", "speedcheck"], None),
    ("mercury", ["drop", "right", "left", "right", "air", "turnback", "leftwall", "rightwall"], None),
    ("venus", ["drop", "air", "jump", "air", "jump", "air", "left", "leftwall", "right", "rightwall"], None),
    ("earth", ["drop", "turnback", "speedcheck", "wave", "straight"], None),
    ("mars", ["drop", "jump", "air", "leftwall", "rightwall", "leftwall", "rightwall", "right", "speedcheck"], None),
    ("asteroids", ["split", "drop", "jump", "air", "speedcheck", "air", "leftwall", "rightwall", "wave"], None),
    ("jupiter", ["drop", "jump", "helix", "narrow", "jump"], None),
    ("io", ["straight", "straight", "drop", "jump", "air", "air", "straight", "turnback", "speedcheck"], None),
    ("europa", ["drop", "jump", "air", "turnback", "turnback", "straight", "speedcheck"], None),
    ("saturn", ["drop", "jump", "left", "air", "right", "air", "right", "straight"], None),
    ("titan", ["drop", "leftwall", "rightwall", "speedcheck", "wave", "straight", "left"], None),
    ("uranus", ["drop", "jump", "drop", "jump", "drop", "right", "air", "right", "air", "turnback"], None),
    ("neptune", ["drop", "straight", "right", "straight", "left", "straight", "speedcheck", "rightwall", "leftwall", "narrow"], None),
    ("kuiper", ["drop", "wave", "wave", "wave", "speedcheck"], None),
    ("pluto", ["drop", "straight", "turnback", "left", "straight", "jump", "drop", "jump", "air", "turnback", "straight", "drop",
               "jump", "air", "leftwall", "rightwall", "wave", "speedcheck", "drop", "straight", "left"], [1, 1]),
]
BUILT = int(os.environ.get("SOLAR_STAGES") or 15)   # (stages built so far: they're made one by one)

RAMPS = {"drop", "straight", "jump", "left", "right", "turnback", "wave", "helix", "narrow", "split"}
WALLS = {"leftwall", "rightwall", "topwall", "bottomwall"}


def plan(items, sides=None):
    """q's layout rules (his sketch, run for run): the pieces in plan from 0,0 heading -z (sketch heading -90; its
    heading h points along (cos h, sin h) in x, z). Returns [{t: ramp|air|mark|end, ...}] in order."""
    x, z, h = 0.0, 0.0, -90.0
    out = []
    side, tb, prev = 1, 0, None
    v = 12.0
    def fwd(d):
        nonlocal x, z
        x += d * math.cos(math.radians(h)); z += d * math.sin(math.radians(h))
    def straight(d, t, kind=None):
        out.append(dict(t=t, kind=kind, x=x, z=z, h=h, segs=[("s", d)], L=d)); fwd(d)
    def arc(deg, r, kind, rec=True):
        nonlocal h
        if rec: out.append(dict(t="ramp", kind=kind, x=x, z=z, h=h, segs=[("t", r, deg)]))
        n = max(6, int(abs(deg) / 6))
        for i in range(n):
            h += deg / n * 0.5; fwd(2 * r * math.sin(math.radians(abs(deg) / n / 2))); h += deg / n * 0.5
    for t in items:
        k = 1.0
        if t in RAMPS and prev in RAMPS: straight(2.2 * v, "air")
        prev = t if t not in ("air", "speedcheck") and t not in WALLS else "air"
        if t == "drop": straight(3.0 * v * k, "ramp", "drop"); v = min(60, v + 12)
        elif t == "straight": straight(3.0 * v * k, "ramp", "straight"); v = min(60, v + 5)
        elif t == "air": straight(3.5 * v * k, "air"); v = min(60, v + 4)
        elif t == "jump": straight(1.5 * v * k, "ramp", "jump")
        elif t == "left": arc(-90, 2.2 * v * k, "left"); v = min(60, v + 5)
        elif t == "right": arc(90, 2.2 * v * k, "right"); v = min(60, v + 5)
        elif t == "turnback":
            R, L = 16 * k, 3.0 * v * k
            side = sides[tb] if sides and tb < len(sides) else side; tb += 1
            out.append(dict(t="ramp", kind="turnback", x=x, z=z, h=h, segs=[("s", L), ("t", R, 180 * side), ("s", L)]))
            fwd(L); arc(180 * side, R, None, rec=False); fwd(L)
            side = -side
        elif t == "speedcheck":
            straight(0.6 * v * k, "air")
            out.append(dict(t="mark", kind="speedcheck", x=x, z=z, h=h, v=v))
            straight(0.6 * v * k, "air")
        elif t in WALLS:
            straight(0.6 * v * k, "air")
            out.append(dict(t="mark", kind=t, x=x, z=z, h=h))
            straight(0.6 * v * k, "air")
        elif t == "wave": straight(4.0 * v * k, "ramp", "wave"); v = min(60, v + 5)
        elif t == "helix": arc(270 * side, 22 * k, "helix")
        elif t == "narrow": straight(45 * k, "ramp", "narrow")
        elif t == "split": straight(50 * k, "ramp", "split")
        else: straight(40 * k, "ramp", t)
    out.append(dict(t="end", x=x, z=z, h=h))
    return out


# ---------------------------------------------------------------------- 3D vectors
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def _len(a): return math.sqrt(_dot(a, a))
def _unit(a):
    L = _len(a); return (a[0] / L, a[1] / L, a[2] / L)


class Ramp:
    """a surf ramp built along a path: start (x, z) at apex height y heading h (degrees: 0 is -z, 90 is +x), half-width
    W (horizontal, apex to foot), faces theta degrees steep; side: the face you ride (+1 the right one, -1 the left,
    seen along the path). The path: s(L) a straight, t(R, deg) an arc (deg > 0 turns right); the apex heights are a
    smooth curve through knots [s, y, slope] (set by `profile`)."""
    def __init__(r, x, z, y, h, W, theta, side):
        r.W, r.theta, r.side = W, theta, side
        r.tan = math.tan(math.radians(theta)); r.rise = W * r.tan
        r.samples = [(0.0, x, z, h)]
        r.knots = [[0.0, y, 0.0]]
        r.cx, r.cz, r.ch, r.cs = x, z, h, 0.0
        r._pts = None
        r.vin = 9.5
        r.notes = []

    def _step(r, ds, dh):
        hm = math.radians(r.ch + dh / 2)
        c = 2 * math.sin(math.radians(abs(dh)) / 2) / math.radians(abs(dh)) if dh else 1.0   # (the chord of the arc piece)
        r.cx += ds * c * math.sin(hm); r.cz -= ds * c * math.cos(hm); r.ch += dh; r.cs += ds
        r.samples.append((r.cs, r.cx, r.cz, r.ch))

    def s(r, L):
        n = max(1, math.ceil(L / STEP - 1e-9))
        for _ in range(n): r._step(L / n, 0.0)
        return r

    def t(r, R, deg):
        L = R * math.radians(abs(deg))
        n = max(1, math.ceil(max(L / ARC, abs(deg) / TURN) - 1e-9))
        for _ in range(n): r._step(L / n, deg / n)
        return r

    @property
    def length(r): return r.cs

    def y_at(r, s):
        K = r.knots
        if s <= 0: return K[0][1]
        if s >= K[-1][0]: return K[-1][1]
        j = 0
        while K[j + 1][0] < s: j += 1
        s0, y0, m0 = K[j]; s1, y1, m1 = K[j + 1]
        hh = s1 - s0; f = (s - s0) / hh
        return (2 * f ** 3 - 3 * f ** 2 + 1) * y0 + (f ** 3 - 2 * f ** 2 + f) * m0 * hh + (-2 * f ** 3 + 3 * f ** 2) * y1 + (f ** 3 - f ** 2) * m1 * hh

    def pts(r):
        if r._pts is None: r._pts = [(x, r.y_at(s), z) for (s, x, z, h) in r.samples]
        return r._pts

    def at(r, s):
        """(x, z, apex y, heading) at s along the path"""
        S = r.samples
        s = max(0.0, min(s, S[-1][0]))
        j = 0
        while j + 2 < len(S) and S[j + 1][0] < s: j += 1
        f = (s - S[j][0]) / max(1e-9, S[j + 1][0] - S[j][0])
        x = S[j][1] + (S[j + 1][1] - S[j][1]) * f; z = S[j][2] + (S[j + 1][2] - S[j][2]) * f
        return x, z, r.y_at(s), S[j][3] + (S[j + 1][3] - S[j][3]) * f

    @property
    def end(r): return r.at(r.length)

    def v_at(r, s):
        """a run's speed at s: riding the face, half gravity (pmove kSlopeGravityBack) turns each metre the line falls
        into speed (v^2 grows 2 x 12 x drop); climbing spends it"""
        return math.sqrt(max(4.0, r.vin ** 2 + G * (r.y_at(0) - r.y_at(s))))

    @property
    def vout(r): return r.v_at(r.length)

    def end_slope(r):
        P = r.pts(); A, B = P[-2], P[-1]
        return (B[1] - A[1]) / math.hypot(B[0] - A[0], B[2] - A[2])


def profile(kind, L, y0, m0):
    """the apex heights of q's ramp kinds: knots [s, y, slope]. From the landing slope m0 the line eases into its own
    fall (a drop: steep; a jump, a helix: steeper than a plain ramp), then ends on a lip (a jump: a flare that throws
    you up); a wave rolls 2.5 m up and down on its way"""
    mid, me, k = {"drop": (-1.5, 0.1, 16.0), "jump": (-0.3, 0.55, 18.0), "helix": (-0.45, 0.1, 12.0),
                  "turnback": (-0.12, 0.15, 14.0)}.get(kind, (-0.15, 0.15, 14.0))
    if kind in ("left", "right"): me = 0.1
    k = min(k, 0.35 * L)
    a = min(20.0, 0.25 * L)
    if a + k > 0.85 * L: a = 0.85 * L - k
    K = [[0.0, y0, m0]]
    y1 = y0 + (m0 + mid) / 2 * a
    K.append([a, y1, mid])
    y2 = y1 + mid * (L - k - a)
    if kind == "wave":
        span = L - k - a
        n = max(1, round(span / 32.0)); lam = span / n; A = 2.5
        for j in range(1, 4 * n):
            s = a + lam * j / 4
            ph = 2 * math.pi * j / 4
            K.append([s, y1 + mid * (s - a) + A * math.sin(ph), mid + A * 2 * math.pi / lam * math.cos(ph)])
    K.append([L - k, y2, mid])
    K.append([L, y2 + (mid + me) / 2 * k, me])
    return K


def _faces(A, B, tan):
    """the two face planes (n . p <= d inside) of the ridge piece A -> B"""
    ux, uz = B[0] - A[0], B[2] - A[2]
    Lh = math.hypot(ux, uz); ux, uz = ux / Lh, uz / Lh
    slope = (B[1] - A[1]) / Lh
    px, pz = -uz, ux
    uA = ux * A[0] + uz * A[2]; pA = px * A[0] + pz * A[2]
    fp = (tan * px - slope * ux, 1.0, tan * pz - slope * uz, A[1] + tan * pA - slope * uA)
    fm = (-tan * px - slope * ux, 1.0, -tan * pz - slope * uz, A[1] - tan * pA - slope * uA)
    return fp, fm, None, (ux, uz)


def _mitre(fa, fb, V, ua, ub):
    """the plane between two ridge pieces at their joint V: through the lines where each face meets the next piece's
    same face, so both faces run on across it exactly; (nx, ny, nz, d) pointing forward"""
    bx, bz = ua[0] + ub[0], ua[1] + ub[1]
    L = math.hypot(bx, bz); bx, bz = bx / L, bz / L
    m = None
    dp = _cross(fa[0][:3], fb[0][:3]); dm = _cross(fa[1][:3], fb[1][:3])
    if _len(dp) > 1e-7 and _len(dm) > 1e-7:
        c = _cross(_unit(dp), _unit(dm))
        if _len(c) > 0.08:
            c = _unit(c)
            if c[0] * bx + c[2] * bz < 0: c = (-c[0], -c[1], -c[2])
            if abs(c[1]) < 0.35 and c[0] * bx + c[2] * bz > 0.8: m = c
    if m is None: m = (bx, 0.0, bz)   # (a straight joint or a pure rise / fall: the vertical plane square to the line)
    return (m[0], m[1], m[2], _dot(m, V))


def _hdir(h):
    """the sketch heading h (degrees) as a unit (x, z) and its right-hand normal"""
    ux, uz = math.cos(math.radians(h)), math.sin(math.radians(h))
    return ux, uz, -uz, ux


class Stage:
    """one stage: q's pieces laid out from (ox, oz) with the ledge top at oy; everything in world coordinates"""
    def __init__(st, idx, name, items, sides, ox=0.0, oz=0.0, oy=0.0):
        st.idx, st.name, st.items = idx, name, items
        st.ox, st.oz, st.oy = ox, oz, oy
        st.theta = round((56 + 6 * idx / 14) * 2) / 2
        st.P = plan(items, sides)
        st.ramps = []       # [(Ramp, kind)]
        st.flights = []     # the flights between: {x, z, h, yr, vx, vy, G, marks}
        st.kills, st.checks = [], []
        st.samples = []     # the line, every ~2 m: (x, z, ux, uz, s, section, lo, hi)
        st.heights()
        st.add_samples_end()

    # -------------------------------------------------------------- heights: the ride, ramp by ramp, flight by flight
    def heights(st):
        P, ox, oz, oy = st.P, st.ox, st.oz, st.oy
        idx = [i for i, p in enumerate(P) if p["t"] == "ramp"]
        sides = []
        for i in idx:       # the face you ride: a curve's inner face; else the next curve's
            p = P[i]
            if p["kind"] in ("left", "right"): sd = -1 if p["kind"] == "left" else 1
            elif p["kind"] in ("helix", "turnback"): sd = 1 if p["segs"][-1 if p["kind"] == "helix" else 1][2] > 0 else -1
            else: sd = None
            sides.append(sd)
        nxt = 1
        for n in range(len(sides) - 1, -1, -1):
            if sides[n] is None: sides[n] = nxt
            else: nxt = sides[n]
        y, m0, vin = -2.5, -0.35, 6.0      # (the first ramp starts under the ledge's front: you walk off onto it)
        sv = 0.0                           # (the line's distance, for the samples)
        st.add_samples_ledge()
        tan = math.tan(math.radians(st.theta))
        for n, i in enumerate(idx):
            p = P[i]
            kind = p["kind"]
            W = WR / 2 if kind in ("narrow", "split") else WR
            ux, uz, nx, nz = _hdir(p["h"])
            back = 3.0 if n == 0 else 0.0
            lanes = [(-12.0, -1), (12.0, 1)] if kind == "split" else [(0.0, sides[n])]
            built = []
            for (lat, sd) in lanes:
                x0 = p["x"] + ox - ux * back + nx * lat; z0 = p["z"] + oz - uz * back + nz * lat
                r = Ramp(x0, z0, oy + y, p["h"] + 90.0, W, st.theta, sd)
                for j, sg in enumerate(p["segs"]):
                    if sg[0] == "s": r.s(sg[1] + (back if j == 0 else 0.0))
                    else: r.t(sg[1], sg[2])
                kk = kind
                if kind == "split": kk = "drop" if lat < 0 else "straight"   # (line choice: the left one falls hard, the right one flies)
                r.knots = profile(kk, r.length, oy + y, m0); r._pts = None
                r.vin = vin
                st.ramps.append((r, kind)); built.append(r)
            for rr in built: st.add_samples_ramp(rr, kind, sv, n)
            r = built[-1]
            sv += r.length - back
            # the flight after it: q's gaps and what stands in them, up to the next ramp (or the end platform)
            j = i + 1; G_ = 0.0; marks = []
            while j < len(P) and P[j]["t"] in ("air", "mark"):
                if P[j]["t"] == "air": G_ += P[j]["L"]
                else: marks.append((G_, P[j]))
                j += 1
            last = P[j]["t"] == "end"
            v = min(rr.vout for rr in built)
            a = math.atan(min(rr.end_slope() for rr in built))
            ey = min(rr.end[2] for rr in built)
            yr = ey - 0.4 * r.rise
            vs = VS * v
            vx, vy = vs * math.cos(a), vs * math.sin(a)
            if last and G_ < 1.0: G_ = max(12.0, vx * 0.6)   # (a stage that ends on a ramp: a short flight onto the platform)
            fl = dict(x=P[i + 1]["x"] + ox, z=P[i + 1]["z"] + oz, h=P[i + 1]["h"], yr=yr, vx=vx, vy=vy, G=G_, marks=marks, v=v)
            st.flights.append(fl)
            t_ = G_ / vx
            yl = yr + vy * t_ - 0.5 * G * t_ * t_
            vyl = vy - G * t_
            st.add_samples_air(fl, sv, n)
            sv += G_
            note = f"gap {G_:.0f} m: {t_:.2f} s of air at {vs:.0f} m/s, {yr - yl:.0f} m down"
            for (dm, mk) in marks:
                tm = dm / vx; ym = yr + vy * tm - 0.5 * G * tm * tm
                if mk["kind"] == "speedcheck":
                    top = ym - 1.2
                    st.checks.append(dict(d=dm, top=top, mk=mk, fl=fl, v=vs))
                    note += f"; speed check {dm:.0f} m out, top {top - oy:+.1f} m (a run under {vs:.0f} m/s hits it)"
                else:
                    st.kills.append(dict(d=dm, mk=mk, fl=fl))
                    note += f"; {mk['kind']} {dm:.0f} m out"
            r.notes.append(note)
            if last:
                st.plat = (fl["x"] + _hdir(fl["h"])[0] * G_, fl["z"] + _hdir(fl["h"])[1] * G_, fl["h"])
                st.plat_top = yl - 0.6
                st.plat_len, st.plat_hw = 36.0, 12.0
                break
            # the next ramp: its line starts at the flight's height there (1 m under the rider), as steep as it falls
            nr = P[j]; W2 = WR / 2 if nr["kind"] in ("narrow", "split") else WR
            y = yl - 1.0 + 0.4 * W2 * tan - oy
            m0 = max(vyl / vx, -1.2)
            # the landing: the velocity less what goes into the face (the face along that line)
            Nn = math.sqrt(1 + m0 * m0 + tan * tan)
            vn = (-m0 * vx + vyl) / Nn
            vin = math.sqrt(max(4.0, vx * vx + vyl * vyl - vn * vn))
        st.lowest = min([min(q[1] for q in r.pts()) - r.rise for (r, k) in st.ramps] + [st.plat_top - 2] + [c["top"] for c in st.checks])

    # -------------------------------------------------------------- the line, sampled (for the walls)
    def _add(st, x, z, ux, uz, s, sec, lo, hi):
        st.samples.append((x, z, ux, uz, s, sec, lo, hi))

    def add_samples_ledge(st):
        for j in range(6):
            d = LEDGE_D - 2.0 * j
            st._add(st.ox, st.oz + d, 0.0, -1.0, -d, "ledge", st.oy - 3, st.oy + 12)

    def add_samples_ramp(st, r, kind, s0, n):
        L = r.length; m = max(1, math.ceil(L / 2.0))
        for j in range(m + 1):
            s = L * j / m
            x, z, y, h = r.at(s)
            hr = math.radians(h)
            sec = f"r{n}" + ("b" if kind == "turnback" and s > L / 2 else "")
            st._add(x, z, math.sin(hr), -math.cos(hr), s0 + s, sec, y - r.rise - 2, y + 8)

    def add_samples_air(st, fl, s0, n):
        ux, uz, nx, nz = _hdir(fl["h"])
        m = max(1, math.ceil(fl["G"] / 2.0))
        for j in range(1, m):
            d = fl["G"] * j / m
            t = d / fl["vx"]; y = fl["yr"] + fl["vy"] * t - 0.5 * G * t * t
            f = 1.25; t2 = d / (fl["vx"] * f); y2 = fl["yr"] + fl["vy"] * f * t2 - 0.5 * G * t2 * t2
            st._add(fl["x"] + ux * d, fl["z"] + uz * d, ux, uz, s0 + d, f"a{n}", y - 4, max(y, y2) + 4)

    def add_samples_end(st):
        x0, z0, h = st.plat
        ux, uz, nx, nz = _hdir(h)
        s0 = st.samples[-1][4]
        m = math.ceil(st.plat_len / 2.0)
        for j in range(m + 1):
            d = st.plat_len * j / m
            st._add(x0 + ux * d, z0 + uz * d, ux, uz, s0 + d, "end", st.plat_top - 2, st.plat_top + 12)

    def extent(st):
        xs = [s[0] for s in st.samples]; zs = [s[1] for s in st.samples]
        return min(xs) - D - 4, min(zs) - D - 4, max(xs) + D + 4, max(zs) + D + 4


PLANET_R = {"sun": 95, "mercury": 30, "venus": 45, "earth": 48, "mars": 34, "jupiter": 110, "saturn": 52, "uranus": 55,
            "neptune": 52, "pluto": 24, "io": 26, "europa": 24, "titan": 40, "kuiper": 14, "asteroids": 20}


def build():
    """two passes: the first lays every stage out from 0,0 to measure it, the second packs them in rows (each at its
    own height: its floor just over the engine's)"""
    first = [Stage(i, name, items, sides) for i, (name, items, sides) in enumerate(STAGES[:BUILT])]
    rows, row, wx = [], [], 0.0
    for st in first:
        x0, z0, x1, z1 = st.extent()
        w = x1 - x0 + 2 * PLANET_R[st.name] + 120
        if row and wx + w > 3000.0: rows.append(row); row, wx = [], 0.0
        row.append((st, w)); wx += w
    if row: rows.append(row)
    place, zc = {}, 0.0
    for row in rows:
        depth = max(st.extent()[3] - st.extent()[1] for st, w in row) + 60
        xc = -sum(w for st, w in row) / 2
        for st, w in row:
            x0, z0, x1, z1 = st.extent()
            place[st.name] = (round(xc - x0 + 30), round(zc - z0), round(-88.0 - (st.lowest - 12.0)))
            xc += w
        zc += depth
    zmid = round(zc / 2)
    stages = []
    for i, (name, items, sides) in enumerate(STAGES[:BUILT]):
        ox, oz, oy = place[name]
        stages.append(Stage(i, name, items, sides, ox, oz - zmid, oy))
    course(stages)


def course(stages):
    m = M("Solar System", "surf", sun=(-0.3, 0.55, 0.78))
    m.top_up_spawns = lambda offered: None   # (eight race spawns of its own on the Sun's ledge)
    k = Kit(m)
    m.raw("palette base 0.14 0.14 0.17 accent 0.42 0.48 0.62 metal 0.55 0.56 0.6 glow 1 0.86 0.6 sky 0.008 0.008 0.02")
    m.raw("setting the solar system")
    m.raw("racedefault 600")   # the ranked race total counts this for anyone with no run here: PLACEHOLDER (15 long stages) until q sets it
    cps = []
    report = []

    # ------------------------------------------------------------------ ramps: pieces, mitres, brushes
    def emit_ramp(r, face, cap, band=0.8):
        P = r.pts()
        F = [_faces(P[i], P[i + 1], r.tan) for i in range(len(P) - 1)]
        caps = []
        for i in range(len(F)):
            ua = F[i][3]
            if i == 0:
                c0 = (-ua[0], 0.0, -ua[1], -(ua[0] * P[0][0] + ua[1] * P[0][2]))
            else:
                mm = _mitre(F[i - 1], F[i], P[i], F[i - 1][3], ua)
                c0 = (-mm[0], -mm[1], -mm[2], -mm[3])
            if i == len(F) - 1:
                c1 = (ua[0], 0.0, ua[1], ua[0] * P[-1][0] + ua[1] * P[-1][2])
            else:
                c1 = _mitre(F[i], F[i + 1], P[i + 1], ua, F[i + 1][3])
            caps.append((c0, c1))
        for i, (fp, fm, _, u) in enumerate(F):
            A, B = P[i], P[i + 1]
            yb = min(A[1], B[1]) - r.rise
            px, pz = -u[1], u[0]
            xs, zs = [], []
            for E in (A, B):
                w = (E[1] - yb) / r.tan + 0.5
                for sg in (1, -1):
                    xs.append(E[0] + px * w * sg); zs.append(E[2] + pz * w * sg)
            mg = 0.4 * (max(A[1], B[1]) - yb) + 0.5
            bb = (min(xs) - mg, yb, min(zs) - mg, max(xs) + mg, max(A[1], B[1]) + 0.01, max(zs) + mg)
            c0, c1 = caps[i]
            base = [fp, fm, c0, c1]
            # split by vertical planes along the line (no flat face inside a ramp, even hidden): the face, and a glow
            # strip down each foot (you see where a ramp ends at speed)
            pA = px * A[0] + pz * A[2]; e = r.W - band
            k.cut(face, bb, base + [(px, 0.0, pz, pA + e), (-px, 0.0, -pz, -pA + e)])
            k.cut(cap, bb, base + [(-px, 0.0, -pz, -pA - e)])
            k.cut(cap, bb, base + [(px, 0.0, pz, pA - e)])

    def globe(mats, cx, cy, cz, rad, n=8, layers=7):
        """a planet: `layers` bands of an n-sided faceted ball (a band's segments are 3-plane cut boxes)"""
        for j in range(layers):
            a0 = -math.pi / 2 + math.pi * j / layers; a1 = -math.pi / 2 + math.pi * (j + 1) / layers
            y0, y1 = cy + rad * math.sin(a0), cy + rad * math.sin(a1)
            r0, r1 = rad * math.cos(a0), rad * math.cos(a1)
            rm = max(r0, r1)
            mat = mats[j % len(mats)]
            for i in range(n):
                t0 = 2 * math.pi * i / n; t1 = 2 * math.pi * (i + 1) / n; tm = (t0 + t1) / 2
                dx, dz = math.cos(tm), math.sin(tm)
                c = math.cos(math.pi / n)
                ra, rb = r0 * c, r1 * c
                gy = (rb - ra) / (y1 - y0)
                pl = (dx, -gy, dz, dx * cx + dz * cz + ra - gy * y0)
                s0 = (-math.sin(t0), 0, math.cos(t0)); s1 = (math.sin(t1), 0, -math.cos(t1))
                side0 = (-s0[0], 0, -s0[2], -(s0[0] * cx + s0[2] * cz)); side1 = (-s1[0], 0, -s1[2], -(s1[0] * cx + s1[2] * cz))
                xsx = [cx, cx + rm * math.cos(t0), cx + rm * math.cos(t1)]
                zsz = [cz, cz + rm * math.sin(t0), cz + rm * math.sin(t1)]
                k.cut(mat, (min(xsx), y0, min(zsz), max(xsx), y1, max(zsz)), [pl, side0, side1])

    def plane_2d(pts, j):
        """the outward edge plane j of convex polygon pts [(a, b)] in its own plane: (na, nb, d)"""
        (ax, ay), (bx, by) = pts[j], pts[(j + 1) % len(pts)]
        cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
        nx, ny = by - ay, -(bx - ax)
        if nx * (cx - ax) + ny * (cy - ay) > 0: nx, ny = -nx, -ny
        L = math.hypot(nx, ny); nx, ny = nx / L, ny / L
        return (nx, ny, nx * ax + ny * ay)

    # ------------------------------------------------------------------ the corridor round a stage's line
    def corridor(st, mats, bot, top, lid):
        """outer walls D + 1 m out from the line (where no other part of the stage goes on), dividing walls halfway
        between two sections side by side, and a roof of kill liquid between two that pass one over the other"""
        S = st.samples
        Bk = {}
        for i, s_ in enumerate(S): Bk.setdefault((math.floor(s_[0] / D), math.floor(s_[1] / D)), []).append(i)
        def near(x, z, rad):
            cx, cz = math.floor(x / D), math.floor(z / D); rr = math.ceil(rad / D); out = []
            for a in range(cx - rr, cx + rr + 1):
                for b in range(cz - rr, cz + rr + 1):
                    for j in Bk.get((a, b), ()):
                        dx, dz = S[j][0] - x, S[j][1] - z
                        if dx * dx + dz * dz < rad * rad: out.append(j)
            return out
        def conflict(i, j):
            """two stretches of the line that are different sections: the line between them is much longer than the
            gap between them (it went away and came back: a turnback's legs, a helix's ends, a crossing)"""
            if S[i][5] == S[j][5]: return False
            d = math.hypot(S[i][0] - S[j][0], S[i][1] - S[j][1])
            return abs(S[i][4] - S[j][4]) > 1.3 * d + 8.0
        groups = {}
        for i in range(len(S)):
            for j in near(S[i][0], S[i][1], 2 * D):
                if j > i and conflict(i, j):
                    d = math.hypot(S[i][0] - S[j][0], S[i][1] - S[j][1])
                    groups.setdefault((S[i][5], S[j][5]), []).append((i, j, d))
        def nearest(prs):
            """for each sample of the earlier section, its nearest partner on each side: [(side, a, b)]"""
            out = []
            for sg in (1, -1):
                best = {}
                for (a, b, d) in prs:
                    cr = S[a][2] * (S[b][1] - S[a][1]) - S[a][3] * (S[b][0] - S[a][0])
                    if (cr > 0) != (sg > 0): continue
                    if a not in best or d < best[a][1]: best[a] = (b, d)
                out += [(sg, a, best[a][0]) for a in sorted(best)]
            return out
        def dsec(x, z, sec):
            return min([math.hypot(S[j][0] - x, S[j][1] - z) for j in near(x, z, D + 2) if S[j][5] == sec] or [1e9])
        def bisector(a, b):
            """the point between samples a and b as far from a's section as from b's (the wall between them): (x, z, d)"""
            lo, hi = 0.0, 1.0
            for _ in range(12):
                t = (lo + hi) / 2
                x, z = S[a][0] + (S[b][0] - S[a][0]) * t, S[a][1] + (S[b][1] - S[a][1]) * t
                if dsec(x, z, S[a][5]) < dsec(x, z, S[b][5]): lo = t
                else: hi = t
            t = (lo + hi) / 2
            x, z = S[a][0] + (S[b][0] - S[a][0]) * t, S[a][1] + (S[b][1] - S[a][1]) * t
            return x, z
        mids = {}
        cls = {}
        for key, prs in groups.items():
            ok = min(d for (i, j, d) in prs) >= 26.0
            if ok:   # (a dividing wall halfway must stand clear of every ramp)
                mids[key] = []
                for (sg, i, j) in nearest(prs):
                    x, z = bisector(i, j)
                    mids[key].append((sg, x, z))
                    if near(x, z, WR + 1.5): ok = False; break
            cls[key] = cls[(key[1], key[0])] = "div" if ok else "slab"
        # the roofs: over the lower section where both corridors cover the ground, halfway between them
        cells = {}
        for key, prs in groups.items():
            if cls[key] != "slab": continue
            for (i, j, d) in prs:
                if S[i][6] >= S[j][7] + 3: u, l = i, j
                elif S[j][6] >= S[i][7] + 3: u, l = j, i
                else:
                    if d < 2 * WR + 4: report.append(f"{st.name}: {S[i][5]} and {S[j][5]} meet at the same height ({d:.0f} m apart)")
                    continue
                mx, mz = (S[i][0] + S[j][0]) / 2, (S[i][1] + S[j][1]) / 2
                rr = D - d / 2 + 2
                for cx in range(math.floor((mx - rr) / 2), math.floor((mx + rr) / 2) + 1):
                    for cz in range(math.floor((mz - rr) / 2), math.floor((mz + rr) / 2) + 1):
                        px, pz = cx * 2 + 1, cz * 2 + 1
                        if (px - S[i][0]) ** 2 + (pz - S[i][1]) ** 2 > (D + 1.5) ** 2: continue
                        if (px - S[j][0]) ** 2 + (pz - S[j][1]) ** 2 > (D + 1.5) ** 2: continue
                        c = cells.setdefault((cx, cz), [1e9, -1e9])
                        c[0] = min(c[0], S[u][6]); c[1] = max(c[1], S[l][7])
        hs = {}
        for c, (lo, hi) in cells.items():
            if lo - hi >= 2: hs[c] = (lo + hi) / 2
        if len(hs) < len(cells): report.append(f"{st.name}: {len(cells) - len(hs)} roof cells with no room")
        for (cx, cz) in sorted(hs):
            y = hs[(cx, cz)]
            ys = [y] + [hs[(cx + a, cz + b)] for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (cx + a, cz + b) in hs]
            k.box(st.mat["kill"], cx * 2, min(ys) - 0.5, cz * 2, cx * 2 + 2, max(ys) + 0.5, cz * 2 + 2)
        def hs_at(x, z):
            c = (math.floor(x / 2), math.floor(z / 2))
            for rr in range(0, 4):
                got = [hs[(c[0] + a, c[1] + b)] for a in range(-rr, rr + 1) for b in range(-rr, rr + 1) if (c[0] + a, c[1] + b) in hs]
                if got: return sum(got) / len(got)
            return None
        def classify(i, x, z):
            """an outer wall's point (x, z) beside sample i: ("full", 0) death floor to the clip, ("up", y) from a roof
            at y up (this section passes over another there), ("low", y) up to a roof at y, or None (open: another
            part of the corridor goes on there)"""
            J = [j for j in near(x, z, D + 0.5) if conflict(i, j)]
            if not J: return ("full", 0.0)
            kind = None
            for j in J:
                if cls.get((S[i][5], S[j][5])) != "slab": return None
                if S[i][6] >= S[j][7] + 3: kd = "up"
                elif S[j][6] >= S[i][7] + 3: kd = "low"
                else: return None
                if kind not in (None, kd): return None
                kind = kd
            y = hs_at(x, z)
            return None if y is None else (kind, y)
        walls = []   # [(a, b, (y0, y1))]
        def chain(pts):
            """runs of points with the same status into bars (straight runs merged)"""
            run = []
            def flush():
                if len(run) >= 2:
                    seg = [0]
                    for n_ in range(1, len(run) - 1):
                        a, q, b = run[seg[-1]], run[n_], run[n_ + 1]
                        d1 = (q[0] - a[0], q[1] - a[1]); d2 = (b[0] - q[0], b[1] - q[1])
                        L1, L2 = math.hypot(*d1), math.hypot(*d2)
                        if L1 > 1e-6 and L2 > 1e-6 and abs(d1[0] * d2[1] - d1[1] * d2[0]) / (L1 * L2) < 0.004 and L1 < 60: continue
                        seg.append(n_)
                    seg.append(len(run) - 1)
                    for a, b in zip(seg, seg[1:]):
                        kd = run[a][2][0]; ys = [q[2][1] for q in run[a:b + 1]]
                        rng = (bot, top) if kd == "full" else (min(ys) - 0.5, top) if kd == "up" else (bot, max(ys) + 0.5)
                        walls.append((run[a][:2], run[b][:2], rng))
                run.clear()
            for q in pts:
                if q[2] is None or (run and (q[2][0] != run[-1][2][0] or math.hypot(q[0] - run[-1][0], q[1] - run[-1][1]) > 6)): flush()
                if q[2] is not None: run.append(q)
            flush()
        for sg in (1, -1):
            chain([(s_[0] - s_[3] * sg * (D + 1), s_[1] + s_[2] * sg * (D + 1), classify(i, s_[0] - s_[3] * sg * (D + 1), s_[1] + s_[2] * sg * (D + 1))) for i, s_ in enumerate(S)])
        for (i, back) in ((0, -1), (len(S) - 1, 1)):   # the two ends
            s_ = S[i]
            pts = []
            for j in range(-13, 14):
                o = (D + 1) * j / 13
                x, z = s_[0] + s_[2] * back - s_[3] * o, s_[1] + s_[3] * back + s_[2] * o
                pts.append((x, z, classify(i, x, z)))
            chain(pts)
        # the dividing walls: halfway between two sections side by side, death floor to the clip
        for key in sorted(groups):
            if cls[key] != "div": continue
            for sg in (1, -1):
                chain([(x, z, ("full", 0.0)) for (s2, x, z) in mids[key] if s2 == sg])
        for (a, b, (y0, y1)) in walls:
            L = math.hypot(b[0] - a[0], b[1] - a[1])
            if L < 1e-6: continue
            ux, uz = (b[0] - a[0]) / L, (b[1] - a[1]) / L
            a2, b2 = (a[0] - ux * 1.2, a[1] - uz * 1.2), (b[0] + ux * 1.2, b[1] + uz * 1.2)
            k.bar(mats["wall2"], a2, b2, 2.0, y0, y1)
            if y1 >= top: k.bar("clip", a2, b2, 2.0, top, lid)
        st.walls = walls
        st.slab = [(c[0] * 2, c[1] * 2) for c in sorted(hs)]
        return near, conflict

    def span(st, near, conflict, i, x, z, nx, nz, a, b):
        """the part of the line across (x, z) from a to b (m, right of the line) that is this section's own"""
        lo, hi = None, None
        o = a
        S = st.samples
        while o <= b + 1e-6:
            px, pz = x + nx * o, z + nz * o
            own = math.hypot(px - S[i][0], pz - S[i][1])
            if all(math.hypot(px - S[j][0], pz - S[j][1]) > own for j in near(px, pz, D + 2) if conflict(i, j)):
                lo = o if lo is None else lo; hi = o
            o += 0.5
        return lo, hi

    def mats(face, accent, wall2, cap, ledge=None, over=None):
        return dict(face=face, accent=accent, wall2=wall2, ledge=ledge or accent, cap=cap, over=over)

    MATS = {
        "sun": (mats("moltenslag", "emberbed", "furnaceglow", "furnaceglow", ledge="cooledmagma"), "magmariver"),
        "mercury": (mats("cooledmagma", "scoria", "pumice", "fumarolecrust"), "liquidvoid"),
        "venus": (mats("sulfurcrust", "amber", "fumarolecrust", "sulfurpitcrust"), "moltengold"),
        "earth": (mats("grass", "glacierice", "cloud", "glowingalgae", over="deepocean"), "liquidvoid"),
        "mars": (mats("redcanyonrock", "ochreearth", "reddunes", "rocksalt"), "magmariver"),
        "asteroids": (mats("boulderfield", "bandediron", "bandediron", "glowingfissures", ledge="boulderfield"), "liquidvoid"),
        "jupiter": (mats("sandstonestrata", "crossbeddedsandstone", "sandstonestrata", "amber", ledge="crossbeddedsandstone"), "moltengold"),
        "io": (mats("sulfurpitcrust", "ropylavarock", "sulfurcrust", "furnaceglow", ledge="ropylavarock"), "magmariver"),
        "europa": (mats("snowflakefrost", "jasper", "lakeice", "seaglass", ledge="jasper"), "liquidvoid"),
        "saturn": (mats("glacierice", "icecave", "icecave", "quartzite", ledge="icecave"), "liquidvoid"),
        "titan": (mats("desertcrust", "ochreearth", "desertcrust", "amber", ledge="ochreearth"), "liquidvoid"),
        "uranus": (mats("glacierice", "frost", "glacierice", "seaglass", ledge="glacierice"), "liquidvoid"),
        "neptune": (mats("icecave", "bluedsteel", "lakeice", "glowwormcave", ledge="bluedsteel", over="deepocean"), "liquidvoid"),
        "kuiper": (mats("snowflakefrost", "blackbasaltblocks", "frost", "glowwormcave", ledge="blackbasaltblocks"), "liquidvoid"),
        "pluto": (mats("tundra", "tuff", "tundra", "quartzcrystals", ledge="tuff"), "liquidvoid"),
    }

    def emit_stage(st, link_in, link_out):
        mt, killmat = MATS[st.name]
        st.mat = dict(mt, kill=killmat)
        Y = st.oy
        x0, z0, x1, z1 = st.extent()
        killY = st.lowest - 12
        assert killY > -97, (st.name, killY)          # (the engine holds feet at y -100: the floor must be over it)
        bot, top, lid = killY - 4, Y + 18, Y + 40
        st.box = (x0, z0, x1, z1); st.killY = killY
        near, conflict = corridor(st, mt, bot, top, lid)
        # the floor: a liquid that kills on touch (some with a band of their planet's own liquid over it: looks only)
        k.box(killmat, x0, killY - 2, z0, x1, killY, z1)
        if mt.get("over"): k.box(mt["over"], x0, killY, z0, x1, killY + 0.6, z1)
        # the arrival ledge, its back on the start wall
        hw = 17.5 if st.items[0] == "split" else LEDGE_W / 2
        lx0, lx1 = st.ox - hw, st.ox + hw
        lz0, lz1 = st.oz, st.oz + LEDGE_D
        k.box(mt["ledge"], lx0, Y - 1.5, lz0, lx1, Y, lz1)
        k.box(mt["accent"], lx0 + 1, Y - 3, lz0 + 1, lx1 - 1, Y - 1.5, lz1)
        for gx in (lx0, lx1 - 0.15): k.box(mt["cap"], gx, Y - 0.35, lz0, gx + 0.15, Y - 0.05, lz1)
        k.box(mt["cap"], lx0, Y - 0.35, lz0, lx1, Y - 0.05, lz0 + 0.15)
        # the arrival portal: a slab over the ledge, under a canopy; you come out of its underside at no more than the
        # run speed and land on the ledge standing. Out of reach from the ledge (7 m up).
        PY = Y + 7.0
        if link_in is not None:
            k.line(f"teleporter {num(st.ox - 3)} {num(PY)} {num(lz0 + 2)} {num(st.ox + 3)} {num(PY + 0.5)} {num(lz1 - 2)} tp {link_in} 3 walk")
            cps.append((lx0 + 0.5, Y, lz0 + 0.5, lx1 - 0.5, Y + 6, lz1 - 0.6))
        k.box(mt["ledge"], lx0, PY + 0.5, lz0, lx1, PY + 1.2, lz1)
        for gx in (lx0, lx1 - 0.6):
            k.box(mt["ledge"], gx, Y, lz1 - 0.6, gx + 0.6, PY + 0.5, lz1)
        for (r, kind) in st.ramps:
            emit_ramp(r, mt["face"], mt["cap"])
            if kind == "helix":   # a column up its middle (inside the inner foot): no dropping through the spiral's eye
                x0_, z0_, y_, h_ = r.at(0.0)
                sg = 1 if r.side > 0 else -1
                hr = math.radians(h_); cx_, cz_ = x0_ + math.cos(hr) * 22 * sg, z0_ + math.sin(hr) * 22 * sg
                R_ = 10.5 / math.cos(math.pi / 8)
                poly = [(cx_ + R_ * math.cos(math.pi * (j + 0.5) / 4), cz_ + R_ * math.sin(math.pi * (j + 0.5) / 4)) for j in range(8)]
                k.prism(mt["wall2"], poly, bot, top)
                k.prism("clip", poly, top, lid)
        # q's kill walls and speed checks, standing alone in the flights
        dbg_k, dbg_c = [], []
        def flight_i(fl, d):
            ux, uz, nx, nz = _hdir(fl["h"])
            x, z = fl["x"] + ux * d, fl["z"] + uz * d
            i = min(range(len(st.samples)), key=lambda j: (st.samples[j][0] - x) ** 2 + (st.samples[j][1] - z) ** 2)
            return x, z, ux, uz, nx, nz, i
        for kw in st.kills:
            x, z, ux, uz, nx, nz, i = flight_i(kw["fl"], kw["d"])
            t = kw["mk"]["kind"]
            a, b = span(st, near, conflict, i, x, z, nx, nz, -D - 1, D + 1)
            if t == "leftwall": a2, b2 = a, D / 3          # (the left two thirds: open on the right)
            elif t == "rightwall": a2, b2 = -D / 3, b
            else: a2, b2 = a, b
            a2, b2 = max(a, a2), min(b, b2)
            P0 = (x + nx * a2 - ux * 0.5, z + nz * a2 - uz * 0.5); P1 = (x + nx * b2 + ux * 0.5, z + nz * b2 + uz * 0.5)
            y0, y1 = bot, lid
            if t in ("topwall", "bottomwall"):
                tt = kw["d"] / kw["fl"]["vx"]; yf = kw["fl"]["yr"] + kw["fl"]["vy"] * tt - 0.5 * G * tt * tt
                if t == "topwall": y1 = yf + 1.0      # (open high: over the flight)
                else: y0 = yf - 1.0                    # (open low: under it)
            k.box(killmat, min(P0[0], P1[0]), y0, min(P0[1], P1[1]), max(P0[0], P1[0]), y1, max(P0[1], P1[1]))
            dbg_k.append((P0, P1))
        for c in st.checks:
            x, z, ux, uz, nx, nz, i = flight_i(c["fl"], c["d"])
            a, b = span(st, near, conflict, i, x, z, nx, nz, -D - 1, D + 1)
            P0 = (x + nx * a - ux * 1.0, z + nz * a - uz * 1.0); P1 = (x + nx * b + ux * 1.0, z + nz * b + uz * 1.0)
            dbg_c.append((P0, P1))
            k.box(mt["wall2"], min(P0[0], P1[0]), bot, min(P0[1], P1[1]), max(P0[0], P1[0]), c["top"] - 0.3, max(P0[1], P1[1]))
            k.box(mt["cap"], min(P0[0], P1[0]), c["top"] - 0.3, min(P0[1], P1[1]), max(P0[0], P1[0]), c["top"], max(P0[1], P1[1]))
        # the end: a platform under the last flight, a framed gate on it (the walk teleporter onto the next stage's
        # ledge; Pluto's: the finish line, in a room)
        px0, pz0, h = st.plat
        ux, uz, nx, nz = _hdir(h)
        tp_ = st.plat_top
        hw, Lp = st.plat_hw, st.plat_len
        def PP(al, ac): return (px0 + ux * al + nx * ac, pz0 + uz * al + nz * ac)
        k.prism(mt["ledge"], [PP(0, -hw), PP(Lp, -hw), PP(Lp, hw), PP(0, hw)], tp_ - 2, tp_)
        k.prism(mt["cap"], [PP(0, -hw), PP(0.6, -hw), PP(0.6, hw), PP(0, hw)], tp_ - 2, tp_ + 0.02)
        gx, gz = PP(Lp - 10, 0)
        gw = 3.5
        gate = (gx - gw, tp_, gz - gw, gx + gw, tp_ + 6, gz + gw)
        if link_out is not None:
            k.line(f"teleporter {num(gate[0])} {num(gate[1])} {num(gate[2])} {num(gate[3])} {num(gate[4])} {num(gate[5])} tp {link_out} 4 walk")
        else:
            for sg in (-1, 1): k.bar(mt["wall2"], PP(8, sg * hw), PP(Lp, sg * hw), 1.0, tp_ - 2, tp_ + 14)
            st.finish = gate
        for (cx_, cz_) in ((gate[0], gate[2]), (gate[0], gate[5]), (gate[3], gate[2]), (gate[3], gate[5])):
            k.box(mt["cap"], cx_ - 0.3, tp_, cz_ - 0.3, cx_ + 0.3, tp_ + 6.6, cz_ + 0.3)
        k.box(mt["cap"], gate[0] - 0.3, tp_ + 6, gate[2] - 0.3, gate[3] + 0.3, tp_ + 6.6, gate[5] + 0.3)
        # lights along the line
        k.light(st.ox, Y + 8, st.oz + 4, 30, 0.7)
        for (r, kind) in st.ramps:
            n = max(1, int(r.length // 60))
            for j in range(n):
                x, z, y, hh = r.at(r.length * (j + 0.5) / n)
                k.light(x, y + 12, z, 60, 0.6)
        k.light(gx, tp_ + 10, gz, 40, 0.8)
        if os.environ.get("SOLAR_TABLE"):
            with open(os.environ["SOLAR_TABLE"], "a") as f:
                f.write(f"== {st.idx + 1} {st.name} (ledge {Y:.0f} m, floor {killY - Y:+.0f} m, faces {st.theta:g} deg)\n")
                for (r, kind) in st.ramps:
                    P = r.pts()
                    f.write(f"   {kind}: {r.length:.0f} m, line {P[0][1] - Y:+.0f} -> {P[-1][1] - Y:+.0f} m, {r.vin:.0f} -> {r.vout:.0f} m/s" + ("; " + "; ".join(r.notes) if r.notes else "") + "\n")
                f.write(f"   end platform {tp_ - Y:+.0f} m\n")
        if os.environ.get("SOLAR_DEBUG"):
            import json
            with open(os.environ["SOLAR_DEBUG"], "a") as f:
                f.write(json.dumps(dict(name=st.name, box=st.box, walls=st.walls,
                                        ramps=[[(p[0], p[2]) for p in r.pts()] + [r.W] for (r, kd) in st.ramps],
                                        plat=[PP(0, -hw), PP(Lp, -hw), PP(Lp, hw), PP(0, hw)], slab=st.slab, kills=dbg_k, checks=dbg_c)) + "\n")

    for i, st in enumerate(stages):
        emit_stage(st, i if i > 0 else None, i + 1 if i + 1 < len(stages) else None)
    for line in report: print("solar:", line)

    # ------------------------------------------------------------------ the planets, each beside its stage (outside the
    # walls, over their tops: out of reach)
    PLANETS = {
        "sun": ["furnaceglow", "moltenslag", "furnaceglow", "moltenslag", "furnaceglow", "moltenslag", "furnaceglow"],
        "mercury": ["pumice", "scoria", "pumice", "cooledmagma", "pumice", "scoria", "pumice"],
        "venus": ["sulfurcrust", "amber", "sulfurcrust", "fumarolecrust", "amber", "sulfurcrust", "amber"],
        "earth": ["glacierice", "malachite", "chippedpaint", "malachite", "cloud", "chippedpaint", "glacierice"],
        "mars": ["redcanyonrock", "reddunes", "ochreearth", "reddunes", "redcanyonrock", "reddunes", "glacierice"],
        "jupiter": ["sandstonestrata", "ochreearth", "crossbeddedsandstone", "sandstonestrata", "ochreearth", "crossbeddedsandstone", "sandstonestrata"],
        "saturn": ["ripplesand", "sandstoneslab", "ripplesand", "crossbeddedsandstone", "ripplesand", "sandstoneslab", "ripplesand"],
        "uranus": ["glacierice", "seafoam", "glacierice", "seafoam", "glacierice", "seafoam", "glacierice"],
        "neptune": ["bluepooltile", "chippedpaint", "bluepooltile", "bluepooltile", "chippedpaint", "bluepooltile", "bluepooltile"],
        "pluto": ["tundra", "tuff", "desertcrust", "tuff", "tundra", "tuff", "tundra"],
        "io": ["sulfurcrust", "sulfurpitcrust", "ropylavarock", "sulfurcrust", "sulfurpitcrust", "sulfurcrust", "ropylavarock"],
        "europa": ["snowflakefrost", "jasper", "snowflakefrost", "saltflat", "jasper", "snowflakefrost", "saltflat"],
        "titan": ["ochreearth", "desertcrust", "ochreearth", "amber", "desertcrust", "ochreearth", "desertcrust"],
        "kuiper": ["blackbasaltblocks", "snowflakefrost", "scoria", "blackbasaltblocks", "snowflakefrost", "scoria", "blackbasaltblocks"],
        "asteroids": ["boulderfield", "bandediron", "boulderfield", "bandediron", "boulderfield", "bandediron", "boulderfield"],
    }
    for st in stages:
        x0, z0, x1, z1 = st.box
        rad = PLANET_R[st.name]
        cx, cy, cz = x1 + 40 + rad, st.oy + 18 + 20, (z0 + z1) / 2
        globe(PLANETS[st.name], cx, cy, cz, rad)
        if st.name == "jupiter": k.box("glow", cx - rad - 1, cy - 22, cz - 2, cx - rad * 0.86, cy - 10, cz + 22)   # (the Great Red Spot)
        if st.name == "neptune": k.box("bluedsteel", cx - rad - 1, cy - 18, cz + 6, cx - rad * 0.86, cy - 8, cz + 24)
        if st.name == "pluto": k.box("saltflat", cx - rad - 1, cy - 12, cz - 10, cx - rad * 0.84, cy + 2, cz + 6)
        if st.name == "earth": globe(["pumice", "volcanicash"], cx + 20, cy + 50, cz + rad + 30, 12, layers=5)
        if st.name == "saturn": k.ring("tuff", cx, cz, rad + 6, rad + 18, cy - 0.4, cy + 0.4, n=16)
        if st.name == "uranus":    # its rings stand on end (in the y-z plane)
            for i in range(12):
                a0 = 2 * math.pi * i / 12; a1 = 2 * math.pi * (i + 1) / 12
                R0, Rr = rad + 12, rad + 16
                pts = [(cz + R0 * math.cos(a0), cy + R0 * math.sin(a0)), (cz + Rr * math.cos(a0), cy + Rr * math.sin(a0)),
                       (cz + Rr * math.cos(a1), cy + Rr * math.sin(a1)), (cz + R0 * math.cos(a1), cy + R0 * math.sin(a1))]
                k.cut("seafoam", (cx - 1, min(p[1] for p in pts), min(p[0] for p in pts), cx + 1, max(p[1] for p in pts), max(p[0] for p in pts)),
                      [(0.0, q[1], q[0], q[2]) for q in (plane_2d(pts, j) for j in range(4))])

    # the start grid: eight surfers on the Sun's ledge, facing down the course
    st0 = stages[0]
    for z in (0.5, 4.5):
        for x in (-5.4, -1.8, 1.8, 5.4): m.spawn("race", st0.ox + x, st0.oy, st0.oz + z, 0)

    cps.append(stages[-1].finish)
    for c in cps: m.raw("checkpoint " + " ".join(num(v) for v in c))
    m.save("solar", dress=False)
