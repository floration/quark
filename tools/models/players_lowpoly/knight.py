"""KNIGHT: the knight (box design: characters_outlined.knight)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, band, plate, profile, FRONT, BACK

DESC = "the knight: light plate, a team tabard front and back, pauldrons, a great helm with a team plume"


def build():
    ch = Char("KNIGHT", DESC, Rig(hip=900, hipx=132, knee=470, ankle=112))
    b = body(ch, legs="team", shins="light", feet="mid", pelvis="mid", torso="light", head="light",
             foot=(66, 195, 92), jaw=0.94, crown=0.84, head_top=1760, waist=(185, 118), chest=(225, 142))
    # the greaves' team stripes
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "team", [(sd * 132 - 40, 170), (sd * 132 + 40, 170), (sd * 132 + 36, 390), (sd * 132 - 36, 390)], -72, -54))
    # the tabard, front and back, hanging to mid thigh; a light cross on the front
    band(ch, b, "torso", "team", 700, 1330, FRONT, grow=7, depth=12)
    band(ch, b, "torso", "team", 700, 1330, BACK, grow=7, depth=12)
    plate(ch, "torso", "light", [(-28, 960), (28, 960), (28, 1260), (-28, 1260)], -166, -150)
    plate(ch, "torso", "light", [(-95, 1135), (95, 1135), (95, 1190), (-95, 1190)], -166, -150)
    b.wrap("torso", 925, 975, "dark", grow=10, shell=False)                     # the sword belt
    pauldron(ch, "light", x=282, y0=1215, w=84, d=104, band="team")
    # the great helm: a band round it at the eyes, the eye slit
    b.wrap("head", 1572, 1628, "mid", grow=5, shell=False)
    visor(ch, b, "bore", 1590, 1612, grow=8, depth=10)
    # the team plume sweeping back over the helm and down
    profile(ch, "head", "team", [(-80, 1730), (130, 1690), (150, 1600), (172, 1700), (110, 1815), (-50, 1822)], -20, 20)
    return ch
