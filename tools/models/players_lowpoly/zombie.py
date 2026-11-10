"""ZOMBIE: the zombie (box design: characters_outlined.zombie)."""
from players_lowpoly.kit import Char, Rig, body, visor, plate, helmet, hexring

DESC = "the zombie: green skin, arms reaching out, a torn team shirt and ripped trousers, glowing eyes"

# the shirt's torn hem (x, y): ragged points hanging over the belt
HEM = [(-205, 990), (205, 990), (205, 850), (110, 880), (40, 800), (-30, 862), (-120, 816), (-205, 856)]


def build():
    ch = Char("ZOMBIE", DESC, Rig(hip=880, hipx=130, knee=460, ankle=90))
    b = body(ch, legs="mid", shins="mid", feet="dark", pelvis="dark", torso="team", head="zgreen",
             calf=False, thigh_mid=False, foot=(62, 165, 82), pelvis_w=(165, 195), pelvis_d=(110, 118), waist=(205, 128), chest=(222, 140), shoulder=(200, 118),
             head_w=160, head_d=158, head_top=1762, jaw=0.86, crown=0.8)
    # the ripped trousers: torn shins showing green skin, a rolled turn-up over the knee
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "zgreen", [(sd * 66, 220), (sd * 128, 232), (sd * 132, 340), (sd * 96, 352), (sd * 64, 330)], -74, -58))
    b.wrap("thigh_", 515, 585, "mid_dk", grow=7, shell=False)
    # the torn shirt: its ragged hem front and back, holes showing the green skin
    plate(ch, "torso", "team", HEM, -142, -122)
    plate(ch, "torso", "team", HEM, 122, 142)
    plate(ch, "torso", "zgreen", [(-60, 1040), (70, 1050), (80, 1150), (-55, 1160)], -158, -138)
    plate(ch, "torso", "zgreen", [(-140, 1200), (-80, 1196), (-84, 1258), (-136, 1252)], -164, -146)
    plate(ch, "torso", "zgreen", [(60, 1080), (140, 1090), (136, 1220), (70, 1210)], 140, 158)
    b.wrap("torso", 1360, 1418, "zgreen", grow=4, shell=False)                     # the bare neck
    ch.sym(lambda sd, s: ch.loft("torso", "team", hexring(86, 96, sd * 282), 1170, hexring(66, 82, sd * 262), 1405, "y"))   # the shirt's sleeves
    # the arms reaching forward, the hands a darker green (the gun arm is drawn over the right one)
    ch.sym(lambda sd, s: ch.rod("torso", "zgreen", (sd * 210, 1250, -40), (sd * 210, 1238, -262), 60, k=4))
    ch.sym(lambda sd, s: ch.rod("torso", "zgreen_dk", (sd * 210, 1238, -252), (sd * 210, 1234, -312), 68, k=4))
    # the head: shaggy hair over the top and a fringe, glowing eyes, a dark mouth
    helmet(ch, b, "hair", 1690, grow=7, top=12)
    plate(ch, "head", "hair", [(-108, 1700), (-10, 1700), (-30, 1652), (-80, 1638), (-108, 1626)], -160, -144)
    ch.sym(lambda sd, s: plate(ch, "head", "zeye", [(sd * 28, 1584), (sd * 80, 1584), (sd * 80, 1622), (sd * 28, 1622)], -170, -156))
    visor(ch, b, "zgreen_dk", 1490, 1518, grow=3, depth=10, wrap=False)           # the mouth
    return ch
