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
(DOUBLE SIZE); girders on the ceiling every 6 m and the catwalk rails are the hooks.

Fix round (1.19.6 review, q's answers): the ceiling over the flip deck is flush (the girder lines and the flip's
warning bands are strips of the ceiling, since pmove has no step for a flipped walker) and the flip column is 0.5 m
inside the ceiling's flat (x +-7.5, z +-9.5), so a ceiling runner leaves the column at its edge and falls to the floor;
the core stands on plain floor (inside it you drift down at half gravity, both ways round: up only by the grapple);
the spawns face out along the floor lanes; coolant tanks cut the bay-to-bay line down each lane; ribs, pipes, catwalk
strips, two more ceiling barriers and a round planet dress it; the warm-up spawns are behind cover.
Known: bots and the walk check treat the flip tiles as floor (no code outside pmove reads FIELD_FLIP) and the camera
doesn't turn over on the ceiling; both are engine work, not map work."""
import math, os
from .prefabs import M, minus
from . import prefabs
from .bigkit import Kit, num, yaw_to

HX, HZ = 24.0, 16.0
TOP = 18.0
FLIP = (-7.5, -9.5, 7.5, 9.5)   # (0.5 m inside the ceiling's flat, |z| 10: a walker's hull leaves the column and falls)
CORE = 4.0
CAT_Y = 6.0
ANY = [(-18.0, 12.0, -24, 12.0), (-22.0, -10.0, -24, -4), (-2.0, -14.5, -10, -14.5), (-20.0, -14.5, -24, -9)]


def build():
    m = M("Orbit", "1v1", sun=(0.6, 0.7, -0.4))
    m.also = ["duel", "vs", "tourney"]
    k = Kit(m)
    m.raw("palette base 0.78 0.80 0.84 accent 0.22 0.36 0.62 metal 0.28 0.30 0.34 glow 0.40 0.85 1.00 sky 0 0 0")

    # ---- the hull: floor, long walls to 12 with chamfers up to the ceiling's 20 m wide flat, end walls, windows
    fx0, fz0, fx1, fz1 = FLIP
    k.box("steeldeckplate", -HX, -1, -HZ, HX, 0, fz0); k.box("steeldeckplate", -HX, -1, fz1, HX, 0, HZ)
    k.box("steeldeckplate", -HX, -1, fz0, fx0, 0, fz1); k.box("steeldeckplate", fx1, -1, fz0, HX, 0, fz1)
    # the flip tiles: a frame round the core (the core stands on plain floor, so inside it you float down at half
    # gravity: a ceiling walker who steps in drifts to the floor)
    for (a, b, c, d) in minus((fx0, fz0, fx1, fz1), [(-CORE, -CORE, CORE, CORE)]):
        k.box("gravityflip", a, -1, b, c, 0, d)
    k.box("steeldeckplate", -CORE, -1, -CORE, CORE, 0, CORE)
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
    # the ceiling (a floor upside down). Over the flip deck it is one flat plane: the girder lines at x -9, -3, 3, 9 and
    # the warning bands round the flip's edge are strips of the ceiling itself, flush (pmove's flipped branch has no
    # step, so anything proud of the ceiling is a wall to a ceiling walker). Real 0.4 m girders only over the bays
    # (|x| 15, 21: outside the flip footprint).
    xs = [-HX - 1, -9.25, -8.75, -3.25, -2.75, 2.75, 3.25, 8.75, 9.25, HX + 1]
    for i in range(len(xs) - 1):
        mat = "anodizedblack" if i in (1, 3, 5, 7) else "hexboltgrid"
        k.box(mat, xs[i], TOP, -9.5, xs[i + 1], TOP + 0.5, 9.5)
    for sz in (-1, 1):
        za, zb = sorted((sz * 9.5, sz * 10))
        k.box("warninglights", -HX - 1, TOP, za, HX + 1, TOP + 0.5, zb)
    for x in (-21, -15, 15, 21):
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
    # two more low barriers on the ceiling (1.2 m, hanging), point symmetric
    for (x, z) in ((-6, -1.5), (6, 1.5)):
        k.box("aluminiumtreadplate", x - 0.6, TOP - 1.2, z - 1.5, x + 0.6, TOP, z + 1.5)
    # crate stacks at the deck's ends (floor and ceiling): no line bay to bay down the middle
    for sx in (-1, 1):
        x = sx * 11
        k.box("aluminiumtreadplate", x - 1.5, 0, -2.5, x + 1.5, 3.0, 2.5)
        k.box("aluminiumtreadplate", x - 1.0, 3.0, -1.5, x + 1.0, 4.4, 1.5)
        k.box("aluminiumtreadplate", x - 1.5, TOP - 3.0, -2.5, x + 1.5, TOP, 2.5)

    # coolant tanks in the floor lanes (point symmetric): no clear line bay to bay along a lane, 3 m free beside
    for (x, z) in ((-4.5, -11.8), (4.5, 11.8)):
        k.disc("electroplatednickel", x, z, 1.0, 0, 3.2, n=8)
        k.disc("powdercoatyellow", x, z, 1.05, 1.4, 1.6, n=8)
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
        tz = -11 if z < 0 else 11                                        # (facing out along a floor lane: you come out moving)
        k.spawn("a", x, 0, z, yaw_to(x, z, -10, tz))
        k.spawn("b", -x, 0, -z, yaw_to(-x, -z, 10, -tz))
    # tourney warm-up / any: behind cover, facing walls or the bay doors
    for (x, z, tx, tz) in ANY:
        k.spawn("any", x, 0, z, yaw_to(x, z, tx, tz))
        k.spawn("any", -x, 0, -z, yaw_to(-x, -z, -tx, -tz))

    # ---- outside: a planet in the windows (unlit scenery, far off)
    # (the planet: a disc of far boxes in rows, so it reads round in the windows)
    R, cy = 46.0, -14.0
    for i in range(12):
        y0 = cy - R + 2 * R * i / 12; y1 = cy - R + 2 * R * (i + 1) / 12
        h = max(abs(y0 - cy), abs(y1 - cy)) if (y0 - cy) * (y1 - cy) > 0 else 0.0
        w = math.sqrt(max(0.0, R * R - h * h))
        k.box("far", -w, y0, -140, w, y1, -120)
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
    # hull ribs every 6 m up the long walls and over the chamfers (0.12 m proud, between the windows), a pipe run at
    # the chamfer's foot, a hazard edge and a lamp strip along each catwalk
    for sz in (-1, 1):
        za, zb = sorted((sz * (HZ - 0.12), sz * HZ))
        for x in (-21, -15, -9, -3, 3, 9, 15, 21):
            k.box("anodizedblack", x - 0.2, 0, za, x + 0.2, 12, zb)
            k.wedge("anodizedblack", 2 if sz > 0 else 3, x - 0.2, 11.83, min(sz * 9.83, sz * (HZ + 0.8)), x + 0.2, TOP + 0.33, max(sz * 9.83, sz * (HZ + 0.8)), up=1)
        k.tube_x("oxidizedsteel", -HX, HX, 12.35, sz * 15.55, 0.25)
        ha, hb = sorted((sz * 13.6, sz * 13.85))
        k.box("hazard", -12, CAT_Y, ha, 12, CAT_Y + 0.02, hb)
        la, lb = sorted((sz * 13.6, sz * 13.75))
        k.box("lightblock", -12, CAT_Y - 0.36, la, 12, CAT_Y - 0.3, lb)
    m.raw("setting space station centrifuge")
    m.save("orbit", dress=False)
    # (one chamber: no free for all, whatever save() adds)
    path = os.path.join(prefabs.OUT, "orbit.qmap")
    text = open(path, encoding="utf-8").read().replace("\nmodes 1v1 duel vs tourney ffa\n", "\nmodes 1v1 duel vs tourney\n")
    with open(path, "w", newline="\n") as f: f.write(text)
