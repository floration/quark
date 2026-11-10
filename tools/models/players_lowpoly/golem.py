"""GOLEM: the golem (box design: characters_outlined_extra2.golem)."""
from players_lowpoly.kit import Char, Rig, Segment, body, band, plate, ring, hexring, rotz, FRONT_WRAP, BACK_WRAP

DESC = "the golem: a hulking body of uneven stone blocks with team-lit cracks, mossy shoulders, a small head sunk between them with a glowing eye slit"


def build():
    # the stones are laid over a glowing team core, which shows through wide cracks between them
    ch = Char("GOLEM", DESC, Rig(hip=880, hipx=140, knee=440, ankle=110))
    b = body(ch, legs="team", shins="stone", feet="stone_dk", pelvis="team", torso="stone", head=None,
             thigh_mid=False, calf=False, thigh=(90, 90, 80, 80), shin=(82, 84, 86, 80), foot=(90, 205, 100),
             pelvis_w=(195, 235), pelvis_d=(135, 150), waist=(226, 138), chest=(250, 152), shoulder=(222, 132), chest_y=1220)
    # the legs: a dark stone block on each glowing thigh, a lit seam between the shin's stones, a dark stone round the glowing hips
    b.wrap("thigh_", 630, 790, "stone_dk", grow=12)
    b.wrap("shin_", 240, 350, "team", grow=6, shell=False)
    b.wrap("pelvis", 760, 820, "stone_dk", grow=8, shell=False)
    # the chest: a dark slab off to one side, lit cracks through the stone
    plate(ch, "torso", "stone_dk", [(-212, 1000), (40, 985), (60, 1250), (-196, 1262)], -192, -150, shell=True)
    plate(ch, "torso", "team", [(-34, 940), (30, 940), (52, 1080), (16, 1180), (-40, 1180), (-6, 1080)], -206, -192)
    plate(ch, "torso", "team", [(-196, 1130), (24, 1116), (24, 1176), (-196, 1188)], -206, -192)
    plate(ch, "torso", "team", [(76, 960), (136, 960), (128, 1330), (70, 1330)], -186, -140)
    # the glowing core in the back
    band(ch, b, "torso", "team", 960, 1215, BACK_WRAP, grow=6, depth=14)

    # the shoulders: big uneven boulders, glowing down their outer faces, moss on top
    def shoulder(sd, s):
        cx = sd * 300
        with ch.placed("torso", rotz(-sd * 0.08), (cx, 1080, 0)):
            ch.loft("torso", "stone", hexring(98, 112, cx), 1050, hexring(80, 94, cx - sd * 14), 1380, "y")
            ch.loft("torso", "moss", hexring(78, 96, cx - sd * 12), 1372, hexring(50, 62, cx - sd * 20), 1420, "y", shell=False)
            ch.solid("torso", "team", [(-70, 1070), (70, 1070), (60, 1340), (-60, 1340)], "x", cx + sd * 88 - 10, cx + sd * 88 + 8, shell=False)
    ch.sym(shoulder)
    # the head: a squat craggy rock sunk between the shoulders, wide at the jaw
    hs = Segment(ch, "head", [(ring(152, 122, c=56), 1424), (ring(150, 128, c=60, cx=-4), 1580), (ring(104, 96, c=44, cx=4, cz=4), 1714)])
    ch.sweep("head", "stone_dk", hs.rings)
    b.seg["head"] = hs
    b.wrap("head", 1450, 1510, "team", grow=6, shell=False)                      # the jaw seam
    band(ch, b, "head", "team", 1560, 1600, FRONT_WRAP, grow=7, depth=10)        # the glowing eye slit
    ch.loft("head", "moss", ring(112, 104, c=46, cx=4, cz=4), 1704, ring(70, 64, c=30, cx=10), 1768, "y")   # the moss cap
    return ch
