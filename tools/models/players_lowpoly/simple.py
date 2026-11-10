"""SIMPLE: the plainest figure (box design: characters_outlined.simple)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor

DESC = "the plainest figure: one team body block, a light head with a visor, plain legs"


def build():
    ch = Char("SIMPLE", DESC, Rig(hip=900, hipx=130, knee=470, ankle=100))
    # one smooth team body, plain legs (team shins so the colour reads down the legs too), a rounded light head
    b = body(ch, legs="mid", shins="team", feet="dark", pelvis="dark", torso="team", head="light",
             foot=(62, 175, 85), waist=(200, 125), chest=(222, 135), shoulder=(215, 125), jaw=0.82, crown=0.74, head_top=1790)
    pauldron(ch, "team", x=272, y0=1240, w=80, d=94, tilt=0.16)                 # rounded team shoulders
    visor(ch, b, "visor", 1580, 1635, grow=5, depth=14)
    return ch
