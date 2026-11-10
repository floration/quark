"""YETI: the yeti (box design: characters_outlined_extra2.yeti)."""
import math
from players_lowpoly.kit import Char, Rig, Segment, body, visor, band, plate, ring, hexring, shift, rotz, BACK_WRAP

DESC = "the yeti: a huge hunched frost-white fur body with shaggy tufts, a big slate face with curling ivory horns, team scarf, shorts and wrist cuffs"


def shag(n, r0, r1, cx=0.0, cz=0.0, sz=1.0, turn=0.0):
    """a jagged fur ring (x, z): n points out at r1, n notches in at r0"""
    out = []
    for i in range(2 * n):
        a = turn + math.pi * i / n
        r = r1 if i % 2 == 0 else r0
        out.append((cx + r * math.cos(a), cz + r * math.sin(a) * sz))
    return out


def build():
    ch = Char("YETI", DESC, Rig(hip=880, hipx=140, knee=460, ankle=90))
    # the shorts are team over the whole thigh; frost fur from the knee down
    b = body(ch, legs="team", shins="frost", feet="frost", pelvis="team", torso="frost", head=None, calf=False, thigh_mid=False,
             thigh=(96, 92, 86, 88), shin=(86, 88, 90, 80), foot=(92, 215, 105), pelvis_w=(205, 240), pelvis_d=(140, 150),
             waist=(240, 150), chest=(272, 168), shoulder=(238, 150), chest_y=1180)
    # the slate toes
    ch.sym(lambda sd, s: ch.solid("foot" + s, "slate", [(-226, 10), (-170, 10), (-170, 60), (-212, 44)], "x", sd * 140 - 84, sd * 140 + 84, shell=False))
    # the shaggy ankles: a jagged frost ruff over each foot
    ch.sym(lambda sd, s: ch.loft("shin" + s, "frost", shag(3, 96, 128, sd * 140, 4), 70, shag(3, 84, 100, sd * 140, 4, turn=0.5), 210, "y"))
    # the team belt riding up over the waist
    b.wrap("torso", 900, 1050, "team", grow=10, shell=False)
    # the hump on the back, the head sunk low into it
    ch.loft("torso", "frost", [(-200, 120), (200, 120), (170, 250), (-170, 250)], 1100, [(-170, 120), (170, 120), (130, 215), (-130, 215)], 1428, "y")
    # the huge shoulders with team wrist cuffs under them
    def shoulder(sd, _):
        cx = sd * 300
        with ch.placed("torso", rotz(-sd * 0.2), (cx, 1120, 0)):
            ch.loft("torso", "frost", hexring(100, 118, cx), 1120, shift(hexring(74, 96, cx), -sd * 26), 1418 - ch.outline, "y")
            ch.loft("torso", "team", hexring(100, 116, cx), 1010, hexring(106, 122, cx), 1140, "y", shell=False)
    Char.sym(shoulder)
    # the team scarf, one end down the front and one down the back
    ch.loft("torso", "team", ring(222, 176, front=184), 1280, ring(190, 154, front=164), 1428, "y")
    plate(ch, "torso", "team", [(40, 1110), (120, 1110), (126, 1330), (46, 1330)], -186, -168)
    plate(ch, "torso", "team", [(-150, 1150), (-60, 1150), (-56, 1330), (-146, 1330)], 254, 268)
    # the head: low and forward, a frost brow shag, a big slate face
    hs = Segment(ch, "head", [(ring(140, 140), 1430), (ring(164, 150), 1560), (ring(118, 112), 1726)])
    ch.sweep("head", "frost", hs.rings)
    b.seg["head"] = hs
    visor(ch, b, "slate", 1446, 1626, grow=5, depth=12, wrap=False)
    b.wrap("head", 1596, 1652, "frost", grow=12, shell=False)                            # the brow shag
    ch.sym(lambda sd, s: plate(ch, "head", "visor", [(sd * 26, 1578), (sd * 66, 1578), (sd * 62, 1600), (sd * 30, 1600)], -164, -156))
    ch.sym(lambda sd, s: plate(ch, "head", "white", [(sd * 20, 1504), (sd * 40, 1504), (sd * 30, 1530)], -166, -156))
    # the curling ivory horns
    ch.sym(lambda sd, s: plate(ch, "head", "ivory", [(sd * x, y) for x, y in ((118, 1612), (170, 1656), (182, 1730), (148, 1796), (150, 1720), (136, 1668))], -26, 22, shell=True))
    return ch
