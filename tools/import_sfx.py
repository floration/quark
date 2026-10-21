#!/usr/bin/env python3
"""Generates src/client/sfx_recorded.h: the recorded sound effects of the HEAVY METAL, MAGICAL and WOODEN sound kits,
from the CC0 clips in assets/sfx/ (sources and licenses: assets/sfx/CREDITS.md). Each sound is one or more layers
(clip, gain, pitch, delay) mixed together, its leading silence trimmed, cut to a length with a fade-out, mono 16-bit at
22.05 kHz. The game matches each one's loudness to the DEFAULT kit's sound at startup (audio.cpp), so levels stay even.
    python3 tools/import_sfx.py          (needs ffmpeg on PATH to decode the .ogg clips; the game doesn't)
DEFAULT and RETRO are synthesized (audio.cpp); a sound missing from a kit here falls back to the DEFAULT one."""
import os, re, struct, subprocess

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "assets", "sfx")
OUT = os.path.join(ROOT, "src", "client", "sfx_recorded.h")
RATE = 22050

def L(f, gain=1.0, pitch=1.0, delay=0.0): return (f, gain, pitch, delay)

# kit index (the setting's order: 0 DEFAULT, 1 HEAVY METAL, 2 MAGICAL, 3 RETRO, 4 WOODEN) -> sound -> (max seconds, layers)
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
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", os.path.join(SRC, name + ".ogg"), "-ac", "1",
                              "-ar", str(int(round(RATE / pitch))), "-f", "s16le", "-"], capture_output=True, check=True).stdout
        x = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)]
        peak = max(1e-6, max(abs(v) for v in x))
        on = next(i for i, v in enumerate(x) if abs(v) > peak * 0.03)
        _cache[key] = x[max(0, on - int(0.002 * RATE)):]   # leading silence off: the sound starts the instant it plays
    return _cache[key]

def build(maxlen, layers):
    n = int(maxlen * RATE)
    out = [0.0] * n
    for f, gain, pitch, delay in layers:
        x = decode(f, pitch)
        d = int(delay * RATE)
        for i in range(min(len(x), n - d)): out[d + i] += x[i] * gain
    # drop the silent tail, then a 15 ms fade so it never clicks
    peak = max(1e-6, max(abs(v) for v in out))
    last = max(i for i, v in enumerate(out) if abs(v) > peak * 0.01)
    out = out[:min(n, last + int(0.01 * RATE))]
    fade = min(len(out), int(0.015 * RATE))
    for i in range(fade): out[len(out) - 1 - i] *= i / fade
    return [int(round(v / peak * 0.89 * 32767)) for v in out]

def main():
    ids = sound_ids()
    parts = ["// generated by tools/import_sfx.py from assets/sfx/ (CC0 clips, see assets/sfx/CREDITS.md): the recorded",
             "// sound kits, mono 16-bit at %d Hz; audio.cpp matches each one's loudness to the DEFAULT kit's sound" % RATE,
             "#pragma once", "#include <cstdint>", "#include <cstddef>", "constexpr int kSfxRate = %d;" % RATE,
             "struct SfxClip { int kit, id; const int16_t* data; size_t size; };   // kit: sound pack index; id: SND_*"]
    entries, total = [], 0
    for kit, sounds in KITS.items():
        for name, (maxlen, layers) in sounds.items():
            s = build(maxlen, layers)
            total += len(s) * 2
            var = "kSfx%d_%s" % (kit, name)
            parts.append("static const int16_t %s[%d] = {%s};" % (var, len(s), ",".join(map(str, s))))
            entries.append("{%d, %d, %s, sizeof %s / 2}" % (kit, ids.index(name), var, var))
            print("  kit %d %-14s %.2f s" % (kit, name, len(s) / RATE))
    parts.append("static const SfxClip kSfxClips[] = {%s};" % ", ".join(entries))
    open(OUT, "w").write("\n".join(parts) + "\n")
    print("wrote", OUT, "-", len(entries), "clips,", total // 1024, "KB of samples")

main()
