"""AGENT: the agent (box design: characters_outlined_extra2.agent)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, helmet, band, plate, ring, BACK_WRAP

DESC = "the agent: a slim dark-team suit jacket with black lapels and black trousers, a white shirt and black tie, slicked hair, black shades, an earpiece cord, polished shoes"


def build():
    ch = Char("AGENT", DESC, Rig(hip=900, hipx=125, knee=470, ankle=60))
    # (the visibility rules keep big areas at sRGB 0.35 or lighter and the mean contrast on a dark backdrop at dE 35:
    # the black trousers and hair are charcoal mid_dk, the shoes dark; the box's near-black stays on the tie, the
    # lapels and the earpiece)
    b = body(ch, legs="mid_dk", shins="mid_dk", feet="dark", pelvis="team_dk", torso="team_dk", head="skin", calf=False,
             thigh=(74, 72, 66, 70), shin=(66, 70, 68, 66), foot=(64, 180, 72), pelvis_w=(170, 184), pelvis_d=(112, 118),
             waist=(184, 112), chest=(205, 124), shoulder=(190, 110), chest_y=1250, head_w=152, head_d=150, head_top=1768, jaw=0.74, crown=0.78)
    # the shirt front and the tie, black lapels either side
    plate(ch, "torso", "white", [(-30, 1120), (30, 1120), (100, 1410), (-100, 1410)], -142, -128)
    plate(ch, "torso", "hairblk", [(-22, 1150), (0, 1110), (22, 1150), (14, 1380), (-14, 1380)], -148, -140)
    ch.sym(lambda sd, s: plate(ch, "torso", "hairblk", [(sd * 34, 1130), (sd * 70, 1150), (sd * 138, 1405), (sd * 104, 1405)], -146, -130))
    plate(ch, "torso", "white", [(-150, 1310), (-100, 1310), (-100, 1335), (-150, 1335)], -138, -128)   # the pocket square
    ch.loft("torso", "white", ring(100, 82), 1375, ring(90, 74), 1428, "y")                             # the collar
    pauldron(ch, "team_dk", x=268, y0=1200, w=80, d=92, tilt=0.14)
    # the face: black shades, the mouth, slicked hair, the earpiece and its cord
    visor(ch, b, "eye", 1585, 1625, grow=4, depth=12)
    plate(ch, "head", "skin_dk", [(-34, 1515), (34, 1515), (34, 1523), (-34, 1523)], -150, -144)
    helmet(ch, b, "mid_dk", 1665, grow=5, top=12)
    band(ch, b, "head", "mid_dk", 1500, 1665, BACK_WRAP, grow=5, depth=12)           # slicked down the back
    ch.solid("head", "hairblk", [(-18, 1530), (18, 1530), (18, 1590), (-18, 1590)], "x", 146, 162, shell=False)
    ch.rod("head", "white", (154, 1535, 10), (130, 1440, 40), 7, k=4, shell=False)
    return ch
