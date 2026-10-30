"""CASTLE (1.19.6): king of the hill, 4v4. A concentric castle. The hill is the inner ward, a 32 x 32 m courtyard at
the keep's foot; the teams spawn in barbicans at the north and south gates of the outer curtain (about 75 m from
the hill's edge). Ways onto the hill: the north and south inner gatehouses, the west postern, the keep's great hall
(its east door opens on the outer bailey), the two wall-walk stairs down the west wall, the keep's balcony over the
hill, and the undercroft tunnel from the west bailey that comes up through a hatch by the well. Loops: round the
keep, the inner wall-walk, the outer wall-walk, the undercroft. Mirrored north to south (each team the same map;
the keep stands east of the ward, the same distance from both gates)."""
from .prefabs import *
from .mkit import wedge, cylinder, slope, peak, ring

# the ward (the hill's courtyard) and the block round it: the inner walls (walk at 9 m) and the keep east of it
WARD = 16                 # the ward: x, z in -16..16
IW = 4                    # inner wall thickness (z 16..20, x -20..-16)
WALK = 9.0                # the inner wall-walk, the keep's first floor and its balcony (one level: they join)
KX0, KX1 = 16, 44         # the keep, x (z -20..20); walls 1.5 m
KT = 1.5
ROOF = 26.0               # the keep's roof terrace
MOAT = 8                  # the moat round the ward + keep block
CX, CZ, CT = 75, 72, 4    # the outer curtain's outer faces, its thickness
OWALK = 12.0              # the outer wall-walk
BZ = 94                   # the barbicans reach out to z = +-94
LID = 45.0                # clip over everything


class ZM:
    """a builder that mirrors north <-> south (z -> -z): each team's half the same"""
    def __init__(s, m): s.m = m
    def box(s, x0, y0, z0, x1, y1, z1, mat="castlestone", mirror=True):
        s.m.box(x0, y0, z0, x1, y1, z1, mat=mat)
        if mirror and not (abs(z0 + z1) < 1e-9): s.m.box(x0, y0, -z1, x1, y1, -z0, mat=mat)
    def wedge(s, mat, d, b, up=0, mirror=True):
        wedge(s.m, mat, d, b, up)
        if mirror: wedge(s.m, mat, {0: 0, 1: 1, 2: 3, 3: 2}[d], (b[0], b[1], -b[5], b[3], b[4], -b[2]), up)
    def corner(s, mat, d, b, mirror=True):
        slope(s.m, "corner", mat, d, b)
        if mirror: slope(s.m, "corner", mat, {0: 3, 3: 0, 1: 2, 2: 1}[d], (b[0], b[1], -b[5], b[3], b[4], -b[2]))
    def stairs(s, x0, z0, x1, z1, top, axis, rising, base=0.0, mirror=True):
        s.m.stairs(x0, z0, x1, z1, top, axis, rising, base=base)
        if mirror: s.m.stairs(x0, -z1, x1, -z0, top, axis, -rising if axis == "z" else rising, base=base)
        s._restyle(2 if mirror else 1)
    def _restyle(s, flights):
        # m.stairs writes plain "box" steps: give the last flights' steps a material (stone stairs)
        n = 0
        for i in range(len(s.m.lines) - 1, -1, -1):
            l = s.m.lines[i]
            if l.startswith("stairs "):
                n += 1
                if n == flights: break
            elif l.startswith("box "): s.m.lines[i] = "largemossystoneblocks " + l[4:]
    def ladder(s, x, z, y0, y1, face, mirror=True):
        s.m.ladder(x, z, y0, y1, face)
        if mirror: s.m.ladder(x, -z, y0, y1, {"n": "s", "s": "n"}.get(face, face))
    def light(s, x, y, z, r=9, i=0.7, mirror=True):
        s.m.light(x, y, z, r, i, fixture=False)
        if mirror and abs(z) > 1e-9: s.m.light(x, y, -z, r, i, fixture=False)
    def spawn(s, team, x, y, z, yaw, mirror_team=None):
        s.m.spawn(team, x, y, z, yaw)
        if mirror_team: s.m.spawn(mirror_team, x, y, -z, (180 - yaw + 180) % 360 - 180)


def build():
    m = M("Castle", "koth", sun=(-0.85, 0.38, 0))   # (late afternoon, low from the west: both halves lit alike)
    m.raw("palette base 0.64 0.58 0.5 accent 0.58 0.3 0.16 metal 0.32 0.29 0.26 glow 1 0.68 0.32 sky 0.96 0.74 0.52")
    m.also = ["power"]
    z = ZM(m)
    ground(m, z)
    curtain(m, z)
    inner(m, z)
    keep(m, z)
    bailey(m, z)
    town(m, z)
    objects(m, z)
    decorate(m, z)
    m.lines = [("castlestone " + l[4:]) if l.startswith("box ") else l for l in m.lines]   # (no untextured base anywhere)
    m.raw("setting a medieval concentric castle")
    m.save("castle", dress=False)


def ground(m, z):
    # the floor: setts in the ward, dirt in the bailey; holes for the moat, the undercroft's stairwell and its hatch
    mo = (-WARD - IW - MOAT, -WARD - IW - MOAT, KX1 + MOAT, WARD + IW + MOAT)    # the moat's outer edge
    block = (-WARD - IW, -WARD - IW, KX1, WARD + IW)
    stairwell = (-58, -2, -45, 2)
    hatch = (-11.2, -0.7, -9.8, 0.7)
    for (x0, z0, x1, z1) in minus((-CX + CT, -CZ + CT, CX - CT, CZ - CT), [mo, stairwell]):
        m.box(x0, -1, z0, x1, 0, z1, mat="dirt")
    for (x0, z0, x1, z1) in minus((-WARD, -WARD, WARD, WARD), [hatch]):
        m.box(x0, -1, z0, x1, 0, z1, mat="cobbledsetts")
    # the moat: a channel 2 m deep of swamp water (slow, never lethal), lined in mossy stone, steps out at the corners.
    # A berm (a stone path at the ground's level) runs along the keep's north, east and south faces inside the moat:
    # round the keep on the ground is a loop (north gate, berm, east door, berm, south gate)
    berms = [(44, -22.5, 46.5, 22.5)]
    for sz in (1, -1):
        for (x0, z0, x1, z1) in ((12, WARD + IW, 46.5, WARD + IW + 2.5), (8, WARD + IW, 12, WARD + IW + 5.5), (3.5, WARD + IW + 3, 8, WARD + IW + 5.5)):
            berms.append((x0, z0, x1, z1) if sz > 0 else (x0, -z1, x1, -z0))
    for (x0, z0, x1, z1) in berms:
        m.box(x0, -3, z0, x1, 0, z1, mat="largemossystoneblocks")
    for (x0, z0, x1, z1) in minus(mo, [block] + berms):
        m.box(x0, -3, z0, x1, -2, z1, mat="largemossystoneblocks")
        m.box(x0, -2, z0, x1, -0.3, z1, mat="swampwater")
    for (x0, z0, x1, z1) in ((mo[0] - 1, mo[1], mo[0], mo[3]), (mo[2], mo[1], mo[2] + 1, mo[3])):
        m.box(x0, -3, z0, x1, -1, z1, mat="largemossystoneblocks")
    z.box(mo[0] - 1, -3, mo[3], mo[2] + 1, -1, mo[3] + 1, mat="largemossystoneblocks")
    m.box(block[0], -3, block[1], block[0] + 1, -1, block[3], mat="largemossystoneblocks")   # the inner side
    m.box(block[2] - 1, -3, block[1], block[2], -1, block[3], mat="largemossystoneblocks")
    z.box(block[0], -3, block[3] - 1, block[2], -1, block[3], mat="largemossystoneblocks")
    # steps out of the water: two flights each side of the block (west and east channels)
    z.stairs(mo[0], 10, mo[0] + MOAT, 13, 0, "x", -1, base=-2)
    z.stairs(mo[2] - MOAT + 2.5, 10, mo[2], 13, 0, "x", 1, base=-2)                   # (from the berm's outer side)
    z.stairs(30, WARD + IW + 2.5, 33, mo[3], 0, "z", 1, base=-2)                              # (and out of the north / south channels)
    z.stairs(-15, WARD + IW, -12, mo[3], 0, "z", 1, base=-2)
    # the gate bridges over the moat (north / south gatehouses, the west postern, the keep's east door)
    z.box(-3.5, -1, WARD + IW, 3.5, 0, mo[3], mat="wideplanks")
    m.box(mo[0], -1, -3, -WARD - IW, 0, 3, mat="wideplanks")
    m.box(KX1, -1, -3, mo[2], 0, 3, mat="wideplanks")


def curtain(m, z):
    """the outer curtain: 4 m thick, its walk at 12 m with merlons on the outer edge (nograpple outside), round
    towers at the corners and halfway along the east and west walls, a gate passage north and south into the
    barbicans"""
    t = CT
    H = OWALK
    def merlons(x0, z0, x1, z1, along):
        a0, a1 = (x0, x1) if along == "x" else (z0, z1)
        k = a0
        while k < a1 - 0.01:
            e = min(k + 1.5, a1)
            if along == "x": m.box(k, H, z0, e, H + 2, z1, mat="castlestone")
            else: m.box(x0, H, k, x1, H + 2, e, mat="castlestone")
            k += 3.0
        if along == "x": m.box(x0, H, z0, x1, H + 0.6, z1, mat="castlestone")
        else: m.box(x0, H, z0, x1, H + 0.6, z1, mat="castlestone")
    # west / east walls (full length), north / south walls with the gate passage (x -5..5, 7 m high)
    for sx in (-1, 1):
        x0, x1 = (-CX, -CX + t) if sx < 0 else (CX - t, CX)
        m.box(x0, -1, -CZ, x1, H, CZ, mat="largecastlestone")
        ox0, ox1 = (x0 - 0.1, x0) if sx < 0 else (x1, x1 + 0.1)
        m.box(ox0, -1, -CZ, ox1, H + 2, CZ, mat="nograpple")                # (no hooking the outside face)
        mx0, mx1 = (x0, x0 + 0.8) if sx < 0 else (x1 - 0.8, x1)
        merlons(mx0, -CZ, mx1, CZ, "z")
    for (a0, a1) in ((-CX + t, -9.5), (-7, -5), (5, 7), (9.5, CX - t)):          # (the gate, and a sally port each side: x 7..9.5)
        z.box(a0, -1, CZ - t, a1, H, CZ, mat="largecastlestone")
        z.box(a0, H, CZ - 0.8, a1, H + 0.6, CZ, mat="castlestone")
    for (a0, a1) in ((-9.5, -7), (7, 9.5)):
        z.box(a0, 3.5, CZ - t, a1, H, CZ, mat="largecastlestone")
        z.box(a0, -1, CZ - t, a1, 0, CZ, mat="dirt")
        z.box(a0, H, CZ - 0.8, a1, H + 0.6, CZ, mat="castlestone")
    for (a0, a1) in ((-CX + t, -14), (14, CX - t)):
        z.box(a0, -1, CZ, a1, H + 2, CZ + 0.1, mat="nograpple")
    z.box(-5, 7, CZ - t, 5, H, CZ)                                         # over the gate passage
    for (a0, a1) in ((-CX + t, -5), (5, CX - t)):
        k = a0
        while k < a1 - 0.01:
            z.box(k, H + 0.6, CZ - 0.8, min(k + 1.5, a1), H + 2, CZ)
            k += 3.0
    # the towers: round, 18 m, a parapet ring on top (hooks: their parapets and the merlons)
    # (hollow above the walk: a floor at 12, doorways where the walks meet them, a parapet; the walk is one loop)
    import math as _m
    for (tx, tz) in ((-CX + 6, CZ - 6), (CX - 6, CZ - 6), (-CX + 6, 0), (CX - 6, 0)):
        for zz in ((tz, -tz) if tz else (0,)):
            cylinder(m, "largecastlestone", tx, zz, 6, -1, H, sides=12)
            sx = 1 if tx > 0 else -1
            def on_walk(px, pz): return CX - t <= abs(px) <= CX or CZ - t <= abs(pz) <= CZ
            def outside(px, pz): return abs(px) > CX or abs(pz) > CZ
            doors = set()
            for k in range(12):   # (a doorway in every segment that stands on a walk, but none facing out of the castle)
                a = _m.radians((k + 0.5) * 30)
                if on_walk(tx + 5.5 * _m.cos(a), zz + 5.5 * _m.sin(a)) and not outside(tx + 6.3 * _m.cos(a), zz + 6.3 * _m.sin(a)): doors.add(k)
            ring(m, "largecastlestone", tx, zz, 5, 6, H, H + 3.2, sides=12, skip=doors)
            ring(m, "largecastlestone", tx, zz, 5, 6, H + 3.2, 18, sides=12)
            ring(m, "castlestone", tx, zz, 5, 6.3, 18, 18.8, sides=12)
    # stairs up to the walk: along the inside of the west and east walls, two flights a side (24 steps of 0.5)
    for (x0, x1, r) in ((-CX + t, -CX + t + 3, 1), (CX - t - 3, CX - t, -1)):
        z.stairs(x0, 26, x1, 50, H, "z", 1)
    z.stairs(30, CZ - t - 3, 54, CZ - t, H, "x", 1)                                    # up to the north / south walks
    # the barbicans: a walled yard outside each gate, 10 m walls, open to the sky (the spawns)
    for (x0, z0, x1, z1) in ((-14, CZ, -12, BZ), (12, CZ, 14, BZ), (-12, BZ - 2, 12, BZ)):
        z.box(x0, -1, z0, x1, 10, z1, mat="largecastlestone")
        z.box(x0, 10, z0, x1, 10.6, z1)
    z.box(-12, -1, CZ, 12, 0, BZ - 2, mat="dirt")
    z.box(-5, -1, CZ - t, 5, 0, CZ, mat="dirt")
    # the gate towers either side of the barbican's mouth, square, 14 m (a perch over the spawn, by grapple)
    for (x0, x1) in ((-14, -10), (10, 14)):   # (inside the barbican's line: nothing hangs over the void)
        z.box(x0, -1, CZ - 2, x1, 14, CZ + 4, mat="largecastlestone")
        z.box(x0, 14, CZ - 2, x1, 14.6, CZ + 4)
        z.box(x0, 14.6, CZ - 2, x1, LID, CZ + 4, mat="clip")                          # (no perch over the spawns)
    # the gate passage's dressing: a raised portcullis (bars from 5.6 m up), an arch (haunches hanging from 5 m at
    # the sides), torches on the barbican face either side
    for k in range(7):
        bx = -4.5 + 1.5 * k
        z.box(bx - 0.08, 5.6, CZ - 2.1, bx + 0.08, 7, CZ - 1.9, mat="metal")
    z.box(-5, 5.6, CZ - 2.12, 5, 5.8, CZ - 1.88, mat="metal")
    z.wedge("castlestone", 1, (-5, 5, CZ - t, -3.2, 7, CZ), up=1)
    z.wedge("castlestone", 0, (3.2, 5, CZ - t, 5, 7, CZ), up=1)
    for bx in (-6.2, 6.2):
        z.box(bx - 0.25, 3.0, CZ, bx + 0.25, 3.2, CZ + 0.4, mat="charredtimber")
        z.box(bx - 0.2, 3.2, CZ + 0.05, bx + 0.2, 3.7, CZ + 0.35, mat="lightblock")
    z.light(0, 4, CZ + 2, 8, 0.6)
    # the barbican's baffle: a solid screen across the yard in front of the gate (x -9..9), the way out goes round
    # both ends of it, to the gate or the sally ports; nothing in the road sees the spawns
    z.box(-9, -1, CZ + 7, 9, 6, CZ + 8, mat="largecastlestone")
    z.box(-9.2, 6, CZ + 6.8, 9.2, 6.4, CZ + 8.2)
    for bx in (-11, 11):
        z.box(bx - 0.3, 2.2, CZ + 7.2, bx + 0.3, 2.7, CZ + 7.8, mat="lightblock")
    # clip: up from every outer wall top to the lid, and the lid (nobody hooks or jumps out)
    for sx in (-1, 1):
        x0, x1 = (-CX - 1, -CX) if sx < 0 else (CX, CX + 1)
        m.box(x0, H + 2, -CZ - 1, x1, LID, CZ + 1, mat="clip")
    for (a0, a1) in ((-CX, -14), (14, CX)):
        z.box(a0, H + 2, CZ, a1, LID, CZ + 1, mat="clip")
    for (x0, z0, x1, z1) in ((-14, CZ + 1, -12, BZ), (12, CZ + 1, 14, BZ), (-12, BZ - 2, 12, BZ)):
        z.box(x0, 10.6, z0, x1, LID, z1, mat="clip")                                   # (on the barbican's wall tops)
    m.box(-CX - 1, LID, -BZ - 1, CX + 1, LID + 1, BZ + 1, mat="clip")


def inner(m, z):
    """the ward's walls: north and south (gatehouses in them), west (a postern, and the two stairs down from the
    walk). The walk is at 9 m, open on the ward side, merlons on the bailey side."""
    W0, W1 = WARD, WARD + IW
    # north / south walls, x -20..16, the gatehouse passage x -3..3 (5 m high)
    z.box(-W1, -1, W0, -3, WALK, W1)
    z.box(3, -1, W0, KX0, WALK, W1)
    z.box(-3, 5, W0, 3, WALK, W1)
    z.box(-3, -1, W0, 3, 0, W1, mat="cobbledsetts")                                     # the passage floor
    m.box(-W1, -1, -2, -W0, 0, 2, mat="cobbledsetts")
    # the gatehouse's flanking towers (square, 14 m, out over the moat edge): hooks, a perch
    for (x0, x1) in ((-8, -3), (3, 8)):
        z.box(x0, -1, W1, x1, 14, W1 + 3, mat="largecastlestone")
        z.box(x0 - 0.3, 14, W1 - 0.3, x1 + 0.3, 14.6, W1 + 3.3)
    # west wall, the postern (z -2..2, 3.5 m high)
    m.box(-W1, -1, 2, -W0, WALK, W0)
    m.box(-W1, -1, -W0, -W0, WALK, -2)
    m.box(-W1, 3.5, -2, -W0, WALK, 2)
    m.box(-13.4, 0, -2.6, -12.8, 2.6, 2.6, mat="largecastlestone")                      # the postern's screen: in round it (a dogleg)
    # merlons on the bailey side of the walk
    def mer(x0, z0, x1, z1, along):
        a0, a1 = (x0, x1) if along == "x" else (z0, z1)
        k = a0
        while k < a1 - 0.01:
            e = min(k + 1.5, a1)
            m.box(*((k, WALK, z0, e, WALK + 1.8, z1) if along == "x" else (x0, WALK, k, x1, WALK + 1.8, e)), mat="castlestone")
            k += 3.0
    for zz in (1, -1):
        z0, z1 = (W1 - 0.7, W1) if zz > 0 else (-W1, -W1 + 0.7)
        mer(-W1, z0, -8, z1, "x"); mer(8, z0, KX0, z1, "x")
    mer(-W1, -W0, -W1 + 0.7, W0, "z")
    # over the gatehouses (x -8..8) the walk has a parapet on the ward side too (1 m, merlons to 1.8): from there
    # the ward is seen through the crenels only
    z.box(-8, WALK, W0, 8, WALK + 1.0, W0 + 0.6, mat="castlestone")
    for zz in (1, -1):
        z0, z1 = (W0, W0 + 0.6) if zz > 0 else (-W0 - 0.6, -W0)
        mer(-8, z0, 8, z1, "x")
    # the stairs down from the walk into the ward, along the west wall's inner face (top at the corners)
    z.stairs(-W0, 3, -W0 + 2.5, W0, WALK, "z", 1)


def keep(m, z):
    """the keep: a great hall on the ground (open to the ward on the west, a door east to the bailey), the first
    floor at the walk's level (arrow slits over the hill, a balcony out over it, doors onto the walks), the roof
    terrace at 26 m: two spiral stairs in the east corners, a ladder between them"""
    x0, x1, zz = KX0, KX1, WARD + IW     # z -20..20
    ix0, ix1, iz = x0 + KT, x1 - KT, zz - KT
    # the hall floor, cut round the hearth (it stands from -1: a floor face it overlapped drew as a hole, seen
    # from the east door) and in 10 m tiles
    for (a0, b0, a1, b1) in tiles(minus((ix0, -iz, ix1, iz), [(26, -3.5, 28.5, 3.5)]), 10):
        m.box(a0, -1, b0, a1, 0, b1, mat="wideplanks")
    # west wall: the great hall's opening (z -6..6, 7 m), the first floor's slits and the balcony door
    z.box(x0, -1, 6, ix0, WALK, iz)
    m.box(x0, 7, -6, ix0, WALK, 6)
    z.box(x0, WALK + 3, WARD, ix0, ROOF, iz)          # over the door from the inner wall-walk (z 16..18.5)
    slits = [(-14, -12.8), (-10, -8.8), (-6, -4.8), (4.8, 6), (8.8, 10), (12.8, 14)]
    edges = [-WARD] + [v for s_ in slits for v in s_] + [WARD]
    for (a0, a1) in zip(edges[0::2], edges[1::2]):
        if a0 < -2 < a1:   # (the balcony door, z -2..2)
            m.box(x0, WALK, a0, ix0, ROOF, -2); m.box(x0, WALK + 3, -2, ix0, ROOF, 2); m.box(x0, WALK, 2, ix0, ROOF, a1)
        else: m.box(x0, WALK, a0, ix0, ROOF, a1)
    for (a0, a1) in slits:
        m.box(x0, WALK, a0, ix0, WALK + 0.9, a1)
        m.box(x0, WALK + 3.6, a0, ix0, ROOF, a1)
        m.box(x0 + 0.7, WALK + 0.9, a0, x0 + 0.825, WALK + 3.6, a1, mat="wallbang")      # the shutter: 1/8 m, shot through
    m.box(x0, -1, -zz, ix0, ROOF, -iz); m.box(x0, -1, iz, ix0, ROOF, zz)                 # (the corners)
    # north / south walls (a door onto each inner wall-walk at the west end, z 16..18.5, 3 m)
    z.box(ix0, -1, iz, x1, ROOF, zz)
    z.box(x0, -1, WARD, ix0 + 0.01, 0, iz)                                             # (the walk door's sill)
    m.box(x0, -1, -6, ix0, 0, 6, mat="wideplanks"); m.box(ix1, -1, -2, x1, 0, 2, mat="wideplanks")
    # east wall, the door (z -2..2, 4 m)
    m.box(ix1, -1, -iz, x1, ROOF, -2); m.box(ix1, -1, 2, x1, ROOF, iz); m.box(ix1, 4, -2, x1, ROOF, 2)
    # the stair towers (interior 10 x 10 round a 4 x 4 column, in the east corners) and the first floor round them
    T0x, T1x, T0z, T1z = ix1 - 10, ix1, iz - 10, iz      # interior of the north tower
    m.box(ix0, WALK - 0.5, -T0z + 0.5, T0x - 0.5, WALK, T0z - 0.5, mat="wideplanks")
    z.box(ix0, WALK - 0.5, T0z - 0.5, T0x - 0.5, WALK, iz, mat="wideplanks")
    m.box(T0x - 0.5, WALK - 0.5, -T0z + 0.5, ix1, WALK, T0z - 0.5, mat="wideplanks")
    spiral(m, z, T0x, T0z, T1x, T1z)
    # the balcony: out over the hill at the walk's level (x 12..16, z -7..7), a low wall at its edge
    m.box(12, WALK - 0.5, -7, x0, WALK, 7, mat="wideplanks")
    m.box(12, WALK, -7, 12.4, WALK + 1.0, -3, mat="castlestone"); m.box(12, WALK, 3, 12.4, WALK + 1.0, 7, mat="castlestone")
    for zc in (-6, 6): m.box(12.2, -1, zc - 0.4, 13.0, WALK - 0.5, zc + 0.4, mat="charredtimber")   # its corbel posts
    # the roof terrace at 26 m: holes over the two stairwells and the ladder's hatch, merlons round the edge
    hatch = (ix1 - 1.6, -0.75, ix1, 0.75)
    for (a0, b0, a1, b1) in minus((x0, -zz, x1, zz), [(T0x, T0z, T1x, T1z), (T0x, -T1z, T1x, -T0z), hatch]):
        m.box(a0, ROOF - 0.5, b0, a1, ROOF, b1, mat="castlestone")
    k = x0
    while k < x1 - 0.01:
        z.box(k, ROOF, zz - 0.7, min(k + 1.5, x1), ROOF + 1.4, zz); k += 3
    for (xa, xb) in ((x0, x0 + 0.7), (x1 - 0.7, x1)):
        k = -zz
        while k < zz - 0.01:
            m.box(xa, ROOF, k, xb, ROOF + 1.4, min(k + 1.5, zz)); k += 3
    m.ladder(ix1 - 0.12, 0, WALK, ROOF, "e")
    m.box(x1 - 0.7, ROOF, -1.5, x1, ROOF + 3.2, 1.5)                                   # (the parapet behind the hatch: stops you on the roof)
    # the hearth in the great hall: a solid chimney breast between the east door and the hall's opening (no line
    # from the east bridge across the hill)
    m.box(26, -1, -3.5, 28.5, 4.0, 3.5, mat="largecastlestone")
    m.box(26.4, 4.0, -2.0, 28.1, WALK - 0.5, 2.0, mat="castlestone")
    m.box(26.0, 0.0, -1.2, 26.05, 1.2, 1.2, mat="lightblock")
    # inside: lamps in the hall and on the first floor
    for (lx, lz) in ((24, 0), (34, 0)):
        m.light(lx, 6.5, lz, 12, 0.7, fixture=False)
        m.light(lx, WALK + 5, lz, 12, 0.6, fixture=False)
    z.light(24, 6.5, 12, 10, 0.6)


def spiral(m, z, X0, Z0, X1, Z1):
    """a square spiral stair in the north tower (mirrored to the south one): 20 cells a lap (a 3 x 3 corner, four
    1 x 3 treads a side), each 0.5 m up, round a 4 x 4 column: from the hall floor to the roof (52 cells)"""
    cx, cz = (X0 + X1) / 2, (Z0 + Z1) / 2
    z.box(cx - 2, -1, cz - 2, cx + 2, ROOF + 1.2, cz + 2, mat="largecastlestone")       # the column
    # the cells round the ring, starting at the south-west corner going east along the south side
    cells = []
    sides = [((X0, Z0, X0 + 3, Z0 + 3), (1, 0)), ((X1 - 3, Z0, X1, Z0 + 3), (0, 1)), ((X1 - 3, Z1 - 3, X1, Z1), (-1, 0)), ((X0, Z1 - 3, X0 + 3, Z1), (0, -1))]
    for (corner, (dx, dz)) in sides:
        cells.append(corner)
        for k in range(4):
            if dx == 1: cells.append((X0 + 3 + k, Z0, X0 + 4 + k, Z0 + 3))
            elif dz == 1: cells.append((X1 - 3, Z0 + 3 + k, X1, Z0 + 4 + k))
            elif dx == -1: cells.append((X1 - 4 - k, Z1 - 3, X1 - 3 - k, Z1))
            else: cells.append((X0, Z1 - 4 - k, X0 + 3, Z1 - 3 - k))
    n = int(ROOF / 0.5)
    exits = {}
    for k in range(n):
        h = 0.5 * (k + 1)
        a0, b0, a1, b1 = cells[k % 20]
        z.box(a0, h - 0.5, b0, a1, h, b1, mat="largemossystoneblocks")
        exits[h] = cells[k % 20]
    # the tower's walls (its west and south sides: the keep's walls make the others), doors: at the bottom (into the
    # hall, by the first cell) and at the first floor (the cell at 9 m)
    w = 0.5
    def wall_with_doors(xa, za, xb, zb, doors, along):
        a0, a1 = (xa, xb) if along == "x" else (za, zb)
        cuts = sorted(doors)
        pos = a0
        for (c0, c1, y0, y1) in cuts:
            if c0 > pos: z.box(*((pos, -1, za, c0, ROOF, zb) if along == "x" else (xa, -1, pos, xb, ROOF, c0)))
            z.box(*((c0, -1, za, c1, y0, zb) if along == "x" else (xa, -1, c0, xb, y0, c1)))
            z.box(*((c0, y1, za, c1, ROOF, zb) if along == "x" else (xa, y1, c0, xb, ROOF, c1)))
            pos = c1
        if pos < a1: z.box(*((pos, -1, za, a1, ROOF, zb) if along == "x" else (xa, -1, pos, xb, ROOF, a1)))
    c9 = exits[WALK]
    doors_s, doors_w = [(X0 + 0.5, X0 + 3, 0, 3.0)], []
    if abs(c9[1] - Z0) < 1e-6: doors_s.append((c9[0], c9[2], WALK, WALK + 3.0))       # (the cell at 9 m is on the south side)
    elif abs(c9[0] - X0) < 1e-6: doors_w.append((c9[1] - 1, c9[3] + 1, WALK, WALK + 3.0))   # (three treads wide: 3 m)
    wall_with_doors(X0, Z0 - w, X1, Z0, [(a, b, 0.0 if y == 0 else y, yy) for (a, b, y, yy) in doors_s], "x")
    wall_with_doors(X0 - w, Z0 - w, X0, Z1, doors_w, "z")
    z.light(cx, 4, Z0 + 1.5, 7, 0.5); z.light(cx, 15, Z0 + 1.5, 7, 0.5)


def bailey(m, z):
    """the outer bailey: stables (thatch) and a smithy each side, haybales and crates as cover near the gates, the
    undercroft's stairwell in the west bailey"""
    # the undercroft: stairs down from the west bailey (x -66..-53), a tunnel east under the moat and the west wall to
    # the hatch by the well (its ladder up the tunnel's end wall)
    TY0, TY1 = -6.5, -2.5
    m.stairs(-58, -2, -45, 2, 0, "x", -1, base=TY0)
    m.box(-45, TY0 - 1, -2, -9.8, TY0, 2, mat="largemossystoneblocks")                  # its floor
    z.box(-58, TY0 - 1, 2, -45, -1, 3, mat="largemossystoneblocks")                    # the stairwell's walls
    z.box(-45, TY0 - 1, 2, -8.8, TY1 + 0.5, 3, mat="largemossystoneblocks")            # the tunnel's
    m.box(-45, TY1, -2, -11.2, TY1 + 0.5, 2, mat="largemossystoneblocks")             # its vault
    m.box(-11.2, TY1, -2, -9.8, -1, -0.7, mat="largemossystoneblocks"); m.box(-11.2, TY1, 0.7, -9.8, -1, 2, mat="largemossystoneblocks")
    m.box(-12.2, TY1, -0.7, -11.2, -1, 0.7, mat="largemossystoneblocks")             # the shaft's west side
    m.box(-9.8, TY0 - 1, -2, -8.8, -1, 2, mat="largemossystoneblocks")               # the end wall
    m.ladder(-9.8 - 0.12, 0, TY0, 0, "e")
    for x in range(-42, -12, 10): m.light(x, TY1 - 0.6, 0, 8, 0.7, fixture=False)
    m.light(-52, -3, 0, 7, 0.6, fixture=False)
    # the chapel (west bailey, on the axis, in front of the postern's bridge): a nave the postern lane runs through
    # (doors west and east, 4 m), stained glass windows north and south, a gable roof
    CH0, CH1, CHZ, CHH = -41, -29.5, 6, 7.5
    for (wx0, wx1) in ((CH0, CH0 + 0.6), (CH1 - 0.6, CH1)):
        z.box(wx0, 0, 2, wx1, CHH, CHZ)
        m.box(wx0, 4.5, -2, wx1, CHH, 2, mat="castlestone")
    wins = [(-38.5, -36.9), (-34.3, -32.7)]
    edges = [CH0] + [v for w_ in wins for v in w_] + [CH1]
    for (a0, a1) in zip(edges[0::2], edges[1::2]):
        z.box(a0, 0, CHZ - 0.6, a1, CHH, CHZ)
    for (a0, a1) in wins:
        z.box(a0, 0, CHZ - 0.6, a1, 2.0, CHZ); z.box(a0, 5.5, CHZ - 0.6, a1, CHH, CHZ)
        z.box(a0, 2.0, CHZ - 0.4, a1, 5.5, CHZ - 0.2, mat="stainedglass")
    z.box(CH0 - 0.3, CHH, 0, CH1 + 0.3, CHH + 0.3, CHZ + 0.3, mat="wideplanks")
    z.wedge("roofshingles", 2, (CH0 - 0.3, CHH + 0.3, -CHZ - 0.3, CH1 + 0.3, CHH + 3.3, 0))
    for (gx0, gx1) in ((CH0, CH0 + 0.6), (CH1 - 0.6, CH1)):                          # the gables
        z.wedge("castlestone", 2, (gx0, CHH + 0.3, -CHZ, gx1, CHH + 3.0, 0))
    m.light((CH0 + CH1) / 2, 5.5, 0, 9, 0.7, fixture=False)
    # stables (west, each half): open-fronted, a thatch roof, stalls inside
    z.box(-64, -1, 50, -42, 4.5, 51, mat="logcabin")                                  # back wall
    for x in (-64, -56.5, -49, -42.6):
        z.box(x, -1, 42, x + 0.6, 4.5, 50, mat="logcabin")                            # stall walls / ends
    z.box(-64.5, 4.5, 41.5, -41.5, 4.9, 51.5, mat="wideplanks")
    z.wedge("thatch", 2, (-64.5, 4.9, 41.5, -41.5, 7.2, 51.5))
    # a smithy (east, each half): a stone hut, a door on each side
    z.box(54, -1, 40, 66, 5, 40.6, mat="castlestone"); z.box(54, -1, 51.4, 66, 5, 52, mat="castlestone")
    z.box(54, -1, 40.6, 54.6, 5, 44, mat="castlestone"); z.box(54, -1, 48, 54.6, 5, 51.4, mat="castlestone")
    z.box(65.4, -1, 40.6, 66, 5, 44, mat="castlestone"); z.box(65.4, -1, 48, 66, 5, 51.4, mat="castlestone")
    z.box(54, 3, 44, 54.6, 5, 48, mat="castlestone"); z.box(65.4, 3, 44, 66, 5, 48, mat="castlestone")
    z.box(53.6, 5, 39.6, 66.4, 5.4, 52.4, mat="wideplanks")
    z.wedge("roofshingles", 3, (53.6, 5.4, 39.6, 66.4, 7.4, 52.4))
    z.light(60, 3.5, 46, 7, 0.6)
    # a long barn (west bailey, each half, near the inner block): two doors, a loft roof
    z.box(-48, -1, 22, -36, 5, 22.6, mat="castlestone"); z.box(-48, -1, 33.4, -36, 5, 34, mat="castlestone")
    z.box(-48, -1, 22.6, -47.4, 5, 34, mat="castlestone"); z.box(-36.6, -1, 22.6, -36, 5, 25, mat="castlestone")
    z.box(-36.6, -1, 31, -36, 5, 33.4, mat="castlestone"); z.box(-36.6, 3.2, 25, -36, 5, 31, mat="castlestone")
    z.box(-48.4, 5, 21.6, -35.6, 5.4, 34.4, mat="wideplanks")
    z.wedge("roofshingles", 1, (-48.4, 5.4, 21.6, -42, 7.6, 34.4)); z.wedge("roofshingles", 0, (-42, 5.4, 21.6, -35.6, 7.6, 34.4))


def house(z, x0, z0, x1, z1, h, doors, roof="gable", wall="castlestone", ladder=None):
    """a bailey house, mirrored to the other half: 0.5 m walls, doors = [(side, centre, width)] (side: zlo zhi xlo
    xhi), 2.8 m doorways; a gable of shingles (along x) or a flat roof with a parapet (ladder = (x, z, face) up the
    outside onto it)"""
    t, dh = 0.5, 2.8
    walls = {"zlo": (x0, z0, x1, z0 + t, "x"), "zhi": (x0, z1 - t, x1, z1, "x"), "xlo": (x0, z0 + t, x0 + t, z1 - t, "z"), "xhi": (x1 - t, z0 + t, x1, z1 - t, "z")}
    for side, (a, b, c, d, ax) in walls.items():
        cuts = sorted((cc - w / 2, cc + w / 2) for (sd, cc, w) in doors if sd == side)
        lo, hi = (a, c) if ax == "x" else (b, d)
        pos = lo
        for (c0, c1) in cuts + [(hi, hi)]:
            if c0 > pos: z.box(*((pos, -1, b, c0, h, d) if ax == "x" else (a, -1, pos, c, h, c0)), mat=wall)
            if c1 > c0: z.box(*((c0, dh, b, c1, h, d) if ax == "x" else (a, dh, c0, c, h, c1)), mat=wall)
            pos = max(pos, c1)
    z.box(x0 + t, -1, z0 + t, x1 - t, 0, z1 - t, mat="wideplanks")
    if roof == "gable":
        zm = (z0 + z1) / 2
        z.box(x0 - 0.3, h, z0 - 0.3, x1 + 0.3, h + 0.3, z1 + 0.3, mat="wideplanks")
        z.wedge("roofshingles", 2, (x0 - 0.3, h + 0.3, z0 - 0.3, x1 + 0.3, h + 2.4, zm))
        z.wedge("roofshingles", 3, (x0 - 0.3, h + 0.3, zm, x1 + 0.3, h + 2.4, z1 + 0.3))
    else:
        z.box(x0, h, z0, x1, h + 0.4, z1, mat="wideplanks")   # (flush: the ladder comes up over its edge)
        for (a, b, c, d) in ((x0, z0, x1, z0 + 0.4), (x0, z1 - 0.4, x1, z1)):
            z.box(a, h + 0.4, b, c, h + 1.1, d, mat=wall)
        if ladder: z.ladder(ladder[0], ladder[1], 0, h + 0.4, ladder[2])
    z.light((x0 + x1) / 2, h - 0.8, (z0 + z1) / 2, max(x1 - x0, z1 - z0) * 0.7, 0.6)


def town(m, z):
    """the bailey's houses each side of the road from the gate to the inner gatehouse (x -5..5 kept clear): cover and
    rooftops between the barbicans and the ward, so the walk in is a fight, not a sniper's field"""
    house(z, -30, -64, -18, -54, 5, [("zhi", -24, 3), ("xhi", -59, 2.5)])
    house(z, -26, -48, -14, -39, 5.5, [("zlo", -20, 3), ("xhi", -43.5, 2.5), ("xlo", -43.5, 2.5)], roof="flat", ladder=(-26.12, -45, "e"))
    house(z, 12, -62, 28, -50, 6, [("xlo", -56, 3), ("zhi", 20, 3), ("xhi", -56, 2.5)])
    house(z, 30, -44, 40, -34, 5, [("zhi", 35, 2.5), ("xlo", -39, 2.5)], roof="flat", ladder=(40.12, -39, "w"))
    house(z, -50, -66, -40, -58, 4.5, [("zhi", -45, 2.5), ("xhi", -62, 2.5)], wall="logcabin")
    # the market hall standing in the road (x -6..6): the road forks round it, so the gate and the inner gatehouse
    # never line up (a pillar didn't break the line)
    z.box(-6, -1, -53, 6, 4.5, -46, mat="castlestone")
    z.box(-6.4, 4.5, -53.4, 6.4, 4.8, -45.6, mat="wideplanks")
    z.wedge("roofshingles", 2, (-6.4, 4.8, -53.4, 6.4, 7.0, -49.5)); z.wedge("roofshingles", 3, (-6.4, 4.8, -49.5, 6.4, 7.0, -45.6))
    for (x0, z0, x1, z1, h) in ((7, -45, 9.4, -42.6, 1.2), (-9.5, -60, -7, -57.5, 1.2), (8, -36, 10.4, -34.8, 1.2)):
        z.box(x0, 0, z0, x1, h, z1, mat="haybales")
    for (x0, z0) in ((44, -60), (-12, -33)):
        z.box(x0, 0, z0, x0 + 1.2, 1.2, z0 + 1.2, mat="woodencrate"); z.box(x0 + 1.3, 0, z0, x0 + 2.5, 1.2, z0 + 1.2, mat="woodencrate")


def objects(m, z):
    # the hill: the ward's middle, 24 x 24 m, 6 m up
    m.raw("hill -12 0 -12 12 6 12")
    # the barbicans' spawns (4 a side) facing the gate, out of each other's sight
    for x in (-6, -2, 2, 6):
        z.spawn("a", x, 0, -87, 180, "b")
    # free for all: eight round the bailey and the walks
    for (x, y, zz, yaw) in ((-46, 0, -12, -90), (72.5, OWALK, -30, 180), (-11, 0, -66, -90), (-72.5, OWALK, -12, 180)):
        z.spawn("any", x, y, zz, yaw, "any")
    # the courtyard's dressing that is also cover: the well (by the hatch), the market stall and the cart across
    # the gate-to-gate line, a low wall (crouch cover)
    m.box(-7.7, 0, -1.2, -5.3, 1.0, 1.2, mat="castlestone")                            # the well
    for wx in (-8.0, -5.2):                                                            # its house: walls to 2.4 m east and west (closed at eye height)
        m.box(wx, 0, -1.6, wx + 0.2, 2.4, 1.6, mat="castlestone")
    for (wx, wz) in ((-7.6, -1.1), (-5.6, -1.1), (-7.6, 0.9), (-5.6, 0.9)):
        m.box(wx, 1.0, wz, wx + 0.2, 3.2, wz + 0.2, mat="charredtimber")
    m.box(-8.0, 3.2, -1.5, -5.0, 3.5, 1.5, mat="roofshingles")
    z.box(-3.5, 0, 4.5, 3.5, 1.1, 6.0, mat="wideplanks")                               # the stalls' counters
    z.box(-3.5, 1.1, 5.5, 3.5, 2.6, 6.0, mat="plywoodboardup")                         # their shutters (dash through)
    for x in (-3.5, 3.2): z.box(x, 0, 4.5, x + 0.3, 3.0, 4.8, mat="charredtimber")
    z.box(-3.8, 3.0, 4.2, 3.8, 3.2, 6.3, mat="thatch")
    z.box(5, 0, -0.8, 9, 1.2, 0.8, mat="largemossystoneblocks", mirror=False)          # the low wall
    # a dais in the ward's middle (three 0.5 m tiers, 1.5 m): cappers aren't at the very bottom
    for (hx, hz, h) in ((4.5, 3.5, 0.5), (3.5, 2.5, 1.0), (2.5, 1.5, 1.5)):
        m.box(-hx, 0, -hz, hx, h, hz, mat="largemossystoneblocks")
    # light the courtyard's corners and the gate passages
    z.light(-12, 4, 12, 10, 0.6); z.light(12, 4, 12, 10, 0.6); z.light(0, 3.5, 18, 7, 0.6)
    m.light(-18, 2.8, 0, 6, 0.6, fixture=False)
    z.light(0, 5, 74, 8, 0.6)


def decorate(m, z):
    """sparing: on walls, roofs and corners, out of the lanes (trim is not solid: nothing to snag on)"""
    x0, x1, zz = KX0, KX1, WARD + IW
    # the keep: corner turrets over the roof (in the roof's corners, out of the way), string courses, pilasters
    for (cx, cz, yb) in ((x0 + 2.2, zz - 2.2, ROOF), (x1 + 2, zz + 2, ROOF - 4)):   # (the east ones corbelled out past the stairwells)
        for c in (cz, -cz):
            cylinder(m, "castlestone", cx, c, 2.2 if yb == ROOF else 2.0, yb, ROOF + 4.5, sides=8)
            cylinder(m, "castlestone", cx, c, 2.6, ROOF + 4.5, ROOF + 5.1, sides=8)
            peak(m, "roofshingles", cx, c, 2.6, ROOF + 5.1, 3.5)
    for y in (WALK - 0.6, 18.0):
        m.box(x1, y, -zz, x1 + 0.2, y + 0.4, zz, mat="trim")
        z.box(x0 - 0.2, y, zz, x1 + 0.2, y + 0.4, zz + 0.2, mat="trim")
        m.box(x0 - 0.2, y, -zz, x0, y + 0.4, -7.5, mat="trim"); m.box(x0 - 0.2, y, 7.5, x0, y + 0.4, zz, mat="trim")
    for k in (-12, -4, 4, 12):
        m.box(x1, 0, k - 0.6, x1 + 0.25, ROOF, k + 0.6, mat="trim")
    for k in (22, 30, 38):
        z.box(k - 0.6, 0, zz, k + 0.6, ROOF, zz + 0.25, mat="trim")
    # banners: on the keep's west face either side of the balcony, on the gatehouse towers, on the barbicans' gate towers
    for (bz0, bz1) in ((-12, -9.5), (9.5, 12)):
        m.box(x0 - 0.12, 13, bz0, x0, 22, bz1, mat="trim")
    z.box(-6.5, 6, WARD + IW + 3, -4.5, 12.5, WARD + IW + 3.12, mat="trim")
    z.box(4.5, 6, WARD + IW + 3, 6.5, 12.5, WARD + IW + 3.12, mat="trim")
    z.box(-14, 6, CZ - 2.12, -12, 12.5, CZ - 2, mat="trim")
    z.box(12, 6, CZ - 2.12, 14, 12.5, CZ - 2, mat="trim")
    # braziers (light blocks) by the gates and on the keep's roof corners, torches in the ward
    for (bx, by, bz) in ((-4.5, 0, WARD + IW + MOAT + 1), (4.5, 0, WARD + IW + MOAT + 1), (-7, 0, CZ - 6), (7, 0, CZ - 6)):
        z.box(bx - 0.4, by, bz - 0.4, bx + 0.4, by + 0.9, bz + 0.4, mat="charredtimber")
        z.box(bx - 0.35, by + 0.9, bz - 0.35, bx + 0.35, by + 1.3, bz + 0.35, mat="lightblock")
    for (tx, tz) in ((-15.8, 2.6), (-4, 15.8), (4, 15.8)):
        z.box(tx - 0.15, 3.6, tz - 0.15, tx + 0.15, 4.2, tz + 0.15, mat="lightblock")
    # the ward: a hand cart and barrels by the north and south walls (out of the gate lanes)
    z.box(7, 0.5, 8.4, 10.5, 1.3, 10.4, mat="wideplanks")                               # (on the hill: cover for cappers)
    z.box(7.4, 0, 8.2, 8.2, 0.9, 8.4, mat="charredtimber"); z.box(7.4, 0, 10.4, 8.2, 0.9, 10.6, mat="charredtimber")
    z.box(10.5, 0.9, 9.2, 12.0, 1.0, 9.6, mat="charredtimber")
    for (bx, bz) in ((-12.5, 14.8), (-11.4, 14.8)):
        z.box(bx - 0.45, 0, bz - 0.45, bx + 0.45, 1.1, bz + 0.45, mat="woodencrate")
    # out past the walls: far hills, a valley floor
    for (hx, hz, r, h) in ((-200, 0, 90, 55), (210, 0, 100, 70), (60, 240, 110, 60), (-140, 220, 90, 45), (60, -240, 110, 60), (-140, -220, 90, 45)):
        peak(m, "far", hx, hz, r, -12, h)
    m.box(-500, -12, -500, 500, -9, 500, mat="far")                                    # (under the undercroft: it looked plugged)
