"""KNIFE: the thrown knife, the held KNIFE weapon itself (gen_weapons_lowpoly.knife: clip-point light blade, dark
spine with teeth, glow fuller, guard, ringed grip, team pommel), recentred so its middle is at the origin (the game spins
it end over end about X there), blade toward -Z, flat faces left/right, scaled 1.2x so it reads metres away."""
from gen_weapons_lowpoly import Model
from lowpoly import loft, ngon, solid

NAME = "KNIFE"

K = 1.2          # scale over the held knife (330 mm -> ~390 mm, the old box was ~400 mm)
Z0, Y0 = -49, -8  # the held knife's middle (z -212..114) and its grip axis


def _zy(poly):
    """a held-knife side profile (z, y) to projectile space"""
    return [((z - Z0) * K, (y - Y0) * K) for z, y in poly]


def _xy(poly):
    """a held-knife cross-section (x, y) to projectile space"""
    return [(x * K, (y - Y0) * K) for x, y in poly]


def _w(z):
    return (z - Z0) * K


def build():
    m = Model(NAME, "thrown combat knife: the held knife (clip-point light blade, dark spine with teeth, glow fuller, "
                    "guard, ringed grip, team pommel) centred on its middle, blade forward")
    B = m.body
    s = lambda v: v * K   # noqa: E731
    blade = [(-20, 8), (-150, 8), (-186, 2), (-212, -6), (-196, -16), (-150, -24), (-20, -24)]
    solid(B, "base_light", _zy(blade), "x", s(-2.6), s(2.6))
    solid(B, "metal_dark", _zy([(-20, 4), (-148, 4), (-148, 9), (-20, 9)]), "x", s(-3.4), s(3.4))
    for lo, hi in ((2.7, 3.4), (-3.4, -2.7)):
        solid(B, "glow", _zy([(-40, -6), (-140, -6), (-140, -2), (-40, -2)]), "x", s(lo), s(hi), shell=False)
    for k in range(4):
        z = -100 + k * 12
        solid(B, "metal_dark", _zy([(z, 9), (z - 6, 9), (z - 3, 14)]), "x", s(-3), s(3))
    solid(B, "metal_dark", _zy([(-22, 16), (-10, 16), (-10, -34), (-22, -34), (-24, -26), (-24, 8)]), "x", s(-16), s(16))
    grip = _xy(ngon(0, -8, 14, 8, sx=0.85, sy=1.15))
    ring = _xy(ngon(0, -8, 15, 8, sx=0.85, sy=1.15))
    solid(B, "metal_dark", grip, "z", _w(-10), _w(92))
    for z in (12, 42, 72):
        solid(B, "metal_mid", ring, "z", _w(z), _w(z + 7))
    loft(B, "team_wood", ring, _w(92), _xy(ngon(0, -8, 11, 8)), _w(106))
    solid(B, "metal_mid", _xy(ngon(0, -8, 5, 6)), "z", _w(106), _w(114))
    return m
