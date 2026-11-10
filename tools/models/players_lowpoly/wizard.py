"""WIZARD: the wizard (box design: characters_outlined.wizard)."""
import math

from players_lowpoly.kit import Char, Rig, body, pauldron, band, plate, brim, ngon, ring, rotx, ccw, BACK_WRAP

DESC = "the wizard: a long purple robe with team panels, a white beard, a tall pointed hat with a team band and a gold star"


def star(cx, cy, r, ri):
    """a four-pointed star (x, y)"""
    return [(cx + (r if i % 2 == 0 else ri) * math.sin(math.pi * i / 4), cy + (r if i % 2 == 0 else ri) * math.cos(math.pi * i / 4)) for i in range(8)]


def build():
    ch = Char("WIZARD", DESC, Rig(hip=880, hipx=125, knee=460, ankle=80))
    # the robe: a skirt over each leg that flares to the hem (the legs walk inside it), the shoes peeking out
    b = body(ch, legs="robe", shins="robe", feet="leather", pelvis="robe", torso="robe", head=None,
             thigh=(92, 100, 102, 108), shin=(104, 110, 112, 116), thigh_mid=False, calf=False, foot=(60, 165, 70),
             pelvis_w=(200, 205), pelvis_d=(128, 132), waist=(205, 132), chest=(222, 140), shoulder=(205, 122))
    b.wrap("shin_", 80, 175, "team", grow=6, shell=False)                                       # the hem bands
    # the team panel down the front from the collar to the hem, the sash, the back panel, the mantle
    # (a strip that follows the robe's front, narrow at the collar and widening to the hem like the robe's opening)
    fz = [(470, 134, 128), (1250, 152, 90), (1408, 122, 80)]      # (y, the front's depth, half width)
    ch.sweep("torso", "team", [(ccw([(-w, -f - 8), (w, -f - 8), (w, -f + 14), (-w, -f + 14)]), y) for y, f, w in fz], shell=False)
    b.wrap("torso", 920, 985, "team", grow=9, shell=False)
    band(ch, b, "torso", "team", 990, 1370, BACK_WRAP, grow=6, depth=12)
    b.wrap("torso", 1355, 1418, "team", grow=8, shell=False)
    # wide robe sleeves with team cuffs
    pauldron(ch, "robe", x=272, y0=1140, w=88, d=100, tilt=0.22, band="team")
    # the face (a faceted block under the hat), the eyes, and the long white beard down onto the chest
    ch.solid("head", "skin", ring(122, 122, 36), "y", 1430, 1630)
    plate(ch, "head", "eye", [(-60, 1556), (60, 1556), (60, 1578), (-60, 1578)], -130, -120)
    ch.loft("head", "beard", [(-55, -140), (55, -140), (40, -70), (-40, -70)], 1250,
            [(-122, -138), (122, -138), (130, -60), (-130, -60)], 1550, "y")
    # the hat: a wide brim, the team band, a tall cone bending back, the gold star on its front
    brim(ch, "robe", 1612, 120, 182, thick=14, k=8)
    ch.solid("head", "team", ngon(0, 0, 128, 6, phase=0.5236), "y", 1620, 1668, shell=False)
    ch.sweep("head", "robe", [(ngon(0, 0, 118, 6, phase=0.5236), 1668), (ngon(0, 14, 92, 6, phase=0.5236), 1720), (ngon(0, 42, 58, 6, phase=0.5236), 1768), (ngon(0, 92, 10, 6, phase=0.5236), 1812)])
    with ch.placed("head", rotx(-0.45), (0, 1694, -106)):
        plate(ch, "head", "gold", star(0, 1694, 34, 13)[::2], -112, -100)
    return ch
