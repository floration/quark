"""PLAGUEDOC: the plague doctor (box design: characters_outlined_extra2.plaguedoc)."""
from players_lowpoly.kit import Char, Rig, Segment, body, band, plate, brim, ring, rrect, ngon, FRONT_WRAP

DESC = "the plague doctor: a long ivory beak mask with round glass eyes, a wide-brim dark hat, a long team-dark coat to the shins, a short shoulder cape"


def build():
    ch = Char("PLAGUEDOC", DESC, Rig(hip=900, hipx=130, knee=450, ankle=90))
    # the coat: its skirt flares down the thighs and hangs on to the shins; dark legs and boots under it
    b = body(ch, legs="team_dk", shins="dark", feet="dark", pelvis="team_dk", torso="team_dk", head=None,
             thigh_mid=False, calf=False, foot=(64, 180, 88), thigh=(86, 88, 92, 94), shin=(68, 70, 70, 66),
             pelvis_w=(190, 215), pelvis_d=(126, 136), waist=(200, 124), chest=(222, 138), shoulder=(180, 110), torso_top=1380)
    b.wrap("shin_", 250, 470, "team_dk", grow=16)                            # the coat's hem
    b.wrap("torso", 960, 1010, "leather", grow=8, shell=False)               # the belt
    plate(ch, "torso", "gold", [(-28, 968), (28, 968), (28, 1002), (-28, 1002)], -142, -126)
    for y in (1080, 1150):                                                   # the coat's buttons
        plate(ch, "torso", "gold", [(-15, y), (15, y), (15, y + 28), (-15, y + 28)], -150, -128)
    # the short shoulder cape: a wide faceted bell over the shoulders, its collar up round the neck (mid_dk, a step
    # lighter than the box's dark: with the team-dark coat it kept the dark-blue backdrop's contrast under dE 35)
    ch.sweep("torso", "mid_dk", [(ring(362, 168, c=110), 1215), (ring(262, 158, c=86), 1360), (ring(118, 100, c=44), 1452)])
    # the hood, the ivory mask over its face
    hs = Segment(ch, "head", [(ring(98, 98, c=40), 1430), (ring(118, 118, c=48), 1500), (ring(114, 114, c=48), 1650)])
    ch.sweep("head", "dark", hs.rings)
    b.seg["head"] = hs
    band(ch, b, "head", "ivory", 1500, 1640, FRONT_WRAP, grow=6, depth=14)
    # the long beak, curving down to its tip
    ch.sweep("head", "ivory", [(rrect(-10, 1452, 10, 1470, 6), -184), (rrect(-36, 1462, 36, 1530, 14), -152), (rrect(-60, 1470, 60, 1584, 22), -112)], "z")
    # the round glass eyes
    ch.sym(lambda sd, s: ch.solid("head", "glass", ngon(sd * 52, 1598, 28, 6), "z", -134, -120, shell=False))
    # the wide-brim hat, its crown and team band
    brim(ch, "dark", 1640, 96, 170, thick=14, k=8)
    cr = Segment(ch, "head", [(ngon(0, 0, 104, 8), 1650), (ngon(0, 0, 92, 8), 1792)])
    ch.sweep("head", "dark", cr.rings)
    cr.wrap(1656, 1694, "team", grow=6, shell=False)
    return ch
