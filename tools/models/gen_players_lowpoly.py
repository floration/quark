#!/usr/bin/env python3
"""The LOW-POLY player set (owner, 2026-11-10: "we need to redo the player models because they are all blocky
instead of low poly"): each box design of the OUTLINED set (characters_outlined*.py) remade as faceted, shaped
meshes with an inverted-hull ink outline, in the style of the low-poly weapons (lowpoly.py), split into the walking
joints' anim_<bone> nodes (docs/ASSETS.md "Player mesh parts").

    python3 tools/models/gen_players_lowpoly.py [--out DIR] [--scratch DIR] [--no-preview] [--only NAME,...]

The characters are tools/models/players_lowpoly/<name>.py (build() -> kit.Char, DESC); the shared body and props are
players_lowpoly/kit.py; the contract for writing one is players_lowpoly/README.md. A name of kPlayerModelNames
without a module yet is skipped (printed). Writes <out>/models/player/<NAME>.gltf (default out:
/mnt/project-files/player-models/lowpoly) and the previews lineup-lowpoly.png (orange), lineup-lowpoly-blue.png,
walk-lowpoly.png and box-vs-lowpoly.png, rendered in --scratch first and copied over (a network mount). Not shipped
yet: nothing here writes assets/overrides.

Checks (ERROR exits 1, warning is printed), all on what the written file reads back as:
- at most 1000 triangles a model, the ink hulls included;
- the size rule (owner, 2026-11-09: "+-10%, in any dimension"): every vertex (and every edge's crossing of the neck
  line) inside the hitbox grown 10% in radius (body cylinder r 0.44 m under the neck line 1.44 m, head cylinder
  r 0.198 m above it); the extents under the neck (X, Z: the 0.8 m diameter; Y: 1.44 m) and above it (X, Z: 0.36 m;
  height above the neck: 0.36 m) at most 110%; X and Y under 90% fail, Z (depth) under 90% warns;
- the nine walking nodes with the rig's pivots (thighs and shins both sides, or the game stands it still);
- colours: every non-exempt material at least sRGB 0.18 (fail); the visibility rules (player-visibility README,
  warnings): team on at least 35% of the front and back view, under 10% of the view in a non-team colour darker than
  sRGB 0.35 and under 25% darker than 0.25 (ink left out), the outline 6-15 mm, limbs at least 120 mm thick, and on a
  dark and a bright backdrop mean dE76 >= 35 against it and mean C* >= 20 (the previews' own light: a rough guide;
  the game's lineup is the judge);
- deterministic: the same input writes the same bytes. Provenance: AI-made placeholders (lowpoly.write_gltf).
"""
import argparse
import base64
import importlib
import json
import math
import os
import shutil
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lowpoly as LP  # noqa: E402
import gen_players_realistic_dark as G  # noqa: E402
import gen_players_outlined as O  # noqa: E402
from players_lowpoly import kit  # noqa: E402

MAX_TRIS = 1000
SIZE_MAX, SIZE_MIN = 1.10, 0.90
R_BODY, R_HEAD = G.R_BODY * 1000, G.R_HEAD * 1000          # mm
NECK, HEIGHT = G.Y_NECK * 1000, G.HEIGHT * 1000
TEAM_MIN, DARK35_MAX, DARK25_MAX = 0.35, 0.10, 0.25
OUTLINE_RANGE = (6.0, 15.0)
LIMB_MIN = 120.0
DE_MIN, C_MIN = 35.0, 20.0
BACKDROPS = {"dark": (0.16, 0.16, 0.17), "bright": (0.70, 0.70, 0.68)}   # sRGB
ORANGE, BLUE = O.ORANGE, O.BLUE
LUM = np.array((0.2126, 0.7152, 0.0722))


def modules(only):
    """(name, module or None) in the game's order"""
    out = []
    for name in O.NAMES:
        if only and name not in only:
            continue
        p = os.path.join(HERE, "players_lowpoly", name.lower() + ".py")
        out.append((name, importlib.import_module("players_lowpoly." + name.lower()) if os.path.exists(p) else None))
    return out


# ---- the file -------------------------------------------------------------------------------------------------------

def write(ch, path):
    tones = {kit.material(t): kit.tone_color(t) for t in ch.tones}
    tones["ink"] = LP.TONES["ink"]
    r = ch.rig
    extras = {"style": "lowpoly", "player": ch.name, "description": ch.desc, "outline_mm": ch.outline,
              "rig": {"hipY": r.hip / 1000, "hipX": r.hipx / 1000, "kneeY": r.knee / 1000, "ankleY": r.ankle / 1000,
                      "neckY": r.neck / 1000, "waistY": r.waist / 1000}}
    return LP.write_gltf(path, ch.name, ch.nodes(), extras, tones=tones, generator="gen_players_lowpoly.py")


def read_back(path):
    """[(node name, pivot mm, [(material, P mm (n,3) in model space, standing)])]"""
    with open(path) as f:
        doc = json.load(f)
    raw = base64.b64decode(doc["buffers"][0]["uri"].split(",", 1)[1])
    out = []
    for nd in doc["nodes"]:
        t = np.array(nd.get("translation", [0, 0, 0]), float) * 1000
        prims = []
        for pr in doc["meshes"][nd["mesh"]]["primitives"]:
            a = doc["accessors"][pr["attributes"]["POSITION"]]
            v = doc["bufferViews"][a["bufferView"]]
            P = np.frombuffer(raw, "<f4", a["count"] * 3, v.get("byteOffset", 0)).reshape(-1, 3).astype(float) * 1000 + t
            prims.append((doc["materials"][pr["material"]]["name"], P))
        out.append((nd["name"], t, prims))
    return doc, out


# ---- checks ---------------------------------------------------------------------------------------------------------

def size_check(P):
    """P: every vertex (n, 3) mm, triangles in rows of 3. -> (errors, warnings, report)"""
    errs, warns = [], []
    T = P.reshape(-1, 3, 3)
    # where each edge crosses the neck line (the hitbox steps there)
    E = np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]])
    a, b = E[:, 0], E[:, 1]
    cross = (a[:, 1] - NECK) * (b[:, 1] - NECK) < 0
    t = (NECK - a[cross, 1]) / (b[cross, 1] - a[cross, 1])
    X = a[cross] + (b[cross] - a[cross]) * t[:, None]
    X[:, 1] = NECK
    allp = np.concatenate([P, X])
    r = np.hypot(allp[:, 0], allp[:, 2])
    y = allp[:, 1]
    head = y >= NECK
    bad = np.where(head, r > R_HEAD * SIZE_MAX + 1e-6, r > R_BODY * SIZE_MAX + 1e-6) | (y < -0.5)
    if bad.any():
        q = allp[bad][0]
        errs.append("%d points past the hitbox + 10%%, e.g. (%.0f, %.0f, %.0f) mm, r %.0f" % (bad.sum(), q[0], q[1], q[2], math.hypot(q[0], q[2])))
    rep = {}
    for reg, sel, dia, hgt, h0 in (("body", ~head | (y == NECK), 2 * R_BODY, NECK, None), ("head", head, 2 * R_HEAD, HEIGHT - NECK, NECK)):
        Q = allp[sel]
        if not len(Q):
            errs.append("nothing in the %s's region" % reg)
            continue
        ex = Q[:, 0].max() - Q[:, 0].min()
        ez = Q[:, 2].max() - Q[:, 2].min()
        ey = Q[:, 1].max() - (Q[:, 1].min() if h0 is None else h0)
        f = {"X": ex / dia, "Z": ez / dia, "Y": ey / hgt}
        rep[reg] = f
        for k, v in f.items():
            if v > SIZE_MAX + 1e-9:
                errs.append("%s %s extent %.0f%% of the hitbox (at most 110%%)" % (reg, k, 100 * v))
            elif v < SIZE_MIN - 1e-9:
                (warns if k == "Z" else errs).append("%s %s extent %.0f%% of the hitbox (at least 90%%%s)" % (reg, k, 100 * v, ", depth: a warning" if k == "Z" else ""))
    return errs, warns, rep


def check(ch, path):
    errs, warns = [], []
    doc, nodes = read_back(path)
    P = np.concatenate([p for _, _, prims in nodes for _, p in prims])
    ntri = len(P) // 3
    if ntri != ch.tris():
        errs.append("read back %d triangles, built %d" % (ntri, ch.tris()))
    if ntri > MAX_TRIS:
        errs.append("%d triangles (at most %d, the outline included)" % (ntri, MAX_TRIS))
    e, w, rep = size_check(P)
    errs += e
    warns += w
    have = {n: t for n, t, prims in nodes if prims}
    for b in kit.BONES:
        if "anim_" + b not in have:
            errs.append("nothing on %s (it must walk)" % b)
        elif np.abs(have["anim_" + b] - np.array(ch.rig.pivot(b))).max() > 0.01:
            errs.append("anim_%s's pivot is not the rig's" % b)
    if ch.rig.pivot("thigh_l")[0] >= 0:
        errs.append("thigh_l must be at -X (the model's left)")
    for t in sorted(ch.tones):
        lum = float(np.array(kit.tone_color(t)) @ LUM) ** (1 / 2.2)
        if not kit.exempt(t) and lum < 0.18:
            errs.append("tone %s: sRGB luminance %.2f (at least 0.18)" % (t, lum))
        team_tone = t in O.TONES and O.TONES[t][0] == "team"
        if kit.material(t).lower().startswith("team") != team_tone:   # (gltf.cpp tintSlotFromName: by the name's prefix)
            errs.append("tone %s: its material %s gets the wrong tint slot" % (t, kit.material(t)))
    if not OUTLINE_RANGE[0] <= ch.outline <= OUTLINE_RANGE[1]:
        warns.append("outline %.1f mm (the visibility rule: 6-15 mm)" % ch.outline)
    for lab, seg in ch.limbs:
        if seg.width() < LIMB_MIN - 1e-6:
            warns.append("%s %.0f mm thick at its thinnest (at least %d)" % (lab, seg.width(), LIMB_MIN))
    return errs, warns, rep, ntri


# ---- previews: a small rasterizer, the game's model light (as gen_players_outlined's), material ids per pixel ------

SUN = G._norm((0.45, 0.80, -0.35))


def posed(ch, pz):
    """[(P (n,3,3) m posed, material, tone)] from the built meshes"""
    out = []
    t2m = {kit.material(t): t for t in ch.tones}
    t2m["ink"] = "ink"
    for b in kit.BONES:
        M = pz[b]
        for mat, tris in sorted(ch.mesh[b].tris.items()):
            P = np.array(tris, float) / 1000
            out.append((P @ M[:3, :3].T + M[:3, 3], mat, t2m[mat]))
    return out


def colour_of(tone, team):
    if tone == "ink":
        return np.array(LP.TONES["ink"])
    c = np.array(kit.tone_color(tone))
    return c * team if kit.material(tone).startswith("team") else c


def raster(items, cam, team, bg):
    W, H = cam.W, cam.H
    depth = np.full((H, W), -np.inf)
    cb = np.zeros((H, W, 3))
    mb = np.full((H, W), -1, np.int32)
    tones = []
    for P, mat, tone in items:
        tones.append(tone)
        ti = len(tones) - 1
        base = colour_of(tone, team)
        S, D = cam.project(P.reshape(-1, 3))
        S, D = S.reshape(-1, 3, 2), D.reshape(-1, 3)
        fn = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
        L = np.linalg.norm(fn, axis=1)
        for t in np.nonzero(fn @ (-cam.f) > 1e-12)[0]:
            n = fn[t] / L[t]
            light = 0.5 * (0.6 + 0.25 * n[1]) + 0.85 * max(float(n @ SUN), 0.0)
            col = base * (light * 0.4 + 0.6)
            (x0, y0), (x1, y1), (x2, y2) = S[t]
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
            z = w0 * D[t, 0] + w1 * D[t, 1] + w2 * D[t, 2]
            sub = depth[by0:by1 + 1, bx0:bx1 + 1]
            msk &= z > sub
            if not msk.any():
                continue
            sub[msk] = z[msk]
            cb[by0:by1 + 1, bx0:bx1 + 1][msk] = col
            mb[by0:by1 + 1, bx0:bx1 + 1][msk] = ti
    cov = mb >= 0
    img = np.where(cov[..., None], cb, np.array([c ** 2.2 for c in bg]))
    img = np.clip(img, 0, 1) ** (1 / 2.2)
    return (img * 255 + 0.5).astype(np.uint8), mb, tones


def render(ch, eye, team, W=240, H=470, sc=230.0, ss=3, pz=None, bg=(0.5, 0.5, 0.5)):
    from PIL import Image
    pz = G.pose(grig(ch)) if pz is None else pz
    cam = G.Cam(eye, sc * ss, (0.0, 0.93), W * ss, H * ss)
    img, mb, tones = raster(posed(ch, pz), cam, team, bg)
    return Image.fromarray(img).resize((W, H), Image.LANCZOS), img, mb, tones


def grig(ch):
    r = ch.rig
    return G.Rig(hipY=r.hip / 1000, hipX=r.hipx / 1000, kneeY=r.knee / 1000, ankleY=r.ankle / 1000, neckY=r.neck / 1000, waistY=r.waist / 1000)


def lab(srgb):
    """sRGB 0-1 (..., 3) -> CIELAB (D65)"""
    c = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def visibility(ch):
    """the view-based rules: team share, dark shares (front + back 3/4, orange), dE / C* on two backdrops"""
    team = dark35 = dark25 = tot = 0.0
    contrast = {}
    for eye in (G.FRONT, G.BACK):
        _, img, mb, tones = render(ch, eye, ORANGE, W=120, H=235, sc=115.0, ss=1)
        tt = np.array(tones + ["ink"])
        m = tt[np.where(mb >= 0, mb, len(tones))]
        vis = (mb >= 0) & (m != "ink")
        tot += vis.sum()
        for t in set(tones) - {"ink"}:
            n = float((vis & (m == t)).sum())
            if kit.material(t).startswith("team"):
                team += n
                continue
            lum = float(np.array(kit.tone_color(t)) @ LUM) ** (1 / 2.2)
            dark35 += n if lum < 0.35 else 0
            dark25 += n if lum < 0.25 else 0
    for name, bg in BACKDROPS.items():
        for tname, team_c in (("orange", ORANGE), ("blue", BLUE)):
            de = cs = 0.0
            for eye in (G.FRONT, G.BACK):
                _, img, mb, _ = render(ch, eye, team_c, W=120, H=235, sc=115.0, ss=1, bg=bg)
                L = lab(img[mb >= 0] / 255.0)
                Lb = lab(np.array(bg))
                de += float(np.linalg.norm(L - Lb, axis=1).mean()) / 2
                cs += float(np.hypot(L[:, 1], L[:, 2]).mean()) / 2
            contrast[name + " " + tname] = (de, cs)
    tot = tot or 1
    return team / tot, dark35 / tot, dark25 / tot, contrast


def sheet(cells, title, ncol, cw, ch_, labels, sub=None):
    from PIL import Image, ImageDraw
    pad, top, lab_ = 8, 64 if sub else 44, 22
    rows = (len(cells) + ncol - 1) // ncol
    img = Image.new("RGB", (pad + ncol * (cw + pad), top + rows * (ch_ + lab_ + pad)), (40, 42, 46))
    d = ImageDraw.Draw(img)
    d.text((pad + 4, 10), title, fill=(240, 240, 240), font=G.font(22, True))
    if sub:
        d.text((pad + 4, 38), sub, fill=(190, 195, 205), font=G.font(14))
    for k, (c, t) in enumerate(zip(cells, labels)):
        if c is None:
            continue
        x = pad + (k % ncol) * (cw + pad)
        y = top + (k // ncol) * (ch_ + lab_ + pad)
        d.text((x + 2, y + 3), t, fill=(225, 228, 235), font=G.font(13, True))
        img.paste(c, (x, y + lab_))
    return img


def previews(chars, scratch, out):
    saved = []

    def save(img, name):
        p = os.path.join(scratch, name)
        img.save(p, optimize=True)
        if out:
            shutil.copyfile(p, os.path.join(out, name))
        saved.append(os.path.join(out or scratch, name))

    n = len(chars)
    for team, fname, tname in ((ORANGE, "lineup-lowpoly.png", "orange"), (BLUE, "lineup-lowpoly-blue.png", "blue")):
        cells, labels = [], []
        for view, vn in ((G.FRONT, ""), (G.BACK, " (back)")):
            for ch in chars:
                cells.append(render(ch, view, team)[0])
                labels.append("%s%s  %d tris" % (ch.name, vn, ch.tris()) if not vn else ch.name + vn)
        save(sheet(cells, "LOW-POLY players: front 3/4 and back 3/4, %s team" % tname, n, 240, 470, labels,
                   "faceted meshes, %s mm ink hull; the previews' light is the box previews' (the game's is the judge)" % "/".join(sorted({"%g" % c.outline for c in chars}))), fname)
    cells, labels = [], []
    for kind in ("run", "walk", "jump"):
        for ch in chars:
            r = grig(ch)
            pz = G.pose(r, phase=0.25, speed=7.0) if kind == "run" else G.pose(r, phase=0.75, speed=2.6) if kind == "walk" else G.pose(r, air=1.0)
            cells.append(render(ch, G.FRONT if kind == "walk" else G.SIDE, ORANGE if kind == "run" else BLUE, pz=pz)[0])
            labels.append(ch.name + " " + kind)
    save(sheet(cells, "LOW-POLY players walking (the game's gait, models.cpp playerPose)", n, 240, 470, labels,
               "rows: running 7 m/s (side, orange); walking 2.6 m/s (front 3/4, blue); the jump pose"), "walk-lowpoly.png")
    # box (the shipped OUTLINED design) beside its low-poly remake
    cells, labels = [], []
    for ch in chars:
        bm = O.Model(O.BUILDERS[ch.name]())
        for view in (G.FRONT, G.BACK):
            cells.append(O.render(bm, view, ORANGE, W=240, H=470, sc=230.0)[0])
            labels.append(ch.name + " box" + (" (back)" if view is G.BACK else ""))
            cells.append(render(ch, view, ORANGE)[0])
            labels.append(ch.name + " low-poly" + (" (back)" if view is G.BACK else ""))
    save(sheet(cells, "BOX (shipped, outlined) vs LOW-POLY", 4, 240, 470, labels, "each row: box front, low-poly front, box back, low-poly back (orange team)"),
         "box-vs-lowpoly.png")
    return saved


# -------------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="/mnt/project-files/player-models/lowpoly/")
    ap.add_argument("--scratch", default="/tmp/quark-players-lowpoly")
    ap.add_argument("--no-preview", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--outline", type=float, default=None, help="try another hull thickness (mm) on every character that uses the kit's")
    args = ap.parse_args()
    if args.outline is not None:
        kit.OUTLINE = args.outline
    only = [s.strip().upper() for s in args.only.split(",") if s.strip()]
    sdir = os.path.join(args.scratch, "models", "player")
    mdir = os.path.join(args.out, "models", "player") if args.out else ""
    os.makedirs(sdir, exist_ok=True)
    if mdir:
        os.makedirs(mdir, exist_ok=True)
    ok, chars, skipped = True, [], []
    for name, mod in modules(only):
        if mod is None:
            skipped.append(name)
            continue
        ch = mod.build()
        if ch.name != name:
            print("   ERROR: %s.build() made %s" % (name.lower(), ch.name))
            ok = False
            continue
        sp = os.path.join(sdir, name + ".gltf")
        write(ch, sp)
        errs, warns, rep, ntri = check(ch, sp)
        if not args.no_preview:
            team, d35, d25, con = visibility(ch)
            if team < TEAM_MIN:
                warns.append("team colour on %.0f%% of the front + back view (at least %.0f%%, aim 40%%)" % (100 * team, 100 * TEAM_MIN))
            if d35 > DARK35_MAX:
                warns.append("%.0f%% of the view in non-team colours darker than sRGB 0.35 (keep big areas lighter)" % (100 * d35))
            if d25 > DARK25_MAX:
                warns.append("%.0f%% of the view darker than sRGB 0.25 (at most 25%%)" % (100 * d25))
            for k, (de, cs) in con.items():
                if de < DE_MIN or cs < C_MIN:
                    warns.append("on the %s backdrop: mean dE %.0f, C* %.0f (targets %d, %d)" % (k, de, cs, DE_MIN, C_MIN))
        if mdir:
            shutil.copyfile(sp, os.path.join(mdir, name + ".gltf"))
        size = os.path.getsize(sp)
        r = ch.rig
        b, h = rep.get("body", {}), rep.get("head", {})
        print("%-10s %4d tris %3d KB  outline %g mm  size body X %3.0f%% Z %3.0f%% Y %3.0f%%  head X %3.0f%% Z %3.0f%% Y %3.0f%%  hip %.0f knee %.0f ankle %.0f neck %.0f hipX %.0f" % (
            name, ntri, size // 1024, ch.outline, 100 * b.get("X", 0), 100 * b.get("Z", 0), 100 * b.get("Y", 0), 100 * h.get("X", 0), 100 * h.get("Z", 0),
            100 * h.get("Y", 0), r.hip, r.knee, r.ankle, r.neck, r.hipx))
        if not args.no_preview:
            print("           team %.0f%%  dark<0.35 %.0f%%  dark<0.25 %.0f%%  " % (100 * team, 100 * d35, 100 * d25) +
                  "  ".join("%s dE %.0f C* %.0f" % (k, de, cs) for k, (de, cs) in con.items()))
        for e in errs:
            print("   ERROR: " + e)
            ok = False
        for w in warns:
            print("   warning: " + w)
        chars.append(ch)
    if skipped:
        print("skipped (no players_lowpoly module yet): %d: %s" % (len(skipped), " ".join(skipped)))
    if not args.no_preview and chars:
        for p in previews(chars, args.scratch, args.out):
            print("wrote " + p)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
