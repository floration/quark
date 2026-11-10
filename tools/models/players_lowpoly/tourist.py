"""TOURIST: the tourist (box design: characters_outlined_extra2.tourist)."""
from players_lowpoly.kit import Char, Rig, Segment, body, visor, plate, brim, ngon, ring, hexring, shift, offset, rotz

DESC = "the tourist: a wide straw sun hat, shades, a team flowered shirt, a camera on the chest, a backpack, khaki shorts, white socks and sandals"


def flower(ch, x, y, z, r, bone="torso"):
    """a white four-petal flower printed on the shirt: a diamond, flush"""
    plate(ch, bone, "white", [(x, y - r), (x + r, y), (x, y + r), (x - r, y)], z - 5, z + 5)


def build():
    ch = Char("TOURIST", DESC, Rig(hip=900, hipx=130, knee=470, ankle=45))
    b = body(ch, legs="skin", feet="leather", pelvis="khaki", torso="team", head=None,
             thigh_mid=False, calf=False, foot=(64, 175, 82), pelvis_w=(175, 205), pelvis_d=(118, 128),
             waist=(215, 135), chest=(232, 145), shoulder=(205, 122), chest_y=1220,
             )
    # the head (under the hat): jaw, cheeks, the crown hidden in the hat
    hs = Segment(ch, "head", [(ring(100, 100), 1430), (ring(124, 126), 1545), (ring(120, 120), 1690)])
    ch.sweep("head", "skin", hs.rings)
    b.seg["head"] = hs
    # the khaki shorts, loose round the thighs; white socks pulled up over the sandals' straps
    b.wrap("thigh_", 640, 940, "khaki", grow=14)
    b.wrap("shin_", 36, 185, "white", grow=4, shell=False)
    # the flowered shirt: white flowers front and back, short team sleeves
    for x, y in ((-150, 1000), (-160, 1270), (155, 1290)):
        flower(ch, x, y, -150, 36)
    flower(ch, -120, 1365, 140, 36)

    def sleeve(sd, _):
        with ch.placed("torso", rotz(-sd * 0.22), (sd * 284, 1290, 0)):
            ch.loft("torso", "team", hexring(96, 110, sd * 284), 1195, shift(hexring(60, 82, sd * 284), -sd * 26), 1420 - ch.outline - 4, "y")
    ch.sym(sleeve)
    # the camera on the chest on its strap
    ch.solid("torso", "dark", [(-82, 1040), (82, 1040), (82, 1118), (60, 1140), (-60, 1140), (-82, 1118)], "z", -192, -142)
    ch.rod("torso", "mid", (0, 1088, -196), (0, 1085, -222), 30, k=4, shell=False)
    plate(ch, "torso", "dark", [(-60, 1134), (-26, 1134), (-128, 1408), (-162, 1408)], -168, -152)
    # the backpack, its team pocket
    pack = [(-140, 1010), (140, 1010), (140, 1270), (100, 1320), (-100, 1320), (-140, 1270)]
    ch.loft("torso", "leather", pack, 125, [(u * 0.9, 1015 + (v - 1015) * 0.95) for u, v in pack], 210, "z")
    plate(ch, "torso", "team_dk", [(-100, 1040), (100, 1040), (100, 1160), (-100, 1160)], 200, 214)
    # the face: shades, a sunburnt nose
    visor(ch, b, "eye", 1572, 1606, grow=4, depth=12, wrap=False)
    ch.solid("head", "glove", [(-146, 1535), (-122, 1535), (-122, 1575)], "x", -16, 16, shell=False)
    # the wide straw sun hat with a team band
    ch.loft("head", "straw", offset(hs.at(1650), 9), 1650, ring(100, 100, cz=6), 1774, "y")
    ch.loft("head", "team", offset(hs.at(1652), 13), 1652, offset(ring(112, 112, cz=3), 3), 1700, "y", shell=False)
    brim(ch, "straw", 1640, 112, 180, thick=16, k=8)
    return ch
