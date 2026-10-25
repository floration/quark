#!/usr/bin/env python3
"""Builds ui-customizer.html: one offline file with the CSS, the JS, the layout dump and the UI font inlined.

    python tools/ui_customizer/build.py                      (from a game checkout; writes <Quark>/ui-customizer/)
    python build.py --out-dir D:/somewhere                     (another folder: the HTML, fonts/catalog.json)
    python build.py --dump other.json --out test.html          (another dump / output file)
    python build.py --layout my-layout.json                    (-> ui-customizer-<name>.html, a PROPOSAL build)

Usually run by install.py (the Quark Project app's installer). The output folder (--out-dir, default the
`ui-customizer` folder next to the checkout, i.e. Quark\\ui-customizer) also holds the owner's own files, which
this script reads and never writes: provenance.json, extract/ (a dump + screenshots made on this PC by
extract/extract.py), fonts/ofl/ (open fonts downloaded from github.com/google/fonts).

--layout <file>: a layout (the EXPORT LAYOUT format) embedded as the starting state when the browser has nothing saved
for this build; the toolbar says PROPOSAL, Reset goes back to the proposal and "Reset to game default" to the game's
layout. Such a build keeps its browser storage under its own namespace (--store-ns, default
quark-ui-customizer-proposal-<layout file name>), so it never shares saved layouts, prefs or uploaded fonts with
ui-customizer.html. Default output: ui-customizer-<layout name without "layout-">.html.

Data (the game's UI, one element list per page): --dump, else the newer (source.extracted) of <out>/extract/ui-dump.json
(made on this PC, with its screenshots) and data/ui-dump.json.gz (committed with the tool), else test/sample-dump.json.
Screenshots are not embedded: the page loads them from extract/shots/... next to the HTML file, and only when they were
taken from the same commit as the dump (extract/shots/SOURCE.txt). Font: the dump's style.font.file, looked up in the
game repo's assets/fonts. Python 3 standard library only.

Provenance: <out>/provenance.json (who made each UI page, human or AI) is embedded, so Export layout / Export spec can
merge the layout's page origins into it offline (written in place through the File System Access API when the owner
picks that folder, else downloaded).

Font trials: every font of FONT_TRIALS below that is on this PC (the repo's assets/fonts, <out>/fonts/ofl/*, then
Windows\\Fonts) is embedded for the toolbar's Font menu, with its licence (name table IDs 13/14), cap height (OS/2
sCapHeight, else the 'H' glyph's top) and variation axes read here. Microsoft-licensed Windows fonts are preview-only
(read from this PC's Windows\\Fonts at build time, never committed); OFL ones could ship. <out>/fonts/catalog.json gets
the same list (without the font data), to read outside the tool.
"""
import argparse
import base64
import datetime
import gzip
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src")
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))       # the game checkout this tool is part of
DEFAULT_OUT = os.path.normpath(os.path.join(REPO, "..", "ui-customizer"))   # Quark\ui-customizer
JS_ORDER = ["model.js", "render.js", "fonts.js", "app.js"]
OUT_DIR = DEFAULT_OUT   # (set by configure(): where the page, its fonts and the owner's files are)


def configure(out_dir):
    global OUT_DIR, FONTS_DIR, OFL_DIR
    OUT_DIR = os.path.abspath(out_dir)
    FONTS_DIR = os.path.join(OUT_DIR, "fonts")
    OFL_DIR = os.path.join(FONTS_DIR, "ofl")


def rel(path):
    """A path as the page shows it: relative to the output folder, or to the repo for the tool's own files."""
    for base in (OUT_DIR, REPO):
        try:
            r = os.path.relpath(path, base).replace("\\", "/")
        except ValueError:   # (another drive)
            continue
        if not r.startswith("../"):
            return r if base == OUT_DIR else "repo:" + r
    return path.replace("\\", "/")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def read_dump_file(path):
    if path.endswith(".gz"):
        with gzip.open(path, "rt", encoding="utf-8-sig") as f:
            return json.load(f)
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def dump_candidates():
    """(path, is_sample) in the order load_dump tries them when no --dump is given."""
    local = os.path.join(OUT_DIR, "extract", "ui-dump.json")
    repo = os.path.join(HERE, "data", "ui-dump.json.gz")
    return [(local, False), (repo, False), (os.path.join(HERE, "test", "sample-dump.json"), True)]


def load_dump(explicit=None):
    """(dump, its path as shown, is_sample). Without --dump: the newer of the local extract and the committed one."""
    if explicit:
        try:
            return read_dump_file(explicit), rel(os.path.abspath(explicit)), False
        except (OSError, ValueError) as e:
            sys.exit(f"build: --dump {explicit} is not readable JSON ({e})")
    found = []
    for path, is_sample in dump_candidates():
        if not os.path.exists(path):
            continue
        try:
            found.append((read_dump_file(path), path, is_sample))
        except (OSError, ValueError) as e:
            print(f"warning: {path} is not readable JSON ({e}): skipped")
    real = [f for f in found if not f[2]]
    if real:
        when = lambda f: str(((f[0] or {}).get("source") or {}).get("extracted") or "")
        best = max(real, key=when)   # (max keeps the first of equals: the local one)
        return best[0], rel(best[1]), False
    if found:
        return found[0][0], rel(found[0][1]), True
    print("warning: no dump found: the page will be empty")
    return None, None, False


# What the dump is drawn from: the game's UI code (a change there can move widgets) and VERSION (every page shows it).
UI_PATHS = ["src/client"]
REFRESH = (r"py -3 tools\ui_customizer\install.py --extract" if os.name == "nt" else "python3 tools/ui_customizer/install.py --extract")


def git(*args):
    try:
        p = subprocess.run(["git", "-C", REPO] + list(args), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                           creationflags=0x08000000 if os.name == "nt" else 0)   # (CREATE_NO_WINDOW)
        return p.stdout.strip() if p.returncode == 0 else None
    except OSError:
        return None


def freshness(dump):
    """Is the dump still the game's UI? {gameVersion, gameCommit, dumpVersion, dumpCommit, uiCommits (commits that
    touched the UI code since the dump's commit; None when git can't tell), stale, why, note, refresh}. Stale when a
    commit since the dump's changed the UI code. Every release bumps VERSION, so a VERSION-only difference (git counts
    0 UI commits) is only a note: the pages match the game apart from the version label (the page shows it in grey,
    not the red banner). The VERSION check decides only when git can't count (no git, or the dump's commit unknown)."""
    src = (dump or {}).get("source") or {}
    try:
        with open(os.path.join(REPO, "VERSION"), encoding="utf-8") as f:
            gver = f.read().strip()
    except OSError:
        gver = ""
    dver, dsha = str(src.get("version") or ""), str(src.get("commit") or "")
    head = git("rev-parse", "HEAD") or ""
    n = None
    if dsha and head and git("cat-file", "-e", dsha + "^{commit}") is not None:
        out = git("rev-list", "--count", dsha + "..HEAD", "--", *UI_PATHS)
        n = int(out) if out and out.isdigit() else None
    why, note = [], ""
    moved = bool(gver and dver and gver != dver)
    if moved and n is None:
        why.append(f"the dump is {dver}, the game {gver}")
    if n:
        why.append(f"{n} commit{'s' if n != 1 else ''} changed the UI code (src/client) since the dump's commit {dsha[:8]}"
                   + (f" (the dump is {dver}, the game {gver})" if moved else ""))
    elif moved and n == 0:
        note = f"the dump is {dver}, the game {gver}; no UI code changed since, only the version label differs"
    return {"gameVersion": gver, "gameCommit": head, "dumpVersion": dver, "dumpCommit": dsha, "uiCommits": n,
            "stale": bool(dump) and bool(why), "why": "; ".join(why), "note": note if dump else "", "refresh": REFRESH}


def check_shots(dump):
    """The page's screenshots are used only when extract/shots/ was taken from the dump's commit; else dropped."""
    if not dump:
        return "none"
    commit = str((dump.get("source") or {}).get("commit") or "")
    stamp = os.path.join(OUT_DIR, "extract", "shots", "SOURCE.txt")
    have = ""
    if os.path.isfile(stamp):
        with open(stamp, encoding="utf-8") as f:
            have = (f.read().split() or [""])[0]
    ok = bool(commit) and bool(have) and (have.startswith(commit) or commit.startswith(have))
    n = 0
    for p in dump.get("pages") or []:
        if p.get("screenshot"):
            if ok and os.path.isfile(os.path.join(OUT_DIR, "extract", p["screenshot"])):
                n += 1
            else:
                p["screenshot"] = None
    return f"{n} screenshots (extract/shots, {commit})" if ok else \
        f"no screenshots of {commit or '?'} on this PC (run install.py --extract to take them)"


def find_font(dump):
    want = (((dump or {}).get("style") or {}).get("font") or {}).get("file") or ""
    cands = []
    if want:
        want = want.replace("\\", "/")
        base = os.path.basename(want)
        for root in (REPO, os.path.join(REPO, "assets"), os.path.join(OUT_DIR, "extract")):
            cands += [os.path.join(root, want), os.path.join(root, "fonts", base), os.path.join(root, "font", base)]
    cands.append(os.path.join(REPO, "assets", "fonts", "Rubik.ttf"))
    for c in cands:
        if os.path.isfile(c) and c.lower().endswith((".ttf", ".otf")):
            return c
    return None


def font_face(path):
    if not path:
        return "/* no UI font found: system fallback */"
    with open(path, "rb") as f:
        data = base64.b64encode(f.read()).decode("ascii")
    kind = "opentype" if path.lower().endswith(".otf") else "truetype"
    mime = "font/otf" if kind == "opentype" else "font/ttf"
    return ("@font-face { font-family: 'QuarkUI'; font-weight: 100 900; font-style: normal; "
            f"src: url(data:{mime};base64,{data}) format('{kind}'); }}")


# ---- font trials -------------------------------------------------------------------------------------------------
WINFONTS = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
REPO_FONTS = os.path.join(REPO, "assets", "fonts")
FONTS_DIR = os.path.join(DEFAULT_OUT, "fonts")   # (configure() moves these to the output folder)
OFL_DIR = os.path.join(FONTS_DIR, "ofl")   # open fonts downloaded from github.com/google/fonts (ofl/<family>/, with OFL.txt)
MAX_FONT_BYTES = 20 * 1024 * 1024   # base64 of all embedded files together; above it DROP_FIRST, then the largest, go
DROP_FIRST = ("consolab.ttf", "impact.ttf")   # (file keys) the trials the owner can most easily do without

# id, menu name, file (looked up in the repo's assets/fonts, fonts/ofl/*/, then Windows/Fonts; None = the game's UI font;
# a tuple = one static file per weight, same family), weight, weights offered, width (variable fonts with a wdth axis:
# a font-stretch keyword, or a percentage such as "85%", drawn from its own FontFace pinned to that width), letter
# spacing (em), uppercase forced, one-line note, [menu group (else the licence's)]
RUBIK_LIKE = "Can ship: Rubik-like picks"
IN_GAME = "In the game: Settings > FONT"
FONT_TRIALS = [
    ("rubik", "Rubik", None, 700, [500, 600, 700, 800, 900], "", 0, False, "the game's font today (Settings > FONT: RUBIK)"),
    ("worksans", "Work Sans", "WorkSans.ttf", 700, [500, 600, 700, 800, 900], "", 0, False, "the game's font before 1.18.10, still in Settings > FONT"),
    ("notosans", "Noto Sans Bold", "NotoSans-Bold.ttf", 700, [700], "", 0, False, "neutral humanist, huge glyph coverage"),
    ("bahn", "Bahnschrift", "bahnschrift.ttf", 700, [400, 500, 600, 700], "", 0, False, "DIN-like, made for screens"),
    ("bahnsc", "Bahnschrift SemiCondensed", "bahnschrift.ttf", 700, [400, 500, 600, 700], "semi-condensed", 0.01, False,
     "DIN-like, narrower"),
    ("bahnc", "Bahnschrift Condensed", "bahnschrift.ttf", 600, [400, 500, 600, 700], "condensed", 0.02, False,
     "DIN-like condensed: long labels fit"),
    ("segsb", "Segoe UI Semibold", "seguisb.ttf", 600, [600], "", 0, False, "the Windows UI font"),
    ("segb", "Segoe UI Bold", "segoeuib.ttf", 700, [700], "", 0, False, "the Windows UI font"),
    ("segbl", "Segoe UI Black", "seguibl.ttf", 900, [900], "", 0, False, "the Windows UI font, heavy"),
    ("arialblk", "Arial Black", "ariblk.ttf", 900, [900], "", 0, False, "very heavy and wide"),
    ("franklin", "Franklin Gothic Medium", "framd.ttf", 500, [500], "", 0.02, False, "classic grotesque, narrow"),
    ("tahoma", "Tahoma Bold", "tahomabd.ttf", 700, [700], "", 0, False, "tight, legible small"),
    ("verdana", "Verdana Bold", "verdanab.ttf", 700, [700], "", 0, False, "wide, made for small sizes"),
    ("trebuchet", "Trebuchet MS Bold", "trebucbd.ttf", 700, [700], "", 0, False, "humanist with character"),
    ("consolas", "Consolas Bold", "consolab.ttf", 700, [700], "", 0, False, "monospace, technical look"),
    ("impact", "Impact", "impact.ttf", 400, [400], "", 0.03, True, "condensed display face (headers)"),
    # the other fonts the game ships (assets/fonts, Settings > FONT: tools/make_font_atlas.py BUILTIN)
    ("jost", "Jost SemiBold", "Jost.ttf", 600, [500, 600, 700], "", 0, False, "geometric, in the game (Settings > FONT)", IN_GAME),
    ("barlowcondensed", "Barlow Condensed Bold", "BarlowCondensed-Bold.ttf", 700, [700], "", 0, False,
     "condensed grotesk, in the game (Settings > FONT)", IN_GAME),
    ("bebasneue", "Bebas Neue", "BebasNeue-Regular.ttf", 400, [400], "", 0, False, "capitals-only display face, in the game", IN_GAME),
    ("decalotype", "Decalotype Bold", "Decalotype-Bold.ttf", 700, [700], "", 0, False, "in the game (Settings > FONT)", IN_GAME),
    ("lilitaone", "Lilita One", "LilitaOne-Regular.ttf", 400, [400], "", 0, False, "heavy rounded display face, in the game", IN_GAME),
    ("dejavusans", "DejaVu Sans Bold", "DejaVuSans-Bold.ttf", 700, [700], "", 0, False, "wide humanist, in the game", IN_GAME),
    # open fonts from google/fonts (fonts/ofl/<family>/ with OFL.txt): could ship with the game
    ("fredokasc", "Fredoka 85% width", "Fredoka[wdth,wght].ttf", 600, [500, 600, 700], "85%", 0, False,
     "rounded like Rubik, softer; 85% width so long labels fit", RUBIK_LIKE),
    ("fredoka", "Fredoka", "Fredoka[wdth,wght].ttf", 600, [500, 600, 700], "", 0, False,
     "rounded like Rubik, softer, normal width", RUBIK_LIKE),
    ("saira", "Saira SemiCondensed", ("SairaSemiCondensed-SemiBold.ttf", "SairaSemiCondensed-Bold.ttf"), 700, [600, 700],
     "", 0, False, "squared techno sans, semi-condensed", RUBIK_LIKE),
    ("signika", "Signika", "Signika[GRAD,wght].ttf", 600, [500, 600, 700], "", 0, False,
     "soft humanist made for signage, very legible small", RUBIK_LIKE),
    ("asap", "Asap Bold", "Asap[wdth,wght].ttf", 700, [500, 600, 700, 800], "", 0, False,
     "rounded-corner grotesque, compact", RUBIK_LIKE),
    ("asapc", "Asap Condensed Bold", "Asap[wdth,wght].ttf", 700, [500, 600, 700, 800], "75%", 0, False,
     "Asap at 75% width: long labels fit", RUBIK_LIKE),
    ("barlowsc", "Barlow Semi Condensed", ("BarlowSemiCondensed-SemiBold.ttf", "BarlowSemiCondensed-Bold.ttf"), 700,
     [600, 700], "", 0, False, "slightly rounded DIN-like grotesk, semi-condensed", RUBIK_LIKE),
    ("nunito", "Nunito", "Nunito[wght].ttf", 700, [600, 700, 800, 900], "", 0, False,
     "fully rounded terminals, friendly", RUBIK_LIKE),
    ("mplusr", "M PLUS Rounded 1c Bold", "MPLUSRounded1c-Bold.ttf", 700, [700], "", 0, False,
     "rounded, also covers Japanese (3.5 MB file)", RUBIK_LIKE),
    ("kanit", "Kanit", ("Kanit-SemiBold.ttf", "Kanit-Bold.ttf"), 600, [600, 700], "", 0, False,
     "squarish geometric, sporty; also covers Thai", RUBIK_LIKE),
    ("archivon", "Archivo Narrow", "ArchivoNarrow[wght].ttf", 700, [500, 600, 700], "", 0, False,
     "narrow grotesque: the longest labels fit", RUBIK_LIKE),
]


class Sfnt:
    """Just enough of a TrueType / OpenType file: name, OS/2, head, fvar, and the 'H' glyph's top."""

    def __init__(self, data):
        self.b = data
        if data[:4] not in (b"\x00\x01\x00\x00", b"OTTO", b"true"):
            raise ValueError("not a TrueType/OpenType file")
        self.t = {}
        for i in range(self.u16(4)):
            tg, _, off, ln = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
            self.t[tg.decode("latin1")] = (off, ln)

    def u16(self, o):
        return struct.unpack(">H", self.b[o:o + 2])[0]

    def i16(self, o):
        return struct.unpack(">h", self.b[o:o + 2])[0]

    def names(self):
        out = {}
        if "name" not in self.t:
            return out
        off = self.t["name"][0]
        cnt, so = self.u16(off + 2), self.u16(off + 4)
        for i in range(cnt):
            p, e, l, nid, ln, o = struct.unpack(">HHHHHH", self.b[off + 6 + 12 * i:off + 18 + 12 * i])
            raw = self.b[off + so + o:off + so + o + ln]
            if p == 3 and l == 0x409:
                out[nid] = raw.decode("utf-16-be", "replace")      # Windows, English (wins)
            elif p == 1 and l == 0 and nid not in out:
                out[nid] = raw.decode("latin1")
        return out

    def upem(self):
        return self.u16(self.t["head"][0] + 18)

    def os2(self):
        if "OS/2" not in self.t:
            return {}
        o = self.t["OS/2"][0]
        r = {"weightClass": self.u16(o + 4), "widthClass": self.u16(o + 6), "fsType": self.u16(o + 8)}
        if self.u16(o) >= 2:
            r["xHeight"], r["capHeight"] = self.i16(o + 86), self.i16(o + 88)
        return r

    def axes(self):
        if "fvar" not in self.t:
            return []
        fo = self.t["fvar"][0]
        ao, ac, asz = self.u16(fo + 4), self.u16(fo + 8), self.u16(fo + 10)
        out = []
        for i in range(ac):
            a = fo + ao + i * asz
            mn, df, mx = (struct.unpack(">i", self.b[a + 4 + 4 * k:a + 8 + 4 * k])[0] / 65536 for k in range(3))
            out.append({"tag": self.b[a:a + 4].decode("latin1"), "min": round(mn, 3), "default": round(df, 3), "max": round(mx, 3)})
        return out

    def glyph_top(self, ch="H"):
        """yMax of a character's glyph (TrueType outlines, cmap format 4); None when it can't be found."""
        if not all(k in self.t for k in ("cmap", "loca", "glyf", "head")):
            return None
        co, sub = self.t["cmap"][0], None
        for i in range(self.u16(co + 2)):
            pid, eid, off = struct.unpack(">HHI", self.b[co + 4 + 8 * i:co + 12 + 8 * i])
            if pid == 3 and eid in (0, 1) and self.u16(co + off) == 4:
                sub = co + off
                break
        if sub is None:
            return None
        segx2 = self.u16(sub + 6)
        ends, starts = sub + 14, sub + 16 + segx2
        deltas, ranges = starts + segx2, starts + 2 * segx2
        c, gid = ord(ch), 0
        for s in range(segx2 // 2):
            if self.u16(ends + 2 * s) < c:
                continue
            if self.u16(starts + 2 * s) <= c:
                ro = self.u16(ranges + 2 * s)
                if ro == 0:
                    gid = (c + self.i16(deltas + 2 * s)) & 0xFFFF
                else:
                    g = self.u16(ranges + 2 * s + ro + 2 * (c - self.u16(starts + 2 * s)))
                    gid = (g + self.i16(deltas + 2 * s)) & 0xFFFF if g else 0
            break
        if not gid:
            return None
        lo = self.t["loca"][0]
        if self.i16(self.t["head"][0] + 50) == 1:
            g0 = struct.unpack(">I", self.b[lo + 4 * gid:lo + 4 * gid + 4])[0]
        else:
            g0 = self.u16(lo + 2 * gid) * 2
        return self.i16(self.t["glyf"][0] + g0 + 8)


def licence_of(names):
    """('open' | 'preview' | 'unknown', one line). The same rules as QFonts.licenceOf in src/fonts.js."""
    low = ((names.get(13) or "") + " " + (names.get(14) or "")).lower()
    if "open font license" in low or "scripts.sil.org/ofl" in low or "openfontlicense" in low:
        return "open", "SIL Open Font License 1.1: can ship with the game (ship its licence text next to it)"
    if "apache license" in low:
        return "open", "Apache License 2.0: can ship with the game"
    if "ubuntu font licence" in low:
        return "open", "Ubuntu Font Licence: can ship with the game"
    if "bitstream vera fonts copyright" in low:
        return "open", "Bitstream Vera licence (DejaVu): can ship with the game"
    if "microsoft" in low:
        return "preview", "Microsoft-licensed Windows font: preview only, cannot ship with the game"
    return "unknown", "no licence stated in the font: check before shipping"


def font_file_info(path):
    with open(path, "rb") as f:
        data = f.read()
    sf = Sfnt(data)
    names, os2, upem = sf.names(), sf.os2(), sf.upem()
    cap, cap_from = os2.get("capHeight"), "OS/2 sCapHeight"
    if not cap:
        cap, cap_from = sf.glyph_top("H"), "the H glyph's top"
    status, summary = licence_of(names)
    return data, {
        "family": names.get(16) or names.get(1) or os.path.splitext(os.path.basename(path))[0],
        "subfamily": names.get(17) or names.get(2) or "", "fullName": names.get(4) or "",
        "copyright": (names.get(0) or "").strip(),
        "licenceText": (names.get(13) or "").strip(), "licenceUrl": (names.get(14) or "").strip(),
        "licence": status, "licenceSummary": summary,
        "upem": upem, "capHeight": cap, "capFrom": cap_from if cap else None,
        "capRatio": round(cap / upem, 4) if cap else None,
        "weightClass": os2.get("weightClass"), "fsType": os2.get("fsType"), "axes": sf.axes(), "bytes": len(data),
    }


def find_trial_file(name):
    ofl = [os.path.join(OFL_DIR, d) for d in sorted(os.listdir(OFL_DIR))] if os.path.isdir(OFL_DIR) else []
    for root in [REPO_FONTS] + ofl + [WINFONTS]:
        p = os.path.join(root, name)
        if os.path.isfile(p):
            return p
    return None


def font_trials(ui_font_path):
    """(catalogue for the page, {file key: base64}, notes). The game's own font isn't embedded twice: it is QuarkUI."""
    ref_ratio, files, b64, notes, fonts = 0.66, {}, {}, [], []
    if ui_font_path:
        try:
            _, info = font_file_info(ui_font_path)
            ref_ratio = info["capRatio"] or ref_ratio
            files["ui"] = dict(info, path=rel(ui_font_path), fileName=os.path.basename(ui_font_path), builtin=True)
        except (OSError, ValueError, struct.error, IndexError) as e:
            notes.append(f"the UI font is unreadable ({e})")
    for fid, name, fname, weight, weights, stretch, ls, upper, why, *grp in FONT_TRIALS:
        if fname is None:
            if "ui" not in files:
                continue
            keys = ["ui"]
        else:
            keys = []
            for fn in (fname if isinstance(fname, tuple) else (fname,)):
                key = fn.lower()
                if key not in files:
                    path = find_trial_file(fn)
                    if not path:
                        notes.append(f"{name}: {fn} is not on this PC, left out")
                        break
                    try:
                        data, info = font_file_info(path)
                    except (OSError, ValueError, struct.error, IndexError) as e:
                        notes.append(f"{name}: {fn} is unreadable ({e}), left out")
                        break
                    files[key] = dict(info, path=path.replace("\\", "/"), fileName=fn)
                    if path.startswith(OFL_DIR):
                        files[key]["path"] = rel(path)
                        lic = os.path.join(os.path.dirname(path), "OFL.txt")
                        files[key]["licenceFile"] = rel(lic) if os.path.isfile(lic) else None
                    b64[key] = base64.b64encode(data).decode("ascii")
                keys.append(key)
            if len(keys) != len(fname if isinstance(fname, tuple) else (fname,)):   # (a file is missing)
                continue
        f = {"id": fid, "name": name, "file": keys[0], "weight": weight, "weights": weights, "stretch": stretch,
             "letterSpacing": ls, "upper": upper, "why": why}
        if len(keys) > 1:
            f["files"] = keys
        if grp:
            f["group"] = grp[0]
        fonts.append(f)
    while sum(len(v) for v in b64.values()) > MAX_FONT_BYTES:   # too big: drop DROP_FIRST, then the largest files
        big = next((k for k in DROP_FIRST if k in b64), None) or max(b64, key=lambda k: len(b64[k]))
        notes.append(f"{files[big]['fileName']} dropped: the embedded fonts were over {MAX_FONT_BYTES // 1048576} MB")
        del b64[big]
        fonts = [f for f in fonts if big not in f.get("files", [f["file"]])]
    for f in fonts:
        r = files[f["file"]].get("capRatio")
        f["sizeK"] = round(ref_ratio / r, 4) if r else None   # (None: the page measures the H on a canvas)
    used = {k for f in fonts for k in f.get("files", [f["file"]])}
    cat = {"refCapRatio": ref_ratio, "fonts": fonts, "files": {k: v for k, v in files.items() if k in used}}
    return cat, {k: v for k, v in b64.items() if k in used}, notes


def write_catalogue(cat):
    os.makedirs(FONTS_DIR, exist_ok=True)
    with open(os.path.join(FONTS_DIR, "catalog.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(cat, f, ensure_ascii=False, indent=1)


def load_provenance():
    """<out>/provenance.json (who made each page), embedded so the page can merge into it offline."""
    path = os.path.join(OUT_DIR, "provenance.json")
    if not os.path.exists(path):
        return None
    try:
        with open(path, encoding="utf-8-sig") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        print(f"warning: provenance.json is not readable JSON ({e}): not embedded")
        return None
    if not isinstance(data, dict) or not isinstance(data.get("items"), dict):
        print("warning: provenance.json has no \"items\" object: not embedded")
        return None
    return data


def game_origins():
    """{registry id: 'human' | 'ai'} of the game's own UI pages (assets/provenance/ui.json in the checkout, read only):
    the page list shows who made each page the game ships."""
    path = os.path.join(REPO, "assets", "provenance", "ui.json")
    try:
        with open(path, encoding="utf-8-sig") as f:
            items = json.load(f).get("items") or {}
    except (OSError, ValueError, AttributeError):
        return {}
    return {k: v["origin"] for k, v in items.items() if isinstance(v, dict) and v.get("origin") in ("human", "ai")}


def script_json(obj):
    # inside <script type="application/json">: only "</" could end the element early
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Build the single-file UI customizer.")
    ap.add_argument("--out-dir", default=DEFAULT_OUT, help="the tool's folder (default: Quark\\ui-customizer, next to the checkout)")
    ap.add_argument("--dump", help="layout dump JSON or .json.gz (default: the newer of <out-dir>/extract/ui-dump.json and "
                                   "data/ui-dump.json.gz, else test/sample-dump.json)")
    ap.add_argument("--out", default=None, help="output HTML (default: <out-dir>/ui-customizer.html; with --layout ui-customizer-<name>.html)")
    ap.add_argument("--layout", help="layout JSON (EXPORT LAYOUT format) the page starts from when its browser storage is empty")
    ap.add_argument("--store-ns", help="browser storage namespace (default: quark-ui-customizer, with --layout "
                                       "quark-ui-customizer-proposal-<layout file name>)")
    args = ap.parse_args(argv)
    configure(args.out_dir)
    os.makedirs(OUT_DIR, exist_ok=True)

    start, layout_name, store_ns = None, None, args.store_ns
    if args.layout:
        try:
            with open(args.layout, encoding="utf-8-sig") as f:
                start = json.load(f)
        except (OSError, ValueError) as e:
            sys.exit(f"build: --layout {args.layout} is not readable JSON ({e})")
        doc = start.get("doc") if isinstance(start, dict) else None
        if not isinstance(doc, dict) or not isinstance(doc.get("pages"), list) or not doc["pages"]:
            sys.exit(f"build: --layout {args.layout} has no doc.pages (expected the EXPORT LAYOUT format)")
        layout_name = rel(os.path.abspath(args.layout))
        stem = re.sub(r"[^A-Za-z0-9_.-]+", "-", os.path.splitext(os.path.basename(args.layout))[0])
        store_ns = store_ns or "quark-ui-customizer-proposal-" + stem
        if not args.out:
            args.out = os.path.join(OUT_DIR, "ui-customizer-" + (stem[len("layout-"):] if stem.startswith("layout-") and len(stem) > 7 else stem) + ".html")
    if store_ns == "quark-ui-customizer" and start is not None:
        sys.exit("build: a --layout build needs its own --store-ns (not the plain tool's)")
    args.out = os.path.abspath(args.out or os.path.join(OUT_DIR, "ui-customizer.html"))

    dump, dump_name, is_sample = load_dump(args.dump)
    shot_pages = lambda: sum(1 for p in (dump or {}).get("pages") or [] if p.get("screenshot"))
    shots_wanted = shot_pages()
    shots = check_shots(dump)
    font = find_font(dump)
    cat, font_files, notes = font_trials(font)
    write_catalogue(cat)
    js = "\n".join(f"// ---- {name} ----\n" + read(os.path.join(SRC, name)) for name in JS_ORDER)
    js = js.replace("</script", "<\\/script")
    fresh = freshness(dump) if not is_sample else None
    info = {
        "dump": dump_name, "sample": is_sample, "freshness": fresh, "origins": game_origins(),
        "shots": shot_pages(), "shotsWanted": shots_wanted,   # (pages with a reference screenshot on this PC / in the dump)
        "font": rel(font) if font else None,
        "built": datetime.datetime.now().isoformat(timespec="seconds"),
    }
    if start is not None:
        info["layout"] = layout_name
    if store_ns:
        info["storeNs"] = store_ns
    prov = load_provenance()
    html = read(os.path.join(SRC, "app.html"))
    parts = {
        "/*__FONT_FACE__*/": font_face(font),
        "/*__CSS__*/": read(os.path.join(SRC, "app.css")),
        "/*__DUMP__*/": script_json(dump) if dump is not None else "",
        "/*__BUILD_INFO__*/": script_json(info),
        "/*__START_LAYOUT__*/": script_json(start) if start is not None else "",
        "/*__FONT_CATALOG__*/": script_json(cat),
        "/*__FONT_FILES__*/": script_json(font_files),
        "/*__PROVENANCE__*/": script_json(prov) if prov is not None else "",
        "/*__JS__*/": js,
    }
    for key in parts:
        if key not in html:
            sys.exit(f"build: placeholder {key} missing from src/app.html")
    # one pass, so inlined text that happens to contain a placeholder is left alone
    html = re.sub("|".join(re.escape(k) for k in parts), lambda m: parts[m.group(0)], html)
    tmp = args.out + ".tmp"   # (written whole, then swapped in: the page is never half there)
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(html)
    os.replace(tmp, args.out)
    pages = len((dump or {}).get("pages") or [])
    print(f"built {rel(args.out)} ({len(html) // 1024} KB): data {dump_name or 'none'}"
          f"{' (SAMPLE)' if is_sample else ''}, {pages} pages, font {info['font'] or 'none'}")
    src = (dump or {}).get("source") or {}
    print(f"dump: {src.get('version', '?')} {str(src.get('commit', '?'))[:12]} (extracted {src.get('extracted', '?')}); {shots}")
    if fresh and fresh["stale"]:
        print(f"WARNING: the UI dump is out of date ({fresh['why']}): pages may not match the game. "
              f"Refresh it: {fresh['refresh']} (in the checkout)")
    elif fresh and fresh["note"]:
        print(f"note: {fresh['note']} (the dump is still the game's UI)")
    print(f"provenance: " + (f"{len(prov['items'])} items embedded" if prov is not None else "none (no provenance.json)"))
    if start is not None:
        print(f"start layout: {layout_name} ({len(start['doc']['pages'])} pages, PROPOSAL), browser storage '{store_ns}'")
    lic = {"open": "can ship", "preview": "preview only", "unknown": "licence unknown"}
    print(f"font trials: {len(cat['fonts'])} ({sum(len(v) for v in font_files.values()) // 1024} KB base64): " +
          ", ".join(f"{f['name']} [{lic[cat['files'][f['file']]['licence']]}]" for f in cat["fonts"]))
    for n in notes:
        print("  note: " + n)
    return info


if __name__ == "__main__":
    main()
