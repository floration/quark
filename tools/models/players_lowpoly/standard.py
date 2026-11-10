"""STANDARD: the soldier (box design: characters_outlined.standard)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, plate, backpack, crest, rrect

DESC = "the soldier: light armour, team torso and thighs, a cyan visor and a team crest"


def build():
    ch = Char("STANDARD", DESC, Rig(hip=910, hipx=125, knee=470, ankle=125))
    b = body(ch, legs="team", shins="light", feet="dark", pelvis="dark", torso="team", head="light", knee_pad="dark")
    # a team plate down the front of each shin (the box design's shin plate)
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "team", [(sd * 125 - 50, 190), (sd * 125 + 50, 190), (sd * 125 + 44, 410), (sd * 125 - 44, 410)], -74, -56))
    plate(ch, "pelvis", "dark", [(-50, 905), (50, 905), (50, 960), (-50, 960)], -150, -128)         # its buckle
    plate(ch, "torso", "light", [(-170, 1180), (170, 1180), (190, 1300), (150, 1365), (-150, 1365), (-190, 1300)], -168, -140, shell=True)  # chest plate
    backpack(ch, "dark", 140, 1030, 1330, 110, 70)
    plate(ch, "torso", "team", [(-110, 1100), (110, 1100), (110, 1280), (-110, 1280)], 176, 190)           # the pack's stripe
    pauldron(ch, "light")
    visor(ch, b, "visor", 1585, 1645, grow=5, depth=16)
    visor(ch, b, "dark", 1490, 1515, grow=3, depth=12, wrap=False)                # the chin
    crest(ch, "team", -120, 120, 1730, 60, x=26)
    return ch
