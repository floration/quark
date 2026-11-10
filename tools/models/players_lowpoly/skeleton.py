"""SKELETON: the skeleton (box design: characters_outlined_extra.skeleton)."""
from players_lowpoly.kit import Char, Rig, Segment, body, pauldron, plate, ring, hexring, rotx

DESC = "the skeleton: thin bone legs, a rib cage over a dark core, a skull with red glowing eyes, team shorts, scarf and pauldrons"


def build():
    ch = Char("SKELETON", DESC, Rig(hip=900, hipx=130, knee=470, ankle=60))
    # bone legs as thin as the 12 cm limb rule lets them, long team shorts, a dark core under the ribs
    b = body(ch, legs="bone", shins="bone", feet="bone", pelvis="team", torso="dark", head=None,
             thigh=(84, 84, 70, 72), shin=(60, 70, 60, 67), foot=(58, 150, 70), calf=False, thigh_mid=False,
             pelvis_w=(170, 200), pelvis_d=(112, 120), waist=(150, 96), chest=(205, 126), shoulder=(190, 110), chest_y=1240)
    ch.sym(lambda sd, s: ch.loft("shin" + s, "bone", hexring(72, 50, sd * 130, -14), 420, hexring(66, 46, sd * 130, -16), 505, "y", shell=False))   # knee caps
    b.wrap("thigh_", 610, 940, "team", grow=12, shell=False)                      # the shorts' legs
    # the rib cage: three ribs round the dark core, a sternum, a spine down the back
    for y in (1060, 1160, 1260):
        b.wrap("torso", y, y + 44, "bone", grow=8, shell=False)
    plate(ch, "torso", "bone", [(-22, 1040), (22, 1040), (22, 1330), (-22, 1330)], -150, -134)              # sternum
    plate(ch, "torso", "bone", [(-26, 930), (26, 930), (26, 1360), (-26, 1360)], 116, 134)                  # spine
    pauldron(ch, "team", x=290, y0=1230, w=80, d=98, tilt=0.2)
    # the scarf: a team wrap at the neck, its end hanging down the chest
    ch.sweep("torso", "team", [(ring(206, 128, front=138), 1330), (ring(176, 122, front=130), 1426)], shell=False)
    with ch.placed("torso", rotx(0.12), (0, 1360, -120)):
        ch.loft("torso", "team", [(10, -134), (110, -134), (110, -116), (10, -116)], 1150, [(20, -128), (110, -128), (110, -110), (20, -110)], 1370, "y", shell=False)
    # the skull: jaw, cheekbones, a big cranium
    n = 1440
    rings_ = [(ring(56, 56), n - 30),                                           # the neck bone
              (ring(118, 120, c=50), n + 30), (ring(130, 140, front=150, c=60), n + 110),  # jaw, cheekbones
              (ring(158, 158, front=160, c=68), n + 170), (ring(158, 156, c=68), n + 260), (ring(112, 112, c=50), 1782)]
    sk = Segment(ch, "head", rings_)
    ch.sweep("head", "bone", sk.rings)
    b.seg["head"] = sk
    # the face: two eye sockets with red glows, a nose hole, teeth
    def eye(sd, _):
        x = sd * 58
        plate(ch, "head", "bore", [(x - 36, 1580), (x + 36, 1580), (x + 32, 1640), (x - 32, 1648)], -170, -155)
        plate(ch, "head", "redeye", [(x - 15, 1596), (x + 15, 1596), (x + 15, 1626), (x - 15, 1626)], -174, -169)
    ch.sym(eye)
    plate(ch, "head", "bore", [(-20, 1540), (20, 1540), (0, 1572)], -165, -150)                              # nose
    plate(ch, "head", "bore", [(-70, 1486), (70, 1486), (70, 1500), (-70, 1500)], -140, -124)               # the teeth's line
    return ch
