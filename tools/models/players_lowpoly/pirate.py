"""PIRATE: the pirate (box design: characters_outlined.pirate)."""
import math

from players_lowpoly.kit import Char, Rig, body, plate, hexring, ngon, ring, ccw, shift, rotx, rotz

DESC = "the pirate: a long team coat with gold buttons, a white shirt, a black tricorn with gold trim, boots and an eye patch"


def tri(r, cz=0.0, cut=0.32):
    """the tricorn's plan (x, z): a triangle with its point forward (-z) and its three corners cut"""
    out = []
    for a in (0.0, 2 * math.pi / 3, 4 * math.pi / 3):
        for da in (-cut, cut):
            out.append((r * math.sin(a + da), cz - r * math.cos(a + da)))
    return ccw(out)


def build():
    ch = Char("PIRATE", DESC, Rig(hip=900, hipx=130, knee=470, ankle=140))
    b = body(ch, legs="light", shins="dark", feet="dark", pelvis="leather", torso="team", head=None,
             foot=(70, 175, 92), calf=False, thigh_mid=False,
             pelvis_w=(170, 200), pelvis_d=(112, 122), waist=(205, 125), chest=(228, 140), shoulder=(212, 120))
    b.wrap("shin_", 350, 470, "dark", grow=14, shell=False)                                       # the boots' wide cuffs
    plate(ch, "pelvis", "gold", [(-40, 850), (40, 850), (40, 930), (-40, 930)], -140, -124)   # the buckle
    # the coat's long skirts: round the sides and back of each thigh, flaring to the knee
    ch.sym(lambda sd, s: ch.loft("thigh" + s, "team", [(sd * 130 - 92, -10), (sd * 130 + 92, -10), (sd * 130 + 104, 100), (sd * 130 - 104, 100)], 560,
                                 [(sd * 130 - 86, -20), (sd * 130 + 86, -20), (sd * 130 + 88, 84), (sd * 130 - 88, 84)], 930, "y"))
    # the white shirt down the front, following the coat (y, the front's depth, half width), its gold buttons
    fz = [(985, 128, 92), (1250, 152, 82), (1408, 122, 74)]
    ch.sweep("torso", "light", [(ccw([(-w, -f - 8), (w, -f - 8), (w, -f + 14), (-w, -f + 14)]), y) for y, f, w in fz], shell=False)
    ch.sym(lambda sd, s: [plate(ch, "torso", "gold", ngon(sd * 110, y, 20, 3, phase=-math.pi / 2), -160 + (y - 1060) * 0.02, -140) for y in (1060, 1200)])
    b.wrap("torso", 975, 1035, "leather", grow=9, shell=False)                     # the sash belt
    b.wrap("torso", 1362, 1418, "light", grow=10, shell=False)                     # the collar
    def shoulder(sd, s):     # (a two-ring pauldron: the kit's has three; 24 triangles less each)
        x = sd * 282
        with ch.placed("torso", rotz(-sd * 0.16), (x, 1220, 0)):
            ch.loft("torso", "team", hexring(84, 96, x), 1220, shift(hexring(56, 66, x), -sd * 20), 1408, "y")
            ch.loft("torso", "gold", hexring(90, 102, x), 1192, hexring(92, 104, x), 1228, "y", shell=False)
    ch.sym(shoulder)                                                               # the coat's shoulders, gold epaulettes
    # the face (a faceted block under the hat): the beard, the eye patch and its strap, the other eye
    ch.solid("head", "skin", ring(150, 148, 52), "y", 1430, 1660)
    ch.loft("head", "hair", [(-92, -156), (92, -156), (146, -70), (-146, -70)], 1440, [(-96, -156), (96, -156), (142, -70), (-142, -70)], 1545, "y")
    plate(ch, "head", "eye", [(14, 1560), (78, 1560), (74, 1612), (18, 1612)], -164, -150)
    plate(ch, "head", "eye", [(-112, 1622), (14, 1594), (14, 1608), (-112, 1636)], -160, -150)
    plate(ch, "head", "eye", [(-64, 1578), (-34, 1578), (-34, 1600), (-64, 1600)], -160, -150)
    # the black tricorn: a three-cornered brim turned up, the crown, its gold trim and a light skull
    ch.sweep("head", "dark", [(tri(136, cut=0.26), 1625), (tri(160, cut=0.26), 1650), (tri(184, cut=0.2), 1762)])
    ch.loft("head", "dark", ngon(0, 8, 112, 8), 1700, ngon(0, 10, 90, 8), 1806, "y")
    with ch.placed("head", rotx(0.26), (0, 1755, -181)):          # the gold trim along the front corner's edge
        plate(ch, "head", "gold", [(-32, 1730), (32, 1730), (36, 1758), (-36, 1758)], -187, -177)
    plate(ch, "head", "light", ngon(0, 1690, 22, 4, phase=0), -170, -160)
    return ch
