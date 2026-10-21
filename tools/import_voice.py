#!/usr/bin/env python3
"""Generates src/client/voice_recorded.h: the recorded announcer lines, male (assets/voice/male/) and female
(assets/voice/female/), trimmed of silence, resampled to 16 kHz and stored as 16-bit samples in the binary.
    python3 tools/import_voice.py
VOICES maps each file to the announcer line(s) in it (line ids: LINES in tools/make_voice.py, the SND_VOICE_* order):
  ["A", "B"]                 split at its pauses, one part per line (the countdown: "3, 2, 1, fight")
  [("A", t0, t1), ...]       one file holding many lines: each line's start / end (s), cut halfway between lines
Lines without a recording use that voice's generated line (voice_data.h). Add one: drop the .wav in, list it, re-run."""
import os, struct, wave

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "src", "client", "voice_recorded.h")
RATE = 16000
# the whole script in one take, in this order (female, british, old man, e-girl, india, spy)
ALL = ["THREE", "TWO", "ONE", "GO", "YOU_DIED", "HEADSHOT", "AIR_ROCKET", "SPREE", "RAMPAGE", "GODLIKE", "UNSTOPPABLE",
       "ONE_MINUTE", "THIRTY_SECONDS", "YOU_WIN", "YOU_LOSE", "ROUND_OVER"]
VOICES = [   # (folder, {file: lines}) in voice set order: 0 male, 1 female, then the recorded-only voices
    ("male", {
        "321fight.wav": ["THREE", "TWO", "ONE", "GO"],
        "youwin.wav": ["YOU_WIN"], "youlose.wav": ["YOU_LOSE"], "roundover.wav": ["ROUND_OVER"],
        "oneminuteremaining.wav": ["ONE_MINUTE"], "30secondsremaining.wav": ["THIRTY_SECONDS"],
        "killing_spree.wav": ["SPREE"], "rampage.wav": ["RAMPAGE"], "unstoppable.wav": ["UNSTOPPABLE"], "godlike.wav": ["GODLIKE"],
        "headshot.wav": ["HEADSHOT"], "youdied.wav": ["YOU_DIED"], "direct.wav": ["AIR_ROCKET"],
    }),
    ("female", {   # one take of everything; the times are where each line is (found with the Vosk recogniser)
        "all.wav": [("THREE", 0.99, 1.65), ("TWO", 2.01, 2.67), ("ONE", 2.94, 3.63), ("GO", 3.96, 4.77),
                    ("YOU_DIED", 4.92, 6.00), ("HEADSHOT", 6.33, 7.20), ("AIR_ROCKET", 7.47, 8.31), ("SPREE", 8.67, 9.93),
                    ("RAMPAGE", 10.20, 11.43), ("GODLIKE", 11.76, 12.75), ("UNSTOPPABLE", 13.14, 14.70),
                    ("ONE_MINUTE", 15.18, 16.68), ("THIRTY_SECONDS", 17.13, 18.81), ("YOU_WIN", 19.17, 20.16),
                    ("YOU_LOSE", 20.67, 21.90), ("ROUND_OVER", 22.23, 23.76)],
    }),
    ("british", {"all.wav": [("THREE", 0.90, 1.59), ("TWO", 1.86, 2.43), ("ONE", 2.79, 3.39), ("GO", 3.75, 4.62),
                             ("YOU_DIED", 5.31, 6.24), ("HEADSHOT", 6.63, 7.41), ("AIR_ROCKET", 7.71, 8.61), ("SPREE", 9.09, 10.32),
                             ("RAMPAGE", 10.68, 11.91), ("GODLIKE", 12.25, 13.44), ("UNSTOPPABLE", 13.89, 15.42),
                             ("ONE_MINUTE", 16.20, 17.73), ("THIRTY_SECONDS", 18.42, 20.22), ("YOU_WIN", 20.70, 21.72),
                             ("YOU_LOSE", 22.20, 23.34), ("ROUND_OVER", 23.73, 25.11)]}),
    ("oldman", {"all.wav": [("THREE", 1.17, 1.83), ("TWO", 2.22, 2.70), ("ONE", 3.30, 3.87), ("GO", 4.62, 5.25),
                            ("YOU_DIED", 6.09, 7.38), ("HEADSHOT", 7.80, 8.76), ("AIR_ROCKET", 9.09, 9.99), ("SPREE", 10.56, 11.88),
                            ("RAMPAGE", 12.33, 13.83), ("GODLIKE", 14.40, 15.51), ("UNSTOPPABLE", 15.99, 17.58),
                            ("ONE_MINUTE", 18.30, 19.92), ("THIRTY_SECONDS", 20.58, 22.41), ("YOU_WIN", 22.92, 24.21),
                            ("YOU_LOSE", 24.75, 26.34), ("ROUND_OVER", 26.82, 28.53)]}),
] + [(v, {"all.wav": ALL}) for v in ("egirl", "india", "spy")]   # same script as female, clear pauses: split at them

# the second take (more.wav in each voice's folder): the mode calls, in this order; times found with the Vosk recogniser
MORE = {
    "male": [('BOMB_PLANTED', 1.41, 3.42), ('BOMB_DEFUSED', 4.2, 5.88), ('HILL_TAKEN', 6.33, 7.65), ('HILL_LOST', 8.22, 9.54), ('POINT_TAKEN', 10.02, 11.4), ('POINT_LOST', 11.88, 13.35), ('ROUND_WON', 13.62, 15.27), ('ROUND_LOST', 15.39, 16.86), ('OVERTIME', 17.31, 18.42), ('HIGH_SCORE', 18.9, 20.88), ('NEW_LEADER', 21.24, 23.04)],
    "female": [('BOMB_PLANTED', 0.75, 2.6), ('BOMB_DEFUSED', 3.15, 4.59), ('HILL_TAKEN', 5.01, 6.15), ('HILL_LOST', 6.6, 7.71), ('POINT_TAKEN', 8.16, 9.27), ('POINT_LOST', 9.66, 10.8), ('ROUND_WON', 11.19, 12.36), ('ROUND_LOST', 12.69, 13.98), ('OVERTIME', 14.46, 15.45), ('HIGH_SCORE', 15.66, 17.1), ('NEW_LEADER', 17.37, 18.6)],
    "british": [('BOMB_PLANTED', 1.05, 2.67), ('BOMB_DEFUSED', 3.39, 4.92), ('HILL_TAKEN', 5.34, 6.51), ('HILL_LOST', 6.96, 8.16), ('POINT_TAKEN', 8.79, 9.93), ('POINT_LOST', 10.41, 11.64), ('ROUND_WON', 12.18, 13.47), ('ROUND_LOST', 13.83, 15.33), ('OVERTIME', 16.08, 17.04), ('HIGH_SCORE', 17.43, 19.08), ('NEW_LEADER', 19.44, 20.85)],
    "oldman": [('BOMB_PLANTED', 0.75, 2.94), ('BOMB_DEFUSED', 3.69, 5.46), ('HILL_TAKEN', 6.12, 7.47), ('HILL_LOST', 8.16, 9.6), ('POINT_TAKEN', 10.41, 11.76), ('POINT_LOST', 12.48, 13.92), ('ROUND_WON', 14.73, 16.23), ('ROUND_LOST', 16.74, 18.45), ('OVERTIME', 19.2, 20.4), ('HIGH_SCORE', 20.88, 22.83), ('NEW_LEADER', 23.34, 25.08)],
    "egirl": [('BOMB_PLANTED', 0.9, 2.69), ('BOMB_DEFUSED', 3.21, 4.5), ('HILL_TAKEN', 4.86, 6.06), ('HILL_LOST', 6.45, 7.56), ('POINT_TAKEN', 8.01, 9.18), ('POINT_LOST', 9.51, 10.68), ('ROUND_WON', 11.1, 12.24), ('ROUND_LOST', 12.6, 13.89), ('OVERTIME', 14.37, 15.3), ('HIGH_SCORE', 15.66, 17.1), ('NEW_LEADER', 17.49, 18.6)],
    "india": [('BOMB_PLANTED', 1.05, 2.64), ('BOMB_DEFUSED', 3.27, 4.47), ('HILL_TAKEN', 5.04, 6.09), ('HILL_LOST', 6.66, 7.68), ('POINT_TAKEN', 8.37, 9.51), ('POINT_LOST', 10.14, 11.31), ('ROUND_WON', 12.0, 13.05), ('ROUND_LOST', 13.59, 14.82), ('OVERTIME', 15.54, 16.5), ('HIGH_SCORE', 17.07, 18.39), ('NEW_LEADER', 18.87, 19.98)],
    "spy": [('BOMB_PLANTED', 1.95, 3.92), ('BOMB_DEFUSED', 4.35, 5.94), ('HILL_TAKEN', 6.24, 7.56), ('HILL_LOST', 7.92, 9.18), ('POINT_TAKEN', 9.54, 10.74), ('POINT_LOST', 11.04, 12.18), ('ROUND_WON', 12.54, 13.86), ('ROUND_LOST', 14.16, 15.57), ('OVERTIME', 15.93, 17.04), ('HIGH_SCORE', 17.31, 19.14), ('NEW_LEADER', 19.53, 20.88), ('_SKIP', 21.09, 22.38)],
}

def line_ids():
    """the announcer line order, from make_voice.py's LINES (without running it)"""
    src = open(os.path.join(ROOT, "tools", "make_voice.py")).read()
    start = src.index("LINES = [")
    body = src[start:src.index("\n]", start)]
    import re
    return re.findall(r'\("([A-Z_0-9]+)", "', body)

def load(path):
    w = wave.open(path)
    n, ch, sw, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    if sw != 2: raise SystemExit(path + ": 16-bit wav only")
    x = struct.unpack("<%dh" % (n * ch), raw)
    if ch > 1: x = [sum(x[i:i + ch]) / ch for i in range(0, len(x), ch)]
    x = [v / 32768.0 for v in x]
    # to RATE (linear; the sources are 44.1 / 48 kHz voice)
    out, step, t = [], rate / RATE, 0.0
    while t < len(x) - 1:
        i = int(t); f = t - i
        out.append(x[i] * (1 - f) + x[i + 1] * f)
        t += step
    return out

def envelope(x, win):
    return [max(abs(v) for v in x[i:i + win]) for i in range(0, len(x), win)]

def trim(x, lead=0.03, tail=0.12):
    """cut silence before and after (below 1.5% of the peak), keeping a little air and the reverb tail"""
    peak = max(1e-6, max(abs(v) for v in x))
    on = [i for i, v in enumerate(x) if abs(v) > peak * 0.015]
    if not on: return x
    a = max(0, on[0] - int(lead * RATE)); b = min(len(x), on[-1] + int(tail * RATE))
    return x[a:b]

def split(x, parts):
    """split at the longest quiet gaps (50 ms windows under 2% of the peak) into `parts` pieces"""
    win = RATE // 20
    env = envelope(x, win)
    peak = max(env)
    quiet = [e < peak * 0.02 for e in env]
    gaps, i = [], 0
    while i < len(quiet):          # runs of quiet windows between sound
        if quiet[i]:
            j = i
            while j < len(quiet) and quiet[j]: j += 1
            if i > 0 and j < len(quiet): gaps.append((j - i, (i + j) // 2))
            i = j
        else: i += 1
    cuts = sorted(c for _, c in sorted(gaps, reverse=True)[:parts - 1])
    if len(cuts) != parts - 1: raise SystemExit("couldn't find %d pauses to split at" % (parts - 1))
    bounds = [0] + [c * win for c in cuts] + [len(x)]
    return [x[bounds[k]:bounds[k + 1]] for k in range(parts)]

def cut_timed(x, spans):
    """one recording, many lines at known times: cut halfway between each line and the next"""
    out = []
    for k, (name, t0, t1) in enumerate(spans):
        a = 0.0 if k == 0 else (spans[k - 1][2] + t0) / 2
        b = len(x) / RATE if k + 1 == len(spans) else (t1 + spans[k + 1][1]) / 2
        out.append((name, x[int(a * RATE):int(b * RATE)]))
    return out

def main():
    ids = line_ids()
    clips = {}   # (voice set, line name) -> samples
    for vset, (folder, files) in enumerate(VOICES):
        files = dict(files)
        if folder in MORE: files["more.wav"] = MORE[folder]
        for f, lines in files.items():
            x = load(os.path.join(ROOT, "assets", "voice", folder, f))
            if isinstance(lines[0], tuple): parts = cut_timed(x, lines)
            else: parts = list(zip(lines, split(x, len(lines)) if len(lines) > 1 else [x]))
            for name, p in parts:
                if name.startswith("_"): continue   # (a part to drop)
                p = trim(p)
                peak = max(1e-6, max(abs(v) for v in p))
                clips[(vset, name)] = [int(round(v / peak * 0.89 * 32767)) for v in p]   # each line to -1 dB
                print("  %-6s %-15s %.2f s  (%s)" % (folder, name, len(p) / RATE, f))
    parts = ["// generated by tools/import_voice.py from assets/voice/<male|female>/*.wav: the recorded announcer lines,",
             "// 16-bit at %d Hz; a line with no recording uses that voice's generated one (voice_data.h)" % RATE,
             "#pragma once", "#include <cstdint>", "#include <cstddef>", "constexpr int kRecordedRate = %d;" % RATE,
             "struct RecordedClip { int set, line; const int16_t* data; size_t size; };   // set: 0 male, 1 female; line: SND_VOICE_* - SND_VOICE_THREE"]
    entries = []
    for vset, (folder, _) in enumerate(VOICES):
        for name in ids:
            if (vset, name) not in clips: continue
            s = clips[(vset, name)]
            var = "kRec_%s_%s" % (folder, name)
            parts.append("static const int16_t %s[%d] = {%s};" % (var, len(s), ",".join(map(str, s))))
            entries.append("{%d, %d, %s, sizeof %s / 2}" % (vset, ids.index(name), var, var))
    parts.append("static const RecordedClip kRecordedVoice[] = {%s};" % ", ".join(entries))
    parts.append("constexpr int kRecordedLines = %d;" % len(entries))
    open(OUT, "w").write("\n".join(parts) + "\n")
    print("wrote", OUT, "-", len(entries), "recorded lines")

main()
