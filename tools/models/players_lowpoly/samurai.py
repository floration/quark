"""SAMURAI: the samurai (box design: characters_outlined_extra.samurai)."""
import math

from players_lowpoly.kit import Char, Rig, Segment, body, visor, plate, hexring, rotz

DESC = "the samurai: red lacquer armour with team lacing, big shoulder plates and skirt, a horned gold crest and a team banner on the back"


def arc(r_out, r_in, a0, a1, n, sz=1.0):
    """a thick arc (x, z) round the y axis from angle a0 to a1 (radians; 0 = +x, pi/2 = the back, +z): n points out,
    n back in (the helmet's neck guard)"""
    out = [(r_out * math.cos(a0 + (a1 - a0) * i / (n - 1)), sz * r_out * math.sin(a0 + (a1 - a0) * i / (n - 1))) for i in range(n)]
    inn = [(r_in * math.cos(a1 - (a1 - a0) * i / (n - 1)), sz * r_in * math.sin(a1 - (a1 - a0) * i / (n - 1))) for i in range(n)]
    return out + inn


def build():
    ch = Char("SAMURAI", DESC, Rig(hip=910, hipx=130, knee=460, ankle=90))
    # team hakama trousers (the box's are grey: team here so the legs read), a dark helmet for the head with the face
    # laid on its front
    b = body(ch, legs="team", shins="team", feet="dark", pelvis="dark", torso="lacquer", head="dark",
             thigh=(86, 86, 74, 76), shin=(70, 74, 74, 66), foot=(62, 175, 85), thigh_mid=False, calf=False,
             waist=(195, 122), chest=(222, 140), shoulder=(196, 116), chest_y=1250,
             head_w=150, head_d=138, head_top=1750, jaw=0.84, crown=0.74)
    # the lacquer shin guards (suneate)
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "lacquer", [(sd * 130 - 52, 130), (sd * 130 + 52, 130), (sd * 130 + 56, 440), (sd * 130 - 56, 440)], -84, -58))

    # the kusazuri: a flared lacquer skirt plate on each thigh, laced team at its hem
    def skirt(sd, s):
        x = sd * 130
        sk = Segment(ch, "thigh" + s, [(hexring(118, 116, x + sd * 8), 610), (hexring(102, 104, x), 905)])
        ch.sweep("thigh" + s, "lacquer", sk.rings)
        sk.wrap(610, 672, "team", grow=5, shell=False)
    ch.sym(skirt)
    # the cuirass: team lace rows and the team obi
    b.wrap("torso", 1090, 1135, "team", grow=4, shell=False)
    b.wrap("torso", 1275, 1320, "team", grow=4, shell=False)
    b.wrap("torso", 932, 1030, "team", grow=8, shell=False)

    # the sode: big flat lacquer shoulder plates hanging off the shoulders, tipped out, a team lace at the hem
    def sode(sd, s):
        x = sd * 290
        top = [(x - 16, -112), (x + 16, -112), (x + 16, 112), (x - 16, 112)]
        with ch.placed("torso", rotz(sd * 0.2), (x, 1410, 0)):
            ch.sweep("torso", "lacquer", [(top, 1090), (top, 1405)])
            ch.sweep("torso", "team", [([(u + 5 * (1 if u > x else -1), v * 1.04) for u, v in top], 1076),
                                       ([(u + 5 * (1 if u > x else -1), v * 1.04) for u, v in top], 1165)], shell=False)
    ch.sym(sode)
    # the banner (sashimono): a pole up the back and its team flag above the shoulders
    ch.rod("torso", "dark", (0, 1040, 168), (0, 1790, 168), 15, k=4)
    plate(ch, "torso", "team", [(-94, 1250), (94, 1250), (94, 1760), (-94, 1760)], 146, 156, shell=True)
    # the face between the mask (the helmet's own dark below) and the brow, the eyes
    visor(ch, b, "skin", 1552, 1650, grow=3, depth=12)
    plate(ch, "head", "eye", [(-62, 1578), (62, 1578), (62, 1598), (-62, 1598)], -150, -138)
    # the helmet's team band, the flared neck guard (shikoro)
    b.wrap("head", 1652, 1686, "team", grow=6, shell=False)
    ch.sweep("head", "dark", [(arc(174, 152, -0.25, math.pi + 0.25, 3, 0.92), 1515), (arc(156, 136, -0.25, math.pi + 0.25, 3, 0.92), 1650)])
    # the gold kuwagata: a V of horns off the helmet's brow
    plate(ch, "head", "gold", [(-22, 1645), (22, 1645), (112, 1790), (90, 1802), (0, 1688), (-90, 1802), (-112, 1790)], -156, -144)
    return ch
