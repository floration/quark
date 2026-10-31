"""MINE: bomb (one site: the TNT pile), also king of the hill (q, 2026-10-30: one site on mine; the hill is a pile of
crystals and metal bars). A deep mine on four levels, 4v4: the pithead at the surface (0, daylight: the attackers'
yard, a), the haulage level (-15: minecart galleries, the loading station, the old stope), the powder magazine cavern
and the ore store (-30: the site, the hill) and the deep workings (-45: lava channels and the crystal grotto; the
defenders' refuge, b, behind its steel screen). Bomb: T reach the TNT pile in about 5 s at 24 m/s (~120 m walked), CT
about 1 s later (~144 m). King of the hill has its own spawn set (koth-a / koth-b) and
the hill is in the ore bay (west of the cavern), four doors, two per team: CT's two open on its north side, T's two are in
the hill (its box runs into them), so T, nearer (~8 s to CT's ~12 s), holds from cover and CT walks into the open.
Everything underground is closed: the rock between the rooms is solid (fill_solid), so no hollow, hidden top or open
shaft is left for a player or the bots' nav graph.
Routes to the site, T: the cage shaft (drop or ladder, 0 -> -15 -> -30, the shaft-bottom drift into the cavern); the
haul road (the open-cut decline to -15, the haulage gallery, the ore chute onto the cavern's south ledge); the old stope
(the bench stairs to -15, rotten planks over the pit, the winze hole onto the west ledge). CT: the crystal grotto (its
stair up to the cavern's north door); the pump shaft (a 15 m ladder, the drainage adit to the north-west door); the
lava gallery (the ramp beside the magma channel to the ore store, the crosscut into the cavern's east door). The lava
runs only along that flank (the deep workings' east end and the gallery), which both teams use, so either side can
knock the other in; the spawn exits cross no lava."""
import math
from .prefabs import M, minus, tiles
from .m2kit import B, wedge, prism, octagon, light, sphere, solid_spans, MAT, fill_solid

ROCK, FLOOR, TIMBER = "wetcaverock", "gravel", "wideplanks"


def build():
    m = M("Mine", "bomb", sun=(0.4, 1.0, 0.3))
    m.also = ["koth"]
    doors = []      # door volumes: every wall they cross is opened there

    def opening(x0, y0, z0, x1, y1, z1): doors.append((min(x0, x1), min(y0, y1), min(z0, z1), max(x0, x1), max(y0, y1), max(z0, z1)))

    def slab_wall(mat, x0, y0, z0, x1, y1, z1):
        """an axis-aligned wall box less the door volumes that cross it (split along its run)"""
        along_x = (x1 - x0) >= (z1 - z0)
        hs = []
        for d in doors:
            if d[0] < x1 and d[3] > x0 and d[2] < z1 and d[5] > z0 and d[1] < y1 and d[4] > y0:
                hs.append((d[0], d[3], d[1], d[4]) if along_x else (d[2], d[5], d[1], d[4]))
        a0, a1 = (x0, x1) if along_x else (z0, z1)
        cuts = sorted({a0, a1} | {min(max(v, a0), a1) for h in hs for v in h[:2]})
        for (s, e) in zip(cuts, cuts[1:]):
            if e - s < 1e-4: continue
            mid = (s + e) / 2
            for (ya, yb) in solid_spans(y0, y1, [(h[2], h[3]) for h in hs if h[0] < mid < h[1]]):
                if along_x: B(m, mat, s, ya, z0, e, yb, z1)
                else: B(m, mat, x0, ya, s, x1, yb, e)

    rooms = []

    def room(name, x0, z0, x1, z1, yf, yc, floor=FLOOR, wall=ROCK, ceil=ROCK, fholes=(), choles=(), nofloor=False, noceil=False, walls="nsew"):
        rooms.append(dict(name=name, r=(x0, z0, x1, z1), yf=yf, yc=yc, floor=floor, wall=wall, ceil=ceil, fholes=list(fholes), choles=list(choles),
                          nofloor=nofloor, noceil=noceil, walls=walls))

    def build_rooms():
        for R in rooms:
            x0, z0, x1, z1 = R["r"]; yf, yc = R["yf"], R["yc"]
            if not R["nofloor"]:
                for t in tiles(minus((x0 - 0.5, z0 - 0.5, x1 + 0.5, z1 + 0.5), R["fholes"]), 16): B(m, R["floor"], t[0], yf - 1, t[1], t[2], yf, t[3])
            if not R["noceil"]:
                for t in tiles(minus((x0 - 0.5, z0 - 0.5, x1 + 0.5, z1 + 0.5), R["choles"]), 16): B(m, R["ceil"], t[0], yc, t[1], t[2], yc + 1, t[3])
            w, yb, yt = R["wall"], yf - 1, yc + (0 if R["noceil"] else 1)
            if "n" in R["walls"]: slab_wall(w, x0 - 1, yb, z0 - 1, x1 + 1, yt, z0)
            if "s" in R["walls"]: slab_wall(w, x0 - 1, yb, z1, x1 + 1, yt, z1 + 1)
            if "w" in R["walls"]: slab_wall(w, x0 - 1, yb, z0, x0, yt, z1)
            if "e" in R["walls"]: slab_wall(w, x1, yb, z0, x1 + 1, yt, z1)

    # ===================================== layout =====================================
    # ---- the surface (0): the pithead yard, open sky
    YZ0, YZ1 = 30, 66
    SHAFT = (-30, 32, -22, 40)                  # the cage shaft (0 -> -15)
    WINZE = (-28, 12, -22, 18)                  # its lower half (-15 -> -30)
    HAUL = (30, 38)                             # the haul road's open cut: x 30..38, from z 62 (0) down north to z 31 (-15)
    STOPE = (-44, -38)                          # the stope's bench stairs: x -44..-38, from z 50 (0) down north to z 31 (-15)
    room("yard", -50, YZ0, 50, YZ1, 0, 12, floor="dirt", wall="graniterock", noceil=True,
         fholes=[SHAFT, (HAUL[0], 31, HAUL[1], 62), (STOPE[0], 31, STOPE[1], 50)])
    # ---- the haulage level (-15)
    room("landing", -32, 10, -14, 42, -15, -9, fholes=[WINZE], choles=[SHAFT])
    room("shaftwalls", SHAFT[0], SHAFT[1], SHAFT[2], SHAFT[3], -9, 0, nofloor=True, noceil=True)
    room("haulgallery", -13, 17, 25, 24, -15, -9.5, floor="railwaysleepers", fholes=[(-4, 16.5, 4, 20)])
    room("loading", 26, 2, 42, 30, -15, -8, fholes=[(30, 2, 36, 6)])
    room("orechute", 30, 2, 36, 6, -18, -15, nofloor=True, noceil=True)
    room("haulcut", HAUL[0], 31, HAUL[1], 62, -15, 0, noceil=True, nofloor=True)
    room("stopecut", STOPE[0], 31, STOPE[1], 50, -15, 0, noceil=True, nofloor=True)
    room("stope", -50, -4, -34, 30, -15, -7, floor="scaffoldboards", fholes=[(-50, 10, -34, 16)])
    room("stopedrift", -33, -3, -17, 3, -15, -11, fholes=[(-21, -3.5, -16, 3.5)])
    room("chute", -4, 6, 4, 16, -24, -9.5, nofloor=True)
    # ---- the magazine level (-30)
    room("winzewalls", WINZE[0], WINZE[1], WINZE[2], WINZE[3], -24, -15, nofloor=True, noceil=True)
    room("shaftdrift", -29, -16, -21, 19, -30, -24, choles=[WINZE])
    room("pit", -50, 10, -34, 16, -30, -15, noceil=True)
    room("pitdrift", -33, 10, -30, 16, -30, -26)
    room("cavern", -20, -25, 20, 5, -30, -16, floor="slate", choles=[(-20, -2, -16, 2)])
    room("orestore", 21, -24, 46, 6, -30, -18, floor="slate", choles=[(30, 2, 36, 6)])
    room("adit", -46, -57, -38, -27, -30, -25)
    room("adit2", -37, -32, -6, -26, -30, -25)
    room("orebay", -46, -25, -30, -12, -30, -20, floor="slate")
    room("grottotunnel", -4, -34, 4, -26, -30, -25)
    room("baydrift", -42, -11, -38, 9, -30, -25)   # (the stope pit -> the ore bay: T's second door to the hill)
    room("baylink", -37, -3, -30, 3, -30, -25)     # (the shaft drift -> the bay drift: T's second way into the bay)
    # ---- the deep workings (-45)
    room("deep", -70, -86, 32, -67, -45, -36, floor="slate", fholes=[(0, -77, 32, -72)], choles=[(0, -77, 32.5, -72)])
    room("refuge", -68, -100, -52, -87, -45, -39, floor="castiron", wall="castiron", ceil="castiron")
    room("pumpshaft", -46, -66, -38, -58, -45, -25)   # (roofed at the adit's ceiling: the ladder leads only into the adit)
    room("grotto", -16, -66, 16, -35, -45, -24, floor="graniterock", wall="glowwormcave", ceil="glowwormcave")
    room("lavagallery", 21, -66, 33, -25, -45, -24, floor="emberbed", fholes=[(27, -66, 33, -25)], ceil="nograpple")

    # ---- doors (every wall in each volume is opened)
    opening(-4, -25, 16, 4, -9.5, 17)            # haul gallery -> chute (the chute's stairs start in the gallery)
    opening(-14.5, -15, 17, -12.5, -10.5, 24)    # landing -> haul gallery
    opening(24.5, -15, 17, 26.5, -10.5, 24)      # haul gallery -> loading
    opening(HAUL[0], -15, 29.5, HAUL[1], -9, 31.5)   # haul road -> loading
    opening(STOPE[0], -15, 29.5, STOPE[1], -9, 31.5)  # stope cut -> stope
    opening(-34.5, -15, -3, -32.5, -11, 3)       # stope -> its drift
    opening(-4, -24, 4.5, 4, -19.5, 6.5)         # chute -> the cavern's south ledge
    opening(-21.5, -30, -16, -19.5, -25, -10)    # shaft drift -> cavern (under the west ledge)
    opening(-34.5, -30, 10, -32.5, -26, 16)      # pit -> pit drift
    opening(-30.5, -30, 10, -28.5, -26, 16)      # pit drift -> shaft drift
    opening(19.5, -30, -18, 21.5, -25, -12)      # cavern -> ore store
    opening(21, -30, -25.5, 27, -25, -23.5)      # lava gallery -> ore store
    opening(21, -45, -67.5, 27, -40, -65.5)      # deep -> lava gallery
    opening(-15, -45, -67.5, -9, -40, -65.5)     # deep -> grotto (its west end: nearer the refuge)
    opening(-46, -45, -67.5, -38, -40, -65.5)    # deep -> pump shaft
    opening(-64, -45, -87.5, -56, -41, -85.5)    # refuge -> deep
    opening(-44, -30, -58.5, -40, -26, -56.5)    # pump shaft top -> adit
    opening(-38.5, -30, -32, -36.5, -25.5, -27)  # adit -> adit2
    opening(-11, -30, -26.5, -7, -25.5, -24.5)   # adit2 -> cavern (north-west door)
    opening(-4, -30, -35.5, 4, -25.5, -33.5)     # grotto -> its tunnel
    opening(-30.5, -30, -16, -28.5, -25, -12)    # ore bay -> shaft drift
    opening(-36, -30, -26.5, -32, -25.5, -24.5)  # adit2 -> ore bay
    opening(-44, -30, -27.5, -40, -25.5, -24.5)  # adit -> ore bay (CT's second door)
    opening(-42, -30, -12.5, -38, -25.5, -10.5)  # bay drift -> ore bay (T's second door)
    opening(-42, -30, 8.5, -38, -25.5, 10.5)     # stope pit -> bay drift
    opening(-38.5, -30, -3, -36.5, -25.5, 3)     # bay drift -> bay link
    opening(-30.5, -30, -3, -28.5, -25.5, 3)     # bay link -> shaft drift
    opening(-4, -30, -26.5, 4, -25.5, -24.5)     # grotto tunnel -> cavern (north door)
    build_rooms()

    # ---- vertical links
    # the cage shaft: ladders 0 <- -15 (south wall) and -15 <- -30 (the winze's north wall)
    m.ladder((SHAFT[0] + SHAFT[2]) / 2, SHAFT[3] - 0.4, -15, 0, "s")
    m.ladder((WINZE[0] + WINZE[2]) / 2, WINZE[1] + 0.4, -30, -15, "n")
    # the haul road: the decline (a ramp, 25 degrees) in its open cut
    wedge(m, "rutteddirtroad", "+z", HAUL[0], -15, 31, HAUL[1], 0, 62)
    # the stope's bench stairs
    m.stairs(STOPE[0], 31, STOPE[1], 50, 0, "z", 1, base=-15)
    # the ore chute: stairs from the haul gallery down onto the cavern's south ledge
    m.stairs(-4, 6, 4, 20, -15, "z", 1, base=-24)
    # the ore chute in the loading station: a drop into the ore store (a ladder back up)
    m.ladder(33, 5.6, -30, -15, "s")
    # the grotto's stair up to its tunnel and the cavern's north door
    m.stairs(-4, -58, 4, -35, -30, "z", 1, base=-45)
    # the lava gallery: the ledge, the ramp up to the ore store
    wedge(m, "graniterock", "+z", 21, -45, -62, 27, -30, -34); B(m, "graniterock", 21, -31, -34, 27, -30, -25)
    # the pump shaft's ladder (15 m) and the stope drift's winze ladder (from the west ledge up to -15)
    m.ladder(-42, -58.4, -45, -30, "s")
    m.ladder(-19.6, 0, -24, -15, "w")
    # the cavern's ledges (slabs on timber posts), with a ladder up to each
    B(m, TIMBER, -20, -24.4, -1, 20, -24, 5)
    for (z0, z1) in ((-25, -17), (-9, -1)): B(m, TIMBER, -20, -24.4, z0, -14, -24, z1)
    B(m, TIMBER, -16, -24.4, -17, -14, -24, -9)   # (a drop hole over the shaft drift's door: you can drop straight in front of it)
    for x in (-14, -6, 6, 14): B(m, TIMBER, x - 0.25, -30, -1.25, x + 0.25, -24.4, -0.75)
    for z in (-21, -7): B(m, TIMBER, -14.25, -30, z - 0.25, -13.75, -24.4, z + 0.25)
    B(m, TIMBER, 15.5, -30, -1.6, 16.5, -24.4, -1.0); m.ladder(16, -2.0, -30, -24, "s")
    # CT's own way up: a ladder onto the west ledge's north end, 4 m from the north-west door (T's ladder is at the
    # south ledge's east end)
    B(m, TIMBER, -14.0, -30, -24.0, -13.4, -24.4, -22.6); m.ladder(-13.0, -23.3, -30, -24, "w")
    # the stope: rotten planks across its pit (they give way under anyone who stops on them)
    for x0 in (-47, -39): B(m, "rottenplanks", x0, -15.3, 10, x0 + 2, -15, 16)
    # the lava: a magma river in the deep workings' channel (crossed by three bridges) and beside the lava gallery's ramp
    B(m, "magmariver", 0, -48, -77, 32, -46.2, -72); B(m, ROCK, 0, -49, -77, 32, -48, -72)
    B(m, "magmariver", 27, -48, -66, 33, -46.2, -25); B(m, ROCK, 27, -49, -66, 33, -48, -25)
    B(m, "nograpple", 0, -36, -77, 32.5, -35, -72)   # (nobody hangs over the river)
    B(m, "castiron", 21, -45.5, -77, 27, -45, -72)    # (the bridge to the lava gallery's door)
    # two rock buttresses, one from each wall, so the deep workings bend (no line runs their length)
    B(m, ROCK, -36, -45, -86, -30, -36, -77); B(m, ROCK, -26, -45, -73, -20, -36, -67)

    # ---- objectives
    # the TNT pile: a solid core, crates stacked round it to 6 m (80 hp each: shooting the top ones out opens a line
    # over the pile from the ledges)
    PX, PZ = 0.0, -10.0
    B(m, "hazard", PX - 1.5, -30, PZ - 1.5, PX + 1.45, -27, PZ + 1.45)   # (the inner 2 x 2 crates' cells, two crates tall)
    cr = 1.5
    for (i, j, lv) in [(i, j, 0) for i in range(-2, 2) for j in range(-2, 2) if not (-1 <= i <= 0 and -1 <= j <= 0)] + \
                      [(i, j, 1) for (i, j) in ((-2, -1), (-2, 0), (1, -1), (1, 0), (-1, -2), (0, -2), (-1, 1), (0, 1))] + \
                      [(i, j, 2) for (i, j) in ((-1, -1), (0, -1), (-1, 0), (0, 0))] + [(-0.5, -0.5, 3)]:
        x0, z0, y0 = PX + i * cr + (0.75 if lv == 1 and False else 0), PZ + j * cr, -30 + lv * cr
        B(m, "woodencrate", x0, y0, z0, x0 + cr - 0.05, y0 + cr, z0 + cr - 0.05)
    m.raw(f"site {PX - 9:g} -30 {PZ - 14:g} {PX + 9:g} -26 {PZ + 6:g}")   # (round the pile, its centre just north of it: on the floor)
    # the hill: a pile of crystals and metal bars in the ore bay (four doors, two per side, each 3-5 m from it)
    HX, HZ = -38.0, -18.0
    for (x0, z0, x1, z1, h) in ((HX - 3.4, HZ + 0.4, HX - 0.6, HZ + 1.8, 1.4), (HX + 0.4, HZ - 2.0, HX + 3.2, HZ - 0.6, 1.8)):
        B(m, "castiron", x0, -30, z0, x1, -30 + h - 0.3, z1); B(m, "gold", x0 + 0.1, -30 + h - 0.3, z0 + 0.1, x1 - 0.1, -30 + h, z1 - 0.1)   # (bar stacks)
    m.raw(f"hill {HX - 6:g} -30 {HZ - 4:g} -28.5 -26 {HZ + 5.5:g}")   # (south to the bay drift door, east into the shaft drift door: T holds the
    # hill from its doors' mouths, CT only from the open north half; with the box 3 m short of both T doors the bots
    # gave the CT side ~57%, with it to z -13 the T side ~45%, to -12.5 even)

    # ===================================== structures and decoration =====================================
    def crystal(x, z, y, h, r, mat="crystal"):
        """a pointed crystal: a square pyramid h tall, r from its axis at the foot"""
        k = r / h
        planes = [(1, k, 0, x + r + k * y), (-1, k, 0, -x + r + k * y), (0, k, 1, z + r + k * y), (0, k, -1, -z + r + k * y)]
        from .m2kit import cut3d
        cut3d(m, mat, planes, (x - r, y, z - r, x + r, y + h, z + r))

    def lamp(x, y_ceil, z, drop=1.2, r=13, i=0.85):
        """a miner's lamp hung from the rock on a chain"""
        B(m, "castiron", x - 0.03, y_ceil - drop + 0.3, z - 0.03, x + 0.03, y_ceil, z + 0.03)
        B(m, "lightblock", x - 0.22, y_ceil - drop, z - 0.22, x + 0.22, y_ceil - drop + 0.3, z + 0.22)
        light(m, x, y_ceil - drop - 0.5, z, r, i)

    def timbers(x0, z0, x1, z1, yf, yc, step=6.0):
        """timber sets along a gallery (posts against both walls, a cap across the roof) every step metres"""
        if (x1 - x0) >= (z1 - z0):
            x = x0 + step / 2
            while x < x1 - 1:
                B(m, TIMBER, x - 0.2, yf, z0, x + 0.2, yc - 0.3, z0 + 0.3); B(m, TIMBER, x - 0.2, yf, z1 - 0.3, x + 0.2, yc - 0.3, z1)
                B(m, TIMBER, x - 0.25, yc - 0.4, z0, x + 0.25, yc, z1); x += step
        else:
            z = z0 + step / 2
            while z < z1 - 1:
                B(m, TIMBER, x0, yf, z - 0.2, x0 + 0.3, yc - 0.3, z + 0.2); B(m, TIMBER, x1 - 0.3, yf, z - 0.2, x1, yc - 0.3, z + 0.2)
                B(m, TIMBER, x0, yc - 0.4, z - 0.25, x1, yc, z + 0.25); z += step

    def cart(x, z, y, along="x", ore="gold"):
        """a minecart on the rails: an iron tub on wheels, a heap of ore"""
        hx, hz = (1.3, 0.75) if along == "x" else (0.75, 1.3)
        B(m, "castiron", x - hx, y + 0.35, z - hz, x + hx, y + 1.4, z + hz)
        for (sx, sz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            B(m, "rust", x + sx * (hx - 0.4) - 0.2, y, z + sz * hz - 0.05 * sz - 0.05, x + sx * (hx - 0.4) + 0.2, y + 0.4, z + sz * hz - 0.05 * sz + 0.05)
        B(m, ore, x - hx + 0.2, y + 1.4, z - hz + 0.2, x + hx - 0.2, y + 1.65, z + hz - 0.2)

    # ---- the surface: the headframe over the cage shaft (a ladder up one leg to its deck at 14 m: a perch over the
    # yard), the winding house, the ore bins, rails across the yard, the cliff over it all
    SX0, SZ0, SX1, SZ1 = SHAFT
    for (x, z) in ((SX0 - 1, SZ0 - 1), (SX1 + 0.4, SZ0 - 1), (SX0 - 1, SZ1 + 0.4), (SX1 + 0.4, SZ1 + 0.4)):
        B(m, "castiron", x, 0, z, x + 0.6, 20, z + 0.6)
    for y in (5, 10):
        B(m, "castiron", SX0 - 1, y, SZ0 - 1, SX1 + 1, y + 0.3, SZ0 - 0.7); B(m, "castiron", SX0 - 1, y, SZ1 + 0.7, SX1 + 1, y + 0.3, SZ1 + 1)
    B(m, "steeldeckplate", SX0 - 1.5, 13.7, SZ0 - 1.5, SX1 + 1.5, 14, SZ1 + 1.5)
    B(m, "galvanizedgrating", SX0 - 1.5, 14, SZ0 - 1.5, SX1 + 1.5, 15, SZ0 - 1.38); B(m, "galvanizedgrating", SX0 - 1.5, 14, SZ1 + 1.38, SX1 + 1.5, 15, SZ1 + 1.5)
    B(m, "galvanizedgrating", SX0 - 1.5, 14, SZ0 - 1.5, SX0 - 1.38, 15, SZ1 + 1.5)
    B(m, "castiron", SX1 + 1.0, 0, 37.7, SX1 + 1.4, 13.7, 38.3); m.ladder(SX1 + 1.8, 38, 0, 14, "w", width=0.6)   # (on its own spine: the deck is straight over the top)
    for (x0, x1) in ((SX0 + 1, SX0 + 1.4), (SX1 - 1.4, SX1 - 1)):
        prism(m, "rust", octagon((x0 + x1) / 2, (SZ0 + SZ1) / 2, 2.4), 16.2, 16.4)
    for x in (SX0 + 1.2, SX1 - 1.2): B(m, "castiron", x - 0.15, 14, (SZ0 + SZ1) / 2 - 0.15, x + 0.15, 16.2, (SZ0 + SZ1) / 2 + 0.15)
    B(m, "castiron", SX0 - 1, 19.7, SZ0 - 1, SX1 + 1, 20.2, SZ1 + 1)
    # the winding house (doors both ends) and its cables up to the headframe
    WH = (-16, 32, -4, 42)
    for (a0, b0, a1, b1) in ((WH[0], WH[1], WH[2], WH[1] + 0.5), (WH[0], WH[3] - 0.5, WH[2], WH[3])):
        B(m, "redengineeringbrick", a0, 0, b0, a1, 7, b1)
    for (x0, x1) in ((WH[0], WH[0] + 0.5), (WH[2] - 0.5, WH[2])):
        B(m, "redengineeringbrick", x0, 0, WH[1], x1, 7, 35); B(m, "redengineeringbrick", x0, 0, 39, x1, 7, WH[3]); B(m, "redengineeringbrick", x0, 4, 35, x1, 7, 39)
    B(m, "corrugated", WH[0] - 0.5, 7, WH[1] - 0.5, WH[2] + 0.5, 7.4, WH[3] + 0.5)
    prism(m, "castiron", octagon(-10, 37, 2.2), 0, 1.2); B(m, "rust", -11.5, 1.2, 35.5, -8.5, 3.6, 38.5)
    light(m, -10, 5.5, 37, 9, 0.7)
    for z in (36.6, 37.4): B(m, "castiron", SX1 + 0.5, 15.8, z - 0.03, WH[0], 16.0, z + 0.03)
    # the ore bins: a hopper on legs (walk under it), a chute
    for (x, z) in ((10, 33), (21.4, 33), (10, 38.4), (21.4, 38.4)): B(m, "castiron", x, 0, z, x + 0.6, 4, z + 0.6)
    B(m, "rustedsheetfence", 9.5, 4, 32.5, 22.5, 7, 39.5); B(m, "rust", 9.5, 7, 32.5, 22.5, 7.3, 39.5)
    wedge(m, "rust", "+z", 14, 2.2, 39.5, 18, 4, 42)
    # rails across the yard to the ore bins and the shaft, two ore carts
    for (x0, z0, x1, z1) in ((-21, 36, 26, 37.4), (16, 37.4, 17.4, 62)):
        B(m, "railtrack", x0, 0, z0, x1, 0.02, z1)
    cart(4, 36.7, 0, ore="bandediron"); cart(16.7, 50, 0, along="z", ore="bandediron")
    # the cliff over the yard's north edge, rock up to the clip
    B(m, "graniterock", -51, 12, 28, 51, 30, 30)
    # the yard's other walls: sheds and a fence line
    for (x0, x1) in ((24, 30), (38, 50)): B(m, "corrugated", x0, 0, 64, x1, 5, 66)
    B(m, "palletstack", -36, 0, 56, -32, 1.6, 60); B(m, "palletstack", 40, 0, 40, 44, 1.6, 44)
    B(m, "wideplanks", -50, 0, 44, -49.5, 3, 56)

    # ---- the haulage level: timber sets, lamps, minecarts on the rails
    timbers(-13, 17, 25, 24, -15, -9.5)
    for x in range(-8, 25, 12): lamp(x, -9.9, 20.5)
    B(m, "railtrack", -13, -15, 20, 25, -14.98, 21.4)
    cart(10, 20.7, -15); cart(-31, 24, -15, along="z")
    timbers(-32, 10, -14, 42, -15, -9, step=8)
    for (x, z) in ((-18, 16), (-18, 30), (-30, 28)): lamp(x, -9.4, z)
    for z in (6, 18): lamp(34, -8.4, z)
    cart(39, 26, -15, along="z"); B(m, "palletstack", 27, -15, 26, 30, -13.6, 29)
    timbers(-33, -3, -17, 3, -15, -11)
    for z in (0, 22): lamp(-42, -7.4, z)
    for x in (-46, -38): B(m, TIMBER, x - 0.2, -15, 9.8, x + 0.2, -7.4, 10.2); B(m, TIMBER, x - 0.2, -15, 15.8, x + 0.2, -7.4, 16.2)

    # cover on CT's approaches (solid, 1.65 m tall; away from the doors' main lines): three ore carts off their rails
    cart(8.5, -21.5, -30, ore="bandediron"); cart(14.5, -6, -30, along="z"); cart(-5.5, -20.5, -30, ore="jasper")
    # ---- the magazine cavern: roof timbers (grapple anchors), work lights, ore seams in the walls, hazard boards
    for z in (-21, -13, -5):
        B(m, TIMBER, -20, -17.2, z - 0.3, 20, -16.6, z + 0.3)
    for (x, z) in ((-12, -18), (12, -18), (-12, -4), (12, -4), (0, -22)): lamp(x, -16.6, z, drop=1.0, r=15)
    for (x0, y0, z0, x1, y1, z1, mat) in ((-20.1, -27, -22, -20, -25, -17, "bandediron"), (20, -23, -6, 20.1, -20, 0, "gold"), (-8, -21, -25.1, 0, -19, -25, "malachite"),
                                          (6, -28, -25.1, 14, -26.5, -25, "jasper"), (20, -29, -24, 20.1, -27, -20, "bandediron")):
        B(m, mat, x0, y0, z0, x1, y1, z1)
    B(m, "hazard", -6, -19, 4.9, 6, -18.4, 5)
    # the ore store: crystals on its walls, lamps, a heap of ore by the chute
    for (x, z, h, r) in ((22.5, -20, 3.4, 0.9), (23, 2, 2.6, 0.7), (44.5, -10, 3.0, 0.8), (44, -20, 2.2, 0.6)): crystal(x, z, -30, h, r, "quartzcrystals")
    for (x, z) in ((28, -16), (40, -16), (34, 0)): lamp(x, -18.4, z)
    B(m, "rubblepile", 28, -30, -2, 38, -29.2, 4)
    # the ore bay: the hill, a pile of crystals and metal bars (cover in the middle, ~2 m), lamps
    B(m, "rubblepile", HX - 4, -30, HZ - 3, HX + 4, -29.4, HZ + 3)
    for (dx, dz, h, r) in ((-2.2, -1.2, 2.6, 0.8), (1.8, 1.0, 2.2, 0.7), (-0.2, 1.8, 1.6, 0.5), (3.4, 0.2, 1.4, 0.5), (0.2, -0.4, 3.0, 0.9)):
        crystal(HX + dx, HZ + dz, -29.4, h, r)
    for (dx, dz) in ((-1.2, -1.6), (2.2, 1.4), (-3.0, -0.4)):   # loose bars on the heap
        B(m, "gold", HX + dx, -29.4, HZ + dz, HX + dx + 1.2, -29.1, HZ + dz + 0.35)
    for (x, z) in ((-42, -22), (-34, -15), (-42, -14), (-34, -22)): lamp(x, -20.4, z, r=14, i=1.0)
    # the shaft drift and the adits: timbers, lamps
    timbers(-29, -16, -21, 19, -30, -24); timbers(-46, -57, -38, -27, -30, -25); timbers(-37, -32, -6, -26, -30, -25); timbers(-42, -11, -38, 9, -30, -25)
    for (x, z) in ((-25, 0), (-25, -12), (-42, -50), (-42, -36), (-28, -29), (-18, -29)): lamp(x, -24.4 if x == -25 else -25.4, z, drop=0.8)
    # the deep workings: the refuge's steel door screen, the pump station, bridges' rails, the lava's light, crystals
    B(m, "steeldoorskin", -68, -45, -84.5, -62, -40, -84); B(m, "steeldoorskin", -58, -45, -84.5, -52, -40, -84)
    B(m, "vaultdoor", -64, -41, -86.2, -56, -39, -85.9)
    for (x0, x1) in ((21, 27),):
        B(m, "castiron", x0, -45, -77, x0 + 0.12, -44, -72); B(m, "castiron", x1 - 0.12, -45, -77, x1, -44, -72)
    for x in range(4, 32, 10):
        light(m, x, -44, -74.5, 9, 0.9)
        B(m, "emberbed", x - 3, -45, -78, x + 3, -44.98, -77)
    for x in range(-62, 32, 16): lamp(x, -36.4, -82)
    for (x, z) in ((-44, -62), (-40, -62)): B(m, "gaugecluster", x - 1, -45, z - 1, x + 1, -43.5, z + 1)
    B(m, "galvanizedsteel", -46, -40, -66, -45.6, -30, -65.6)
    # the grotto: glowing crystals round its walls (out of the stair and the lanes), cover crystals
    for (x, z, h, r) in ((-14, -62, 4.0, 1.1), (-13.5, -48, 3.2, 0.9), (-14, -38, 2.6, 0.8), (14, -60, 3.6, 1.0), (13.5, -46, 4.4, 1.2), (14, -38, 2.8, 0.8),
                         (-9, -54, 1.8, 0.6), (9, -42, 1.8, 0.6), (8, -58, 2.4, 0.7), (-8, -40, 2.0, 0.6)):
        crystal(x, z, -45, h, r, "crystal" if h < 2.5 else "quartzcrystals")
    for (x, z) in ((-10, -50), (10, -50), (0, -40)): light(m, x, -38, z, 14, 0.8)
    # the lava gallery: its ledge's edge glows, lamps
    for z in range(-60, -28, 10): light(m, 30, -43, z, 9, 0.9); lamp(24, -24.4, z, drop=1.4)

    # ---- rock shapes (review 2: no more square basements): chamfered ceiling edges in the big rooms, sloped wall
    # feet between the doors, ore seams and glow-worm patches along the galleries (on walls and roofs, out of the lanes)
    def chamfer(x0, z0, x1, z1, yc, d=2.0, h=2.0, skip_w=None, skip_s=False):
        """(skip_w: a z range on the west side left square, for a ladder through the roof; skip_s: no south side)"""
        wedge(m, ROCK, "-z", x0, yc - h, z0, x1, yc, z0 + d, up=1)
        if not skip_s: wedge(m, ROCK, "+z", x0, yc - h, z1 - d, x1, yc, z1, up=1)
        for (a, b) in ([(z0 + d, skip_w[0]), (skip_w[1], z1 - d)] if skip_w else [(z0 + d, z1 - d)]):
            wedge(m, ROCK, "-x", x0, yc - h, a, x0 + d, yc, b, up=1)
        wedge(m, ROCK, "+x", x1 - d, yc - h, z0 + d, x1, yc, z1 - (0 if skip_s else d), up=1)
    chamfer(-20, -25, 20, 5, -16, 2.5, 2.5, skip_w=(-2.5, 2.5)); chamfer(-16, -66, 16, -35, -24, 3, 3); chamfer(-46, -25, -30, -12, -20); chamfer(21, -24, 46, 6, -18, skip_s=True)
    def feet(runs, yf, d=0.9, h=0.7):
        for (side, a0, a1, w) in runs:     # side n/s/w/e, from a0 to a1 along it, the wall's inner face at w
            if side == "n": wedge(m, ROCK, "-z", a0, yf, w, a1, yf + h, w + d)
            if side == "s": wedge(m, ROCK, "+z", a0, yf, w - d, a1, yf + h, w)
            if side == "w": wedge(m, ROCK, "-x", w, yf, a0, w + d, yf + h, a1)
            if side == "e": wedge(m, ROCK, "+x", w - d, yf, a0, w, yf + h, a1)
    feet([("n", 4, 20, -25), ("n", -20, -14.5, -25), ("e", -25, -18, 20), ("e", -12, 5, 20)], -30)
    feet([("w", -66, -35, -16), ("e", -66, -35, 16), ("s", -16, -4, -35), ("s", 4, 16, -35)], -45, 1.4, 1.1)
    feet([("w", -25, -12, -46), ("n", -30, -32, -25)], -30)
    import random
    rng = random.Random(1906)
    ORES = ["bandediron", "gold", "malachite", "jasper", "geode"]
    def seams(x0, z0, x1, z1, yf, yc, step=12.0):
        along_x = (x1 - x0) >= (z1 - z0)
        a, a1 = (x0, x1) if along_x else (z0, z1)
        a += step / 2; k = 0
        while a < a1 - 2:
            w, ht, y = rng.uniform(1.6, 3.0), rng.uniform(0.5, 1.1), rng.uniform(yf + 1.5, yc - 1.5)
            mat = ORES[rng.randrange(len(ORES))]
            if along_x:
                zz = z0 if k % 2 == 0 else z1 - 0.08
                B(m, mat, a - w / 2, y, zz, a + w / 2, y + ht, zz + 0.08)
            else:
                xx = x0 if k % 2 == 0 else x1 - 0.08
                B(m, mat, xx, y, a - w / 2, xx + 0.08, y + ht, a + w / 2)
            if k % 2 == 1:   # (a glow-worm patch on the roof between seams)
                if along_x: B(m, "glowwormcave", a - 1.5, yc - 0.05, z0 + 1, a + 1.5, yc, z1 - 1)
                else: B(m, "glowwormcave", x0 + 1, yc - 0.05, a - 1.5, x1 - 1, yc, a + 1.5)
            a += step; k += 1
    for (x0, z0, x1, z1, yf, yc) in ((-13, 17, 25, 24, -15, -9.5), (-70, -86, 32, -67, -45, -36), (-46, -57, -38, -27, -30, -25), (-37, -32, -6, -26, -30, -25),
                                     (-29, -16, -21, 19, -30, -24), (-33, -3, -17, 3, -15, -11), (-42, -11, -38, 9, -30, -25), (-50, -4, -34, 30, -15, -7)):
        seams(x0, z0, x1, z1, yf, yc)
    # the grotto's big crystals: against its walls and in its corners (the lanes and the stair stay clear)
    for (x, z, h, r) in ((-14.2, -64.2, 7.0, 1.8), (14.2, -64.2, 6.2, 1.6), (-14.2, -52, 5.4, 1.5), (14.2, -55, 6.6, 1.7), (-14.2, -42, 4.8, 1.4), (14.2, -40, 5.8, 1.6)):
        crystal(x, z, -45, h, r, "quartzcrystals")

    # ---- spawns
    for x in (-6, -2, 2, 6): m.spawn("a", x, 0, 54, 40)   # (facing the headframe and the cage shaft)
    for (x, z) in ((-64, -95), (-60, -95), (-56, -95), (-60, -91)): m.spawn("b", x, -45, z, 180)
    # king of the hill's own spawn set (map format 11): T 66-90 m, CT 110-120 m (bot routes) from the ore bay's hill,
    # away from the hill room (T: the haulage level's loading station and gallery; CT: the ore store and the deep
    # workings), so koth fights move through the cavern, the drifts and the adits, not only the hill room
    for (x, y, z, yaw) in ((12, -15, 22, 90), (24, -15, 22, 90), (32, -15, 14, 90), (36, -15, 22, 90)):
        m.spawn("koth-a", x, y, z, yaw)
    for (x, y, z, yaw) in ((-60, -45, -95, 180), (-10, -45, -82, 90), (-4, -45, -62, 180), (-56, -45, -95, 180)):
        m.spawn("koth-b", x, y, z, yaw)
    for (x, y, z, yaw) in ((-48, 0, 40, -90), (34, -15, 20, 90), (-42, -15, 20, 0), (0, -30, -22, 180), (40, -30, -20, 0), (-40, -45, -82, -90), (30, -45, -82, 90), (10, -45, -45, 180)):
        m.spawn("any", x, y, z, yaw)

    # ---- lights (layout pass)
    for R in rooms:
        x0, z0, x1, z1 = R["r"]
        nx, nz = max(1, round((x1 - x0) / 14)), max(1, round((z1 - z0) / 14))
        for i in range(nx):
            for k in range(nz):
                light(m, x0 + (x1 - x0) * (i + 0.5) / nx, min(R["yc"] - 1, R["yf"] + 6), z0 + (z1 - z0) * (k + 0.5) / nz, 14, 0.8)

    # ---- the rock: everything underground that isn't a room (with its 1 m shell), a door or a lava channel is solid,
    # up to the surface (y 0; under the yard, its floor at -1)
    air = [(R["r"][0] - 1, R["yf"] - 1, R["r"][1] - 1, R["r"][2] + 1, R["yc"] + (0 if R["noceil"] else 1), R["r"][3] + 1) for R in rooms]
    air += doors + [(0, -49, -77, 32, -46, -72), (27, -49, -66, 33, -46, -25), (-4, -25, 6, 4, -15, 20)]   # (the lava channels; the chute's stairs under the gallery)
    fill_solid(m, ROCK, (-72, -50, -102, 48, 0, 64), air)

    # ---- bounds: the yard is the only open sky; clip over it at 45 m and round the map
    B(m, "clip", -110, 45, -110, 110, 46, 110)
    for (a, b, c, d) in ((-110, -110, -50, 110), (50, -110, 110, 110), (-50, -110, 50, YZ0), (-50, YZ1, 50, 110)):
        B(m, "clip", a, 0, b, c, 45, d)
    m.raw("palette base 0.45 0.4 0.35 accent 0.46 0.3 0.16 metal 0.3 0.29 0.28 glow 1 0.75 0.4 sky 0.42 0.38 0.33")
    m.raw("setting a deep mine")
    m.save("mine", dress=False)
