"""HEAVY: the tank (box design: characters_outlined.heavy)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, band, plate, backpack, crest, hexring, FRONT_WRAP

DESC = "the tank: bulky dark armour, huge light pauldrons, a team chest plate, a small helmet with a glow slit"


def build():
    ch = Char("HEAVY", DESC, Rig(hip=880, hipx=150, knee=450, ankle=150))
    b = body(ch, legs="team", shins="team", calf=False, feet="dark", pelvis="dark", torso="dark", head="mid", bulk=1.0,
             thigh=(92, 92, 78, 80), shin=(78, 80, 88, 74), foot=(82, 205, 105), pelvis_w=(190, 230), pelvis_d=(130, 145),
             waist=(225, 140), chest=(255, 160), shoulder=(215, 135), chest_y=1230, jaw=0.86, crown=0.66, head_top=1765)
    # the boots' toe caps
    ch.sym(lambda sd, s: ch.loft("foot" + s, "mid", [(sd * 150 - 74, -200), (sd * 150 + 74, -200), (sd * 150 + 70, -40), (sd * 150 - 70, -40)], 120,
                                 [(sd * 150 - 60, -170), (sd * 150 + 60, -170), (sd * 150 + 56, -60), (sd * 150 - 56, -60)], 175, "y", shell=False))
    # the knee guards
    ch.sym(lambda sd, s: ch.loft("shin" + s, "mid", hexring(72, 26, sd * 150, -62), 395, hexring(64, 22, sd * 150, -60), 500, "y", shell=False))
    b.wrap("torso", 920, 985, "mid", grow=8, shell=False)                                                   # the belt
    band(ch, b, "torso", "team", 995, 1395, FRONT_WRAP, grow=6, depth=20)         # the chest plate
    plate(ch, "torso", "light", [(-90, 1120), (90, 1120), (70, 1250), (-70, 1250)], -188, -170)   # its badge
    backpack(ch, "mid", 175, 990, 1350, 125, 75)                                                # the power pack
    plate(ch, "torso", "team", [(-150, 1010), (150, 1010), (150, 1320), (-150, 1320)], 196, 212)  # its panel
    pauldron(ch, "light", x=280, y0=1190, w=96, d=110, tilt=0.22)
    b.wrap("head", 1430, 1520, "dark", grow=6, shell=False)                                                  # the jaw guard
    visor(ch, b, "visor", 1590, 1620, grow=5, depth=14, wrap=False)                                        # the glow slit
    crest(ch, "team", -110, 110, 1720, 55, x=34)                                                # the helmet's ridge
    return ch
