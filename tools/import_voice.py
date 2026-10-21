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
VOICES = [   # (folder, {file: lines}) in voice set order: 0 male, 1 female
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
]

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
        for f, lines in files.items():
            x = load(os.path.join(ROOT, "assets", "voice", folder, f))
            if isinstance(lines[0], tuple): parts = cut_timed(x, lines)
            else: parts = list(zip(lines, split(x, len(lines)) if len(lines) > 1 else [x]))
            for name, p in parts:
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
