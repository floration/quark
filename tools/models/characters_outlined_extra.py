"""20 more player designs for the OUTLINED set (gen_players_outlined.py), drawn on the same rules as
characters_outlined.py: box model files, one bone a box, all 9 walking bones, inside the shared hitbox (the head
cylinder r 17.6 cm above 1.44 m, the body cylinder r 39.6 cm below), a 5 mm grid, at most 1000 triangles.

Same conventions: feet on the floor at the origin, facing -Z, +X its right; a box is (x0, x1, y0, y1, z0, z1) in cm.
Each design is told apart from the 11 and from each other by its silhouette first (a tall toque, a sashimono banner,
horns, a brass dome, a cranium, a mohawk, a cape and collar, a crest, eye domes, a pickaxe across the back) and its
palette second (a fixed colour the character needs, in gen_players_outlined.TONES with the 11's; the team slot on
about a quarter of what's seen).

gen_players_outlined.py writes them with the 11 (its NAMES: the 11, then these).
"""
from characters_outlined import Design, plate



# ---------------------------------------------------------------------------------------------------------------

def astronaut():
    d = Design("ASTRONAUT", "the astronaut: a puffy white suit, a big round helmet with a gold visor, a life-support pack, team shoulders, knees and chest box")
    d.foot((4, 22.5, 0, 4, -20, 11.5), "dark", "sole")
    d.foot((4.5, 22, 4, 15, -19, 11), "white", "moon boot")
    d.leg(13, 8, -9, 9, 13, 91, 48, "white")
    d.shin((7, 19, 37, 47, -9.5, -8.5), "team", "knee pad", **plate())
    d.shin((5, 21, 18, 22, -9, 9), "light", "boot ring")
    d.thigh((9, 17, 60, 74, -9.5, -9), "team", "thigh pocket", **plate())
    d.box((-21, 21, 82, 97, -13, 13), "light", "pelvis", "hips")
    d.box((-24, 24, 95, 138, -15, 15), "white", "torso", "suit")
    d.box((-24.5, 24.5, 96, 103, -15.5, 15.5), "team", "torso", "belt")
    d.box((-11, 11, 108, 126, -18, -15), "light", "torso", "chest box")
    d.box((-9, -1, 117, 124, -18.5, -18), "team", "torso", "dial", **plate())
    d.box((2, 8, 117, 121, -18.5, -18), "visor", "torso", "status light", **plate())
    d.box((-18, 18, 100, 139, 15, 26), "light", "torso", "life pack")
    d.box((-13, 13, 106, 132, 26, 27), "team", "torso", "pack panel", **plate())
    d.sym((20, 37, 123, 140, -11, 11), "team", "torso", "shoulder")
    d.box((-13, 13, 136, 143.5, -13, 13), "light", "torso", "neck ring")
    d.box((-12, 12, 144, 172, -12, 12), "white", "head", "helmet")
    d.box((-9, 9, 151, 166, -12.5, -12), "amber", "head", "visor", **plate())
    d.box((-6, 6, 172, 174.5, -8, 8), "light", "head", "helmet top")
    d.sym((12, 13.5, 154, 164, -4, 4), "team", "head", "ear lamp")
    return d


def samurai():
    d = Design("SAMURAI", "the samurai: red lacquer armour with team lacing, big shoulder plates and skirt, a horned gold crest and a team banner on the back")
    d.foot((6, 20, 0, 9, -16, 8), "dark", "sandal")
    d.leg(13, 7.5, -8, 8, 8, 91, 48, "mid", "dark")
    d.shin((8.5, 17.5, 14, 44, -9, -8), "lacquer", "shin guard", **plate())
    d.shin((9, 17, 28, 31, -9.5, -9), "team", "shin lace", **plate())
    d.thigh((4, 22, 62, 90, -11, 11), "lacquer", "skirt")
    d.thigh((3.5, 22.5, 61.5, 66.5, -11.5, 11.5), "team", "skirt lace")
    d.box((-20, 20, 82, 97, -12.5, 12.5), "dark", "pelvis", "hips")
    d.box((-22, 22, 95, 137, -14, 14), "lacquer", "torso", "cuirass")
    for k, y in enumerate((106, 116, 126)):
        d.box((-20, 20, y, y + 2.5, -14.5, -14), "team", "torso", "lace %d" % (k + 1), **plate())
    d.box((-22.5, 22.5, 93, 99, -14.5, 14.5), "team", "torso", "obi")
    d.sym((22, 37, 108, 140, -11, 11), "lacquer", "torso", "shoulder plate")
    d.sym((22.5, 37.5, 112, 116, -11.5, 11.5), "team", "torso", "shoulder lace 1")
    d.sym((22.5, 37.5, 123, 127, -11.5, 11.5), "team", "torso", "shoulder lace 2")
    d.box((-13, 13, 136, 143.5, -11, 11), "dark", "torso", "gorget")
    # the banner (sashimono): a pole and a team flag up the back, inside the head cylinder above the neck line
    d.box((-1, 1, 108, 179.5, 15, 17), "dark", "torso", "banner pole")
    d.box((-8, 8, 128, 176, 14, 15), "team", "torso", "banner")
    d.box((-9, 9, 146, 163, -9.5, 9.5), "skin", "head", "face")
    d.box((-9.5, 9.5, 146, 154, -10.5, -9.5), "dark", "head", "mask")
    d.box((-6, 6, 156.5, 158.5, -10, -9.5), "eye", "head", "eyes", **plate())
    d.box((-11, 11, 162, 172, -11, 11), "dark", "head", "helmet bowl")
    d.box((-12.5, 12.5, 156, 162, -4, 12), "dark", "head", "neck guard 1")
    d.box((-13.5, 13.5, 151, 156, -2, 10.5), "dark", "head", "neck guard 2")
    d.sym((11, 14, 160, 168, -10, -6), "lacquer", "head", "helmet flap")
    d.box((-2, 2, 167, 172, -12, -11), "gold", "head", "crest badge")
    d.sym((2, 5, 169.5, 172.5, -12, -11), "gold", "head", "crest 1")
    d.sym((5, 8.5, 172.5, 176, -12, -11), "gold", "head", "crest 2")
    d.sym((8.5, 11.5, 176, 179.5, -12, -11), "gold", "head", "crest 3")
    return d


def viking():
    d = Design("VIKING", "the viking: a horned steel helmet, a big straw beard, a fur mantle, a team tunic and a round team shield on the back")
    d.foot((5.5, 20.5, 0, 12, -17, 9), "fur", "fur boot")
    d.leg(13, 7.5, -8, 8, 11, 91, 48, "mid")
    for k, y in enumerate((17, 28, 39)):
        d.shin((7.5, 18.5, y, y + 3, -8, -7.5), "leather", "leg wrap %d" % (k + 1), **plate())
    d.thigh((5, 21, 68, 90.5, -9, 9), "team", "tunic skirt")
    d.box((-20, 20, 82, 97, -12.5, 12.5), "mid", "pelvis", "hips")
    d.box((-22, 22, 95, 137, -13.5, 13.5), "team", "torso", "tunic")
    d.box((-22.5, 22.5, 94.5, 101, -14, 14), "leather", "torso", "belt")
    d.box((-4, 4, 95.5, 100.5, -14.5, -14), "gold", "torso", "buckle", **plate())
    d.box((-24, 24, 128, 143, -14.5, 14.5), "fur", "torso", "fur mantle")
    d.sym((24, 36, 124, 141, -11.5, 11.5), "fur", "torso", "fur shoulder")
    d.box((-15, 15, 103, 127, 13.5, 16.5), "team", "torso", "shield")
    d.box((-11, 11, 99, 131, 14, 16), "team", "torso", "shield round")
    d.box((-4, 4, 111, 119, 16.5, 19), "light", "torso", "shield boss")
    d.box((-7, 7, 124, 140, -17, -14.5), "straw", "torso", "beard braid")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 166, -10, 10), "skin", "head", "face")
    d.box((-10, 10, 140, 155, -13, -10), "straw", "head", "beard")
    d.box((-6, 6, 157.5, 159.5, -10.5, -10), "eye", "head", "eyes", **plate())
    d.box((-10.5, 10.5, 163, 172, -10.5, 10.5), "light", "head", "helmet")
    d.box((-11, 11, 162.5, 166.5, -11, 11), "mid", "head", "helmet band")
    d.box((-1.5, 1.5, 155, 162.5, -11.5, -10.5), "mid", "head", "nose guard")
    d.box((-1.5, 1.5, 172, 174, -8, 8), "mid", "head", "helmet ridge")
    d.sym((10.5, 14, 165, 168, -3, 3), "ivory", "head", "horn 1")
    d.sym((13, 16, 168, 172, -3, 3), "ivory", "head", "horn 2")
    d.sym((13.5, 16.5, 172, 177, -2.5, 2.5), "ivory", "head", "horn 3")
    d.sym((12.5, 15, 177, 179.5, -2, 2), "ivory", "head", "horn tip")
    return d


def chef():
    d = Design("CHEF", "the chef: a tall white toque, a white double-breasted jacket, a team apron and neckerchief, checked trousers and a curly moustache")
    d.foot((6, 20, 0, 9, -16, 8), "dark", "clog")
    d.leg(13, 7.5, -8, 8, 8, 91, 48, "mid_dk")
    for k, (x, y) in enumerate(((8, 58), (16, 58), (12, 66), (8, 74), (16, 74), (12, 82))):
        d.thigh((x - 1, x + 3, y, y + 4, -8.5, -8), "light", "check %d" % (k + 1), **plate())
    for k, (x, y) in enumerate(((8, 20), (14, 28), (8, 36))):
        d.shin((x - 1, x + 3, y, y + 4, -8, -7.5), "light", "shin check %d" % (k + 1), **plate())
    d.box((-19, 19, 82, 96, -12, 12), "mid_dk", "pelvis", "hips")
    d.box((-22, 22, 93, 137, -13.5, 13.5), "white", "torso", "jacket")
    d.box((-15, 15, 70, 108, -15, -14), "team", "torso", "apron", **plate())
    d.box((-22.5, 22.5, 106, 110, -14, 14), "team", "torso", "apron string")
    for k, y in enumerate((114, 122, 130)):
        d.sym((4, 7, y, y + 3, -14.5, -13.5), "dark", "torso", "button %d" % (k + 1), **plate())
    d.box((-12, 12, 136, 142, -11, 11), "team", "torso", "neckerchief")
    d.box((-3, 3, 129, 137, -14.5, -13.5), "team", "torso", "kerchief knot", **plate())
    d.sym((22, 34, 118, 138, -10, 10), "white", "torso", "sleeve")
    d.sym((22.5, 34.5, 115, 119, -10.5, 10.5), "team", "torso", "cuff")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 165, -10, 10), "skin", "head", "face")
    d.box((-6, 6, 151.5, 154, -10.5, -10), "hair", "head", "moustache", **plate())
    d.sym((6, 8.5, 152.5, 156, -10.5, -10), "hair", "head", "moustache curl", **plate())
    d.box((-6, 6, 157.5, 159.5, -10.5, -10), "eye", "head", "eyes", **plate())
    d.box((-10, 10, 163, 167, -10.5, 10.5), "white", "head", "toque band")
    d.box((-11.5, 11.5, 167, 179, -11.5, 11.5), "white", "head", "toque")
    d.sym((3.5, 4.5, 168, 178, -12, -11.5), "light", "head", "pleat 1", **plate())
    d.sym((8, 9, 168, 178, -12, -11.5), "light", "head", "pleat 2", **plate())
    return d


def firefighter():
    d = Design("FIREFIGHTER", "the firefighter: a khaki turnout coat and trousers with yellow reflective bands, a team helmet with a long back brim, an air tank")
    d.foot((5.5, 20.5, 0, 14, -17, 9), "dark", "rubber boot")
    d.leg(13, 8, -8.5, 8.5, 12, 91, 48, "khaki")
    d.shin((5, 21, 11.5, 20, -8.5, 8.5), "dark", "boot top")
    d.shin((5, 21, 24, 28, -8.5, 8.5), "hivis", "leg band")
    d.box((-20, 20, 82, 97, -12.5, 12.5), "khaki", "pelvis", "hips")
    d.box((-23.5, 23.5, 86, 139, -15, 15), "khaki", "torso", "coat")
    d.box((-24, 24, 92, 96, -15.5, 15.5), "hivis", "torso", "coat band 1")
    d.box((-24, 24, 110, 114, -15.5, 15.5), "hivis", "torso", "coat band 2")
    d.box((-1.5, 1.5, 86, 136, -16, -15), "dark", "torso", "closure", **plate())
    d.box((-9, 9, 100, 134, 15, 25), "light", "torso", "air tank")
    d.box((-9.5, 9.5, 120, 125, 14.5, 25.5), "team", "torso", "tank band")
    d.box((-3, 3, 134, 138, 18, 22), "dark", "torso", "valve")
    d.sym((22, 35, 124, 140, -11, 11), "team", "torso", "shoulder")
    d.sym((22.5, 35.5, 126, 130, -11.5, 11.5), "hivis", "torso", "shoulder band")
    d.box((-14, 14, 136, 143.5, -12, 12), "team", "torso", "collar")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 165, -10, 10), "skin", "head", "face")
    d.box((-6, 6, 156.5, 158.5, -10.5, -10), "eye", "head", "eyes", **plate())
    d.box((-10.5, 10.5, 163, 174, -11, 11), "team", "head", "helmet")
    d.box((-12, 12, 162, 164.5, -12, 12), "team_dk", "head", "brim")
    d.box((-8, 8, 159, 164, 11, 15), "team_dk", "head", "back brim")
    d.box((-4, 4, 166, 173, -11.5, -11), "gold", "head", "badge", **plate())
    d.box((-1.5, 1.5, 174, 176, -9, 9), "team_dk", "head", "comb")
    return d


def diver():
    d = Design("DIVER", "the deep-sea diver: a big brass dome helmet with portholes, a canvas suit, lead boots and team weights front and back")
    d.foot((4.5, 21.5, 0, 13, -19, 10), "dark", "lead boot")
    d.foot((5, 21, 13, 15, -18.5, -2), "brass_dk", "toe cap")
    d.leg(13, 8, -8.5, 8.5, 12, 91, 48, "canvas")
    d.shin((5, 21, 14, 18, -8.5, 8.5), "leather", "boot strap")
    d.box((-21, 21, 82, 97, -13, 13), "canvas", "pelvis", "hips")
    d.box((-23, 23, 95, 136, -14, 14), "canvas", "torso", "suit")
    d.box((-23.5, 23.5, 94.5, 100, -14.5, 14.5), "leather", "torso", "belt")
    d.box((-13, 13, 106, 124, -16, -14), "team", "torso", "chest weight")
    d.box((-13, 13, 106, 124, 14, 16), "team", "torso", "back weight")
    d.box((-19, 19, 130, 143.5, -14.5, 14.5), "brass", "torso", "corselet")
    d.sym((19, 33, 120, 136.5, -11, 11), "team", "torso", "shoulder")
    d.box((-12, 12, 145, 172, -11.5, 11.5), "brass", "head", "helmet")
    d.box((-9, 9, 172, 177, -9, 9), "brass", "head", "dome")
    d.box((-2, 2, 177, 179.5, -2, 2), "brass_dk", "head", "valve")
    d.box((-7.5, 7.5, 151, 166, -12.5, -11.5), "brass_dk", "head", "port frame", **plate())
    d.box((-5.5, 5.5, 153, 164, -13, -12.5), "glass", "head", "port glass", **plate())
    d.box((-0.5, 0.5, 153, 158, -13.5, -13), "brass_dk", "head", "grille", **plate())
    d.box((-0.5, 0.5, 159, 164, -13.5, -13), "brass_dk", "head", "grille top", **plate())
    d.box((-5.5, 5.5, 158, 159, -13.5, -13), "brass_dk", "head", "grille bar", **plate())
    d.sym((12, 12.5, 154, 162, -4, 4), "brass_dk", "head", "side port", **plate())
    d.sym((12.5, 13, 155, 161, -3, 3), "glass", "head", "side glass", **plate())
    d.box((-3, 3, 162, 167, 11.5, 14), "brass_dk", "head", "air valve")
    return d


def alien():
    d = Design("ALIEN", "the alien: a teal skin, a huge cranium with big black slanted eyes, a long thin neck, a skinny silver suit with team yoke and belt")
    d.foot((7, 18, 0, 6, -17, 6), "alien", "foot")
    d.leg(12, 5, -5.5, 5.5, 5, 90, 47, "light", "alien")
    d.box((-15, 15, 82, 96, -10, 10), "light", "pelvis", "hips")
    d.box((-17, 17, 95, 134, -11, 11), "light", "torso", "suit")
    d.box((-17.5, 17.5, 120, 134.5, -11.5, 11.5), "team", "torso", "yoke")
    d.box((-15.5, 15.5, 92, 97, -10.5, 10.5), "team", "torso", "belt")
    d.box((-3, 3, 125, 130, -12, -11.5), "visor", "torso", "chest gem", **plate())
    d.sym((17, 30, 124, 135, -8, 8), "team", "torso", "shoulder")
    d.box((-4, 4, 134, 141, -4, 4), "alien", "torso", "neck")
    d.box((-3, 3, 140.5, 151, -3, 3), "alien", "head", "upper neck")
    d.box((-8.5, 8.5, 149, 160, -9, 8), "alien", "head", "jaw")
    d.box((-12, 12, 160, 177, -12, 11), "alien", "head", "cranium")
    d.box((-9, 9, 177, 179.5, -9, 8), "alien", "head", "crown")
    d.sym((2.5, 8, 161, 166, -12.5, -12), "eye", "head", "eye", **plate())
    d.sym((5, 10.5, 166, 169.5, -12.5, -12), "eye", "head", "eye corner", **plate())
    d.box((-2.5, 2.5, 152.5, 153.5, -9.5, -9), "eye", "head", "mouth", **plate())
    return d


def mummy():
    d = Design("MUMMY", "the mummy: cream bandages wrapped head to toe with darker and team bands, loose strips, a hunched head with one glowing eye")
    d.foot((6, 20, 0, 9, -16, 8), "bandage", "wrapped foot")
    d.leg(13, 7, -7.5, 7.5, 8, 90, 47, "bandage")
    for k, (y, t) in enumerate(((15, "bandage_dk"), (27, "team"), (38, "bandage_dk"))):
        d.shin((6, 20, y, y + (5 if t == "team" else 3), -7.5, 7.5), t, "shin wrap %d" % (k + 1))
    for k, (y, t) in enumerate(((56, "bandage_dk"), (69, "team"), (81, "bandage_dk"))):
        d.thigh((5.5, 20.5, y, y + (5 if t == "team" else 3), -8, 8), t, "thigh wrap %d" % (k + 1))
    d.box((-18, 18, 82, 96, -11.5, 11.5), "bandage_dk", "pelvis", "hips")
    d.box((-20, 20, 94, 137, -12.5, 12.5), "bandage", "torso", "body")
    for k, (y, t) in enumerate(((100, "team"), (112, "bandage_dk"), (124, "team"))):
        d.box((-20.5, 20.5, y, y + (6 if t == "team" else 3.5), -13, 13), t, "torso", "body wrap %d" % (k + 1))
    d.box((-16, -12, 78, 98, -13.5, -12.5), "bandage", "torso", "loose strip", **plate())
    d.box((8, 12, 84, 104, 12.5, 13.5), "bandage", "torso", "loose strip back", **plate())
    d.box((-4, 4, 115.5, 121.5, -14, -13), "team", "torso", "amulet", **plate())
    d.sym((20, 32, 120, 139, -9, 9), "bandage", "torso", "shoulder")
    d.sym((20.5, 32.5, 128, 131, -9.5, 9.5), "team", "torso", "shoulder wrap")
    d.box((-14, 14, 136, 143.5, -11, 11), "bandage_dk", "torso", "neck wrap")
    # the head hunched forward
    d.box((-10, 10, 143, 166, -13, 8), "bandage", "head", "head")
    d.box((-10.5, 10.5, 150, 153, -13.5, 8.5), "bandage_dk", "head", "head wrap 1")
    d.box((-10.5, 10.5, 160, 162.5, -13.5, 8.5), "bandage_dk", "head", "head wrap 2")
    d.box((2, 6, 155, 158, -13.5, -13), "zeye", "head", "eye", **plate())
    d.box((-6, -2, 156, 157.5, -13.5, -13), "eye", "head", "eye slit", **plate())
    return d


def skeleton():
    d = Design("SKELETON", "the skeleton: thin bone legs, a rib cage over a dark core, a skull with red glowing eyes, team shorts, scarf and pauldrons")
    d.foot((7, 19, 0, 6, -15, 6), "bone", "foot bones")
    d.leg(13, 3.5, -3.5, 3.5, 5, 90, 47, "bone")
    d.shin((8.5, 17.5, 43, 49, -4.5, 4.5), "bone", "knee cap")
    d.thigh((7, 19, 66, 89.5, -5.5, 5.5), "team", "shorts")
    d.box((-15, 15, 86, 96, -8, 8), "bone", "pelvis", "pelvis bone")
    d.box((-17, 17, 80, 92, -9, 9), "team", "pelvis", "shorts top")
    d.box((-3, 3, 95, 108, -2, 4), "bone", "torso", "spine")
    d.box((-15, 15, 106, 134, -9, 7), "dark", "torso", "core")
    for k, y in enumerate((108, 114, 120, 126, 132)):
        w = (13, 16, 17, 17, 17)[k]
        d.box((-w, w, y, y + 3, -10, 8), "bone", "torso", "rib %d" % (k + 1))
    d.box((-2, 2, 110, 134, -10.5, -10), "bone", "torso", "sternum", **plate())
    d.box((-2, 2, 106, 134, 7, 9), "bone", "torso", "back spine")
    d.box((-24, 24, 134, 138, -5, 5), "bone", "torso", "collarbone")
    d.sym((20, 32, 126, 140, -8, 8), "team", "torso", "pauldron")
    d.box((-10, 10, 136, 143, -8, 8), "team", "torso", "scarf")
    d.box((-5, 5, 122, 136, -9.5, -8), "team", "torso", "scarf end")
    d.box((-2.5, 2.5, 140, 148, -2.5, 2.5), "bone", "head", "neck bone")
    d.box((-9.5, 9.5, 150, 170, -10.5, 9.5), "bone", "head", "skull")
    d.box((-7, 7, 146, 152, -10, 6), "bone", "head", "jaw")
    d.sym((2, 7, 157, 162, -11, -10.5), "bore", "head", "eye socket", **plate())
    d.sym((3.5, 5.5, 158.5, 160.5, -11.5, -11), "redeye", "head", "eye glow", **plate())
    d.box((-1.5, 1.5, 154, 156.5, -11, -10.5), "bore", "head", "nose", **plate())
    d.box((-6, 6, 148, 149, -10.5, -10), "bore", "head", "teeth", **plate())
    return d


def clown():
    d = Design("CLOWN", "the clown: a white face, a red nose, pink side tufts and a tiny hat, a big white ruffle, a team suit with pompoms, yellow trousers, long red shoes")
    d.foot((5, 21, 0, 8, -24, 8), "glove", "big shoe")
    d.leg(13, 8.5, -9, 9, 7, 91, 48, "canary")
    d.shin((4, 22, 6.5, 12, -9.5, 9.5), "white", "ankle cuff")
    d.thigh((9, 14, 60, 65, -9.5, -9), "team", "dot 1", **plate())
    d.thigh((12, 17, 75, 80, -9.5, -9), "team", "dot 2", **plate())
    d.shin((8, 13, 30, 35, -9, -8.5), "team", "dot 3", **plate())
    d.box((-21, 21, 82, 97, -13, 13), "canary", "pelvis", "hips")
    d.box((-23, 23, 95, 138, -14.5, 14.5), "team", "torso", "suit")
    d.box((-3, 3, 124, 130, -16, -14.5), "white", "torso", "pompom 1")
    d.box((-3, 3, 110, 116, -16, -14.5), "white", "torso", "pompom 2")
    d.sym((7, 13, 96, 137, -15, -14.5), "canary", "torso", "suspender", **plate())
    d.box((-19, 19, 135, 141, -17, 17), "white", "torso", "ruffle")
    d.box((-15, 15, 141, 143.5, -13, 13), "glove", "torso", "ruffle top")
    d.sym((23, 35, 124, 139, -11, 11), "team", "torso", "puff shoulder")
    d.box((-9.5, 9.5, 143, 166, -10, 10), "white", "head", "face")
    d.box((-2.5, 2.5, 153, 158, -13, -10), "glove", "head", "nose")
    d.box((-6, 6, 148, 150.5, -10.5, -10), "glove", "head", "mouth", **plate())
    d.sym((6, 7.5, 150.5, 152.5, -10.5, -10), "glove", "head", "smile", **plate())
    d.sym((2.5, 5.5, 159, 161, -10.5, -10), "eye", "head", "eye", **plate())
    d.sym((2.5, 5.5, 162, 163, -10.5, -10), "team", "head", "brow", **plate())
    d.sym((9.5, 16, 154, 166, -6, 6), "pink", "head", "hair tuft")
    d.box((-5, 5, 166, 168, -5, 5), "dark", "head", "hat brim")
    d.box((-3.5, 3.5, 168, 176, -3.5, 3.5), "dark", "head", "hat")
    d.box((-4, 4, 168.5, 170.5, -4, 4), "team", "head", "hat band")
    return d


def gladiator():
    d = Design("GLADIATOR", "the gladiator: a brass helmet with a face grille and a tall team crest, bare skin, a leather strap, a brass left shoulder guard, a team skirt and greaves")
    d.foot((6, 20, 0, 4, -16, 8), "leather", "sandal")
    d.leg(13, 7, -7.5, 7.5, 4, 90, 47, "skin")
    d.shin((6, 20, 7, 10, -7.5, 7.5), "leather", "sandal strap")
    d.shin((7.5, 18.5, 14, 46, -8, -7), "brass", "greave", **plate())
    d.thigh((5, 21, 72, 89.5, -9, 9), "team", "skirt")
    d.thigh((12, 14, 72.5, 89, -9.5, -9), "leather", "skirt strip", **plate())
    d.box((-20, 20, 80, 97, -13, 13), "team", "pelvis", "skirt top")
    d.box((-21, 21, 92, 100, -13.5, 13.5), "leather", "torso", "belt")
    d.sym((8, 11, 94, 98, -14, -13.5), "brass", "torso", "stud", **plate())
    d.box((-20.5, 20.5, 98, 137, -12, 12), "skin", "torso", "chest")
    for i in range(5):
        x0 = -17 + i * 7
        d.box((x0, x0 + 7, 101 + i * 7, 108 + i * 7, -13, -12), "leather", "torso", "strap %d" % (i + 1), **plate())
    d.box((-38, -20.5, 118, 142, -11, 11), "brass", "torso", "shoulder guard")
    d.box((-37.5, -21, 112, 118, -10.5, 10.5), "brass_dk", "torso", "shoulder guard lame")
    d.box((20.5, 31, 118, 138, -9, 9), "skin", "torso", "right shoulder")
    d.box((21, 31.5, 112, 118, -9.5, 9.5), "leather", "torso", "arm wrap")
    d.box((-13, 13, 136, 143.5, -10, 10), "skin", "torso", "shoulders")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-11, 11, 146, 170, -11, 11), "brass", "head", "helmet")
    d.box((-14, 14, 158, 160.5, -9, 9), "brass_dk", "head", "brim")
    d.box((-7, 7, 149, 162, -11.5, -11), "bore", "head", "grille", **plate())
    for k, x in enumerate((-4.5, -0.5, 3.5)):
        d.box((x, x + 1, 149, 162, -12, -11.5), "brass", "head", "grille bar %d" % (k + 1), **plate())
    d.box((-7, 7, 155, 156, -12.5, -12), "brass", "head", "grille cross", **plate())
    d.box((-2, 2, 170, 175, -13, 13), "team", "head", "crest")
    d.box((-2.5, 2.5, 175, 179.5, -15, 15), "team", "head", "crest top")
    return d


def ghost():
    d = Design("GHOST", "the ghost: a white sheet from head to hem with team bedsheet stripes, black eye holes and an O mouth, tattered hem that swings as it walks")
    d.foot((7, 19, 0, 6, -13, 6), "white", "toe")
    d.leg(13, 6, -6, 6, 5, 85, 45, "white")
    # the sheet to the floor: its upper half swings with the thighs, the lower with the shins, a ragged edge
    d.thigh((2, 24, 44, 84.5, -13, 13), "white", "sheet")
    d.thigh((1.5, 24.5, 58, 64, -13.5, 13.5), "team", "sheet stripe")
    d.shin((2.5, 23.5, 10, 44, -12.5, 12.5), "white", "sheet hem")
    d.shin((2, 24, 22, 28, -13, 13), "team", "hem stripe")
    d.shin((2.5, 10, 6, 10, -12.5, 12.5), "white", "tatter 1")
    d.shin((16, 23.5, 7, 10, -12.5, 12.5), "white", "tatter 2")
    d.box((-22, 22, 80, 96, -14, 14), "white", "pelvis", "sheet hips")
    d.box((-24, 24, 94, 136, -15, 15), "white", "torso", "sheet body")
    d.box((-24.5, 24.5, 104, 110, -15.5, 15.5), "team", "torso", "stripe 1")
    d.box((-24.5, 24.5, 118, 124, -15.5, 15.5), "team", "torso", "stripe 2")
    d.box((-21, 21, 136, 143.5, -13, 13), "white", "torso", "sheet shoulders")
    d.sym((24, 33, 118.5, 134, -11, 11), "white", "torso", "droop")
    d.box((-12, 12, 143, 170, -12, 12), "white", "head", "sheet head")
    d.box((-9, 9, 170, 176, -9, 9), "white", "head", "sheet top")
    d.sym((2.5, 7, 154, 161, -12.5, -12), "eye", "head", "eye hole", **plate())
    d.box((-2.5, 2.5, 147, 151, -12.5, -12), "eye", "head", "mouth", **plate())
    return d


def cat():
    d = Design("CAT", "the cat mascot: a black cat head with pointed ears, a white muzzle and glowing eyes, a team jersey and shorts, a red collar, a curled tail")
    d.foot((6, 20, 0, 9, -17, -11), "white", "paw toes")
    d.foot((6, 20, 0, 9, -11, 8), "dark", "paw")
    d.leg(13, 7.5, -8, 8, 8, 91, 48, "dark")
    d.thigh((5, 21, 72, 90.5, -8.5, 8.5), "team", "shorts")
    d.box((-20, 20, 82, 97, -13, 13), "team", "pelvis", "shorts top")
    d.box((-2, 2, 88, 93, 13, 26), "dark", "pelvis", "tail 1")
    d.box((-2, 2, 93, 118, 22, 27), "dark", "pelvis", "tail 2")
    d.box((-2, 2, 118, 124, 18, 26.5), "white", "pelvis", "tail tip")
    d.box((-23, 23, 95, 138, -14, 14), "team", "torso", "jersey")
    d.box((-10, 10, 112, 136, -15, -14), "white", "torso", "bib", **plate())
    d.sym((23, 34, 124, 139, -10, 10), "dark", "torso", "shoulder")
    d.box((-13, 13, 136, 143.5, -11, 11), "glove", "torso", "collar")
    d.box((-2, 2, 132, 136, -12, -11), "gold", "torso", "bell", **plate())
    d.box((-12, 12, 144, 168, -11.5, 11.5), "dark", "head", "head")
    d.box((-6.5, 6.5, 145, 154, -13, -11.5), "white", "head", "muzzle")
    d.box((-1.5, 1.5, 152, 154, -13.5, -13), "pink", "head", "nose", **plate())
    d.sym((3, 8, 156, 161, -12, -11.5), "zeye", "head", "eye", **plate())
    d.sym((5, 6, 156.5, 160.5, -12.5, -12), "eye", "head", "pupil", **plate())
    d.sym((12, 15.5, 150, 151, -7, -6), "white", "head", "whisker")
    d.sym((5, 12, 168, 172, -5, 3), "dark", "head", "ear 1")
    d.sym((7, 11, 172, 176, -4, 2), "dark", "head", "ear 2")
    d.sym((8, 10, 176, 178.5, -3, 1), "dark", "head", "ear tip")
    d.sym((6.5, 10.5, 168.5, 171.5, -5.5, -5), "pink", "head", "inner ear", **plate())
    return d


def frog():
    d = Design("FROG", "the frog mascot: lime green with a yellow belly, a wide flat head with two eye domes on top, a wide mouth, team shorts and neck scarf, webbed feet")
    d.foot((5, 23, 0, 5, -22, 6), "lime", "webbed foot")
    d.leg(13, 8, -8.5, 8.5, 4, 90, 46, "lime")
    d.thigh((4.5, 21.5, 68, 89.5, -9, 9), "team", "shorts")
    d.box((-21, 21, 80, 96, -13.5, 13.5), "team", "pelvis", "shorts top")
    d.box((-23, 23, 94, 136, -14.5, 14.5), "lime", "torso", "body")
    d.box((-16, 16, 96, 128, -16.5, -14.5), "canary", "torso", "belly")
    d.box((-16, 16, 134, 141, -13, 13), "team", "torso", "scarf")
    d.box((4, 10, 128.5, 134, -15, -14.5), "team", "torso", "scarf end", **plate())
    d.sym((23, 33, 120, 136, -10, 10), "lime", "torso", "shoulder")
    d.box((-12, 12, 141, 160, -12, 11), "lime", "head", "head")
    d.box((-10, 10, 141, 148, -12.5, -12), "canary", "head", "chin", **plate())
    d.box((-11, 11, 148, 149.5, -12.5, -12), "bore", "head", "mouth", **plate())
    d.sym((3, 11, 160, 169, -10, -1), "lime", "head", "eye dome")
    d.sym((4, 10, 161, 168, -10.5, -10), "white", "head", "eyeball", **plate())
    d.sym((5.5, 8.5, 162.5, 166.5, -11, -10.5), "eye", "head", "pupil", **plate())
    return d


def punk():
    d = Design("PUNK", "the punk: a tall team mohawk, dark shades, a black leather jacket with silver studs over a team shirt, ripped grey jeans, tall boots")
    d.foot((5.5, 20.5, 0, 16, -17, 9), "dark", "tall boot")
    d.sym((10, 16, 5, 15, -17.5, -17), "light", "foot_", "laces", **plate())
    d.leg(13, 7, -7.5, 7.5, 14, 90, 47, "team")
    # tartan trousers: dark bands round the team cloth and a stripe down the front
    for k, y in enumerate((60, 76)):
        d.thigh((5.5, 20.5, y, y + 2, -8, 8), "dark", "tartan band %d" % (k + 1))
    d.shin((6, 20, 22, 24, -7.5, 7.5), "dark", "tartan band 3")
    d.thigh((15, 17, 46, 90, -8.5, -8), "dark", "tartan stripe", **plate())
    d.thigh((8, 12, 66, 70, -8, -7.5), "skin", "rip", **plate())
    d.shin((14, 18, 28, 31, -7.5, -7), "skin", "knee rip", **plate())
    d.box((-19, 19, 82, 96, -12, 12), "dark", "pelvis", "belt")
    d.box((-4, 4, 85, 92, -12.5, -12), "light", "pelvis", "buckle", **plate())
    d.box((-22.5, 22.5, 93, 137, -13.5, 14), "dark", "torso", "jacket")
    d.box((-7, 7, 94, 134, -14, -13.5), "team", "torso", "shirt", **plate())
    d.box((-12, 12, 108, 130, 14, 14.5), "team", "torso", "back patch", **plate())
    d.sym((22.5, 34, 123, 139, -10.5, 10.5), "dark", "torso", "shoulder")
    for k, (x, z) in enumerate(((24, -7), (29, -7), (24, 5), (29, 5))):
        d.sym((x, x + 2, 139, 141, z, z + 2), "light", "torso", "stud %d" % (k + 1))
    d.box((-13, 13, 135, 143.5, 2, 13), "dark", "torso", "popped collar")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 166, -10, 10), "skin", "head", "head")
    d.box((-8, 8, 156, 159.5, -10.5, -10), "eye", "head", "shades", **plate())
    d.box((-1, 4, 150, 151, -10.5, -10), "skin_dk", "head", "smirk", **plate())
    d.box((-2, 2, 166, 172, -9, 10), "team", "head", "mohawk 1")
    d.box((-2, 2, 172, 177, -8, 8), "team", "head", "mohawk 2")
    d.box((-1.5, 1.5, 177, 179.5, -6, 5), "team", "head", "mohawk 3")
    return d


def vampire():
    d = Design("VAMPIRE", "the vampire: a black cape with team lining and a tall flared collar, a team waistcoat and white jabot, pale skin, slicked hair, red eyes")
    d.foot((6, 20, 0, 10, -17, 8), "dark", "shoe")
    d.leg(13, 7, -7.5, 7.5, 9, 90, 47, "dark")
    d.box((-19, 19, 82, 96, -12, 12), "dark", "pelvis", "hips")
    d.box((-21, 21, 94, 137, -12.5, 12.5), "team", "torso", "waistcoat")
    d.box((-5, 5, 118, 137, -13.5, -12.5), "white", "torso", "jabot", **plate())
    d.box((-3, 3, 110, 118, -13.5, -12.5), "white", "torso", "jabot frill", **plate())
    d.box((-1.5, 1.5, 131, 134, -14, -13.5), "glove", "torso", "brooch", **plate())
    d.box((-25, 25, 60, 140, 13, 17), "dark", "torso", "cape")
    d.sym((21, 26, 60, 140, -12, 13), "dark", "torso", "cape side")
    d.sym((21, 26, 60, 140, -13, -12), "team", "torso", "cape lining", **plate())
    d.sym((21.5, 33, 124, 141, -11, 12.5), "dark", "torso", "cape shoulder")
    d.box((-25.5, 25.5, 59.5, 66, 12.5, 17.5), "team", "torso", "cape hem")
    d.sym((20.5, 26.5, 59.5, 66, -13.5, 12.5), "team", "torso", "cape hem side")
    d.box((-9, 9, 112, 116, 17, 17.5), "team", "torso", "back emblem", **plate())
    d.box((-3, 3, 106, 122, 17.5, 18), "team", "torso", "back emblem upright", **plate())
    d.box((-14, 14, 136, 152, 7.5, 10), "dark", "torso", "collar")
    d.box((-14, 14, 136.5, 151.5, 7, 7.5), "team", "torso", "collar lining", **plate())
    d.sym((10.5, 14, 136, 151, -2, 7), "dark", "torso", "collar wing")
    d.box((-5, 5, 141, 147, -5, 5), "pale", "head", "neck")
    d.box((-9.5, 9.5, 146, 167, -10, 10), "pale", "head", "face")
    d.box((-10, 10, 163, 170, -10.5, 10.5), "dark", "head", "slick hair")
    d.box((-2, 2, 160, 163, -10.5, -10), "dark", "head", "widow's peak", **plate())
    d.sym((3, 6, 156, 158, -10.5, -10), "redeye", "head", "eye", **plate())
    d.box((-4, 4, 150.5, 151.5, -10.5, -10), "eye", "head", "mouth", **plate())
    d.sym((1.5, 2.5, 149, 150.5, -10.5, -10), "white", "head", "fang", **plate())
    return d


def pharaoh():
    d = Design("PHARAOH", "the pharaoh: a team-and-gold striped headdress with lappets and a cobra, a gold false beard, a broad team-and-gold collar, a white kilt, bare tan skin")
    d.foot((6, 20, 0, 4, -16, 8), "gold", "sandal")
    d.leg(13, 7, -7.5, 7.5, 4, 90, 47, "tan")
    d.thigh((5, 21, 66, 89.5, -9, 9), "white", "kilt")
    d.box((-20, 20, 80, 97, -13, 13), "white", "pelvis", "kilt top")
    d.box((-7, 7, 64, 94, -14, -13), "team", "pelvis", "kilt apron", **plate())
    d.box((-20.5, 20.5, 94, 99, -13.5, 13.5), "gold", "torso", "belt")
    d.box((-21, 21, 97, 137, -12, 12), "tan", "torso", "chest")
    d.box((-20, 20, 123, 140, -12.5, 12.5), "team", "torso", "broad collar")
    d.box((-18, 18, 134, 137, -13, -12.5), "gold", "torso", "collar row 1", **plate())
    d.box((-15, 15, 127, 130, -13, -12.5), "gold", "torso", "collar row 2", **plate())
    d.box((-18, 18, 130, 133, 12.5, 13), "gold", "torso", "collar back row", **plate())
    d.sym((20, 32, 118, 138, -9, 9), "tan", "torso", "shoulder")
    d.sym((20.5, 32.5, 120, 124, -9.5, 9.5), "gold", "torso", "arm band")
    d.box((-5, 5, 141, 147, -5, 5), "tan", "head", "neck")
    d.box((-9, 9, 146, 163, -9.5, 9.5), "tan", "head", "face")
    d.sym((2, 6.5, 155.5, 157.5, -10, -9.5), "eye", "head", "kohl eye", **plate())
    d.box((-2, 2, 141, 147, -10.5, -7.5), "gold", "head", "false beard")
    d.box((-11, 11, 160, 173, -10.5, 11), "team", "head", "headdress")
    d.box((-11.5, 11.5, 163, 165.5, -11, 11.5), "gold", "head", "headdress stripe 1")
    d.box((-11.5, 11.5, 168, 170.5, -11, 11.5), "gold", "head", "headdress stripe 2")
    d.sym((9.5, 14, 140, 163, -9, -2), "team", "head", "lappet")
    d.sym((9, 14.5, 148, 151, -9.5, -1.5), "gold", "head", "lappet stripe 1")
    d.sym((9, 14.5, 155, 158, -9.5, -1.5), "gold", "head", "lappet stripe 2")
    d.box((-6, 6, 150, 165, 11, 13.5), "team", "head", "queue")
    d.box((-1.5, 1.5, 166, 172, -12, -11), "gold", "head", "cobra", **plate())
    return d


def sumo():
    d = Design("SUMO", "the sumo: a huge round skin body and thick legs, a wide team belt with hanging cords, a black topknot")
    d.foot((5.5, 22.5, 0, 7, -15, 8), "skin", "foot")
    d.leg(14, 9.5, -10, 10, 6, 88, 44, "skin")
    d.box((-25, 25, 78, 98, -17, 17), "team", "pelvis", "belt")
    for k, x in enumerate((-10, -6, -2, 2, 6)):
        d.box((x, x + 2, 66, 82, -18, -17), "team_dk", "pelvis", "cord %d" % (k + 1), **plate())
    d.box((-27, 27, 96, 130, -19, 17), "skin", "torso", "belly")
    d.box((-25, 25, 130, 140, -15, 14), "skin", "torso", "chest")
    d.box((-1, 1, 106, 108, -19.5, -19), "skin_dk", "torso", "navel", **plate())
    d.sym((25, 34, 118, 140, -11, 11), "skin", "torso", "shoulder")
    d.box((-7, 7, 140, 147, -7, 7), "skin", "head", "neck")
    d.box((-10.5, 10.5, 145, 166, -10.5, 10.5), "skin", "head", "head")
    d.box((-11, 11, 160, 168, -10, 11), "hairblk", "head", "hair")
    d.box((-2, 2, 168, 172.5, -6, 4), "hairblk", "head", "topknot")
    d.box((-2.5, 2.5, 168, 170, 4, 6), "team", "head", "knot tie")
    d.sym((2.5, 7.5, 158, 159.5, -11, -10.5), "hairblk", "head", "brow", **plate())
    d.sym((3, 6, 155.5, 157, -11, -10.5), "eye", "head", "eye", **plate())
    d.box((-3, 3, 150, 151, -11, -10.5), "skin_dk", "head", "mouth", **plate())
    return d


def king():
    d = Design("KING", "the king: a gold crown with team jewels, a brown beard, a plum cape with a spotted white ermine mantle, a team tunic with a gold chain")
    d.foot((5.5, 20.5, 0, 10, -17, 8), "leather", "boot")
    d.leg(13, 7, -7.5, 7.5, 9, 90, 47, "white")
    d.thigh((5, 21, 68, 89.5, -9, 9), "team", "tunic skirt")
    d.box((-20, 20, 82, 97, -12.5, 12.5), "team", "pelvis", "hips")
    d.box((-22, 22, 94, 137, -13, 13), "team", "torso", "tunic")
    d.box((-22.5, 22.5, 93.5, 99, -13.5, 13.5), "gold", "torso", "belt")
    d.box((-10, 10, 126, 129, -14, -13), "gold", "torso", "chain", **plate())
    d.box((-3, 3, 118, 126, -14, -13), "gold", "torso", "medallion", **plate())
    d.box((-25, 25, 56, 136, 13, 17.5), "plum", "torso", "cape")
    d.sym((22, 26, 56, 136, -11, 13), "plum", "torso", "cape side")
    d.box((-26.5, 26.5, 55.5, 62, 12.5, 18), "white", "torso", "cape hem")
    d.box((-25.5, 25.5, 134, 143.5, -14, 14), "white", "torso", "ermine mantle")
    for k, x in enumerate((8, 18)):
        d.sym((x, x + 2, 137, 140, -14.5, -14), "eye", "torso", "spot %d" % (k + 1), **plate())
    for k, x in enumerate((-15, -1, 13)):
        d.box((x, x + 2, 137, 140, 14, 14.5), "eye", "torso", "back spot %d" % (k + 1), **plate())
    d.sym((23, 35, 124, 141, -11.5, 11.5), "white", "torso", "ermine shoulder")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 166, -10, 10), "skin", "head", "face")
    d.box((-10, 10, 145, 155, -10.5, -5), "hair", "head", "beard")
    d.box((-10, 10, 158, 165, -9, 10.5), "hair", "head", "hair")
    d.box((-6, 6, 157, 159, -10.5, -10), "eye", "head", "eyes", **plate())
    d.box((-10.5, 10.5, 165, 170, -10.5, 10.5), "gold", "head", "crown band")
    d.box((-9, 9, 169.5, 173, -9, 9), "plum", "head", "crown velvet")
    for sx in (-1, 1):
        for sz in (-1, 1):
            x0, z0 = (7.5, 7.5)
            d.box((min(sx * x0, sx * 10.5), max(sx * x0, sx * 10.5), 170, 175, min(sz * z0, sz * 10.5), max(sz * z0, sz * 10.5)),
                  "gold", "head", "crown point %s%s" % ("rl"[sx < 0], "bf"[sz < 0]))
    d.box((-1.5, 1.5, 170, 177, -10.5, -9), "gold", "head", "crown point front")
    d.box((-1.5, 1.5, 170, 177, 9, 10.5), "gold", "head", "crown point back")
    d.box((-2, 2, 166, 169, -11, -10.5), "team", "head", "jewel", **plate())
    d.sym((10.5, 11, 166, 169, -2, 2), "team", "head", "side jewel", **plate())
    return d


def miner():
    d = Design("MINER", "the miner: a yellow hard hat with a lit lamp, olive coveralls with team straps and knee pads, a tool belt and a pickaxe across the back")
    d.foot((5.5, 20.5, 0, 12, -17, 9), "dark", "work boot")
    d.leg(13, 7.5, -8, 8, 11, 91, 48, "olive")
    d.shin((8.5, 17.5, 38, 47, -8.5, -7.5), "team", "knee pad", **plate())
    d.box((-20, 20, 82, 97, -12.5, 12.5), "olive", "pelvis", "hips")
    d.box((-22.5, 22.5, 95, 137, -13.5, 13.5), "olive", "torso", "coveralls")
    d.sym((8, 13, 101, 137, -14, -13.5), "team", "torso", "strap", **plate())
    d.box((-8, 8, 116, 128, -14, -13.5), "team", "torso", "bib pocket", **plate())
    d.box((-13, 13, 101, 137, 13.5, 14), "team", "torso", "back straps", **plate())
    d.box((-23, 23, 94, 100, -14, 14), "leather", "torso", "tool belt")
    d.box((14, 20, 86, 96, -15.5, -14), "leather", "torso", "pouch")
    d.box((-1.5, 1.5, 96, 141, 14.5, 16.5), "leather", "torso", "pick handle")
    d.box((-24, 24, 136.5, 140.5, 14, 17), "mid", "torso", "pick head")
    d.sym((24, 30, 137.5, 139.5, 14.5, 16.5), "light", "torso", "pick point")
    d.sym((22.5, 34, 123, 139, -10.5, 10.5), "team", "torso", "shoulder")
    d.box((-13, 13, 136, 143.5, -11, 11), "olive", "torso", "collar")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 165, -10, 10), "skin", "head", "face")
    d.box((-7, 7, 150, 154, -10.5, -10), "hair", "head", "beard", **plate())
    d.box((-6, 6, 157.5, 159.5, -10.5, -10), "eye", "head", "eyes", **plate())
    d.box((-10.5, 10.5, 165, 174, -11, 11), "hivis", "head", "hard hat")
    d.box((-12, 12, 164, 166, -12, 12), "hivis", "head", "hat brim")
    d.box((-1.5, 1.5, 174, 176, -9, 9), "khaki", "head", "hat ridge")
    d.box((-3.5, 3.5, 166, 171, -12.5, -11), "dark", "head", "lamp")
    d.box((-2.5, 2.5, 167, 170, -13, -12.5), "lamp", "head", "lamp lens", **plate())
    return d


NAMES = ["ASTRONAUT", "SAMURAI", "VIKING", "CHEF", "FIREFIGHTER", "DIVER", "ALIEN", "MUMMY", "SKELETON", "CLOWN",
         "GLADIATOR", "GHOST", "CAT", "FROG", "PUNK", "VAMPIRE", "PHARAOH", "SUMO", "KING", "MINER"]
BUILDERS = {"ASTRONAUT": astronaut, "SAMURAI": samurai, "VIKING": viking, "CHEF": chef, "FIREFIGHTER": firefighter,
            "DIVER": diver, "ALIEN": alien, "MUMMY": mummy, "SKELETON": skeleton, "CLOWN": clown, "GLADIATOR": gladiator,
            "GHOST": ghost, "CAT": cat, "FROG": frog, "PUNK": punk, "VAMPIRE": vampire, "PHARAOH": pharaoh, "SUMO": sumo,
            "KING": king, "MINER": miner}
