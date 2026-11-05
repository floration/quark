#!/usr/bin/env python3
"""The OUTLINED player set (owner, 2026-11-05, of three clean directions: "outlined for sure not even close"): all 64
player models (the 11, then the 20 and the 33 extras the owner approved the same day) as box model files (docs/ASSETS.md "Box models": meshes[0].extras.quark version 2), so the game's own
box path draws them with its outlines, face shading, crouch and walk, the same way as the outlined weapon set.

    python3 tools/models/gen_players_outlined.py [--out DIR] [--scratch DIR] [--no-preview] [--only NAME,...]

Writes <out>/models/player/<NAME>.gltf (default out: /mnt/project-files/player-models/outlined) and the previews
lineup-outlined.png (orange team), lineup-outlined-blue.png and walk-outlined.png, each rendered in --scratch first
and copied over (a network mount). Shipped (owner, 2026-11-05): the files in assets/overrides/models/player/ are this
script's output, byte for byte: `--out assets/overrides --no-preview` rewrites them. They are AI-made: curated `ai` entries
(by Claude) in assets/provenance/models.json, which tools/provenance.py sync keeps.

The designs are characters_outlined.py (the 11) and characters_outlined_extra.py (the 20), characters_outlined_extra2.py (the 33); NAMES is their order, the
game's (kPlayerModelNames, src/game/gamedefs.h). A new model: its builder in a characters file, its name in that file's
NAMES and in kPlayerModelNames (the same place). Shared palette (the outlined weapons' rules): a 5 mm grid, a few big blocks,
no chamfers; dark = the metal slot x 0.55, mid = the metal slot, light = the base slot, bore / recess = metal x 0.25,
team panels and bands, a glow piece or two (visor, eyes); plus a few fixed colours a character needs (skin, the
zombie's green, the wizard's robe). On players the game drives only the team slot (models.cpp drivenSlots): metal and
base boxes draw at the reference colours whatever the map, so the luminance floor checked here holds on every map.

Checks (a failure exits 1): every point of every box (corners and a grid through it) inside the shared hitbox
(gen_players_realistic_dark.hitbox_misses: the body cylinder, the head cylinder above the neck line); the 5 mm grid;
at most 1000 triangles (12 a box, the triangles in the file; the assert of the D2 generator) and 256 boxes
(kMaxOverrideBoxes); a thigh, shin and foot on each side (the game walks it); min sRGB 0.18 luminance but for team,
recesses, visors and eyes; no two same-facing faces of different boxes on one plane overlapping (they would flicker
and draw each other's outlines). Deterministic: no randomness; the same input writes the same bytes.
"""
import argparse
import base64
import json
import math
import os
import shutil
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_players_realistic_dark as G  # noqa: E402
from characters_outlined import BUILDERS as BUILDERS_11  # noqa: E402
import characters_outlined_extra as X  # noqa: E402
import characters_outlined_extra2 as X2  # noqa: E402

# the 11, then the 20 extras, then the 33 more (2026-11-05): the order of kPlayerModelNames (src/game/gamedefs.h)
NAMES = G.NAMES + X.NAMES + X2.NAMES
BUILDERS = dict(BUILDERS_11, **X.BUILDERS, **X2.BUILDERS)
MAX_TRIS, MAX_BOXES = G.MAX_TRIS, 256
LUM = np.array((0.2126, 0.7152, 0.0722))
FLOOR = 0.18 ** 2.2


def lin(c):
    return np.array([v ** 2.2 for v in c])


# the reference slot colours (models.cpp makeReference): what metal / base draw as on a player
REF = {"metal": lin((0.30, 0.33, 0.37)) * 1.6 + 0.05, "base": lin((0.62, 0.62, 0.64)) * 1.1, "team": np.ones(3)}

# tone: (slot or None, slot multiplier or fixed linear colour, exempt from the luminance floor)
TONES = {
    "dark": ("metal", 0.55, False), "mid_dk": ("metal", 0.75, False), "mid": ("metal", 1.0, False),
    "light": ("base", 1.0, False), "bore": ("metal", 0.25, True),
    "team": ("team", 1.0, True), "team_dk": ("team", 0.6, True),
    "visor": (None, (0.25, 1.30, 1.70), True),        # glowing cyan (over 1: it reads as lit)
    "zeye": (None, (1.50, 1.60, 0.30), True),         # the zombie's glowing eyes
    "eye": (None, lin((0.05, 0.05, 0.06)), True),
    "skin": (None, lin((0.88, 0.66, 0.50)), False), "skin_dk": (None, lin((0.62, 0.42, 0.32)), False),
    "zgreen": (None, lin((0.52, 0.74, 0.38)), False), "zgreen_dk": (None, lin((0.38, 0.54, 0.28)), False),
    "hair": (None, lin((0.38, 0.25, 0.16)), False), "beard": (None, lin((0.92, 0.92, 0.90)), False),
    "robe": (None, lin((0.46, 0.30, 0.74)), False), "gold": (None, lin((0.95, 0.76, 0.26)), False),
    "leather": (None, lin((0.58, 0.38, 0.22)), False), "glove": (None, lin((0.86, 0.17, 0.12)), False),
    "denim": (None, lin((0.33, 0.43, 0.64)), False),
    "hat": (None, lin((0.74, 0.56, 0.36)), False), "hat_dk": (None, lin((0.56, 0.41, 0.26)), False),
    # the 20 extras' fixed colours (characters_outlined_extra.py)
    "white": (None, lin((0.94, 0.94, 0.92)), False),
    "amber": (None, lin((0.88, 0.62, 0.18)), False),       # the astronaut's gold visor (not lit)
    "lacquer": (None, lin((0.70, 0.18, 0.15)), False),     # the samurai's red lacquer
    "fur": (None, lin((0.55, 0.40, 0.27)), False),
    "straw": (None, lin((0.96, 0.80, 0.40)), False),       # the viking's beard
    "ivory": (None, lin((0.94, 0.90, 0.78)), False),
    "khaki": (None, lin((0.80, 0.68, 0.40)), False),       # the firefighter's turnout coat
    "hivis": (None, lin((0.97, 0.88, 0.18)), False),
    "brass": (None, lin((0.84, 0.64, 0.30)), False), "brass_dk": (None, lin((0.62, 0.45, 0.22)), False),
    "glass": (None, lin((0.36, 0.56, 0.62)), True),
    "canvas": (None, lin((0.70, 0.67, 0.58)), False),
    "alien": (None, lin((0.38, 0.82, 0.74)), False),
    "bandage": (None, lin((0.86, 0.80, 0.62)), False), "bandage_dk": (None, lin((0.66, 0.58, 0.42)), False),
    "bone": (None, lin((0.95, 0.93, 0.86)), False),
    "redeye": (None, (1.60, 0.22, 0.15), True),             # glowing red (over 1: it reads as lit)
    "pink": (None, lin((0.96, 0.48, 0.74)), False),
    "canary": (None, lin((0.98, 0.86, 0.34)), False),
    "lime": (None, lin((0.48, 0.84, 0.22)), False),
    "pale": (None, lin((0.86, 0.86, 0.84)), False),
    "tan": (None, lin((0.74, 0.52, 0.35)), False),          # the pharaoh's skin
    "hairblk": (None, lin((0.22, 0.21, 0.23)), False),
    "plum": (None, lin((0.50, 0.15, 0.38)), False),         # the king's cape
    "olive": (None, lin((0.46, 0.49, 0.32)), False),
    "lamp": (None, (1.70, 1.60, 1.00), True),               # the miner's lamp (lit)
    # the 33 more (characters_outlined_extra2.py)
    "frost": (None, lin((0.80, 0.86, 0.92)), False),        # the yeti's fur
    "slate": (None, lin((0.38, 0.42, 0.52)), False),        # the yeti's face
    "carrot": (None, lin((0.95, 0.50, 0.15)), False),
    "burlap": (None, lin((0.74, 0.62, 0.44)), False),
    "wolf": (None, lin((0.50, 0.46, 0.44)), False), "wolf_lt": (None, lin((0.74, 0.70, 0.64)), False),
    "forest": (None, lin((0.38, 0.64, 0.30)), False),       # the dinosaur costume
    "pumpkin": (None, lin((0.93, 0.54, 0.14)), False), "pumpkin_dk": (None, lin((0.72, 0.38, 0.10)), False),
    "stem": (None, lin((0.38, 0.52, 0.22)), False),
    "candle": (None, (1.80, 1.20, 0.25), True),             # the pumpkin's carved face (lit)
    "capred": (None, lin((0.86, 0.17, 0.15)), False),       # the mushroom cap
    "stone": (None, lin((0.56, 0.52, 0.47)), False), "stone_dk": (None, lin((0.42, 0.39, 0.35)), False),
    "moss": (None, lin((0.46, 0.60, 0.28)), False),
    "crt": (None, lin((0.78, 0.72, 0.60)), False),          # the TV casing
    "screen": (None, (0.35, 1.30, 0.70), True),             # the TV's lit screen
    "tights": (None, lin((0.97, 0.82, 0.80)), False),
    "marine": (None, lin((0.36, 0.45, 0.41)), False), "marine_dk": (None, lin((0.27, 0.33, 0.31)), False),
    "ember": (None, (1.70, 0.75, 0.15), True),              # the marine's visor (lit)
    "sage": (None, lin((0.58, 0.64, 0.54)), False),         # the engineer's coveralls
    "ghillie": (None, lin((0.42, 0.50, 0.30)), False), "ghillie_dk": (None, lin((0.33, 0.38, 0.24)), False),
    "sand": (None, lin((0.74, 0.66, 0.50)), False),         # the bomb suit
    "hazmat": (None, lin((0.95, 0.82, 0.22)), False),
    "cream": (None, lin((0.96, 0.92, 0.80)), False),
    "honey": (None, lin((0.98, 0.78, 0.18)), False),        # the bee's stripes
}
GROUP = {"dark": "dark", "mid_dk": "dark", "bore": "dark", "mid": "mid", "light": "light", "team": "team", "team_dk": "team"}


def tone_color(tone, team=None):
    slot, v, _ = TONES[tone]
    if slot is None:
        return np.array(v, float)
    ref = REF[slot] if (slot != "team" or team is None) else np.asarray(team, float)
    return ref * v


# ---------------------------------------------------------------------------------------------------------------
# a design's boxes in metres, checked

class Model:
    def __init__(self, d):
        self.name, self.desc = d.name, d.desc
        self.boxes = []
        for b in d.boxes:
            x0, x1, y0, y1, z0, z1 = b["b"]
            c = np.array(((x0 + x1) / 200, (y0 + y1) / 200, (z0 + z1) / 200))
            h = np.array(((x1 - x0) / 200, (y1 - y0) / 200, (z1 - z0) / 200))
            self.boxes.append(dict(b, c=c, h=h, crouch=b["bone"] != "head"))
        self.rig = measure_rig(self.boxes)

    def tris(self):
        return 12 * len(self.boxes)


def measure_rig(boxes):
    """models.cpp playerRig: the joints from the boxes"""
    th = [b for b in boxes if b["bone"] in ("thigh_l", "thigh_r")]
    top = max(b["c"][1] + b["h"][1] for b in th)
    bot = min(b["c"][1] - b["h"][1] for b in th)
    hx = sum(abs(b["c"][0]) for b in th) / len(th)
    heads = [b["c"][1] - b["h"][1] for b in boxes if b["bone"] == "head"]
    ank = max([b["c"][1] + b["h"][1] for b in boxes if b["bone"] in ("foot_l", "foot_r")] or [0])
    return G.Rig(hipY=top, hipX=hx, kneeY=bot, ankleY=min(ank, bot), neckY=min(heads) if heads else 1.44)


def corners(c, h):
    k = np.array([[i & 1, (i >> 1) & 1, (i >> 2) & 1] for i in range(8)], float) * 2 - 1
    return c + k * h


def check(m):
    errs, warns = [], []
    g = np.linspace(-1, 1, 5)
    grid = np.stack(np.meshgrid(g, g, g, indexing="ij"), -1).reshape(-1, 3)
    for i, b in enumerate(m.boxes):
        lab = "%s %d (%s, %s)" % (m.name, i + 1, b["name"], b["bone"])
        pts = np.concatenate([corners(b["c"], b["h"]), b["c"] + grid * b["h"]])
        # (the neck line: points just either side of it, where the cylinder narrows)
        lo, hi = b["c"][1] - b["h"][1], b["c"][1] + b["h"][1]
        if lo < G.Y_NECK <= hi:
            ring = corners(b["c"], b["h"]).copy()
            ring[:, 1] = G.Y_NECK
            pts = np.concatenate([pts, ring])
        bad = G.hitbox_misses(pts)
        if bad.any():
            q = pts[bad][0]
            errs.append("%s: %d points outside the hitbox, e.g. (%.3f, %.3f, %.3f) r %.3f" % (lab, bad.sum(), q[0], q[1], q[2], math.hypot(q[0], q[2])))
        for v in b["b"]:
            if abs(v * 2 - round(v * 2)) > 1e-9:
                errs.append("%s: %.2f cm off the 5 mm grid" % (lab, v))
        if b["bone"] not in G.BONES:
            errs.append("%s: unknown bone" % lab)
        if b["tone"] not in TONES:
            errs.append("%s: unknown tone %s" % (lab, b["tone"]))
            continue
        slot, v, exempt = TONES[b["tone"]]
        if not exempt and float(tone_color(b["tone"]) @ LUM) < FLOOR:
            errs.append("%s: luminance under sRGB 0.18" % lab)
    if m.tris() > MAX_TRIS:
        errs.append("%s: %d triangles (at most %d)" % (m.name, m.tris(), MAX_TRIS))
    if len(m.boxes) > MAX_BOXES:
        errs.append("%s: %d boxes (at most %d)" % (m.name, len(m.boxes), MAX_BOXES))
    have = {b["bone"] for b in m.boxes}
    for bn in ("pelvis", "torso", "head", "thigh_l", "shin_l", "thigh_r", "shin_r", "foot_l", "foot_r"):
        if bn not in have:
            errs.append("%s: nothing on %s (it must walk)" % (m.name, bn))
    # coplanar same-facing faces of two boxes that overlap: flicker and doubled outlines
    for i, a in enumerate(m.boxes):
        for j in range(i + 1, len(m.boxes)):
            b = m.boxes[j]
            if a["bone"] != b["bone"]:
                continue
            for ax in range(3):
                o = [k for k in range(3) if k != ax]
                ov = all(min(a["c"][k] + a["h"][k], b["c"][k] + b["h"][k]) - max(a["c"][k] - a["h"][k], b["c"][k] - b["h"][k]) > 1e-6 for k in o)
                if not ov:
                    continue
                rlo = [max(a["c"][k] - a["h"][k], b["c"][k] - b["h"][k]) for k in o]
                rhi = [min(a["c"][k] + a["h"][k], b["c"][k] + b["h"][k]) for k in o]
                for s in (-1, 1):
                    p = a["c"][ax] + s * a["h"][ax]
                    if abs(p - (b["c"][ax] + s * b["h"][ax])) >= 1e-6:
                        continue
                    # (no matter when a third box hides that patch of the plane)
                    if any(k is not a and k is not b and k["bone"] == a["bone"] and abs(p - k["c"][ax]) < k["h"][ax] - 1e-6
                           and all(k["c"][o[t]] - k["h"][o[t]] <= rlo[t] + 1e-6 and rhi[t] <= k["c"][o[t]] + k["h"][o[t]] + 1e-6 for t in (0, 1))
                           for k in m.boxes):
                        continue
                    warns.append("%s: %s and %s share a %s%s face" % (m.name, a["name"], b["name"], "+" if s > 0 else "-", "xyz"[ax]))
    return errs, warns


# ---------------------------------------------------------------------------------------------------------------
# the file: the box list in meshes[0].extras.quark (what the game draws) + the same boxes as triangles (viewers)

def r9(v):
    return float("%.9g" % v)


def box_mesh(c, h):
    P = corners(c, h)
    F = np.array([[0, 2, 3], [0, 3, 1], [4, 5, 7], [4, 7, 6], [0, 1, 5], [0, 5, 4], [2, 6, 7], [2, 7, 3], [0, 4, 6], [0, 6, 2],
                  [1, 3, 7], [1, 7, 5]])
    # flat faces: 4 vertices a face with its normal (24 vertices, 12 triangles)
    Ps, Ns, Fs = [], [], []
    for t in range(0, 12, 2):
        tri = F[t]
        n = np.cross(P[tri[1]] - P[tri[0]], P[tri[2]] - P[tri[0]])
        n = n / np.linalg.norm(n)
        idx = list(dict.fromkeys(list(F[t]) + list(F[t + 1])))
        base = len(Ps)
        Ps += [P[k] for k in idx]
        Ns += [n] * 4
        Fs += [[base + idx.index(k) for k in F[t]], [base + idx.index(k) for k in F[t + 1]]]
    return np.array(Ps), np.array(Ns), np.array(Fs)


def box_json(b):
    slot, v, _ = TONES[b["tone"]]
    col = tone_color(b["tone"])
    d = {"c": [r9(x) for x in b["c"]], "h": [r9(x) for x in b["h"]], "slot": slot or "body", "color": [r9(x) for x in col]}
    if slot:
        d["mul"] = [r9(v)] * 3
        d["add"] = [0, 0, 0]
    d.update({"bone": b["bone"], "shadow": b["shadow"], "visOnly": b["vis"], "crouch": b["crouch"], "name": b["name"]})
    return d


def material_name(tone):
    slot = TONES[tone][0]
    return tone if slot is None or tone.startswith(slot) else slot + "_" + tone


def write_gltf(m, path):
    bin_ = bytearray()
    views, accs, prims, mats, mat_index = [], [], [], [], {}

    def put(arr, target):
        b = arr.tobytes()
        off = len(bin_)
        bin_.extend(b)
        while len(bin_) % 4:
            bin_.append(0)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(b), "target": target})
        return len(views) - 1

    order = list(dict.fromkeys(b["tone"] for b in m.boxes))
    for tone in order:
        Ps, Ns, Cs, Fs, base = [], [], [], [], 0
        for b in m.boxes:
            if b["tone"] != tone:
                continue
            P, N, F = box_mesh(b["c"], b["h"])
            Ps.append(P)
            Ns.append(N)
            Cs.append(np.tile(tone_color(tone), (len(P), 1)))
            Fs.append(F + base)
            base += len(P)
        P = np.round(np.concatenate(Ps), 6).astype("<f4")
        N = np.concatenate(Ns).astype("<f4")
        C = np.round(np.concatenate(Cs), 5).astype("<f4")
        F = np.concatenate(Fs).astype("<u4").reshape(-1)
        a0 = len(accs)
        accs.append({"bufferView": put(P, 34962), "componentType": 5126, "count": len(P), "type": "VEC3",
                     "min": [float(x) for x in P.min(0)], "max": [float(x) for x in P.max(0)]})
        accs.append({"bufferView": put(N, 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
        accs.append({"bufferView": put(C, 34962), "componentType": 5126, "count": len(C), "type": "VEC3"})
        accs.append({"bufferView": put(F, 34963), "componentType": 5125, "count": len(F), "type": "SCALAR"})
        mn = material_name(tone)
        if mn not in mat_index:
            mat_index[mn] = len(mats)
            mats.append({"name": mn, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": 1}})
        prims.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "COLOR_0": a0 + 2}, "indices": a0 + 3, "material": mat_index[mn], "mode": 4})
    doc = {
        "asset": {"version": "2.0", "generator": "quark tools/models/gen_players_outlined.py"},
        "extras": {"style": "outlined", "player": m.name, "description": m.desc},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": m.name, "mesh": 0}],
        "meshes": [{"name": m.name, "primitives": prims, "extras": {"quark": {"version": 2, "boxes": [box_json(b) for b in m.boxes]}}}],
        "materials": mats, "accessors": accs, "bufferViews": views,
        "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(bin_)).decode("ascii")}],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    txt = json.dumps(doc, separators=(",", ":")) + "\n"
    with open(path, "w") as f:
        f.write(txt)
    return len(txt)


def read_back(path):
    with open(path) as f:
        doc = json.load(f)
    boxes = doc["meshes"][0]["extras"]["quark"]["boxes"]
    ntri = sum(doc["accessors"][p["indices"]]["count"] // 3 for p in doc["meshes"][0]["primitives"])
    return boxes, ntri


# ---------------------------------------------------------------------------------------------------------------
# previews: the game's box look (models.cpp / renderer.cpp boxXf): face factors +x/+y 1, -x 0.92, -y 0.8, +-z 0.96,
# the dynamic shader's light (sky + sun, 60% own light), and the box outline: 1 px (+ a 1 px ramp) inside every face edge

FACE_K = {(0, 1): 1.0, (0, -1): 0.92, (1, 1): 1.0, (1, -1): 0.8, (2, 1): 0.96, (2, -1): 0.96}
SUN = G._norm((0.45, 0.80, -0.35))
ORANGE = np.array((0.95, 0.30, 0.04))
BLUE = np.array((0.06, 0.25, 0.95))


def posed_faces(m, pz, team, scale=1.0):
    """every box's 6 faces as quads: (corners 4x3, normal, colour, id, group)"""
    out = []
    for i, b in enumerate(m.boxes):
        M = pz[b["bone"]]
        col = tone_color(b["tone"], team)
        c, h = b["c"], b["h"]
        for ax in range(3):
            for s in (-1, 1):
                o = [k for k in range(3) if k != ax]
                q = []
                for u, v in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    p = c.copy()
                    p[ax] += s * h[ax]
                    p[o[0]] += u * h[o[0]]
                    p[o[1]] += v * h[o[1]]
                    q.append(p)
                q = np.array(q) @ M[:3, :3].T + M[:3, 3]
                n = np.zeros(3)
                n[ax] = s
                n = M[:3, :3] @ n
                out.append((q, n, col * FACE_K[(ax, s)], i * 6 + ax * 2 + (s > 0), GROUP.get(b["tone"], "fixed")))
    return out


GROUPS = ["dark", "mid", "light", "team", "fixed"]


def raster(faces, cam, ss, ow=1.0, bg=0.5):
    W, H = cam.W, cam.H
    depth = np.full((H, W), -np.inf)
    fid = np.full((H, W), -1, np.int64)
    cb = np.zeros((H, W, 3))
    gb = np.full((H, W), -1, np.int8)
    for q, n, col, ident, grp in faces:
        if n @ (-cam.f) <= 1e-6:
            continue   # (back faces culled)
        S, D = cam.project(q)
        ndl = max(float(n @ SUN), 0.0)
        light = 0.5 * (0.6 + 0.25 * n[1]) + 0.85 * ndl
        shade = col * (light * 0.4 + 0.6)
        for tri in ((0, 1, 2), (0, 2, 3)):
            (x0, y0), (x1, y1), (x2, y2) = S[list(tri)]
            bx0, bx1 = max(int(math.floor(min(x0, x1, x2))), 0), min(int(math.ceil(max(x0, x1, x2))), W - 1)
            by0, by1 = max(int(math.floor(min(y0, y1, y2))), 0), min(int(math.ceil(max(y0, y1, y2))), H - 1)
            if bx0 > bx1 or by0 > by1:
                continue
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(den) < 1e-12:
                continue
            px, py = np.meshgrid(np.arange(bx0, bx1 + 1) + 0.5, np.arange(by0, by1 + 1) + 0.5)
            w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
            w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
            w2 = 1 - w0 - w1
            msk = (w0 >= -1e-9) & (w1 >= -1e-9) & (w2 >= -1e-9)
            z = w0 * D[tri[0]] + w1 * D[tri[1]] + w2 * D[tri[2]]
            sub = depth[by0:by1 + 1, bx0:bx1 + 1]
            msk &= z > sub + 1e-7
            if not msk.any():
                continue
            sub[msk] = z[msk]
            fid[by0:by1 + 1, bx0:bx1 + 1][msk] = ident
            cb[by0:by1 + 1, bx0:bx1 + 1][msk] = shade
            gb[by0:by1 + 1, bx0:bx1 + 1][msk] = GROUPS.index(grp)
    covered = fid >= 0
    # the outline: a covered pixel within ow output pixels of another face (or of the background) is the edge colour
    r = max(1, int(round(ow * ss)))
    edge = np.zeros((H, W), bool)
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy > r * r:
                continue
            sh = np.full((H, W), -1, np.int64)
            ys, yd = (slice(max(dy, 0), H), slice(0, H - max(dy, 0))) if dy >= 0 else (slice(0, H + dy), slice(-dy, H))
            xs, xd = (slice(max(dx, 0), W), slice(0, W - max(dx, 0))) if dx >= 0 else (slice(0, W + dx), slice(-dx, W))
            sh[yd, xd] = fid[ys, xs]
            edge |= covered & (sh != fid)
    col = np.where(edge[..., None], 0.0, cb)
    img = np.where(covered[..., None], col, np.full(3, bg ** 2.2))
    img = np.clip(img, 0, 1) ** (1 / 2.2)
    return (img * 255 + 0.5).astype(np.uint8), gb, covered & ~edge


def render(m, eye, team, W=230, H=440, sc=225.0, ss=3, pz=None):
    from PIL import Image
    pz = G.pose(m.rig) if pz is None else pz
    cam = G.Cam(eye, sc * ss, (0.0, 0.92), W * ss, H * ss)
    img, gb, inner = raster(posed_faces(m, pz, team), cam, ss)
    cov = {g: float((gb[inner] == k).sum()) for k, g in enumerate(GROUPS)}
    return Image.fromarray(img).resize((W, H), Image.LANCZOS), cov


def sheet(cells, title, ncol, cw, ch, labels, sub=None):
    from PIL import Image, ImageDraw
    pad, head, lab = 6, 64 if sub else 44, 20
    rows = (len(cells) + ncol - 1) // ncol
    img = Image.new("RGB", (pad + ncol * (cw + pad), head + rows * (ch + lab + pad)), (40, 42, 46))
    d = ImageDraw.Draw(img)
    d.text((pad + 4, 10), title, fill=(240, 240, 240), font=G.font(22, True))
    if sub:
        d.text((pad + 4, 38), sub, fill=(190, 195, 205), font=G.font(14))
    for k, (c, t) in enumerate(zip(cells, labels)):
        if c is None:
            continue   # (a padding cell)
        x = pad + (k % ncol) * (cw + pad)
        y = head + (k // ncol) * (ch + lab + pad)
        d.text((x + 2, y + 2), t, fill=(225, 228, 235), font=G.font(13, True))
        img.paste(c, (x, y + lab))
    return img


def previews(models, scratch, out):
    saved, cover = [], {}

    def save(img, name):
        p = os.path.join(scratch, name)
        img.save(p, optimize=True)
        if out:
            shutil.copyfile(p, os.path.join(out, name))
        saved.append(os.path.join(out or scratch, name))

    ncol = min(len(models), 11)   # (rows of 11: each group's front row, then its back row)
    groups = [models[k:k + ncol] for k in range(0, len(models), ncol)]
    for team, fname, tname in ((ORANGE, "lineup-outlined.png", "orange"), (BLUE, "lineup-outlined-blue.png", "blue")):
        cells, labels = [], []
        for grp in groups:
            for view in (G.FRONT, G.BACK):
                for m in grp:
                    img, cov = render(m, view, team)
                    cells.append(img)
                    labels.append("%s  %d boxes" % (m.name, len(m.boxes)) if view is G.FRONT else m.name + " (back)")
                    if tname == "orange":
                        acc = cover.setdefault(m.name, {g: 0.0 for g in GROUPS})
                        for g in GROUPS:
                            acc[g] += cov[g]
                if len(grp) < ncol:   # (a short last group: its row padded, so its back row starts a row)
                    for _ in range(ncol - len(grp)):
                        cells.append(None)
                        labels.append("")
        save(sheet(cells, "OUTLINED: the %d player models (front 3/4, back 3/4), %s team" % (len(models), tname), ncol, 230, 440, labels,
                   "box model files, drawn as the game draws boxes: its face shading and 1 px box outlines; every box inside the hitbox"), fname)
    cells, labels = [], []
    for grp in groups:
        for kind in ("run", "walk", "jump"):
            for m in grp:
                pz = G.pose(m.rig, phase=0.25, speed=7.0) if kind == "run" else G.pose(m.rig, phase=0.75, speed=2.6) if kind == "walk" else G.pose(m.rig, air=1.0)
                cells.append(render(m, G.FRONT if kind == "walk" else G.SIDE, ORANGE if kind == "run" else BLUE, pz=pz)[0])
                labels.append(m.name + " " + kind)
            for _ in range(ncol - len(grp)):
                cells.append(None)
                labels.append("")
    save(sheet(cells, "OUTLINED: walking on the game's gait (models.cpp playerPose, joints measured from the boxes)", ncol, 230, 440, labels,
               "each group of rows: running 7 m/s (side 3/4, orange); walking 2.6 m/s (front 3/4, blue); the jump pose"), "walk-outlined.png")
    return saved, cover


# ---------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="/mnt/project-files/player-models/outlined")
    ap.add_argument("--scratch", default="/tmp/quark-players-outlined")
    ap.add_argument("--no-preview", action="store_true")
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    only = [s.strip().upper() for s in args.only.split(",") if s.strip()]
    sdir = os.path.join(args.scratch, "models", "player")
    mdir = os.path.join(args.out, "models", "player") if args.out else ""
    os.makedirs(sdir, exist_ok=True)
    if mdir:
        os.makedirs(mdir, exist_ok=True)
    ok, models = True, []
    for name in NAMES:
        if only and name not in only:
            continue
        m = Model(BUILDERS[name]())
        errs, warns = check(m)
        sp = os.path.join(sdir, name + ".gltf")
        size = write_gltf(m, sp)
        boxes, ntri = read_back(sp)
        if ntri != m.tris() or len(boxes) != len(m.boxes):
            errs.append("%s: read back %d triangles / %d boxes" % (name, ntri, len(boxes)))
        if mdir:
            shutil.copyfile(sp, os.path.join(mdir, name + ".gltf"))
        r = m.rig
        print("%-11s %3d boxes %4d triangles %3d KB  hip %.3f knee %.3f ankle %.3f neck %.3f hipX %.3f" % (
            name, len(m.boxes), m.tris(), size // 1024, r.hipY, r.kneeY, r.ankleY, r.neckY, r.hipX))
        for e in errs:
            print("   ERROR: " + e)
            ok = False
        for w in warns:
            print("   warning: " + w)
        models.append(m)
    if not args.no_preview and models:
        saved, cover = previews(models, args.scratch, args.out)
        print("visible surface (front + back 3/4, outlines left out): dark / mid / light / team / fixed colours")
        for name, c in cover.items():
            t = sum(c.values()) or 1
            print("   %-11s " % name + "  ".join("%s %2.0f%%" % (g, 100 * c[g] / t) for g in GROUPS))
        for p in saved:
            print("wrote " + p)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
