"""RUNNER: the runner (box design: characters_outlined_extra2.runner)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, helmet, plate, ring

DESC = "the runner: a very lean sprinter in a team tank top and split shorts, a cap with a long bill, a headset with a mic arm, sweatbands, white racing shoes"


def build():
    ch = Char("RUNNER", DESC, Rig(hip=900, hipx=120, knee=470, ankle=50))
    b = body(ch, legs="skin", shins="skin", feet="white", pelvis="team", torso="team", head="skin", calf=False, thigh_mid=False,
             thigh=(72, 72, 64, 70), shin=(64, 70, 68, 66), foot=(62, 185, 70), pelvis_w=(150, 172), pelvis_d=(100, 112),
             waist=(165, 104), chest=(200, 120), shoulder=(178, 104), chest_y=1240, head_w=148, head_d=132, head_top=1768, jaw=0.72, crown=0.74)
    # the split shorts and the ankle socks
    b.wrap("thigh_", 650, 905, "team", grow=9)
    b.wrap("shin_", 40, 150, "team", grow=6, shell=False)
    # the tank top: a white stripe round it, a white race bib
    b.wrap("torso", 1110, 1150, "white", grow=5, shell=False)
    plate(ch, "torso", "white", [(-75, 1180), (75, 1180), (75, 1300), (-75, 1300)], -140, -126)
    # bare shoulders with team sweatbands
    pauldron(ch, "skin", x=272, y0=1185, w=84, d=84, tilt=0.16, band="team")
    # the face: the eyes, a white headband under the team cap, its long bill
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 30, 1585), (sd * 62, 1585), (sd * 62, 1602), (sd * 30, 1602)], -134, -126))
    b.wrap("head", 1630, 1672, "white", grow=5, shell=False)
    helmet(ch, b, "team", 1680, grow=8, top=8)
    ch.loft("head", "team_dk", [(-84, -110), (84, -110), (38, -180), (-38, -180)], 1664, [(-82, -110), (82, -110), (36, -174), (-36, -174)], 1682, "y")
    # the headset: an earpiece and the mic arm round to the mouth
    ch.solid("head", "dark", [(-35, 1545), (35, 1545), (35, 1615), (-35, 1615)], "x", 148, 166, shell=False)
    ch.rod("head", "dark", (150, 1560, -30), (60, 1515, -132), 9, k=4, shell=False)
    return ch
