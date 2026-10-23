#!/usr/bin/env python3
"""Generates src/client/sfx_recorded.h: the recorded sound effects of the HEAVY METAL, MAGICAL and WOODEN sound kits,
from the CC0 clips in assets/sfx/ (sources and licenses: assets/sfx/CREDITS.md). Each sound is one or more layers
(clip, gain, pitch, delay) mixed together, its leading silence trimmed, cut to a length with a fade-out, mono 16-bit at
22.05 kHz. The game matches each one's loudness to the DEFAULT kit's sound at startup (audio.cpp), so levels stay even.
    python3 tools/import_sfx.py          (needs ffmpeg on PATH to decode the .ogg clips; the game doesn't)
DEFAULT and RETRO are synthesized (audio.cpp); a sound missing from a kit here falls back to the DEFAULT one.

Your own sounds: put a file at assets/sfx/custom/<kit>/<SOUND>.<wav|ogg|mp3|flac> (kit: default, heavy_metal, magical,
retro, wooden; SOUND: a name from the list in assets/sfx/custom/README.md, e.g. ROCKET.wav) and run this again. It
replaces that sound in that kit (any kit, DEFAULT and RETRO too), as is: trimmed, mono, normalised, up to 3 s."""
import math, os, re, struct, subprocess

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "assets", "sfx")
OUT = os.path.join(ROOT, "src", "client", "sfx_recorded.h")
RATE = 22050

def L(f, gain=1.0, pitch=1.0, delay=0.0): return (f, gain, pitch, delay)   # f: under assets/sfx/, .ogg unless it says

# kit index (the setting's order: 0 DEFAULT, 1 HEAVY METAL, 2 MAGICAL, 3 RETRO, 4 WOODEN) -> sound -> (max seconds, layers
# [, drive: tanh saturation, the crunch of the heavier hitsound tiers])
KITS = {
    1: {   # HEAVY METAL: struck steel plates, bells, pots and real explosions
        "HIT": (0.25, [L("impact/impactMetal_light_000", pitch=1.2)]),
        "KILL": (0.6, [L("impact/impactBell_heavy_004", pitch=1.1), L("impact/impactMetal_light_003", 0.6, 1.3, 0.07)]),
        "HURT": (0.3, [L("impact/impactPlate_heavy_000")]),
        "STEP": (0.15, [L("impact/footstep_concrete_000"), L("impact/impactPlate_light_000", 0.25)]),
        "JUMP": (0.15, [L("impact/impactTin_medium_000", 0.7, 1.1)]),
        "LAND": (0.3, [L("impact/impactPlate_heavy_001", pitch=0.85)]),
        "RIFLE": (0.3, [L("impact/impactMetal_heavy_002"), L("scifi/explosionCrunch_000", 0.35, 1.6)]),
        "ROCKET": (0.7, [L("scifi/explosionCrunch_001", 0.6, 1.3), L("impact/impactMetal_heavy_001", 0.8, 0.8)]),
        "KNIFE": (0.35, [L("rpg/drawKnife1")]),
        "SNIPER": (1.0, [L("impact/impactMetal_heavy_000", pitch=0.8), L("scifi/lowFrequency_explosion_001", 0.7, 1.2), L("impact/impactBell_heavy_002", 0.25, 1.4)]),
        "LASER": (0.08, [L("impact/impactMetal_medium_004", pitch=1.4)]),
        "SHOTGUN": (0.6, [L("impact/impactMetal_heavy_003", pitch=0.75), L("scifi/explosionCrunch_002", 0.7, 1.2)]),
        "STICKY": (0.3, [L("rpg/metalLatch")]),
        "CROSSBOW": (0.35, [L("impact/impactMetal_medium_001", pitch=0.9), L("rpg/metalClick", 0.5)]),
        "BUBBLE": (0.3, [L("rpg/metalPot2", pitch=1.3)]),
        "SYRINGE": (0.2, [L("rpg/metalClick", pitch=1.3)]),
        "EXPLOSION": (1.3, [L("scifi/explosionCrunch_003"), L("scifi/lowFrequency_explosion_000", 0.8), L("impact/impactMetal_heavy_001", 0.4, 0.7, 0.03)]),
        "STICKY_ATTACH": (0.12, [L("impact/impactTin_medium_001", pitch=1.2)]),
        "BOLT_HIT": (0.2, [L("impact/impactMetal_medium_002")]),
        "DASH": (0.35, [L("scifi/thrusterFire_000", pitch=1.2)]),
        "GRAPPLE_FIRE": (0.25, [L("rpg/metalLatch", pitch=0.8), L("rpg/beltHandle1", 0.5)]),
        "GRAPPLE_HIT": (0.4, [L("impact/impactBell_heavy_003", pitch=1.3)]),
        "DEATH": (0.8, [L("impact/impactPlate_heavy_002", pitch=0.7), L("rpg/metalPot1", 0.6, 0.8, 0.08)]),
        "STREAK": (0.6, [L("impact/impactBell_heavy_002", pitch=1.2), L("impact/impactBell_heavy_004", 0.7, 1.6, 0.1)]),
        "TYPING": (0.06, [L("rpg/metalClick", pitch=1.6)]),
        "CHECKPOINT": (0.5, [L("impact/impactBell_heavy_004", pitch=1.5)]),
        "FINISH": (1.0, [L("impact/impactBell_heavy_004", pitch=1.3), L("impact/impactBell_heavy_004", 0.9, 1.65, 0.1), L("impact/impactBell_heavy_002", 0.9, 2.0, 0.2)]),
        # the hitsound's tiers (HIT is the light one): a lower clank, a plate slam with a crunch, a pitched-down steel wreck
        "HIT_MED": (0.3, [L("impact/impactMetal_medium_004", pitch=0.85), L("impact/impactMetal_light_000", 0.6, 0.9), L("impact/impactTin_medium_001", 0.4, 0.9)], 1.5),
        "HIT_HEAVY": (0.45, [L("impact/impactMetal_medium_001", pitch=0.8), L("impact/impactPlate_heavy_001", 0.7, 1.1), L("scifi/explosionCrunch_000", 0.35, 2.0)], 2.5),
        "HIT_BRUTAL": (0.6, [L("impact/impactPlate_heavy_000", pitch=0.75), L("impact/impactMetal_heavy_002", 0.8, 0.7), L("scifi/explosionCrunch_001", 0.6, 1.6), L("rpg/metalPot1", 0.3, 0.6, 0.015)], 4.0),
    },
    2: {   # MAGICAL: glass chimes, shimmering force fields, plucks and whooshes (with a hall reverb added in audio.cpp)
        "HIT": (0.2, [L("iface/glass_002")]),
        "KILL": (0.5, [L("iface/glass_001"), L("iface/glass_003", 0.8, 1.5, 0.08)]),
        "HURT": (0.3, [L("impact/impactGlass_light_000", pitch=0.8), L("impact/impactSoft_medium_000", 0.6)]),
        "STEP": (0.15, [L("impact/footstep_snow_000")]),
        "JUMP": (0.2, [L("iface/maximize_007", 0.7)]),
        "LAND": (0.3, [L("impact/impactSoft_heavy_000"), L("iface/glass_006", 0.25, 0.7)]),
        "RIFLE": (0.25, [L("scifi/laserSmall_000", pitch=1.2), L("iface/glass_005", 0.4, 1.3)]),
        "ROCKET": (0.7, [L("scifi/forceField_001", pitch=1.2), L("scifi/laserLarge_000", 0.5, 0.8)]),
        "KNIFE": (0.35, [L("rpg/drawKnife2", pitch=1.3), L("iface/glass_006", 0.4, 1.6)]),
        "SNIPER": (0.9, [L("scifi/laserLarge_001"), L("impact/impactBell_heavy_003", 0.5, 1.8)]),
        "LASER": (0.08, [L("iface/glass_005", pitch=1.6)]),
        "SHOTGUN": (0.6, [L("impact/impactGlass_heavy_000"), L("scifi/forceField_002", 0.6, 1.4)]),
        "STICKY": (0.3, [L("scifi/slime_000", pitch=1.2)]),
        "CROSSBOW": (0.3, [L("iface/pluck_002"), L("iface/maximize_003", 0.4, 1.2)]),
        "BUBBLE": (0.25, [L("iface/drop_002")]),
        "SYRINGE": (0.25, [L("iface/confirmation_001", pitch=1.2)]),
        "EXPLOSION": (1.3, [L("impact/impactGlass_heavy_001", pitch=0.8), L("scifi/lowFrequency_explosion_001", 0.9), L("scifi/forceField_000", 0.5, 0.7)]),
        "STICKY_ATTACH": (0.15, [L("iface/drop_001", pitch=1.2)]),
        "BOLT_HIT": (0.25, [L("impact/impactGlass_medium_000")]),
        "DASH": (0.35, [L("scifi/forceField_003", pitch=1.6), L("iface/maximize_006", 0.5)]),
        "GRAPPLE_FIRE": (0.25, [L("iface/maximize_003", pitch=0.9)]),
        "GRAPPLE_HIT": (0.4, [L("impact/impactBell_heavy_004", pitch=1.8), L("iface/glass_003", 0.5)]),
        "DEATH": (0.8, [L("iface/minimize_005", pitch=0.7), L("iface/glass_004", 0.5, 0.6)]),
        "STREAK": (0.6, [L("iface/confirmation_002")]),
        "TYPING": (0.06, [L("iface/tick_001")]),
        "CHECKPOINT": (0.4, [L("iface/glass_001", pitch=1.2), L("iface/glass_003", 0.6, 1.8, 0.05)]),
        "FINISH": (0.9, [L("iface/confirmation_004"), L("iface/glass_001", 0.6, 1.5, 0.15), L("iface/glass_003", 0.5, 2.0, 0.25)]),
        # the hitsound's tiers: the chime lower over a glass tap and a soft knock, cracking glass in a force field, a shattering pane and a boom
        "HIT_MED": (0.3, [L("iface/glass_002", pitch=0.8), L("impact/impactGlass_light_000", 0.6, 1.1), L("impact/impactSoft_medium_000", 0.5, 1.2)], 1.4),
        "HIT_HEAVY": (0.4, [L("impact/impactGlass_medium_000", pitch=0.85), L("iface/glass_002", 0.5, 0.7), L("scifi/forceField_002", 0.35, 1.8)], 2.2),
        "HIT_BRUTAL": (0.55, [L("impact/impactGlass_heavy_001"), L("scifi/forceField_000", 0.6, 1.1), L("scifi/lowFrequency_explosion_000", 0.45, 1.4), L("iface/glass_004", 0.4, 0.8, 0.02)], 3.5),
    },
    4: {   # WOODEN: knocks, planks, chops, creaks and doors slamming
        "HIT": (0.2, [L("impact/impactWood_light_000", pitch=1.2)]),
        "KILL": (0.5, [L("impact/impactWood_medium_000", pitch=1.1), L("impact/impactWood_light_001", 0.9, 1.5, 0.08)]),
        "HURT": (0.3, [L("impact/impactWood_heavy_000", pitch=0.9)]),
        "STEP": (0.15, [L("impact/footstep_wood_000")]),
        "JUMP": (0.15, [L("impact/footstep_wood_001", 0.6, 1.2)]),
        "LAND": (0.3, [L("impact/impactWood_heavy_001", pitch=0.8)]),
        "RIFLE": (0.3, [L("impact/impactPlank_medium_000", pitch=1.2)]),
        "ROCKET": (0.6, [L("rpg/doorClose_1"), L("impact/impactWood_heavy_002", 0.6, 0.7)]),
        "KNIFE": (0.25, [L("rpg/chop")]),
        "SNIPER": (0.8, [L("rpg/doorClose_2"), L("impact/impactPlank_medium_001", 0.6, 0.8)]),
        "LASER": (0.08, [L("impact/impactWood_light_002", pitch=1.6)]),
        "SHOTGUN": (0.6, [L("impact/impactWood_heavy_003", pitch=0.75), L("impact/impactPlank_medium_002", 0.8, 0.9)]),
        "STICKY": (0.3, [L("casino/die-throw-1")]),
        "CROSSBOW": (0.35, [L("impact/impactPlank_medium_003", pitch=1.3), L("rpg/creak3", 0.4, 1.4)]),
        "BUBBLE": (0.2, [L("impact/impactWood_light_003", pitch=1.8)]),
        "SYRINGE": (0.15, [L("impact/impactWood_light_004", pitch=2.0)]),
        "EXPLOSION": (1.2, [L("rpg/doorClose_3", pitch=0.7), L("impact/impactWood_heavy_004", 0.8, 0.6), L("scifi/lowFrequency_explosion_001", 0.6, 0.9)]),
        "STICKY_ATTACH": (0.12, [L("impact/impactWood_light_000", pitch=1.6)]),
        "BOLT_HIT": (0.2, [L("impact/impactPlank_medium_004", pitch=1.3)]),
        "DASH": (0.35, [L("rpg/cloth1", pitch=1.2)]),
        "GRAPPLE_FIRE": (0.3, [L("rpg/creak3", pitch=1.2)]),
        "GRAPPLE_HIT": (0.35, [L("impact/impactWood_medium_001", pitch=1.2)]),
        "DEATH": (0.7, [L("rpg/doorClose_4", pitch=0.8)]),
        "STREAK": (0.5, [L("impact/impactWood_medium_002", pitch=1.3), L("impact/impactWood_medium_003", 0.9, 1.8, 0.1)]),
        "TYPING": (0.06, [L("impact/impactWood_light_001", pitch=2.2)]),
        "CHECKPOINT": (0.3, [L("impact/impactWood_light_002", pitch=2.0), L("impact/impactWood_light_003", 0.9, 2.5, 0.07)]),
        "FINISH": (0.5, [L("impact/impactWood_light_002", pitch=1.8), L("impact/impactWood_light_003", 0.9, 2.25, 0.09), L("impact/impactWood_light_004", 1.0, 2.7, 0.18)]),
        # (kit 4 is MOUTH now: its sounds above are replaced by the owner's recordings in custom/mouth/. The hitsound's
        # tiers are made from those: the HIT pitched down, layered with the kit's own LAND, HURT, SHOTGUN, EXPLOSION)
        "HIT_MED": (0.25, [L("custom/mouth/HIT.wav", pitch=0.85), L("custom/mouth/SHOTGUN.wav", 0.4, 1.3)], 1.5),
        "HIT_HEAVY": (0.35, [L("custom/mouth/HIT.wav", pitch=0.7), L("custom/mouth/LAND.wav", 0.7), L("custom/mouth/SHOTGUN.wav", 0.4, 1.0)], 2.5),
        "HIT_BRUTAL": (0.5, [L("custom/mouth/HIT.wav", pitch=0.58), L("custom/mouth/HURT.wav", 0.6, 0.8), L("custom/mouth/EXPLOSION.wav", 0.5, 1.3), L("custom/mouth/SHOTGUN.wav", 0.5, 0.8, 0.02)], 4.0),
    },
}

def sound_ids():
    """SND_* names in enum order (from audio.h, without compiling it)"""
    src = open(os.path.join(ROOT, "src", "client", "audio.h")).read()
    body = re.sub(r"//[^\n]*", "", src[src.index("SND_HIT"):src.index("SND_COUNT")])
    return [n[4:] for n in re.findall(r"SND_[A-Z_]+", body)]

_cache = {}
def decode(name, pitch):
    """mono float samples at RATE / pitch (played back at RATE: pitch up / down, shorter / longer)"""
    key = (name, pitch)
    if key not in _cache:
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", os.path.join(SRC, name if os.path.splitext(name)[1] else name + ".ogg"), "-ac", "1",
                              "-ar", str(int(round(RATE / pitch))), "-f", "s16le", "-"], capture_output=True, check=True).stdout
        x = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)]
        peak = max(1e-6, max(abs(v) for v in x))
        on = next(i for i, v in enumerate(x) if abs(v) > peak * 0.03)
        _cache[key] = x[max(0, on - int(0.002 * RATE)):]   # leading silence off: the sound starts the instant it plays
    return _cache[key]

def build(maxlen, layers, drive=0.0):
    n = int(maxlen * RATE)
    out = [0.0] * n
    for f, gain, pitch, delay in layers:
        x = decode(f, pitch)
        d = int(delay * RATE)
        for i in range(min(len(x), n - d)): out[d + i] += x[i] * gain
    if drive > 0:   # saturate: the mix at peak 1 through tanh (crunch, more body at the same peak)
        peak = max(1e-6, max(abs(v) for v in out))
        k = 1.0 / math.tanh(drive)
        out = [math.tanh(v / peak * drive) * k for v in out]
    # drop the silent tail, then a 15 ms fade so it never clicks
    peak = max(1e-6, max(abs(v) for v in out))
    last = max(i for i, v in enumerate(out) if abs(v) > peak * 0.01)
    out = out[:min(n, last + int(0.01 * RATE))]
    fade = min(len(out), int(0.015 * RATE))
    for i in range(fade): out[len(out) - 1 - i] *= i / fade
    return [int(round(v / peak * 0.89 * 32767)) for v in out]

CUSTOM = os.path.join(SRC, "custom")
KIT_DIRS = ["default", "heavy_metal", "magical", "retro", "mouth"]

def decode_file(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
                         capture_output=True, check=True).stdout
    x = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)]
    peak = max(1e-6, max(abs(v) for v in x))
    on = next(i for i, v in enumerate(x) if abs(v) > peak * 0.03)
    return x[max(0, on - int(0.002 * RATE)):]

# the longest each sound may be (the length guide in assets/sfx/custom/README.md): a custom clip is cut there, with
# a short fade, so a long take (several footsteps, a long tail) plays as one short sound
MAXLEN = {"HIT": 0.2, "KILL": 0.6, "HURT": 0.3, "STEP": 0.15, "JUMP": 0.2, "LAND": 0.3, "RIFLE": 0.3, "ROCKET": 0.7,
          "KNIFE": 0.35, "SNIPER": 1.0, "LASER": 0.1, "SHOTGUN": 0.6, "STICKY": 0.4, "CROSSBOW": 0.5, "BUBBLE": 0.3,
          "SYRINGE": 0.4, "EXPLOSION": 1.0, "STICKY_ATTACH": 0.2, "BOLT_HIT": 0.3, "DASH": 0.4, "GRAPPLE_FIRE": 0.3,
          "GRAPPLE_HIT": 0.3, "DEATH": 0.7, "STREAK": 0.6, "TYPING": 0.06, "CHECKPOINT": 0.35, "FINISH": 0.8,
          "HIT_MED": 0.25, "HIT_HEAVY": 0.35, "HIT_BRUTAL": 0.5}

def custom_sounds(ids):
    """{kit: {SOUND: path}} from assets/sfx/custom/<kit>/<SOUND>.<ext> (the owner's own recordings win)"""
    out = {}
    for k, d in enumerate(KIT_DIRS):
        folder = os.path.join(CUSTOM, d)
        if not os.path.isdir(folder): continue
        for f in sorted(os.listdir(folder)):
            name, ext = os.path.splitext(f)
            if ext.lower() not in (".wav", ".ogg", ".mp3", ".flac"): continue
            if name.upper() not in ids: print("  ignoring", os.path.join(d, f), "(not a sound name: see assets/sfx/custom/README.md)"); continue
            out.setdefault(k, {})[name.upper()] = os.path.join(folder, f)
    return out

def main():
    ids = sound_ids()
    ids_all = ids
    ids = [i for i in ids if not i.startswith("VOICE_")]
    custom = custom_sounds(ids)
    parts = ["// generated by tools/import_sfx.py from assets/sfx/ (CC0 clips, see assets/sfx/CREDITS.md): the recorded",
             "// sound kits, mono 16-bit at %d Hz; audio.cpp matches each one's loudness to the DEFAULT kit's sound" % RATE,
             "#pragma once", "#include <cstdint>", "#include <cstddef>", "constexpr int kSfxRate = %d;" % RATE,
             "struct SfxClip { int kit, id; const int16_t* data; size_t size; };   // kit: sound pack index; id: SND_*"]
    entries, total = [], 0
    jobs = []
    for kit, sounds in KITS.items():
        for name, spec in sounds.items():
            if name not in custom.get(kit, {}): jobs.append((kit, name, lambda spec=spec: build(*spec)))
    for kit, sounds in custom.items():
        for name, path in sounds.items():
            def one(path=path, name=name):
                x = decode_file(path)[:int(MAXLEN.get(name, 3.0) * RATE)]
                fade = min(len(x), int(min(0.04, MAXLEN.get(name, 3.0) * 0.25) * RATE))
                for i in range(fade): x[len(x) - 1 - i] *= i / fade
                peak = max(1e-6, max(abs(v) for v in x))
                return [int(round(v / peak * 0.89 * 32767)) for v in x]
            jobs.append((kit, name, one))
            print("  custom:", KIT_DIRS[kit], name, "<-", os.path.relpath(path, ROOT))
    for kit, name, make in jobs:
        if True:
            s = make()
            total += len(s) * 2
            var = "kSfx%d_%s" % (kit, name)
            parts.append("static const int16_t %s[%d] = {%s};" % (var, len(s), ",".join(map(str, s))))
            entries.append("{%d, %d, %s, sizeof %s / 2}" % (kit, ids_all.index(name), var, var))
            print("  kit %d %-14s %.2f s" % (kit, name, len(s) / RATE))
    parts.append("static const SfxClip kSfxClips[] = {%s};" % ", ".join(entries))
    open(OUT, "w").write("\n".join(parts) + "\n")
    print("wrote", OUT, "-", len(entries), "clips,", total // 1024, "KB of samples")

main()
