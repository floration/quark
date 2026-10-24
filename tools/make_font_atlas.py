#!/usr/bin/env python3
"""Builds the UI font atlases as signed distance fields. Re-run after changing a font, the table or the constants:

    python3 tools/make_font_atlas.py        (needs Pillow + numpy)

- src/client/font_ui.h: the default UI font, Rubik Bold (assets/fonts/Rubik.ttf, a variable font set to wght 700),
  raw R8 texels: on the GPU at startup with no decoding.
- src/client/font_builtin.h: the other built-in fonts of Settings > FONT (BUILTIN below; Work Sans Bold), each atlas
  as a PNG (stb_image decodes it when the font is picked: a distance field compresses about 5:1, so a font costs the
  binary ~100 KB instead of ~490 KB raw), with its advances and kerning. A row here needs its row in kBuiltinFonts
  (src/client/uifonts.cpp) and the font file + licence in assets/fonts.

Layout: 16x6 cells for ASCII 32..126 (rows 0-5) plus a solid block (used by Renderer::rect) at the start of
row 6 (0-based: the 7th and last row).
Font units match the old 5x7 bitmap font: a line is 8 units tall (Renderer::textHeight = 8 * scale),
the em is 7.5 units, cap tops sit 0.75 units below the line top, the pen origin is ORIGIN units into
the glyph box. Each cell is the 8x8-unit glyph box plus PAD units of padding on every side, so the
distance field (and the shader's outline) reaches past the glyph. Advances (kUiAdv, units) and kerning
pairs (kUiKern, 1/16 units) come from the font. One SDF serves every text size: the shader smooths the
edge over one screen pixel, so small HUD labels and big banners are both crisp."""
import io
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets", "fonts", "Rubik.ttf")   # the default (font_ui.h)
WEIGHT = 700
# The other built-in fonts (font_builtin.h), in menu order: id (kBuiltinFonts' row in src/client/uifonts.cpp), kind,
# file in assets/fonts (with its licence beside it), weight (a variable font's wght; None = a static font), credit.
# kind: "ttf" (sized so its capitals are as tall as Rubik's), "classic" (the original 5x7 font, CLASSIC below, its
# original metrics) or "conchars" (a Quake-style 16x16 sheet of 8x8 cells; capitals sized like Rubik's).
# A row whose file isn't in assets/fonts is left out (its menu row stays hidden): add the file and re-run.
BUILTIN = [
    ("worksans", "ttf", "WorkSans.ttf", 700, "Work Sans Bold (SIL OFL 1.1, assets/fonts/WorkSans-OFL.txt)"),
    ("classic", "classic", None, None, "the original quark 5x7 pixel font"),
    # the closest free matches of game fonts (owner, 2026-10-23); files not downloaded yet: see docs/ASSETS.md
    ("bebasneue", "ttf", "BebasNeue-Regular.ttf", None, "Bebas Neue (SIL OFL 1.1, assets/fonts/BebasNeue-OFL.txt)"),
    ("decalotype", "ttf", "Decalotype-Bold.ttf", None, "Decalotype Bold (SIL OFL 1.1, assets/fonts/Decalotype-OFL.txt)"),
    ("librequake", "conchars", "LibreQuake-CONCHARS.png", None, "LibreQuake CONCHARS (BSD-3-Clause, assets/fonts/LibreQuake-COPYING.txt)"),
    ("dejavusans", "ttf", "DejaVuSans-Bold.ttf", None, "DejaVu Sans Bold (Bitstream Vera licence, assets/fonts/DejaVu-LICENSE.txt)"),
    ("lilitaone", "ttf", "LilitaOne-Regular.ttf", None, "Lilita One (SIL OFL 1.1, assets/fonts/LilitaOne-OFL.txt)"),
    ("jost", "ttf", "Jost.ttf", 600, "Jost SemiBold (SIL OFL 1.1, assets/fonts/Jost-OFL.txt)"),
    ("barlowcondensed", "ttf", "BarlowCondensed-Bold.ttf", None, "Barlow Condensed Bold (SIL OFL 1.1, assets/fonts/BarlowCondensed-OFL.txt)"),
]
TPU = 6            # atlas texels per font unit
BOX = 8            # glyph box (units)
PAD = 1.5          # padding / distance range (units)
ORIGIN = 0.5       # pen origin inside the glyph box (units)
EM = 7.5           # em size (units)
SS = 8             # supersampling of the source raster
CELL = int((BOX + 2 * PAD) * TPU)
SPREAD = PAD * TPU  # distance (texels) mapped to the 0..1 range: 0.5 = glyph edge
W, H = 16 * CELL, 7 * CELL


def sdf(hi, cheb=False):
    """hi: bool array (CELL*SS)^2 -> CELL^2 distance field in 0..1 (0.5 = edge). cheb: the max norm (square corners)."""
    # boundary hi-res pixels (inside with an outside 4-neighbour, and vice versa)
    p = np.pad(hi, 1)
    nb_out = ~p[:-2, 1:-1] | ~p[2:, 1:-1] | ~p[1:-1, :-2] | ~p[1:-1, 2:]
    nb_in = p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
    edge = (hi & nb_out) | (~hi & nb_in)
    ey, ex = np.nonzero(edge)
    ex = (ex + 0.5) / SS
    ey = (ey + 0.5) / SS
    c = (np.arange(CELL) + 0.5)
    gx, gy = np.meshgrid(c, c)
    gx, gy = gx.ravel(), gy.ravel()
    d = np.full(gx.shape, SPREAD * 2.0)
    for i in range(0, len(ex), 512):
        dx = gx[:, None] - ex[None, i:i + 512]
        dy = gy[:, None] - ey[None, i:i + 512]
        dist = np.maximum(np.abs(dx), np.abs(dy)) if cheb else np.sqrt(dx * dx + dy * dy)
        d = np.minimum(d, dist.min(axis=1))
    d = np.maximum(d - 0.5 / SS, 0)  # edge pixel centres sit half a hi-res pixel off the true edge
    # inside/outside from the coverage of the hi-res pixels under each texel
    cov = hi.reshape(CELL, SS, CELL, SS).mean(axis=(1, 3)).ravel()
    signed = np.where(cov >= 0.5, d, -d)
    return np.clip(0.5 + signed / (2 * SPREAD), 0, 1).reshape(CELL, CELL)


CLASSIC = [  # 5x7, one int per row, bit 4 = leftmost; a-z are drawn as A-Z
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x04,0x04,0x04,0x04,0x00,0x00,0x04], [0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00], [0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A],
    [0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04], [0x18,0x19,0x02,0x04,0x08,0x13,0x03], [0x0C,0x12,0x14,0x08,0x15,0x12,0x0D], [0x0C,0x04,0x08,0x00,0x00,0x00,0x00],
    [0x02,0x04,0x08,0x08,0x08,0x04,0x02], [0x08,0x04,0x02,0x02,0x02,0x04,0x08], [0x00,0x04,0x15,0x0E,0x15,0x04,0x00], [0x00,0x04,0x04,0x1F,0x04,0x04,0x00],
    [0x00,0x00,0x00,0x00,0x0C,0x04,0x08], [0x00,0x00,0x00,0x1F,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x0C,0x0C], [0x00,0x01,0x02,0x04,0x08,0x10,0x00],
    [0x0E,0x11,0x13,0x15,0x19,0x11,0x0E], [0x04,0x0C,0x04,0x04,0x04,0x04,0x0E], [0x0E,0x11,0x01,0x02,0x04,0x08,0x1F], [0x1F,0x02,0x04,0x02,0x01,0x11,0x0E],
    [0x02,0x06,0x0A,0x12,0x1F,0x02,0x02], [0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E], [0x06,0x08,0x10,0x1E,0x11,0x11,0x0E], [0x1F,0x01,0x02,0x04,0x08,0x08,0x08],
    [0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E], [0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C], [0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00], [0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08],
    [0x02,0x04,0x08,0x10,0x08,0x04,0x02], [0x00,0x00,0x1F,0x00,0x1F,0x00,0x00], [0x08,0x04,0x02,0x01,0x02,0x04,0x08], [0x0E,0x11,0x01,0x02,0x04,0x00,0x04],
    [0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E], [0x0E,0x11,0x11,0x11,0x1F,0x11,0x11], [0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E], [0x0E,0x11,0x10,0x10,0x10,0x11,0x0E],
    [0x1C,0x12,0x11,0x11,0x11,0x12,0x1C], [0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F], [0x1F,0x10,0x10,0x1E,0x10,0x10,0x10], [0x0E,0x11,0x10,0x17,0x11,0x11,0x0F],
    [0x11,0x11,0x11,0x1F,0x11,0x11,0x11], [0x0E,0x04,0x04,0x04,0x04,0x04,0x0E], [0x07,0x02,0x02,0x02,0x02,0x12,0x0C], [0x11,0x12,0x14,0x18,0x14,0x12,0x11],
    [0x10,0x10,0x10,0x10,0x10,0x10,0x1F], [0x11,0x1B,0x15,0x15,0x11,0x11,0x11], [0x11,0x11,0x19,0x15,0x13,0x11,0x11], [0x0E,0x11,0x11,0x11,0x11,0x11,0x0E],
    [0x1E,0x11,0x11,0x1E,0x10,0x10,0x10], [0x0E,0x11,0x11,0x11,0x15,0x12,0x0D], [0x1E,0x11,0x11,0x1E,0x14,0x12,0x11], [0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E],
    [0x1F,0x04,0x04,0x04,0x04,0x04,0x04], [0x11,0x11,0x11,0x11,0x11,0x11,0x0E], [0x11,0x11,0x11,0x11,0x11,0x0A,0x04], [0x11,0x11,0x11,0x15,0x15,0x15,0x0A],
    [0x11,0x11,0x0A,0x04,0x0A,0x11,0x11], [0x11,0x11,0x11,0x0A,0x04,0x04,0x04], [0x1F,0x01,0x02,0x04,0x08,0x10,0x1F], [0x0E,0x08,0x08,0x08,0x08,0x08,0x0E],
    [0x00,0x10,0x08,0x04,0x02,0x01,0x00], [0x0E,0x02,0x02,0x02,0x02,0x02,0x0E], [0x04,0x0A,0x11,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x1F],
    [0x08,0x04,0x02,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x00,0x00,0x00,0x00,0x00,0x00,0x00], [0x02,0x04,0x04,0x08,0x04,0x04,0x02],
    [0x04,0x04,0x04,0x04,0x04,0x04,0x04], [0x08,0x04,0x04,0x02,0x04,0x04,0x08], [0x00,0x00,0x08,0x15,0x02,0x00,0x00],
]


def cap_units(path, weight):
    """The font's cap height (H) in units at EM."""
    font = ImageFont.truetype(path, int(EM * TPU * SS))
    if weight is not None:
        font.set_variation_by_axes([weight])
    b = font.getbbox("H")
    return (b[3] - b[1]) / (TPU * SS)


def bake_ttf(path, weight, em=EM):
    """A TrueType / OpenType font -> (atlas uint8 HxW, advances[95] in units, kerning[95*95] in 1/16 units)."""
    font = ImageFont.truetype(path, int(round(em * TPU * SS)))
    if weight is not None:
        font.set_variation_by_axes([weight])
    px_per_unit = TPU * SS
    cap_top = font.getbbox("H")[1]
    atlas = np.zeros((H, W), np.uint8)
    adv = []
    for g in range(95):
        ch = chr(32 + g)
        adv.append(font.getlength(ch) / px_per_unit)
        if ch == " ":
            continue
        img = Image.new("L", (CELL * SS, CELL * SS), 0)
        ox = (PAD + ORIGIN) * px_per_unit
        oy = (PAD + 0.75) * px_per_unit - cap_top
        ImageDraw.Draw(img).text((ox, oy), ch, font=font, fill=255)
        hi = np.asarray(img) >= 128
        if hi[:, -1].any() or hi[-1, :].any() or hi[0, :].any():
            print(f"  warning: {os.path.basename(path)} {ch!r} reaches past its atlas cell")
        cx, cy = (g % 16) * CELL, (g // 16) * CELL
        atlas[cy:cy + CELL, cx:cx + CELL] = np.round(sdf(hi) * 255).astype(np.uint8)
    atlas[6 * CELL:7 * CELL, 0:CELL] = 255  # solid block
    kern = []
    for a in range(95):
        for b in range(95):
            pa, pb = chr(32 + a), chr(32 + b)
            k = (font.getlength(pa + pb) - font.getlength(pa) - font.getlength(pb)) / px_per_unit
            kern.append(max(-127, min(127, round(k * 16))))
    return atlas, adv, kern


def bake_bitmap(glyphs, unit, top, advance):
    """A pixel font -> (atlas, advances, None). glyphs: 95 bool arrays (rows x cols, True = ink) for ASCII 32..126;
    unit: font units per font pixel; top: units from the line top to the glyph's first row; advance: units per glyph.
    The distance field uses the max (Chebyshev) norm, so the edges stay square at every size and the outline is a
    square ring like the old 3x3 one."""
    px_per_unit = TPU * SS
    atlas = np.zeros((H, W), np.uint8)
    for g in range(95):
        gl = glyphs[g]
        if not gl.any():
            continue
        hi = np.zeros((CELL * SS, CELL * SS), bool)
        for r, c in zip(*np.nonzero(gl)):
            x0 = int(round((PAD + ORIGIN + c * unit) * px_per_unit))
            x1 = int(round((PAD + ORIGIN + (c + 1) * unit) * px_per_unit))
            y0 = int(round((PAD + top + r * unit) * px_per_unit))
            y1 = int(round((PAD + top + (r + 1) * unit) * px_per_unit))
            hi[y0:y1, x0:x1] = True
        cx, cy = (g % 16) * CELL, (g // 16) * CELL
        atlas[cy:cy + CELL, cx:cx + CELL] = np.round(sdf(hi, cheb=True) * 255).astype(np.uint8)
    atlas[6 * CELL:7 * CELL, 0:CELL] = 255
    return atlas, [advance] * 95, None


def classic_glyphs():
    """QUARK CLASSIC: the game's original 5x7 font (before 8678b00), lower case drawn as upper case."""
    out = []
    for g in range(95):
        src = g - 32 if 97 <= g + 32 <= 122 else g   # a-z -> A-Z
        out.append(np.array([[(row >> (4 - b)) & 1 for b in range(5)] for row in CLASSIC[src]], bool))
    return out


def conchars_glyphs(path):
    """A Quake-style console font sheet: 16x16 cells, character n at cell n; ink = any non-zero (non-transparent)
    pixel. Returns (glyphs, cell size, rows of 'H' (top, height))."""
    img = Image.open(path)
    if img.mode == "P" or img.mode == "L":
        ink = np.asarray(img) != 0
    else:
        a = np.asarray(img.convert("RGBA"))
        ink = (a[:, :, 3] > 127) & (a[:, :, :3].max(axis=2) > 0)
    cell = ink.shape[1] // 16
    glyphs = [ink[(c // 16) * cell:(c // 16 + 1) * cell, (c % 16) * cell:(c % 16 + 1) * cell] for c in range(32, 127)]
    hrows = np.nonzero(glyphs[ord("H") - 32].any(axis=1))[0]
    return glyphs, cell, (int(hrows[0]), int(hrows[-1] - hrows[0] + 1))


def write_array(f, name, ctype, data, per_line=40):
    f.write(f"static const {ctype} {name}[] = {{\n")
    for i in range(0, len(data), per_line):
        f.write(",".join(str(v) for v in data[i:i + per_line]) + ",\n")
    f.write("};\n")


def main():
    atlas, adv, kern = bake_ttf(FONT, WEIGHT)
    cap = cap_units(FONT, WEIGHT)   # every other built-in is sized so its capitals are this tall
    out = os.path.join(ROOT, "src", "client", "font_ui.h")
    with open(out, "w") as f:
        f.write("// Generated by tools/make_font_atlas.py - do not edit. Rubik Bold (SIL OFL 1.1,\n"
                "// assets/fonts/Rubik-OFL.txt) as a signed distance field, R8, 0.5 = glyph edge.\n#pragma once\n")
        f.write(f"constexpr int kUiTexW = {W}, kUiTexH = {H}, kUiCell = {CELL};\n")
        cf = lambda v: f"{float(v)!r}f"   # a C++ float literal whether the constant is an int or a float (6 -> 6.0f)
        f.write(f"constexpr float kUiTpu = {cf(TPU)}, kUiPad = {cf(PAD)}, kUiOrigin = {cf(ORIGIN)}, kUiSpread = {cf(SPREAD)};\n")
        f.write(f"constexpr float kUiCapHeight = {cap:.4f}f;   // Rubik Bold's capitals (units): user fonts are sized to match\n")
        write_array(f, "kUiAtlas", "unsigned char", atlas.tobytes())
        f.write("static const float kUiAdv[95] = {" + ",".join(f"{v:.4f}f" for v in adv) + "};\n")
        f.write("static const signed char kUiKern[95 * 95] = {" + ",".join(str(v) for v in kern) + "};\n")
    if os.path.isdir(os.path.join(ROOT, "build")):
        Image.fromarray(atlas).save(os.path.join(ROOT, "build", "font_ui_atlas.png"))
    print(f"{out}: {W}x{H}, caps {cap:.2f} units")

    out2 = os.path.join(ROOT, "src", "client", "font_builtin.h")
    rows = []
    with open(out2, "w") as f:
        f.write("// Generated by tools/make_font_atlas.py - do not edit. The built-in fonts of Settings > FONT other than\n"
                "// the default (font_ui.h): each one's distance-field atlas as a PNG (font_ui.h's layout), its advances\n"
                "// (units) and kerning (1/16 units; null: none).\n#pragma once\n")
        f.write("struct BakedFont { const char* id; const unsigned char* png; unsigned pngSize; const float* adv; const signed char* kern; };\n")
        for fid, kind, file, weight, credit in BUILTIN:
            src = os.path.join(ROOT, "assets", "fonts", file) if file else None
            if src and not os.path.isfile(src):
                print(f"{out2}: {fid}: assets/fonts/{file} isn't there - left out (its menu row stays hidden)")
                continue
            if kind == "ttf":
                em = EM * cap / cap_units(src, weight)
                a, ad, kn = bake_ttf(src, weight, em)
            elif kind == "classic":   # the original metrics: 1 unit per pixel, from the line top, 6 units a glyph
                a, ad, kn = bake_bitmap(classic_glyphs(), 1.0, 0.0, 6.0)
            elif kind == "conchars":  # sized so 'H' is as tall as Rubik's capitals, its top where theirs is
                glyphs, cellpx, (htop, hh) = conchars_glyphs(src)
                unit = cap / hh
                a, ad, kn = bake_bitmap(glyphs, unit, 0.75 - htop * unit, cellpx * unit)
            else:
                raise SystemExit(f"{fid}: unknown kind {kind}")
            buf = io.BytesIO()
            Image.fromarray(a).save(buf, "PNG", optimize=True)
            png = buf.getvalue()
            f.write(f"// {fid}: {credit}\n")
            write_array(f, f"kBaked_{fid}_png", "unsigned char", png)
            f.write(f"static const float kBaked_{fid}_adv[95] = {{" + ",".join(f"{v:.4f}f" for v in ad) + "};\n")
            kname = "nullptr"
            if kn is not None and any(kn):
                f.write(f"static const signed char kBaked_{fid}_kern[95 * 95] = {{" + ",".join(str(v) for v in kn) + "};\n")
                kname = f"kBaked_{fid}_kern"
            rows.append(f'{{"{fid}", kBaked_{fid}_png, {len(png)}u, kBaked_{fid}_adv, {kname}}}')
            print(f"{out2}: {fid} {len(png)} bytes of PNG")
        f.write("static const BakedFont kBakedFonts[] = {\n    " + "".join(r + ",\n    " for r in rows) + "{nullptr, nullptr, 0, nullptr, nullptr}};\n")


if __name__ == "__main__":
    main()
