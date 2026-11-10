"""GLACIER SUMMIT (1.19.6): king of the hill (and team vs, power, free for all) on a snowy peak, 150 x 130 m. North
is -z. A snowfield at 10 m round a peak whose summit plateau (40 x 40 m) stands at 28 m, its faces 31 degree snow
slopes (walkable) with structure on them (review 2026-10-30, q: yes to all):
  The ridges: A's (the west face, south of the tunnel) and B's (the east face, north of the mast), raised rock spines
  from the shelf to the plateau, 1.6 m over the snow with walkable sides, a rock step across each every 8-9 m
  (2 m over the crest, the other half of the crest left open) to fight up from.
  The rock bands: ledges round the peak at 16 and 22 m (19 and 22 on the short west face), their downhill walls 2 m
  (standing cover from the plateau), broken where the ways up go; one gap on the south face's 22 m band is a frozen
  waterfall (breakable: a rocket or two opens B's straight way up to the slit side for the round).
  The south gully: a channel of darker frost between two rock ribs, the covered way up from the south.
  The north face's ice chute: from behind a rock band across its top (it doesn't aim at the dome) down 10 m wide,
  a dog-leg west at 19 m round the top of a rock rib, then down between two ribs into the crevasse field, where a
  fast slide has to thread the west snow bridge.
  The north flank: the crevasse field under the north face (void) with two snow bridges (yellow wands at their ends)
  and ice towers standing up out of it, the north path behind it along the edge, dunes across the path.
  The south: a scree moraine along the edge (boulders on its crest), dunes across the lane under the face, seracs
  and drifts in the four corners of the snowfield: no clear eye-height line over 60 m on the flat snow.
  The hill: the observatory on the summit, a 16-sided drum 20 m across with a stepped dome (a slit in it facing
  south), the telescope pier in the middle (cover; its fork mount and tube aimed at the slit), control desks round
  the wall, a gallery ring inside at 5 m (a stair up the north wall). The hill box takes in the floor, the gallery
  and the terrace in front of its west and east doors; rock outcrops on the plateau's corners.
  Team A: the cable car top station on the west shelf (16 m), the cabin parked at the edge, its cables going down off
  the west edge over two pylons into the cloud deck, icicles under the eaves.
  Team B: the climbers' hut on the east shelf (14 m): plank walls (shoot-through above 2 m: stone to standing height
  inside them), a stone baffle inside the door and a stone back wall; the snow cat, the ski rack and the melt pond
  (ice slush) beside it.
  A's hidden way: the service tunnel, from a portal in the west slope up a ramp inside the peak to a hatch (a 4 m
  ladder) beside the telescope pier: unseen, but it ends in a choke in the middle of the room.
  B's high way: the radio mast on the east slope (a ladder to its 33 m platform) and its catwalk bridge into the
  gallery: seen, but it gives height.
Ways onto the hill: the two doors, the tunnel's hatch, the mast bridge, the dome's slit (grapple from the terrace
masts), the dome's own steps from the terrace."""
import math
from .prefabs import M
from .m5kit import Kit, fill, slab, walls, rock, beam, yaw_to

HX, HZ = 75, 65
SNOW = 10.0            # the snowfield
SUM = 28.0             # the summit plateau
P = 20.0               # the plateau's half size
SL = 0.6               # the faces' slope (rise per metre)
FOOT = P + (SUM - SNOW) / SL   # 50: where the faces meet the snowfield
A_SH, B_SH = 16.0, 14.0
A_X = -(P + (SUM - A_SH) / SL)  # -40: A's shelf meets the west face
B_X = P + (SUM - B_SH) / SL     # 43.3: B's shelf meets the east face
R = 10.0               # the observatory's radius
GAL = SUM + 5.0        # its gallery
TUN = 2.0              # the tunnel's half width


def surface(x, z):     # the peak's snow surface height over the snowfield
    d = max(abs(x) - P, abs(z) - P, 0)
    return max(SNOW, SUM - SL * d)


def ground(x, z):     # the walk height at (x, z) (the shelves, the peak, the snowfield)
    if abs(z) <= 30 and x <= A_X: return A_SH
    if abs(z) <= 30 and x >= B_X: return B_SH
    return surface(x, z)


# ---- the faces: each one a frame (its outward axis o, its across axis u); a point on a face is (a, d): a across it,
# d out from the plateau's edge (the snow there is at SUM - SL d)
FACES = {"n": ((0, -1), (1, 0)), "s": ((0, 1), (1, 0)), "w": ((-1, 0), (0, 1)), "e": ((1, 0), (0, 1))}


def fy(d): return SUM - SL * d


def frect(f, a0, a1, d0, d1):
    (ox, oz), (ux, uz) = FACES[f]
    pts = [(ox * (P + d) + ux * a, oz * (P + d) + uz * a) for a in (a0, a1) for d in (d0, d1)]
    return min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)


def above_face(f, h):
    """the plane y <= the face's snow + h"""
    (ox, oz), _ = FACES[f]
    return (SL * ox, 1, SL * oz, SUM + SL * P + h)


def in_quadrant(f, s):
    """the face's own half of a corner pyramid (the diagonal through the plateau's corner), on the +u (s 1) or -u side"""
    (ox, oz), (ux, uz) = FACES[f]
    return (s * ux - ox, 0, s * uz - oz, 0)


def rib(k, f, a0, a1, d0, d1, h=2.2, mat="graniterock"):
    """a rock rib down a face (its top h over the snow, following the slope): cover across the face"""
    x0, z0, x1, z1 = frect(f, a0, a1, d0, d1)
    k.cut(mat, (x0, fy(d1) - 1.0, z0, x1, fy(d0) + h, z1), [above_face(f, h)])


def band(k, f, ytop, h, pieces, mat="graniterock"):
    """a rock band across a face: a flat ledge at ytop, its downhill side a wall h tall, in pieces (the gaps between
    them are the ways up); a piece running into a corner pyramid stops at its diagonal, where the next face's band
    at the same height goes on round"""
    dm, dl = (SUM - ytop) / SL - 0.3, (SUM - ytop + h) / SL
    for (a0, a1) in pieces:
        planes = ([in_quadrant(f, 1)] if a1 > P else []) + ([in_quadrant(f, -1)] if a0 < -P else [])
        x0, z0, x1, z1 = frect(f, a0, a1, dm, dl)
        k.cut(mat, (x0, ytop - h - 0.8, z0, x1, ytop, z1), planes)
        # icicles along its lip, on the wall's face (stopping short of a corner's diagonal)
        b0, b1 = max(a0, -P - (dl - 0.4)) + 0.3, min(a1, P + (dl - 0.4)) - 0.3
        if b1 - b0 > 1:
            x0, z0, x1, z1 = frect(f, b0, b1, dl, dl + 0.06)
            k.box("icicles", x0, ytop - 0.7, z0, x1, ytop - 0.05, z1)


RH, RW, RS, RT = 1.6, 2.0, 0.6, 0.9   # a ridge: its crest's height over the face, crest half width, side and foot slopes


def crest(d, d_end, y_end): return min(fy(d) + RH, y_end + RT * (d_end - d), SUM + 0.3 * d)


def ridge(k, f, c, d_end, y_end, steps, mat="graniterock"):
    """a ridge down a face (a team's path to the hill): a raised spine (its crest RH over the snow, 2 RW wide, its
    sides 40 degrees, walkable) from the plateau to the shelf, with rock steps across it to fight from: (d, side)
    each, a block 2 m over the crest that leaves the crest's other side open"""
    (ox, oz), (ux, uz) = FACES[f]
    half = RW + RH / RS
    x0, z0, x1, z1 = frect(f, c - half, c + half, 0, d_end)
    planes = [above_face(f, RH),
              (SL * ox + RS * ux, 1, SL * oz + RS * uz, SUM + SL * P + RH + RS * (RW + c)),
              (SL * ox - RS * ux, 1, SL * oz - RS * uz, SUM + SL * P + RH + RS * (RW - c)),
              (RT * ox, 1, RT * oz, y_end + RT * (P + d_end)),
              (-0.3 * ox, 1, -0.3 * oz, SUM - 0.3 * P)]
    k.cut(mat, (x0, y_end - 1.0, z0, x1, SUM + RH, z1), planes)
    for i, (d, side) in enumerate(steps):
        a0, a1 = (c - half, c + 0.4) if side < 0 else (c - 0.4, c + half)
        x0, z0, x1, z1 = frect(f, a0, a1, d, d + 1.6)
        k.box(("graniterock", "glacierpolishedgranite")[i % 2], x0, fy(d + 1.6) - 0.5, z0, x1, crest(d, d_end, y_end) + 2.0, z1)


def face(k, f, strips):
    """a face as wedges: strips [(a0, a1, [(d0, d1, mat)...])], rock under a piece that starts above the snowfield"""
    (ox, oz), _ = FACES[f]
    rising = {(0, -1): "+z", (0, 1): "-z", (-1, 0): "+x", (1, 0): "-x"}[(ox, oz)]
    for (a0, a1, cells) in strips:
        for (d0, d1, mat) in cells:
            x0, z0, x1, z1 = frect(f, a0, a1, d0, d1)
            k.ramp(mat, x0, z0, x1, z1, fy(d1), fy(d0), rising)
            if fy(d1) > SNOW + 1e-6: k.box("graniterock", x0, SNOW - 4, z0, x1, fy(d1), z1)


def build():
    m = M("Glacier Summit", "koth", sun=(0.5, 0.8, 0.45))
    m.also = ["vs", "power"]
    k = Kit(m)

    # ---- the snowfield (holes: the crevasse field under the north face, ice walls down into the dark; its two snow
    # bridges are the slab left between them)
    crev = CREV
    slab(k, "windpackedsnow", -HX, -HZ, HX, HZ, SNOW - 4, SNOW, holes=crev, tile=30)
    for (x0, z0, x1, z1) in crev:
        for (a, b, c, d) in ((x0, z0 - 0.4, x1, z0), (x0, z1, x1, z1 + 0.4), (x0 - 0.4, z0, x0, z1), (x1, z0, x1 + 0.4, z1)):
            k.box("glacierice", a, SNOW - 16, b, c, SNOW - 4, d)
    m.raw("void -8")

    # ---- the peak: the plateau (rock, the service tunnel in it), four faces and four corners (31 degrees)
    fill(k, "graniterock", (-P, SNOW - 4, -P, P, SUM - 0.4, P), [(-P, 19.9, -TUN, -4, SUM - 0.4, TUN)])
    slab(k, "windpackedsnow", -P, -P, P, P, SUM - 0.4, SUM, holes=[(-R, -R, R, R)], tile=20)
    slab(k, "concrete", -R, -R, R, R, SUM - 0.4, SUM, holes=[(-6, -1.2, -4, 1.2)])          # the observatory's floor
    # the west face, in strips round the tunnel: its floor a ramp from the portal (x -34, 16 m) to 20 m at the
    # plateau's edge, its roof rock under the snow; inside, a ramp on to 24 m and a ladder up to the hatch
    for (z0, z1) in ((-P, -TUN), (TUN, P)):
        k.box("graniterock", -FOOT, SNOW - 4, z0, -P, A_SH, z1)
        k.ramp("windpackedsnow", A_X, z0, -P, z1, A_SH, SUM, "+x")
    k.box("graniterock", -FOOT, SNOW - 4, -TUN, -P, A_SH, TUN)
    k.ramp("concrete", -34, -TUN, -P, TUN, A_SH, 20, "+x")
    s_top = (-SL, 1, 0, SUM + SL * P)                         # y <= 28 + 0.6 (x + 20): the snow
    r_bot = (4 / 14, -1, 0, -(19.5 + 4 / 14 * 34))           # y >= 19.5 + (x + 34) 4/14: the tunnel's roof
    k.cut("graniterock", (-34, 19.5, -TUN, -P, SUM, TUN), [s_top, r_bot])
    k.ramp("concrete", -P, -TUN, -6, TUN, 20, 24, "+x")
    k.box("concrete", -6, 19.9, -TUN, -4, 24, TUN)
    k.ladder(-4, 0, 24, SUM, "e", mat="galvanizedsteel")
    k.light(-27, 20.5, 0, 8, 0.6); k.light(-8, 26, 0, 8, 0.6)
    # the east face (B's), the north face (the ice chute: 10 m wide down from the plateau, a dog-leg west at 19 m
    # round the top of a rock rib, then 12 m wide down to the crevasse field's west bridge) and the south face (its
    # gully: darker, wind-blown frost between two rock ribs)
    k.box("graniterock", P, SNOW - 4, -P, FOOT, SNOW, P)
    k.ramp("windpackedsnow", P, -P, FOOT, P, SNOW, SUM, "-x")
    S, D = "windpackedsnow", FOOT - P
    face(k, "n", [(-P, -10, [(0, D, S)]), (-10, 0, [(0, 14, S), (14, D, "ice")]), (0, 2, [(0, D, "ice")]),
                  (2, 10, [(0, 17, "ice"), (17, D, S)]), (10, P, [(0, D, S)])])
    face(k, "s", [(-P, -12, [(0, D, S)]), (-12, -4, [(0, D, "frost")]), (-4, P, [(0, D, S)])])
    # the corners: pyramids, the apex over the plateau's corner
    for (x0, x1, z0, z1, d) in ((-FOOT, -P, -FOOT, -P, 2), (P, FOOT, -FOOT, -P, 3), (P, FOOT, P, FOOT, 0), (-FOOT, -P, P, FOOT, 1)):
        k.wedge("windpackedsnow", d, x0, SNOW, z0, x1, SUM, z1, kind="corner")
    # the shelves: A's (west, 16 m) and B's (east, 14 m, the melt pond south of the hut), a ramp down off each one's
    # north and south ends
    k.box("windscouredrock", -HX, SNOW - 4, -30, A_X, A_SH - 0.5, 30)
    k.box("windpackedsnow", -HX, A_SH - 0.5, -30, A_X, A_SH, 30)
    pond = (61, 11, 68, 17)
    slab(k, "windscouredrock", B_X, -30, HX, 30, SNOW - 4, B_SH - 0.5, holes=[pond])
    slab(k, "windpackedsnow", B_X, -30, HX, 30, B_SH - 0.5, B_SH, holes=[pond])
    k.box("windscouredrock", pond[0], SNOW - 4, pond[1], pond[2], B_SH - 1.4, pond[3])
    k.box("iceslush", pond[0], B_SH - 1.4, pond[1], pond[2], B_SH - 0.25, pond[3])
    for (x0, x1, y) in ((-70, -52, A_SH), (52, 70, B_SH)):
        L = (y - SNOW) / SL
        k.ramp("windpackedsnow", x0, 30, x1, 30 + L, SNOW, y, "-z")
        k.ramp("windpackedsnow", x0, -30 - L, x1, -30, SNOW, y, "+z")

    terrain(k)
    observatory(k)
    station(k)
    hut(k)
    mast(k)
    snowfield(k)
    decorate(k)
    edge(k)

    # ---- spawns: A in the station, B in the hut, free for all round the peak
    for (x, z) in ((-67, -4), (-63, -4), (-67, 3), (-63, 3)):
        k.spawn("a", x, A_SH, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((64.2, -3), (67.6, -3), (64.2, 2.5), (67.6, 2.5)):
        k.spawn("b", x, B_SH, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((-55, 22), (55, -22), (-40, 53), (36, 53), (-56, -56), (56, 47), (0, -61), (-58, -44)):
        k.spawn("any", x, ground(x, z), z, yaw_to(x, z, 0, 0))
    m.raw(f"hill -13 {SUM:g} -11 13 {SUM + 7:g} 11")

    m.raw("setting an observatory on a snowy peak, a cable car station and a climbers' hut")
    m.raw("palette base 0.8 0.82 0.86 accent 0.2 0.35 0.55 metal 0.3 0.32 0.36 glow 0.6 0.9 1 sky 0.45 0.62 0.85")
    m.save("glacier", dress=False)


# the crevasse field under the north face: three crevasses from the face's foot to the north path, the snow bridges
# between them (x -9..-4 under the chute, x 18..22)
CREV = [(-32, -58, -9, -50), (-4, -58, 18, -50), (22, -58, 32, -50)]


def terrain(k):
    """the structure on the faces: the two ridges (A's west, B's east) with their rock steps, the rock bands at 16 and
    22 m (19 and 22 on the short west face) broken for the ways up, the ribs either side of the south gully and the
    chute, the band across the chute's top"""
    ridge(k, "w", 9, 20, A_SH, [(6, -1), (14, 1)])
    ridge(k, "e", -9, B_X - P, B_SH, [(7, 1), (16, -1)])
    # the bands (pieces: across the face, the gaps between them the ways up)
    band(k, "n", 22, 2.0, [(-33, -24), (-19, -1), (11, 22), (27, 33)])
    band(k, "n", 16, 2.0, [(-43, -26), (-21, -13), (5, 16), (21, 43)])
    band(k, "s", 22, 2.0, [(-33, -26), (-21, -14), (-2, 12), (17, 22), (27, 33)], mat="glacierpolishedgranite")
    band(k, "s", 16, 2.0, [(-43, -28), (-23, -14), (-2, 8), (13, 22), (27, 43)])
    band(k, "e", 22, 2.0, [(-33, -24), (-20, -14), (5, 14), (19, 33)], mat="glacierpolishedgranite")
    band(k, "e", 16, 2.0, [(-43, -26), (-21, -14), (5, 16), (22, 43)])
    band(k, "w", 22, 2.0, [(-33, -16), (-11, -3), (14, 33)])
    band(k, "w", 19, 1.8, [(-P, -13), (-8, -3.5), (14, P)], mat="glacierpolishedgranite")
    # the frozen waterfall: the south face's 22 m band's gap at x 12..17 (B's straight way up to the slit side), iced
    # over: a rocket or two melts it open for the round
    dm, dl = (SUM - 22) / SL - 0.3, (SUM - 20) / SL
    x0, z0, x1, z1 = frect("s", 12, 17, dm, dl)
    k.box("frozenwaterfall", x0, 22 - 2.8, z0, x1, 22.3, z1)
    # the ribs: either side of the south gully, and either side of the chute's lower half (the dog-leg goes round
    # the east one's top)
    for (a0, a1) in ((-14, -12), (-4, -2)):
        rib(k, "s", a0, a1, 3, 11); rib(k, "s", a0, a1, 15, FOOT - P)
    rib(k, "n", -13, -10, 15, FOOT - P)
    rib(k, "n", 2, 5, 17, FOOT - P)
    # across the chute's top on the plateau: it doesn't aim at the dome, you drop in round its ends
    k.box("graniterock", -1, SUM - 0.5, -P - 1.5, 11, SUM + 2.2, -P + 1.0)


def snowfield(k):
    """the snowfield round the peak's foot: the moraine (scree) along the south, snow dunes across the lanes (sight
    breaks you run over), ice towers in the crevasses, seracs (tilted ice blocks) and drifts in the corners, the
    shelves' boulders and the plateau's outcrops"""
    # the moraine: a walkable scree ridge along the south edge (its 35 degree side to the north), 2.4 m up from z 61
    for (x0, x1) in ((-48, -16), (-16, 16), (16, 48)):
        planes = [(0, 1, -0.7, SNOW + 2.4 - 0.7 * 61)]                           # (flat behind its crest, to the cornice)
        if x0 < -40: planes.append((-0.5, 1, 0, SNOW + 24))
        if x1 > 40: planes.append((0.5, 1, 0, SNOW + 24))
        k.cut("scree", (x0, SNOW - 0.5, 56, x1, SNOW + 2.4, HZ), planes)
    for i, x in enumerate((-30, 4, 30)):                       # boulders on its crest, where the dunes cross it
        rock(k, "glacierpolishedgranite", x, 62, 2.3, SNOW + 1.6, 2.8, seed=60 + i)
    # the dunes: two wedges back to back, 2.2 m (over eye height), across the north path and the south lane
    for (x, z0, z1) in ((-48, -HZ, -FOOT), (-20, -HZ, -58), (8, -HZ, -58), (38, -HZ, -FOOT), (-30, FOOT - 3, HZ), (4, FOOT - 3, HZ), (30, FOOT - 3, HZ)):
        k.ramp("windpackedsnow", x - 4.4, z0, x, z1, SNOW, SNOW + 2.2, "+x")
        k.ramp("windpackedsnow", x, z0, x + 4.4, z1, SNOW, SNOW + 2.2, "-x")
    # ice towers standing up out of the crevasses (unclimbable: they break the lines along the crevasse field)
    for (x0, x1, top) in ((-22, -19.5, 4.6), (6, 8.5, 5.2)):
        k.cut("glacierice", (x0, SNOW - 16, -58, x1, SNOW + top + 0.8, -50), [(0.25, 1, 0.12, SNOW + top + 0.25 * (x0 + x1) / 2 + 0.12 * -54)])
    # seracs round the corners of the peak's foot (r, h, tilt), with a drift banked against each cluster
    seracs = [(-63, -55, 2.2, 5.0), (-58, -61, 1.6, 3.4), (-68, -47, 1.6, 4.0),
              (61, -56, 2.4, 5.5), (66, -48, 1.7, 3.6), (55, -61, 1.5, 3.2),
              (-62, 51, 2.2, 5.0), (-68, 58, 1.8, 4.0), (-56, 60, 1.5, 3.2),
              (61, 53, 2.4, 5.5), (67, 59, 1.8, 4.0), (55, 59, 1.5, 3.4), (-65, 44, 1.8, 4.2), (65, 44, 1.8, 4.2)]
    for i, (x, z, r, h) in enumerate(seracs):
        rock(k, "glacierice", x, z, r, SNOW - 0.6, h, seed=40 + i, tilt=(0.35 if x < 0 else -0.35, 0.25 if z < 0 else -0.25))
    for (x0, z0, x1, z1, d) in ((-69, -60, -66, -55, 0), (64, -62, 67, -57, 1), (-69, 50, -66, 55, 0), (64, 49, 67, 54, 1)):
        k.wedge("windpackedsnow", d, x0, SNOW, z0, x1, SNOW + 1.4, z1)
    # the shelves' boulders (cover between the spawn buildings and the faces), and outcrops on the plateau's corners
    # (the terrace's cover at the top of the corner faces)
    for i, (x, z, r) in enumerate(((-50, 18, 2.0), (50, -18, 2.0), (-47, -14, 1.8), (48, 20, 1.8))):
        rock(k, "glacierpolishedgranite", x, z, r, ground(x, z) - 0.6, 2.4, seed=20 + i)
    for i, (x, z) in enumerate(((-18, -18), (18, -18), (18, 18), (-18, 18))):
        rock(k, "graniterock", x, z, 1.6, SUM - 0.6, 2.2, seed=30 + i)


def observatory(k):
    """the drum (16 sides, doors west and east, the bridge's opening east over the door), the stepped dome with its
    slit (south), the pier, the gallery and its stair, lamps"""
    y = SUM
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y, y + 4.2, n=16, a0=-11.25, skip=(0, 8))
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y + 4.2, y + 5.0, n=16, a0=-11.25, skip=(0,))
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y + 5.0, y + 7.6, n=16, a0=-11.25, skip=(0,))
    k.ring("whiteconcrete", 0, 0, R - 0.6, R, y + 7.6, y + 8.4, n=16, a0=-11.25)
    # the dome: three steps and a cap, the slit (segment 4: south) left open in the steps
    for (r0, r1, y0, y1) in ((R - 2.2, R + 0.3, 8.4, 9.6), (R - 4.2, R - 2.0, 9.6, 10.8), (R - 6.4, R - 4.0, 10.8, 12.0)):
        k.ring("standingseamroof", 0, 0, r0, r1, y + y0, y + y1, n=16, a0=-11.25, skip=(4,))
    k.disc("standingseamroof", 0, 0, R - 6.2, y + 12.0, y + 12.8, n=8)
    # the gallery (a ring walk at 5 m) and its stair, a chord inside the north wall, and the landing onto it
    k.ring("aluminiumtreadplate", 0, 0, R - 2.2, R - 0.6, GAL - 0.4, GAL, n=16, a0=-11.25)
    k.stairs("aluminiumtreadplate", -3.5, -6.0, 4.5, -4.2, GAL, "x", 1, base=y, solid=False)
    k.box("aluminiumtreadplate", 4.5, GAL - 0.4, -6.4, 6.6, GAL, -4.2)
    # the telescope pier (cover south of the middle), its fork mount and the tube (1.5 m across), aimed up at the slit
    k.disc("whiteconcrete", 0, 4, 1.5, y, y + 2.4, n=8)
    k.box("galvanizedsteel", -1.1, y + 2.4, 3.0, 1.1, y + 3.0, 5.0)
    for x in (-1.1, 0.8):
        k.box("galvanizedsteel", x, y + 3.0, 3.6, x + 0.3, y + 4.4, 4.4)
    beam(k, "chrome", -0.75, 0.75, (y + 3.4, 1.0), (y + 6.0, 6.8), 1.5)
    k.box("anodizedblack", -0.6, y + 2.6, 0.2, 0.6, y + 3.4, 1.0)                      # the eyepiece end
    # the control desks round the wall, under the gallery (NE, SE, SW, NW: clear of the doors, the stair and the pier)
    for deg in (45, 135, 225, 315):
        t = math.radians(deg); cx, cz = 8.6 * math.cos(t), 8.6 * math.sin(t); tx, tz = -math.sin(t) * 1.6, math.cos(t) * 1.6
        k.bar("controlpanel", (cx - tx, cz - tz), (cx + tx, cz + tz), 0.9, y, y + 1.0)
    # the hatch (the tunnel's ladder comes up through it): a rail on its west side
    k.box("galvanizedsteel", -6.2, y, -1.2, -6.0, y + 1.0, 1.2)
    for (x, z) in ((5, 5), (-5, -5), (5, -5), (-5, 5)):
        k.box("lightblock", x - 0.3, y + 7.8, z - 0.3, x + 0.3, y + 8.2, z + 0.3)
        k.light(x, y + 7.2, z, 10, 0.7)
    # the terrace masts (hooks for the slit)
    for (x, z) in ((-15, 15), (15, 15)):
        k.box("galvanizedsteel", x - 0.25, y, z - 0.25, x + 0.25, y + 11, z + 0.25)
        k.box("galvanizedsteel", x - 0.8, y + 10.6, z - 0.15, x + 0.8, y + 11, z + 0.15)


def station(k):
    """A: the cable car top station (concrete, the cabin parked in its west door); the spawns inside"""
    y = A_SH
    walls(k, "precastconcrete", -72, -9, -57, 8, y, y + 6, [("e", -0.5, 4, 4.2), ("n", -61, 3.2, 4.2), ("s", -61, 3.2, 4.2), ("w", -1, 4.8, 4.6)], t=0.5)
    k.box("precastconcrete", -72.4, y + 6, -9.4, -56.6, y + 6.6, 8.4)
    k.box("powdercoatyellow", -74.6, y + 0.8, -3.2, -71.5, y + 4.4, 1.2)
    k.box("dustyglass", -74.65, y + 2.6, -2.8, -71.5, y + 3.8, 0.8)
    k.box("galvanizedsteel", -73.4, y + 4.4, -1.3, -72.6, y + 7.4, -0.7)
    k.box("galvanizedsteel", -75, y + 7.2, -1.1, -66, y + 7.4, -0.9)
    k.box("lightblock", -66, y + 5.6, -1, -65, y + 5.9, 0)
    k.light(-65.5, y + 5, -0.5, 12, 0.7)
    # icicles under the roof's eaves (high over the doors)
    k.box("icicles", -72.3, y + 5.2, -9.3, -56.7, y + 6, -9.2)
    k.box("icicles", -72.3, y + 5.2, 8.2, -56.7, y + 6, 8.3)
    k.box("icicles", -56.8, y + 5.2, -9.2, -56.7, y + 6, 8.2)


def hut(k):
    """B: the climbers' hut (plank walls, shoot-through above standing height: a riverstone lining to 2 m inside them,
    a stone baffle inside the door and the stone back wall keep the spawns safe), the snow cat, the skis"""
    y = B_SH
    x0, z0, x1, z1 = 58, -7, 70, 6
    for (a, b, c, d) in ((x0, z0, x1, z0 + 0.12), (x0, z1 - 0.12, x1, z1)):          # north and south: planks (1/8 m)
        k.box("largefrostedplanks", a, y, b, c, y + 4.2, d)
    k.box("largefrostedplanks", x0, y, z0 + 0.12, x0 + 0.12, y + 4.2, -2)              # west: planks, the door
    k.box("largefrostedplanks", x0, y, 2, x0 + 0.12, y + 4.2, z1 - 0.12)
    k.box("largefrostedplanks", x0, y + 3.4, -2, x0 + 0.12, y + 4.2, 2)
    k.box("riverstonewall", x1 - 0.6, y, z0, x1, y + 4.2, z1)                          # east: stone
    # the lining (north, south, west: stone to 2 m, over a standing player's head) and the baffle inside the door
    k.box("riverstonewall", x0 + 0.12, y, z0 + 0.12, x1 - 0.6, y + 2.0, z0 + 0.42)
    k.box("riverstonewall", x0 + 0.12, y, z1 - 0.42, x1 - 0.6, y + 2.0, z1 - 0.12)
    k.box("riverstonewall", x0 + 0.12, y, z0 + 0.42, x0 + 0.42, y + 2.0, -2)
    k.box("riverstonewall", x0 + 0.12, y, 2, x0 + 0.42, y + 2.0, z1 - 0.42)
    k.box("riverstonewall", 60.6, y, -2.8, 61.1, y + 4.2, 2.8)
    k.box("icicles", x0 - 0.4, y + 3.6, z0 - 0.45, x1 + 0.4, y + 4.2, z0 - 0.35)
    k.wedge("roofshingles", 2, x0 - 0.5, y + 4.2, z0 - 0.5, x1 + 0.5, y + 6.2, (z0 + z1) / 2)
    k.wedge("roofshingles", 3, x0 - 0.5, y + 4.2, (z0 + z1) / 2, x1 + 0.5, y + 6.2, z1 + 0.5)
    k.box("lightblock", 64.2, y + 3.8, -0.8, 64.8, y + 4.1, -0.2)
    k.light(64.5, y + 3.3, -0.5, 10, 0.7)
    # the snow cat (cover south of the hut) and the ski rack (north)
    k.box("powdercoatyellow", 52, y, 9, 57, y + 2.0, 12)
    k.box("dustyglass", 52.5, y + 2.0, 9.4, 55, y + 2.9, 11.6)
    k.box("anodizedblack", 51.6, y, 8.6, 57.4, y + 0.8, 12.4)
    k.box("frostedplanks", 59, y, -10.5, 66, y + 1.6, -10)


def mast(k):
    """B's high way: the radio mast on the east face (a ladder up its west side to the 33 m platform) and the catwalk
    bridge from it into the dome's gallery"""
    mx = 30.0
    base = surface(mx + 1, 0) - 0.6
    for (x, z) in ((mx - 1, -1), (mx + 1, -1), (mx - 1, 1), (mx + 1, 1)):
        k.box("galvanizedsteel", x - 0.2, base, z - 0.2, x + 0.2, GAL + 9, z + 0.2)
    k.box("galvanizedsteel", mx - 1.2, base, 1.0, mx + 1.2, GAL - 0.4, 1.2)            # the ladder's backing (south)
    foot = surface(mx - 1.2, 0)                                                       # a concrete footing at its foot
    k.box("concrete", mx - 1.2, base, 1.2, mx + 1.2, foot, 3.0)
    k.ladder(mx, 1.2, foot, GAL, "n", mat="galvanizedsteel")
    k.box("aluminiumtreadplate", mx - 1.6, GAL - 0.4, -1.6, mx + 1.6, GAL, 1.0)        # the platform
    k.box("aluminiumtreadplate", R - 0.6, GAL - 0.4, -1.0, mx - 1.0, GAL, 1.0)          # the bridge
    for z in (-1.1, 1.0):
        k.box("galvanizedsteel", R + 0.2, GAL, z, mx - 1.0, GAL + 1.1, z + 0.1)
    k.box("anodizedblack", mx - 1.6, GAL, -1.9, mx + 1.6, GAL + 2.2, -1.6)              # the radio cabinet, and a rail
    k.box("galvanizedsteel", mx + 1.6, GAL, -1.7, mx + 1.7, GAL + 1.1, 1.0)
    k.box("glow", mx - 0.3, GAL + 9, -0.3, mx + 0.3, GAL + 9.5, 0.3)


def decorate(k):
    """a little, off the lanes: marker wands at the snow bridges' ends (where to cross), the station's bullwheel
    overhead and its cable off the west edge (two pylons, down into the cloud deck), the weather instruments on the
    summit terrace"""
    for (x0, x1) in ((-9, -4), (18, 22)):
        for x in (x0 + 0.3, x1 - 0.3):
            for z in (-58.4, -49.6):
                k.box("powdercoatyellow", x - 0.08, surface(x, z) if z > -50 else SNOW, z - 0.08, x + 0.08, (surface(x, z) if z > -50 else SNOW) + 1.6, z + 0.08)
    k.box("far", -600, -46, -600, 600, -45, 600)                                       # the cloud deck
    k.box("galvanizedsteel", -75, A_SH + 7.2, 0.5, -66, A_SH + 7.4, 0.7)               # the return cable
    for zc in (-1.0, 0.6):                                                             # the cables' far side
        for (ax, ay, bx, by) in ((-76, A_SH + 7.3, -130, 12), (-130, 12, -215, -14), (-215, -14, -300, -46)):
            xbar(k, "far", ax, ay, bx, by, zc - 0.1, zc + 0.1, 0.2)
    for (px, top) in ((-130, 12), (-215, -14)):                                        # the pylons
        k.box("far", px - 0.7, -46, -1.0, px + 0.7, top - 0.6, 0.6)
        k.box("far", px - 0.4, top - 0.6, -2.2, px + 0.4, top - 0.1, 1.8)
    k.disc("galvanizedsteel", -68, -0.5, 2.2, A_SH + 4.8, A_SH + 5.2, n=8)
    k.box("anodizedblack", -68.2, A_SH + 5.2, -0.7, -67.8, A_SH + 6, -0.3)
    for (x, z) in ((-15, -15), (15, -15)):
        k.box("whiteconcrete", x - 0.5, SUM, z - 0.5, x + 0.5, SUM + 1.1, z + 0.5)
        k.box("galvanizedsteel", x - 0.06, SUM + 1.1, z - 0.06, x + 0.06, SUM + 2.6, z + 0.06)
        k.box("galvanizedsteel", x - 0.5, SUM + 2.5, z - 0.04, x + 0.5, SUM + 2.6, z + 0.04)


def xbar(k, mat, ax, ay, bx, by, z0, z1, w):
    """a straight bar in the x-y plane from (ax, ay) to (bx, by), w thick, z0..z1 (a cable going down)"""
    dx, dy = bx - ax, by - ay
    L = math.hypot(dx, dy)
    nx, ny = -dy / L, dx / L
    c = nx * ax + ny * ay
    k.cut(mat, (min(ax, bx), min(ay, by) - w, z0, max(ax, bx), max(ay, by) + w, z1), [(nx, ny, 0, c + w / 2), (-nx, -ny, 0, -c + w / 2)])


def edge(k):
    """the cornice round the edge (a low snow lip), clip over it up to 90 m (the dome's top is 40.8 m)"""
    for (x0, z0, x1, z1) in ((-HX - 1, -HZ - 1, HX + 1, -HZ), (-HX - 1, HZ, HX + 1, HZ + 1), (-HX - 1, -HZ, -HX, HZ), (HX, -HZ, HX + 1, HZ)):
        k.box("nograpple", x0, SNOW - 4, z0, x1, SNOW + 1.2, z1)
        k.box("clip", x0, SNOW + 1.2, z0, x1, 90, z1)
    k.box("clip", -HX, 90, -HZ, HX, 91, HZ)
