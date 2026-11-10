"""SURFER: the surfer (box design: characters_outlined_extra2.surfer)."""
from players_lowpoly.kit import Char, Rig, body, visor, plate, profile, hexring, shift, offset, rotz, BACK_WRAP

DESC = "the surfer: a tall team surfboard on the back past the head, shaggy blond hair, shades, bare tan chest with a shell necklace, team board shorts"

BOARD = [(-66, 580), (66, 580), (168, 730), (200, 960), (188, 1210), (104, 1440), (68, 1640), (0, 1800), (-68, 1640), (-104, 1440),
         (-188, 1210), (-200, 960), (-168, 730)]


def build():
    ch = Char("SURFER", DESC, Rig(hip=900, hipx=130, knee=470, ankle=45))
    b = body(ch, legs="tan", feet="dark", pelvis="team", torso="tan", head="tan",
             thigh_mid=False, calf=False, foot=(62, 172, 80), pelvis_w=(170, 200), pelvis_d=(115, 124),
             waist=(185, 115), chest=(225, 128), shoulder=(200, 112), chest_y=1250,
             head_w=150, head_d=148, head_top=1730, jaw=0.78, crown=0.82)
    # the team board shorts, long and loose, a white stripe round each leg
    b.wrap("thigh_", 520, 940, "team", grow=16)
    b.wrap("thigh_", 610, 650, "white", grow=19, shell=False)
    # the shoulders, bare
    def shoulder(sd, _):
        with ch.placed("torso", rotz(-sd * 0.3), (sd * 286, 1290, 0)):
            ch.loft("torso", "tan", hexring(98, 100, sd * 286), 1195, shift(hexring(60, 72, sd * 286), -sd * 24), 1420 - ch.outline - 6, "y")
    ch.sym(shoulder)
    # the shell necklace: a white cord round the neck, a pink shell on it
    plate(ch, "torso", "white", [(-118, 1395), (-88, 1395), (0, 1296), (88, 1395), (118, 1395), (0, 1266)], -150, -138)
    plate(ch, "torso", "pink", [(0, 1290), (22, 1262), (0, 1232), (-22, 1262)], -156, -140)
    # the surfboard up the back, from the knees to over the head: a white stringer, a dark fin
    ch.solid("torso", "team", BOARD, "z", 120, 150)
    plate(ch, "torso", "white", [(-9, 600), (9, 600), (9, 1770), (-9, 1770)], 148, 153)
    profile(ch, "torso", "dark", [(150, 720), (150, 620), (204, 596)], -9, 9)
    # the face: shades, a grin
    visor(ch, b, "eye", 1570, 1604, grow=4, depth=12, wrap=False)
    plate(ch, "head", "white", [(-40, 1505), (40, 1505), (28, 1520), (-28, 1520)], -138, -126)
    # the shaggy blond hair: over the crown, down the back and sides, a ragged fringe
    hs = b.seg["head"]
    ch.loft("head", "straw", offset(hs.at(1620), 11), 1620, offset(hs.rings[-1][0], 10), hs.rings[-1][1] + 22, "y")
    sg = [((offset(hs.at(w), 10), offset(hs.at(w), -10)), w) for w in (1500, 1630)]
    ch.sweep("head", "straw", [([p[i] for i in BACK_WRAP] + [q[i] for i in reversed(BACK_WRAP)], w) for (p, q), w in sg], "y", shell=False)
    plate(ch, "head", "straw", [(-112, 1690), (112, 1690), (112, 1628), (84, 1608), (52, 1640), (12, 1604), (-30, 1640), (-70, 1608), (-112, 1630)], -160, -144)
    return ch
