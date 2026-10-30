"""SEWER (1.19.6): free for all, power and 1v1 inside giant sewer pipes, about 110 x 110 m and 90 m tall. The Drop,
a brick shaft 36 m across, has walkway rings at 0, 18, 36, 54 and 72 m round an open middle (a sewage pool at the
bottom: every fall ends in it). A trunk pipe enters the shaft on each level, one compass side each (N at 0, E at 18, S
at 36, W at 54, N again at 72), and runs 21 m to a junction chamber; sloped feeder tunnels spiral round the outside
from chamber to chamber (each 18 m up, two 36 m legs at 14 degrees) and end in the pump room at 72. A maintenance stair
tower beside the shaft (west) links every ring too: two walkable routes up besides the grapple (pipes hung across the
shaft every 18 m), and a dash down the middle is the fast way down. Sludge falls pour from broken pipes at 54 and 72;
a healing balcony hangs on the wall at 45 m, reachable only by grapple or a drop from the ring above.
Teams (power, vs, duel, 1v1): A spawns in the N (0) and W (54) chambers, B in the E (18) and S (36) ones: the same
mean height and the same trunk to the shaft for both.

Second review round (2026-10-30):
- Sight lines down the trunks: each trunk has a cast-iron valve gate across its middle (u 33.6..34.4) with a 4 m gap
  on the side away from its chamber's spawns, and the team spawns sit in the chamber's far corner (v +-6), so no ring
  sees a chamber spawn (spawn exposure from the rings: 0 grid points). The two ring spawns moved onto the stair
  tower's landings (behind its wall). The spawning chambers of E and W also screen the door the other team's feeder
  comes in by (v +-4..4.4, u 42.5..52): a rusher turns a corner first.
- The sludge falls have solid cast-iron downpipes (r 0.85) inside them, so they block shots and bots' sight; the hung
  pipes moved to +-9.5, clear of the falls.
- Ring cover: per ring two valve housings against the wall (1.8 m) and a parapet stub on the inner edge (1.2 m),
  rotated by level, so the 7 m running lane stays clear.
- Looks: each chamber has its own wall material (stained concrete, mossy brick, green subway tile, crackle glaze),
  a sewage channel down its floor and a ceiling manhole with a lit grating; each feeder has a pipe run along its wall
  and a sluice frame at its landing.
- Feeders as flank routes: each feeder landing has a wall teleporter (lit) into an enclosed lookout in the shaft
  wall at L+9 with a firing slit over the ring below (the feeders are too far from the shaft for a real window).
- The rotten planks now cover a sewage pool 2.2 m deep (stairs out on the +-x sides): a real drop.
- Maintenance ladders on cast-iron posts link each ring to the next (18 m at 6.75 m/s); the stair tower's lights are
  brighter.
- Measured (quark_maptool metrics, vs, nearest enemy spawn): A 11.07 s / B 10.23 s, a 0.83 s gap inside the 1 s
  band: the bottom chamber's nearest enemy is a climb away (123 m) while the others drop (84-110 m). Check passes,
  out of bounds included. 1300 brushes, 60 lights."""
import math
from .prefabs import M
from .bigkit import Kit, num, yaw_to, clip_poly

R_IN, R_OUT, N_SEG = 18.0, 20.0, 16       # the shaft (segment k spans 22.5k .. 22.5(k+1) degrees from +x toward +z)
RING_IN = 11.0                            # the walkways: r 11..18
LEVELS = (0, 18, 36, 54, 72)
TOP = 90.0
# the frames of the four compass sides: (u, v) -> (x, z); u runs out from the shaft, v across
U = {0: (0, -1), 1: (1, 0), 2: (0, 1), 3: (-1, 0)}     # N, E, S, W
V = {0: (1, 0), 1: (0, 1), 2: (-1, 0), 3: (0, -1)}


class F:
    """drawing in a compass side's frame (k: 0 N, 1 E, 2 S, 3 W)"""
    def __init__(s, kit, k): s.k, s.f = kit, k
    def xz(s, u, v):
        (ux, uz), (vx, vz) = U[s.f], V[s.f]
        return (u * ux + v * vx, u * uz + v * vz)
    def box(s, mat, u0, u1, y0, y1, v0, v1):
        a, b = s.xz(u0, v0), s.xz(u1, v1)
        s.k.box(mat, a[0], y0, a[1], b[0], y1, b[1])
    def cut(s, mat, u0, u1, y0, y1, v0, v1, planes):
        """planes in the frame: (nu, ny, nv, d)"""
        a, b = s.xz(u0, v0), s.xz(u1, v1)
        (ux, uz), (vx, vz) = U[s.f], V[s.f]
        wp = [(nu * ux + nv * vx, ny, nu * uz + nv * vz, d) for (nu, ny, nv, d) in planes]
        s.k.cut(mat, (min(a[0], b[0]), y0, min(a[1], b[1]), max(a[0], b[0]), y1, max(a[1], b[1])), wp)
    def spawn(s, team, u, y, v, tu, tv):
        x, z = s.xz(u, v); tx, tz = s.xz(tu, tv)
        s.k.spawn(team, x, y, z, yaw_to(x, z, tx, tz))
    def light(s, u, y, v, r=14, i=0.8):
        x, z = s.xz(u, v); s.k.light(x, y, z, r, i)


def seg_poly(i, r0, r1):
    t0, t1 = math.radians(22.5 * i), math.radians(22.5 * (i + 1))
    return [(r0 * math.cos(t0), r0 * math.sin(t0)), (r1 * math.cos(t0), r1 * math.sin(t0)),
            (r1 * math.cos(t1), r1 * math.sin(t1)), (r0 * math.cos(t1), r0 * math.sin(t1))]


def build():
    m = M("Sewer", "ffa", sun=(0.2, 1.0, 0.1))
    m.also = ["power", "1v1", "duel", "vs"]
    k = Kit(m)
    WALL, RING, PIPE, ROOM, TUNNEL = "largeredengineeringbrick", "galvanizedgrating", "tunnellining", "waterstainedconcrete", "largeredengineeringbrick"
    FLOOR = "redengineeringbrick"

    # ---- the shaft wall: 16 segments, openings where the trunks come in and the stair tower's corridor on each ring
    trunk_at = {0: [0, 72], 1: [18], 2: [36], 3: [54]}     # side -> trunk levels
    segs_of = {0: (11, 12), 1: (15, 0), 2: (3, 4), 3: (7, 8)}
    CORR_SEG, CZ0, CZ1 = 9, -13.5, -8.5                     # the stair tower's corridors: through segment 9, z -13.5..-8.5
    for i in range(N_SEG):
        holes = []
        for side, segs in segs_of.items():
            if i in segs: holes += [(L, L + 10) for L in trunk_at[side]]
        poly = seg_poly(i, R_IN, R_OUT)
        y = -3.0
        for (h0, h1) in sorted(holes) + [(TOP, TOP)]:
            if h0 > y: k.prism(WALL, poly, y, h0)
            y = h1
    # segment 9 was drawn whole: the corridors cut it into pieces instead (redraw: remove and rebuild)
    seg9 = seg_poly(CORR_SEG, R_IN, R_OUT)
    m.lines = [l for l in m.lines if not _is_prism_of(l, seg9)]
    y = -3.0
    for L in LEVELS:
        if L > y: k.prism(WALL, seg9, y, L)
        k.prism(WALL, clip_poly(seg9, 0, -1, -CZ1), L, L + 4.5)     # beside the corridor (z > -8.5)
        k.prism(WALL, clip_poly(seg9, 0, 1, CZ0), L, L + 4.5)       # (z < -13.5)
        y = L + 4.5
    k.prism(WALL, seg9, y, TOP)
    # the roof and the daylight down the manhole at the top
    k.ring(ROOM, 0, 0, 0, R_OUT, TOP, TOP + 1, n=N_SEG)
    k.disc("glow", 0, 0, 3.0, TOP - 0.15, TOP, n=8)
    m.raw(f"indoor {num(TOP)}")

    # ---- the walkway rings, the pool at the bottom (sewage: slows you), rotten planks across it
    for L in LEVELS:
        k.ring(RING if L else "waterstainedconcrete", 0, 0, RING_IN, R_IN, (L - 0.6) if L else -3, L, n=N_SEG)
    # (the pool is 1.6 m deep under the planks: when they give way you drop 2 m into it and wade out by the steps east
    # and west, which costs you the fight up top)
    k.ring("mouldbloom", 0, 0, 0, RING_IN - 1, -3, -2.2, n=N_SEG)
    k.ring("waterstainedconcrete", 0, 0, RING_IN - 1, RING_IN, -3, -0.5, n=N_SEG)
    k.ring("sewage", 0, 0, 0, RING_IN - 1, -2.2, -0.6, n=N_SEG)
    for sx in (-1, 1):
        k.stairs("waterstainedconcrete", min(7.2 * sx, 9.8 * sx), -1.5, max(7.2 * sx, 9.8 * sx), 1.5, -0.5, "x", sx, base=-2.2)
    k.box("rottenplanks", -1.0, -0.3, -11.2, 1.0, 0, 11.2)
    # catwalks across the top (72) and pipes hung across the shaft between the rings: grapple anchors every 18 m
    k.box("meshcatwalk", -11.5, 71.6, -1.2, 11.5, 72, 1.2)
    k.box("meshcatwalk", -1.2, 71.6, -11.5, 1.2, 72, -1.2); k.box("meshcatwalk", -1.2, 71.6, 1.2, 1.2, 72, 11.5)
    # (clear of the sludge falls)
    k.tube_x("castiron", -18.5, 18.5, 9, 9.5, 0.6)
    k.tube_z("castiron", -18.5, 18.5, -9.5, 27, 0.6)
    k.tube_x("castiron", -18.5, 18.5, 45, -9.5, 0.6)
    k.tube_z("castiron", -18.5, 18.5, 9.5, 63, 0.6)

    # ---- trunks (21 m, 12 m across) and junction chambers (16 x 16 x 10)
    def trunk(f, L, gap):
        # floor (2 cm under the ring's: no fighting) either side of a sewage channel down the middle (0.4 m down)
        f.box(PIPE, 16.5, 38, L - 1, L - 0.02, -8, -1.5); f.box(PIPE, 16.5, 38, L - 1, L - 0.02, 1.5, 8)
        f.box("waterstainedconcrete", 16.5, 38, L - 1, L - 0.42, -1.5, 1.5)
        f.box("sewage", 17, 38, L - 0.42, L - 0.14, -1.5, 1.5)
        for sv in (-1, 1):
            f.box(PIPE, 17, 38, L - 1, L + 11, 6 * sv, 8 * sv)              # walls (thick: they close the shaft's opening)
            # chamfered corners above and below (an octagon's look; the floor stays 9 m wide)
            if sv > 0:
                f.cut(PIPE, 17, 38, L + 8, L + 10, 4, 6, [(0, -1, -1, -(L + 14))])
                f.cut(PIPE, 17, 38, L, L + 1.5, 4.5, 6, [(0, 1, -1, L - 4.5)])
            else:
                f.cut(PIPE, 17, 38, L + 8, L + 10, -6, -4, [(0, -1, 1, -(L + 14))])
                f.cut(PIPE, 17, 38, L, L + 1.5, -6, -4.5, [(0, 1, 1, L - 4.5)])
        f.box(PIPE, 17, 38, L + 10, L + 11, -8, 8)
        f.light(27, L + 7.5, 0, 16, 0.8)
        # the valve gate across its chamber end (a 4 m gap at one side): no line runs from the shaft into the chamber's
        # spawn corner
        g0, g1 = (-6, 2) if gap > 0 else (-2, 6)
        f.box("castiron", 33.6, 34.4, L - 0.42, L + 10, g0, g1)
        f.box("ruststreaks", 33.2, 34.8, L + 7.6, L + 8.2, g0, g1)                       # (its rack and wheel housing)
        f.box("lightblock", 33.5, 33.6, L + 6.0, L + 6.4, (g0 + g1) / 2 - 1, (g0 + g1) / 2 + 1)
    def chamber(f, L, fin, fout, ROOM=ROOM, screen=0):
        # the floor either side of the sewage channel (the trunk's, carried through to the far wall)
        f.box("waterstainedconcrete", 37, 55, L - 1, L, -9, -1.5); f.box("waterstainedconcrete", 37, 55, L - 1, L, 1.5, 9)
        f.box("waterstainedconcrete", 37, 54, L - 1, L - 0.42, -1.5, 1.5); f.box("sewage", 37, 54, L - 0.42, L - 0.14, -1.5, 1.5)
        # the ceiling, a manhole in it: a short shaft up to a lit grating (daylight's stand-in)
        for (a, b, c, d) in ((37, 45, -9, 9), (47, 55, -9, 9), (45, 47, -9, -1), (45, 47, 1, 9)):
            f.box(ROOM, a, b, L + 10, L + 11, c, d)
        for (a, b, c, d) in ((44.6, 45, -1.4, 1.4), (47, 47.4, -1.4, 1.4), (45, 47, -1.4, -1), (45, 47, 1, 1.4)):
            f.box(ROOM, a, b, L + 11, L + 13.5, c, d)
        f.box("stormdraingrate", 45, 47, L + 13.3, L + 13.5, -1, 1); f.box("glow", 45, 47, L + 13.5, L + 13.6, -1, 1)
        f.light(46, L + 11, 0, 9, 0.7)
        # (clip on the roof's top, round the manhole: the movement-tech check found a way onto it)
        for (a, b, c, d) in ((37, 44.6, -9, 9), (47.4, 55, -9, 9), (44.6, 47.4, -9, -1.4), (44.6, 47.4, 1.4, 9)):
            f.box("clip", a, b, L + 11, L + 11.6, c, d)
        f.box(ROOM, 54, 55, L, L + 10, -8, 8)
        if screen:   # a short wall just inside the feeder door from the other team's chamber: rushers turn a corner
            a, b = sorted((4 * screen, 4.4 * screen))
            f.box(ROOM, 42.5, 52, L, L + 4.5, a, b)
        for sv, door in ((1, fout), (-1, fin)):
            a, b = sorted((8 * sv, 9 * sv))
            if door:
                f.box(ROOM, 37, 44, L, L + 10, a, b); f.box(ROOM, 52, 55, L, L + 10, a, b); f.box(ROOM, 44, 52, L + 6, L + 10, a, b)
            else:
                f.box(ROOM, 37, 55, L, L + 10, a, b)
            c, d = sorted((6 * sv, 8 * sv))
            f.box(ROOM, 37, 38, L, L + 10, c, d)
        f.light(46, L + 8, 0, 16, 0.9)
    def feeder(f, L):
        """from this side's chamber (L) to the next side's (L + 18): leg 1 along +v, a landing, leg 2 back along -u"""
        q = 0.25   # rise per metre (9 m over 36)
        # (floors: steps of 0.25 m every metre, a stepped channel; ceilings: sloped, in 4 m pieces. A walked route's
        # checks see each brush as its box, so a long sloped floor or ceiling would read as a wall across the tunnel)
        # leg 1: v 8..44, rising toward +v
        for i in range(36):
            f.box(FLOOR, 44, 52, L - 0.8 + q * i, L + q * (i + 1), 8 + i, 9 + i)
        for j in range(9):
            v0, v1 = 8 + 4 * j, 12 + 4 * j
            f.cut(TUNNEL, 44, 52, L + 6 + q * (v0 - 8), L + 6.8 + q * (v1 - 8), v0, v1, [(0, -1, q, -(L + 6 - 8 * q)), (0, 1, -q, L + 6.8 - 8 * q)])
        f.box(TUNNEL, 43, 44, L - 1, L + 16.8, 9, 44); f.box(TUNNEL, 52, 53, L - 1, L + 16.8, 9, 44)
        # the landing (L + 9)
        f.box(TUNNEL, 43, 53, L + 8.2, L + 9, 44, 53)
        f.box(TUNNEL, 43, 53, L + 15, L + 15.8, 44, 53)
        f.box(TUNNEL, 52, 53, L + 9, L + 15, 44, 53); f.box(TUNNEL, 43, 52, L + 9, L + 15, 52, 53)
        # leg 2: u 44..8, rising toward -u
        for i in range(36):
            f.box(FLOOR, 43 - i, 44 - i, L + 8.2 + q * i, L + 9 + q * (i + 1), 44, 52)
        for j in range(9):
            u1, u0 = 44 - 4 * j, 40 - 4 * j
            f.cut(TUNNEL, u0, u1, L + 15 + q * (44 - u1), L + 15.8 + q * (44 - u0), 44, 52, [(-q, -1, 0, -(L + 15 + 44 * q)), (q, 1, 0, L + 15.8 + 44 * q)])
        f.box(TUNNEL, 9, 44, L + 8, L + 25, 43, 44); f.box(TUNNEL, 9, 43, L + 8, L + 25, 52, 53)
        for (u, v, y) in ((48, 26, L + 9.5), (48, 48, L + 13), (26, 48, L + 18.5)): f.light(u, y, v, 14, 0.7)
        # a pipe run along each leg's outer wall, up under the ceiling (one sloped brush a leg, against the wall)
        f.cut("castiron", 51.4, 52, L + 5.0, L + 5.6 + 36 * q, 8, 44, [(0, -1, q, -(L + 5.0 - 8 * q)), (0, 1, -q, L + 5.6 - 8 * q)])
        f.cut("castiron", 8, 44, L + 14.0, L + 14.6 + 36 * q, 51.4, 52, [(-q, -1, 0, -(L + 14.0 + 44 * q)), (q, 1, 0, L + 14.6 + 44 * q)])
        # the landing: a sluice gate's frame on its corner, and the wall teleporter straight ahead of leg 1 (a flank:
        # out onto a balcony in the shaft wall, 9 m over the ring below, and back)
        f.box("ruststreaks", 52, 52.6, L + 9, L + 15, 44, 44.6); f.box("ruststreaks", 52, 52.6, L + 14.2, L + 15, 44.6, 51.4)
        f.box("castiron", 52.2, 52.6, L + 10.5, L + 13, 45.2, 51)                       # (the gate, wound up)
        x0, z0 = f.xz(47, 51.5); x1, z1 = f.xz(51, 52.5)
        nx, nz = f.xz(0, -1)
        face = (0 if nx > 0 else 1) if nx else (4 if nz > 0 else 5)
        k.line(f"teleporter {num(min(x0, x1))} {num(L + 9)} {num(min(z0, z1))} {num(max(x0, x1))} {num(L + 12.5)} {num(max(z0, z1))} tp {f.f + 1} {face}")
    def balcony(side, L):
        """the shaft end of a feeder's teleporter: a lookout in the shaft wall at L + 9 (a box with a firing slit, 1 m
        tall, toward the middle of the shaft): only the teleporter reaches it, nobody drops in or out"""
        seg = (4 * side + 14) % 16                       # (the quadrant's diagonal: N 14, E 2, S 6, W 10)
        k.ring(RING, 0, 0, 14.5, R_IN, L + 8.6, L + 9, n=N_SEG, only=[seg])
        LW = "castinplacewalls"
        k.ring(LW, 0, 0, 14.5, 14.9, L + 9, L + 10.2, n=N_SEG, only=[seg])
        k.ring(LW, 0, 0, 14.5, 14.9, L + 11.2, L + 17.4, n=N_SEG, only=[seg])      # (up to the ring over it: no roof to drop onto)
        for (t, sgn) in ((math.radians(22.5 * seg), 1), (math.radians(22.5 * (seg + 1)), -1)):   # its two side walls
            c, s_ = math.cos(t), math.sin(t); tx, tz = -s_ * sgn * 0.3, c * sgn * 0.3
            k.prism(LW, [(14.5 * c, 14.5 * s_), (R_IN * c, R_IN * s_), (R_IN * c + tx, R_IN * s_ + tz), (14.5 * c + tx, 14.5 * s_ + tz)], L + 8.6, L + 17.4)
        t = math.radians(22.5 * (seg + 0.5)); cx, cz = R_IN * math.cos(t), R_IN * math.sin(t)
        dx, dz = -math.cos(t), -math.sin(t)
        face = (0 if dx > 0 else 1) if abs(dx) >= abs(dz) else (4 if dz > 0 else 5)
        k.line(f"teleporter {num(cx - 0.7)} {num(L + 9)} {num(cz - 0.7)} {num(cx + 0.7)} {num(L + 12.5)} {num(cz + 0.7)} tp {side + 1} {face}")
        k.light(16 * math.cos(t), L + 13.5, 16 * math.sin(t), 7, 0.6)
    # (each chamber: its own accent; the spawning chambers screen the door the other team's feeder comes in by, and
    # their trunks' gates leave the gap on the side away from the spawns)
    looks = {0: "waterstainedconcrete", 1: "mossybrick", 2: "greensubwaytile", 3: "crackleglazetile"}
    gaps = {0: -1, 1: -1, 2: -1, 3: -1}          # (the side of each trunk gate's gap: away from the spawn corner)
    screens = {0: 0, 1: -1, 2: 0, 3: -1}         # (E18 and W54: the doors a climbing rusher comes in by)
    for side in range(4):
        f = F(k, side)
        L = 18 * side
        trunk(f, L, gaps[side]); chamber(f, L, side > 0, True, looks[side], screens[side]); feeder(f, L); balcony(side, L)
    top = F(k, 0)
    trunk(top, 72, 1); chamber(top, 72, True, False)     # the pump room (N, at 72): the last feeder comes in, none goes on

    # ---- the stair tower (x -38..-20, z -19..-8.5), its corridors to each ring through segment 9
    TW = "castinplacewalls"
    k.box(TW, -38.5, -1, -19.5, -38, 78, -8); k.box(TW, -38, -1, -19.5, -20, 78, -19)
    k.box(TW, -38, -1, -8.5, -20, 78, -8)
    k.box(TW, -20, -1, -19.5, -19.5, 78, -13.5)
    k.box(TW, -38.5, 78, -19.5, -19.5, 79, -8)
    k.box(TW, -35, -1, -14, -23, 78, -13.5)                                   # (between the flights)
    k.box(ROOM, -38, -1, -19, -20, 0, -8.5)                                    # the ground floor
    y = -1
    for L in LEVELS:   # the east wall between the corridors
        if L > y: k.box(TW, -20, y, -13.5, -19.5, L, -8.5)
        y = L + 4.5
    k.box(TW, -20, y, -13.5, -19.5, 78, -8.5)
    for L in LEVELS:
        # the corridor to the ring
        k.box("bargrating", -20, L - 0.6 if L else -1, CZ0, -15, L, CZ1)
        k.box(TW, -20, L - 0.6, CZ0 - 0.5, -12, L + 4.5, CZ0); k.box(TW, -20, L - 0.6, CZ1, -16.3, L + 4.5, CZ1 + 0.5)
        k.box(TW, -20, L + 4.5, CZ0 - 0.5, -14.5, L + 5, CZ1 + 0.5)
        if L < 72:
            if L: k.box("bargrating", -23, L - 0.6, -19, -20, L, -8.5)                        # near landing
            k.stairs("bargrating", -35, -13.5, -23, -8.5, L + 9, "x", -1, base=L, solid=False)
            k.box("bargrating", -38, L + 8.4, -19, -35, L + 9, -8.5)                          # far landing
            k.stairs("bargrating", -35, -19, -23, -14, L + 18, "x", 1, base=L + 9, solid=False)
            k.light(-29, L + 6, -11, 13, 0.9); k.light(-29, L + 15, -16.5, 13, 0.9)
            k.box("lightblock", -37.9, L + 6, -12, -37.8, L + 6.4, -10); k.box("lightblock", -37.9, L + 15, -17.5, -37.8, L + 15.4, -15.5)
        else:
            k.box("bargrating", -23, L - 0.6, -19, -20, L, -8.5)

    # ---- the sludge falls (5% a second) from broken pipes at 54 and 72, each pouring round a broken cast-iron
    # downpipe (1.6 m: the solid core that really cuts the sightlines across the shaft)
    k.box("toxicsludge", -7.5, -0.3, 4.5, -4.5, 53.4, 7.5)
    k.box("toxicsludge", 4.5, -0.3, -7.5, 7.5, 71.4, -4.5)
    k.disc("castiron", -6, 6, 0.85, -1, 53.4, n=8)
    k.disc("castiron", 6, -6, 0.85, -1, 71.4, n=8)
    # ---- the healing balcony on the wall at 45 m (south-east): grapple up, or drop from the ring at 54
    k.prism("healingtile", [(14.5 * math.cos(math.radians(a)), 14.5 * math.sin(math.radians(a))) for a in (36, 54)] +
            [(R_IN * math.cos(math.radians(a)), R_IN * math.sin(math.radians(a))) for a in (54, 36)], 44.4, 45)
    # ---- the acid drain in the bottom chamber's corner (10% on contact, 10% a second)
    F(k, 0).box("acid", 50, 54, 0, 0.08, -8, -5)

    # ---- maintenance ladders up the shaft from each ring to the next, on a pipe just inside the ring's inner edge, round
    # from that ring's trunk mouth (a walker's quickest way up one level: 18 m at 6.75 m/s)
    for (L, px, pz, face) in ((0, 6, -8.5, "n"), (18, 10.37, 3, "e"), (36, -3, 10.37, "s"), (54, -10.37, -3, "w")):
        k.box("castiron", px - 0.3, L - 1 if L else -3, pz - 0.3, px + 0.3, L + 17.4, pz + 0.3)
        d = {"n": (0, 0.42), "s": (0, -0.42), "e": (-0.42, 0), "w": (0.42, 0)}[face]
        k.ladder(px + d[0], pz + d[1], L, L + 18, face)

    # ---- cover on the rings: per ring a valve housing or two against the wall (1.8 m) and a parapet stub on the inner
    # edge (1.2 m), placed differently on every level and clear of the trunk mouths, the corridors and the catwalks
    COVER = {0: ((170, "h"), (30, "p"), (340, "h")), 18: ((70, "h"), (160, "p"), (290, "h")),
             36: ((20, "h"), (250, "p"), (160, "h")), 54: ((100, "h"), (300, "p"), (250, "h")),
             72: ((200, "h"), (125, "p"), (30, "h"))}
    for L, items in COVER.items():
        for (a, kind) in items:
            j = int(a / 11.25)
            if kind == "h":
                k.ring("castiron", 0, 0, 16.4, R_IN, L, L + 1.8, n=32, only=[j])
                k.ring("ruststreaks", 0, 0, 16.6, R_IN, L + 1.8, L + 2.1, n=32, only=[j])
            else:
                k.ring("castinplacewalls", 0, 0, RING_IN, RING_IN + 0.4, L, L + 1.2, n=32, only=[j, j + 1])

    # ---- lights round each ring
    for L in LEVELS:
        for a in (45, 135, 225, 315):
            k.light(14.5 * math.cos(math.radians(a)), L + 5, 14.5 * math.sin(math.radians(a)), 18, 0.8)
    k.light(0, 1.5, 0, 16, 0.7)

    # ---- spawns: A in the N (0) and W (54) chambers, B in E (18) and S (36); free for all spread over every level
    # (in the chamber's far corner on the side away from the other team's feeder door and the trunk gate's gap: out
    # of every line down the trunk)
    for side, team in ((0, "a"), (3, "a"), (1, "b"), (2, "b")):
        f = F(k, side); L = 18 * side; sv = -6 * gaps[side]
        # (N0's pair nearer its trunk: the bottom chamber's nearest enemy is a climb away; W54's at the chamber's mouth,
        # behind the gate, which evens the teams' routes to the nearest enemy spawn)
        u0 = {0: 44.5, 3: 39.5}.get(side, 49.5)
        f.spawn(team, u0, L, sv, 30, 0 if side != 3 else sv); f.spawn(team, u0 + 3, L, sv, 30, 0 if side != 3 else sv)
        f.spawn("any", 42, L, sv * 0.8, 30, 0)
    top.spawn("any", 44, 72, -5, 30, 0); top.spawn("any", 47.5, 72, -5, 30, 0)
    for L in (18, 54):   # (two more on the stair tower's landings, behind its wall)
        k.spawn("any", -21.5, L, -17, yaw_to(-21.5, -17, -21.5, 0))

    decorate(k, m)
    m.raw("setting giant sewer pipes")
    m.save("sewer", dress=False)


def _is_prism_of(line, poly):
    """a cut line whose box is this polygon's bounds (the whole segment drawn before the corridors were known)"""
    w = line.split()
    if not w or w[0] != "cut": return False
    n = int(w[2]); b = list(map(float, w[3 + 4 * n:9 + 4 * n]))
    xs = [p[0] for p in poly]; zs = [p[1] for p in poly]
    return abs(b[0] - min(xs)) < 1e-3 and abs(b[3] - max(xs)) < 1e-3 and abs(b[2] - min(zs)) < 1e-3 and abs(b[5] - max(zs)) < 1e-3


def decorate(k, m):
    """off the lanes: on the walls, under the rings, in the chambers' corners and against the pump room's walls"""
    # maintenance lamps (light blocks) on the wall above every ring. (Nothing thin along the curved wall at foot
    # height: the walk checks see each brush as its box, and a chord's box reaches metres into the walkway.)
    for L in LEVELS:
        for i in (2, 6, 10, 14):
            k.ring("lightblock", 0, 0, R_IN - 0.1, R_IN, L + 4.6, L + 5.0, n=N_SEG, only=[i])
    # the outflow grate in the wall over the bottom ring
    k.ring("stormdraingrate", 0, 0, R_IN - 0.15, R_IN, 2.0, 5.0, n=N_SEG, only=[5, 6])
    # the sludge falls' broken pipes, out from the wall under the rings at 54 and 72
    for (a, y, end) in ((135, 53.4, (-6, 6)), (315, 71.4, (6, -6))):
        wx, wz = R_IN * math.cos(math.radians(a)), R_IN * math.sin(math.radians(a))
        k.bar("castiron", (wx, wz), end, 1.6, y - 1.4, y)
    # the healing balcony: a lamp over it, a gauge panel behind it
    k.ring("lightblock", 0, 0, R_IN - 0.1, R_IN, 48.5, 49, n=N_SEG, only=[1, 2])
    k.prism("gaugecluster", [(17.85 * math.cos(math.radians(a)), 17.85 * math.sin(math.radians(a))) for a in (40, 50)] +
            [(R_IN * math.cos(math.radians(a)), R_IN * math.sin(math.radians(a))) for a in (50, 40)], 45, 47)
    # the chambers: drain pipes up the far corners, a manhole in the ceiling, a lamp block on the far wall
    for side in range(4):
        f = F(k, side); L = 18 * side
        for sv in (-1, 1):
            x, z = f.xz(53.4, 7.4 * sv); k.disc("castiron", x, z, 0.5, L, L + 10, n=8)
        f.box("lightblock", 53.9, 54, L + 6, L + 7, -2, 2)
    # the pump room (N, 72): two pump housings and a control desk against the walls, a pipe along the ceiling
    f = F(k, 0)
    for v0 in (-6.5, 2.5):
        f.box("castiron", 50.5, 54, 72, 74.6, v0, v0 + 4)
        x, z = f.xz(52.2, v0 + 2); k.disc("ruststreaks", x, z, 1.2, 74.6, 75.2, n=8)
    f.box("controlpanel", 42, 48, 72, 73.3, -8, -7.2)
    f.box("gaugecluster", 42, 48, 73.3, 75, -8, -7.7)
    x0, z0 = f.xz(38, 6.5); x1, z1 = f.xz(54, 6.5)
    k.tube_z("castiron", min(z0, z1), max(z0, z1), x0, 80.6, 0.6)
    # the feeders: light blocks on the tunnel walls by the lamps
    for side in range(4):
        f = F(k, side); L = 18 * side
        f.box("lightblock", 52, 52.1, L + 6.5, L + 7.1, 25, 27)
        f.box("lightblock", 25, 27, L + 17.2, L + 17.8, 52, 52.1)
