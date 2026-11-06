"""33 more player designs for the OUTLINED set (gen_players_outlined.py), on the same rules as characters_outlined.py
and characters_outlined_extra.py: box model files, one bone a box, all 9 walking bones, inside the shared hitbox (the
head cylinder r 17.6 cm above 1.44 m, the body cylinder r 39.6 cm below), a 5 mm grid, at most 1000 triangles.

Same conventions: feet on the floor at the origin, facing -Z, +X its right; a box is (x0, x1, y0, y1, z0, z1) in cm.
The legs keep the usual joints (thigh top 88-91 cm, knee 44-48 cm: no thigh box above the hip or under the knee).
Each design is told apart from the 64 by silhouette first (a stacked snowball body, a tutu disc, a mushroom cap, a
bearskin, a beak mask, a CRT head, a surfboard, a long snout, rabbit ears, goalie pads, a bomb-suit collar, twin
fuel tanks...) and palette second; the team slot is on at least about a fifth of what's seen on every one.

The eight shooter archetypes (MARINE MEDIC ENGINEER MARKSMAN DEMOLITION RUNNER AGENT FLAMER) are original takes on
the general roles, not any game's characters: no names, outfits, logos or colour schemes taken from one.

Their fixed colours are in gen_players_outlined.TONES with the others' (some use the 20 extras' too);
gen_players_outlined.py writes them after the 31 (its NAMES).
"""
from characters_outlined import Design, plate



def brim(d, y, tone, name="brim", r=12, arm=16):
    """a round-reading hat brim inside the head cylinder: a square and a cross through it"""
    d.box((-r, r, y, y + 2, -r, r), tone, "head", name)
    d.box((-arm, arm, y + 0.5, y + 1.5, -4, 4), tone, "head", name + " x")
    d.box((-4, 4, y + 0.5, y + 1.5, -arm, arm), tone, "head", name + " z")


def face(d, skin="skin", y0=146, y1=166, eyes=True, neck=True):
    if neck:
        d.box((-5, 5, 141, 147, -5, 5), skin, "head", "neck")
    d.box((-9.5, 9.5, y0, y1, -10, 10), skin, "head", "face")
    if eyes:
        d.box((-6, 6, 157.5, 159.5, -10.5, -10), "eye", "head", "eyes", **plate())


# ---------------------------------------------------------------------------------------------------------------
# costume and fantasy figures

def yeti():
    d = Design("YETI", "the yeti: a huge hunched frost-white fur body with shaggy tufts, a big slate face with curling ivory horns, team scarf, shorts and wrist cuffs")
    d.foot((4, 23, 0, 9, -20, 10), "frost", "big foot")
    d.sym((4.5, 22.5, 0, 3, -21.5, -20), "slate", "foot_", "toes", **plate())
    d.leg(14, 9, -10, 10, 8, 88, 46, "frost")
    d.thigh((4.5, 23.5, 64, 87.5, -10.5, 10.5), "team", "shorts")
    d.shin((4, 24, 8.5, 16, -11, 11), "frost", "ankle shag")
    d.box((-23, 23, 78, 96, -15, 15), "team", "pelvis", "belt")
    d.box((-26, 26, 94, 132, -17, 17), "frost", "torso", "body")
    for k, x in enumerate((-22, -8, 8)):
        d.box((x, x + (14 if x != -8 else 16), 90, 95, -17.5, 17.5), "frost", "torso", "shag %d" % (k + 1))
    d.box((-24, 24, 128, 140, -15, 15), "frost", "torso", "hump")
    d.sym((24, 36, 108, 136, -11, 11), "frost", "torso", "shoulder")
    d.sym((25, 37, 102, 110, -10, 10), "team", "torso", "cuff")
    d.sym((36, 37.5, 114, 132, -6, 6), "frost", "torso", "shoulder shag")
    d.box((-16, 16, 134, 143, -14, 12), "team", "torso", "scarf")
    d.box((4, 12, 114, 134, -17.5, -17), "team", "torso", "scarf end", **plate())
    # the head low and forward, sunk into the hump
    d.box((-10.5, 10.5, 142, 166, -13, 7), "frost", "head", "head")
    d.box((-7.5, 7.5, 144, 157, -14, -13), "slate", "head", "face", **plate())
    d.sym((2, 5.5, 153, 155.5, -14.5, -14), "visor", "head", "eye", **plate())
    d.box((-5, 5, 147, 149, -14.5, -14), "eye", "head", "mouth", **plate())
    d.sym((1.5, 3, 148.5, 151, -15, -14.5), "white", "head", "fang", **plate())
    d.box((-11, 11, 157, 170, -13.5, 8), "frost", "head", "brow shag")
    for k, x in enumerate((-9, -3, 3)):
        d.box((x, x + 6, 155.5, 157.5, -14.5, -13.5), "frost", "head", "brow fringe %d" % (k + 1))
    d.sym((11, 14, 163, 169, -6, -1), "ivory", "head", "horn")
    d.sym((13, 16, 167, 172, -5.5, -1.5), "ivory", "head", "horn bend")
    d.sym((14, 16.5, 172, 175, -5, -2), "ivory", "head", "horn tip")
    return d


def snowman():
    d = Design("SNOWMAN", "the snowman: three stacked snowballs, coal eyes, team buttons, a carrot nose, a team top hat, scarf and mittens on stick arms")
    d.foot((7, 19, 0, 5, -12, 8), "white", "snow foot")
    d.leg(13, 7, -7.5, 7.5, 4, 88, 46, "white")
    # the bottom ball: hips + both thighs (it parts a little as it walks)
    d.thigh((1, 25, 50, 86.5, -16, 16), "white", "bottom ball")
    d.thigh((3, 23, 44.5, 50, -13, 13), "white", "ball base")
    d.box((-24, 24, 76, 98, -16.5, 16.5), "white", "pelvis", "bottom ball top")
    d.box((-19, 19, 97, 130, -14, 14), "white", "torso", "middle ball")
    d.box((-21, 21, 103, 124, -15, 15), "white", "torso", "middle bulge")
    for k, y in enumerate((106, 114, 122)):
        d.box((-2.5, 2.5, y, y + 5, -15.5, -15), "team", "torso", "button %d" % (k + 1), **plate())
    d.box((-17, 17, 128, 141, -14.5, 14.5), "team", "torso", "scarf")
    d.box((-15, -5, 96, 128.5, -15.5, -15), "team", "torso", "scarf end", **plate())
    d.box((6, 15, 104, 128.5, 15, 15.5), "team", "torso", "scarf back", **plate())
    d.sym((21, 33, 120, 122.5, -1, 1), "leather", "torso", "stick arm")
    d.sym((30, 32, 122.5, 128, -1, 1), "leather", "torso", "twig")
    d.sym((33, 38, 114, 127, -4, 4), "team", "torso", "mitten")
    d.box((-11, 11, 140, 163, -11, 11), "white", "head", "head ball")
    d.sym((3, 6, 153, 156, -11.5, -11), "eye", "head", "coal eye", **plate())
    d.box((-1.5, 1.5, 148.5, 151.5, -16.5, -12), "carrot", "head", "carrot")
    d.box((-2.5, 2.5, 148, 152, -12.5, -11), "carrot", "head", "carrot root")
    for k, x in enumerate((-5, -2.5, 0.5, 3)):
        d.box((x, x + 2, 144.5 if k in (0, 3) else 143.5, (144.5 if k in (0, 3) else 143.5) + 2, -11.5, -11), "eye", "head", "coal mouth %d" % (k + 1), **plate())
    brim(d, 162, "team", "hat brim", r=11.5, arm=14)
    d.box((-9, 9, 164, 179.5, -9, 9), "team", "head", "top hat")
    d.box((-9.5, 9.5, 165, 168, -9.5, 9.5), "team_dk", "head", "hat band")
    d.sym((11, 13, 149, 158, -3.5, 3.5), "team", "head", "earmuff")
    d.box((-12, 12, 157, 159, -1.5, 1.5), "team_dk", "head", "earmuff band")
    return d


def scarecrow():
    d = Design("SCARECROW", "the scarecrow: a wide floppy straw hat, a stitched burlap sack head, a patched team shirt hung on a pole across the back, straw tufts at every cuff")
    d.foot((6, 20, 0, 7, -15, 8), "leather", "old boot")
    d.leg(13, 7.5, -8, 8, 7, 90, 47, "denim")
    d.thigh((8, 14, 64, 72, -8.5, -8), "team", "knee patch", **plate())
    d.shin((4.5, 21.5, 7.5, 13, -9, 9), "straw", "ankle straw")
    d.box((-19, 19, 82, 96, -12, 12), "denim", "pelvis", "hips")
    d.box((-3, 3, 84, 90, -12.5, -12), "leather", "pelvis", "rope knot", **plate())
    d.box((-22, 22, 94, 136, -13, 13), "team", "torso", "shirt")
    d.box((-16, -6, 104, 114, -13.5, -13), "burlap", "torso", "patch", **plate())
    d.box((6, 14, 118, 126, -13.5, -13), "denim", "torso", "patch 2", **plate())
    for k, y in enumerate((100, 110, 120, 130)):
        d.box((-1, 1, y, y + 2, -13.5, -13), "eye", "torso", "button %d" % (k + 1), **plate())
    d.box((-21.5, 21.5, 92, 96, -13.5, 13.5), "straw", "torso", "waist straw")
    d.box((-35, 35, 126, 130, 13, 16), "hat", "torso", "pole")
    d.box((-2, 2, 60, 126, 13, 16), "hat", "torso", "post")
    d.sym((22, 32, 118, 136, -10, 10), "team", "torso", "sleeve")
    d.sym((32, 36, 112, 124, -7, 7), "straw", "torso", "hand straw")
    d.box((-12, 12, 134, 142, -10, 10), "straw", "torso", "neck straw")
    d.box((-10, 10, 142, 164, -10, 10), "burlap", "head", "sack head")
    d.box((-4, 4, 140, 143, -10.5, 10.5), "leather", "head", "sack tie")
    d.sym((2.5, 6, 155, 158.5, -10.5, -10), "eye", "head", "button eye", **plate())
    d.box((-6, 6, 149, 150, -10.5, -10), "eye", "head", "stitched mouth", **plate())
    for k, x in enumerate((-5, -2, 1, 4)):
        d.box((x, x + 1, 148, 151, -11, -10.5), "eye", "head", "stitch %d" % (k + 1), **plate())
    brim(d, 163, "straw", "hat brim")
    d.box((-8, 8, 165, 172, -8, 8), "straw", "head", "hat crown")
    d.box((-8.5, 8.5, 165.5, 167.5, -8.5, 8.5), "team", "head", "hat band")
    d.box((-14, -11, 159, 164, -3, 3), "straw", "head", "hat droop")
    return d


def jester():
    d = Design("JESTER", "the jester: a two-pointed drooping hat with gold bells, a harlequin suit split team and gold, a scalloped collar, curled shoes")
    d.foot((6, 20, 0, 6, -17, 8), "team", "shoe")
    d.foot((10, 16, 4, 9, -21, -15), "gold", "curled toe")
    d.box((5.5, 20.5, 45, 90, -7.5, 7.5), "gold", "thigh_r", "leg thigh_r")
    d.box((6, 20, 6, 47, -7, 7), "team", "shin_r", "leg shin_r")
    d.box((-20.5, -5.5, 45, 90, -7.5, 7.5), "team", "thigh_l", "leg thigh_l")
    d.box((-20, -6, 6, 47, -7, 7), "gold", "shin_l", "leg shin_l")
    d.box((-19, 19, 82, 96, -12, 12), "dark", "pelvis", "hips")
    d.box((0, 22, 94, 136, -13, 13), "team", "torso", "suit right")
    d.box((-22, 0, 94, 136, -13, 13), "gold", "torso", "suit left")
    d.box((-12, -4, 106, 118, -13.5, -13), "team", "torso", "diamond l", **plate())
    d.box((4, 12, 106, 118, -13.5, -13), "gold", "torso", "diamond r", **plate())
    d.sym((22, 32, 120, 136, -10, 10), "dark", "torso", "shoulder")
    d.box((-18, 18, 134, 141, -15, 15), "white", "torso", "collar")
    for k, x in enumerate((-18, -10, -2, 6, 14)):
        d.box((x, x + 4, 128, 134, -15, -14), "team" if k % 2 else "gold", "torso", "collar point %d" % (k + 1))
        d.box((x, x + 4, 128, 134, 14, 15), "gold" if k % 2 else "team", "torso", "collar point back %d" % (k + 1))
    face(d)
    d.box((-4, 4, 150, 151, -10.5, -10), "eye", "head", "grin", **plate())
    d.sym((4, 5, 151, 152.5, -10.5, -10), "eye", "head", "grin up", **plate())
    d.box((0, 10.5, 163, 172, -10.5, 10.5), "team", "head", "cap right")
    d.box((-10.5, 0, 163, 172, -10.5, 10.5), "gold", "head", "cap left")
    for sx, t in ((1, "team"), (-1, "gold")):
        for k, (x0, x1, y0, y1, z) in enumerate(((6, 12, 172, 178, 3), (12, 16, 170, 177, 2.5), (14, 17, 162, 170, 2))):
            d.box((min(sx * x0, sx * x1), max(sx * x0, sx * x1), y0, y1, -z, z), t, "head", "point %s %d" % ("rl"[sx < 0], k + 1))
    d.sym((14, 17, 157, 162, -2.5, 2.5), "white", "head", "bell")
    d.box((-1.5, 1.5, 172, 176, -1.5, 1.5), "white", "head", "pompom")
    return d


def mime():
    d = Design("MIME", "the mime: a white face with black lips and a painted tear, a tilted black beret, a team-and-white striped shirt, black braces and trousers, a team neck scarf")
    d.foot((6, 20, 0, 8, -16, 8), "dark", "shoe")
    d.leg(13, 7, -7.5, 7.5, 7, 90, 47, "hairblk")
    d.box((-19, 19, 82, 96, -12, 12), "hairblk", "pelvis", "hips")
    d.box((-21, 21, 94, 137, -12.5, 12.5), "white", "torso", "shirt")
    for k, y in enumerate((97, 105, 113, 121, 129)):
        d.box((-21.5, 21.5, y, y + 5.5, -13, 13), "team", "torso", "stripe %d" % (k + 1))
    d.sym((8, 10.5, 94, 137, -13.5, -13), "hairblk", "torso", "brace", **plate())
    d.sym((8, 10.5, 94, 137, 13, 13.5), "hairblk", "torso", "brace back", **plate())
    d.sym((21, 31, 118, 137, -9.5, 9.5), "white", "torso", "sleeve")
    for k, y in enumerate((121, 129)):
        d.sym((21.5, 31.5, y, y + 5, -10, 10), "team", "torso", "sleeve stripe %d" % (k + 1))
    d.box((-11, 11, 136, 142, -10, 10), "team", "torso", "scarf")
    d.box((4, 9, 124, 136, -11, -10), "team", "torso", "scarf tail", **plate())
    d.box((-5, 5, 141, 147, -5, 5), "white", "head", "neck")
    d.box((-9.5, 9.5, 146, 166, -10, 10), "white", "head", "face")
    d.sym((3, 6, 157, 158.5, -10.5, -10), "eye", "head", "eye", **plate())
    d.sym((3, 6, 161, 162, -10.5, -10), "eye", "head", "brow", **plate())
    d.box((-4, -3, 153, 156, -10.5, -10), "eye", "head", "tear", **plate())
    d.box((-3, 3, 150, 152, -10.5, -10), "hairblk", "head", "lips", **plate())
    d.box((-12, 8, 165, 169, -11, 11), "hairblk", "head", "beret")
    d.box((-13, 4, 169, 171.5, -9, 9), "hairblk", "head", "beret top")
    d.box((-4, -2, 171.5, 174, -1, 1), "hairblk", "head", "beret stalk")
    return d


def bee():
    d = Design("BEE", "the bee mascot: a round honey-and-black striped body, glassy wings on the back, a stinger, antennae, big dark eyes, team shorts, sleeves and boots")
    d.foot((6, 20, 0, 9, -16, 8), "team", "boot")
    d.leg(13, 6.5, -7, 7, 8, 90, 47, "hairblk")
    d.thigh((5, 21, 68, 89.5, -9, 9), "team", "shorts")
    d.box((-20, 20, 80, 97, -14, 14), "honey", "pelvis", "abdomen")
    d.box((-3, 3, 82, 88, 14, 22), "hairblk", "pelvis", "stinger")
    d.box((-1.5, 1.5, 83.5, 86.5, 22, 26), "hairblk", "pelvis", "stinger tip")
    d.box((-24, 24, 95, 134, -16, 16), "honey", "torso", "body")
    for k, y in enumerate((99, 111, 123)):
        d.box((-24.5, 24.5, y, y + 6, -16.5, 16.5), "hairblk", "torso", "stripe %d" % (k + 1))
    d.sym((3, 24, 104, 138, 16.5, 17.5), "glass", "torso", "wing")
    d.sym((10, 26, 108, 134, 17.5, 18), "white", "torso", "wing vein", **plate())
    d.sym((24, 34, 120, 137, -10, 10), "team", "torso", "sleeve")
    d.box((-15, 15, 134, 143.5, -12, 12), "white", "torso", "fluff collar")
    d.box((-11, 11, 143, 166, -11, 11), "honey", "head", "head")
    d.box((-11.5, 11.5, 160, 166.5, -11.5, 11.5), "hairblk", "head", "head stripe")
    d.sym((3, 10, 150, 160, -12, -11), "eye", "head", "big eye")
    d.sym((4.5, 6.5, 156, 158, -12.5, -12), "white", "head", "eye shine", **plate())
    d.box((-3, 3, 148, 149, -11.5, -11), "eye", "head", "smile", **plate())
    d.sym((4, 5.5, 166, 174, -5, -3.5), "hairblk", "head", "antenna")
    d.sym((5.5, 7, 172, 176, -5, -3.5), "hairblk", "head", "antenna bend")
    d.sym((6, 10, 176, 179.5, -6, -2.5), "team", "head", "antenna ball")
    return d


def pumpkin():
    d = Design("PUMPKIN", "the pumpkin head: a big ribbed pumpkin with a candle-lit carved face and a green stem, a ragged dark tailcoat over a team waistcoat, team trousers")
    d.foot((6, 20, 0, 8, -17, 8), "dark", "shoe")
    d.leg(13, 7, -7.5, 7.5, 7, 90, 47, "team")
    d.shin((5.5, 20.5, 7.5, 10, -8, 8), "dark", "trouser cuff")
    d.box((-19, 19, 82, 96, -12, 12), "team", "pelvis", "hips")
    d.box((-21, 21, 94, 136, -12.5, 12.5), "hairblk", "torso", "tailcoat")
    d.box((-10, 10, 96, 134, -13.5, -12.5), "team", "torso", "waistcoat", **plate())
    d.box((-2, 2, 104, 130, -14, -13.5), "gold", "torso", "buttons", **plate())
    for k, (x0, x1, y0) in enumerate(((-21, -12, 62), (-12, -3, 70), (3, 12, 66), (12, 21, 58))):
        d.box((x0, x1, y0, 95, 12.5, 14.5), "hairblk", "torso", "tail %d" % (k + 1))
    d.sym((21, 31, 118, 136, -10, 10), "hairblk", "torso", "shoulder")
    d.sym((28, 31.5, 112, 118, -9, 9), "hairblk", "torso", "rag")
    d.box((-12, 12, 134, 142, -10, 10), "team", "torso", "cravat")
    d.box((-3, 3, 140, 146, -3, 3), "stem", "head", "neck vine")
    d.box((-13, 13, 145, 167, -10.5, 10.5), "pumpkin", "head", "pumpkin")
    d.box((-10.5, 10.5, 147, 165, -13, 13), "pumpkin", "head", "pumpkin round")
    d.sym((9, 10.5, 147, 165, -13.5, -13), "pumpkin_dk", "head", "rib", **plate())
    d.sym((3, 8, 157, 162, -13.5, -13), "candle", "head", "eye", **plate())
    d.box((-1.5, 1.5, 153.5, 156, -13.5, -13), "candle", "head", "nose", **plate())
    d.box((-7, 7, 149, 152.5, -13.5, -13), "candle", "head", "mouth", **plate())
    for k, x in enumerate((-4.5, 2)):
        d.box((x, x + 2.5, 151, 152.5, -14, -13.5), "pumpkin", "head", "tooth %d" % (k + 1), **plate())
    d.box((-2, 2, 167, 173, -2, 2), "stem", "head", "stem")
    d.box((-1.5, 5, 172, 174, -1.5, 1.5), "stem", "head", "stem curl")
    d.box((2, 8, 167, 168.5, -4, 1), "stem", "head", "leaf")
    return d


def werewolf():
    d = Design("WEREWOLF", "the werewolf: a grey wolf head with a long snout, pointed ears and yellow eyes, a hunched furry back, a torn team shirt and shorts, claws")
    d.foot((5, 21, 0, 7, -19, 8), "wolf", "paw")
    for k, x in enumerate((6, 11.5, 17)):
        d.sym((x, x + 2.5, 0, 3, -20.5, -19), "bone", "foot_", "claw %d" % (k + 1), **plate())
    d.leg(13, 8, -8.5, 8.5, 6, 88, 46, "wolf")
    d.thigh((4.5, 21.5, 64, 87.5, -9, 9), "team", "torn shorts")
    d.thigh((8, 18, 61, 64, -9, 9), "team", "shorts rag")
    d.box((-21, 21, 80, 96, -13, 13), "team", "pelvis", "shorts top")
    d.box((-3, 3, 78, 86, 13, 22), "wolf", "pelvis", "tail")
    d.box((-2.5, 2.5, 66, 78, 18, 24), "wolf_lt", "pelvis", "tail tip")
    d.box((-24, 24, 94, 130, -14, 14), "team", "torso", "shirt")
    for k, (x0, x1) in enumerate(((-23.5, -14), (-8, 2), (10, 20))):
        d.box((x0, x1, 90, 94.5, -13.5, 13.5), "team", "torso", "rag %d" % (k + 1))
    d.box((-10, 6, 98, 120, -14.5, -14), "wolf_lt", "torso", "rip", **plate())
    d.box((-26, 26, 126, 143.5, -15, 15), "wolf", "torso", "hunched back")
    d.box((-16, 16, 134, 143.5, 15, 19), "wolf", "torso", "hackles")
    d.sym((26, 36, 108, 138, -11, 11), "wolf", "torso", "shoulder")
    d.sym((27, 35, 102, 108, -8, 8), "wolf_lt", "torso", "claws")
    d.box((-9.5, 9.5, 145, 165, -8, 10), "wolf", "head", "head")
    d.box((-5.5, 5.5, 146, 156, -16, -8), "wolf_lt", "head", "snout")
    d.box((-2.5, 2.5, 152.5, 155.5, -16.5, -15), "eye", "head", "nose")
    d.box((-5, 5, 146, 147.5, -16.5, -16), "white", "head", "teeth", **plate())
    d.sym((2.5, 7, 158, 160.5, -8.5, -8), "zeye", "head", "eye", **plate())
    d.sym((3, 8.5, 165, 170, -3, 4), "wolf", "head", "ear")
    d.sym((4.5, 7.5, 170, 175, -2, 3), "wolf", "head", "ear tip")
    d.box((-10, 10, 150, 160, 10, 13), "wolf", "head", "mane")
    return d


def dino():
    d = Design("DINO", "the dinosaur costume: a green hood with an open toothy jaw round the face, team spikes from the crown down the back to a long tail, a team belly")
    d.foot((5.5, 21.5, 0, 8, -19, 9), "team", "dino slipper")
    for k, x in enumerate((6.5, 12, 17.5)):
        d.sym((x, x + 3, 0, 3, -20.5, -19), "white", "foot_", "toe claw %d" % (k + 1), **plate())
    d.leg(13, 8.5, -9, 9, 7, 90, 47, "forest")
    d.box((-21, 21, 80, 97, -14, 14), "forest", "pelvis", "hips")
    d.box((-12, 12, 80, 96, -14.5, -14), "team", "pelvis", "belly low", **plate())
    d.box((-5, 5, 70, 88, 14, 22), "forest", "pelvis", "tail 1")
    d.box((-5.5, 5.5, 74, 80, 14.5, 22.5), "team", "pelvis", "tail band")
    d.box((-4, 4, 52, 74, 20, 28), "forest", "pelvis", "tail 2")
    d.box((-3, 3, 38, 54, 26, 33), "forest", "pelvis", "tail 3")
    for k, (y, z) in enumerate(((84, 22), (68, 28), (50, 33))):
        d.box((-1.5, 1.5, y - 8, y, z, z + 4), "team", "pelvis", "tail spike %d" % (k + 1))
    d.box((-23, 23, 95, 138, -14.5, 14.5), "forest", "torso", "body")
    d.box((-16, 16, 97, 136, -16, -14.5), "team", "torso", "belly")
    for k, y in enumerate((106, 116, 126)):
        d.box((-14.5, 14.5, y, y + 1, -16.5, -16), "team_dk", "torso", "belly ridge %d" % (k + 1), **plate())
    for k, y in enumerate((100, 111, 122, 133)):
        d.box((-2, 2, y, y + 9, 14.5, 21), "team", "torso", "spike %d" % (k + 1))
    d.sym((23, 33, 120, 137, -10, 10), "forest", "torso", "shoulder")
    d.sym((23.5, 33.5, 114, 120, -9, 9), "team", "torso", "costume paw")
    d.box((-13, 13, 136, 143.5, -11, 11), "forest", "torso", "neck")
    face(d, neck=False, y0=145, y1=160)
    d.box((-10.5, 10.5, 140, 147, -11, 9.5), "forest", "head", "lower jaw")
    d.box((-9.5, 9.5, 140.5, 146, -11.5, -11), "team", "head", "jaw lining", **plate())
    d.box((-10, 10, 160, 172, -13.5, 10), "forest", "head", "upper jaw")
    for k, x in enumerate((-8, -3.5, 1, 5.5)):
        d.box((x, x + 2.5, 157.5, 160, -13, -11), "white", "head", "tooth %d" % (k + 1))
    for k, x in enumerate((-7, -1, 4.5)):
        d.box((x, x + 2.5, 147, 149.5, -11.5, -10.5), "white", "head", "low tooth %d" % (k + 1))
    d.sym((4, 8, 166, 169.5, -14, -13.5), "eye", "head", "costume eye", **plate())
    d.box((-1.5, 1.5, 172, 178, -6, 4), "team", "head", "crown spike")
    d.box((-1.5, 1.5, 156, 168, 10, 15), "team", "head", "nape spike")
    return d


def penguin():
    d = Design("PENGUIN", "the penguin mascot: a pear-shaped black body with a white front, flippers, an orange beak and wide orange feet, team flippers, a team bobble hat and scarf")
    d.foot((4, 22, 0, 4, -21, 6), "carrot", "flat foot")
    d.leg(13, 7, -7.5, 7.5, 4, 88, 46, "hairblk")
    d.thigh((2, 24, 52, 86.5, -15, 15), "hairblk", "lower body")
    d.thigh((4, 22, 52, 86, -16, -15), "white", "lower belly", **plate())
    d.box((-24, 24, 78, 98, -16, 16), "hairblk", "pelvis", "hips")
    d.box((-18, 18, 80, 97, -16.5, -16), "white", "pelvis", "belly low", **plate())
    d.box((-21, 21, 96, 136, -14, 14), "hairblk", "torso", "body")
    d.box((-15, 15, 96, 132, -15, -14), "white", "torso", "belly", **plate())
    d.sym((21, 24, 96, 130, -7, 7), "team", "torso", "flipper")
    d.sym((24, 26, 94, 116, -6, 6), "team_dk", "torso", "flipper tip")
    d.box((-16, 16, 130, 141, -15.5, 15.5), "team", "torso", "scarf")
    d.box((6, 13, 104, 130.5, -16, -15.5), "team", "torso", "scarf end", **plate())
    d.box((-12, -4, 110, 130, 14, 14.5), "team", "torso", "scarf back", **plate())
    d.box((-11, 11, 140, 164, -11, 11), "hairblk", "head", "head")
    d.box((-7, 7, 146, 160, -11.5, -11), "white", "head", "face", **plate())
    d.sym((2.5, 5, 154, 157, -12, -11.5), "eye", "head", "eye", **plate())
    d.box((-3, 3, 149, 153, -16, -11.5), "carrot", "head", "beak")
    d.box((-11.5, 11.5, 160, 166, -11.5, 11.5), "team_dk", "head", "hat cuff")
    d.box((-10.5, 10.5, 166, 175, -10.5, 10.5), "team", "head", "bobble hat")
    d.box((-4, 4, 175, 179.5, -4, 4), "team_dk", "head", "bobble")
    d.box((-4, 4, 132, 138, -15, -14), "team_dk", "torso", "bow tie", **plate())
    return d


def bunny():
    d = Design("BUNNY", "the bunny mascot: tall upright ears with pink insides, a round grey-white head with buck teeth, team dungarees, a cotton tail, long feet")
    d.foot((5, 21, 0, 6, -22, 8), "pale", "long foot")
    d.sym((7, 19, 0, 2, -23, -22), "pink", "foot_", "toe pad", **plate())
    d.leg(13, 7.5, -8, 8, 5, 90, 47, "team")
    d.shin((5, 21, 5.5, 10, -8.5, 8.5), "pale", "ankle fur")
    d.box((-20, 20, 82, 97, -12.5, 12.5), "team", "pelvis", "hips")
    d.box((-4, 4, 86, 94, 12.5, 18), "white", "pelvis", "cotton tail")
    d.box((-22, 22, 95, 136, -13.5, 13.5), "pale", "torso", "body")
    d.box((-14, 14, 95, 122, -14.5, -13.5), "team", "torso", "dungaree bib", **plate())
    d.box((-16, 16, 95, 112, 13.5, 14.5), "team", "torso", "dungaree back", **plate())
    d.sym((8, 12, 122, 136, -14.5, -13.5), "team", "torso", "strap", **plate())
    d.sym((8, 12, 112, 136, 13.5, 14.5), "team", "torso", "strap back", **plate())
    d.sym((8.5, 11.5, 117, 120, -15, -14.5), "gold", "torso", "button", **plate())
    d.box((-22.5, 22.5, 94, 102, -14, 14), "team", "torso", "waistband")
    d.sym((22, 32, 120, 136, -10, 10), "pale", "torso", "shoulder")
    d.box((-12, 12, 136, 143.5, -10, 10), "pale", "torso", "neck fur")
    d.box((-11, 11, 143, 164, -11, 11), "pale", "head", "head")
    d.box((-6, 6, 145, 152, -12, -11), "white", "head", "muzzle")
    d.box((-1.5, 1.5, 151, 153.5, -12.5, -12), "pink", "head", "nose", **plate())
    d.box((-2, 2, 142, 145, -11.5, -10.5), "white", "head", "buck teeth")
    d.sym((3, 6.5, 155, 159, -11.5, -11), "eye", "head", "eye", **plate())
    d.sym((6, 12, 148.5, 149.5, -12.5, -11.5), "eye", "head", "whisker", **plate())
    d.sym((2.5, 8, 164, 179.5, -2.5, 2.5), "pale", "head", "ear")
    d.sym((3.5, 7, 166, 178, -3, -2.5), "pink", "head", "inner ear", **plate())
    return d


def mushroom():
    d = Design("MUSHROOM", "the mushroom folk: a wide red cap with white spots over a cream face, a team tunic and shorts, cream legs, brown shoes")
    d.foot((6, 20, 0, 8, -16, 8), "leather", "shoe")
    d.leg(13, 7, -7.5, 7.5, 7, 90, 47, "cream")
    d.thigh((5, 21, 68, 89.5, -9, 9), "team", "shorts")
    d.box((-20, 20, 80, 97, -13, 13), "team", "pelvis", "tunic hem")
    d.box((-21.5, 21.5, 94, 136, -13, 13), "team", "torso", "tunic")
    d.box((-22, 22, 98, 103, -13.5, 13.5), "leather", "torso", "belt")
    d.box((-3, 3, 99, 102, -14, -13.5), "gold", "torso", "buckle", **plate())
    d.sym((21.5, 32, 118, 136, -10, 10), "team", "torso", "sleeve")
    d.box((-13, 13, 134, 143.5, -11, 11), "cream", "torso", "gill collar")
    d.box((-4, 4, 141, 147, -4, 4), "cream", "head", "neck")
    d.box((-8.5, 8.5, 146, 162, -9, 9), "cream", "head", "face")
    d.sym((2.5, 5.5, 154, 157.5, -9.5, -9), "eye", "head", "eye", **plate())
    d.box((-2.5, 2.5, 150, 151, -9.5, -9), "eye", "head", "mouth", **plate())
    d.sym((4.5, 7.5, 151, 153, -9.5, -9), "pink", "head", "blush", **plate())
    d.box((-12, 12, 160, 168, -12, 12), "capred", "head", "cap")
    d.box((-16.5, 16.5, 161, 166, -4, 4), "capred", "head", "cap x")
    d.box((-4, 4, 161, 166, -16.5, 16.5), "capred", "head", "cap z")
    d.box((-10, 10, 168, 174, -10, 10), "capred", "head", "cap dome")
    d.box((-6, 6, 174, 177, -6, 6), "capred", "head", "cap top")
    d.box((-11.5, 11.5, 159.5, 160.5, -11.5, 11.5), "cream", "head", "gills")
    for k, (x0, x1, y0, y1) in enumerate(((-9, -4, 168.5, 172.5), (5, 9, 162, 165.5))):
        d.box((x0, x1, y0, y1, -12.5, -12), "white", "head", "spot %d" % (k + 1), **plate())
    d.box((-3, 3, 177, 177.5, -3, 3), "white", "head", "top spot", **plate())
    d.box((4, 8, 169, 172.5, 10, 10.5), "white", "head", "back spot", **plate())
    d.sym((16.5, 17, 162, 165, -1.5, 1.5), "white", "head", "side spot", **plate())
    d.box((-2, 2, 162, 166, -17, -16.5), "white", "head", "front spot", **plate())
    return d


def golem():
    d = Design("GOLEM", "the golem: a hulking body of uneven stone blocks with team-lit cracks, mossy shoulders, a small head sunk between them with a glowing eye slit")
    d.foot((4.5, 23.5, 0, 11, -19, 10), "stone_dk", "stone foot")
    d.leg(14, 9.5, -10, 10, 11, 88, 46, "stone")
    d.shin((4, 24, 26, 29, -10, 10), "team", "seam shin")
    d.shin((4, 24, 38, 41, -10, 10), "team", "seam knee")
    d.thigh((5, 23, 66, 76, -10.5, 10.5), "stone_dk", "thigh block")
    d.thigh((4, 24, 62, 65, -10.5, 10.5), "team", "seam thigh")
    d.thigh((9, 11, 76, 86, -10.5, -10), "team", "thigh crack", **plate())
    d.box((-24, 24, 78, 96, -15, 15), "stone_dk", "pelvis", "hips")
    d.box((-26, 26, 94, 132, -16, 16), "stone", "torso", "body")
    d.box((-20, 8, 100, 124, -17.5, -16), "stone_dk", "torso", "chest slab")
    d.box((8, 22, 110, 130, -17, -16), "stone", "torso", "chest stone")
    d.box((-4, 4, 96, 116, -18, -17.5), "team", "torso", "crack 1", **plate())
    d.box((-8, 8, 128, 141, -14.5, -14), "team", "torso", "throat glow", **plate())
    d.box((-14, 8, 115, 117, -18.5, -18), "team", "torso", "crack 2", **plate())
    d.box((8, 9, 104, 130, -17.5, -17), "team", "torso", "crack 3", **plate())
    d.box((-16, 16, 98, 122, 16, 17), "team", "torso", "back core", **plate())
    d.box((-24, 24, 128, 142, -14, 14), "stone", "torso", "shoulder mass")
    d.sym((24, 37, 106, 140, -10, 10), "stone", "torso", "shoulder")
    d.sym((26, 36, 140, 143, -7, 7), "moss", "torso", "moss")
    d.sym((37, 38, 108, 138, -8, 8), "team", "torso", "arm glow", **plate())
    d.box((-18, 18, 140, 143.5, -10, 10), "moss", "torso", "moss ridge")
    d.box((-26.5, 26.5, 124, 127, -16.5, 16.5), "team", "torso", "seam chest")
    d.sym((24.5, 37.5, 120, 123, -10.5, 10.5), "team", "torso", "seam shoulder")
    d.box((-24.5, 24.5, 86, 89, -15.5, 15.5), "team", "pelvis", "seam hips")
    d.box((-8, 8, 142, 160, -12, 6), "stone_dk", "head", "head")
    d.box((-9, 9, 152, 158, -10, 7), "stone", "head", "brow")
    d.box((-6, 6, 149, 151.5, -12.5, -12), "team", "head", "eye slit", **plate())
    d.box((-6, 6, 160, 162, -8, 4), "moss", "head", "moss cap")
    d.box((-9.5, 9.5, 145, 148, -12.5, 6.5), "team", "head", "jaw seam")
    return d


def tvhead():
    d = Design("TVHEAD", "the TV head: an old cream CRT set for a head with a lit green screen face and rabbit-ear antennae, a team suit jacket, grey trousers, white shirt, dark tie")
    d.foot((6, 20, 0, 7, -16, 8), "dark", "shoe")
    d.leg(13, 7, -7.5, 7.5, 6, 90, 47, "mid_dk")
    d.box((-19, 19, 82, 96, -12, 12), "mid_dk", "pelvis", "hips")
    d.box((-21, 21, 94, 137, -12.5, 12.5), "team", "torso", "jacket")
    d.box((-6, 6, 112, 137, -13.5, -12.5), "white", "torso", "shirt", **plate())
    d.box((-2, 2, 106, 134, -14, -13.5), "dark", "torso", "tie", **plate())
    d.sym((6, 9, 110, 137, -13.5, -12.5), "team_dk", "torso", "lapel", **plate())
    d.box((-10, -4, 98, 100, -13, -12.5), "team_dk", "torso", "pocket", **plate())
    d.sym((21, 31, 120, 137, -9.5, 9.5), "team", "torso", "shoulder")
    d.box((-10, 10, 136, 141, -9, 9), "white", "torso", "collar")
    d.box((-4, 4, 141, 145, -4, 4), "dark", "head", "neck")
    d.box((-12.5, 12.5, 145, 167, -11.5, 9), "crt", "head", "tv")
    d.box((-9, 9, 148, 164, 9, 12), "crt", "head", "tube back")
    d.box((-12, 7, 147.5, 165, -12, -11.5), "dark", "head", "screen bezel", **plate())
    d.box((-10.5, 5.5, 149, 163.5, -12.5, -12), "screen", "head", "screen", **plate())
    d.sym((-6.5, -4, 156, 159, -13, -12.5), "dark", "head", "pixel eye", **plate())
    d.box((-7, 2.5, 152, 153.5, -13, -12.5), "dark", "head", "pixel smile", **plate())
    d.box((8.5, 11, 159, 162, -12.5, -11.5), "dark", "head", "knob 1")
    d.box((8.5, 11, 153, 156, -12.5, -11.5), "dark", "head", "knob 2")
    d.box((-12, 12, 144.5, 146.5, -11, 8.5), "dark", "head", "tv foot")
    d.box((-3, 3, 167, 169, -3, 3), "dark", "head", "antenna base")
    d.sym((3, 4.5, 169, 174, -1, 1), "light", "head", "antenna 1")
    d.sym((4.5, 6, 174, 179.5, -1, 1), "light", "head", "antenna 2")
    return d


def plaguedoc():
    d = Design("PLAGUEDOC", "the plague doctor: a long ivory beak mask with round glass eyes, a wide-brim dark hat, a long team-dark coat to the shins, a short shoulder cape")
    d.foot((6, 20, 0, 9, -16, 8), "dark", "boot")
    d.leg(13, 7, -7.5, 7.5, 8, 90, 47, "dark")
    d.thigh((3, 23, 52, 89.5, -11, 11), "team_dk", "coat skirt")
    d.shin((3.5, 22.5, 24, 46.5, -10.5, 10.5), "team_dk", "coat hem")
    d.box((-21, 21, 82, 97, -13, 13), "team_dk", "pelvis", "coat hips")
    d.box((-22, 22, 95, 136, -13.5, 13.5), "team_dk", "torso", "coat")
    d.box((-22.5, 22.5, 96, 101, -14, 14), "leather", "torso", "belt")
    d.box((-3, 3, 97, 100, -14.5, -14), "gold", "torso", "buckle", **plate())
    for k, y in enumerate((106, 116, 126)):
        d.box((-1.5, 1.5, y, y + 3, -14, -13.5), "gold", "torso", "button %d" % (k + 1), **plate())
    d.box((-25, 25, 122, 140, -15.5, 15.5), "dark", "torso", "shoulder cape")
    d.sym((25, 34, 118, 138, -10.5, 10.5), "dark", "torso", "cape shoulder")
    d.box((-12, 12, 138, 143.5, -11, 11), "dark", "torso", "collar")
    d.box((-9.5, 9.5, 143, 165, -10, 10), "dark", "head", "hood")
    d.box((-8, 8, 146, 162, -11, -10), "ivory", "head", "mask")
    d.box((-4.5, 4.5, 147, 157, -14.5, -10.5), "ivory", "head", "beak 1")
    d.box((-3, 3, 144, 153, -17, -14), "ivory", "head", "beak 2")
    d.sym((2, 7, 156, 161, -11.5, -11), "brass_dk", "head", "lens rim", **plate())
    d.sym((3, 6, 157, 160, -12, -11.5), "glass", "head", "lens", **plate())
    brim(d, 164, "dark", "hat brim")
    d.box((-8.5, 8.5, 166, 177, -8.5, 8.5), "dark", "head", "hat crown")
    d.box((-9, 9, 166.5, 169, -9, 9), "team", "head", "hat band")
    return d


def guard():
    d = Design("GUARD", "the royal guard: a towering black bearskin hat, a team tunic with gold buttons, white cross-belts, dark trousers with a team stripe, shiny boots")
    d.foot((6, 20, 0, 12, -17, 8), "hairblk", "boot")
    d.sym((10, 16, 9, 12, -17.5, -17), "white", "foot_", "boot shine", **plate())
    d.leg(13, 7, -7.5, 7.5, 11, 90, 47, "dark")
    d.thigh((5.5, 20.5, 46, 89.5, -3, 3), "team", "stripe")
    d.shin((6, 20, 11.5, 46.5, -2.5, 2.5), "team", "shin stripe")
    d.box((-19, 19, 82, 96, -12, 12), "dark", "pelvis", "hips")
    d.box((-21, 21, 86, 137, -12.5, 12.5), "team", "torso", "tunic")
    d.box((-21.5, 21.5, 96, 101, -13, 13), "white", "torso", "belt")
    d.box((-3, 3, 97, 100, -13.5, -13), "gold", "torso", "buckle", **plate())
    for k, y in enumerate((104, 112, 120, 128)):
        d.box((-1.5, 1.5, y, y + 3, -13.5, -13), "gold", "torso", "button %d" % (k + 1), **plate())
    for k in range(5):
        x0 = -17 + k * 6
        d.box((x0, x0 + 6, 106 + k * 6, 112 + k * 6, -14, -12.5), "white", "torso", "cross belt %d" % (k + 1), **plate())
        d.box((x0, x0 + 6, 106 + k * 6, 112 + k * 6, 12.5, 14), "white", "torso", "cross belt back %d" % (k + 1), **plate())
    d.sym((21, 31, 122, 138, -10, 10), "team", "torso", "shoulder")
    d.sym((21.5, 31.5, 136, 139, -8, 8), "gold", "torso", "epaulette")
    d.box((-11, 11, 136, 143.5, -10, 10), "team_dk", "torso", "collar")
    face(d, y1=160)
    d.box((-10, 10, 160, 179.5, -10.5, 10.5), "hairblk", "head", "bearskin")
    d.box((-11.5, 11.5, 162, 175, -11.5, 11.5), "hairblk", "head", "bearskin bulge")
    d.box((-10, 10, 145.5, 147.5, -10.5, -9.5), "gold", "head", "chin strap")
    d.sym((9.5, 10, 147, 158, -2, 0), "gold", "head", "strap side", **plate())
    return d


def caveman():
    d = Design("CAVEMAN", "the caveman: a wild black mane and beard with a bone through it, a one-shoulder spotted team pelt, bare skin, a knobbly club on the back")
    d.foot((6, 20, 0, 6, -16, 8), "fur", "fur wrap")
    d.leg(13, 8, -8.5, 8.5, 5, 89, 46, "skin")
    d.thigh((4.5, 21.5, 66, 88.5, -9, 9), "team", "pelt skirt")
    d.thigh((9, 13, 72, 76, -9.5, -9), "eye", "skirt spot", **plate())
    d.box((-21, 21, 80, 96, -13, 13), "team", "pelvis", "pelt hips")
    d.box((-21.5, 21.5, 94, 137, -12, 12), "skin", "torso", "chest")
    d.box((-22, 8, 93.5, 120, -13, 13), "team", "torso", "pelt")
    for k, (x, y) in enumerate(((-16, 98), (-6, 108), (2, 99), (-18, 112))):
        d.box((x, x + 4, y, y + 4, -13.5, -13), "eye", "torso", "spot %d" % (k + 1), **plate())
    d.box((-12, 0, 100, 104, 13, 13.5), "eye", "torso", "back spot", **plate())
    d.box((-22, -8, 120, 140, -13.5, 13.5), "team", "torso", "pelt strap")
    d.box((-34, -21.5, 122, 139.5, -10, 10), "team", "torso", "pelt shoulder")
    d.box((21.5, 32, 118, 137, -9, 9), "skin", "torso", "bare shoulder")
    d.box((-3, 3, 99, 140, 12, 15), "hat", "torso", "club handle")
    d.box((-6, 6, 128, 141, 12.5, 18), "hat", "torso", "club head")
    d.box((-7, 7, 132, 138, 14, 16.5), "hat_dk", "torso", "club knob")
    face(d)
    d.box((-12, 12, 158, 172, -11, 12), "hairblk", "head", "mane")
    d.box((-13.5, 13.5, 146.5, 160, -4, 11), "hairblk", "head", "mane sides")
    d.box((-9, 9, 144, 153, -12, -10), "hairblk", "head", "beard")
    d.box((-13, 13, 168, 170, -3, -1), "bone", "head", "bone")
    d.sym((12.5, 15, 167, 171, -3.5, -0.5), "bone", "head", "bone knob")
    d.box((-7, 7, 162, 163.5, -11.5, -10.5), "hairblk", "head", "brow")
    return d


def dancer():
    d = Design("DANCER", "the ballerina: a wide flat team tutu, a team bodice, pink tights and ballet shoes, a hair bun with a gold tiara")
    d.foot((7.5, 18.5, 0, 6, -14, 6), "pink", "ballet shoe")
    d.shin((7.5, 18.5, 4, 8, -1, 1), "pink", "ribbon", **plate())
    d.leg(13, 5.5, -6, 6, 5, 90, 47, "tights")
    d.box((-16, 16, 86, 97, -10, 10), "team", "pelvis", "hips")
    d.box((-26, 26, 86.5, 93, -20, 20), "team", "pelvis", "tutu")
    d.box((-32, 32, 87, 92, -11, 11), "team", "pelvis", "tutu x")
    d.box((-12, 12, 87, 92, -27, 27), "team", "pelvis", "tutu z")
    d.box((-27, 27, 91.5, 92.5, -21, 21), "white", "pelvis", "tutu layer")
    d.box((-17, 17, 96, 132, -10.5, 10.5), "team", "torso", "bodice")
    d.box((-17.5, 17.5, 95.5, 99, -11, 11), "white", "torso", "waist")
    d.box((-14, 14, 128, 137, -9, 9), "tights", "torso", "shoulders")
    d.sym((14, 25, 126, 136, -7, 7), "tights", "torso", "shoulder")
    d.box((-4, 4, 136, 141, -4, 4), "tights", "torso", "neck")
    d.box((-4, 4, 141, 147, -4, 4), "tights", "head", "upper neck")
    d.box((-8.5, 8.5, 146, 164, -9, 9), "tights", "head", "face")
    d.sym((2.5, 5.5, 156, 157.5, -9.5, -9), "eye", "head", "eye", **plate())
    d.box((-2, 2, 150.5, 151.5, -9.5, -9), "glove", "head", "lips", **plate())
    d.box((-9, 9, 158, 168, -8, 9.5), "hair", "head", "hair")
    d.box((-5, 5, 166, 174, -2, 8), "hair", "head", "bun")
    d.box((-7, 7, 166, 169, -9, -8), "gold", "head", "tiara")
    d.box((-2, 2, 169, 171.5, -9, -8), "gold", "head", "tiara peak")
    return d


def tourist():
    d = Design("TOURIST", "the tourist: a wide straw sun hat, shades, a team flowered shirt, a camera on the chest, a backpack, khaki shorts, white socks and sandals")
    d.foot((6, 20, 0, 3, -16, 8), "leather", "sandal")
    d.shin((7, 19, 3, 18, -6.5, 6.5), "white", "sock")
    d.leg(13, 7, -7.5, 7.5, 17, 90, 47, "skin")
    d.thigh((4.5, 21.5, 64, 89.5, -9, 9), "khaki", "shorts")
    d.box((-20, 20, 82, 97, -12.5, 12.5), "khaki", "pelvis", "hips")
    d.box((-23, 23, 94, 137, -14, 14), "team", "torso", "shirt")
    for k, (x, y) in enumerate(((-18, 100), (-19, 112), (10, 98), (-20, 124), (12, 124))):
        d.box((x, x + 5, y, y + 5, -14.5, -14), "white", "torso", "flower %d" % (k + 1), **plate())
    d.box((-8, 8, 104, 114, -18, -14), "dark", "torso", "camera")
    d.box((-3, 3, 105.5, 111.5, -20, -18), "mid", "torso", "lens")
    d.box((-10, 10, 114, 135, -14.5, -14), "dark", "torso", "camera strap", **plate())
    d.box((-14, 14, 100, 132, 14, 22), "leather", "torso", "backpack")
    d.box((-10, 10, 104, 116, 22, 23), "team_dk", "torso", "backpack pocket", **plate())
    d.sym((23, 33, 120, 137, -10, 10), "team", "torso", "sleeve")
    d.box((-10, 10, 136, 141, -9, 9), "team", "torso", "collar")
    face(d, y1=165, eyes=False)
    d.box((-8, 8, 157, 160, -10.5, -10), "eye", "head", "shades", **plate())
    d.box((-1.5, 1.5, 154, 157, -11, -10), "glove", "head", "sunburnt nose")
    brim(d, 164, "straw", "sun hat brim")
    d.box((-9, 9, 166, 173, -9, 9), "straw", "head", "sun hat crown")
    d.box((-9.5, 9.5, 166.5, 168.5, -9.5, 9.5), "team", "head", "hat band")
    return d


def surfer():
    d = Design("SURFER", "the surfer: a tall team surfboard on the back past the head, shaggy blond hair, shades, bare tan chest with a shell necklace, team board shorts")
    d.foot((6, 20, 0, 3, -16, 8), "dark", "flip flop")
    d.leg(13, 7, -7.5, 7.5, 2, 90, 47, "tan")
    d.thigh((4.5, 21.5, 60, 89.5, -9, 9), "team", "board shorts")
    d.thigh((4, 22, 66, 69, -9.5, 9.5), "white", "shorts stripe")
    d.box((-20, 20, 82, 97, -12.5, 12.5), "team", "pelvis", "shorts top")
    d.box((-21, 21, 95, 137, -12, 12), "tan", "torso", "chest")
    d.box((-10, 10, 128, 131, -12.5, -12), "white", "torso", "necklace", **plate())
    d.box((-2, 2, 125, 128, -12.5, -12), "pink", "torso", "shell", **plate())
    d.sym((21, 31, 118, 137, -9, 9), "tan", "torso", "shoulder")
    # the board: up the back from the knees to over the head (inside the head cylinder above the neck)
    d.box((-8, 8, 60, 176, 12, 15), "team", "torso", "surfboard")
    d.box((-5.5, 5.5, 176, 179.5, 12, 15), "team", "torso", "board nose")
    d.box((-1, 1, 62, 176, 15, 15.5), "white", "torso", "stringer", **plate())
    d.box((-2, 2, 56, 60, 13, 14), "dark", "torso", "fin")
    d.box((-4, 4, 136, 141, -4, 4), "tan", "torso", "neck")
    face(d, skin="tan", neck=False, eyes=False)
    d.box((-4, 4, 140.5, 147, -4, 4), "tan", "head", "neck")
    d.box((-8, 8, 156.5, 159.5, -10.5, -10), "eye", "head", "shades", **plate())
    d.box((-3, 3, 150.5, 151.5, -10.5, -10), "white", "head", "grin", **plate())
    d.box((-10.5, 10.5, 160, 170, -10.5, 10.5), "straw", "head", "hair")
    d.box((-11, 11, 150, 162, -2, 11), "straw", "head", "hair sides")
    for k, x in enumerate((-9, -3, 3)):
        d.box((x, x + 6, 161, 163.5, -11, -10.5), "straw", "head", "fringe %d" % (k + 1), **plate())
    d.box((-6, 6, 170, 172.5, -6, 6), "straw", "head", "hair top")
    return d


def rockstar():
    d = Design("ROCKSTAR", "the rockstar: a huge black hair mane, a team guitar slung across the front, a sleeveless black vest over a team shirt, skinny jeans, team sneakers")
    d.foot((6, 20, 2, 7, -16, 8), "team", "sneaker")
    d.sym((5.5, 20.5, 0, 2, -16.5, 8.5), "white", "foot_", "sole")
    d.leg(13, 6, -6.5, 6.5, 6, 90, 47, "denim")
    d.thigh((6.5, 19.5, 82, 88, -7, 7), "team", "bandana")
    d.shin((7.5, 18.5, 22, 28, -7, -6.5), "skin", "knee rip", **plate())
    d.box((-18, 18, 82, 96, -11, 11), "hairblk", "pelvis", "belt")
    d.box((-3, 3, 86, 92, -11.5, -11), "gold", "pelvis", "buckle", **plate())
    d.box((-20, 20, 94, 136, -12, 12), "team", "torso", "shirt")
    d.sym((12, 20.5, 93.5, 136.5, -12.5, 12.5), "hairblk", "torso", "vest")
    d.sym((20, 29, 120, 136, -8.5, 8.5), "skin", "torso", "bare shoulder")
    d.sym((20.5, 29.5, 116, 120, -9, 9), "team", "torso", "wristband")
    # the guitar: a body at the hip, the neck up across the chest to the left shoulder
    d.box((-6, 16, 80, 106, -17, -13), "team", "torso", "guitar body")
    d.box((-2, 12, 88, 100, -17.5, -17), "white", "torso", "pickguard", **plate())
    d.box((2, 6, 92, 96, -18, -17.5), "dark", "torso", "pickup", **plate())
    for k in range(5):
        x = -1 - k * 4.5
        d.box((x - 4.5, x, 104 + k * 5, 109 + k * 5, -16, -14), "hat_dk", "torso", "neck %d" % (k + 1))
    d.box((-28, -23.5, 127, 136, -16, -13), "hairblk", "torso", "headstock")
    for k in range(4):
        d.box((-1 - k * 6, 5 - k * 6, 108 + k * 6, 112 + k * 6, -13, -12.5), "hairblk", "torso", "strap %d" % (k + 1), **plate())
    d.box((-10, 10, 136, 141, -8, 8), "skin", "torso", "neck")
    face(d, neck=False, eyes=False)
    d.box((-4.5, 4.5, 140.5, 147, -4.5, 4.5), "skin", "head", "neck")
    d.box((-8, 8, 156.5, 159.5, -10.5, -10), "eye", "head", "shades", **plate())
    d.box((-12, 12, 158, 174, -11, 12), "hairblk", "head", "mane")
    d.box((-14, 14, 144, 164, -3, 10.5), "hairblk", "head", "mane sides")
    d.box((-9, 9, 174, 178, -8, 9), "hairblk", "head", "mane top")
    d.box((-9.5, 9.5, 163, 166, -11.5, -11), "hairblk", "head", "fringe", **plate())
    return d


def lumberjack():
    d = Design("LUMBERJACK", "the lumberjack: a team plaid shirt, a bushy brown beard, a knit beanie, an axe on the back, rolled-up jeans and big boots")
    d.foot((5, 21, 0, 13, -18, 9), "leather", "work boot")
    d.leg(13, 8, -8.5, 8.5, 12, 90, 47, "denim")
    d.shin((4.5, 21.5, 12.5, 18, -9, 9), "denim", "rolled cuff")
    d.box((-20, 20, 82, 96, -12.5, 12.5), "denim", "pelvis", "hips")
    d.box((-23, 23, 94, 138, -14, 14), "team", "torso", "plaid shirt")
    for k, y in enumerate((100, 112, 124)):
        d.box((-23.5, 23.5, y, y + 3, -14.5, 14.5), "team_dk", "torso", "plaid h %d" % (k + 1))
    for k, x in enumerate((-15, -3, 9)):
        d.box((x, x + 3, 95, 137, -15, -14.5), "team_dk", "torso", "plaid v %d" % (k + 1), **plate())
        d.box((x, x + 3, 95, 137, 14.5, 15), "team_dk", "torso", "plaid v back %d" % (k + 1), **plate())
    d.box((-23.5, 23.5, 93, 97, -14.5, 14.5), "leather", "torso", "belt")
    d.sym((23, 34, 120, 138, -10.5, 10.5), "team", "torso", "shoulder")
    d.sym((23.5, 34.5, 126, 129, -11, 11), "team_dk", "torso", "shoulder plaid")
    d.box((-12, 12, 137, 143.5, -10, 10), "team_dk", "torso", "collar")
    # the axe: a handle up the back, the head beside the right shoulder blade
    d.box((9, 11.5, 92, 138, 15, 17), "hat", "torso", "axe handle")
    d.box((11.5, 20, 124, 136, 15.5, 16.5), "light", "torso", "axe head")
    d.box((20, 21, 122, 138, 15.5, 16.5), "white", "torso", "axe edge")
    face(d, y1=165)
    d.box((-10, 10, 143, 156, -12, 0), "hair", "head", "beard")
    d.box((-7, 7, 138, 143, -14, -11), "hair", "torso", "beard tip")
    d.box((-6, 6, 155, 157, -12.5, -11.5), "hair", "head", "moustache")
    d.box((-10.5, 10.5, 162, 166, -10.5, 10.5), "mid", "head", "beanie cuff")
    d.box((-10, 10, 166, 173, -10, 10), "mid_dk", "head", "beanie")
    d.box((-3, 3, 173, 176, -3, 3), "mid", "head", "beanie knot")
    return d


def cyborg():
    d = Design("CYBORG", "the cyborg: split down the middle, a human left half in team fatigues and a chrome right half with a red eye, a big piston arm mount and a mechanical leg")
    d.foot((5, 21, 0, 10, -18, 9), "mid", "steel foot")
    d.foot((-20, -6, 0, 8, -16, 8), "dark", "boot_l")
    d.box((5.5, 20.5, 46, 90, -7.5, 7.5), "light", "thigh_r", "steel thigh")
    d.box((9.5, 16.5, 46, 90, -8.5, -7.5), "mid", "thigh_r", "thigh piston", **plate())
    d.box((6, 20, 9, 48, -7, 7), "mid", "shin_r", "steel shin")
    d.box((4.5, 21.5, 42, 49, -8, 8), "dark", "shin_r", "knee joint")
    d.box((10, 16, 18, 38, -7.5, -7), "visor", "shin_r", "shin light", **plate())
    d.box((-20.5, -5.5, 46, 90, -8, 8), "team", "thigh_l", "fatigue thigh")
    d.box((-20, -6, 8, 48, -7.5, 7.5), "team", "shin_l", "fatigue shin")
    d.box((-20, 20, 82, 96, -12.5, 12.5), "dark", "pelvis", "hips")
    d.box((-22, 0, 94, 136, -13, 13), "team", "torso", "vest")
    d.box((0, 22, 94, 136, -13, 13), "light", "torso", "chrome chest")
    for k, y in enumerate((102, 110, 118)):
        d.box((3, 19, y, y + 4, -13.5, -13), "mid", "torso", "rib %d" % (k + 1), **plate())
    d.box((4, 12, 124, 132, -13.5, -13), "visor", "torso", "core", **plate())
    d.box((-18, -6, 112, 126, -13.5, -13), "team_dk", "torso", "pocket", **plate())
    d.box((22, 37, 110, 141, -11, 11), "mid", "torso", "arm mount")
    d.box((24, 36, 104, 110, -8, 8), "dark", "torso", "piston")
    d.box((22.5, 37.5, 128, 132, -9, 9), "light", "torso", "mount band")
    d.box((-31, -22, 120, 136, -9, 9), "team", "torso", "left shoulder")
    d.box((-12, 12, 136, 143.5, -10, 10), "dark", "torso", "collar")
    d.box((-5, 5, 141, 147, -5, 5), "mid", "head", "neck")
    d.box((-9.5, 0, 146, 166, -10, 10), "skin", "head", "face left")
    d.box((0, 10, 146, 167, -10.5, 10.5), "light", "head", "chrome right")
    d.box((-10, -0.5, 162, 168, -10.5, 10.5), "hair", "head", "hair")
    d.box((-6, -3, 157.5, 159.5, -10.5, -10), "eye", "head", "eye", **plate())
    d.box((2.5, 7.5, 156.5, 160.5, -11, -10.5), "redeye", "head", "red eye", **plate())
    d.box((10, 11.5, 152, 162, -4, 4), "dark", "head", "ear unit")
    d.box((0.5, 9, 149, 150.5, -11, -10.5), "mid", "head", "jaw plate", **plate())
    return d


# ---------------------------------------------------------------------------------------------------------------
# the shooter archetypes (original takes on the general roles) and PIMP

def goalie():
    d = Design("GOALIE", "the ice-hockey goalie: huge white leg pads with team bands, skates, a bulky team jersey with white stripes, a white mask with a cage, a stick on the back")
    d.foot((7, 19, 0, 2, -18, 10), "light", "skate blade")
    d.foot((5, 21, 2, 12, -17, 9), "dark", "skate boot")
    d.leg(13, 7.5, -8, 8, 11, 90, 47, "dark")
    # the pads: wide slabs in front of shin and thigh
    d.shin((2.5, 23.5, 8, 47, -15, -8.5), "white", "shin pad")
    for k, y in enumerate((16, 30)):
        d.shin((2, 24, y, y + 5, -15.5, -9), "team", "pad band %d" % (k + 1))
    d.shin((4, 22, 40, 48, -16, -15), "white", "knee roll", **plate())
    d.thigh((3, 23, 54, 89, -14, -8.5), "white", "thigh pad")
    d.thigh((2.5, 23.5, 68, 73, -14.5, -9), "team", "thigh band")
    d.box((-22, 22, 80, 97, -14, 14), "team", "pelvis", "pants")
    d.box((-26, 26, 92, 138, -17, 17), "team", "torso", "jersey")
    d.box((-26.5, 26.5, 98, 103, -17.5, 17.5), "white", "torso", "jersey stripe 1")
    d.box((-26.5, 26.5, 106, 108, -17.5, 17.5), "white", "torso", "jersey stripe 2")
    d.box((-20, 20, 112, 134, -18, -17), "team_dk", "torso", "chest protector", **plate())
    d.sym((26, 37, 112, 140, -11, 11), "team", "torso", "shoulder")
    d.sym((26.5, 37.5, 120, 124, -11.5, 11.5), "white", "torso", "sleeve stripe")
    d.box((-14, 14, 136, 143.5, -12, 12), "white", "torso", "collar")
    d.box((14, 18, 60, 141, 17, 20), "hat", "torso", "stick")
    d.box((12, 26, 52, 60, 17, 20), "hat", "torso", "stick blade")
    d.box((-11, 11, 144, 170, -12, 11), "white", "head", "mask")
    d.box((-11.5, 11.5, 150, 164, -1, 11.5), "team", "head", "mask paint")
    d.box((-7, 7, 147, 162, -13, -12), "bore", "head", "cage", **plate())
    for k, x in enumerate((-5, -1, 3)):
        d.box((x, x + 2, 146.5, 162.5, -14, -13), "light", "head", "cage bar %d" % (k + 1))
    d.box((-7.5, 7.5, 154, 156, -14.5, -14), "light", "head", "cage cross", **plate())
    d.box((-10, 10, 170, 172, -9, 8), "white", "head", "mask top")
    return d


def marine():
    d = Design("MARINE", "the marine: grim green-grey power armour, a broad helmet with a lit ember visor band and a jaw guard, huge squared team pauldrons, a chest plate with vents")
    d.foot((4.5, 22.5, 0, 14, -20, 11), "marine_dk", "armoured boot")
    d.leg(14, 8.5, -10, 10, 13, 89, 47, "marine", "marine_dk")
    d.shin((7, 21, 18, 44, -10.5, -10), "marine", "shin plate", **plate())
    d.shin((8.5, 19.5, 42, 48, -11, -10.5), "team", "knee guard", **plate())
    d.thigh((6.5, 21.5, 60, 84, -10.5, -10), "team", "thigh plate", **plate())
    d.box((-23, 23, 80, 96, -14, 14), "marine_dk", "pelvis", "hips")
    d.box((-7, 7, 78, 92, -15, -14), "marine", "pelvis", "codpiece", **plate())
    d.box((-24, 24, 94, 136, -15, 15), "marine", "torso", "torso")
    d.box((-20, 20, 112, 135, -17, -15), "marine_dk", "torso", "chest plate")
    for k, y in enumerate((116, 121, 126)):
        d.box((-14, -4, y, y + 2, -17.5, -17), "bore", "torso", "vent l %d" % (k + 1), **plate())
        d.box((4, 14, y, y + 2, -17.5, -17), "bore", "torso", "vent r %d" % (k + 1), **plate())
    d.box((-22, 22, 98, 110, -16, -15), "team_dk", "torso", "abdomen", **plate())
    d.box((-16, 16, 100, 134, 15, 21), "marine_dk", "torso", "back unit")
    d.sym((9, 13, 106, 130, 21, 22), "ember", "torso", "exhaust", **plate())
    d.sym((22, 38, 114, 143.5, -11, 11), "team", "torso", "pauldron")
    d.sym((22.5, 38.5, 112, 116, -9, 9), "marine_dk", "torso", "pauldron rim")
    d.sym((30, 34, 124, 136, -11.5, -11), "marine_dk", "torso", "pauldron mark", **plate())
    d.box((-15, 15, 134, 143.5, -12, 12), "marine_dk", "torso", "gorget")
    d.box((-11, 11, 143, 168, -11.5, 11.5), "marine", "head", "helmet")
    d.box((-11.5, 11.5, 155.5, 160, -12.5, 10), "bore", "head", "visor band")
    d.box((-10, 10, 156, 159.5, -13, -12.5), "ember", "head", "visor", **plate())
    d.box((-8, 8, 145, 154, -12.5, -11.5), "marine_dk", "head", "jaw guard")
    for k, x in enumerate((-5, -1, 3)):
        d.box((x, x + 2, 147, 152, -13, -12.5), "bore", "head", "grille %d" % (k + 1), **plate())
    d.box((-9, 9, 168, 171, -9, 9), "marine_dk", "head", "helmet top")
    d.sym((11, 13, 150, 164, -6, 6), "marine_dk", "head", "ear guard")
    return d


def medic():
    d = Design("MEDIC", "the field medic: a pale grey field top, team trousers, a team armband and cap with white crosses, a big white med pack on the back, a pouch belt")
    d.foot((5.5, 20.5, 0, 11, -17, 9), "dark", "boot")
    d.leg(13, 7.5, -8, 8, 10, 90, 47, "team")
    d.box((-20, 20, 82, 96, -12.5, 12.5), "team", "pelvis", "hips")
    d.box((-22, 22, 94, 137, -13.5, 13.5), "pale", "torso", "field top")
    d.box((-22.5, 22.5, 93.5, 99, -14, 14), "leather", "torso", "belt")
    for k, x in enumerate((-18, -10, 6, 14)):
        d.box((x, x + 5, 96, 103, -15.5, -14), "leather", "torso", "pouch %d" % (k + 1))
    d.box((-1, 1, 100, 134, -14, -13.5), "light", "torso", "zip", **plate())
    d.box((-12, 12, 98, 134, 13.5, 21), "white", "torso", "med pack")
    d.box((-9, 9, 102, 108, 21, 22), "team", "torso", "pack strap", **plate())
    d.box((-2.5, 2.5, 113, 129, 20.5, 22), "team", "torso", "pack cross v", **plate())
    d.box((-8, 8, 118.5, 123.5, 21, 22.5), "team", "torso", "pack cross h")
    d.sym((7, 11, 106, 137, -14, -13.5), "leather", "torso", "pack strap front", **plate())
    d.sym((22, 32, 120, 137, -10, 10), "pale", "torso", "shoulder")
    d.box((-32.5, -21.5, 110, 120, -10.5, 10.5), "team", "torso", "armband")
    d.box((-33.5, -32.5, 112, 118, -2.5, 2.5), "white", "torso", "arm cross h", **plate())
    d.box((-33, -32, 111, 119, -1, 1), "white", "torso", "arm cross v", **plate())
    d.box((-12, 12, 136, 143, -10, 10), "pale", "torso", "collar")
    face(d)
    d.box((-10, 10, 162, 170, -10.5, 10.5), "team", "head", "cap")
    d.box((-7, 7, 162, 164, -15.5, -10.5), "team", "head", "cap bill")
    d.box((-1, 1, 164, 168.5, -11, -10), "white", "head", "cap cross v", **plate())
    d.box((-2.5, 2.5, 165, 167, -11.5, -10.5), "white", "head", "cap cross h", **plate())
    return d


def engineer():
    d = Design("ENGINEER", "the engineer: sage coveralls, a welding mask flipped up on the head, a tool harness with wrenches, team knee and elbow pads, a team toolbox on the back")
    d.foot((5, 21, 0, 12, -18, 9), "hat_dk", "work boot")
    d.sym((5.5, 20.5, 8, 11.5, -18.5, -12), "light", "foot_", "steel toe", **plate())
    d.leg(13, 8, -8.5, 8.5, 11, 90, 47, "sage")
    d.shin((8, 18, 38, 47, -9.5, -8.5), "team", "knee pad", **plate())
    d.box((-20, 20, 82, 96, -12.5, 12.5), "sage", "pelvis", "hips")
    d.box((-22.5, 22.5, 94, 137, -13.5, 13.5), "sage", "torso", "coveralls")
    d.box((-23, 23, 93.5, 100, -14, 14), "leather", "torso", "tool belt")
    d.sym((7, 11, 100, 137, -14, -13.5), "leather", "torso", "harness", **plate())
    d.box((-12, 12, 116, 120, -14.5, -14), "leather", "torso", "harness cross", **plate())
    d.box((13, 15, 102, 116, -14.5, -14), "light", "torso", "wrench", **plate())
    d.box((12, 16, 114, 118, -15, -14.5), "light", "torso", "wrench jaw", **plate())
    d.box((-16, -13, 100, 112, -14.5, -14), "gold", "torso", "screwdriver", **plate())
    d.box((-20, -14, 88, 98, -16, -14), "leather", "torso", "pouch")
    d.box((-15, 15, 102, 128, 13.5, 23), "team", "torso", "toolbox")
    d.box((-15.5, 15.5, 118, 120, 14, 23.5), "team_dk", "torso", "toolbox lid line")
    d.box((-4, 4, 128, 131, 17, 19), "dark", "torso", "toolbox handle")
    d.box((-6, 6, 110, 114, 23, 24), "light", "torso", "toolbox latch", **plate())
    d.sym((22.5, 33, 120, 137, -10, 10), "team", "torso", "shoulder")
    d.sym((23, 33.5, 113, 121, -10.5, 10.5), "team", "torso", "elbow pad")
    d.box((-12, 12, 136, 143.5, -10, 10), "team", "torso", "collar")
    face(d, y1=164)
    d.box((-7, 7, 150, 151, -10.5, -10), "hair", "head", "stubble", **plate())
    d.box((-10, 10, 162, 167, -10.5, 10.5), "team", "head", "head band")
    # the welding mask, hinged up: a slab standing on the crown
    d.box((-10, 10, 167, 175, -9, 3), "dark", "head", "mask")
    d.box((-6, 6, 169, 172, -9.5, -9), "glass", "head", "mask window", **plate())
    d.sym((10, 11, 162, 170, -1.5, 1.5), "light", "head", "hinge")
    return d


def marksman():
    d = Design("MARKSMAN", "the marksman: a deep hood and ghillie cloak of ragged green strips, a rangefinder over the right eye with a red lens, team trousers and gloves strap")
    d.foot((5.5, 20.5, 0, 11, -17, 9), "hat_dk", "boot")
    d.leg(13, 7, -7.5, 7.5, 10, 90, 47, "team")
    d.box((-19, 19, 82, 96, -12, 12), "team", "pelvis", "hips")
    d.box((-21.5, 21.5, 94, 136, -13, 13), "ghillie_dk", "torso", "body")
    d.box((-12, 12, 96, 112, -13.5, -13), "team", "torso", "vest", **plate())
    # the cloak: ragged strips round the shoulders and down the back
    d.box((-24, 24, 118, 143.5, -15, 15), "ghillie", "torso", "cloak")
    for k, (x0, x1, y0) in enumerate(((-24, -16, 106), (-16, -6, 100), (-6, 4, 108), (4, 14, 98), (14, 24, 104))):
        d.box((max(x0, -23.5), min(x1, 23.5), y0, 118.5, 13.5, 14.5), "ghillie" if k % 2 else "ghillie_dk", "torso", "back strip %d" % (k + 1))
    for k, (x0, x1, y0) in enumerate(((-24, -16, 110), (16, 24, 112))):
        d.box((max(x0, -23.5), min(x1, 23.5), y0, 118.5, -14.5, -13.5), "ghillie_dk", "torso", "front strip %d" % (k + 1))
    d.sym((24, 34, 114, 140, -11, 11), "ghillie", "torso", "cloak shoulder")
    d.sym((28, 32, 106, 114, -9, 9), "ghillie_dk", "torso", "shoulder strip")
    d.box((-24.5, 24.5, 126, 129, -15.5, 15.5), "moss", "torso", "cloak band")
    face(d, y0=146, y1=162)
    d.box((-7, 7, 148, 153, -10.5, -10), "ghillie_dk", "head", "face wrap", **plate())
    d.box((-11.5, 11.5, 144, 170, -6, 12), "ghillie", "head", "hood")
    d.box((-11, 11, 160, 172, -11.5, 12.5), "ghillie", "head", "hood top")
    d.sym((10, 11.5, 146, 160, -10.5, -6), "ghillie", "head", "hood side")
    d.box((-8, 8, 172, 174, -6, 8), "ghillie_dk", "head", "hood tuft")
    d.box((2, 9, 155, 161, -14, -9.5), "dark", "head", "rangefinder")
    d.box((3.5, 7.5, 156, 160, -14.5, -14), "redeye", "head", "range lens", **plate())
    return d


def demolition():
    d = Design("DEMOLITION", "the bomb tech: a bulky sand blast suit, a huge padded collar round a helmet with a flat clear visor, team padding, a belt of team charges with wires")
    d.foot((4.5, 23, 0, 13, -19, 10), "dark", "heavy boot")
    d.leg(14, 9.5, -11, 11, 12, 88, 46, "sand")
    d.shin((6, 22, 14, 44, -11.5, -11), "team", "shin pad", **plate())
    d.thigh((5.5, 22.5, 54, 80, -11.5, -11), "team", "thigh pad", **plate())
    d.box((-25, 25, 78, 96, -15, 15), "sand", "pelvis", "hips")
    d.box((-26, 26, 94, 134, -17, 17), "sand", "torso", "suit")
    d.box((-21, 21, 100, 132, -18.5, -17), "team", "torso", "chest pad")
    for k, y in enumerate((110, 121)):
        d.box((-21.5, 21.5, y, y + 1.5, -19, -18.5), "sand", "torso", "pad seam %d" % (k + 1), **plate())
    d.box((-26.5, 26.5, 92, 100, -17.5, 17.5), "leather", "torso", "charge belt")
    for k, x in enumerate((-22, -13, 6, 15)):
        d.box((x, x + 7, 90, 99, -20, -17.5), "team", "torso", "charge %d" % (k + 1))
        d.box((x + 2, x + 5, 99, 101, -19.5, -18), "glove", "torso", "wire %d" % (k + 1))
    d.box((-3, 3, 91, 99, -19, -17.5), "visor", "torso", "timer", **plate())
    d.box((-18, 18, 100, 130, 17, 20), "team", "torso", "back pad")
    d.sym((26, 37, 106, 136, -11, 11), "sand", "torso", "shoulder pad")
    d.sym((26.5, 37.5, 116, 120, -11.5, 11.5), "team", "torso", "shoulder band")
    # the padded collar: rings round the helmet's base, high at the back
    d.box((-21, 21, 132, 143.5, -15, 16), "sand", "torso", "collar")
    d.box((-18, 18, 134, 143, 14, 22), "sand", "torso", "collar back")
    d.box((-12, 12, 143, 166, -12, 12), "sand", "head", "helmet")
    d.box((-12.5, 12.5, 150, 160, 2, 11.5), "team", "head", "helmet pad")
    d.box((-11.5, 11.5, 150, 160, 12, 12.5), "team", "head", "helmet pad back")
    d.box((-9, 9, 148, 164, -13, -12), "glass", "head", "visor", **plate())
    d.box((-9.5, 9.5, 147.5, 148.5, -13.5, -12.5), "dark", "head", "visor rim low", **plate())
    d.box((-9.5, 9.5, 164, 165, -13.5, -12.5), "dark", "head", "visor rim top", **plate())
    d.box((-6, 6, 156, 158, -13.5, -13), "eye", "head", "eyes behind", **plate())
    d.box((-9, 9, 166, 169, -9, 9), "sand", "head", "helmet top")
    d.box((-3, 3, 169, 171, -7, -3), "lamp", "head", "helmet lamp")
    return d


def runner():
    d = Design("RUNNER", "the runner: a very lean sprinter in a team tank top and split shorts, a cap with a long bill, a headset with a mic arm, sweatbands, white racing shoes")
    d.foot((7, 19, 0, 5, -17, 6), "white", "racing shoe")
    d.sym((7.5, 18.5, 0, 1.5, -17.5, -17), "team", "foot_", "toe stripe", **plate())
    d.leg(12, 5, -5.5, 5.5, 5, 90, 47, "skin")
    d.thigh((6.5, 17.5, 74, 89.5, -6.5, 6.5), "team", "split shorts")
    d.shin((6.5, 17.5, 5.5, 10, -6, 6), "team", "ankle sock")
    d.box((-16, 16, 82, 96, -10, 10), "team", "pelvis", "hips")
    d.box((-16, 16, 95, 134, -10, 10), "team", "torso", "tank top")
    d.box((-16.5, 16.5, 112, 116, -10.5, 10.5), "white", "torso", "stripe")
    d.box((-6, 6, 120, 128, -11, -10.5), "white", "torso", "bib", **plate())
    d.box((-3, 3, 121, 127, -11.5, -11), "team", "torso", "bib mark", **plate())
    d.sym((16, 26, 122, 134, -7, 7), "skin", "torso", "shoulder")
    d.sym((16.5, 26.5, 116, 120, -7.5, 7.5), "team", "torso", "sweatband")
    d.box((-10, 10, 133, 140, -7, 7), "skin", "torso", "neck base")
    d.box((-4, 4, 140, 146, -4, 4), "skin", "head", "neck")
    d.box((-8.5, 8.5, 146, 164, -9, 9), "skin", "head", "face")
    d.sym((2.5, 5.5, 156, 157.5, -9.5, -9), "eye", "head", "eye", **plate())
    d.box((-9, 9, 162, 169, -9.5, 9.5), "team", "head", "cap")
    d.box((-5, 5, 162, 163.5, -16, -9.5), "team_dk", "head", "cap bill")
    d.box((-9.5, 9.5, 159, 162, -9.5, 9.5), "white", "head", "headband")
    d.box((8.5, 11, 152, 159, -3, 3), "dark", "head", "earpiece")
    d.box((9, 10, 150, 152, -10, -2), "dark", "head", "mic arm")
    d.box((4, 9, 150, 151.5, -10.5, -9.5), "dark", "head", "mic")
    d.box((-11, 11, 168.5, 170, -1, 1), "dark", "head", "headset band")
    return d


def agent():
    d = Design("AGENT", "the agent: a slim dark-team suit jacket with black lapels and black trousers, a white shirt and black tie, slicked hair, black shades, an earpiece cord, polished shoes")
    d.foot((6.5, 19.5, 0, 6, -16, 7), "hairblk", "polished shoe")
    d.leg(12.5, 6, -6.5, 6.5, 5, 90, 47, "hairblk")
    d.box((-17, 17, 82, 96, -10.5, 10.5), "hairblk", "pelvis", "hips")
    d.box((-19, 19, 86, 137, -11, 11), "team_dk", "torso", "jacket")
    d.box((-5, 5, 114, 137, -12, -11), "white", "torso", "shirt", **plate())
    d.box((-1.5, 1.5, 104, 135, -12.5, -12), "hairblk", "torso", "tie", **plate())
    d.sym((5, 8.5, 108, 137, -12, -11), "hairblk", "torso", "lapel", **plate())
    d.box((-1, 1, 88, 112, -11.5, -11), "hairblk", "torso", "button line", **plate())
    d.box((-16, -10, 128, 130, -11.5, -11), "white", "torso", "pocket square", **plate())
    d.sym((19, 28, 122, 137, -8, 8), "team_dk", "torso", "shoulder")
    d.box((-9, 9, 136, 141, -8, 8), "white", "torso", "collar")
    d.box((-4.5, 4.5, 140.5, 147, -4.5, 4.5), "skin", "head", "neck")
    d.box((-9, 9, 146, 165, -9.5, 9.5), "skin", "head", "face")
    d.box((-8, 8, 156, 159.5, -10, -9.5), "eye", "head", "shades", **plate())
    d.box((-3, 3, 151, 151.5, -10, -9.5), "skin_dk", "head", "mouth", **plate())
    d.box((-9.5, 9.5, 162, 169, -10, 10), "hairblk", "head", "slick hair")
    d.box((9, 10.5, 152, 157, -1, 2), "hairblk", "head", "earpiece")
    d.box((9.5, 10.5, 144, 152, 1, 2), "white", "head", "earpiece cord")
    return d


def flamer():
    d = Design("FLAMER", "the flamer: a sealed baggy hazmat suit, a gas mask with round goggles and twin filters, team boots and gloves straps, twin team fuel tanks and a hose on the back")
    d.foot((5, 21, 0, 13, -18, 9), "team", "rubber boot")
    d.leg(13, 8.5, -9, 9, 12, 90, 47, "hazmat")
    d.shin((4.5, 21.5, 12.5, 16, -9.5, 9.5), "team", "boot seal")
    d.box((-21, 21, 82, 96, -13, 13), "hazmat", "pelvis", "hips")
    d.box((-23.5, 23.5, 94, 138, -14.5, 14.5), "hazmat", "torso", "suit")
    d.box((-24, 24, 95, 100, -15, 15), "team", "torso", "belt")
    d.box((-1.5, 1.5, 100, 136, -15, -14.5), "dark", "torso", "seal strip", **plate())
    d.sym((14, 19, 104, 109, -15, -14.5), "dark", "torso", "warning patch", **plate())
    d.sym((14.5, 18.5, 104.5, 108.5, -15.5, -15), "hivis", "torso", "warning", **plate())
    # the tanks: two upright cylinders on a frame, a hose looping round to the right
    d.sym((2, 12, 98, 136, 15, 25), "team", "torso", "fuel tank")
    d.sym((3, 11, 136, 140, 16, 24), "team_dk", "torso", "tank cap")
    d.box((-13, 13, 112, 116, 14.5, 25.5), "dark", "torso", "tank strap")
    d.box((12, 24, 100, 103, 18, 21), "dark", "torso", "hose 1")
    d.box((24, 27, 88, 103, 10, 21), "dark", "torso", "hose 2")
    d.sym((23.5, 34, 120, 138, -11, 11), "hazmat", "torso", "shoulder")
    d.sym((24, 34.5, 112, 117, -11.5, 11.5), "team", "torso", "glove cuff")
    d.box((-14, 14, 136, 143.5, -12, 12), "team", "torso", "collar seal")
    d.box((-11.5, 11.5, 143, 170, -11.5, 11.5), "hazmat", "head", "hood")
    d.box((-8.5, 8.5, 152, 166, -12.5, -11.5), "dark", "head", "mask")
    d.sym((2, 7.5, 157, 163, -13, -12.5), "glass", "head", "goggle", **plate())
    d.box((-3, 3, 148, 154, -15, -12.5), "dark", "head", "snout")
    d.sym((3, 8, 146, 151, -15, -11), "mid", "head", "filter")
    d.sym((4, 7, 147, 150, -15.5, -15), "bore", "head", "filter mesh", **plate())
    d.box((-8, 8, 170, 172, -8, 8), "hazmat", "head", "hood top")
    return d


def pimp():
    d = Design("PIMP", "the 1970s flash: a long team coat with a white fur collar and cuffs, a wide-brim team hat with a gold band and a tall feather, big shades, a gold chain, platform shoes, a cane at the hip")
    d.foot((6, 20, 0, 4, -16, 8), "gold", "platform")
    d.foot((6.5, 19.5, 4, 11, -15.5, 7.5), "white", "shoe")
    d.leg(13, 7, -7.5, 7.5, 10, 90, 47, "white")
    d.shin((5.5, 20.5, 10.5, 18, -8, 8), "white", "flare")
    d.thigh((3, 23, 54, 89.5, -11, 11), "team", "coat tail")
    d.thigh((2.5, 23.5, 53.5, 58, -11.5, 11.5), "white", "coat hem fur")
    d.box((-20, 20, 82, 96, -12.5, 12.5), "team", "pelvis", "coat hips")
    d.box((-22, 22, 94, 136, -13.5, 13.5), "team", "torso", "coat")
    d.box((-7, 7, 104, 132, -14, -13.5), "plum", "torso", "shirt", **plate())
    d.box((-22.5, 22.5, 100, 104, -14, 14), "dark", "torso", "belt")
    d.box((-3, 3, 100.5, 103.5, -14.5, -14), "gold", "torso", "buckle", **plate())
    # the chain: a V of gold links with a medallion
    for k in range(3):
        d.sym((1 + k * 3, 4 + k * 3, 118 + k * 5, 123 + k * 5, -14.5, -14), "gold", "torso", "chain %d" % (k + 1), **plate())
    d.box((-3, 3, 112, 118, -14.5, -14), "gold", "torso", "medallion", **plate())
    d.box((-20, 20, 129, 143.5, -15.5, 15.5), "white", "torso", "fur collar")
    d.sym((22, 33, 118, 136, -10.5, 10.5), "team", "torso", "shoulder")
    d.sym((22.5, 33.5, 110, 118, -11, 11), "white", "torso", "fur cuff")
    d.sym((33.5, 34, 113, 115, -2, 2), "gold", "torso", "ring", **plate())
    d.box((-27, -24.5, 40, 98, -8, -5.5), "hairblk", "torso", "cane")
    d.box((-28, -23.5, 98, 103, -9, -4.5), "gold", "torso", "cane knob")
    face(d, y1=165, eyes=False)
    d.box((-9, 9, 155.5, 160, -11, -10), "hairblk", "head", "big shades")
    d.box((-9.5, 9.5, 159.5, 160.5, -11.5, -10.5), "gold", "head", "shades rim", **plate())
    d.box((-5, 5, 151, 152.5, -10.5, -10), "hair", "head", "moustache", **plate())
    brim(d, 164, "team", "hat brim")
    d.box((-9, 9, 166, 174, -9, 9), "team", "head", "hat crown")
    d.box((-9.5, 9.5, 166.5, 169, -9.5, 9.5), "gold", "head", "hat band")
    d.box((9.5, 12, 167, 179.5, -1.5, 3.5), "honey", "head", "feather")
    d.box((12, 13.5, 171, 179, -1, 3), "white", "head", "feather vane")
    return d


NAMES = ["MARINE", "MEDIC", "ENGINEER", "MARKSMAN", "DEMOLITION", "RUNNER", "AGENT", "FLAMER", "PIMP",
         "YETI", "SNOWMAN", "SCARECROW", "JESTER", "MIME", "BEE", "PUMPKIN", "WEREWOLF", "DINO", "PENGUIN", "BUNNY",
         "MUSHROOM", "GOLEM", "TVHEAD", "PLAGUEDOC", "GUARD", "CAVEMAN", "DANCER", "TOURIST", "SURFER", "ROCKSTAR",
         "LUMBERJACK", "CYBORG", "GOALIE"]
BUILDERS = {n: globals()[n.lower()] for n in NAMES}
