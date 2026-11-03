#!/usr/bin/env python3
"""Lists every player-facing text in the game's code: the keys the language files translate (assets/lang/README.md).
  python3 tools/lang/extract.py            writes assets/lang/keys.txt (one key a line, sorted)
A key is a string literal in src/client (and the server's announcements in src/game/gamemodes.cpp, server.cpp) that
reads like text: a letter in it, no path / code look. printf numbers (%d %i %u %g %.1f ...) become '#', as the game
looks a drawn string up with its digit runs as '#' (lang.cpp). A literal with %s gives its pieces and the whole line
with each %s as "{}", which the game matches as a pattern ("WARMUP - {} TO VOTE START")."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = [os.path.join(ROOT, "src", "client")] 
EXTRA = [os.path.join(ROOT, "src", "game", f) for f in ("gamemodes.cpp", "server.cpp", "gamedefs.h", "settings.h", "trainer.cpp", "trainer.h")]
SKIPFILES = {"surface_looks.h", "font_ui.h", "font_builtin.h", "logo_data.h", "voice_data.h", "sfx_recorded.h", "uitest.cpp", "lang.cpp"}
LIT = re.compile(r'(?<![A-Za-z0-9_])(?:u8)?"((?:[^"\\\n]|\\.)*)"')
NUM = re.compile(r'%[-+0#]*\d*(?:\.\d+)?(?:l|ll|z|h)?[diufgeExX]')   # (no ' ' flag: "10% damage" is text)


def keyish(s):
    if len(s) < 2 or not re.search(r"[A-Za-z]{2}", s): return False
    sentence = len(re.findall(r"[A-Za-z]{2,}", s)) >= 4 and " " in s and not re.search(r"[\\{}\[\]|@`^=]", s)
    shout = re.search(r"[A-Z]{2}", s) and not re.search(r"[a-z]", s)   # (UI text is upper case: "MAX (0=AUTO):", "0 = OFF")
    if re.search(r'[\\/_=<>{}\[\]|@`~^]', s) and not re.search(r"[A-Z]{3}.*[/]", s) and not sentence:
        if not (shout and not re.search(r'[\\_{}\[\]|@`^]', s)): return False
    if re.fullmatch(r"[a-z0-9_.\-]+", s): return False          # (identifiers, file names, config keys)
    if re.fullmatch(r"[a-z]+[A-Z][A-Za-z0-9]*", s): return False   # (camelCase names)
    if not re.match(r"[A-Za-z0-9(\"'#%+\-]", s): return False   # (a fragment: ") - ...", ", mode")
    if s.count("(") != s.count(")"): return False
    if s.startswith(("u_", "v_", "GL_", "uniform", "layout", "vec", "http", "HKLM", "add \"HKLM")): return False
    if re.search(r"\.(png|wav|ogg|qmap|cfg|txt|json|ttf|glb|qpak|exe|dll|so|md|js|html|qdemo)\b", s) and not (sentence and re.search(r" \.(qmap|cfg|qdemo) ", s)): return False
    return True


def main():
    files = []
    for d in SRC:
        for f in sorted(os.listdir(d)):
            if f.endswith((".cpp", ".h")) and f not in SKIPFILES: files.append(os.path.join(d, f))
    files += [f for f in EXTRA if os.path.isfile(f)]
    keys = set()
    for path in files:
        text = open(path, encoding="utf-8", errors="replace").read()
        text = re.sub(r"//[^\n]*", "", text)                     # (comments)
        text = re.sub(r"R\"[A-Za-z]*\(.*?\)[A-Za-z]*\"", "", text, flags=re.S)   # (raw strings: shaders)
        for m in LIT.finditer(text):
            s = bytes(m.group(1), "utf-8").decode("unicode_escape", errors="replace") if "\\" in m.group(1) else m.group(1)
            pieces = s.split("%s") + (s.split("\n") if "\n" in s else [])   # (a multi-line literal: each line too)
            for part in pieces:
                if "%s" in part: continue   # (a line of a multi-line literal with %s in it: its pieces cover it)
                part = NUM.sub("#", part).replace("%%", "%")
                part = part.strip()
                if keyish(part) and "\n" not in part: keys.add(part)
            if "%s" in s and s.count("%s") <= 3:   # the whole line too, each %s a "{}" (lang.cpp matches it as a pattern)
                whole = NUM.sub("#", s).replace("%%", "%").replace("%s", "{}").strip()
                letters = re.sub(r"\{\}|#", "", whole)
                if "\n" not in whole and re.search(r"[A-Za-z]{2}", letters) and keyish(letters.strip() or "x") and "%" not in whole.replace("#%", ""): keys.add(whole)
    out = os.path.join(ROOT, "assets", "lang", "keys.txt")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", newline="\n") as f: f.write("\n".join(sorted(keys)) + "\n")
    print(f"{len(keys)} keys -> {out}")


if __name__ == "__main__":
    main()
