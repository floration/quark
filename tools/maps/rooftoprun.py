"""ROOFTOP RUN (1.19.6): a one-way night race over a city, about 1.3 km of course (a minute at full speed), 12
checkpoints, in six sections:
  1 the start: a rooftop bar at 31 m and a sprint over three more roofs (5-6 m gaps, a plank beside each);
  2 the crane yard: a scaffold stair up to a tower crane's slewing platform at 60 m, along its 2 m jib, a drop down
    through a half-built tower's floors (holes at alternate ends) to 20 m, and a crumbling plank bridge (it falls 1 s
    after you land: a slow runner drops to the catwalk under it and climbs a ladder back up, +2 s);
  3 the neon district: a canyon of signs between tall blocks, run on shop balconies that zig-zag down it from 19 to
    15 m (sign gantries at 26 m over the middle are a grapple line that saves about 1.5 s), onto a covered market's
    roof (its sugar-glass skylights break as you touch them: a drop into the hall, out of its east door);
  4 the subway dive: down a long straight station stair from 8 to -18 m, along the platform, onto the tracks and
    through a 7 m tunnel with a stopped train you can run along the roof of;
  5 the climb: a ventilation shaft (two ladders and a hatched landing) and a fire escape up a facade to 36 m (open
    hatches above each other: a grapple line saves about 2 s);
  6 the finish: four roofs and a plank bridge up to a water tower's deck at 40 m.
The streets are 58 m below the start and you die 1 m above them (void -27): a miss sends you back to your last
checkpoint. Off-course scenery is `far` (not solid). Clip walls the site and lids the neon canyon at 30 m."""
import random
from .prefabs import M, minus
from .bigkit import Kit, num, MAT

STREET = -28.0
VOID = -27.0
LID = 110.0
AWN = "stripedwallpaper"   # (the awnings)


def build():
    m = M("Rooftop Run", "race", sun=(0.35, 0.55, -0.4))
    m.no_ffa = True
    m.raw("setting night city rooftops")
    m.raw("palette base 0.2 0.21 0.26 accent 0.62 0.24 0.5 metal 0.3 0.31 0.36 glow 1.0 0.62 0.3 sky 0.035 0.04 0.085")
    m.raw(f"void {num(VOID)}")
    k = Kit(m)
    R = random.Random(1196)

    def block(x0, z0, x1, z1, top, roof="concreterooftiles", facade="darkconcrete", parapet=()):
        """a course building: its facade down to the street, a roof slab; parapet: sides (n s w e) with a 0.8 m wall"""
        k.box(facade, x0, STREET, z0, x1, top - 0.35, z1)
        k.box(roof, x0, top - 0.35, z0, x1, top, z1)
        for sd in parapet:
            if sd == "n": k.box(facade, x0, top, z0, x1, top + 0.8, z0 + 0.3)
            if sd == "s": k.box(facade, x0, top, z1 - 0.3, x1, top + 0.8, z1)
            if sd == "w": k.box(facade, x0, top, z0, x0 + 0.3, top + 0.8, z1)
            if sd == "e": k.box(facade, x1 - 0.3, top, z0, x1, top + 0.8, z1)

    def windows(x0, z0, x1, z1, y0, y1, side, mat="glow", step=4.0, seed=0):
        """lit window strips on a facade (trim-thin glow, flush): side n/s/w/e is the face"""
        r = random.Random(seed)
        y = y0 + 2
        while y < y1 - 1.5:
            a = (x0 if side in "ns" else z0) + 1.5
            b = (x1 if side in "ns" else z1) - 1.5
            while a < b - 1.5:
                if r.random() < 0.55:
                    w = min(b, a + 1.6)
                    if side == "n": k.box(mat, a, y, z0 - 0.04, w, y + 1.1, z0)
                    if side == "s": k.box(mat, a, y, z1, w, y + 1.1, z1 + 0.04)
                    if side == "w": k.box(mat, x0 - 0.04, y, a, x0, y + 1.1, w)
                    if side == "e": k.box(mat, x1, y, a, x1 + 0.04, y + 1.1, w)
                a += step
            y += 3.5

    def cp(x0, y, z0, x1, z1, h=4.0):
        m.raw(f"checkpoint {num(x0)} {num(y)} {num(z0)} {num(x1)} {num(y + h)} {num(z1)}")

    def plank(x0, z0, x1, z1, y, mat="scaffoldboards"):
        k.box(mat, x0, y - 0.15, z0, x1, y, z1)

    # ================= 1. the start: a rooftop bar and three roofs (y 30 / 28) =================
    block(-338, -152, -298, -128, 31, roof="rainwetpaving", facade="londonstock", parapet="nsw")   # the bar's roof
    for i in range(8): k.spawn("race", -332, 31, -145.25 + 1.5 * i, -90)
    k.box("cratewood", -337.7, 31, -150, -336.5, 32.1, -130)               # the bar counter along the back wall
    k.box("lightblock", -337.6, 32.1, -149, -336.6, 32.2, -131)
    for (z0, z1) in ((-151.7, -150.9), (-129.1, -128.3)): k.box("lightblock", -336, 31.9, z0, -300, 32.0, z1)   # string lights
    k.light(-318, 34, -140, 22, 0.9)
    k.ramp("scaffoldboards", -298, -151.5, -292, -150.3, 29.85, 31, "-x")  # the plank over gap 0
    block(-292, -152, -250, -128, 30, roof="concreterooftiles", facade="darkconcrete", parapet="ns")
    for (x, z) in ((-270, -149.2), (-258, -149.2), (-270, -130.8), (-258, -130.8)):   # umbrellas over the tables along the parapets
        k.box("galvanizedsteel", x - 0.1, 30, z - 0.1, x + 0.1, 32.6, z + 0.1)
        k.box(AWN, x - 1.5, 32.6, z - 1.2, x + 1.5, 32.8, z + 1.2)
    for (z0, z1) in ((-151.7, -150.9), (-129.1, -128.3)):                   # string lights along the parapets
        k.box("lightblock", -290, 30.9, z0, -252, 31.0, z1)   # (more along roof 1's parapets)
    k.light(-272, 33, -140, 22, 0.9)
    k.ramp("scaffoldboards", -250, -151.5, -245, -150.3, 27.8, 30, "-x")   # the plank over gap 1 (down to roof 2)
    block(-245, -150, -205, -130, 28, roof="birdspottedroof", facade="castinplacewalls", parapet="ns")
    k.box("ventilationduct", -235, 28, -149.7, -229, 30, -147.2)           # roof plant on the edges, the lane clear
    k.box("ventilationduct", -218, 28, -132.8, -212, 29.6, -130.3)
    plank(-205, -149, -199, -147.8, 28)                                    # the plank over gap 2
    block(-199, -150, -166, -130, 28, roof="leadsheet", facade="londonstock", parapet="n")
    cp(-199, 28, -150, -195, -130)                                         # 1
    k.light(-225, 31.5, -140, 22, 0.8); k.light(-183, 31.5, -140, 20, 0.8)

    # ================= 2. the crane yard =================
    # the scaffold stair: three flights (lanes z -144..-140.5 and -147.5..-144), 28 -> 60
    k.stairs("largescaffoldboards", -166, -144, -150, -140.5, 28 + 32 / 3, "x", 1, base=28, solid=False)
    k.box("galvanizedgrating", -150, 28 + 32 / 3 - 0.3, -147.5, -146, 28 + 32 / 3, -140.5)
    k.stairs("largescaffoldboards", -166, -147.5, -150, -144, 28 + 64 / 3, "x", -1, base=28 + 32 / 3, solid=False)
    k.box("galvanizedgrating", -170, 28 + 64 / 3 - 0.3, -147.5, -166, 28 + 64 / 3, -140.5)
    k.stairs("largescaffoldboards", -166, -144, -150, -140.5, 60, "x", 1, base=28 + 64 / 3, solid=False)
    for x in (-170.2, -146.2):                                             # the scaffold's corner standards
        for z in (-148.2, -140.3):
            k.box("galvanizedsteel", x, STREET, z, x + 0.2, 61.2, z + 0.2)
    for y in range(32, 61, 6):                                             # ledgers and toe boards on the outer faces only
        k.box("powdercoatyellow", -170.2, y, -148.2, -146, y + 0.12, -148.0)
        k.box("powdercoatyellow", -170.2, y, -140.3, -146, y + 0.12, -140.1)
    k.box("hazard", -170, 28 + 64 / 3, -147.7, -169.8, 28 + 64 / 3 + 1, -140.3)   # guard rail on the west landing
    # the tower crane: mast, slewing platform at 60 (checkpoint 2), jib north to z -46, counter-jib south
    for (x, z) in ((-145, -145), (-141.4, -145), (-145, -141.4), (-141.4, -141.4)):
        k.box("powdercoatyellow", x, STREET, z, x + 0.4, 59.7, z + 0.4)
    for y in range(-20, 59, 8): k.box("powdercoatyellow", -145, y, -145, -141, y + 0.3, -141)
    k.box("steeldeckplate", -150, 59.6, -148, -139, 60, -138)
    k.box("powdercoatyellow", -139, 60, -146, -136, 63, -142)                # the operator's cab (beside the lane)
    k.box("glow", -136.04, 61.4, -145.5, -136, 62.6, -142.5)
    cp(-150, 60, -148, -139, -138)                                         # 2
    k.box("galvanizedgrating", -144, 59.7, -138, -142, 60, -46)             # the jib walk (2 m)
    k.box("bridgegirder", -144, 57.6, -138, -143.8, 59.7, -46); k.box("bridgegirder", -142.2, 57.6, -138, -142, 59.7, -46)
    for z in range(-136, -46, 6): k.box("bridgegirder", -144, 57.4, z, -142, 57.6, z + 0.3)
    k.box("powdercoatyellow", -143.3, 60, -147.9, -142.7, 72, -147.3)       # the apex over the counter-jib (behind the lane)
    k.box("bridgegirder", -144.6, 59, -160, -141.4, 60, -148)               # counter-jib and its weights
    k.box("weatheredconcrete", -145.2, 55, -160, -140.8, 59, -156)
    k.box("castiron", -143.4, 38, -92.4, -142.6, 57.4, -92.3)               # the hook's cable and block (a grapple anchor)
    k.box("powdercoatyellow", -144.2, 36, -93.2, -141.8, 38, -91.6)
    m.lines.append("light -143 62 -100 26 0.7"); m.lines.append("light -143 62 -60 26 0.7")
    # the half-built tower: floors 8 m apart (a bot drops at most 8 m) with a hole at alternate ends; you drop down through it
    TX0, TZ0, TX1, TZ1 = -160, -62, -126, -30
    holes = {52: (-138, -46, -126, -30), 44: (-160, -46, -148, -30), 36: (-138, -46, -126, -30), 28: (-160, -46, -148, -30)}
    for y in (52, 44, 36, 28, 20):
        for (a, b, c, d) in minus((TX0, TZ0, TX1, TZ1), [holes[y]] if y in holes else []):
            k.box("freshgreyconcrete", a, y - 0.6, b, c, y, d)
    k.box("castinplacewalls", TX0, STREET, TZ0, TX1, 19.4, TZ1)              # the finished lower storeys
    for (x, z) in ((TX0, TZ0), (TX1 - 0.8, TZ0), (TX0, TZ1 - 0.8), (TX1 - 0.8, TZ1 - 0.8), (TX0, -46.4), (TX1 - 0.8, -46.4)):
        k.box("castinplacewalls", x, 20, z, x + 0.8, 51.4, z + 0.8)          # columns on the edges only
    for (x, z) in ((TX0 + 0.2, TZ0 + 0.2), (TX1 - 0.6, TZ0 + 0.2), (TX0 + 0.2, TZ1 - 0.6)):
        k.box("rust", x, 52, z, x + 0.12, 53.4, z + 0.12)                    # rebar stubs on the top corners
    for y in (24, 32, 40, 48): m.lines.append(f"light -143 {y:g} -46 18 0.6")
    cp(-160, 20, -46, -148, -30)                                           # 3
    # the crumbling plank bridge north to the canyon head (z -30 -> -12), the catwalk under it, the ladder back up
    k.box("crumbleplatform", -136, 19.85, -30, -131, 20, -12)
    k.box("galvanizedgrating", -136, 13.7, -30, -126, 14, -12)
    k.box("hazard", -136, 14, -30.1, -126, 15, -30)
    k.ladder(-128, -12, 14, 20, "s")
    m.lines.append("light -130 18 -21 12 0.6")

    # ================= 3. the neon district (the canyon: street z -6..6 between tall blocks) =================
    block(-140, -12, -118, 4, 20, roof="concreterooftiles", facade="londonstock", parapet="w")
    cp(-140, 20, -12, -118, 4)                                             # 4
    k.box("clip", -118, 30, -30, 40, LID, 30)                              # the canyon's lid: nobody over the blocks
    # the blocks either side, 34-44 m tall
    x = -118
    for i, w in enumerate((30, 26, 34, 28, 40)):
        for (z0, z1, side) in ((-30, -6, "s"), (6, 30, "n")):
            top = 34 + (i * 7 + (0 if side == "s" else 4)) % 11
            fac = ("darkconcrete", "graffitibuffedwall", "londonstock", "fibrecementboard", "castinplacewalls")[(i + (side == "n")) % 5]
            k.box(fac, x, STREET, z0, min(x + w, 40), top, z1)
            windows(x, z0, min(x + w, 40), z1, 21, top, side, seed=i * 2 + (side == "n"))
        x += w
    # the shop balconies, alternating sides, stepping down; each joined to the next by a sign-frame bridge
    BAL = [("s", -118, -96, 19), ("n", -98, -74, 18), ("s", -76, -52, 17), ("n", -54, -30, 16.5),
           ("s", -32, -8, 16), ("n", -10, 14, 15.5), ("s", 12, 40, 15)]
    for i, (sd, x0, x1, top) in enumerate(BAL):
        z0, z1 = (-6, -2) if sd == "s" else (2, 6)
        k.box("rainwetpaving", x0, top - 0.4, z0, x1, top, z1)
        k.box("castiron", x0, top - 1.2, z0 if sd == "n" else z1 - 0.2, x1, top - 0.4, z0 + 0.2 if sd == "n" else z1)   # fascia
        k.box("glow", x0 + 1, top - 1.1, (z0 - 0.03) if sd == "n" else z1, x1 - 1, top - 0.5, z0 if sd == "n" else z1 + 0.03)
        fz = -6 if sd == "s" else 6                                         # the shopfront under it
        k.box("lightblock", x0 + 2, top - 4.5, fz - 0.05 if sd == "n" else fz, x1 - 2, top - 1.6, fz if sd == "n" else fz + 0.05)
        if i + 1 < len(BAL):
            bx0 = BAL[i + 1][1]
            k.box("galvanizedgrating", bx0, top - 0.3, -2, bx0 + 2, top, 2)   # the bridge at this balcony's height
        m.lines.append(f"light {num((x0 + x1) / 2)} {num(top + 3)} {num((z0 + z1) / 2)} 16 0.7")
    NEON = ["neonmaze", "hologrampanel", "lightblock"]
    for j, sx in enumerate((-100, -76, -52, -28, -4, 20)):                   # the sign gantries over the middle (grapple line)
        k.box("castiron", sx - 0.3, 25.6, -6, sx + 0.3, 26.2, 6)
        k.box(NEON[j % 2], sx - 0.06, 22.6, -3, sx + 0.06, 25.6, 3)            # a hanging sign (thin: shoot-through)
    for j in range(14):                                                    # small neon panels on the facades
        sx = -112 + j * 11 + R.uniform(-2, 2); sd = (-6, 6)[j % 2]; y = R.uniform(21, 27)
        mat = NEON[j % 3]
        if sd < 0: k.box(mat, sx, y, -6, sx + 2.2, y + 3.4, -5.9)
        else: k.box(mat, sx, y, 5.9, sx + 2.2, y + 3.4, 6)
    # the covered market (hall floor 8, roof 14, three sugar-glass skylights), the forecourt east of it
    MX0, MX1 = 40, 104
    k.box("castinplacewalls", MX0, STREET, -30, MX1, 7.6, 30)
    k.box("terrazzo", MX0, 7.6, -30, MX1, 8, 30)
    sky = [(52, -4, 60, 4), (68, -4, 76, 4), (84, -4, 92, 4)]
    for (a, b, c, d) in minus((MX0, -30, MX1, 30), sky): k.box("corrugated", a, 13.6, b, c, 14, d)
    for (a, b, c, d) in sky: k.box("sugarglass", a, 13.7, b, c, 13.9, d)
    for (z0, z1) in ((-30, -29.6), (29.6, 30)): k.box("londonstock", MX0, 8, z0, MX1, 13.6, z1)
    k.box("londonstock", MX0, 8, -29.6, MX0 + 0.4, 13.6, 29.6)
    for (a, b) in ((-29.6, -3), (3, 29.6)): k.box("londonstock", MX1 - 0.4, 8, a, MX1, 13.6, b)   # the east door (6 m)
    k.box("londonstock", MX1 - 0.4, 12.6, -3, MX1, 13.6, 3)
    for x0 in range(46, 100, 9):                                            # stalls along the walls, the aisle clear
        for (z0, z1) in ((-29.6, -24), (24, 29.6)):
            k.box("cratewood", x0, 8, z0, x0 + 6, 9.1, z1)
            k.box(AWN, x0, 10.8, z0, x0 + 6, 11, z1)
    for x0 in (48, 64, 80, 96): k.box("lightblock", x0, 13.3, -1, x0 + 1, 13.6, 1)
    for x0 in (56, 72, 88): m.lines.append(f"light {x0} 11.5 0 14 0.8")
    cp(MX0, 14, -30, MX0 + 4, 30)                                          # 5 (onto the market roof)
    block(104, -30, 124, 30, 8, roof="concretepavers", facade="castinplacewalls", parapet="ns")   # the forecourt

    # ================= 4. the subway dive =================
    # the entrance stair: straight east, 8 -> -5 -> -18 (lane z -3..3)
    k.box("concretepavers", 124, 7.65, -30, 140, 8, -3); k.box("concretepavers", 124, 7.65, 3, 140, 8, 30)
    k.stairs("tactilepaving", 124, -3, 150, 3, 8, "x", -1, base=-5, solid=False)
    k.box("subwaytile", 150, -5.4, -3, 154, -5, 3)
    k.stairs("tactilepaving", 154, -3, 180, 3, -5, "x", -1, base=-18, solid=False)
    cp(118, 8, -3, 124, 3)                                                 # 6 (the top of the stair)
    for (z0, z1) in ((-3.4, -3), (3, 3.4)):                                # the stair's walls and its roof steps
        k.box("subwaytile", 124, -19, z0, 180, 14, z1)
    k.box("whiteconcrete", 124, 13.6, -3.4, 150, 14, 3.4)
    k.box("whiteconcrete", 150, 3, -3.4, 165, 3.4, 3.4)
    k.box("whiteconcrete", 165, -8, -3.4, 180, -7.6, 3.4)
    k.box("whiteconcrete", 149.6, 3.4, -3.4, 150, 13.6, 3.4); k.box("whiteconcrete", 164.6, -7.6, -3.4, 165, 3, 3.4)   # (the roof steps closed)
    for x0 in (128, 140, 158, 172): k.box("lightblock", x0, (13.3 if x0 < 150 else 3 - 0.3 if x0 < 165 else -8.3), -1, x0 + 2, (13.6 if x0 < 150 else 3 if x0 < 165 else -8), 1)
    for (x, y) in ((136, 9), (158, -1), (172, -12)): m.lines.append(f"light {x} {y} 0 12 0.8")
    # the station: concourse x 180..190, platform north, the tracks (x 190..197) and the tunnel to z 130
    k.box("subwaytile", 180, -19, -10, 190, -18, 60)                       # concourse + platform floor
    k.box("tactilepaving", 189.4, -18.02, -10, 190, -18, 60)
    k.box("railtrack", 190, -20, -10, 197, -19, 137)                       # the track bed
    for z0 in range(-8, 136, 3): k.box("railwaysleepers", 191, -19, z0, 196, -18.96, z0 + 0.4)   # (4 cm: a checkpoint respawn at +0.05 stands on them)
    k.box("greensubwaytile", 179.6, -20, -10.4, 197.4, -10, -10)           # station walls: south, west, east
    k.box("greensubwaytile", 179.6, -20, -10, 180, -10, -3.4); k.box("greensubwaytile", 179.6, -20, 3.4, 180, -10, 60)
    k.box("greensubwaytile", 197, -20, -10, 197.4, -10, 137)
    k.box("whiteconcrete", 180, -10, -10.4, 197.4, -9.6, 60)               # the station ceiling (the stair cuts in: z -3..3, x 180)
    k.box("greensubwaytile", 179.6, -18, 60, 190, -10, 60.4)               # the platform's north end wall
    k.box("tunnellining", 190, -11, 60, 197, -10.6, 130)                   # tunnel ceiling (no grapple through it: a roof)
    k.box("tunnellining", 189.6, -20, 60, 190, -10.6, 137)                 # tunnel west wall
    for z0 in range(-6, 58, 8): k.box("lightblock", 184, -10.3, z0, 186, -10, z0 + 0.8)
    for z0 in range(64, 136, 8): k.box("lightblock", 196.9, -13, z0, 197, -12.6, z0 + 2)   # strip lights on the tunnel wall
    for z in (5, 30, 50): m.lines.append(f"light 186 -12 {z} 16 0.8")
    for z in (75, 100, 125): m.lines.append(f"light 193.5 -12 {z} 12 0.6")
    cp(180, -18, -10, 190, 2)                                              # 7 (the platform)
    # the stopped train (x 191.5..195.5, roof at -15.5): 1.5 m beside it each side
    k.box("aircraftskin", 191.5, -19, 70, 195.5, -15.5, 122)
    for z0 in (82, 96, 110): k.box("darkconcrete", 191.5, -15.5, z0, 195.5, -15.45, z0 + 0.4)   # car joints (flush)
    k.box("glow", 191.46, -17.6, 72, 191.5, -16.4, 120); k.box("glow", 195.5, -17.6, 72, 195.54, -16.4, 120)
    k.box("glow", 192, -18, 69.96, 195, -17, 70)
    cp(190, -19, 124, 197, 128)                                            # 8 (past the train)

    # ================= 5. the climb: the vent shaft (x 190..197, z 130..137) and the fire escape =================
    k.box("ventilationduct", 189.6, -20, 137, 197.4, 8, 137.4)             # shaft north wall
    k.box("ventilationduct", 189.6, -10.6, 130, 190, 8, 137)
    k.box("ventilationduct", 197, -10.6, 130, 197.4, 8, 137)
    k.box("ventilationduct", 190, -10.6, 129.6, 197, 8, 130)
    k.box("ventilationduct", 190, -19, 135.5, 197, -5.5, 137)              # the landing: a fan housing's top (z 135.5..137)
    k.box("steeldeckplate", 190, -5.5, 135.5, 197, -5.45, 137)
    k.ladder(192.5, 135.5, -19, -5.5, "s")
    k.ladder(195.5, 137, -5.5, 8, "s")
    k.box("radiatorfins", 190, -2, 130.2, 190.3, 3, 133)                   # the fan cage (on the wall, off the line)
    k.box("castiron", 190, -2.2, 129.8, 190.5, 3.2, 130.2)
    m.lines.append("light 193.5 -10 133 10 0.7"); m.lines.append("light 193.5 4 133 10 0.7")
    cp(190, -5.45, 135.5, 197, 137)                                        # 9 (the landing)
    for (a, b, c, d) in minus((180, 126, 204, 140), [(190, 130, 197, 137)]):   # the low roof round the shaft top
        k.box("leadsheet", a, 7.6, b, c, 8, d)
    k.box("castinplacewalls", 180, STREET, 126, 189.6, 7.6, 130); k.box("castinplacewalls", 197.4, STREET, 126, 204, 7.6, 130)
    k.box("castinplacewalls", 189.6, -10.6, 126, 197.4, 7.6, 129.6)        # (over the tunnel mouth)
    k.box("castinplacewalls", 180, STREET, 130, 190, 7.6, 140); k.box("castinplacewalls", 197, STREET, 130, 204, 7.6, 140)
    # the fire escape on block B5's south face (z 140): ladder 8 -> 20, a landing, ladder 20 -> 36
    block(176, 140, 206, 166, 36, roof="concreterooftiles", facade="londonstock", parapet="se")
    windows(176, 140, 206, 166, 8, 36, "n", seed=21)
    k.box("londonstock", 189.8, 8, 138.2, 193.2, 20, 140)                   # the fire escape's first stage: a chimney breast
    for (a, b, c, d) in minus((186, 136.6, 198, 140), [(190.3, 136.6, 192.7, 138.2), (189.8, 138.2, 193.2, 140)]):
        k.box("castiron", a, 19.7, b, c, 20, d)                             # the landing (open over ladder 1)
    k.box("castiron", 186, 20, 136.6, 190.3, 21, 136.75); k.box("castiron", 192.7, 20, 136.6, 198, 21, 136.75)   # its rail
    k.ladder(191.5, 138.2, 8, 20, "s", mat="castiron")
    k.ladder(196, 140, 20, 36, "s", mat="castiron")
    k.box("castiron", 186, 31.7, 138.6, 189, 32, 140)                       # an upper landing stub (a grapple anchor)
    cp(186, 20, 136.6, 190, 140)                                           # 10 (the fire escape landing)
    m.lines.append("light 192 24 138 12 0.7")

    # ================= 6. the finish: three roofs west and a plank bridge to the water tower =================
    k.light(190, 39.5, 153, 18, 0.7)
    k.ramp("scaffoldboards", 170, 151, 176, 153, 35.85, 37, "-x")         # the plank up onto roof 2
    block(126, 144, 170, 162, 37, roof="birdspottedroof", facade="darkconcrete", parapet="ns")
    k.box("ventilationduct", 140, 37, 144.3, 146, 39, 147)                 # roof plant on the edges
    k.box("ventilationduct", 156, 37, 159.3, 160, 38.4, 161.7)
    k.light(148, 40, 153, 18, 0.7)
    cp(126, 37, 144, 130, 162)                                             # 11
    k.ramp("scaffoldboards", 120, 152, 126, 154, 36.85, 38.5, "-x")
    block(76, 146, 120, 160, 38.5, roof="concreterooftiles", facade="londonstock", parapet="ns")
    k.light(98, 41.5, 153, 18, 0.7)
    k.ramp("scaffoldboards", 70, 152, 76, 154, 38.35, 39, "-x")
    block(26, 146, 70, 160, 39, roof="leadsheet", facade="castinplacewalls", parapet="ns")
    k.box("ventilationduct", 40, 39, 146.3, 46, 40.6, 148.6)
    k.light(48, 42, 153, 18, 0.7)
    k.ramp("scaffoldboards", -6, 152, 26, 154, 38.85, 40, "-x")              # the last plank, rising to the deck
    # the water tower: legs, a deck ring at 40, the tank in the middle
    for (x, z) in ((-20, 146), (-6.6, 146), (-20, 159.4), (-6.6, 159.4)):
        k.box("castiron", x, STREET, z, x + 0.6, 39.6, z + 0.6)
    k.box("wideplanks", -22, 39.6, 144, -6, 40, 162)
    k.box("logcabin", -18, 40, 148, -10, 49, 158)
    k.box("castiron", -18.4, 49, 147.6, -9.6, 50.2, 158.4)
    k.box("castiron", -22, 40, 144, -6, 41, 144.1); k.box("castiron", -22, 40, 161.9, -6, 41, 162)
    k.box("castiron", -22.1, 40, 144, -22, 41, 162)
    cp(-10, 40, 144, -6, 162)                                               # 12: the finish
    k.box("lightblock", -6.2, 40, 144.2, -6, 43, 144.6); k.box("lightblock", -6.2, 40, 161.4, -6, 43, 161.8)
    k.box("glow", -6.1, 43, 144.2, -6, 43.3, 161.8)
    k.light(-14, 44, 153, 16, 0.9)

    # ================= the city round it: far scenery (not solid), the street, the site's clip =================
    X0, Z0, X1, Z1 = -346, -170, 214, 176
    k.box("far", X0 - 300, STREET - 1, Z0 - 300, X1 + 300, STREET, Z1 + 300)
    course = [(-338, -152, -166, -128), (-170, -160, -136, -138), (-145, -138, -141, -46), (-160, -62, -126, -12),
              (-140, -30, 40, 30), (40, -30, 124, 30), (124, -10, 204, 166), (-30, 140, 206, 166)]
    def free(x0, z0, x1, z1):
        return not any(x0 < c[2] + 6 and x1 > c[0] - 6 and z0 < c[3] + 6 and z1 > c[1] - 6 for c in course)
    n = 0
    for gx in range(int(X0) - 120, int(X1) + 120, 26):
        for gz in range(int(Z0) - 120, int(Z1) + 120, 26):
            w, d = R.uniform(12, 22), R.uniform(12, 22)
            x0, z0 = gx + R.uniform(0, 26 - w), gz + R.uniform(0, 26 - d)
            if not free(x0, z0, x0 + w, z0 + d): continue
            inside = X0 < x0 < X1 and Z0 < z0 < Z1
            top = R.uniform(-6, 16) if inside else R.uniform(10, 70)
            k.box("far", x0, STREET, z0, x0 + w, top, z0 + d); n += 1
    # the site: clip walls and a lid
    for (a, b, c, d) in ((X0 - 2, Z0 - 2, X0, Z1 + 2), (X1, Z0 - 2, X1 + 2, Z1 + 2), (X0, Z0 - 2, X1, Z0), (X0, Z1, X1, Z1 + 2)):
        k.box("clip", a, STREET, b, c, LID, d)
    k.box("clip", X0 - 2, LID, Z0 - 2, X1 + 2, LID + 2, Z1 + 2)
    m.save("rooftoprun", dress=False)
