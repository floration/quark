#!/usr/bin/env python3
"""Settings search (1.19.4): every settings widget's label, the page it's on and its help text (the hint lines drawn
under it), read from src/client/game.cpp at build time (a CMake step, like the embedded maps), so the index never goes
stale; plus KEYWORDS below, the words a label and its help miss. Settings > SEARCH draws the pages' own widgets and
matches each against its label, its value names, this help and these keywords.

usage: settings_index.py <out dir> <game.cpp>   -> <out dir>/settings_index.h

A label is the string literal right after the widget's coordinates in a ui_.toggle / slider / cycle / colorPicker /
button call inside one of the settings pages' draw functions. Labels built at run time (with + or a variable) are
skipped. The page is the section (the `case SS_...:` inside drawSetSection) or the function; search places the widgets
itself, so only the label and its help are written.
"""
import re
import sys

FUNCS = {   # the settings draw functions (1.19.9 regroup: every section is a case of drawSetSection, kSetSec in game.cpp)
    "drawSetSection", "drawSetPage", "drawControls", "drawCrosshairSettings", "drawHudSettings", "drawFontSettings",
}
CALL = re.compile(r'(?:ui_\.|\bpct)(toggle|slider|Slider|cycle|colorPicker|button)\((?:[^;"]*?),\s*"([A-Z0-9][A-Z0-9 /:+\'().,%-]*)"\s*,')
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
    "PRESENT": "VSYNC TEARING SCREEN FPS FRAMERATE SYNC",
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
    "MODEL": "PLAYER MODEL CHARACTER SKIN LOOK AVATAR",
    "ENEMY MODEL": "FORCE PLAYER MODELS CHARACTER SKIN",
    "TEAM MODEL": "FORCE PLAYER MODELS CHARACTER SKIN",
    # (settings review 2026-11-07: searches that missed - SENS, FPS, GRAPHICS, MOUSE, VOLUME, LAG, NETWORK)
    "CM PER 360": "SENSITIVITY SENS MOUSE AIM DPI EDPI",
    "FROM": "SENSITIVITY SENS CONVERT CONVERTER OTHER GAME CS2 VALORANT APEX OVERWATCH QUAKE MOUSE",
    "THEIRS": "SENSITIVITY SENS CONVERT CONVERTER OTHER GAME CS2 VALORANT APEX OVERWATCH QUAKE MOUSE",
    "INVERT Y": "MOUSE AIM LOOK FLIP",
    "MOUSE ACCELERATION": "SENSITIVITY SENS AIM",
    "RAW INPUT": "MOUSE SENSITIVITY SENS AIM",
    "WEAPON SENSITIVITY": "SENS MOUSE ZOOM SCOPE AIM",
    "PRESET": "GRAPHICS QUALITY PERFORMANCE FPS LOW MEDIUM HIGH",
    "FPS LIMIT": "FRAMERATE FRAME RATE CAP UNCAP PERFORMANCE",
    "G-SYNC / FREESYNC": "VSYNC VRR ADAPTIVE TEARING FPS",
    "SHOW FPS": "FRAMERATE PERFORMANCE COUNTER OVERLAY",
    "SHOW PING": "LATENCY LAG NETWORK OVERLAY",
    "NET GRAPH": "NETWORK LAG PING LATENCY PACKET LOSS OVERLAY",
    "PACKET LOSS PROTECTION": "NETWORK LAG PING NETCODE",
    "MAX PING": "NETWORK LAG LATENCY SERVER",
    "MY FOOTSTEPS": "SOUND AUDIO VOLUME",
    "OTHERS' FOOTSTEPS": "SOUND AUDIO VOLUME ENEMY STEPS",
    "GUNFIRE": "SOUND AUDIO VOLUME SHOTS",
    "LANGUAGE": "TRANSLATION LOCALE",
    "ENEMY COLOR": "BRIGHT VISIBILITY",
    "ENEMY OUTLINE": "BRIGHT VISIBILITY GLOW",
    "WEAPON MODEL OPACITY": "VIEWMODEL HIDE GUN HAND",
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
            func = m.group(1) if m.group(1) in FUNCS else None
            page = func
            continue
        if not func:
            continue
        if line.startswith("}"):
            func = None
            continue
        m = re.match(r'^\s*case (SS_\w+):', line)
        if m and func == "drawSetSection":
            page, pending, closed = m.group(1), [], False
        if re.search(r'\.(section|header)\(', line):
            pending, closed = [], False
        if re.search(r'\.next\(\)', line):
            closed = True   # (the row is done: the hint right after it is its help; a widget first starts a new row)
        elif closed and CALL.search(line):
            pending, closed = [], False
        if HINT.search(line) and pending:
            text = " ".join(t for t in LIT.findall(line) if t.strip())
            for e in pending:
                e[1] = (e[1] + " " + text).strip()
            pending = []
        for c in CALL.finditer(line):
            label = c.group(2).strip()
            if label in SKIP or len(label) < 3:
                continue
            key = (label, page)
            if key in seen:
                continue
            seen.add(key)
            e = [label, ""]
            entries.append(e)
            pending.append(e)
    def q(t):
        return t.replace("\\", "\\\\").replace('"', '\\"')
    with open(out + "/settings_index.h", "w", encoding="utf-8") as f:
        f.write("// generated by tools/settings_index.py from src/client/game.cpp: settings search's labels, pages and help\n#pragma once\n")
        f.write("struct SettingsIndexEntry { const char* label; const char* help; };\n")
        f.write("static const SettingsIndexEntry kSettingsIndex[] = {\n")
        for e in entries:
            f.write('    {"%s", "%s"},\n' % (q(e[0]), q(e[1].upper())))
        f.write("};\n")
        f.write("struct SettingsKeywords { const char* key; const char* words; };\n")
        f.write("static const SettingsKeywords kSettingsKeywords[] = {\n")
        for k, v in KEYWORDS.items():
            f.write('    {"%s", "%s"},\n' % (q(k), q(v)))
        f.write("};\n")


if __name__ == "__main__":
    main()
