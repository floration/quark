"""ALIEN: the alien (box design: characters_outlined_extra.alien)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, plate, ring, hexring

DESC = "the alien: a teal skin, a huge cranium with big black slanted eyes, a long thin neck, a skinny silver suit with team yoke and belt"


def build():
    ch = Char("ALIEN", DESC, Rig(hip=900, hipx=120, knee=450, ankle=60))
    # a skinny silver suit: the torso is the team yoke, the suit's light lower half wrapped over it
    b = body(ch, legs="light", shins="alien", feet="alien", pelvis="light", torso="team", head=None,
             thigh=(70, 72, 64, 70), shin=(64, 70, 68, 67), foot=(62, 165, 70), thigh_mid=False, calf=True,
             pelvis_w=(150, 172), pelvis_d=(100, 108), waist=(160, 104), chest=(182, 112), shoulder=(170, 100), chest_y=1250)
    b.wrap("torso", 935, 1090, "light", grow=5, shell=False)       # the suit below the yoke
    b.wrap("pelvis", 850, 965, "team", grow=8)                       # the team belt
    plate(ch, "torso", "visor", [(-30, 1250), (30, 1250), (36, 1290), (0, 1318), (-36, 1290)], -136, -118)   # the chest gem
    pauldron(ch, "team", x=262, y0=1250, w=80, d=86, tilt=0.2)
    # the long thin neck
    ch.sweep("torso", "alien", [(hexring(44, 42), 1380), (hexring(38, 36), 1470)], shell=False)
    ch.sweep("head", "alien", [(hexring(38, 36), 1436), (hexring(34, 34), 1520)])
    # the small pointed jaw
    ch.sweep("head", "alien", [(ring(40, 40, front=48), 1500), (ring(90, 88, front=96), 1575)])
    # the huge cranium: flat across the eyes, swelling back and up
    ch.sweep("head", "alien", [(ring(96, 92, c=40), 1560), (ring(158, 150, c=66, front=148), 1618), (ring(164, 158, c=72, front=150), 1712),
                               (ring(130, 128, c=56), 1772), (ring(70, 66), 1796)])
    # the big black slanted eyes, the small mouth
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 14, 1624), (sd * 56, 1606), (sd * 104, 1650), (sd * 112, 1704), (sd * 64, 1692), (sd * 18, 1656)],
                               -160, -138))
    plate(ch, "head", "eye", [(-22, 1528), (22, 1528), (22, 1536), (-22, 1536)], -102, -84)
    return ch
