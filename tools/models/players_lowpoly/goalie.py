"""GOALIE: the ice-hockey goalie (box design: characters_outlined_extra2.goalie)."""
from players_lowpoly.kit import Char, Rig, Segment, Mesh, body, plate, profile, ring, hexring, shift, offset, rotz, BACK_WRAP

DESC = "the ice-hockey goalie: huge white leg pads with team bands, skates, a bulky team jersey with white stripes, a white mask with a cage, a stick on the back"


def pad(x, w=108, z0=-58, z1=-150, grow=0.0):
    """a leg pad's cross-section (x, z): a wide slab over the front of the leg, ridged down its middle"""
    g = grow
    return [(x - w - g, z0), (x + w + g, z0), (x + w + g - 4, z0 - 58 - g), (x, z1 - g), (x - w - g + 4, z0 - 58 - g)]


def build():
    ch = Char("GOALIE", DESC, Rig(hip=900, hipx=130, knee=470, ankle=120))
    b = body(ch, legs="dark", feet="dark", pelvis="team", torso="team", head=None,
             thigh_mid=False, calf=False, pelvis_w=(190, 225), pelvis_d=(130, 140),
             waist=(240, 152), chest=(262, 166), shoulder=(232, 140), chest_y=1230)
    r = ch.rig

    def leg(sd, s):
        x = sd * r.hipx
        # the skate: a dark boot raised on a light blade (the boot rebuilt over the blade)
        ch.mesh["foot" + s] = Mesh()
        ch.solid("foot" + s, "dark", [(95, 44), (-170, 44), (-196, 70), (-90, r.ankle + 10), (-10, r.ankle + 22), (95, r.ankle + 22)], "x", x - 70, x + 70)
        profile(ch, "foot" + s, "light", [(-206, 30), (-180, 10), (126, 10), (118, 46), (-196, 46)], x - 9, x + 9, shell=False)
        # the shin pad, knee to toe, with a team band; the thigh pad with its band
        ch.solid("shin" + s, "white", pad(x), "y", 90, 520)
        ch.solid("shin" + s, "team", pad(x, grow=4), "y", 230, 290, shell=False)
        ch.solid("thigh" + s, "white", pad(x, w=104, z0=-50, z1=-136), "y", 540, 880)
        ch.solid("thigh" + s, "team", pad(x, w=104, z0=-50, z1=-136, grow=4), "y", 680, 740, shell=False)
    ch.sym(leg)
    # the bulky jersey: a white stripe round it, a team_dk chest protector
    b.wrap("torso", 1000, 1060, "white", grow=5, shell=False)
    plate(ch, "torso", "team_dk", [(-180, 1120), (180, 1120), (170, 1340), (-170, 1340)], -186, -168)

    # the big shoulders
    def shoulder(sd, _):
        cx = sd * 292
        with ch.placed("torso", rotz(-sd * 0.2), (cx, 1290, 0)):
            ch.loft("torso", "team", hexring(104, 120, cx), 1150, shift(hexring(66, 88, cx), -sd * 22), 1420 - ch.outline - 4, "y")
    ch.sym(shoulder)
    # the stick up the back, its blade at the bottom
    ch.rod("torso", "hat", (160, 640, 180), (160, 1400, 180), 22, k=4)
    profile(ch, "torso", "hat", [(170, 540), (195, 540), (195, 650), (170, 650)], 150, 285)
    # the mask: white, team paint over its back, a dark cage with light bars over the face
    hs = Segment(ch, "head", [(ring(118, 124), 1430), (ring(158, 156), 1545), (ring(152, 150), 1690), (ring(106, 112), 1780)])
    ch.sweep("head", "white", hs.rings)
    sg = [((offset(hs.at(w), 4), offset(hs.at(w), -10)), w) for w in (1500, 1545, 1690)]
    ch.sweep("head", "team", [([p[i] for i in BACK_WRAP] + [q[i] for i in reversed(BACK_WRAP)], w) for (p, q), w in sg], "y", shell=False)
    plate(ch, "head", "bore", [(-74, 1470), (74, 1470), (84, 1530), (78, 1625), (-78, 1625), (-84, 1530)], -168, -150)
    for x in (-36, 26):
        plate(ch, "head", "light", [(x, 1462), (x + 10, 1462), (x + 10, 1632), (x, 1632)], -178, -160)
    plate(ch, "head", "light", [(-80, 1540), (80, 1540), (80, 1556), (-80, 1556)], -176, -158)
    return ch
