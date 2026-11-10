"""FIREFIGHTER: the firefighter (box design: characters_outlined_extra.firefighter)."""
from players_lowpoly.kit import Char, Rig, body, helmet, plate, hexring, ngon, rotz, rotx

DESC = "the firefighter: a khaki turnout coat and trousers with yellow reflective bands, a team helmet with a long back brim, an air tank"


def build():
    ch = Char("FIREFIGHTER", DESC, Rig(hip=910, hipx=130, knee=460, ankle=140))
    # team trousers (the box's are khaki: team here so the legs read) under the long khaki coat, whose skirt is the
    # wide pelvis
    b = body(ch, legs="team", shins="team", feet="dark", pelvis="khaki", torso="khaki", head="skin",
             thigh=(86, 86, 74, 76), shin=(72, 76, 78, 70), foot=(70, 190, 95), thigh_mid=False, calf=False,
             pelvis_w=(200, 222), pelvis_d=(132, 142), waist=(212, 136), chest=(232, 146), shoulder=(205, 122), chest_y=1240,
             head_w=148, head_d=134, head_top=1755, jaw=0.82, crown=0.8)
    # the trousers' reflective bands
    b.wrap("shin_", 255, 300, "hivis", grow=5, shell=False)
    # the coat's reflective bands, its dark closure
    b.wrap("torso", 960, 1010, "hivis", grow=5, shell=False)
    b.wrap("torso", 1120, 1170, "hivis", grow=5, shell=False)
    plate(ch, "torso", "dark", [(-15, 950), (15, 950), (15, 1330), (-15, 1330)], -168, -140)
    # the team collar
    b.wrap("torso", 1340, 1408, "team", grow=12, shell=False)

    # the team shoulders with a reflective band
    def shoulder(sd, s):
        x = sd * 282
        with ch.placed("torso", rotz(-sd * 0.18), (x, 1220, 0)):
            ch.sweep("torso", "team", [(hexring(90, 108, x), 1215), (hexring(62, 76, x - sd * 22), 1405)])
            ch.sweep("torso", "hivis", [(hexring(97, 115, x), 1240), (hexring(92, 110, x - sd * 4), 1290)], shell=False)
    ch.sym(shoulder)
    # the air tank on the back: a light cylinder, its team band, the dark valve
    ch.sweep("torso", "light", [(ngon(0, 172, 86, 6), 990), (ngon(0, 172, 70, 6), 1350)])
    ch.sweep("torso", "team", [(ngon(0, 172, 92, 6), 1180), (ngon(0, 172, 90, 6), 1240)], shell=False)
    ch.solid("torso", "dark", ngon(0, 172, 28, 4), "y", 1350, 1395, shell=False)
    # the face's eyes
    plate(ch, "head", "eye", [(-60, 1578), (60, 1578), (60, 1596), (-60, 1596)], -150, -138)
    # the team helmet, its long dark brim swept back, the gold badge
    helmet(ch, b, "team", 1620, grow=9, top=14)
    with ch.placed("head", rotx(0.3), (0, 1622, 40)):
        ch.solid("head", "team_dk", [(0, -168), (150, -90), (166, 56), (74, 172), (-74, 172), (-166, 56), (-150, -90)],
                 "y", 1606, 1630)
    plate(ch, "head", "gold", [(-38, 1655), (38, 1655), (32, 1725), (0, 1742), (-32, 1725)], -166, -150)
    return ch
