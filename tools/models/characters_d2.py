"""The 11 player designs for gen_players_realistic_dark.py: stylized, chunky, faceted low poly (at most 1000 triangles
a model, owner 2026-11-05), broad colour blocks with the D2 baked shading (rim, worn edges, AO) doing the detail.

Every model: feet on the floor at the origin, facing -Z, +X its right; standing pose. Each part names the joint it
moves with (Char.add(..., bone)); the joints are the shared Rig (hip 0.90 m, knee 0.48 m, ankle 0.11 m, neck 1.45 m)
unless a design moves them. Measurements in metres. Hero proportions: big boots, shoulders and heads, a broad chest,
a narrow waist. The game draws the gun arm (prop GUN_ARM) from the right shoulder and the grapple arm from the left,
so the bodies have shoulder pads and no arms (but the zombie's and the fighter's, which are who they are).
Team colour is an accent (stripes, trims, bands, panels); every walking model also gets team stripes down its legs
(gen_players_realistic_dark.py leg_stripes).
"""
import math

import numpy as np

from gen_players_realistic_dark import (BLOOD, CREAM, EYE_DK, GOLD, HAIR_DK, LEATHER, LEATHER_DK, NINJA_CLOTH, RED,  # noqa: F401
                                        ROT, SKIN, SKIN_ZOMBIE, TAPE, WHITE, Char, Look, Rig, alloy, brass, chamfer_rect, cloth,
                                        extrude_y, front_plate, glow, gunmetal, leather, lens, lin3, loft_y, polymer, recess, rot,
                                        poly_offset, rubber, shell, skin, sweep, team, tube, xf)

PI = math.pi


# ---------------------------------------------------------------------------------------------------------------
# faceted primitives (flat shaded, few sides)

def prism(st, k=8, n=2.0, **kw):
    """a faceted loft up y: stations (y, a, b[, cx, cz]) or dicts; a face toward -Z"""
    return loft_y(st, k=k, n=n, mode="flat", phase=PI / k, **kw)


def box(c, h):
    """a plain box: 12 triangles"""
    c, h = np.asarray(c, float), np.asarray(h, float)
    poly = [(c[0] + h[0], c[2] - h[2]), (c[0] + h[0], c[2] + h[2]), (c[0] - h[0], c[2] + h[2]), (c[0] - h[0], c[2] - h[2])]
    return extrude_y(poly, c[1] - h[1], c[1] + h[1])


def blk(c, h, bev=0.02, cut=None):
    """a chunky block: an octagon (corners cut by `cut`) up y, its top edge chamfered (the edge the key light and the
    wear catch): 44 triangles"""
    c, h = np.asarray(c, float), np.asarray(h, float)
    cut = min(h[0], h[2]) * 0.35 if cut is None else cut
    bev = min(bev, h[1] * 0.45, cut * 0.9)
    poly = np.array([(c[0] + x, c[2] + z) for x, z in chamfer_rect(h[0], h[2], cut)])
    ins = poly_offset(poly, bev)
    rg = lambda y, pp: np.stack([pp[:, 0], np.full(len(pp), y), pp[:, 1]], 1)
    return sweep([rg(c[1] - h[1], poly), rg(c[1] + h[1] - bev, poly), rg(c[1] + h[1], ins)], "flat")


def ring(y0, y1, a, b, k=8, cx=0.0, cz=0.0, caps=False, **kw):
    """a band (belt, trim, hat band) around something: 2 rings, open ends (what it wraps fills them)"""
    return prism([(y0, a, b, cx, cz), (y1, a, b, cx, cz)], k=k, cap0=caps, cap1=caps, **kw)


def turn(mesh, axis, a, about):
    about = np.asarray(about, float)
    return xf(xf(mesh, t=-about), rot(axis, a), about)


def L(sd):
    return "_l" if sd < 0 else "_r"


def chrome():
    return Look(lin3(0.34, 0.35, 0.37), (0.92, 0.95, 1.0), 0.7, env=4.0, key=1.5, rim=0.5, grad=0.2, kp=30)


# ---------------------------------------------------------------------------------------------------------------
# shared body parts

def legs(ch, mat, look, w=1.0, shin_mat=None, shin_look=None):
    """6-sided thighs (hip to just under the knee) and shins (just over the knee to the ankle)"""
    r = ch.rig
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(prism([(r.kneeY - 0.04, 0.056 * w, 0.06 * w, x), (0.70, 0.08 * w, 0.085 * w, x), (r.hipY + 0.05, 0.086 * w, 0.09 * w, x)], k=6), mat, look, "thigh" + L(sd))
        ch.add(prism([(r.ankleY - 0.01, 0.05 * w, 0.054 * w, x), (0.31, 0.066 * w, 0.07 * w, x), (r.kneeY + 0.045, 0.054 * w, 0.058 * w, x)], k=6),
               shin_mat or mat, shin_look or look, "shin" + L(sd))


def boots(ch, mat, look, s=1.0, shaft=0.27, sole=False, toe=0.0):
    """big boots: a chunky foot block on the foot, its shaft up the shin"""
    r = ch.rig
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(blk((x, 0.062, -0.045 - toe), (0.072 * s, 0.062, 0.155 * s), 0.024), mat, look, "foot" + L(sd))
        if sole:
            ch.add(box((x, 0.012, -0.045 - toe), (0.075 * s, 0.012, 0.158 * s)), "rubber_sole", rubber(), "foot" + L(sd))
        if shaft:
            ch.add(prism([(r.ankleY + 0.02, 0.066 * s, 0.07 * s, x), (shaft, 0.07 * s, 0.074 * s, x)], k=6), mat, look, "shin" + L(sd))


def hips(ch, mat, look, a=0.2, b=0.13, y0=0.8, y1=0.98, bone="pelvis", k=8, **kw):
    ch.add(prism([(y0, a * 0.92, b * 0.95), (y1, a, b)], k=k, **kw), mat, look, bone)


def torso(ch, mat, look, s=1.0, sz=1.0, st=None, k=8, **kw):
    """the hero chest: a narrow waist, a broad chest, sloped shoulders"""
    st = st or [(0.94, 0.19, 0.13), (1.12, 0.235, 0.15), (1.30, 0.29, 0.16), (1.39, 0.24, 0.13), (1.43, 0.11, 0.08)]
    ch.add(prism([(y, a * s, b * sz) + tuple(rest) for y, a, b, *rest in st], k=k, **kw), mat, look, "torso")


def pads(ch, mat, look, x=0.29, y=1.33, s=1.0, rim=True, k=6):
    """big shoulder pads tipped outward, a team rim under each"""
    for sd in (-1, 1):
        cx = sd * x
        dome = prism([(y - 0.075, 0.095 * s, 0.11 * s, cx), (y + 0.02, 0.105 * s, 0.12 * s, cx), (y + 0.075, 0.06 * s, 0.07 * s, cx)], k=k)
        ch.add(turn(dome, (0, 0, 1), -sd * 0.4, (cx, y, 0)), mat, look, "torso")
        if rim:
            band = ring(y - 0.085, y - 0.06, 0.1 * s, 0.115 * s, k=k, cx=cx)
            ch.add(turn(band, (0, 0, 1), -sd * 0.4, (cx, y, 0)), "team_rim", team(1.15, 0.3), "torso")


def head(ch, mat, look, s=1.0, off=(0, 0, 0), eyes=True, eye_mat="poly_eyes", eye_look=None, top=1.75, k=8):
    """a big faceted head; two block eyes"""
    ox, oy, oz = off
    ch.add(prism([(1.465 + oy, 0.06 * s, 0.065 * s, ox, -0.03 + oz), (1.52 + oy, 0.095 * s, 0.105 * s, ox, -0.01 + oz),
                   (1.64 + oy, 0.1 * s, 0.11 * s, ox, oz), (1.71 + oy, 0.085 * s, 0.095 * s, ox, 0.005 + oz), (top + oy, 0.0, 0.0, ox, 0.005 + oz)], k=k),
           mat, look, "head")
    if eyes:
        for sd in (-1, 1):
            ch.add(box((ox + sd * 0.038, 1.61 + oy, oz - 0.11 * s * 0.924 - 0.003), (0.018, 0.011, 0.004)), eye_mat, eye_look or polymer(EYE_DK), "head")


def neck(ch, mat, look, r=0.055):
    ch.add(prism([(1.39, r, r), (1.49, r, r)], k=6), mat, look, "head")


def belt(ch, y, a, b, h, mat, look, buckle=None, bone="torso", tilt=0.0, k=8):
    wave = (lambda th: tilt * np.cos(th)) if tilt else None
    ch.add(ring(y - h, y + h, a, b, k=k, ywave=wave), mat, look, bone)
    if buckle:
        bm, bl, bw = buckle
        ch.add(box((0, y, -b - 0.004), (bw, h * 1.3, 0.008)), bm, bl, bone)


# ---------------------------------------------------------------------------------------------------------------

def standard():
    ch = Char("STANDARD", "soldier: olive fatigues, a chunky plate carrier and pack, big boots, a helmet with a team band and goggles", Rig(hipX=0.13))
    fat = cloth(lin3(0.27, 0.29, 0.23), 0.25)
    legs(ch, "cloth_fatigues", fat)
    boots(ch, "leather_boot", leather(LEATHER_DK))
    r = ch.rig
    for sd in (-1, 1):
        ch.add(blk((sd * r.hipX, r.kneeY + 0.01, -0.066), (0.055, 0.055, 0.022), 0.012), "poly_kneepad", polymer(), "shin" + L(sd))
    hips(ch, "cloth_fatigues", fat)
    torso(ch, "cloth_shirt", cloth(lin3(0.24, 0.26, 0.21), 0.2))
    ch.add(blk((0, 1.15, -0.065), (0.24, 0.2, 0.11), 0.03), "poly_carrier", polymer(), "torso")
    ch.add(box((0, 1.26, -0.177), (0.08, 0.035, 0.004)), "team_patch", team(1.0, 0.3), "torso")
    ch.add(blk((0, 1.02, -0.18), (0.13, 0.05, 0.025), 0.012), "poly_pouch", polymer(lin3(0.3, 0.29, 0.27)), "torso")
    ch.add(blk((0, 1.16, 0.19), (0.16, 0.18, 0.06), 0.03), "poly_pack", polymer(), "torso")
    ch.add(box((0, 1.25, 0.252), (0.07, 0.03, 0.004)), "team_patch", team(1.0, 0.3), "torso")
    pads(ch, "poly_pad", polymer())
    belt(ch, 0.92, 0.205, 0.137, 0.025, "poly_belt", polymer(lin3(0.22, 0.21, 0.195)), ("nitride_buckle", gunmetal(1.2), 0.03))
    neck(ch, "cloth_shirt", cloth(lin3(0.24, 0.26, 0.21), 0.2))
    head(ch, "skin_face", skin())
    rimw = lambda th: -0.03 * np.sin(th)
    ch.add(prism([dict(y=1.6, a=0.118, b=0.128, ywave=rimw), (1.68, 0.122, 0.132), (1.75, 0.095, 0.105), (1.785, 0.0, 0.0)], k=8), "nitride_helmet", gunmetal(1.2), "head")
    ch.add(ring(1.665, 1.69, 0.124, 0.134), "team_helmetband", team(1.05, 0.3), "head")
    ch.add(shell([(1.6, 0.112, 0.12), (1.64, 0.114, 0.122)], -PI + 0.8, -0.8, thick=0.012, k=4, mode="flat"), "lens_goggles", lens(), "head")
    return ch


def heavy():
    ch = Char("HEAVY", "armoured trooper: huge gunmetal pauldrons, a slab chest plate with team chevrons, a power pack, a T-visor helmet", Rig(hipX=0.14))
    gm, us = gunmetal(1.15), polymer(lin3(0.24, 0.24, 0.255))
    legs(ch, "poly_undersuit", us, w=1.12)
    r = ch.rig
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(blk((x, 0.70, -0.02), (0.09, 0.13, 0.08), 0.025), "nitride_cuisse", gm, "thigh" + L(sd))
        ch.add(blk((x, 0.29, -0.02), (0.08, 0.14, 0.078), 0.025), "nitride_greave", gm, "shin" + L(sd))
    boots(ch, "nitride_boot", gm, s=1.12, shaft=0)
    hips(ch, "poly_undersuit", us, a=0.215, b=0.14)
    ch.add(blk((0, 0.86, -0.01), (0.22, 0.07, 0.15), 0.025), "nitride_tasset", gm, "pelvis")
    torso(ch, "poly_undersuit", us, s=1.05, sz=1.05)
    ch.add(blk((0, 1.17, -0.02), (0.27, 0.21, 0.165), 0.04, cut=0.08), "nitride_chest", gm, "torso")
    for sd in (-1, 1):
        ch.add(turn(box((sd * 0.1, 1.2, -0.187), (0.085, 0.02, 0.005)), (0, 0, 1), sd * 0.45, (sd * 0.1, 1.2, -0.187)), "team_chevron", team(1.1, 0.35), "torso")
    ch.add(blk((0, 1.15, 0.22), (0.19, 0.18, 0.07), 0.03), "nitride_pack", gunmetal(0.95), "torso")
    for x in (-0.08, 0.0, 0.08):
        ch.add(box((x, 1.15, 0.292), (0.014, 0.11, 0.004)), "glow_vent", glow((1.6, 0.8, 0.25)), "torso")
    pads(ch, "nitride_pauldron", gm, x=0.28, y=1.33, s=1.22)
    belt(ch, 0.92, 0.225, 0.158, 0.026, "rubber_belt", rubber(), ("team_buckle", team(1.1, 0.4), 0.04))
    ch.add(prism([(1.45, 0.11, 0.12, 0, -0.005), (1.53, 0.14, 0.15), (1.66, 0.145, 0.155), (1.75, 0.115, 0.125), (1.79, 0.0, 0.0)], k=8), "nitride_helmet", gm, "head")
    ch.add(box((0, 1.635, -0.146), (0.08, 0.016, 0.005)), "lens_visor", lens(), "head")
    ch.add(box((0, 1.575, -0.146), (0.018, 0.045, 0.005)), "lens_visor", lens(), "head")
    ch.add(ring(1.45, 1.47, 0.116, 0.126), "team_helmetrim", team(1.1, 0.3), "head")
    ch.add(box((0, 1.765, -0.04), (0.015, 0.025, 0.07)), "team_crest", team(1.1, 0.35), "head")
    return ch


def simple():
    ch = Char("SIMPLE", "the hitbox, deliberately: the body and head cylinders (bevelled, team), a gunmetal visor band with a lens slit and a waist band; still", Rig(animated=False))
    R, h = 0.392, 0.172
    ch.add(loft_y([(0.0, R - 0.03, R - 0.03), (0.03, R, R), (1.397, R, R), (1.427, R - 0.03, R - 0.03)], k=16, n=2.0, mode="flat", phase=PI / 16),
           "team_body", team(0.85, 0.3), "root")
    ch.add(loft_y([(1.448, h - 0.02, h - 0.02), (1.468, h, h), (1.774, h, h), (1.794, h - 0.02, h - 0.02)], k=12, n=2.0, mode="flat", phase=PI / 12),
           "team_head", team(1.05, 0.3), "root")
    ch.add(ring(1.42, 1.455, 0.12, 0.12, k=8), "alloy_neck", alloy(), "root")
    ch.add(ring(1.595, 1.67, h + 0.002, h + 0.002, k=12), "nitride_visor", gunmetal(1.1), "root")
    ch.add(shell([(1.622, h + 0.004, h + 0.004), (1.643, h + 0.004, h + 0.004)], -PI + 0.75, -0.75, thick=0.004, k=5, n=2.0, mode="flat"), "lens_visor", lens(), "root")
    ch.add(ring(0.85, 0.89, R + 0.0025, R + 0.0025, k=16), "nitride_band", gunmetal(1.2), "root")
    return ch


def robot():
    ch = Char("ROBOT", "machine: blocky gunmetal frame on piston legs, a team chest panel with a glowing core, a big box head with a glowing eye bar", Rig(hipX=0.14, ankleY=0.13))
    r = ch.rig
    gm, al = gunmetal(1.2), alloy()
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(blk((x, 0.69, 0.0), (0.065, 0.16, 0.07), 0.02), "nitride_thigh", gm, "thigh" + L(sd))
        ch.add(prism([(r.kneeY - 0.035, 0.05, 0.05, x), (r.kneeY + 0.035, 0.05, 0.05, x)], k=6), "alloy_knee", al, "shin" + L(sd))
        ch.add(blk((x, 0.29, 0.01), (0.06, 0.15, 0.065), 0.02), "nitride_shin", gm, "shin" + L(sd))
        ch.add(prism([(0.17, 0.015, 0.015, x, -0.075), (0.42, 0.015, 0.015, x, -0.075)], k=4), "alloy_piston", chrome(), "shin" + L(sd))
        ch.add(blk((x, 0.06, -0.04), (0.08, 0.055, 0.16), 0.02), "nitride_foot", gm, "foot" + L(sd))
    ch.add(blk((0, 0.88, 0), (0.19, 0.065, 0.12), 0.02), "nitride_pelvis", gm, "pelvis")
    ch.add(prism([(0.93, 0.08, 0.08), (1.04, 0.08, 0.08)], k=6), "alloy_spine", al, "torso")
    ch.add(blk((0, 1.24, 0.0), (0.27, 0.17, 0.17), 0.04, cut=0.07), "nitride_chest", gm, "torso")
    ch.add(box((0, 1.24, -0.172), (0.13, 0.09, 0.004)), "team_panel", team(0.9, 0.4), "torso")
    ch.add(prism([(-0.018, 0.045, 0.045), (0.018, 0.045, 0.045)], k=6), "team_core", glow((2.4, 2.4, 2.4)), "torso")
    ch.parts[-1][0] = xf(ch.parts[-1][0], R=np.array([[1, 0, 0], [0, 0, -1], [0, 1, 0]], float), t=(0, 1.24, -0.178))
    ch.add(blk((0, 1.24, 0.19), (0.16, 0.12, 0.03), 0.012), "nitride_back", gm, "torso")
    for x in (-0.06, 0.06):
        ch.add(box((x, 1.24, 0.222), (0.018, 0.09, 0.004)), "glow_vent", glow((1.6, 0.8, 0.25)), "torso")
    for sd in (-1, 1):
        ch.add(blk((sd * 0.29, 1.36, 0.0), (0.075, 0.05, 0.1), 0.018), "nitride_shoulder", gm, "torso")
        ch.add(box((sd * 0.29, 1.315, 0.0), (0.078, 0.007, 0.103)), "team_rim", team(1.15, 0.3), "torso")
    ch.add(prism([(1.41, 0.05, 0.05), (1.49, 0.05, 0.05)], k=6), "alloy_neck", al, "head")
    ch.add(blk((0, 1.6, 0.0), (0.135, 0.11, 0.12), 0.03), "nitride_head", gunmetal(1.35), "head")
    ch.add(box((0, 1.615, -0.121), (0.1, 0.03, 0.004)), "nitride_eyerecess", recess(), "head")
    ch.add(box((0, 1.615, -0.126), (0.085, 0.016, 0.004)), "team_eye", glow((2.4, 2.4, 2.4)), "head")
    ch.add(box((0, 1.495, 0.0), (0.13, 0.008, 0.115)), "team_headrim", team(1.15, 0.3), "head")
    ch.add(prism([(1.71, 0.006, 0.006, 0.07, 0.04), (1.775, 0.006, 0.006, 0.07, 0.04)], k=4), "alloy_antenna", al, "head")
    ch.add(box((0.07, 1.783, 0.04), (0.012, 0.012, 0.012)), "team_tip", glow((2.2, 2.2, 2.2)), "head")
    return ch


def knight():
    ch = Char("KNIGHT", "knight: gunmetal plate, a big great helm with a visor slit and team crest, a team surcoat with a cross, huge pauldrons, a sword at the hip",
              Rig(hipX=0.13))
    r = ch.rig
    gm = gunmetal(1.3)
    legs(ch, "nitride_leg", gm, w=1.05)
    for sd in (-1, 1):
        ch.add(blk((sd * r.hipX, r.kneeY, -0.05), (0.06, 0.06, 0.035), 0.015), "nitride_poleyn", gm, "shin" + L(sd))
    boots(ch, "nitride_sabaton", gm, shaft=0.2, toe=0.01)
    hips(ch, "alloy_mail", alloy(), a=0.21, b=0.145, y0=0.7)
    torso(ch, "nitride_breastplate", gm, s=1.02, sz=1.06)
    sur = team(0.85, 0.08)
    ch.add(box((0, 1.0, -0.15), (0.13, 0.16, 0.006)), "team_surcoat", sur, "torso")
    ch.add(box((0, 1.0, 0.15), (0.13, 0.16, 0.006)), "team_surcoat", sur, "torso")
    for sd in (-1, 1):   # the split skirt: a flap on each thigh
        ch.add(box((sd * r.hipX, 0.72, -0.098), (0.075, 0.13, 0.006)), "team_surcoat", sur, "thigh" + L(sd))
    ch.add(box((0, 1.02, -0.157), (0.02, 0.1, 0.002)), "cloth_cross", cloth(CREAM, 0.1), "torso")
    ch.add(box((0, 1.05, -0.157), (0.06, 0.018, 0.002)), "cloth_cross", cloth(CREAM, 0.1), "torso")
    pads(ch, "nitride_pauldron", gm, x=0.285, s=1.18)
    belt(ch, 0.9, 0.215, 0.157, 0.022, "leather_belt", leather(), ("brass_buckle", brass(), 0.032))
    ch.add(prism([(0.47, 0.012, 0.022, -0.24, 0.09), (0.88, 0.015, 0.025, -0.24, 0.02)], k=4), "leather_scabbard", leather(LEATHER_DK), "pelvis")
    ch.add(box((-0.24, 0.9, 0.02), (0.014, 0.012, 0.06)), "brass_guard", brass(), "pelvis")
    ch.add(prism([(0.91, 0.012, 0.012, -0.24, 0.02), (1.0, 0.012, 0.012, -0.24, 0.02)], k=4), "leather_grip", leather(), "pelvis")
    ch.add(prism([(1.44, 0.11, 0.12), (1.56, 0.128, 0.138), (1.70, 0.125, 0.135), (1.755, 0.08, 0.09), (1.77, 0.0, 0.0)], k=8), "nitride_helm", gm, "head")
    ch.add(box((0, 1.62, -0.128), (0.085, 0.012, 0.004)), "nitride_slit", recess(), "head")
    ch.add(box((0, 1.56, -0.129), (0.008, 0.05, 0.004)), "nitride_ridge", gunmetal(1.6), "head")
    ch.add(ring(1.44, 1.46, 0.114, 0.124), "team_helmrim", team(1.1, 0.3), "head")
    ch.add(prism([dict(y=1.75, a=0.016, b=0.11), dict(y=1.795, a=0.012, b=0.09)], k=6), "team_crest", team(1.05, 0.1), "head")
    return ch


def wizard():
    ch = Char("WIZARD", "wizard: a flowing violet robe with team trims, big sleeves, a gold sash, a long white beard, a huge pointed hat with a team band and a glowing star",
              Rig(hipX=0.11))
    r = ch.rig
    robe, robe_hi, trim = cloth(lin3(0.19, 0.16, 0.34), 0.2), cloth(lin3(0.22, 0.19, 0.39), 0.2), team(1.0, 0.2)
    for sd in (-1, 1):   # legs under the robe; the shoes peek out
        x = sd * r.hipX
        ch.add(prism([(r.kneeY - 0.04, 0.05, 0.055, x), (r.hipY + 0.04, 0.07, 0.075, x)], k=6), "cloth_robe", robe, "thigh" + L(sd))
        ch.add(prism([(r.ankleY, 0.045, 0.05, x), (r.kneeY + 0.04, 0.05, 0.055, x)], k=6), "cloth_robe", robe, "shin" + L(sd))
        ch.add(blk((x, 0.045, -0.06), (0.05, 0.045, 0.12), 0.015), "leather_shoe", leather(), "foot" + L(sd))
    hem = dict(folds=5, famp=0.06)
    ch.add(prism([dict(y=0.0, a=0.33, b=0.3, **hem), dict(y=0.42, a=0.25, b=0.22, **hem)], k=10, cap1=False), "cloth_robe", robe, "root")
    ch.add(prism([dict(y=0.0, a=0.338, b=0.308, **hem), dict(y=0.045, a=0.332, b=0.302, **hem)], k=10), "team_trim", trim, "root")
    ch.add(prism([(0.40, 0.255, 0.225), (0.93, 0.205, 0.15)], k=10), "cloth_robe", robe, "pelvis")
    ch.add(box((0, 0.46, -0.205), (0.03, 0.36, 0.004)), "team_trim", trim, "pelvis")
    ch.parts[-1][0] = turn(ch.parts[-1][0], (1, 0, 0), -0.105, (0, 0.82, -0.17))
    torso(ch, "cloth_robe", robe_hi, st=[(0.92, 0.2, 0.14), (1.12, 0.225, 0.15), (1.30, 0.27, 0.155), (1.39, 0.23, 0.13), (1.43, 0.11, 0.08)])
    for sd in (-1, 1):   # sleeves, flaring, with team cuffs
        ch.add(tube((sd * 0.27, 1.38, 0.0), (sd * 0.275, 0.99, -0.06), [(0, 0.075, 0.08), (1, 0.1, 0.105)], k=6, mode="flat"), "cloth_robe", robe_hi, "torso")
        ch.add(tube((sd * 0.275, 0.995, -0.06), (sd * 0.276, 0.975, -0.063), [(0, 0.104, 0.109), (1, 0.104, 0.109)], k=6, mode="flat"), "team_trim", trim, "torso")
    belt(ch, 0.9, 0.214, 0.16, 0.03, "cloth_sash", cloth(GOLD * 0.75, 0.1))
    ch.add(box((0.06, 0.78, -0.165), (0.02, 0.1, 0.006)), "cloth_sash", cloth(GOLD * 0.75, 0.1), "pelvis")
    neck(ch, "skin_face", skin())
    head(ch, "skin_face", skin())
    ch.add(prism([(1.30, 0.0, 0.0, 0, -0.12), (1.40, 0.06, 0.04, 0, -0.115), (1.50, 0.095, 0.05, 0, -0.09), (1.57, 0.08, 0.04, 0, -0.07)], k=6), "cloth_beard", cloth(WHITE, 0.0), "head")
    for sd in (-1, 1):
        ch.add(box((sd * 0.038, 1.632, -0.112), (0.026, 0.008, 0.008)), "cloth_beard", cloth(WHITE, 0.0), "head")
    ch.add(prism([(1.655, 0.172, 0.172), (1.67, 0.174, 0.174), (1.68, 0.12, 0.12)], k=10), "cloth_robe", robe_hi, "head")
    ch.add(prism([(1.675, 0.11, 0.11), (1.72, 0.08, 0.08, 0, 0.015), (1.76, 0.045, 0.045, 0, 0.045), (1.785, 0.02, 0.02, 0, 0.075), (1.795, 0.0, 0.0, 0, 0.1)],
                 k=8), "cloth_robe", robe_hi, "head")
    ch.add(ring(1.676, 1.698, 0.112, 0.112), "team_trim", trim, "head")
    star = [(0.026 * (1 if i % 2 == 0 else 0.45) * math.sin(i * PI / 5), 1.722 + 0.026 * (1 if i % 2 == 0 else 0.45) * math.cos(i * PI / 5)) for i in range(10)]
    ch.add(turn(front_plate(star[::-1], -0.104, 0.008, 0.0), (1, 0, 0), -0.45, (0, 1.722, -0.096)), "glow_star", glow((1.6, 1.4, 0.5)), "head")
    return ch


def zombie():
    ch = Char("ZOMBIE", "zombie: torn dirty clothes over grey-green skin, a team armband and scarf, a big hunched head with glowing eyes, arms reaching out, one bare foot",
              Rig(hipX=0.13))
    r = ch.rig
    sk = skin(SKIN_ZOMBIE)
    pants, shirt = cloth(lin3(0.29, 0.25, 0.2), 0.35), cloth(lin3(0.38, 0.4, 0.36), 0.35)
    jag = lambda th: 0.03 * np.sign(np.sin(4 * th))
    legs(ch, "cloth_trousers", pants)
    x = -r.hipX
    ch.add(blk((x, 0.06, -0.045), (0.062, 0.055, 0.13), 0.018), "leather_shoe", leather(LEATHER_DK), "foot_l")
    x = r.hipX
    ch.add(blk((x, 0.04, -0.04), (0.052, 0.04, 0.12), 0.015), "skin_zombie", sk, "foot_r")
    ch.add(prism([(r.ankleY - 0.01, 0.05, 0.054, x), (0.24, 0.058, 0.062, x)], k=6), "skin_zombie", sk, "shin_r")
    hips(ch, "cloth_trousers", pants)
    hunch = [(0.9, 0.2, 0.135), (1.12, 0.23, 0.15, 0, -0.015), (1.29, 0.27, 0.15, 0, -0.045), (1.38, 0.22, 0.125, 0, -0.06), (1.42, 0.1, 0.07, 0, -0.065)]
    ch.add(prism([dict(y=y, a=a, b=b, cx=c[0] if c else 0, cz=c[1] if c else 0, **(dict(ywave=jag) if i == 0 else {})) for i, (y, a, b, *c) in enumerate(hunch)], k=8),
           "cloth_shirt", shirt, "torso")
    ch.add(box((-0.08, 1.1, -0.158), (0.05, 0.04, 0.005)), "skin_rot", skin(ROT), "torso")
    ch.add(box((0.1, 1.2, -0.176), (0.04, 0.03, 0.005)), "cloth_blood", cloth(BLOOD, 0.0), "torso")
    ch.add(prism([dict(y=1.33, a=0.14, b=0.11, cz=-0.05, ywave=jag), (1.42, 0.1, 0.08, 0, -0.06)], k=8), "team_scarf", team(0.9, 0.05, wear=0.3), "torso")
    for sd in (-1, 1):   # arms reaching forward
        sh, el, wr = np.array((sd * 0.25, 1.32, -0.03)), np.array((sd * 0.23, 1.26, -0.16)), np.array((sd * 0.17, 1.28, -0.23))
        ch.add(tube(sh, el, [(0, 0.075, 0.075), (1, 0.06, 0.06)], k=6, mode="flat"), "cloth_shirt", shirt, "torso")
        if sd < 0:
            ch.add(tube(sh + (el - sh) * 0.4, sh + (el - sh) * 0.75, [(0, 0.08, 0.08), (1, 0.066, 0.066)], k=6, mode="flat"), "team_armband", team(1.0, 0.05, wear=0.3), "torso")
        ch.add(tube(el, wr, [(0, 0.05, 0.05), (1, 0.04, 0.04)], k=6, mode="flat"), "skin_zombie", sk, "torso")
        ch.add(blk(wr + np.array((0, -0.01, -0.035)), (0.042, 0.022, 0.045), 0.012), "skin_zombie", sk, "torso")
    neck(ch, "skin_zombie", sk, r=0.05)
    ch.parts[-1][0] = xf(ch.parts[-1][0], t=(0, 0, -0.04))
    head(ch, "skin_zombie", sk, s=1.08, off=(0.02, -0.03, -0.04), eye_mat="glow_eyes", eye_look=glow((1.8, 1.6, 0.4)), top=1.74)
    ch.add(box((0.02, 1.49, -0.128), (0.045, 0.012, 0.004)), "cloth_mouth", cloth(BLOOD * 0.5, 0.0), "head")
    return ch


def fighter():
    ch = Char("FIGHTER", "fighter: big team gloves up in a guard, bare arms and chest, dark shorts with a team trim and a gold champion belt, white shoes, a team headband",
              Rig(hipX=0.12))
    r = ch.rig
    sk = skin()
    legs(ch, "skin_body", sk)
    shorts = cloth(lin3(0.2, 0.2, 0.22), 0.1)
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(prism([(0.64, 0.1, 0.105, x), (r.hipY + 0.06, 0.1, 0.105, x)], k=6), "cloth_shorts", shorts, "thigh" + L(sd))
        ch.add(ring(0.635, 0.66, 0.104, 0.109, k=6, cx=x), "team_shortstrim", team(1.2, 0.2), "thigh" + L(sd))
        ch.add(ring(0.13, 0.22, 0.056, 0.06, k=6, cx=x), "cloth_tape", cloth(TAPE, 0.1), "shin" + L(sd))
    boots(ch, "cloth_shoe", cloth(WHITE, 0.1), s=0.9, shaft=0)
    hips(ch, "cloth_shorts", shorts, a=0.205, b=0.138)
    torso(ch, "skin_body", sk)
    ch.add(blk((0, 1.25, -0.1), (0.17, 0.06, 0.055), 0.03), "skin_body", sk, "torso")
    for sd in (-1, 1):
        ch.add(prism([(1.25, 0.08, 0.08, sd * 0.255), (1.33, 0.09, 0.09, sd * 0.255), (1.41, 0.0, 0.0, sd * 0.255)], k=6), "skin_body", sk, "torso")
    belt(ch, 0.9, 0.212, 0.142, 0.045, "leather_belt", leather(LEATHER_DK), ("brass_plate", brass(), 0.08))
    ch.add(box((0, 0.9, -0.158), (0.035, 0.025, 0.004)), "team_gem", glow((1.8, 1.8, 1.8)), "torso")
    for sd in (-1, 1):   # arms up in a guard, big gloves
        sh, el, wr = np.array((sd * 0.26, 1.3, 0.0)), np.array((sd * 0.25, 1.1, -0.11)), np.array((sd * 0.14, 1.29, -0.22))
        ch.add(tube(sh, el, [(0, 0.062, 0.062), (1, 0.05, 0.05)], k=6, mode="flat"), "skin_body", sk, "torso")
        ch.add(tube(el, wr, [(0, 0.05, 0.05), (1, 0.04, 0.04)], k=6, mode="flat"), "skin_body", sk, "torso")
        ch.add(prism([(1.27, 0.06, 0.068, sd * 0.13, -0.26), (1.33, 0.072, 0.08, sd * 0.13, -0.26), (1.38, 0.05, 0.06, sd * 0.13, -0.26)], k=8), "team_glove", team(1.0, 0.4), "torso")
        ch.add(ring(1.24, 1.27, 0.052, 0.058, k=8, cx=sd * 0.135, cz=-0.245), "cloth_cuff", cloth(WHITE, 0.1), "torso")
    neck(ch, "skin_body", sk, r=0.062)
    head(ch, "skin_face", skin())
    ch.add(prism([dict(y=1.655, a=0.103, b=0.113, ywave=lambda th: -0.02 * np.sin(th)), (1.715, 0.09, 0.1, 0, 0.005), (1.755, 0.0, 0.0, 0, 0.005)], k=8), "cloth_hair", cloth(HAIR_DK, 0.0), "head")
    ch.add(ring(1.655, 1.685, 0.104, 0.114), "team_headband", team(1.15, 0.1), "head")
    for dx, dy in ((0.03, -0.035), (-0.03, -0.06)):
        ch.add(turn(box((dx, 1.665 + dy * 0.5, 0.14), (0.012, 0.004, 0.035)), (1, 0, 0), 0.6, (dx, 1.67, 0.11)), "team_headband", team(1.15, 0.1), "head")
    return ch


def pirate():
    ch = Char("PIRATE", "pirate: big cuffed boots, striped trousers, a navy long coat with team lapels and brass buttons, a red sash, a big black beard, an eye patch, a huge tricorn",
              Rig(hipX=0.12))
    r = ch.rig
    legs(ch, "cloth_trousers", cloth(CREAM, 0.15))
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(prism([(r.ankleY, 0.068, 0.072, x), dict(y=r.kneeY + 0.03, a=0.085, b=0.09, cx=x, ywave=lambda th: -0.035 * np.sin(th))], k=6), "leather_boot", leather(LEATHER_DK), "shin" + L(sd))
    boots(ch, "leather_boot", leather(LEATHER_DK), shaft=0, toe=0.01)
    hips(ch, "cloth_trousers", cloth(CREAM, 0.15))
    coat = cloth(lin3(0.2, 0.22, 0.36), 0.25)
    torso(ch, "cloth_coat", coat, s=1.03, sz=1.05)
    ch.add(shell([(0.5, 0.26, 0.27), (0.95, 0.215, 0.152)], -0.35, PI + 0.35, thick=0.016, k=7, mode="flat"), "cloth_coat", coat, "pelvis")
    ch.add(front_plate([(-0.07, 1.0), (0.07, 1.0), (0.1, 1.36), (-0.1, 1.36)], -0.16, 0.006, 0.002), "cloth_shirt", cloth(CREAM, 0.1), "torso")
    for sd in (-1, 1):
        ch.add(front_plate([(sd * 0.07, 1.0), (sd * 0.15, 1.1), (sd * 0.16, 1.36), (sd * 0.1, 1.36)][:: (1 if sd > 0 else -1)], -0.166, 0.006, 0.002), "team_lapel", team(0.95, 0.1), "torso")
        ch.add(box((sd * 0.13, 1.06, -0.172), (0.012, 0.012, 0.005)), "brass_button", brass(), "torso")
        ch.add(box((sd * 0.13, 1.14, -0.172), (0.012, 0.012, 0.005)), "brass_button", brass(), "torso")
    pads(ch, "cloth_coat", coat, s=0.95)
    belt(ch, 0.88, 0.222, 0.163, 0.04, "cloth_sash", cloth(RED, 0.12), tilt=0.015)
    ch.add(box((0.12, 0.76, -0.162), (0.035, 0.09, 0.008)), "cloth_sash", cloth(RED, 0.12), "pelvis")
    neck(ch, "skin_face", skin())
    head(ch, "skin_face", skin())
    ch.add(prism([(1.42, 0.0, 0.0, 0, -0.11), (1.49, 0.085, 0.055, 0, -0.095), (1.56, 0.1, 0.06, 0, -0.07), (1.59, 0.085, 0.05, 0, -0.06)], k=6), "cloth_beard", cloth(HAIR_DK, 0.0), "head")
    ch.add(box((-0.038, 1.612, -0.11), (0.028, 0.022, 0.006)), "leather_patch", leather(lin3(0.05, 0.045, 0.04)), "head")
    ch.add(turn(ring(1.62, 1.628, 0.103, 0.113, k=8), (0, 0, 1), 0.3, (0, 1.62, 0)), "leather_patch", leather(lin3(0.05, 0.045, 0.04)), "head")
    hat = cloth(lin3(0.2, 0.19, 0.2), 0.1)
    tri = dict(folds=3, famp=0.14, fphase=1.5 * PI)
    ch.add(prism([dict(y=1.67, a=0.15, b=0.15, **tri), dict(y=1.75, a=0.148, b=0.148, **tri)], k=12), "cloth_hat", hat, "head")
    ch.add(prism([(1.70, 0.11, 0.115), (1.775, 0.1, 0.105), (1.795, 0.0, 0.0)], k=8), "cloth_hat", hat, "head")
    ch.add(prism([dict(y=1.744, a=0.149, b=0.149, **tri), dict(y=1.752, a=0.149, b=0.149, **tri)], k=12, cap0=False, cap1=False), "team_trim", team(1.0, 0.2), "head")
    ch.add(box((0, 1.72, -0.168), (0.022, 0.02, 0.005)), "cloth_skull", cloth(WHITE, 0.0), "head")
    return ch


def ninja():
    ch = Char("NINJA", "ninja: a slate wrapped suit, shin wraps, a team sash and knot, gunmetal shoulder guards, a katana on the back, a hood with an eye slit and team headband tails",
              Rig(hipX=0.12))
    r = ch.rig
    suit = cloth(NINJA_CLOTH, 0.2)
    legs(ch, "cloth_suit", suit, w=1.05)
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(ring(0.13, 0.36, 0.06, 0.064, k=6, cx=x), "cloth_wrap", cloth(lin3(0.34, 0.34, 0.37), 0.25), "shin" + L(sd))
    boots(ch, "cloth_tabi", cloth(lin3(0.19, 0.19, 0.21), 0.1), s=0.85, shaft=0, sole=False)
    hips(ch, "cloth_suit", suit)
    torso(ch, "cloth_suit", suit)
    ch.add(turn(box((0.0, 1.17, -0.152), (0.025, 0.2, 0.004)), (0, 0, 1), -0.5, (0, 1.17, -0.15)), "cloth_lapel", cloth(lin3(0.34, 0.34, 0.37), 0.2), "torso")
    pads(ch, "nitride_guard", gunmetal(1.3), s=0.9)
    belt(ch, 0.9, 0.205, 0.137, 0.045, "team_sash", team(0.95, 0.08))
    ch.add(blk((0.11, 0.88, -0.145), (0.04, 0.035, 0.02), 0.01), "team_sash", team(0.95, 0.08), "torso")
    ch.add(box((0.11, 0.78, -0.145), (0.02, 0.07, 0.005)), "team_sash", team(0.95, 0.08), "pelvis")
    a, b = np.array((-0.17, 0.98, 0.172)), np.array((0.15, 1.33, 0.172))
    d = (b - a) / np.linalg.norm(b - a)
    ch.add(tube(a, b, [(0, 0.018, 0.026), (1, 0.02, 0.028)], k=4, mode="flat"), "poly_saya", polymer(lin3(0.17, 0.16, 0.17)), "torso")
    ch.add(tube(b, b + d * 0.014, [(0, 0.038, 0.038), (1, 0.038, 0.038)], k=6, mode="flat"), "nitride_tsuba", gunmetal(1.4), "torso")
    ch.add(tube(b + d * 0.014, b + d * 0.1, [(0, 0.018, 0.02), (1, 0.017, 0.019)], k=4, mode="flat"), "team_tsuka", team(0.8, 0.05), "torso")
    ch.add(prism([(1.45, 0.075, 0.085, 0, -0.02), (1.52, 0.1, 0.11, 0, -0.01), (1.64, 0.106, 0.116), (1.71, 0.09, 0.1, 0, 0.005), (1.755, 0.0, 0.0, 0, 0.005)], k=8),
           "cloth_hood", suit, "head")
    ch.add(box((0, 1.61, -0.113), (0.07, 0.02, 0.004)), "skin_face", skin(), "head")
    for sd in (-1, 1):
        ch.add(box((sd * 0.035, 1.61, -0.118), (0.016, 0.009, 0.003)), "poly_eyes", polymer(EYE_DK), "head")
    ch.add(ring(1.65, 1.68, 0.107, 0.117), "team_headband", team(1.05, 0.08), "head")
    for dx, ln, ang in ((0.03, 0.05, 0.5), (-0.03, 0.045, 0.8)):
        ch.add(turn(box((dx, 1.665, 0.117 + ln * 0.5), (0.014, 0.004, ln * 0.5)), (1, 0, 0), ang, (dx, 1.665, 0.117)), "team_headband", team(1.05, 0.08), "head")
    return ch


def bandit():
    ch = Char("BANDIT", "bandit: leather chaps over denim, spurred boots, a dusty shirt under a dark vest, a slung gun belt with a holstered revolver, a team bandana, a wide hat with a team band",
              Rig(hipX=0.12))
    r = ch.rig
    denim = cloth(lin3(0.26, 0.32, 0.44), 0.25)
    legs(ch, "cloth_denim", denim)
    chaps = leather(LEATHER)
    for sd in (-1, 1):
        x = sd * r.hipX
        ch.add(shell([(r.kneeY - 0.02, 0.072, 0.078), (r.hipY + 0.03, 0.096, 0.1)], -PI + 0.2, -0.2, thick=0.01, k=4, cx=x, mode="flat"), "leather_chaps", chaps, "thigh" + L(sd))
        ch.add(box((x, 0.07, 0.12), (0.004, 0.022, 0.022)), "nitride_spur", gunmetal(1.5), "foot" + L(sd))
    boots(ch, "leather_boot", leather(LEATHER_DK), toe=0.012)
    hips(ch, "cloth_denim", denim)
    torso(ch, "cloth_shirt", cloth(lin3(0.58, 0.52, 0.43), 0.2))
    vest = leather(LEATHER_DK * 0.95)
    vst = [(0.95, 0.196, 0.133), (1.12, 0.24, 0.153), (1.30, 0.295, 0.164)]
    ch.add(shell(vst, -PI / 2 + 0.25, PI / 2 + 0.2, thick=0.014, k=4, mode="flat"), "leather_vest", vest, "torso")
    ch.add(shell(vst, PI / 2 - 0.2, 3 * PI / 2 - 0.25, thick=0.014, k=4, mode="flat"), "leather_vest", vest, "torso")
    pads(ch, "leather_vest", vest, s=0.92)
    belt(ch, 0.85, 0.214, 0.15, 0.024, "leather_gunbelt", leather(LEATHER_DK), bone="pelvis", tilt=-0.04, k=8)
    ch.add(box((0, 0.85, -0.156), (0.1, 0.008, 0.004)), "brass_bullets", brass(), "pelvis")
    ch.add(box((0.24, 0.72, 0.0), (0.035, 0.1, 0.055)), "leather_holster", leather(), "pelvis")
    ch.add(blk((0.24, 0.86, 0.03), (0.018, 0.045, 0.04), 0.01), "leather_grip", leather(lin3(0.45, 0.28, 0.15)), "pelvis")
    neck(ch, "skin_face", skin())
    head(ch, "skin_face", skin())
    band = team(0.9, 0.05)
    ch.add(prism([(1.47, 0.085, 0.095, 0, -0.02), (1.585, 0.104, 0.114, 0, -0.002)], k=8), "team_bandana", band, "head")
    ch.add(turn(front_plate([(-0.07, 1.585), (0.07, 1.585), (0.0, 1.45)], -0.118, 0.006, 0.0), (1, 0, 0), 0.25, (0, 1.585, -0.115)), "team_bandana", band, "head")
    felt = cloth(lin3(0.36, 0.26, 0.17), 0.12)
    up = lambda th: 0.03 * np.cos(th) ** 2
    ch.add(prism([dict(y=1.67, a=0.174, b=0.174, ywave=up), dict(y=1.685, a=0.174, b=0.174, ywave=up)], k=10), "cloth_felt", felt, "head")
    ch.add(prism([(1.68, 0.108, 0.118), (1.76, 0.104, 0.114), (1.79, 0.07, 0.08)], k=8), "cloth_felt", felt, "head")
    ch.add(ring(1.69, 1.715, 0.11, 0.12), "team_hatband", team(1.0, 0.1), "head")
    return ch


BUILDERS = {"STANDARD": standard, "HEAVY": heavy, "SIMPLE": simple, "ROBOT": robot, "KNIGHT": knight, "WIZARD": wizard, "ZOMBIE": zombie,
            "FIGHTER": fighter, "PIRATE": pirate, "NINJA": ninja, "BANDIT": bandit}
