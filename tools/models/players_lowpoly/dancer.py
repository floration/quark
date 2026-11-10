"""DANCER: the ballerina (box design: characters_outlined_extra2.dancer)."""
import math

from players_lowpoly.kit import Char, Rig, body, band, plate, profile, ngon, hexring, offset, shift, rotz, BACK_WRAP

DESC = "the ballerina: a wide flat team tutu, a team bodice, pink tights and ballet shoes, a hair bun with a gold tiara"


def frill(r_out, r_in, n, cz=0.0):
    """a star round the hips (x, z): n points out at r_out, n notches at r_in: the tutu's layered edge"""
    return [(r * math.cos(math.pi * k / n + math.pi / (2 * n)), cz + r * math.sin(math.pi * k / n + math.pi / (2 * n)))
            for k, r in ((k, r_out if k % 2 == 0 else r_in) for k in range(2 * n))]


def build():
    ch = Char("DANCER", DESC, Rig(hip=900, hipx=130, knee=470, ankle=75))
    b = body(ch, legs="tights", feet="pink", pelvis="team", torso="team", head="tights",
             foot=(56, 150, 66), thigh_mid=False, calf=False, pelvis_w=(150, 175), pelvis_d=(105, 112),
             waist=(150, 100), chest=(180, 118), shoulder=(175, 104), chest_y=1260,
             head_w=148, head_d=150, head_top=1752, jaw=0.74, crown=0.78)
    # the ballet shoes' team ribbons, laced up round the ankles
    b.wrap("shin_", 70, 190, "team", grow=5, shell=False)
    # the tutu: a wide flat star of tulle round the hips, team on top, a white layer under it
    ch.loft("pelvis", "team", frill(372, 318, 6), 872, ngon(0, 0, 150, 12, sy=0.8), 936, "y")
    ch.loft("pelvis", "white", ngon(0, 0, 335, 8), 848, ngon(0, 0, 150, 8, sy=0.8), 874, "y", shell=False)
    # the bodice: bare shoulders over it, a white waist band, a team front point down over the tutu
    b.wrap("torso", 1335, 1418, "tights", grow=4, shell=False)
    b.wrap("torso", 940, 990, "white", grow=6, shell=False)
    # the bare shoulders: a faceted cap each side, tipped out
    def shoulder(sd, _):
        with ch.placed("torso", rotz(-sd * 0.35), (sd * 245, 1290, 0)):
            ch.loft("torso", "tights", hexring(70, 80, sd * 245), 1250, shift(hexring(44, 56, sd * 245), -sd * 18), 1420 - ch.outline - 6, "y")
    ch.sym(shoulder)
    # the eyes
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 28, 1592), (sd * 62, 1592), (sd * 62, 1608), (sd * 28, 1608)], -156, -146))
    # the hair: swept back over the crown, down the back of the head
    hs = b.seg["head"]
    ch.loft("head", "hair", offset(hs.at(1655), 9), 1655, offset(hs.rings[-1][0], 8), hs.rings[-1][1] + 8, "y")
    sg = [(offset(hs.at(w), 8), w) for w in (1550, 1660)]
    ch.sweep("head", "hair", [([p[i] for i in BACK_WRAP] + [q[i] for i in reversed(BACK_WRAP)], w) for (p, w), (q, _) in
                              zip(sg, [(offset(hs.at(w), -10), w) for w in (1550, 1660)])], "y", shell=False)
    # the bun on the crown, a team ribbon round it
    ch.loft("head", "hair", ngon(0, 40, 74, 6), 1728, ngon(0, 40, 46, 6), 1812, "y")
    ch.loft("head", "team", ngon(0, 40, 74, 6), 1736, ngon(0, 40, 70, 6), 1756, "y", shell=False)
    # the gold tiara over the brow, its peak in the middle
    profile(ch, "head", "gold", [(-166, 1688), (-136, 1688), (-136, 1724), (-164, 1720)], -70, 70, shell=False)
    plate(ch, "head", "gold", [(-26, 1712), (26, 1712), (0, 1756)], -170, -150)
    return ch
