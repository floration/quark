"""ORBIT (1.19.6): 1v1 (and duel, team vs, tourney) in one space-station module, a centrifuge lab 48 x 32 m and 18 m
tall, mirrored west to east:
  bays       a docking bay at each end (10 m deep, normal gravity), a shuttle's nose as cover; the spawns are behind it
             (A west, B east), stairs up both long walls to the catwalks;
  flip deck  the middle 16 x 20 m of floor is gravity-flip tiles: step on and you fall up 18 m and stand on the
             ceiling, which is a second fighting floor (crates and console screens built upside down). Floor lanes
             6 m wide run beside it on both sides with normal gravity, crate stacks at the deck's ends break the line
             bay to bay;
  core       a 8 x 8 m low-gravity column (x0.5) floor to ceiling round the reactor spindle (can't be hooked), through
             the flip deck's middle: inside you float between the floors;
  catwalks   2.5 m wide at 6 m along both long walls, outside the flip footprint (normal gravity): the walk route and
             the safe flank.
Windows (clip) along the long walls look out on a planet; the sky is black, so it draws stars. Every route is 4 m tall
(DOUBLE SIZE); girders on the ceiling every 6 m and the catwalk rails are the hooks."""
import os
from .prefabs import M
from . import prefabs
from .bigkit import Kit, num, yaw_to

HX, HZ = 24.0, 16.0
TOP = 18.0
FLIP = (-8.0, -10.0, 8.0, 10.0)
CORE = 4.0
CAT_Y = 6.0


def build():
    m = M("Orbit", "1v1", sun=(0.6, 0.7, -0.4))
    m.also = ["duel", "vs", "tourney"]
    k = Kit(m)
    m.raw("palette base 0.78 0.80 0.84 accent 0.22 0.36 0.62 metal 0.28 0.30 0.34 glow 0.40 0.85 1.00 sky 0 0 0")

    # ---- the hull: floor, long walls to 12 with chamfers up to the ceiling's 20 m wide flat, end walls, windows
    fx0, fz0, fx1, fz1 = FLIP
    k.box("steeldeckplate", -HX, -1, -HZ, HX, 0, fz0); k.box("steeldeckplate", -HX, -1, fz1, HX, 0, HZ)
    k.box("steeldeckplate", -HX, -1, fz0, fx0, 0, fz1); k.box("steeldeckplate", fx1, -1, fz0, HX, 0, fz1)
    k.box("gravityflip", fx0, -1, fz0, fx1, 0, fz1)
    for sz in (-1, 1):
        za, zb = sorted((sz * HZ, sz * (HZ + 1)))
        # the long wall: solid below the windows (8) and above them (11), piers between the windows
        k.box("scifipanels", -HX - 1, -1, za, HX + 1, 8, zb); k.box("scifipanels", -HX - 1, 11, za, HX + 1, 12, zb)
        x = -HX
        for wx in (-18, -6, 6, 18):
            k.box("scifipanels", x, 8, za, wx - 2, 11, zb); x = wx + 2
            k.box("clip", wx - 2, 8, za, wx + 2, 11, zb)
        k.box("scifipanels", x, 8, za, HX, 11, zb)
        # the chamfer (12 -> 18, |z| 16 -> 10)
        k.wedge("aircraftskin", 2 if sz > 0 else 3, -HX - 1, 12, min(sz * 10, sz * (HZ + 1)), HX + 1, TOP + 0.5, max(sz * 10, sz * (HZ + 1)), up=1)
    for sx in (-1, 1):
        xa, xb = sorted((sx * HX, sx * (HX + 1)))
        k.box("boltedaccesspanels", xa, -1, -HZ - 1, xb, TOP + 0.5, HZ + 1)
    k.box("hexboltgrid", -HX - 1, TOP, -10, HX + 1, TOP + 0.5, 10)                          # the ceiling (a floor upside down)
    # girders across the ceiling every 6 m (hooks; 0.4 deep, so the ceiling stays a floor to run on)
    for x in range(-21, 22, 6):
        k.box("anodizedblack", x - 0.25, TOP - 0.4, -10, x + 0.25, TOP, 10)

    # ---- the core: the low-gravity column and the reactor spindle
    k.box("lowgravityfield", -CORE, 0, -CORE, CORE, TOP, CORE)
    k.disc("nograpple", 0, 0, 1.1, 0, TOP, n=8)
    k.disc("heattintedtitanium", 0, 0, 1.6, 0, 1.0, n=8)
    k.disc("heattintedtitanium", 0, 0, 1.6, TOP - 1.0, TOP, n=8)

    # ---- the flip deck's two floors: low cover on the floor, the same built upside down on the ceiling
    for (x, z) in ((-5.5, -7), (5.5, 7)):                         # floor: consoles with 1/8 m screens
        k.box("controlpanel", x - 1.5, 0, z - 0.6, x + 1.5, 1.1, z + 0.6)
        k.box("hologrampanel", x - 1.5, 1.1, z - 0.0625, x + 1.5, 2.3, z + 0.0625)
    for (x, z) in ((-5.5, 7), (5.5, -7)):                         # ceiling: crates hanging, consoles upside down
        k.box("controlpanel", x - 1.5, TOP - 1.1, z - 0.6, x + 1.5, TOP, z + 0.6)
        k.box("hologrampanel", x - 1.5, TOP - 2.3, z - 0.0625, x + 1.5, TOP - 1.1, z + 0.0625)
    for (x, z) in ((-2, -8), (2, 8)):
        k.box("aluminiumtreadplate", x - 1.2, TOP - 1.4, z - 1.2, x + 1.2, TOP, z + 1.2)
    # ceiling rails along the flip deck's long edges (the edge where the flip ends: a lip so you see it)
    for sz in (-1, 1):
        za, zb = sorted((sz * 9.9, sz * 10))
        k.box("warninglights", fx0, TOP - 0.6, za, fx1, TOP - 0.4, zb)
    # crate stacks at the deck's ends (floor and ceiling): no line bay to bay down the middle
    for sx in (-1, 1):
        x = sx * 11
        k.box("aluminiumtreadplate", x - 1.5, 0, -2.5, x + 1.5, 3.0, 2.5)
        k.box("aluminiumtreadplate", x - 1.0, 3.0, -1.5, x + 1.0, 4.4, 1.5)
        k.box("aluminiumtreadplate", x - 1.5, TOP - 3.0, -2.5, x + 1.5, TOP, 2.5)

    # ---- the catwalks at 6 m along both long walls (x -12..12), stairs up from both bays
    for sz in (-1, 1):
        za, zb = sorted((sz * 13.5, sz * HZ))
        k.box("fibreglassgrating", -12, CAT_Y - 0.3, za, 12, CAT_Y, zb)
        ra, rb = sorted((sz * 13.5, sz * 13.6))
        k.box("chrome", -12, CAT_Y, ra, 12, CAT_Y + 1.0, rb)
        for (xa, xb, r) in ((-HX, -12, 1), (12, HX, -1)):
            k.stairs("aluminiumtreadplate", xa, za, xb, zb, CAT_Y, "x", r)
        for x in (-6, 0, 6):
            k.box("chrome", x - 0.15, 0, ra, x + 0.15, CAT_Y - 0.3, rb)

    # ---- the bays: shuttle noses (a block and a sloped nose toward the deck), spawns behind them
    for sx in (-1, 1):
        xb = sx * 16
        xa = sx * 20
        a, b = sorted((xa, xb))
        k.box("aircraftskin", a, 0, -3.5, b, 3.6, 3.5)
        k.wedge("aircraftskin", 0 if sx < 0 else 1, *((b, 0, -3.5, b + 3, 3.6, 3.5) if sx < 0 else (a - 3, 0, -3.5, a, 3.6, 3.5)))
        k.box("anodizedblack", a + 0.5, 2.2, -3.55, b - 0.5, 3.0, 3.55)                      # (its window band)
    for (x, z) in ((-22.6, -3), (-22.6, -1), (-22.6, 1), (-22.6, 3)):   # (behind the nose: out of the other bay's sight)
        k.spawn("a", x, 0, z, yaw_to(x, z, 0, z * 4))
        k.spawn("b", -x, 0, -z, yaw_to(-x, -z, 0, -z * 4))
    for (x, z) in ((-18, -11.5), (-18, 11.5), (-12.5, -6.5), (-12.5, 6.5)):
        k.spawn("any", x, 0, z, yaw_to(x, z, 0, z))
        k.spawn("any", -x, 0, -z, yaw_to(-x, -z, 0, -z))

    # ---- outside: a planet in the windows (unlit scenery, far off)
    k.box("far", -60, -40, -140, 60, 30, -120)
    k.box("far", -40, -30, 120, 20, 10, 130)

    # ---- light: cold strips along both decks, orange at the reactor
    for x in (-18, -6, 6, 18):
        for z in (-12, 12): k.light(x, 10, z, 14, 0.8)
    for x in (-20, 20): k.light(x, 6, 0, 14, 0.8)
    k.light(0, 9, 0, 12, 0.9)
    for sz in (-1, 1):
        k.box("lightblock", -HX, 11.6, sz * 15.9 - 0.1, HX, 11.9, sz * 15.9 + 0.1)
    # ---- decoration (flush, on the walls): hazard bands round the flip deck, panels and lamps in the bays, the bay
    # doors' frames on the end walls
    for (a0, b0, a1, b1) in ((fx0 - 0.4, fz0 - 0.4, fx1 + 0.4, fz0), (fx0 - 0.4, fz1, fx1 + 0.4, fz1 + 0.4), (fx0 - 0.4, fz0, fx0, fz1), (fx1, fz0, fx1 + 0.4, fz1)):
        k.box("hazard", a0, 0, b0, a1, 0.02, b1)
    for sx in (-1, 1):
        xw = sx * HX
        a, b = sorted((xw, xw - sx * 0.06))
        k.box("anodizedblack", a, 0, -6, b, 9, 6)                                           # (the docking door)
        k.box("warninglights", a - sx * 0.02, 9, -6.2, b - sx * 0.02, 9.4, 6.2)
        for z in (-8, 8): k.box("controlpanel", a, 1.0, z - 1.2, b, 2.4, z + 1.2)
    k.box("electroplatednickel", -1.8, TOP - 3, -1.8, 1.8, TOP - 1.0, 1.8)                  # (the reactor's upper collar)
    m.raw("setting space station centrifuge")
    m.save("orbit", dress=False)
    # (one chamber: no free for all, whatever save() adds)
    path = os.path.join(prefabs.OUT, "orbit.qmap")
    text = open(path, encoding="utf-8").read().replace("\nmodes 1v1 duel vs tourney ffa\n", "\nmodes 1v1 duel vs tourney\n")
    with open(path, "w", newline="\n") as f: f.write(text)
