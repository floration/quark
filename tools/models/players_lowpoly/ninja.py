"""NINJA: the ninja (box design: characters_outlined.ninja)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, plate, profile, rotx

DESC = "the ninja: dark wraps head to toe, a hood with an eye slit, team sash, shin wraps, headband and scarf tails"


def build():
    ch = Char("NINJA", DESC, Rig(hip=900, hipx=128, knee=470, ankle=95))
    b = body(ch, legs="mid", shins="team", feet="dark", pelvis="mid", torso="mid", head="dark",
             foot=(60, 170, 85), thigh_mid=False, thigh=(78, 78, 64, 70), waist=(180, 112), chest=(212, 128), shoulder=(190, 110), jaw=0.84, crown=0.72)
    b.wrap("thigh_", 450, 660, "team", grow=5, shell=False)                       # the knee wraps
    # the gi's crossed front: a dark V down the chest, laid on its slope
    with ch.placed("torso", rotx(0.21), (0, 1250, -150)):
        plate(ch, "torso", "dark", [(-118, 1405), (-66, 1405), (0, 1290), (66, 1405), (118, 1405), (0, 1215)], -156, -144)
    b.wrap("torso", 925, 1040, "team", grow=9)                                     # the sash
    plate(ch, "torso", "team", [(70, 700), (130, 700), (140, 920), (80, 920)], -142, -126)   # its tail
    # the sword across the back, its grip over the left shoulder
    ch.rod("torso", "dark", (95, 1010, 150), (-55, 1385, 150), 34, k=4)
    ch.rod("torso", "dark", (-55, 1385, 150), (-78, 1470, 145), 20, k=4)
    pauldron(ch, "team", x=268, y0=1240, w=70, d=90)                               # the shoulders, wrapped
    visor(ch, b, "skin", 1585, 1640, grow=4, depth=14)                             # the eye slit
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 22, 1600), (sd * 70, 1600), (sd * 70, 1624), (sd * 22, 1624)], -166, -156))
    b.wrap("head", 1682, 1722, "team", grow=6)                                     # the headband
    profile(ch, "head", "team", [(140, 1705), (168, 1695), (178, 1540), (150, 1560)], -24, 24)   # its tails
    return ch
