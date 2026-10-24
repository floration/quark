#!/usr/bin/env python3
"""Builds the local Quark Sound Editor: one HTML page plus its sound data, to open from disk (file://).

    build/quark_tests exportassets build/assets_export
    python3 tools/sound_editor/build.py <export_dir> <out_dir>
    python3 tools/sound_editor/build.py <export_dir> <out_dir> --page-only   (only the page, keeps the data)

<export_dir> is what `quark_tests exportassets` writes (manifest.json, sounds/<kit>/<SOUND>.wav,
voices/<voice>/<LINE>.wav). <out_dir> gets:

    sound-editor.html          the editor (index.html with its .css / .js inlined)
    data/manifest.js           every sound and voice line, its description and length guide
    data/kit_<kit>.js          the five sound kits' clips (base64 audio)
    data/voice_<voice>.js      the seven announcer voices' lines (loaded when the page needs them)

A browser blocks fetch() on file://, so the data files are scripts that call window.qseRegister(...).
With ffmpeg on PATH the clips are stored as FLAC (lossless, about half the size); without it, as the WAV
files themselves. Python 3 standard library only.
"""
import base64, hashlib, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

KITS = ["default", "heavy_metal", "magical", "retro", "mouth"]                     # Audio::kitDir order
VOICES = ["male", "female", "british", "old_man", "e_girl", "india", "spy"]         # Audio::voiceDir order


def inline_page():
    """index.html with every local <link rel=stylesheet href=..> and <script src=..> inlined (one file to open)."""
    html = open(os.path.join(HERE, "index.html"), encoding="utf-8").read()

    def css(m):
        return "<style>\n" + open(os.path.join(HERE, m.group(1)), encoding="utf-8").read() + "\n</style>"

    def js(m):
        src = m.group(1)
        if src.startswith("data/"): return m.group(0)                      # (data stays a separate file)
        code = open(os.path.join(HERE, src), encoding="utf-8").read().replace("</script", "<\\/script")
        return "<script>/* %s */\n%s\n</script>" % (src, code)

    html = re.sub(r'<link rel="stylesheet" href="([^"]+)">', css, html)
    html = re.sub(r'<script src="([^"]+)"></script>', js, html)
    return html


def find_ffmpeg():
    exe = shutil.which("ffmpeg")
    if not exe: return None
    try:
        subprocess.run([exe, "-hide_banner", "-version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return exe
    except Exception:
        return None


def encode(path, ffmpeg, tmp):
    """(mime, bytes) of one clip: FLAC through ffmpeg when it's there and works, else the WAV as it is."""
    raw = open(path, "rb").read()
    if ffmpeg:
        out = os.path.join(tmp, "c.flac")
        r = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", path, "-c:a", "flac",
                            "-compression_level", "8", out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0 and os.path.isfile(out):
            data = open(out, "rb").read()
            if 0 < len(data) < len(raw): return "audio/flac", data
    return "audio/wav", raw


def guide_seconds(desc):
    m = re.search(r"<\s*([0-9.]+)\s*s", desc or "")
    return float(m.group(1)) if m else None


def write_js(path, payload):
    with open(path, "w", encoding="utf-8") as f:
        f.write("window.qseRegister(")
        json.dump(payload, f, separators=(",", ":"))
        f.write(");\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit("usage: build.py <export_dir> <out_dir> [--page-only]")
    export, out = os.path.abspath(args[0]), os.path.abspath(args[1])
    if "--page-only" in sys.argv:          # (only the page: the data is already there)
        with open(os.path.join(out, "sound-editor.html"), "w", encoding="utf-8") as f: f.write(inline_page())
        print("page -> " + os.path.join(out, "sound-editor.html")); return
    man_path = os.path.join(export, "manifest.json")
    if not os.path.isfile(man_path):
        sys.exit("no %s: run `build/quark_tests exportassets <dir>` first" % man_path)
    man = json.load(open(man_path, encoding="utf-8"))
    data_dir = os.path.join(out, "data")
    os.makedirs(data_dir, exist_ok=True)
    for n in os.listdir(data_dir):                                         # (old data files only)
        if n.endswith(".js"): os.remove(os.path.join(data_dir, n))

    ffmpeg = find_ffmpeg()
    print("sound editor: clips stored as %s" % ("FLAC (ffmpeg)" if ffmpeg else "WAV (no ffmpeg found)"))

    sounds, lines, items = [], [], {}
    for it in man.get("items", []):
        if it.get("category") not in ("Sounds", "Voices"): continue
        f = os.path.join(export, it["file"])
        if not os.path.isfile(f): continue
        kind = "sound" if it["category"] == "Sounds" else "voice"
        names = sounds if kind == "sound" else lines
        if it["name"] not in names: names.append(it["name"])
        desc = it.get("desc", "")
        when = desc.split(":", 1)[1].strip() if ":" in desc else desc
        if kind == "sound" and " - " in when: when = when.rsplit(" - ", 1)[0]
        items[it["file"][:-4]] = {"kind": kind, "pack": it["kit"], "name": it["name"], "desc": desc, "when": when,
                                  "guide": guide_seconds(desc) if kind == "sound" else None, "src": f}

    # which kits' sounds are DEFAULT's own (the game gives them a DEFAULT override too; RETRO is always DEFAULT chiptuned)
    digest = {k: hashlib.sha256(open(v["src"], "rb").read()).hexdigest() for k, v in items.items()}
    for k, v in items.items():
        if v["kind"] == "sound" and v["pack"] not in ("default", "retro"):
            v["sameAsDefault"] = digest.get(k) == digest.get("sounds/default/" + v["name"])
        if v["kind"] == "sound" and v["pack"] == "retro": v["derived"] = "retro"

    # the owner's own files already in the repository (assets/overrides): shown as "shipped override"
    ov = os.path.join(ROOT, "assets", "overrides")
    owned = set()
    if os.path.isdir(ov):
        for d, _, names in os.walk(ov):
            for n in names:
                rel = os.path.relpath(os.path.join(d, os.path.splitext(n)[0]), ov).replace(os.sep, "/").lower()
                owned.add(rel)
    for k, v in items.items():
        if k.lower() in owned: v["shippedOverride"] = True

    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for group, packs in (("kit", KITS), ("voice", VOICES)):
            for p in packs:
                clips, mime_used = {}, set()
                for k, v in items.items():
                    if v["pack"] != p or v["kind"] != ("sound" if group == "kit" else "voice"): continue
                    mime, data = encode(v["src"], ffmpeg, tmp)
                    mime_used.add(mime)
                    clips[v["name"]] = {"m": mime, "d": base64.b64encode(data).decode("ascii")}
                if not clips:
                    print("  warning: no clips for %s %s" % (group, p)); continue
                path = os.path.join(data_dir, "%s_%s.js" % (group, p))
                write_js(path, {"type": group, "id": p, "clips": clips})
                size = os.path.getsize(path); total += size
                print("  %-22s %3d clips  %6.1f KB" % (os.path.basename(path), len(clips), size / 1024))

    meta = {k: {kk: vv for kk, vv in v.items() if kk != "src"} for k, v in items.items()}
    version = man.get("version") or ""
    write_js(os.path.join(data_dir, "manifest.js"), {"type": "manifest", "version": version, "kits": KITS,
                                                      "voices": VOICES, "sounds": sounds, "lines": lines, "items": meta})
    with open(os.path.join(out, "sound-editor.html"), "w", encoding="utf-8") as f:
        f.write(inline_page())
    print("sound editor %s: %d sounds x %d kits, %d lines x %d voices, %.1f MB of data -> %s" % (
        version, len(sounds), len(KITS), len(lines), len(VOICES), total / 1048576, os.path.join(out, "sound-editor.html")))


main()
