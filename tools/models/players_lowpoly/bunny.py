"""BUNNY: the bunny mascot (box design: characters_outlined_extra2.bunny)."""
from players_lowpoly.kit import Char, Rig, Segment, body, pauldron, band, plate, ring, hexring, ngon, rotz, FRONT, BACK

DESC = "the bunny mascot: tall upright ears with pink insides, a round grey-white head with buck teeth, team dungarees, a cotton tail, long feet"


def strip(ch, seg, t, y0, y1, xa, xb, grow=5.0, depth=12.0, back=False, shell=False):
    """a vertical strip (x from xa to xb) laid on the flat front (or back) face of a ring()-made piece: a strap"""
    ws = [y0] + [w for _, w in seg.rings if y0 < w < y1] + [y1]
    rings_ = []
    for w in ws:
        p = seg.at(w)
        if back:
            z = max(v for _, v in p)
            rings_.append(([(xa, z - depth), (xb, z - depth), (xb, z + grow), (xa, z + grow)], w))
        else:
            z = min(v for _, v in p)
            rings_.append(([(xa, z - grow), (xb, z - grow), (xb, z + depth), (xa, z + depth)], w))
    ch.sweep(seg.bone, t, rings_, "y", shell)


def shoulders(ch, t, x=284, y0=1236, y1=1408, w=80, d=94, tilt=0.18):
    """a rounded shoulder each side: one tapered hexagonal block (cheaper than kit.pauldron's three rings)"""
    def one(sd, _):
        cx = sd * x
        with ch.placed("torso", rotz(-sd * tilt), (cx, y0, 0)):
            ch.loft("torso", t, hexring(w, d, cx), y0, hexring(w * 0.7, d * 0.72, cx - sd * w * 0.2), y1, "y")
    ch.sym(one)


def build():
    ch = Char("BUNNY", DESC, Rig(hip=900, hipx=130, knee=450, ankle=60))
    b = body(ch, legs="team", shins="team", feet="pale", pelvis="team", torso="pale", head=None,
             foot=(70, 250, 95), calf=False, thigh_mid=False, thigh=(84, 84, 70, 72), waist=(195, 128), chest=(222, 140), shoulder=(196, 116), chest_y=1220)
    # the ankle fur and the toe pads of the long feet
    b.wrap("shin_", 50, 170, "pale", grow=10, shell=False)
    ch.sym(lambda sd, s: plate(ch, "foot" + s, "pink", [(sd * 130 - 50, 12), (sd * 130 + 50, 12), (sd * 130 + 44, 40), (sd * 130 - 44, 40)], -262, -240))
    # the dungarees: bib front and back, the straps over the shoulders
    tor = b.seg["torso"]
    band(ch, b, "torso", "team", 900, 1218, FRONT, grow=6, depth=16)
    band(ch, b, "torso", "team", 900, 1218, BACK, grow=6, depth=16)
    ch.sym(lambda sd, s: strip(ch, tor, "team", 1222, 1405, sd * 75, sd * 125, grow=6, depth=12))
    ch.sym(lambda sd, s: strip(ch, tor, "team", 1222, 1405, sd * 75, sd * 125, grow=6, depth=12, back=True))
    # the cotton tail on the seat
    ch.loft("pelvis", "white", ngon(0, 148, 46, 4, phase=0, sy=0.8), 845, ngon(0, 170, 28, 4, phase=0, sy=0.8), 940, "y")
    shoulders(ch, "pale")
    # the round head: a soft chin, full cheeks, a dome
    hs = Segment(ch, "head", [(ring(108, 102, c=58), 1430), (ring(160, 148, c=82), 1515), (ring(146, 136, c=74), 1610), (ring(84, 78, c=42), 1664)])
    ch.sweep("head", "pale", hs.rings)
    # the white muzzle, its pink nose, the buck teeth
    ch.loft("head", "white", hexring(80, 26, 0, -136), 1458, hexring(60, 20, 0, -146), 1548, "y")
    plate(ch, "head", "pink", [(-22, 1540), (22, 1540), (12, 1566), (-12, 1566)], -172, -156)
    plate(ch, "head", "white", [(-24, 1440), (24, 1440), (24, 1470), (-24, 1470)], -164, -140)
    # the eyes and the whiskers
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 36, 1566), (sd * 70, 1566), (sd * 68, 1606), (sd * 38, 1606)], -152, -138))

    # the tall ears, splayed a little, pink inside
    def ear(sd, s):
        x0, x1 = sd * 58, sd * 84
        ch.sweep("head", "pale", [(hexring(26, 18, x0), 1615), (hexring(38, 19, (x0 + x1) / 2), 1730), (hexring(15, 12, x1), 1803)])
        plate(ch, "head", "pink", [(x0, 1650), ((x0 + x1) / 2 + 22, 1728), (x1, 1790), ((x0 + x1) / 2 - 22, 1728)], -22, -14)
    ch.sym(ear)
    return ch
