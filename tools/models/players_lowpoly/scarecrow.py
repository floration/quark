"""SCARECROW: the scarecrow (box design: characters_outlined_extra2.scarecrow)."""
import math
from players_lowpoly.kit import Char, Rig, Segment, body, plate, ring, ngon, rotz

DESC = "the scarecrow: a wide floppy straw hat, a stitched burlap sack head, a patched team shirt hung on a pole across the back, straw tufts at every cuff"


def tuft(n, r0, r1, cx=0.0, cz=0.0, sz=1.0, turn=0.0):
    """a jagged straw ring (x, z): n points out at r1, n notches in at r0"""
    out = []
    for i in range(2 * n):
        a = turn + math.pi * i / n
        r = r1 if i % 2 == 0 else r0
        out.append((cx + r * math.cos(a), cz + r * math.sin(a) * sz))
    return out


def build():
    ch = Char("SCARECROW", DESC, Rig(hip=900, hipx=130, knee=470, ankle=70))
    b = body(ch, legs="denim", shins="denim", feet="leather", pelvis="denim", torso="team", head=None, calf=False, thigh_mid=False,
             thigh=(76, 74, 68, 72), shin=(68, 72, 70, 68), foot=(66, 180, 82), pelvis_w=(170, 192), pelvis_d=(115, 122),
             waist=(192, 118), chest=(222, 132), shoulder=(202, 116), chest_y=1250)
    # the team knee patches, the straw sticking out of the trouser cuffs
    ch.sym(lambda sd, s: plate(ch, "thigh" + s, "team", [(sd * 130 - 50, 520), (sd * 130 + 50, 520), (sd * 130 + 46, 640), (sd * 130 - 46, 640)], -74, -58))
    ch.sym(lambda sd, s: ch.loft("shin" + s, "straw", tuft(3, 84, 116, sd * 130, 4), 66, tuft(3, 80, 92, sd * 130, 4, turn=0.5), 190, "y"))
    # the shirt's patches, the straw at the waist
    plate(ch, "torso", "burlap", [(-150, 1030), (-50, 1030), (-50, 1130), (-150, 1130)], -136, -122)
    plate(ch, "torso", "denim", [(50, 1200), (140, 1200), (140, 1280), (50, 1280)], -152, -138)
    b.wrap("torso", 905, 965, "straw", grow=14, shell=False)
    # the pole across the back the shirt hangs on
    ch.rod("torso", "hat", (-372, 1300, 168), (372, 1300, 168), 26, k=4)
    # the team sleeves hanging off the pole, straw at their cuffs
    def sleeve(sd, _):
        cx = sd * 282
        with ch.placed("torso", rotz(-sd * 0.12), (cx, 1418, 0)):
            ch.loft("torso", "team", [(cx - 74, -100), (cx + 74, -100), (cx + 74, 100), (cx - 74, 100)], 1170,
                    [(cx - 64, -90), (cx + 64, -90), (cx + 64, 90), (cx - 64, 90)], 1408, "y")
        ch.loft("torso", "straw", tuft(3, 50, 74, sd * 312, 0), 1060, tuft(3, 54, 66, sd * 300, 0, turn=0.5), 1170, "y")
    Char.sym(sleeve)
    # the burlap sack head, tied at the neck with a leather cord; its top hidden by the hat
    hs = Segment(ch, "head", [(ring(110, 110), 1432), (ring(150, 148), 1540), (ring(136, 134), 1660)])
    ch.sweep("head", "burlap", hs.rings)
    hs.wrap(1436, 1480, "leather", grow=8, shell=False)
    ch.sym(lambda sd, s: plate(ch, "head", "eye", ngon(sd * 56, 1592, 22, 4, phase=0), -150, -140))
    plate(ch, "head", "eye", [(-64, 1504), (64, 1504), (64, 1514), (-64, 1514)], -150, -138)
    # the wide floppy straw hat: the brim sags to its edge, a team band round the crown
    ch.loft("head", "straw", ngon(0, 0, 187, 8), 1630, ngon(0, 0, 118, 8), 1676, "y")
    ch.loft("head", "straw", ring(112, 106), 1668, ring(94, 90), 1760, "y")
    ch.loft("head", "team", ring(118, 112), 1672, ring(112, 106), 1710, "y", shell=False)
    return ch
