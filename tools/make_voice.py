#!/usr/bin/env python3
"""Generates src/client/voice_data.h: the announcer's lines in two voices, spoken by Piper (neural text to speech),
given a room and stored as 8-bit samples at 16 kHz in the binary (no sound files to ship).
    pip install piper-tts
    python3 tools/make_voice.py <folder with the .onnx voices> [<vosk model folder>]
With a Vosk speech model (alphacephei.com/vosk/models, e.g. vosk-model-small-en-us) every line is listened to:
of the takes tried (from the most epic to the plainest), the first one heard as the right words is kept.
Voices (from huggingface.co/rhasspy/piper-voices): en_US-ryan-high (DEEP: pitched down, slowed, a big hall)
and en_US-kristin-medium (SMOOTH: a touch slower, softer, a small warm room). Edit LINES / VOICES and re-run."""
import os, re, sys, subprocess, tempfile, wave, struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "client", "voice_data.h")
RATE = 16000
# (id, text): ids match the SND_VOICE_* order in audio.h
LINES = [
    ("THREE", "Three!"), ("TWO", "Two!"), ("ONE", "One!"), ("GO", "Fight!"),
    ("BOMB_PLANTED", "The bomb has been planted."), ("BOMB_DEFUSED", "Bomb defused."),
    ("HILL_TAKEN", "Hill captured."), ("HILL_LOST", "Hill lost."),
    ("POINT_TAKEN", "Point captured."), ("POINT_LOST", "Point lost."),
    ("ROUND_WON", "Round won."), ("ROUND_LOST", "Round lost."), ("OVERTIME", "Overtime!"),
    ("YOU_WIN", "You win!"), ("YOU_LOSE", "You lose."), ("HIGH_SCORE", "New high score!"), ("NEW_LEADER", "New leader!"),
    ("YOU_DIED", "You died."), ("HEADSHOT", "Headshot!"), ("AIR_ROCKET", "Air rocket!"),
    ("SPREE", "Killing spree!"), ("RAMPAGE", "Rampage!"), ("UNSTOPPABLE", "Unstoppable!"), ("GODLIKE", "Godlike!"),
    ("ONE_MINUTE", "One minute remaining."), ("THIRTY_SECONDS", "Thirty seconds remaining."),
    ("ROUND_OVER", "Round over."),
]
# name, model, length scale (slower > 1), pitch (resample: < 1 = deeper and slower), echoes [(delay s, gain)]
HALL = [(0.045, 0.30), (0.09, 0.20), (0.16, 0.12), (0.27, 0.06)]
VOICES = [   # both deep and epic, in a big hall: a man's voice and a woman's
    ("MALE", "en_US-ryan-high|en_GB-alan-medium", 1.12, 0.84, HALL),   # (a line Ryan can't say clearly falls back to Alan)
    ("FEMALE", "en_US-kristin-medium", 1.10, 0.90, HALL),
]
COUNT = {"THREE", "TWO", "ONE"}   # the countdown: each number crisp and short (less slowed, less pitched)

def speak(model, text, length):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "v.wav")
        subprocess.run([sys.executable, "-m", "piper", "-m", model, "-f", p, "--length-scale", str(length)],
                       input=text.encode(), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        w = wave.open(p)
        rate, n = w.getframerate(), w.getnframes()
        x = [v / 32768.0 for v in struct.unpack("<%dh" % n, w.readframes(n))]
    return x, rate

def render(model, text, length, pitch, echoes, count=False):
    if count: length, pitch, echoes = 1.0, min(1.0, pitch + 0.04), []   # each number its own clean word: no hall tail into the next
    x, rate = speak(model, text, length)
    # to RATE, pitched by reading it slower (deeper and slower together, like a big voice)
    out, step, t = [], rate / RATE * pitch, 0.0
    while t < len(x) - 1:
        i = int(t); f = t - i
        out.append(x[i] * (1 - f) + x[i + 1] * f)
        t += step
    a = next((i for i, v in enumerate(out) if abs(v) > 0.01), 0)
    b = len(out) - next((i for i, v in enumerate(reversed(out)) if abs(v) > 0.01), 0)
    out = out[max(0, a - 800):min(len(out), b + 400)]   # (keep the soft start of "th", "f": 50 ms before the first loud sample)
    dry = list(out)
    tail = int(RATE * (max([d for d, g in echoes] + [0.0]) + 0.1))
    out = out + [0.0] * tail
    for (delay, gain) in echoes:
        k = int(RATE * delay)
        for i, v in enumerate(dry): out[i + k] += v * gain
    # gentle compression (soft clip) so the quiet syllables carry, then normalise to -1 dB
    peak = max(1e-6, max(abs(v) for v in out))
    out = [v / peak for v in out]
    out = [v * 1.6 / (1 + abs(v) * 0.6) for v in out]
    peak = max(1e-6, max(abs(v) for v in out))
    return [max(0, min(255, int(round(v / peak * 0.89 * 127 + 128)))) for v in out]

SOUNDS_SAME = {"won": "one", "loss": "lost", "hell": "hill", "fuse": "defused"}   # (the ear can't spell homophones)
def words(t): return [SOUNDS_SAME.get(w, w) for w in re.sub(r"[^a-z ]", "", t.lower()).split()]

def make_judge(path):
    """an offline recogniser (Vosk) as the ear: a line is only kept if it's heard as the words it should say"""
    if not path: return None
    import vosk, json
    vosk.SetLogLevel(-1)
    model = vosk.Model(path)
    def hears(samples):
        pcm = b"".join(((v - 128) * 256).to_bytes(2, "little", signed=True) for v in samples) + bytes(RATE)
        rec = vosk.KaldiRecognizer(model, RATE)
        rec.AcceptWaveform(pcm)
        return json.loads(rec.FinalResult())["text"]
    return hears

def best_take(model, text, length, pitch, echoes, count, hears):
    """try takes from the most epic (the voice's own settings) to the plainest; keep the first that's heard right"""
    takes = [(t, l, p, e) for t in (text, text.rstrip("!.") + ".", text.rstrip("!.") + "...", text.rstrip("!.") + "!")
             for (l, p, e) in ((length, pitch, echoes), (length * 1.1, min(1.0, pitch + 0.04), echoes[:2]), (1.15, min(1.0, pitch + 0.08), []),
                               (1.25, 1.0, []), (1.0, 1.0, []))]
    if count: takes = [tk for tk in takes if not tk[3]] + [tk for tk in takes if tk[3]]   # (numbers: dry first)
    last = None
    for (t, l, p, e) in takes:
        s = render(model, t, l, p, e, False)
        if not hears: return s, True
        if words(hears(s)) == words(text): return s, True
        if l == 1.15 and not e: last = s     # (the plain, clear, dry take: the fallback)
    print("  (no take heard as %r in %s)" % (text, os.path.basename(model)))
    return last, False

def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    hears = make_judge(sys.argv[2] if len(sys.argv) > 2 else None)
    parts = ["// generated by tools/make_voice.py (Piper neural voices): the announcer's lines, 8-bit unsigned at %d Hz" % RATE,
             "#pragma once", "#include <cstdint>", "#include <cstddef>", "constexpr int kVoiceRate = %d;" % RATE,
             "struct VoiceClip { const uint8_t* data; size_t size; };",
             "constexpr int kVoiceSets = %d, kVoiceLines = %d;" % (len(VOICES), len(LINES))]
    sets = []
    for (vname, model, length, pitch, echoes) in VOICES:
        names = []
        for (ident, text) in LINES:
            for k, mdl in enumerate(model.split("|")):
                s, ok = best_take(os.path.join(folder, mdl + ".onnx"), text, length, pitch, echoes, ident in COUNT, hears)
                if ok: break
                if k == 0: fallback = s
            if not ok: s = fallback
            n = "kVoice_%s_%s" % (vname, ident); names.append(n)
            parts.append("static const uint8_t %s[%d] = {%s}; // \"%s\"" % (n, len(s), ",".join(map(str, s)), text))
        sets.append("{" + ", ".join("{%s, sizeof %s}" % (n, n) for n in names) + "}")
    parts.append("static const char* const kVoiceNames[kVoiceSets] = {" + ", ".join('"%s"' % v[0] for v in VOICES) + "};")
    parts.append("static const VoiceClip kVoiceClips[kVoiceSets][kVoiceLines] = {" + ", ".join(sets) + "};")
    open(OUT, "w").write("\n".join(parts) + "\n")
    print("voices written:", ", ".join(v[0] for v in VOICES), "-", len(LINES), "lines each")

main()
