"""MUSHROOM: the mushroom folk (box design: characters_outlined_extra2.mushroom)."""
from players_lowpoly.kit import Char, Rig, Segment, body, plate, ring, hexring, ngon, rotx, roty, rotz, matmul

DESC = "the mushroom folk: a wide red cap with white spots over a cream face, a team tunic and shorts, cream legs, brown shoes"

K = 8              # the cap's sides (a face straight to the front)


def apo(r):
    """the distance from the centre to a face of the cap's K-gon of radius r"""
    import math
    return r * math.cos(math.pi / K)


def shoulders(ch, t, x=282, y0=1236, y1=1408, w=80, d=94, tilt=0.18):
    """a rounded shoulder (a sleeve) each side: one tapered hexagonal block"""
    def one(sd, _):
        cx = sd * x
        with ch.placed("torso", rotz(-sd * tilt), (cx, y0, 0)):
            ch.loft("torso", t, hexring(w, d, cx), y0, hexring(w * 0.7, d * 0.72, cx - sd * w * 0.2), y1, "y")
    ch.sym(one)


def spot(ch, a, y0, y1, r0, r1, half):
    """a white spot on the cap's face that points at angle a (radians about y, 0 = the front, + toward the left),
    on the slope from (y0, r0) to (y1, r1), half wide"""
    import math
    z0, z1 = -apo(r0), -apo(r1)
    tilt = math.atan2(z1 - z0, y1 - y0)            # (the face leans back as it rises)
    L = math.hypot(z1 - z0, y1 - y0)
    ym, zm = (y0 + y1) / 2, (z0 + z1) / 2
    s = (0.0, ym, zm)
    R = matmul(roty(a), rotx(tilt))
    rs = roty(a)
    sw = tuple(sum(rs[i][k] * s[k] for k in range(3)) - s[i] for i in range(3))
    h = min(half, L * 0.42)
    with ch.placed("head", R, s, sw):
        plate(ch, "head", "white", [(-half, ym), (-half * 0.5, ym - h), (half * 0.5, ym - h), (half, ym), (half * 0.5, ym + h), (-half * 0.5, ym + h)], zm - 4, zm + 5)


def build():
    ch = Char("MUSHROOM", DESC, Rig(hip=900, hipx=130, knee=450, ankle=80))
    b = body(ch, legs="cream", shins="cream", feet="leather", pelvis="team", torso="team", head=None,
             thigh_mid=False, calf=False, foot=(68, 190, 90), thigh=(84, 84, 68, 72), shin=(66, 70, 70, 67),
             waist=(200, 124), chest=(222, 138), shoulder=(200, 116), pelvis_w=(175, 210))
    # the shorts, round the top of each thigh
    b.wrap("thigh_", 640, 930, "team", grow=8, shell=False)
    # the belt and its gold buckle
    b.wrap("torso", 975, 1035, "leather", grow=8, shell=False)
    plate(ch, "torso", "gold", [(-32, 985), (32, 985), (32, 1025), (-32, 1025)], -150, -134)
    shoulders(ch, "team")
    # the frilled gill collar
    ch.loft("torso", "cream", hexring(140, 112), 1380, hexring(100, 86), 1442, "y")
    # the face: a small cream head under the cap
    hs = Segment(ch, "head", [(ring(62, 60, c=28), 1430), (ring(98, 94, c=44), 1478), (ring(100, 96, c=46), 1600)])
    ch.sweep("head", "cream", hs.rings)
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 26, 1536), (sd * 50, 1536), (sd * 50, 1578), (sd * 26, 1578)], -106, -92))
    plate(ch, "head", "eye", [(-24, 1500), (24, 1500), (16, 1512), (-16, 1512)], -106, -92)
    # the wide red cap: its cream gills underneath, a white spot all round
    cap = [(1592, 96), (1616, 184), (1680, 176), (1746, 122), (1782, 60)]
    ch.sweep("head", "capred", [(ngon(0, 0, r, K), y) for y, r in cap])
    ch.loft("head", "cream", ngon(0, 0, 50, K), 1584, ngon(0, 0, 174, K), 1613, "y", shell=False)
    import math
    q = math.pi / 4
    spot(ch, 0.0, 1680, 1746, 176, 122, 40)          # front, on the dome
    spot(ch, -q, 1616, 1680, 184, 176, 30)           # front right, on the rim
    spot(ch, 2 * q, 1680, 1746, 176, 122, 34)        # left
    spot(ch, -2 * q, 1680, 1746, 176, 122, 30)       # right
    spot(ch, -4 * q, 1680, 1746, 176, 122, 40)       # back
    return ch
