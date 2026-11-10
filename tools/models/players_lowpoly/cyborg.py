"""CYBORG: the cyborg (box design: characters_outlined_extra2.cyborg)."""
from players_lowpoly.kit import Char, Rig, Segment, Mesh, body, plate, ring, hexring, shift, offset, rotz

DESC = "the cyborg: split down the middle, a human left half in team fatigues and a chrome right half with a red eye, a big piston arm mount and a mechanical leg"


FOOT = (70, 190, 92)


def half(poly, side):
    """the part of a convex polygon (u, v) on one side of u = 0 (side +1: u >= 0, the model's right)"""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ina, inb = a[0] * side >= 0, b[0] * side >= 0
        if ina:
            out.append(a)
        if ina != inb:
            t = a[0] / (a[0] - b[0])
            out.append((0.0, a[1] + (b[1] - a[1]) * t))
    return out


def half_cover(ch, seg, t, side, grow, y0=None, shell=False):
    """one half of a swept body piece (x >= 0 or x <= 0) covered in another tone, `grow` mm proud: the split down
    the middle"""
    ws = [w for _, w in seg.rings if y0 is None or w > y0]
    if y0 is not None:
        ws = [y0] + ws
    ch.sweep(seg.bone, t, [(half(offset(seg.at(w), grow), side), w) for w in ws], "y", shell)


def recolour(ch, bone, t, rings):
    """rebuild one bone's swept piece in another tone (the right leg: steel instead of fatigues)"""
    ch.mesh[bone] = Mesh()
    ch.sweep(bone, t, rings)


def build():
    ch = Char("CYBORG", DESC, Rig(hip=900, hipx=130, knee=470, ankle=105))
    b = body(ch, legs="team", feet="dark", pelvis="dark", torso="team", head=None,
             thigh_mid=False, calf=False, foot=FOOT, pelvis_w=(165, 200), pelvis_d=(112, 125),
             waist=(200, 125), chest=(225, 135), shoulder=(205, 122), chest_y=1240)
    # the right leg is a machine: a chrome thigh with a piston up its front, a steel shin with a dark knee joint and
    # a glowing strip, a steel foot (the left one stays a dark boot)
    recolour(ch, "thigh_r", "light", b.seg["thigh_r"].rings)
    ch.rod("thigh_r", "mid", (130, 520, -76), (130, 870, -76), 22, k=4)
    recolour(ch, "shin_r", "mid", b.seg["shin_r"].rings)
    ch.rod("shin_r", "dark", (52, 468, 0), (208, 468, 0), 54, k=5)
    plate(ch, "shin_r", "visor", [(105, 180), (155, 180), (155, 380), (105, 380)], -72, -58)
    r, (fw, toe, heel) = ch.rig, FOOT       # (the kit's foot profile, rebuilt in steel)
    ch.mesh["foot_r"] = Mesh()
    y0, y1 = ch.outline, r.ankle + 22
    ch.solid("foot_r", "mid", [(heel, y0), (-toe + 20, y0), (-toe, y0 + 34), (-toe * 0.3, y1 - 14), (-10, y1), (heel, y1)], "x", r.hipx - fw, r.hipx + fw)
    # the body: team fatigues on the left (the hips too), chrome on the right with ribs and a glowing core, a pocket on the left
    half_cover(ch, b.seg["torso"], "light", 1, 3)
    half_cover(ch, b.seg["pelvis"], "team", -1, 3)
    for y in (1050, 1150):
        plate(ch, "torso", "mid", [(40, y), (190, y), (190, y + 40), (40, y + 40)], -152, -136)
    plate(ch, "torso", "visor", [(50, 1260), (130, 1260), (130, 1340), (50, 1340)], -152, -138)
    plate(ch, "torso", "team_dk", [(-180, 1120), (-60, 1120), (-60, 1260), (-180, 1260)], -150, -136)
    # the big piston arm mount on the right shoulder, a light band round it, the piston under it
    ch.loft("torso", "mid", ring(86, 106, cx=298), 1090, ring(72, 92, cx=292), 1410, "y")
    ch.loft("torso", "light", ring(92, 112, cx=297), 1270, ring(92, 112, cx=299), 1310, "y", shell=False)
    ch.rod("torso", "dark", (296, 1000, 0), (296, 1092, 0), 46, k=6)
    # the left shoulder in fatigues
    with ch.placed("torso", rotz(0.24), (-286, 1290, 0)):
        ch.loft("torso", "team", hexring(96, 106, -286), 1190, shift(hexring(58, 78, -286), 24), 1420 - ch.outline - 4, "y")
    # the head: the human left with an eye and hair, the chrome right with a red eye, an ear unit, a jaw plate
    hs = Segment(ch, "head", [(ring(110, 112), 1430), (ring(150, 158), 1545), (ring(148, 154), 1690), (ring(108, 112), 1772)])
    ch.sweep("head", "skin", hs.rings)
    half_cover(ch, hs, "light", 1, 3)
    half_cover(ch, hs, "hair", -1, 7, y0=1660)
    plate(ch, "head", "eye", [(-90, 1585), (-40, 1585), (-40, 1605), (-90, 1605)], -164, -150)
    plate(ch, "head", "redeye", [(28, 1575), (92, 1575), (92, 1612), (28, 1612)], -168, -154)
    plate(ch, "head", "mid", [(8, 1490), (110, 1490), (100, 1510), (8, 1510)], -152, -136)
    ch.solid("head", "dark", hexring(30, 52, cx=150), "y", 1530, 1640)
    return ch
