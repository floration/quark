"""CAVEMAN: the caveman (box design: characters_outlined_extra2.caveman)."""
import math

from players_lowpoly.kit import tone, Char, Rig, Segment, body, plate, ring, hexring, offset, rotz

DESC = "the caveman: a wild black mane and beard with a bone through it, a one-shoulder spotted team pelt, bare skin, a knobbly club on the back"


def part_band(ch, seg, t, y0, y1, xf, xb, grow=6.0, depth=14.0, shell=False):
    """a panel over the LEFT part of a ring()-made piece, from x = xf on its front face round the left side to
    x = xb on its back face (a one-shoulder pelt)"""
    ws = [y0] + [w for _, w in seg.rings if y0 < w < y1] + [y1]
    rings_ = []
    for w in ws:
        p = seg.at(w)
        (fa, fb), (ba, bb) = (p[0], p[1]), (p[4], p[5])
        F = (xf, fa[1] + (fb[1] - fa[1]) * (xf - fa[0]) / (fb[0] - fa[0]))
        B = (xb, ba[1] + (bb[1] - ba[1]) * (xb - ba[0]) / (bb[0] - ba[0]))
        q = [p[0], F, p[1], p[2], p[3], p[4], B, p[5], p[6], p[7]]
        out, inn = offset(q, grow), offset(q, -depth)
        chain = [6, 7, 8, 9, 0, 1]
        rings_.append(([out[i] for i in chain] + [inn[i] for i in reversed(chain)], w))
    ch.sweep(seg.bone, t, rings_, "y", shell)


def star(r_out, r_in, n, cx=0.0, cz=0.0, ph=0.0):
    """a jagged ring (x, z): n spikes, for wild hair"""
    return [(cx + (r_out if k % 2 == 0 else r_in) * math.cos(ph + math.pi * k / n), cz + (r_out if k % 2 == 0 else r_in) * math.sin(ph + math.pi * k / n))
            for k in range(2 * n)]

# a black that stays at the visibility floor for big areas (sRGB luminance 0.35; the box set's hairblk is 0.22)
HAIR = tone("cave_hair", (0.37, 0.35, 0.37))


def build():
    ch = Char("CAVEMAN", DESC, Rig(hip=890, hipx=130, knee=440, ankle=60))
    b = body(ch, legs="team", shins="skin", feet="fur", pelvis="team", torso="skin", head=None,
             thigh_mid=False, calf=False, foot=(70, 175, 88), thigh=(84, 84, 70, 72), shin=(68, 70, 72, 68),
             waist=(200, 120), chest=(226, 132), shoulder=(204, 114), chest_y=1200, pelvis_w=(184, 210), pelvis_d=(122, 132))
    # (the thighs: the pelt skirt, to the knees)
    # the pelt: over the left half of the chest and up over the left shoulder; its spots
    tor = b.seg["torso"]
    part_band(ch, tor, "team", 900, 1200, 90, 20, grow=7, depth=14)
    part_band(ch, tor, "team", 1200, 1408, -60, -60, grow=7, depth=14)
    plate(ch, "torso", "eye", [(-150, 990), (-100, 990), (-100, 1030), (-150, 1030)], -150, -132)
    plate(ch, "torso", "eye", [(-130, 1010), (-60, 1010), (-60, 1044), (-130, 1044)], 132, 150)

    # the shoulders: the pelt over the left, bare skin on the right
    def shoulder(sd, s):
        cx = sd * 282
        with ch.placed("torso", rotz(-sd * 0.18), (cx, 1236, 0)):
            ch.loft("torso", "team" if sd < 0 else "skin", hexring(82, 94, cx), 1230, hexring(58, 68, cx - sd * 16), 1404, "y")
    ch.sym(shoulder)
    # the knobbly club slung across the back
    with ch.placed("torso", rotz(0.42), (90, 960, 0)):
        ch.loft("torso", "hat", hexring(24, 22, 90, 156), 960, hexring(58, 50, 90, 168), 1400, "y")
    # the face; the wild mane round its back and sides, a jagged crown of hair, the beard, the bone
    hs = Segment(ch, "head", [(ring(70, 70, c=28), 1430), (ring(100, 102, c=40), 1480), (ring(98, 100, c=40), 1640)])
    ch.sweep("head", "skin", hs.rings)
    mane = []
    for w in (1450, 1650):
        p = hs.at(w)
        out, inn = offset(p, 44 if w < 1600 else 38), offset(p, -10)
        chain = [2, 3, 4, 5, 6, 7]
        outer = []
        for i, j in zip(chain, chain[1:]):     # a tuft sticking out between each two corners
            a, c = out[i], out[j]
            L = math.hypot(c[0] - a[0], c[1] - a[1])
            outer += [a, ((a[0] + c[0]) / 2 + (c[1] - a[1]) / L * 34, (a[1] + c[1]) / 2 - (c[0] - a[0]) / L * 34)]
        outer.append(out[chain[-1]])
        mane.append((outer + [inn[i] for i in reversed(chain)], w))
    ch.sweep("head", HAIR, mane)
    ch.loft("head", HAIR, star(166, 104, 4, 0, 8, ph=-math.pi / 2), 1626, star(118, 46, 4, 0, 16, ph=-math.pi / 2 + 0.5), 1786, "y")
    ch.loft("head", "hairblk", hexring(84, 30, 0, -86), 1436, hexring(72, 26, 0, -94), 1530, "y")
    plate(ch, "head", "eye", [(-58, 1572), (58, 1572), (54, 1596), (-54, 1596)], -110, -96)         # the eyes, in the brow's shadow
    ch.rod("head", "bone", (-170, 1688, -6), (170, 1700, -6), 17, k=4)
    return ch
