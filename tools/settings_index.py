#!/usr/bin/env python3
"""Settings search (1.19.4): every settings widget's label, the page it's on and its help text (the hint lines drawn
under it), read from src/client/game.cpp at build time (a CMake step, like the embedded maps), so the index never goes
stale; plus KEYWORDS below, the words a label and its help miss. Settings > SEARCH draws the pages' own widgets and
matches each against its label, its value names, this help and these keywords.

usage: settings_index.py <out dir> <game.cpp>   -> <out dir>/settings_index.h

A label is the string literal right after the widget's coordinates in a ui_.toggle / slider / cycle / colorPicker /
button call inside one of the settings pages' draw functions. Labels built at run time (with + or a variable) are
skipped. The page is the function's tab, and inside drawMoreSettings / drawControls the branch it's in.
"""
import re
import sys

PAGES = {   # function -> (tab, more page, controls sub-page)
    "drawPlayerSettings": (0, 0, 0), "drawGameplaySettings": (1, 0, 0), "drawControls": (2, 0, 0),
    "drawVideoSettings": (3, 0, 0), "drawCrosshairSettings": (4, 0, 0), "drawHudSettings": (5, 0, 0),
    "drawMoreSettings": (3, 1, 0),
    "drawGameplayMore2": (1, 2, 0), "drawGameplayMore3": (1, 3, 0), "drawGameplayMore4": (1, 4, 0),
    "drawControlsMore2": (2, 2, 0), "drawPlayerMore": (0, 1, 0), "drawHudMore": (5, 1, 0),
}
CALL = re.compile(r'ui_\.(toggle|slider|cycle|colorPicker|button)\((?:[^;"]*?),\s*"([A-Z0-9][A-Z0-9 /:+\'().,%-]*)"\s*,')
HINT = re.compile(r'\.hint\(')
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
# Words a search should find a setting by that its label and help text don't have (owner, 2026-10-30: "typing in screen
# should show the devices, fullscreen setting, ..."). Keyed by the widget's label, or for rows with a run-time label (a
# device, a font) by the first part of their id, before ':'. Upper case; any words.
KEYWORDS = {
    "DISPLAY": "SCREEN FULLSCREEN WINDOWED BORDERLESS MONITOR MODE",
    "WINDOW SIZE": "SCREEN RESOLUTION WIDTH HEIGHT",
    "FULLSCREEN SIZE": "SCREEN RESOLUTION MONITOR MODE",
    "RENDER SCALE": "SCREEN RESOLUTION SUPERSAMPLING UPSCALE DOWNSCALE PERFORMANCE",
    "SHARPEN": "SCREEN RESOLUTION",
    "MONITOR": "SCREEN DISPLAY DEVICE",
    "REFRESH RATE": "SCREEN HZ MONITOR DISPLAY",
    "OUTPUT": "SOUND AUDIO SPEAKERS HEADPHONES DEVICE",
    "INPUT": "MICROPHONE MIC AUDIO DEVICE",
    "PRESENT": "VSYNC TEARING SCREEN",
    "FPS LIMIT": "FRAMERATE FRAME RATE CAP",
    "BRIGHTNESS": "SCREEN GAMMA DARK LIGHT",
    "FOV": "FIELD OF VIEW ZOOM",
    "FOV MODE": "FIELD OF VIEW SCREEN ASPECT",
    "SENSITIVITY": "MOUSE AIM DPI",
    "VOLUME": "SOUND AUDIO LOUD MASTER",
    "FULLBRIGHT": "LIGHTING BRIGHT VISIBILITY",
    "HUD SIZE": "SCALE INTERFACE",
    "ANTI-ALIASING": "MSAA JAGGIES SMOOTH EDGES",
    "SHADOW QUALITY": "GRAPHICS PERFORMANCE",
    "LOW LATENCY": "INPUT LAG DELAY",
    "INTERP": "NETWORK LAG PING NETCODE",
}

SKIP = {"BACK", "RESET", "MORE  >", "PRESS...", "IMPORT", "EXPORT", "COPY CODE", "PASTE CODE", "UNDO", "USE IT", "KEEP", "REVERT"}


def main():
    out, src = sys.argv[1], sys.argv[2]
    lines = open(src, encoding="utf-8").read().split("\n")
    entries, seen = [], set()
    func, page = None, None
    pending, closed = [], False   # (the last row's entries: the hint after it is their help)
    for line in lines:
        m = re.match(r'^void Game::(\w+)\(', line)
        if m:
            func = m.group(1) if m.group(1) in PAGES else None
            page = list(PAGES[func]) if func else None
            continue
        if not func:
            continue
        if line.startswith("}"):
            func = None
            continue
        if func == "drawMoreSettings":   # (its branches: CONTROLS, GAMEPLAY, VIDEO pages 2 and 3, else VIDEO 1)
            if "setTab_ == TAB_CONTROLS" in line: page = [2, 1, 0]
            elif "setTab_ == TAB_GAMEPLAY" in line: page = [1, 1, 0]
            elif "moreOpen_ == 2)" in line: page = [3, 2, 0]
            elif "moreOpen_ == 3)" in line: page = [3, 3, 0]
            elif "moreOpen_ == 4)" in line: page = [3, 4, 0]
            elif "moreOpen_ == 5)" in line: page = [3, 5, 0]
            elif "moreOpen_ == 6)" in line: page = [3, 6, 0]
            elif "moreOpen_ == 7)" in line: page = [3, 7, 0]
            elif "moreOpen_ == 8)" in line: page = [3, 8, 0]
            elif "// VIDEO + AUDIO" in line and "MORE" not in line: page = [3, 1, 0]
        if re.search(r'\.(section|header)\(', line):
            pending, closed = [], False
        if re.search(r'\.next\(\)', line):
            closed = True   # (the row is done: the hint right after it is its help; a widget first starts a new row)
        elif closed and CALL.search(line):
            pending, closed = [], False
        if HINT.search(line) and pending:
            text = " ".join(t for t in LIT.findall(line) if t.strip())
            for e in pending:
                e[4] = (e[4] + " " + text).strip()
            pending = []
        if func == "drawControls":
            if "controlsSub_ == 2)" in line: page = [2, 0, 2]
            elif "controlsSub_ == 1)" in line: page = [2, 0, 1]
            elif line.strip().startswith("} else {"): page = [2, 0, 0]
        for c in CALL.finditer(line):
            label = c.group(2).strip()
            if label in SKIP or len(label) < 3:
                continue
            key = (label, tuple(page))
            if key in seen:
                continue
            seen.add(key)
            e = [label, *page, ""]
            entries.append(e)
            pending.append(e)
    def q(t):
        return t.replace("\\", "\\\\").replace('"', '\\"')
    with open(out + "/settings_index.h", "w", encoding="utf-8") as f:
        f.write("// generated by tools/settings_index.py from src/client/game.cpp: settings search's labels, pages and help\n#pragma once\n")
        f.write("struct SettingsIndexEntry { const char* label; int tab, more, sub; const char* help; };\n")
        f.write("static const SettingsIndexEntry kSettingsIndex[] = {\n")
        for e in entries:
            f.write('    {"%s", %d, %d, %d, "%s"},\n' % (q(e[0]), e[1], e[2], e[3], q(e[4].upper())))
        f.write("};\n")
        f.write("struct SettingsKeywords { const char* key; const char* words; };\n")
        f.write("static const SettingsKeywords kSettingsKeywords[] = {\n")
        for k, v in KEYWORDS.items():
            f.write('    {"%s", "%s"},\n' % (q(k), q(v)))
        f.write("};\n")


if __name__ == "__main__":
    main()
