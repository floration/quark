"""CLOWN: the clown (box design: characters_outlined_extra.clown)."""
import math

from players_lowpoly.kit import Char, Rig, body, plate, ring, hexring, ngon, rotz

DESC = "the clown: a white face, a red nose, pink side tufts and a tiny hat, a big white ruffle, a team suit with pompoms, yellow trousers, long red shoes"


def star(r_out, r_in, k, cx=0.0, cz=0.0, phase=0.0):
    """a k-pointed star (x, z): a ruffle's frills"""
    out = []
    for i in range(2 * k):
        a = phase + math.pi * i / k
        r = r_out if i % 2 == 0 else r_in
        out.append((cx + r * math.cos(a), cz + r * math.sin(a)))
    return out


def blob(ch, bone, t, x, y, z, r, depth, shell=False):
    """a faceted knob pushed out of a surface toward -z (a nose): a hexagon tapering to a smaller one"""
    ch.loft(bone, t, ngon(x, y, r, 6), z - depth, ngon(x, y, r * 0.6, 6, math.pi / 6), z - depth * 0.05 - 0.01, "z", shell)
    # (wa > wb: loft sorts the ends)


def puff(ch, t, sd, x=290, y0=1215, y1=1418, w=86, d=100):
    """a puffed sleeve top (a 2-ring pauldron: 20 triangles less than the kit's)"""
    ch.loft("torso", t, hexring(w, d, sd * x), y0, hexring(w * 0.7, d * 0.75, sd * (x - 20)), y1 - ch.outline, "y")


def build():
    ch = Char("CLOWN", DESC, Rig(hip=910, hipx=130, knee=480, ankle=80))
    # baggy yellow trousers that flare to the ankles, a team suit, yellow hips
    b = body(ch, legs="canary", shins="canary", feet="glove", pelvis="canary", torso="team", head=None, thigh_mid=False, calf=False,
             thigh=(90, 90, 80, 82), shin=(80, 82, 90, 88), foot=(74, 250, 92),
             pelvis_w=(195, 215), pelvis_d=(125, 132), waist=(215, 140), chest=(232, 150), shoulder=(205, 128), chest_y=1220)
    b.wrap("shin_", 92, 160, "white", grow=14, shell=False)                                       # the ankle cuffs
    # team polka dots on the trousers
    def dots(sd, s):
        x = sd * 130
        plate(ch, "thigh" + s, "team", ngon(x + sd * 22, 740, 62, 4, 0), -96, -84)
        plate(ch, "shin" + s, "team", ngon(x - sd * 16, 320, 56, 4, 0), -96, -84)
    ch.sym(dots)
    # two white pompoms
    for y in (1120, 1260):
        plate(ch, "torso", "white", ngon(0, y, 40, 4, 0), -172, -150)
    # the big white ruffle round the neck (its outline under the neck line)
    ch.sweep("torso", "white", [(star(296, 205, 6, phase=math.pi / 2), 1372), (star(236, 170, 6, phase=math.pi / 2), 1428)])
    ch.sym(lambda sd, s: puff(ch, "team", sd))                                       # the puffed shoulders
    # the head: a round white face
    n = 1440
    ch.sweep("head", "white", [(ring(112, 112), n - 20), (ring(160, 158, c=66), n + 160), (ring(92, 92, c=38), 1722)])
    blob(ch, "head", "glove", 0, 1565, -148, 38, 34, shell=True)                       # the red nose
    plate(ch, "head", "glove", [(-80, 1512), (0, 1488), (80, 1512), (0, 1502)], -162, -150)   # the grin
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 28, 1610), (sd * 62, 1610), (sd * 58, 1634), (sd * 32, 1634)], -164, -152))
    ch.sym(lambda sd, s: plate(ch, "head", "team", [(sd * 22, 1652), (sd * 72, 1660), (sd * 68, 1676), (sd * 26, 1668)], -160, -146))   # brows
    # the pink hair tufts on each side
    ch.sym(lambda sd, s: ch.sweep("head", "pink", [(ngon(16, 1596, 96, 4, 0, 0.9, 1.0), sd * 118), (ngon(16, 1604, 50, 4, 0, 0.8, 1.0), sd * 170)], "x"))
    # the tiny hat, cocked to one side, with a team band
    with ch.placed("head", rotz(-0.22), (0, 1715, 0)):
        ch.loft("head", "dark", ngon(0, 0, 70, 6), 1705, ngon(0, 0, 66, 6), 1722)
        ch.sweep("head", "dark", [(ngon(0, 0, 46, 6), 1720), (ngon(0, 0, 40, 6), 1792)])
        ch.sweep("head", "team", [(ngon(0, 0, 51, 6), 1722), (ngon(0, 0, 47, 6), 1748)], shell=False)
    return ch
