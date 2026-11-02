#!/usr/bin/env python3
"""Bakes the languages' glyph atlases (src/client/lang.h): every character past ASCII that a language's table uses, as
a signed distance field in the UI font's cell layout (tools/make_font_atlas.py: TPU 6, PAD 1.5, 66-texel cells, the
same line metrics), so the game draws it with the UI font's shader. Re-run after changing a translation:

    python3 tools/lang/make_atlas.py --cjk /usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc

(needs Pillow, numpy, scipy and fontTools; the CJK font is Debian / Ubuntu's fonts-noto-cjk, SIL OFL 1.1, not kept in
the repository: 20 MB). Writes assets/lang/atlas/<name>.png (grey) and <name>.glyphs ("cols C cell N", then one
"codepoint advance" a line, hex and font units, in cell order: left to right, top to bottom):
- base: Latin-1 and Latin Extended-A, Cyrillic, the usual typographic marks, every character of the Latin and Cyrillic
  languages' tables, and every language's own name (the LANGUAGE list): English, Spanish, Portuguese, German, French,
  Turkish and Russian draw with it (players' names and chat in those scripts too).
- zh-CN, ja, ko: base plus that language's characters (CJK from Noto Sans CJK Bold in that language's forms).
Latin and Cyrillic come from Rubik Bold (assets/fonts/Rubik.ttf at wght 700), as ASCII does: one look. A CJK
character is sized so its ideographs are a little taller than Rubik's capitals and centred on them."""
import argparse, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LANG = os.path.join(ROOT, "assets", "lang")
RUBIK = os.path.join(ROOT, "assets", "fonts", "Rubik.ttf")
TPU, BOX, PAD, ORIGIN, EM, SS = 6, 8, 1.5, 0.5, 7.5, 8   # (make_font_atlas.py's)
CELL = int((BOX + 2 * PAD) * TPU)
SPREAD = PAD * TPU
COLS = 62                                                 # 4092 texels wide: under 4096
CJK_INDEX = {"zh-CN": 2, "ja": 0, "ko": 1, "base": 2}     # NotoSansCJK-Bold.ttc's faces: JP, KR, SC
CJK_HEIGHT = 6.0                                          # an ideograph's ink (units); Rubik's capitals are 5.25
CAP_MID = 0.75 + 5.25 / 2
LATIN_LANGS = ["es", "pt-BR", "de", "fr", "tr", "ru"]
NAMES = "ENGLISH 简体中文 РУССКИЙ ESPAÑOL PORTUGUÊS (BRASIL) DEUTSCH 日本語 FRANÇAIS 한국어 TÜRKÇE"
EXTRA = [*range(0xA0, 0x180), *range(0x400, 0x460), *map(ord, "–—‘’‚“”„…«»•€™")]


def chars_of(code):
    out = set()
    for line in open(os.path.join(LANG, code + ".tsv"), encoding="utf-8"):
        k, _, v = line.rstrip("\n").partition("\t")
        out.update(ord(c) for c in v if ord(c) > 126)
    return out


def sdf(hi):
    """hi: bool (CELL*SS)^2 -> CELL^2 distance field 0..1 (0.5 the edge), from exact distance transforms at SS x."""
    inside = distance_transform_edt(hi)
    outside = distance_transform_edt(~hi)
    signed = np.where(hi, inside - 0.5, 0.5 - outside) / SS     # texels, inside positive
    signed = signed.reshape(CELL, SS, CELL, SS).mean(axis=(1, 3))
    return np.clip(0.5 + signed / (2 * SPREAD), 0, 1)


class Fonts:
    def __init__(self, cjk_path, cjk_index):
        ppu = TPU * SS
        self.ppu = ppu
        self.rubik = ImageFont.truetype(RUBIK, int(round(EM * ppu)))
        self.rubik.set_variation_by_axes([700])
        self.rubik_cmap = set(TTFont(RUBIK).getBestCmap().keys())
        self.cap_top = self.rubik.getbbox("H")[1]
        self.cjk = None
        if cjk_path:
            probe = ImageFont.truetype(cjk_path, 1000, index=cjk_index)
            b = probe.getbbox("国")
            size = CJK_HEIGHT * ppu / ((b[3] - b[1]) / 1000)
            self.cjk = ImageFont.truetype(cjk_path, int(round(size)), index=cjk_index)
            b = self.cjk.getbbox("国")
            self.cjk_oy = (PAD + CAP_MID) * ppu - (b[1] + b[3]) / 2
            self.cjk_cmap = set(TTFont(cjk_path, fontNumber=cjk_index).getBestCmap().keys())

    def glyph(self, cp):
        ch = chr(cp)
        cjkish = 0x2E80 <= cp <= 0xFFEF and not (0x2000 <= cp <= 0x206F)
        if cp in self.rubik_cmap and not cjkish:
            font, oy = self.rubik, (PAD + 0.75) * self.ppu - self.cap_top
        elif self.cjk and cp in self.cjk_cmap:
            font, oy = self.cjk, self.cjk_oy
        else:
            return None
        img = Image.new("L", (CELL * SS, CELL * SS), 0)
        ImageDraw.Draw(img).text(((PAD + ORIGIN) * self.ppu, oy), ch, font=font, fill=255)
        hi = np.asarray(img) >= 128
        if hi[:, -1].any() or hi[-1, :].any() or hi[0, :].any():
            print(f"  warning: U+{cp:04X} {ch} reaches past its cell")
        return np.round(sdf(hi) * 255).astype(np.uint8), font.getlength(ch) / self.ppu


def bake(name, cps, cjk_path):
    fonts = Fonts(cjk_path, CJK_INDEX[name])
    cells, glyphs, missing = [], [], []
    for cp in sorted(cps):
        g = fonts.glyph(cp)
        if g is None: missing.append(cp); continue
        cells.append(g[0]); glyphs.append((cp, g[1]))
    rows = (len(cells) + COLS - 1) // COLS
    atlas = np.zeros((rows * CELL, COLS * CELL), np.uint8)
    for i, c in enumerate(cells):
        y, x = (i // COLS) * CELL, (i % COLS) * CELL
        atlas[y:y + CELL, x:x + CELL] = c
    out = os.path.join(LANG, "atlas")
    os.makedirs(out, exist_ok=True)
    Image.fromarray(atlas, "L").save(os.path.join(out, name + ".png"), optimize=True)
    with open(os.path.join(out, name + ".glyphs"), "w", newline="\n") as f:
        f.write(f"cols {COLS} cell {CELL}\n")
        for cp, adv in glyphs: f.write(f"{cp:04x} {adv:.4f}\n")
    print(f"{name}: {len(glyphs)} glyphs, {rows} rows, {os.path.getsize(os.path.join(out, name + '.png')) // 1024} KB"
          + (f"; no glyph for {''.join(chr(c) for c in missing[:40])}" if missing else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cjk", help="NotoSansCJK-Bold.ttc (fonts-noto-cjk)")
    ap.add_argument("--only", help="one atlas: base, zh-CN, ja, ko")
    a = ap.parse_args()
    base = set(EXTRA) | {ord(c) for c in NAMES if ord(c) > 126}
    for c in LATIN_LANGS:
        if os.path.isfile(os.path.join(LANG, c + ".tsv")): base |= chars_of(c)
    jobs = {"base": base}
    for c in ("zh-CN", "ja", "ko"):
        if os.path.isfile(os.path.join(LANG, c + ".tsv")): jobs[c] = base | chars_of(c)
    for name, cps in jobs.items():
        if a.only and name != a.only: continue
        bake(name, cps, a.cjk)


if __name__ == "__main__":
    main()
