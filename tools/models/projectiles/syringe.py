"""SYRINGE: the thrown syringe, the SYRINGE weapon's look (gen_weapons_lowpoly.syringe) at the old box's size: a
see-through glass barrel with glowing serum inside and dark dose marks, dark end caps, a tapered hub and a steel needle
with a hot tip first (-Z), a team finger flange and the plunger rod with its thumb pad at the back."""
from gen_weapons_lowpoly import Model
from lowpoly import loft, ngon, solid

NAME = "SYRINGE"


def build():
    m = Model(NAME, "thrown syringe: a see-through glass barrel with glowing serum and dose marks, dark caps, a tapered "
                    "hub and needle with a hot tip first, a team finger flange and the plunger with a team thumb pad")
    B = m.body
    solid(B, "glow", ngon(0, 0, 24, 8), "z", -108, 92, shell=False)                     # serum
    solid(B, "base_glass", ngon(0, 0, 30, 8), "z", -118, 112)                           # barrel
    for z in (-70, -30, 10, 50):                                                       # dose marks on top
        solid(B, "metal_dark", [(-10, 27.5), (10, 27.5), (10, 31.5), (-10, 31.5)], "z", z, z + 6, shell=False)
    solid(B, "metal_dark", ngon(0, 0, 32, 8), "z", -130, -118)                          # front cap
    loft(B, "metal_dark", ngon(0, 0, 18, 8), -130, ngon(0, 0, 7, 8), -156)              # hub
    solid(B, "metal_steel", ngon(0, 0, 3.5, 6), "z", -244, -156)                        # needle
    solid(B, "glow_hot", ngon(0, 0, 2.6, 6), "z", -250, -244, shell=False)              # hot tip
    solid(B, "metal_dark", ngon(0, 0, 32, 8), "z", 112, 124)                            # back cap
    solid(B, "team_wood", [(-64, -6), (-54, -13), (54, -13), (64, -6), (64, 6), (54, 13), (-54, 13), (-64, 6)],
          "z", 124, 140)                                                                # finger flange
    solid(B, "metal_mid", ngon(0, 0, 8, 8), "z", 140, 192)                              # plunger rod
    solid(B, "metal_dark", ngon(0, 0, 26, 8), "z", 192, 200)                            # thumb pad
    solid(B, "team_wood", ngon(0, 0, 19, 8), "z", 200, 206, shell=False)
    return m
