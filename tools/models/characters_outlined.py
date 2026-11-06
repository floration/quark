"""The 11 player designs for gen_players_outlined.py: the OUTLINED direction (owner, 2026-11-05: "outlined for sure
not even close"), as box model files drawn by the game's own box path (its outlines, its face shading).

Every model: feet on the floor at the origin, facing -Z, +X its right, standing. A box is (x0, x1, y0, y1, z0, z1)
in centimetres on a 5 mm grid, a tone (gen_players_outlined.TONES: the shared slots dark / mid / light / bore / team
plus a few fixed colours a character needs), a joint (`bone`) and a name. Joints are measured by the game from the
boxes (models.cpp playerRig): the hip is the top of the thigh boxes, the knee their bottom, the ankle the top of the
foot boxes, the neck the bottom of the head boxes. leg() splits a leg at the knee as the built-ins do (the thigh
reaches 2 cm under the knee, the shin is 5 mm slimmer). The game draws the gun arm from the right shoulder (1.3 m up,
0.3 m out) and the grapple arm from the left, so bodies have shoulder pads, not arms (but the zombie's and the
fighter's, which are who they are).

Shared look: a few big blocks, no chamfers; dark / mid / light from the metal and base slots, big team panels on the
chest, back and shins (about a quarter of what's seen), at most a glow piece or two (visor, eyes).
"""


class Design:
    def __init__(self, name, desc):
        self.name, self.desc = name, desc
        self.boxes = []   # dict(b=(x0, x1, y0, y1, z0, z1), tone, bone, name, vis, shadow)

    def box(self, b, tone, bone="torso", name="", vis=False, shadow=True):
        self.boxes.append(dict(b=tuple(float(v) for v in b), tone=tone, bone=bone, name=name, vis=vis, shadow=shadow))
        return self

    def sym(self, b, tone, bone="torso", name="", **kw):
        """the box at +X (the model's right) and its mirror; a bone ending in _ takes _r / _l"""
        x0, x1, y0, y1, z0, z1 = b
        for sd, sfx in ((1, "_r"), (-1, "_l")):
            bb = (x0, x1, y0, y1, z0, z1) if sd > 0 else (-x1, -x0, y0, y1, z0, z1)
            self.box(bb, tone, bone + sfx[1:] if bone.endswith("_") else bone, name + sfx, **kw)
        return self

    # -- legs: the right side given (x > 0), mirrored
    def leg(self, x, hx, z0, z1, y0, y1, kn, tone, shin_tone=None, name="leg"):
        """a leg box from y0 (in the boot) to y1 (the hip), cut at the knee kn"""
        self.sym((x - hx, x + hx, kn - 2, y1, z0, z1), tone, "thigh_", name + " thigh")
        self.sym((x - hx + 0.5, x + hx - 0.5, y0, kn, z0 + 0.5, z1 - 0.5), shin_tone or tone, "shin_", name + " shin")

    def foot(self, b, tone, name="boot"):
        self.sym(b, tone, "foot_", name)

    def thigh(self, b, tone, name, **kw):
        self.sym(b, tone, "thigh_", name, **kw)

    def shin(self, b, tone, name, **kw):
        self.sym(b, tone, "shin_", name, **kw)


def plate(**kw):
    """a face plate: drawn only seen from outside (hidden on your own body), no shadow"""
    return dict(vis=True, shadow=False, **kw)


# ---------------------------------------------------------------------------------------------------------------

def standard():
    d = Design("STANDARD", "the soldier: light armour, team torso and thighs, a cyan visor and a team crest")
    d.foot((5, 21, 0, 13, -17.5, 9.5), "dark")
    d.leg(13, 7.5, -8, 8, 11, 91, 48, "team", "light")
    d.shin((8.5, 17.5, 22, 40, -8, -7.5), "team", "shin plate", **plate())
    d.shin((9, 17, 41, 47.5, -8.5, -7.5), "dark", "knee", **plate())
    d.box((-20.5, 20.5, 81.5, 97, -12.5, 12.5), "dark", "pelvis", "hips")
    d.box((-23.5, 23.5, 95, 138, -14, 14), "team", "torso", "torso")
    d.box((-19, 19, 116, 136, -15.5, -14), "light", "torso", "chest plate", **plate())
    d.box((-6, 6, 100, 112, -15, -14), "dark", "torso", "belt buckle", **plate())
    d.box((-17, 17, 136, 143.5, -11, 11), "light", "torso", "collar")
    d.box((-15, 15, 102, 132, 14, 18), "dark", "torso", "pack")
    d.box((-10, 10, 117, 127, 18, 19), "team", "torso", "pack stripe", **plate())
    d.sym((20, 37, 124.5, 139.5, -10.5, 10.5), "light", "torso", "pad")
    d.sym((20.5, 36.5, 122.5, 124.5, -10, 10), "dark", "torso", "pad rim")
    d.box((-5, 5, 143, 150, -5, 5), "dark", "head", "neck")
    d.box((-10.5, 10.5, 149, 172, -11, 11), "light", "head", "helmet")
    d.box((-8.5, 8.5, 158, 164.5, -11.5, -11), "visor", "head", "visor", **plate())
    d.box((-6, 6, 151, 153.5, -11.5, -11), "dark", "head", "chin", **plate())
    d.box((-3, 3, 172, 176.5, -10, 10), "team", "head", "crest")
    return d


def heavy():
    d = Design("HEAVY", "the tank: bulky dark armour, huge light pauldrons, a team chest plate, a small helmet with a glow slit")
    d.foot((5, 25, 0, 15, -21, 11), "dark", "boot")
    d.foot((5.5, 24.5, 15, 17, -20.5, 0), "mid", "boot cap")
    d.leg(15, 9.5, -10.5, 10.5, 14, 88, 46, "mid", "dark")
    d.shin((7, 23, 18, 43, -11, -10), "team", "shin plate", **plate())
    d.thigh((7.5, 22.5, 56, 80, -11.5, -10.5), "team", "thigh plate", **plate())
    d.box((-24, 24, 78, 95, -14.5, 14.5), "dark", "pelvis", "hips")
    d.box((-26, 26, 92, 137, -17.5, 17.5), "dark", "torso", "torso")
    d.box((-21, 21, 100, 134, -19, -17.5), "team", "torso", "chest plate", **plate())
    d.box((-10, 10, 110, 124, -20, -19), "light", "torso", "chest badge", **plate())
    d.box((-18, 18, 98, 134, 17.5, 23.5), "mid", "torso", "power pack")
    d.box((-13, 13, 104, 128, 23.5, 25), "team", "torso", "pack panel", **plate())
    d.box((-23, 23, 92.5, 98.5, -18.5, 18.5), "mid", "torso", "belt")
    d.sym((21, 38, 119, 143.5, -9.5, 9.5), "light", "torso", "pauldron")
    d.sym((22, 37.5, 112, 119, -9, 9), "team", "torso", "pauldron band")
    d.box((-14, 14, 136, 143.5, -12, 12), "mid", "torso", "gorget")
    d.box((-9.5, 9.5, 143, 165, -10, 10), "mid", "head", "helmet")
    d.box((-10.5, 10.5, 147, 158, -11.5, 11.5), "dark", "head", "jaw guard")
    d.box((-7.5, 7.5, 158.5, 161.5, -11, -10), "visor", "head", "visor slit", **plate())
    d.box((-4, 4, 165, 168, -7, 7), "team", "head", "helmet ridge")
    return d


def simple():
    d = Design("SIMPLE", "the plainest figure: one team body block, a light head with a visor, plain legs")
    d.foot((6.5, 19.5, 0, 10, -14, 8), "dark", "shoe")
    d.leg(13, 6.5, -7, 7, 9, 90, 47, "mid")
    d.box((-18, 18, 86, 96, -11, 11), "dark", "pelvis", "hips")
    d.box((-22, 22, 94, 140, -13, 13), "team", "torso", "body")
    d.box((-10, 10, 146, 170, -10.5, 10.5), "light", "head", "head")
    d.box((-7.5, 7.5, 157, 162, -11, -10.5), "visor", "head", "visor", **plate())
    d.box((-4.5, 4.5, 140, 146.5, -4.5, 4.5), "dark", "head", "neck")
    return d


def robot():
    d = Design("ROBOT", "the machine: a boxy head with glowing eyes and an antenna, piston legs, a team chest and thigh panels")
    d.foot((4.5, 21.5, 0, 14, -18.5, 10.5), "dark", "foot")
    d.leg(13, 6, -6, 6, 13, 88, 48, "mid")
    d.shin((7.5, 18.5, 18, 44, -8, -5.5), "team", "piston cover", **plate())
    d.thigh((6.5, 19.5, 55, 84, -8.5, 8.5), "team", "thigh block")
    d.box((-19, 19, 82, 93, -11, 11), "dark", "pelvis", "hips")
    d.box((-24, 24, 93, 139, -15.5, 15.5), "light", "torso", "chassis")
    d.box((-18, 18, 106, 133, -17, -15.5), "team", "torso", "chest panel", **plate())
    d.box((-5, 5, 112, 120, -18, -17), "visor", "torso", "core", **plate())
    d.box((-18, 18, 100, 135, 15.5, 21.5), "team", "torso", "back block")
    d.sym((15, 17, 103, 132, 21.5, 23), "bore", "torso", "vent", **plate())
    d.sym((24, 34, 118, 140, -9, 9), "mid", "torso", "shoulder")
    d.sym((34, 36.5, 121, 137, -7, 7), "dark", "torso", "shoulder bolt")
    d.box((-6, 6, 139, 147, -6, 6), "dark", "head", "neck")
    d.box((-11.5, 11.5, 146, 168, -12, 12), "light", "head", "head")
    d.box((-10, 10, 155, 162, -12.5, -12), "bore", "head", "eye bar", **plate())
    d.sym((3, 8, 156.5, 160.5, -13, -12.5), "visor", "head", "eye", **plate())
    d.box((-12, 12, 151, 153, -12.5, 12.5), "team", "head", "jaw band")
    d.sym((11.5, 13.5, 154, 162, -5, 5), "dark", "head", "ear bolt")
    d.box((5, 6.5, 168, 176, 4, 5.5), "mid", "head", "antenna")
    d.box((4, 7.5, 176, 179.5, 3, 6.5), "team", "head", "antenna tip")
    return d


def knight():
    d = Design("KNIGHT", "the knight: light plate, a team tabard front and back, pauldrons, a great helm with a team plume")
    d.foot((5.5, 21.5, 0, 11, -19.5, 9), "mid", "sabaton")
    d.leg(13.5, 7.5, -8, 8, 10, 90, 47, "light")
    d.shin((9, 18, 40.5, 48, -9, -8), "mid", "knee cop", **plate())
    d.shin((9.5, 17.5, 18, 38, -8.5, -7.5), "team", "greave stripe", **plate())
    d.box((-20, 20, 80, 96, -12.5, 12.5), "mid", "pelvis", "faulds")
    d.box((-22.5, 22.5, 94, 137, -14, 14), "light", "torso", "breastplate")
    d.box((-14, 14, 72, 130, -15.5, -14), "team", "torso", "tabard front", **plate())
    d.box((-14, 14, 72, 130, 14, 15.5), "team", "torso", "tabard back", **plate())
    d.box((-3, 3, 100, 124, -16.5, -15), "light", "torso", "cross upright", **plate())
    d.box((-9, 9, 109, 115, -16, -15), "light", "torso", "cross arm", **plate())
    d.box((-23, 23, 94.5, 99.5, -14.5, 14.5), "dark", "torso", "belt")
    d.sym((19.5, 37, 125, 140, -11, 11), "light", "torso", "pauldron")
    d.sym((20.5, 36, 120.5, 125, -10.5, 10.5), "mid", "torso", "pauldron lame")
    d.box((-12, 12, 136, 143.5, -10, 10), "mid", "torso", "gorget")
    d.box((-11, 11, 146, 171, -11.5, 11.5), "light", "head", "great helm")
    d.box((-9, 9, 157, 160, -12.5, -12), "bore", "head", "eye slit", **plate())
    d.box((-1, 1, 147, 156.5, -12.5, -11.5), "mid", "head", "breath ridge", **plate())
    d.box((-11.5, 11.5, 156.5, 160.5, -12, 12), "mid", "head", "helm band")
    d.box((-2.5, 2.5, 171, 175, -9, 9), "mid", "head", "crest mount")
    d.box((-2, 2, 175, 179.5, -6, 15), "team", "head", "plume")
    d.box((-2, 2, 166, 175, 11.5, 15.5), "team", "head", "plume tail")
    return d


def wizard():
    d = Design("WIZARD", "the wizard: a long purple robe with team panels, a white beard, a tall pointed hat with a team band and a gold star")
    # legs under the robe: thighs and shins hidden inside it, the shoes peek out as they step
    d.foot((6.5, 18.5, 0, 8, -15.5, 6), "leather", "shoe")
    d.leg(12.5, 5.5, -6, 6, 7, 88, 46, "robe")
    d.shin((7, 18, 8, 30, -9, 9), "robe", "robe hem")
    d.shin((6.5, 18.5, 7.5, 12, -9.5, 9.5), "team", "hem band")
    d.thigh((5, 20, 46, 80, -11, 11), "robe", "robe skirt")
    d.box((-21, 21, 78, 98, -13.5, 13.5), "robe", "pelvis", "robe hips")
    d.box((-22, 22, 96, 138, -14, 14), "robe", "torso", "robe body")
    d.box((-7, 7, 46, 138, -15, -14), "team", "torso", "front panel", **plate())
    d.box((-22.5, 22.5, 92, 98, -14.5, 14.5), "team", "torso", "sash")
    d.box((-16, 16, 100, 136, 14, 15.5), "team", "torso", "back panel", **plate())
    d.sym((19, 34, 116, 139, -11, 11), "robe", "torso", "sleeve")
    d.sym((19.5, 33.5, 113, 116, -11, 11), "team", "torso", "cuff")
    d.box((-16, 16, 136, 143, -11, 11), "team", "torso", "mantle")
    d.box((-8.5, 8.5, 146, 162, -9, 9), "skin", "head", "face")
    d.box((-9, 9, 134, 155, -11.5, -7), "beard", "head", "beard")
    d.box((-6, 6, 126, 134, -11, -7.5), "beard", "torso", "beard tip")
    d.box((-6, 6, 155.5, 158, -9.5, -9), "eye", "head", "eyes", **plate())
    d.box((-12, 12, 161.5, 164, -12, 12), "robe", "head", "hat brim")
    d.box((-16.5, 16.5, 162, 163.5, -5, 5), "robe", "head", "hat brim wide")
    d.box((-5, 5, 162, 163.5, -16.5, 16.5), "robe", "head", "hat brim deep")
    d.box((-9.5, 9.5, 164, 167.5, -9.5, 9.5), "team", "head", "hat band")
    d.box((-8, 8, 167.5, 171.5, -8, 8), "robe", "head", "hat 1")
    d.box((-6, 6, 171.5, 175, -5, 7), "robe", "head", "hat 2")
    d.box((-4, 4, 175, 177.5, -2, 9), "robe", "head", "hat 3")
    d.box((-2, 2, 177.5, 179.5, 3, 12), "robe", "head", "hat tip")
    d.box((-2.5, 2.5, 168, 171, -8.5, -8), "gold", "head", "star", **plate())
    return d


def zombie():
    d = Design("ZOMBIE", "the zombie: green skin, arms reaching out, a torn team shirt and ripped trousers, glowing eyes")
    d.foot((6, 20, 0, 9, -15, 8), "dark", "shoe")
    d.foot((6.5, 11, 9, 11, -14.5, -4), "zgreen", "toe")
    d.leg(13, 7, -7.5, 7.5, 8, 88, 46, "mid")
    d.shin((7, 13, 22, 34, -7.5, -6.5), "zgreen", "torn knee", **plate())
    d.sym((14.5, 18.5, 36, 40, -7.5, -6.5), "zgreen", "shin_", "rip", **plate())
    d.thigh((5.5, 20.5, 52, 58, -8.5, 8.5), "mid_dk", "turn-up")
    d.box((-19, 19, 80, 94, -12, 12), "dark", "pelvis", "belt")
    d.box((-21.5, 21.5, 92, 136, -13, 13), "team", "torso", "shirt")
    # the torn hem: uneven tatters below the shirt
    d.box((-21.5, -11, 86, 92, -13, 13), "team", "torso", "tatter l")
    d.box((2, 14, 84, 92, -13, 13), "team", "torso", "tatter m")
    d.box((-6, 8, 104, 116, -14, -13), "zgreen", "torso", "hole", **plate())
    d.box((-14, -8, 120, 126, -14, -13), "zgreen", "torso", "rip", **plate())
    d.box((6, 14, 108, 122, 13, 14), "zgreen", "torso", "back rip", **plate())
    d.box((-16, 16, 133, 141, -10, 10), "zgreen", "torso", "neck")
    d.sym((21.5, 30.5, 118, 138, -9, 9), "team", "torso", "sleeve")
    # the arms reach forward (the gun arm is drawn over the right one)
    d.sym((18, 25, 120, 128, -29, -9), "zgreen", "torso", "arm")
    d.sym((17.5, 25.5, 118.5, 129.5, -30, -29), "zgreen_dk", "torso", "hand")
    d.box((-11, 11, 142, 165, -11, 10), "zgreen", "head", "head")
    d.box((-11.5, 11.5, 162, 168, -11.5, 10.5), "hair", "head", "hair")
    d.box((-11.5, -2.5, 159, 162, -11.5, -8), "hair", "head", "fringe")
    d.sym((2.5, 8, 155, 159, -11.5, -11), "zeye", "head", "eye", **plate())
    d.box((-7, 7, 147, 150, -11.5, -11), "zgreen_dk", "head", "mouth", **plate())
    d.box((-5, 5, 140, 144, -5, 5), "zgreen_dk", "head", "neck joint")
    return d


def fighter():
    d = Design("FIGHTER", "the brawler: bare arms, a team tank top and headband, red boxing gloves, dark trousers, bare feet")
    d.foot((6.5, 19.5, 0, 7, -15, 7), "skin", "foot")
    d.leg(13, 7.5, -8, 8, 6, 90, 47, "dark")
    d.shin((6.5, 19.5, 5.5, 12, -8.5, 8.5), "light", "ankle wrap")
    d.thigh((6, 20, 82, 91, -8.5, 8.5), "dark", "trouser top")
    d.box((-19, 19, 84, 96, -12, 12), "dark", "pelvis", "trousers")
    d.box((-20, 20, 90, 97, -12.5, 12.5), "team", "pelvis", "belt")
    d.box((-19, 19, 96, 136, -12, 12), "team", "torso", "tank top")
    d.box((-7, 7, 121, 136, -12.5, -12), "skin", "torso", "neckline", **plate())
    d.box((-21, 21, 128, 141, -11, 11), "skin", "torso", "shoulders")
    d.sym((19, 30, 118, 140.5, -9, 9), "skin", "torso", "deltoid")
    d.sym((20, 29, 101, 118, -8, 8), "skin", "torso", "arm")
    d.sym((19.5, 29.5, 100.5, 104, -8.5, 8.5), "light", "torso", "wrist wrap")
    d.sym((18.5, 31, 86, 101, -10, 8), "glove", "torso", "glove")
    d.sym((18.5, 31, 92, 96, 8, 9), "light", "torso", "glove cuff")
    d.box((-5.5, 5.5, 139, 147, -5.5, 5.5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 168, -10.5, 10), "skin", "head", "head")
    d.box((-10, 10, 164, 170, -10, 10.5), "hair", "head", "hair")
    d.box((-10.5, 10.5, 160, 164, -11, 11), "team", "head", "headband")
    d.box((-3, 3, 152, 161, 11, 15), "team", "head", "headband tail")
    d.box((-6.5, 6.5, 155.5, 157.5, -11, -10.5), "eye", "head", "eyes", **plate())
    d.box((-3, 3, 148.5, 150, -11, -10.5), "skin_dk", "head", "mouth", **plate())
    return d


def pirate():
    d = Design("PIRATE", "the pirate: a long team coat with gold buttons, a white shirt, a black tricorn with gold trim, boots and an eye patch")
    d.foot((5.5, 20.5, 0, 14, -17, 9), "dark", "boot")
    d.leg(13, 7, -7.5, 7.5, 13, 90, 47, "light", "dark")
    d.shin((5, 21, 35, 45, -9, 9), "dark", "boot cuff")
    d.box((-20, 20, 82, 96, -12, 12), "leather", "pelvis", "belt")
    d.box((-4, 4, 85, 93, -12.5, -12), "gold", "pelvis", "buckle", **plate())
    d.box((-23, 23, 94, 138, -14, 14), "team", "torso", "coat")
    d.thigh((5, 21, 58, 92, 4, 14), "team", "coat tail")
    d.box((-9, 9, 98.5, 138, -15, -14), "light", "torso", "shirt", **plate())
    d.sym((9, 10.5, 106, 108, -15.5, -14), "gold", "torso", "button 1", **plate())
    d.sym((9, 10.5, 120, 122, -15.5, -14), "gold", "torso", "button 2", **plate())
    d.box((-22.5, 22.5, 98, 103, -14.5, 14.5), "leather", "torso", "sash belt")
    d.box((-14, 14, 136, 142, -11.5, 11.5), "light", "torso", "collar")
    d.sym((20, 35, 123, 138.5, -10, 10), "team", "torso", "shoulder")
    d.sym((21, 34, 120, 123, -9.5, 9.5), "gold", "torso", "epaulette")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 166, -10, 10), "skin", "head", "head")
    d.box((-9, 9, 145.5, 154, -10.5, -6), "hair", "head", "beard")
    d.box((1.5, 7.5, 156, 161.5, -10.5, -10), "eye", "head", "eye patch", **plate())
    d.box((-6, -3.5, 157.5, 160, -10.5, -10), "eye", "head", "eye", **plate())
    d.box((-9.5, 1.5, 161, 162, -10.5, -10), "eye", "head", "patch strap", **plate())
    # the tricorn: a crown over a cross of brim boxes, the brim's sides turned up
    d.box((-10, 10, 166.5, 173, -10, 10), "dark", "head", "tricorn crown")
    d.box((-12, 12, 165.5, 168, -12, 12), "dark", "head", "brim")
    d.box((-16.5, 16.5, 166, 167.5, -5, 5), "dark", "head", "brim wide")
    d.box((-5, 5, 166, 167.5, -16.5, 16.5), "dark", "head", "brim deep")
    d.sym((11.5, 16.5, 167.5, 176, -5, 5), "dark", "head", "brim turn")
    d.box((-5, 5, 167.5, 176, 11.5, 16.5), "dark", "head", "brim turn back")
    d.box((-11.5, 11.5, 168, 170, -12.5, -12), "gold", "head", "trim", **plate())
    d.box((-3, 3, 170, 172.5, -10.5, -10), "light", "head", "skull", **plate())
    return d


def ninja():
    d = Design("NINJA", "the ninja: dark wraps head to toe, a hood with an eye slit, team sash, shin wraps, headband and scarf tails")
    d.foot((7, 19, 0, 8, -15, 7.5), "dark", "tabi")
    d.leg(13, 7, -7.5, 7.5, 7, 90, 47, "mid", "dark")
    d.shin((7, 19, 12, 40, -8, 8), "team", "shin wrap")
    d.thigh((5.5, 20.5, 46, 56, -8, 8), "mid_dk", "knee wrap")
    d.box((-18, 18, 82, 96, -11.5, 11.5), "mid", "pelvis", "hips")
    d.box((-20, 20, 94, 137, -12.5, 12.5), "mid", "torso", "gi")
    d.box((-6, 6, 106, 137, -13.5, -12.5), "dark", "torso", "gi cross", **plate())
    d.box((-20.5, 20.5, 92, 101, -13, 13), "team", "torso", "sash")
    d.box((8, 14, 70, 92, -14, -13), "team", "torso", "sash tail", **plate())
    d.box((-14, 14, 104, 131, 12.5, 18), "dark", "torso", "sword scabbard")
    d.box((-3, 3, 131, 143.5, 15, 18), "dark", "torso", "sword grip")
    d.sym((19, 32, 121, 138, -9, 9), "mid", "torso", "shoulder")
    d.sym((20, 31, 117, 121, -8.5, 8.5), "team", "torso", "arm wrap")
    d.box((-13, 13, 135, 143.5, -10, 10), "dark", "torso", "cowl")
    d.box((-10, 10, 142, 169, -10.5, 10.5), "dark", "head", "hood")
    d.box((-8, 8, 155, 160.5, -11, -10.5), "skin", "head", "eye strip", **plate())
    d.sym((2, 6.5, 156.5, 159, -11.5, -11), "eye", "head", "eye", **plate())
    d.box((-10.5, 10.5, 161.5, 165, -11, 11), "team", "head", "headband")
    d.box((-2.5, 2.5, 150, 164.5, 11, 15.5), "team", "head", "headband tail")
    return d


def bandit():
    d = Design("BANDIT", "the bandit: a wide tan cowboy hat with a team band, a team bandana over the face, a leather vest, chaps and a bandolier")
    d.foot((5.5, 20.5, 0, 12, -18, 8.5), "leather", "boot")
    d.foot((19.5, 22.5, 2, 6, 5, 8), "gold", "spur")
    d.leg(13, 7.5, -8, 8, 11, 90, 47, "denim")
    d.shin((5, 21, 18, 40, -8.5, -7.5), "team", "chap shin", **plate())
    d.thigh((5, 21, 50, 82, -9, -8), "leather", "chap", **plate())
    d.box((-19.5, 19.5, 82, 96, -12, 12), "leather", "pelvis", "gun belt")
    d.box((-4, 4, 85, 93, -12.5, -12), "gold", "pelvis", "buckle", **plate())
    d.box((19.5, 24, 74, 92, -5, 5), "dark", "pelvis", "holster")
    d.box((-21.5, 21.5, 94, 137, -13, 13), "team", "torso", "shirt")
    d.sym((5, 21.5, 96, 135, -14, -13), "leather", "torso", "vest front", **plate())
    d.box((-21.5, 21.5, 96, 135, 13, 14), "leather", "torso", "vest back", **plate())
    # the bandolier: corner to corner across the chest in steps (boxes don't turn)
    for i in range(5):
        x0 = -17 + i * 7
        d.box((x0, x0 + 7, 101 + i * 6.5, 108 + i * 6.5, -15, -14), "dark", "torso", "bandolier %d" % (i + 1), **plate())
    d.sym((20, 33, 122, 138, -9.5, 9.5), "team", "torso", "shoulder")
    d.box((-13, 13, 135, 143, -11, 11), "team", "torso", "bandana neck")
    d.box((-5, 5, 141, 147, -5, 5), "skin", "head", "neck")
    d.box((-9.5, 9.5, 146, 166, -10, 10), "skin", "head", "head")
    d.box((-10, 10, 145.5, 156, -11, 10.5), "team", "head", "bandana")
    d.box((-4, 4, 141.5, 145.5, -11, -8), "team", "head", "bandana point")
    d.box((-7, 7, 157, 159.5, -10.5, -10), "eye", "head", "eyes", **plate())
    d.box((-12, 12, 165, 167, -12, 12), "hat", "head", "brim")
    d.box((-16.5, 16.5, 165.5, 166.5, -5, 5), "hat", "head", "brim wide")
    d.box((-5, 5, 165.5, 166.5, -16.5, 16.5), "hat", "head", "brim deep")
    d.sym((12, 16.5, 166.5, 169.5, -5, 5), "hat", "head", "brim curl")
    d.box((-9, 9, 167, 170.5, -9.5, 9.5), "team", "head", "hat band")
    d.box((-8.5, 8.5, 170.5, 177.5, -9, 9), "hat", "head", "crown")
    d.box((-1.5, 1.5, 176.5, 178, -6.5, 6.5), "hat_dk", "head", "crown dent", shadow=False)
    return d


BUILDERS = {"STANDARD": standard, "HEAVY": heavy, "SIMPLE": simple, "ROBOT": robot, "KNIGHT": knight, "WIZARD": wizard,
            "ZOMBIE": zombie, "FIGHTER": fighter, "PIRATE": pirate, "NINJA": ninja, "BANDIT": bandit}
