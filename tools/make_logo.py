#!/usr/bin/env python3
"""Bakes the QUARK title logo: the word in Rubik ExtraBold (wght 800) drawn like the app icon (tools/make_icon.py): the Q
split cyan / red along a slanted line with a black seam, every letter on a square black outline. Re-run after changing
the font or the look:

    python3 tools/make_logo.py        (needs Pillow + numpy)

Writes src/client/logo_data.h: one RGBA PNG holding two layers of the same rectangle, one above the other:
  layer A (top):    the outline (black) and the split Q (its fixed colours, make_icon.py's CY / RD)
  layer B (bottom): the fill of "UARK" in white, which the game tints with the friendly colour (cfg color_friendly)
The game draws A, then B tinted, as two textured quads in the 2D batch (Renderer::logo): no extra pass or draw call.
It's baked at the size it has at 2160p and drawn smaller with mipmaps (linear filtering), so it's crisp at every size.

Sizes (docs: ui-redesign-spec "Title logo"): the em is the game text's em (7.5 units) at text scale 8 (the title,
labelCentered 4.0 x the UI's 2) times 0.66 / 0.70 (Rubik's cap height matched to the old UI font's), times s = H / 720;
outline = a square dilation by 18/844 em (make_icon.py's MaxFilter(37) at an 844 px em), seam 12/844 em. The split
line runs through (pen x + the Q's advance / 2, the middle of the Q's ink box), 9.09 degrees off vertical, top leaning
right: x = cx - 0.16 (y - cy). Drawn at 4x and box-filtered down (premultiplied), so edges are antialiased."""
import io
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets", "fonts", "Rubik.ttf")
OUT = os.path.join(ROOT, "src", "client", "logo_data.h")
TEXT = "QUARK"
CY, RD = (64, 217, 255), (255, 77, 64)
EM = 7.5 * 8 * 3 * (0.66 / 0.70)   # the em at 2160p (s = 3), px
SS = 4                              # supersampling
OUTLINE, SEAM, TILT = 18 / 844, 12 / 844, 0.16


def font(px):
    f = ImageFont.truetype(FONT, int(round(px)))
    f.set_variation_by_axes([800])
    return f


def dilate(a, r):
    """Square (max-norm) dilation of a float image by r pixels: a max over shifts, x then y."""
    out = a.copy()
    for axis in (1, 0):
        src, m = out, out.copy()
        for d in range(1, r + 1):
            m[(slice(None),) * axis + (slice(d, None),)] = np.maximum(m[(slice(None),) * axis + (slice(d, None),)], src[(slice(None),) * axis + (slice(None, -d),)])
            m[(slice(None),) * axis + (slice(None, -d),)] = np.maximum(m[(slice(None),) * axis + (slice(None, -d),)], src[(slice(None),) * axis + (slice(d, None),)])
        out = m
    return out


def main():
    P = EM * SS
    f = font(P)
    R = int(round(OUTLINE * P))
    adv = f.getlength(TEXT)
    asc, desc = f.getmetrics()
    l, t, r, b = f.getbbox(TEXT, anchor="ls")   # ink box around the pen origin on the baseline
    pad = R + 4 * SS
    ox = pad - l
    oy = pad - t
    W = int(np.ceil(ox + max(r, adv) + pad))
    H = int(np.ceil(oy + b + pad))
    W += (-W) % SS   # whole output pixels
    H += (-H) % SS
    ox, oy = round(ox / SS) * SS, round(oy / SS) * SS   # (the origin on an output pixel)

    def mask(text, x):
        img = Image.new("L", (W, H), 0)
        ImageDraw.Draw(img).text((x, oy), text, font=f, fill=255, anchor="ls")
        return np.asarray(img, np.float32) / 255

    q = mask(TEXT[0], ox)
    rest = mask(TEXT[1:], ox + adv - f.getlength(TEXT[1:]))   # (keeps the word's pair kerning)
    outline = dilate(np.maximum(q, rest), R)
    # the Q: cyan left of the line, red right of it, a black seam on it
    ql, qt, qr, qb = f.getbbox(TEXT[0], anchor="ls")
    qcx, qcy = ox + f.getlength(TEXT[0]) / 2, oy + (qt + qb) / 2
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    lx = qcx - TILT * (yy - qcy)                       # the line's x at each row
    d = (xx - lx) / np.sqrt(1 + TILT * TILT)           # signed distance to it (px)
    red = np.clip(d + 0.5, 0, 1)
    seam = np.clip(SEAM * P / 2 - np.abs(d) + 0.5, 0, 1)
    qrgb = (np.array(CY, np.float32)[None, None, :] * (1 - red[..., None]) + np.array(RD, np.float32)[None, None, :] * red[..., None]) / 255
    qrgb *= (1 - seam)[..., None]
    # layer A, premultiplied: the Q over the black outline
    a_alpha = np.maximum(outline, q)
    a_rgb = qrgb * q[..., None]
    # layer B: the rest of the word in white
    b_alpha = rest

    def down(rgb, alpha):
        h, w = H // SS, W // SS
        al = alpha.reshape(h, SS, w, SS).mean(axis=(1, 3))
        pm = rgb.reshape(h, SS, w, SS, 3).mean(axis=(1, 3))
        col = np.where(al[..., None] > 1e-4, pm / np.maximum(al[..., None], 1e-4), 0)
        return np.dstack([np.clip(col, 0, 1), al])

    A = down(a_rgb, a_alpha)
    B = down(np.ones((H, W, 3), np.float32) * b_alpha[..., None], b_alpha)
    B[..., :3] = 1   # (white everywhere: the tint is the colour, and filtering never darkens the edge)
    gap = 8   # transparent rows between the layers (mipmaps don't bleed one into the other)
    h, w = H // SS, W // SS
    img = np.zeros((2 * h + gap, w, 4), np.float32)
    img[:h] = A
    img[h + gap:] = B
    img[h + gap:, :, :3] = 1
    png = io.BytesIO()
    Image.fromarray(np.round(img * 255).astype(np.uint8), "RGBA").save(png, "PNG", optimize=True)
    data = png.getvalue()
    with open(OUT, "w", newline="\n") as o:
        o.write("// Generated by tools/make_logo.py - do not edit. The QUARK title (Rubik ExtraBold, SIL OFL 1.1,\n")
        o.write("// assets/fonts/Rubik-OFL.txt) drawn like the app icon, as an RGBA PNG of two layers (Renderer::logo).\n#pragma once\n")
        o.write("// each layer: kLogoW x kLogoH px; layer B starts kLogoBY rows down. The pen origin (on the baseline) is at\n")
        o.write("// (kLogoOX, kLogoOY) in a layer; kLogoAdv: the word's advance; kLogoEm: the em it was baked at (px)\n")
        o.write("constexpr int kLogoW = %d, kLogoH = %d, kLogoBY = %d;\n" % (w, h, h + gap))
        o.write("constexpr float kLogoOX = %.3ff, kLogoOY = %.3ff, kLogoAdv = %.3ff, kLogoEm = %.3ff;\n" % (ox / SS, oy / SS, adv / SS, EM))
        o.write("static const unsigned char kLogoPng[] = {\n")
        for i in range(0, len(data), 40):
            o.write(",".join(str(v) for v in data[i:i + 40]) + ",\n")
        o.write("};\n")
    print("logo: %dx%d per layer, %d bytes of PNG -> %s" % (w, h, len(data), os.path.relpath(OUT, ROOT)))
    Image.fromarray(np.round(img * 255).astype(np.uint8), "RGBA").save(os.path.join(os.environ.get("LOGO_PREVIEW_DIR", ROOT), "logo_preview.png")) if os.environ.get("LOGO_PREVIEW_DIR") else None


if __name__ == "__main__":
    main()
