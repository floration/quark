#include "audio.h"
#include "../common/u8file.h"
#include "voice_data.h"
#include "voice_recorded.h"
#include "sfx_recorded.h"
#include "../common/profile.h"
#include "../game/map.h"   // (findDataPath: the game folder, for mods/)
#include "overrides.h"     // the owner's sounds (docs/ASSETS.md) and the sound file decoders
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstring>
#include <set>
#include <string>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <mmsystem.h>
  #include <objbase.h>
  #include <mmdeviceapi.h>
  #include <audioclient.h>
  #include <propidl.h>
#elif defined(__APPLE__)
  #include <AudioToolbox/AudioToolbox.h>
  #include <CoreAudio/CoreAudio.h>
#else
  #include <dlfcn.h>
#endif

namespace {
constexpr int R = Audio::kRate;
constexpr int kPeriod = 240;        // frames per buffer: 5 ms at 48 kHz
constexpr int kMaxVoices = 64;
constexpr int kMaxPending = 4 * kMaxVoices;   // sounds started between two mixes (more only when the mixer stalls)
constexpr int kMixFrames = kPeriod * 4;         // the most one mix() makes (the backends ask in pieces up to this)

// --- synthesis helpers -------------------------------------------------------
// Sounds are built in layers: a pitch-dropping "body" (weight), band-limited noise (texture),
// soft attacks/fades (no clicks), then a short room reverb so nothing sounds dry or synthetic.
struct Noise { uint32_t s = 0x12345678; float operator()() { s = s * 1664525u + 1013904223u; return ((s >> 9) & 0xFFFF) / 32768.0f - 1.0f; } };

std::vector<float> make(float seconds) { return std::vector<float>((size_t)(seconds * R), 0.0f); }
float env(float t, float attack, float decay) { return (t < attack ? std::sin(0.5f * PI * t / attack) : 1.0f) * std::exp(-t / decay); }
float onePole(float cutoffHz) { return 1.0f - std::exp(-2.0f * PI * cutoffHz / R); }
// where a layer has died away (its envelope under e^-18, below 16-bit silence): the synthesis stops there (startup time)
size_t layerEnd(const std::vector<float>& b, float start, float attack, float decay) {
    return std::min(b.size(), (size_t)((start + attack + 18.0f * decay) * R) + 1);
}

// sine whose pitch falls exponentially from f0 to f1 (kicks, thumps, booms)
void addThump(std::vector<float>& b, float f0, float f1, float pitchTime, float amp, float attack, float decay, float start = 0) {
    float ph = 0;
    for (size_t i = (size_t)(start * R), e = layerEnd(b, start, attack, decay); i < e; i++) {
        float t = i / (float)R - start;
        float f = f1 + (f0 - f1) * std::exp(-t / pitchTime);
        ph += 2 * PI * f / R;
        b[i] += std::sin(ph) * amp * env(t, attack, decay);
    }
}
// noise band-limited between lo..hi Hz (difference of two low-passes), with a cutoff sweep
void addBand(std::vector<float>& b, float lo, float hi, float amp, float attack, float decay, uint32_t seed, float hiEnd = -1, float start = 0) {
    Noise n; n.s ^= seed * 2654435761u;
    float a = 0, c = 0;
    const float kLo = onePole(lo), kHi = onePole(hi);   // (the same every sample: worked out once)
    for (size_t i = (size_t)(start * R), e = layerEnd(b, start, attack, decay); i < e; i++) {
        float t = i / (float)R - start;
        float x = n();
        a += (x - a) * (hiEnd > 0 ? onePole(hi + (hiEnd - hi) * std::min(1.0f, t / (b.size() / (float)R))) : kHi);
        c += (x - c) * kLo;
        b[i] += (a - c) * amp * env(t, attack, decay);
    }
}
void addTone(std::vector<float>& b, float f, float amp, float attack, float decay, float start = 0) {
    for (size_t i = (size_t)(start * R), e = layerEnd(b, start, attack, decay); i < e; i++) {
        float t = i / (float)R - start;
        b[i] += std::sin(2 * PI * f * t) * amp * env(t, attack, decay);
    }
}
// small room: a few feedback combs + a low-pass, mixed in quietly
void addRoom(std::vector<float>& b, float mix, float size = 1.0f) {
    const int delays[4] = {1116, 1188, 1277, 1356};
    std::vector<float> wet(b.size(), 0.0f);
    for (int d : delays) {
        int D = (int)(d * size);
        std::vector<float> line(D, 0.0f);
        float lp = 0;
        for (size_t i = 0, k = 0; i < b.size(); i++, k = k + 1 == (size_t)D ? 0 : k + 1) {   // (k = i % D, without a divide)
            float y = line[k];
            lp += (y - lp) * 0.35f;
            line[k] = b[i] + lp * 0.72f;
            wet[i] += y * 0.25f;
        }
    }
    for (size_t i = 0; i < b.size(); i++) b[i] += wet[i] * mix;
}
void finish(std::vector<float>& b, float peak) {
    // fade the last 15 ms so tails never click, then set the level
    size_t f = std::min(b.size(), (size_t)(0.015f * R));
    for (size_t i = 0; i < f; i++) b[b.size() - 1 - i] *= i / (float)f;
    float m = 1e-6f;
    for (float v : b) m = std::max(m, std::fabs(v));
    for (float& v : b) v *= peak / m;
}
} // namespace

static void voice(std::vector<float>& b, int pack);
static float loudness(const std::vector<float>& v);

// soft saturation (crunch): the clip at peak 1, through tanh with this drive, back to peak 1
static void drive(std::vector<float>& b, float d) {
    const float k = 1.0f / std::tanh(d);
    for (float& v : b) v = std::tanh(v * d) * k;
}
// the hitsound's four tiers (hitSoundFor: < 10, < 25, <= 50, > 50 damage), each heavier and louder: light is the old
// single tick as it sounded on small hits; then a deeper knock with a low thump under it, a heavy crunch, and a
// brutal double crack with a sub drop and a room tail. Levelled by heard loudness to kHitTierGain x the old tick's.
static void buildHitTiers(std::vector<float>* s) {
    std::vector<float> old = make(0.09f);   // (the single hitsound before the tiers: the level they're set against)
    addThump(old, 1500, 1100, 0.01f, 1, 0.0015f, 0.022f); addTone(old, 2600, 0.15f, 0.001f, 0.01f); finish(old, 0.32f);
    const float ref = Audio::heardLoudness(old);
    auto& l = s[SND_HIT];
    l = make(0.08f); addThump(l, 1800, 1320, 0.0083f, 1, 0.0013f, 0.018f); addTone(l, 3120, 0.15f, 0.001f, 0.0083f); finish(l, 1);
    auto& m = s[SND_HIT_MED];
    m = make(0.13f); addThump(m, 1250, 820, 0.012f, 1, 0.001f, 0.028f); addThump(m, 260, 140, 0.02f, 0.55f, 0.002f, 0.035f);
    addBand(m, 1800, 6000, 0.35f, 0.0005f, 0.008f, 31); finish(m, 1); drive(m, 1.6f); finish(m, 1);
    auto& h = s[SND_HIT_HEAVY];
    h = make(0.22f); addThump(h, 900, 480, 0.015f, 1, 0.001f, 0.04f); addThump(h, 170, 65, 0.03f, 0.9f, 0.002f, 0.07f);
    addBand(h, 700, 5000, 0.7f, 0.0005f, 0.02f, 32, 2000); finish(h, 1); drive(h, 2.6f); addRoom(h, 0.15f, 0.6f); finish(h, 1);
    auto& b = s[SND_HIT_BRUTAL];
    b = make(0.4f); addThump(b, 650, 300, 0.02f, 1, 0.001f, 0.055f); addThump(b, 120, 42, 0.05f, 1.2f, 0.002f, 0.12f);
    addBand(b, 400, 7000, 0.9f, 0.0005f, 0.03f, 33, 1500); addBand(b, 800, 5000, 0.6f, 0.0005f, 0.02f, 34, -1, 0.025f);
    finish(b, 1); drive(b, 4.0f); addRoom(b, 0.25f, 0.9f); finish(b, 1);
    for (int id : {(int)SND_HIT, (int)SND_HIT_MED, (int)SND_HIT_HEAVY, (int)SND_HIT_BRUTAL}) {
        float g = kHitTierGain[hitTier(id)] * ref / std::max(1e-6f, Audio::heardLoudness(s[id]));
        for (float& v : s[id]) v *= g;
    }
}

void Audio::synthesize() {
    voices_.reserve(kMaxVoices);   // (never grows under the mixing lock)
    auto& s = sounds_[0];
    // feedback: soft rounded ticks
    buildHitTiers(s);
    s[SND_KILL] = make(0.3f);  addThump(s[SND_KILL], 1300, 1000, 0.01f, 1, 0.0015f, 0.03f); addThump(s[SND_KILL], 1900, 1500, 0.01f, 0.9f, 0.0015f, 0.05f, 0.075f);
    addRoom(s[SND_KILL], 0.25f, 0.6f); finish(s[SND_KILL], 0.36f);
    s[SND_HURT] = make(0.22f); addThump(s[SND_HURT], 140, 60, 0.04f, 1, 0.003f, 0.06f); addBand(s[SND_HURT], 80, 900, 0.5f, 0.002f, 0.04f, 1); finish(s[SND_HURT], 0.35f);
    // movement: muffled, quiet
    s[SND_STEP] = make(0.09f); addBand(s[SND_STEP], 60, 700, 1, 0.003f, 0.018f, 3); addThump(s[SND_STEP], 120, 70, 0.02f, 0.5f, 0.002f, 0.02f); finish(s[SND_STEP], 0.16f);
    s[SND_JUMP] = make(0.12f); addBand(s[SND_JUMP], 200, 1500, 1, 0.01f, 0.03f, 4); finish(s[SND_JUMP], 0.1f);
    s[SND_LAND] = make(0.16f); addThump(s[SND_LAND], 110, 50, 0.03f, 1, 0.002f, 0.05f); addBand(s[SND_LAND], 60, 600, 0.6f, 0.002f, 0.03f, 5); finish(s[SND_LAND], 0.25f);
    // weapons: body + crack + room
    s[SND_RIFLE] = make(0.3f); addThump(s[SND_RIFLE], 220, 80, 0.015f, 1, 0.001f, 0.035f); addBand(s[SND_RIFLE], 400, 4500, 0.9f, 0.001f, 0.025f, 6, 1200);
    addRoom(s[SND_RIFLE], 0.35f, 0.8f); finish(s[SND_RIFLE], 0.28f);
    s[SND_ROCKET] = make(0.7f); addThump(s[SND_ROCKET], 160, 45, 0.05f, 1, 0.004f, 0.12f); addBand(s[SND_ROCKET], 80, 1800, 0.9f, 0.03f, 0.22f, 7, 500);
    addRoom(s[SND_ROCKET], 0.3f); finish(s[SND_ROCKET], 0.32f);
    s[SND_KNIFE] = make(0.18f); addBand(s[SND_KNIFE], 1500, 6000, 1, 0.04f, 0.035f, 8, 2500); finish(s[SND_KNIFE], 0.16f);
    s[SND_SNIPER] = make(1.0f); addThump(s[SND_SNIPER], 260, 45, 0.03f, 1, 0.001f, 0.12f); addBand(s[SND_SNIPER], 500, 6000, 0.8f, 0.0008f, 0.03f, 9, 1500);
    addBand(s[SND_SNIPER], 60, 500, 0.5f, 0.01f, 0.3f, 10); addRoom(s[SND_SNIPER], 0.45f, 1.3f); finish(s[SND_SNIPER], 0.4f);
    s[SND_LASER] = make(0.07f); addTone(s[SND_LASER], 330, 0.6f, 0.006f, 0.05f); addTone(s[SND_LASER], 495, 0.3f, 0.006f, 0.04f); addBand(s[SND_LASER], 1000, 3000, 0.15f, 0.005f, 0.04f, 19); finish(s[SND_LASER], 0.1f);
    s[SND_SHOTGUN] = make(0.55f); addThump(s[SND_SHOTGUN], 180, 50, 0.025f, 1, 0.001f, 0.08f); addBand(s[SND_SHOTGUN], 150, 3500, 1, 0.001f, 0.06f, 11, 700);
    addRoom(s[SND_SHOTGUN], 0.4f, 1.1f); finish(s[SND_SHOTGUN], 0.38f);
    s[SND_STICKY] = make(0.25f); addThump(s[SND_STICKY], 300, 110, 0.03f, 1, 0.002f, 0.05f); addBand(s[SND_STICKY], 200, 1500, 0.3f, 0.002f, 0.03f, 12); addRoom(s[SND_STICKY], 0.2f, 0.7f); finish(s[SND_STICKY], 0.26f);
    s[SND_BUBBLE] = make(0.22f); addThump(s[SND_BUBBLE], 700, 260, 0.06f, 1, 0.002f, 0.07f); addThump(s[SND_BUBBLE], 1100, 500, 0.04f, 0.5f, 0.01f, 0.05f); addRoom(s[SND_BUBBLE], 0.15f, 0.6f); finish(s[SND_BUBBLE], 0.22f);   // a wet bloop
    s[SND_SYRINGE] = make(0.16f); addBand(s[SND_SYRINGE], 3000, 9000, 0.35f, 0.01f, 0.08f, 15, 4000); addTone(s[SND_SYRINGE], 1400, 0.15f, 0.002f, 0.03f); finish(s[SND_SYRINGE], 0.12f);   // a hiss and a click
    s[SND_CROSSBOW] = make(0.35f); addThump(s[SND_CROSSBOW], 420, 300, 0.05f, 1, 0.001f, 0.09f); addBand(s[SND_CROSSBOW], 1500, 5000, 0.4f, 0.001f, 0.012f, 13); addRoom(s[SND_CROSSBOW], 0.2f, 0.7f); finish(s[SND_CROSSBOW], 0.24f);
    s[SND_EXPLOSION] = make(1.2f); addThump(s[SND_EXPLOSION], 120, 32, 0.08f, 1, 0.002f, 0.3f); addBand(s[SND_EXPLOSION], 40, 2500, 1, 0.003f, 0.25f, 14, 300);
    addRoom(s[SND_EXPLOSION], 0.4f, 1.4f); finish(s[SND_EXPLOSION], 0.42f);
    s[SND_STICKY_ATTACH] = make(0.06f); addThump(s[SND_STICKY_ATTACH], 900, 500, 0.008f, 1, 0.001f, 0.01f); finish(s[SND_STICKY_ATTACH], 0.14f);
    s[SND_BOLT_HIT] = make(0.12f); addThump(s[SND_BOLT_HIT], 320, 140, 0.015f, 1, 0.001f, 0.03f); addBand(s[SND_BOLT_HIT], 300, 2500, 0.4f, 0.001f, 0.015f, 16); finish(s[SND_BOLT_HIT], 0.2f);
    // abilities
    s[SND_DASH] = make(0.35f); addBand(s[SND_DASH], 300, 900, 1, 0.05f, 0.09f, 17, 4000); finish(s[SND_DASH], 0.22f);
    s[SND_GRAPPLE_FIRE] = make(0.2f); addBand(s[SND_GRAPPLE_FIRE], 800, 5000, 1, 0.005f, 0.06f, 18, 1500); addThump(s[SND_GRAPPLE_FIRE], 700, 300, 0.04f, 0.4f, 0.002f, 0.05f); finish(s[SND_GRAPPLE_FIRE], 0.18f);
    s[SND_GRAPPLE_HIT] = make(0.35f);
    for (float f : {1180.0f, 1630.0f, 2360.0f}) addTone(s[SND_GRAPPLE_HIT], f, 0.35f, 0.001f, 0.07f);
    addThump(s[SND_GRAPPLE_HIT], 400, 200, 0.01f, 0.5f, 0.001f, 0.02f); addRoom(s[SND_GRAPPLE_HIT], 0.25f, 0.6f); finish(s[SND_GRAPPLE_HIT], 0.2f);
    // feedback: your death (a low falling thud), someone else's killstreak (a rising two-note sting), typing (a soft click)
    s[SND_DEATH] = make(0.7f); addThump(s[SND_DEATH], 180, 40, 0.15f, 1, 0.004f, 0.25f); addTone(s[SND_DEATH], 220, 0.25f, 0.01f, 0.2f); addTone(s[SND_DEATH], 165, 0.25f, 0.01f, 0.3f, 0.12f);
    addRoom(s[SND_DEATH], 0.35f, 1.2f); finish(s[SND_DEATH], 0.34f);
    s[SND_STREAK] = make(0.5f); addTone(s[SND_STREAK], 660, 0.5f, 0.003f, 0.09f); addTone(s[SND_STREAK], 990, 0.5f, 0.003f, 0.16f, 0.1f);
    addRoom(s[SND_STREAK], 0.3f, 0.8f); finish(s[SND_STREAK], 0.22f);
    s[SND_TYPING] = make(0.05f); addThump(s[SND_TYPING], 2400, 1600, 0.004f, 1, 0.0005f, 0.006f); addBand(s[SND_TYPING], 2000, 7000, 0.3f, 0.0005f, 0.004f, 21); finish(s[SND_TYPING], 0.12f);
    // race: a bright two-tone ding for a checkpoint, a rising three-note chime for the finish
    s[SND_CHECKPOINT] = make(0.35f); addTone(s[SND_CHECKPOINT], 1320, 0.5f, 0.002f, 0.08f); addTone(s[SND_CHECKPOINT], 1760, 0.5f, 0.002f, 0.14f, 0.06f);
    addRoom(s[SND_CHECKPOINT], 0.25f, 0.7f); finish(s[SND_CHECKPOINT], 0.26f);
    s[SND_FINISH] = make(0.8f);
    for (int k = 0; k < 3; k++) { float f = 1047.0f * std::pow(1.26f, (float)k); addTone(s[SND_FINISH], f, 0.45f, 0.002f, k == 2 ? 0.3f : 0.1f, k * 0.09f); addTone(s[SND_FINISH], 2 * f, 0.12f, 0.002f, 0.08f, k * 0.09f); }
    addRoom(s[SND_FINISH], 0.3f, 0.9f); finish(s[SND_FINISH], 0.3f);
    // movement tech: a super (dash jump) is a bright rising whoosh with a rising tone; a hyper (super dash) the same
    // lower and heavier, with an impact under it; the locator a sonar ping with its echo
    s[SND_SUPER] = make(0.4f); addBand(s[SND_SUPER], 500, 1400, 1, 0.01f, 0.16f, 51, 5200); addThump(s[SND_SUPER], 560, 1120, 0.12f, 0.55f, 0.002f, 0.13f);
    addTone(s[SND_SUPER], 1680, 0.18f, 0.002f, 0.08f, 0.07f); addRoom(s[SND_SUPER], 0.18f, 0.6f); finish(s[SND_SUPER], 0.3f);
    s[SND_HYPER] = make(0.5f); addThump(s[SND_HYPER], 150, 55, 0.08f, 1, 0.002f, 0.12f); addBand(s[SND_HYPER], 300, 900, 1, 0.01f, 0.22f, 52, 6000);
    addThump(s[SND_HYPER], 380, 1150, 0.18f, 0.6f, 0.003f, 0.2f); addRoom(s[SND_HYPER], 0.22f, 0.8f); finish(s[SND_HYPER], 0.36f);
    s[SND_LOCATOR] = make(0.9f); addTone(s[SND_LOCATOR], 1480, 0.8f, 0.002f, 0.25f); addTone(s[SND_LOCATOR], 2960, 0.12f, 0.002f, 0.1f);
    addTone(s[SND_LOCATOR], 1480, 0.35f, 0.002f, 0.25f, 0.24f); addRoom(s[SND_LOCATOR], 0.35f, 1.0f); finish(s[SND_LOCATOR], 0.24f);
    // the kits: RETRO re-voices these (chiptune), HEAVY METAL / MAGICAL / WOODEN are recorded (sfx_recorded.h);
    // anything a kit lacks is the DEFAULT sound
    for (int p = 1; p < kPacks; p++)
        for (int i = 0; i < SND_VOICE_THREE; i++) { sounds_[p][i] = s[i]; if (p == kPackRetro) voice(sounds_[p][i], p); }
    loadRecordedKits();
    loadOverrideSounds();
    loadModSounds();
    loadAnnouncer(announcer_);
}

// The announcer's lines (8-bit 16 kHz, voice_data.h) up to the mix rate, the same in every sound pack.
// Each voice: its recorded lines (voice_recorded.h), the generated one (voice_data.h) for any line not recorded.
void Audio::loadAnnouncer(int set) {
    set = set < 0 || set >= kAnnouncerVoices ? 0 : set;
    static const int fallback[kAnnouncerVoices] = {0, 1, 0, 0, 1, 1, 0};   // the generated voice for lines not recorded
    const int tts = std::min(fallback[set], kVoiceSets - 1);
    constexpr int kLines = SND_COUNT - SND_VOICE_THREE;
    auto built = std::make_shared<AnnouncerLines>();
    std::vector<float>* lines = built->line;
    for (int k = 0; k < kVoiceLines && k < kLines; k++) {
        const VoiceClip& c = kVoiceClips[tts][k];
        auto& v = lines[k];
        size_t n = c.size * R / kVoiceRate;
        v.resize(n);
        for (size_t i = 0; i < n; i++) {
            float t = (float)i * kVoiceRate / R; size_t a = (size_t)t; float f = t - a;
            float x0 = (c.data[std::min(a, c.size - 1)] - 128) / 128.0f, x1 = (c.data[std::min(a + 1, c.size - 1)] - 128) / 128.0f;
            v[i] = (x0 + (x1 - x0) * f) * 0.9f;
        }
    }
    for (const RecordedClip& c : kRecordedVoice) {
            if (c.set != set || c.line < 0 || c.line >= kLines) continue;
            auto& v = lines[c.line];
            size_t n = c.size * R / kRecordedRate;
            v.assign(n, 0.0f);
            for (size_t i = 0; i < n; i++) {
                float t = (float)i * kRecordedRate / R; size_t a = (size_t)t; float f = t - a;
                float x0 = c.data[std::min(a, c.size - 1)] / 32768.0f, x1 = c.data[std::min(a + 1, c.size - 1)] / 32768.0f;
                v[i] = (x0 + (x1 - x0) * f) * 0.9f;
            }
        }
    loadOverrideVoice(set, lines, kLines);   // the owner's lines (docs/ASSETS.md); nothing without overrides
    std::shared_ptr<const AnnouncerLines> ready = std::move(built), unused, retired;   // (freed here, after the lock)
    {
        std::lock_guard<std::mutex> l(mu_);   // just the handover
        // (lines asked for before the change don't play in the new voice: the mixer cuts the ones playing when it takes it)
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](const Voice& v) { return v.id >= SND_VOICE_THREE; }), pending_.end());
        unused = std::move(nextLines_);       // (a voice the mixer never took)
        retired = std::move(retiredLines_);   // (one it let go of: the slot is empty again before it can take these)
        nextLines_ = ready;
        linesNew_ = true;
    }
    announcerLines_ = std::move(ready);
    announcer_ = set;
}

// short-term loudness: the loudest 20 ms (RMS), so a clip's level doesn't depend on how long its tail rings
static float loudness(const std::vector<float>& v) {
    const size_t w = R / 50;
    double best = 0, e = 0;
    for (size_t i = 0; i < v.size(); i++) {
        e += (double)v[i] * v[i];
        if (i >= w) e -= (double)v[i - w] * v[i - w];
        best = std::max(best, e);
    }
    return (float)std::sqrt(best / w);
}

// loudness as heard: two high-pass poles at 250 Hz first (the ear hears a low thump far quieter than a tick of the
// same energy), so a heavy hit with a big low end isn't measured louder than it sounds
float Audio::heardLoudness(const std::vector<float>& v) {   // (one pass, nothing allocated: loudness() of the filtered clip)
    constexpr size_t w = R / 50;
    float sq[w] = {};
    const float a = onePole(250.0f);
    float l1 = 0, l2 = 0;
    double best = 0, e = 0;
    for (size_t i = 0, k = 0; i < v.size(); i++, k = k + 1 == w ? 0 : k + 1) {
        l1 += (v[i] - l1) * a; float x = v[i] - l1; l2 += (x - l2) * a; x -= l2;
        float x2 = x * x;
        e += x2 - sq[k]; sq[k] = x2;
        best = std::max(best, e);
    }
    return (float)std::sqrt(best / w);
}

// a recording's rumble below hearing (a mic bump, a DC offset) cut: it adds no loudness you hear but eats headroom,
// and levelling such a clip by heard loudness would push it into clipping (the hitsound's tiers, recorded or replaced)
static void cutRumble(std::vector<float>& v) {
    const float a = onePole(25.0f);
    float l1 = 0, l2 = 0;
    for (float& s : v) { l1 += (s - l1) * a; float x = s - l1; l2 += (x - l2) * a; s = x - l2; }
}
// a recorded or replaced hitsound tier as loud as `match` (DEFAULT's, by heard loudness: every kit keeps the ladder);
// a clip whose low end would then clip is soft-limited instead (a little more crunch, never a hard clip)
static void levelHit(std::vector<float>& v, const std::vector<float>& match) {
    cutRumble(v);
    const float want = Audio::heardLoudness(match);
    for (int pass = 0; pass < 4; pass++) {
        float have = Audio::heardLoudness(v), peak = 1e-6f;
        if (want < 1e-6f || have < 1e-6f) return;
        for (float& s : v) { s *= want / have; peak = std::max(peak, std::fabs(s)); }
        if (peak <= 0.95f) return;
        if (pass == 3) { for (float& s : v) s *= 0.95f / peak; return; }   // (as close as it gets)
        for (float& s : v) s = 0.95f * std::tanh(s / 0.95f);
    }
}

// RETRO: the DEFAULT sounds a little higher, crushed to 4-bit levels held for 4 samples (~12 kHz): chiptune.
static void voice(std::vector<float>& b, int pack) {
    if (pack != Audio::kPackRetro || b.empty()) return;
    float before = loudness(b);
    const float pitch = 1.1f;
    std::vector<float> o((size_t)(b.size() / pitch) + 1);
    for (size_t i = 0; i < o.size(); i++) {
        float x = i * pitch;
        size_t k = (size_t)x;
        float f = x - k;
        o[i] = k + 1 < b.size() ? b[k] + (b[k + 1] - b[k]) * f : (k < b.size() ? b[k] : 0.0f);
    }
    float held = 0;
    for (size_t i = 0; i < o.size(); i++) { if (i % 4 == 0) held = std::round(o[i] * 8.0f) / 8.0f; o[i] = held; }
    // fairness: as loud as DEFAULT, so no kit hears or hides footsteps better
    float after = loudness(o);
    if (after > 1e-6f) { float g = before / after; for (auto& v : o) v = std::clamp(v * g, -1.0f, 1.0f); }
    b.swap(o);
}

// The recorded kits (sfx_recorded.h, 22.05 kHz) up to the mix rate, each matched to the DEFAULT sound's loudness.
// All done once at startup: the mixer only ever reads ready float buffers.
void Audio::loadRecordedKits() {
    for (int pass = 0; pass < 2; pass++)   // (DEFAULT's own recordings first: the other kits match their loudness)
    for (const SfxClip& c : kSfxClips) {
        if ((c.kit == 0) != (pass == 0) || c.kit < 0 || c.kit >= kPacks || c.id < 0 || c.id >= SND_VOICE_THREE || c.size < 2) continue;
        std::vector<float> v((size_t)((double)c.size * R / kSfxRate));
        for (size_t i = 0; i < v.size(); i++) {
            float t = (float)((double)i * kSfxRate / R); size_t a = (size_t)t; float f = t - a;
            float x0 = c.data[std::min(a, c.size - 1)] / 32768.0f, x1 = c.data[std::min(a + 1, c.size - 1)] / 32768.0f;
            v[i] = x0 + (x1 - x0) * f;
        }
        if (c.kit == kPackMagical) { v.resize(v.size() + R / 10, 0.0f); addRoom(v, 0.3f, 1.3f); finish(v, 1.0f); }  // a shimmering hall
        if (c.kit != 0 && hitTier(c.id) >= 0) levelHit(v, sounds_[0][c.id]);   // (the hitsound's tiers: heard loudness)
        else {
            float want = c.kit == 0 ? 0.0f : loudness(sounds_[0][c.id]), have = loudness(v);
            if (want > 1e-6f && have > 1e-6f) { float g = want / have; for (auto& x : v) x = std::clamp(x * g, -1.0f, 1.0f); }
        }
        if (c.kit == 0) for (int p = 1; p < kPacks; p++) if (sounds_[p][c.id] == sounds_[0][c.id]) sounds_[p][c.id] = v;   // (kits without their own take the new DEFAULT)
        sounds_[c.kit][c.id].swap(v);
    }
}

Audio::Audio() {
    // (room for a full mixer and a burst of new sounds, made once: nothing is allocated while playing or mixing)
    voices_.reserve(kMaxVoices);
    pending_.reserve(kMaxPending);
    incoming_.reserve(kMaxPending);
}

// the listener's right (yaw 0 looks down -Z, right = +X)
static Vec3 rightOf(float yaw) { return {std::cos(yaw), 0, -std::sin(yaw)}; }

void Audio::voiceGains(Voice& v, const Vec3& listener, const Vec3& right) {
    if (!v.spatial) { v.gainL = v.gainR = v.volume; return; }
    Vec3 d = v.at - listener;
    float dist = length(d);
    float g = v.volume / (1.0f + dist / 10.0f);
    if (dist > 90.0f) g = 0;
    // pan: how far to the listener's right the sound is
    float pan = dist > 0.01f ? clampf(dot(d / dist, right), -1, 1) : 0;
    v.gainL = g * std::sqrt(0.5f * (1 - pan * 0.8f));
    v.gainR = g * std::sqrt(0.5f * (1 + pan * 0.8f));
}

void Audio::setListener(const Vec3& pos, float yaw) {
    listener_ = pos; listenerYaw_ = yaw;
    std::shared_ptr<const AnnouncerLines> retired;   // (a voice change's old lines, once the mixer let go: freed here)
    {
        std::lock_guard<std::mutex> l(mu_);
        listenerNext_ = pos; listenerYawNext_ = yaw;
        retired = std::move(retiredLines_);
    }
}

// (the gains are worked out before taking the lock: the game thread holds it only for the push, and an inaudible sound
// never takes it; the mixer holds it only to swap the list out, never for a mix pass)
void Audio::play(int id, float volume, float pitch, int pack) {
    if (!running_ || id < 0 || id >= SND_COUNT) return;
    Voice v{id, (pack < 0 || pack >= kPacks) ? pack_.load() : pack, 0, pitch, volume, volume, false, {}, volume};
    std::lock_guard<std::mutex> l(mu_);
    v.seq = ++voiceSeq_;
    addVoice(pending_, v, kMaxPending);
}

void Audio::play3D(int id, const Vec3& pos, float volume, float pitch, int pack) {
    if (!running_ || id < 0 || id >= SND_COUNT) return;
    Voice v{id, (pack < 0 || pack >= kPacks) ? pack_.load() : pack, 0, pitch, 0, 0, true, pos, volume};
    voiceGains(v, listener_, rightOf(listenerYaw_));
    if (v.gainL + v.gainR < 0.002f) return; // inaudible
    std::lock_guard<std::mutex> l(mu_);
    v.seq = ++voiceSeq_;
    addVoice(pending_, v, kMaxPending);
}

// With every voice busy the oldest goes, a sound effect before any announcer line (64 at once is a big fight: a burst
// of shots must not cut a call-out that just started). It used to drop voices_.front(), taking that for the oldest,
// but mix()'s swap-and-pop moves the newest voice into a finished one's slot, so the front could be a line started
// a moment ago. One pass over the list, only when full; the new voice takes the slot (no shifting the rest down).
// (pending_ by the same rule, only ever full when the mixer has stalled: a device reopening)
void Audio::addVoice(std::vector<Voice>& list, const Voice& v, size_t cap) {
    if (list.size() < cap) { list.push_back(v); return; }
    size_t drop = 0;
    bool dropFx = list[0].id < SND_VOICE_THREE;
    for (size_t k = 1; k < list.size(); k++) {
        const bool fx = list[k].id < SND_VOICE_THREE;
        if ((fx && !dropFx) || (fx == dropFx && (int32_t)(list[k].seq - list[drop].seq) < 0)) { drop = k; dropFx = fx; }   // (wrap-safe)
    }
    list[drop] = v;
}

// The top of each period: what the game thread handed over since the last (mu_ held for a swap and a few copies).
void Audio::takePending() {
    bool newLines = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        incoming_.swap(pending_);   // (both reserved: pending_ gets the empty one back, nothing allocated)
        mixListener_ = listenerNext_; mixListenerYaw_ = listenerYawNext_;
        if (linesNew_) {
            linesNew_ = false; newLines = true;
            retiredLines_ = std::move(mixLines_);   // (empty before this: loadAnnouncer emptied it; the game thread frees them)
            mixLines_ = std::move(nextLines_);
        }
    }
    if (newLines)   // (a voice change cuts the lines playing in the old voice)
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.id >= SND_VOICE_THREE; }), voices_.end());
    for (const Voice& v : incoming_) addVoice(voices_, v, kMaxVoices);   // (in the order they were played)
    incoming_.clear();
}

int Audio::voicesPlaying(int id) {
    std::lock_guard<std::mutex> l(mixMu_);
    takePending();
    int n = 0;
    for (auto& v : voices_) n += v.id == id;
    return n;
}

void Audio::render(float* buf, int n) {
    QZONE("audio mix");
    const float rateScale = rateScale_;   // (1, or our 48 kHz over the device's own rate: exclusive, or shared in its mix format)
    std::memset(buf, 0, sizeof(float) * n * 2);
    {
        std::lock_guard<std::mutex> l(mixMu_);   // (the mixer's own: the game thread never waits on it)
        takePending();
        const Vec3 right = rightOf(mixListenerYaw_);
        const float perFrame = n > 0 ? 1.0f / n : 0.0f;
        const float fxVol = fxVolume_, annVol = annVolume_;   // (EFFECTS / ANNOUNCER VOLUME: read once a period)
        for (size_t k = 0; k < voices_.size();) {
            Voice& v = voices_[k];
            static const std::vector<float> none;
            const bool line = v.id >= SND_VOICE_THREE;
            const std::vector<float>& s = !line ? sounds_[v.pack][v.id]
                                        : mixLines_ ? mixLines_->line[v.id - SND_VOICE_THREE] : none;
            // a 3D sound follows the listener: its gains worked out again each period, ramped to over it (no clicks)
            float gl = v.gainL, gr = v.gainR, dl = 0, dr = 0;
            if (v.spatial) { voiceGains(v, mixListener_, right); dl = (v.gainL - gl) * perFrame; dr = (v.gainR - gr) * perFrame; }
            {   // its category's volume (the voice keeps its own gains: a change applies from the next period)
                const float c = line ? annVol : fxVol;
                gl *= c; gr *= c; dl *= c; dr *= c;
            }
            // the frames it has left, counted once: no end check in the loop (it reads the sample after: it ends at size - 1)
            const uint64_t end = s.size() >= 2 ? (uint64_t)(s.size() - 1) << 32 : 0;
            const uint64_t step = (uint64_t)std::llround(std::max(0.0, (double)v.step * rateScale) * 4294967296.0);
            int m = 0;
            if (v.pos < end) m = step == 0 ? n : (int)std::min<uint64_t>((uint64_t)n, (end - v.pos + step - 1) / step);
            const float* d = s.data();
            uint64_t p = v.pos;
            for (int i = 0; i < m; i++) {
                const size_t idx = (size_t)(p >> 32);
                const float f = (float)(uint32_t)p * (1.0f / 4294967296.0f), x = d[idx] + (d[idx + 1] - d[idx]) * f;
                gl += dl; gr += dr;
                buf[i * 2] += x * gl;
                buf[i * 2 + 1] += x * gr;
                p += step;
            }
            v.pos = p;
            if (p >= end) { voices_[k] = voices_.back(); voices_.pop_back(); } // finished: swap-and-pop (order doesn't matter)
            else k++;
        }
    }
    float m = master_;
    for (int i = 0; i < n * 2; i++) {
        float x = buf[i] * m;
        buf[i] = x / (1.0f + std::fabs(x) * 0.3f); // soft limiter: many overlapping sounds never clip harshly
    }
}

void Audio::mix(int16_t* out, int frames) {
    float buf[kMixFrames * 2];
    int n = std::min(frames, kMixFrames);
    render(buf, n);
    for (int i = 0; i < n * 2; i++) out[i] = (int16_t)clampf(buf[i] * 32767.0f, -32767, 32767);
    for (int i = n * 2; i < frames * 2; i++) out[i] = 0;
}

// Settings > DEVICES: a new output reopens the device the way a lost device or EXCLUSIVE AUDIO does (the backend's own
// loop, the same buffers and period: nothing added to the latency); the same one again does nothing.
void Audio::setOutputDevice(const std::string& id) {
    {
        std::lock_guard<std::mutex> l(devMu_);
        if (id == outDevice_) return;
        outDevice_ = id;
    }
#if defined(__APPLE__)
    if (running_ && backend_) { closeQueue(); openQueue(); }   // (no thread of ours: the queue is made again here)
#else
    reopen_ = true;   // (before start(): the first open takes it anyway)
#endif
}

// --- backends ------------------------------------------------------------------

#if defined(_WIN32)
// Output through WASAPI (shared mode, woken by the audio engine each period): about 20 ms from a sound to the speaker at
// the usual 10 ms period (less at the engine's shortest, Windows 10+ where the driver allows it), where WinMM needed a
// 30 ms queue on top of its own buffering (shorter WinMM queues broke up on the owner's PC). At 10 ms our 48 kHz 16-bit
// stereo is converted by Windows to the device's format. WinMM stays as the fallback (no WASAPI device).
namespace {
const GUID kClsidDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIidAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};     // KSDATAFORMAT_SUBTYPE_PCM
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};   // ..._IEEE_FLOAT
template <class T> void releaseCom(T*& p) { if (p) { p->Release(); p = nullptr; } }
enum SampleFormat { kFmt16, kFmtFloat, kFmt24 };   // what the device's buffer takes (kFmt24: 24 bits at the top of 32)
}   // (out of the anonymous namespace: an abstract class there with no derived class in this file let GCC treat every
    // call through it as unreachable, and the cold path called nowhere: a crash at audio start on Windows 10+)
// IAudioClient3 (Windows 10+: shared mode at a shorter engine period), declared here as older MinGW headers lack it: the
// IAudioClient2 methods before it are only slots (never called)
const GUID kIidAudioClient3 = {0x7ED4EE07, 0x8E67, 0x4CD4, {0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42}};
struct AudioClient3 : public IAudioClient {
    virtual HRESULT STDMETHODCALLTYPE IsOffloadCapable_() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClientProperties_() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBufferSizeLimits_() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSharedModeEnginePeriod(const WAVEFORMATEX* fmt, UINT32* def, UINT32* fundamental, UINT32* mn, UINT32* mx) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSharedModeEnginePeriod(WAVEFORMATEX** fmt, UINT32* cur) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitializeSharedAudioStream(DWORD flags, UINT32 periodFrames, const WAVEFORMATEX* fmt, LPCGUID session) = 0;
};
namespace {
#ifndef AUDCLNT_E_ENGINE_PERIODICITY_LOCKED
#define AUDCLNT_E_ENGINE_PERIODICITY_LOCKED AUDCLNT_ERR(0x028)
#endif
// the engine's mix format is one fill() writes: 32-bit float, front left and right first (any further channels silent)
bool floatMix(const WAVEFORMATEX* f) {
    if (f->wBitsPerSample != 32 || f->nChannels < 2) return false;
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag != WAVE_FORMAT_EXTENSIBLE || f->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) return false;
    const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)f;
    return IsEqualGUID(x->SubFormat, kSubtypeFloat) && (x->dwChannelMask & 3) == 3;   // (SPEAKER_FRONT_LEFT | _RIGHT)
}
std::wstring wideOf(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string utf8Of(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 1 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
// PKEY_Device_FriendlyName ("Speakers (Realtek(R) Audio)"), declared here: MinGW's functiondiscoverykeys header
// defines it only with INITGUID
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
}

// The active endpoints (unplugged and disabled ones aren't offered), in Windows' order. On a thread of its own: COM in
// its own apartment, whatever the game thread's is.
std::vector<DeviceEntry> Audio::listDevices(bool input) {
    std::vector<DeviceEntry> out;
    std::thread t([&out, input] {
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        IMMDeviceEnumerator* en = nullptr;
        IMMDeviceCollection* col = nullptr;
        UINT n = 0;
        if (SUCCEEDED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, (void**)&en)) &&
            SUCCEEDED(en->EnumAudioEndpoints(input ? eCapture : eRender, DEVICE_STATE_ACTIVE, &col)) && SUCCEEDED(col->GetCount(&n)))
            for (UINT i = 0; i < n; i++) {
                IMMDevice* d = nullptr;
                IPropertyStore* ps = nullptr;
                LPWSTR id = nullptr;
                if (SUCCEEDED(col->Item(i, &d)) && SUCCEEDED(d->GetId(&id)) && id) {
                    DeviceEntry e{utf8Of(id), ""};
                    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
                        PROPVARIANT v;
                        PropVariantInit(&v);
                        if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) e.name = utf8Of(v.pwszVal);
                        PropVariantClear(&v);
                    }
                    if (e.name.empty()) e.name = e.id;
                    out.push_back(e);
                }
                if (id) CoTaskMemFree(id);
                releaseCom(ps); releaseCom(d);
            }
        releaseCom(col); releaseCom(en);
        if (com) CoUninitialize();
    });
    t.join();
    return out;
}

void Audio::wasapiThread(std::atomic<int>* started) {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    // (the multimedia scheduler's "Pro Audio" class, when it's there: steadier wake-ups than a priority alone)
    HANDLE mmcss = nullptr;
    if (HMODULE avrt = LoadLibraryW(L"avrt.dll")) {
        using SetChar = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
        DWORD idx = 0;
        if (auto f = (SetChar)(void*)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW")) mmcss = f(L"Pro Audio", &idx);
    }
    (void)mmcss;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    bool reported = false;
    // One session: open the default output, run until it fails (unplugged, default device changed) or the EXCLUSIVE AUDIO
    // setting changes, then open it again.
    while (!quit_) {
        reopen_ = false;
        IMMDeviceEnumerator* en = nullptr;
        IMMDevice* dev = nullptr;
        IAudioClient* ac = nullptr;
        IAudioRenderClient* rc = nullptr;
        bool ok = ev && SUCCEEDED(CoCreateInstance(kClsidDeviceEnumerator, nullptr, CLSCTX_ALL, kIidDeviceEnumerator, (void**)&en));
        // the output chosen in Settings > DEVICES (by its endpoint id), if it's there and working; else the default
        const char* devNote = "";
        if (ok) {
            const std::string want = outputDevice();
            bool missing = false;
            if (!want.empty()) {
                const std::wstring w = wideOf(want);
                DWORD st = 0;
                if (SUCCEEDED(en->GetDevice(w.c_str(), &dev)) && SUCCEEDED(dev->GetState(&st)) && st == DEVICE_STATE_ACTIVE &&
                    SUCCEEDED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, (void**)&ac)))
                    devNote = ", the chosen device";
                else { releaseCom(ac); releaseCom(dev); missing = true; devNote = ", the default device (the chosen one is gone)"; }
            }
            if (!dev) ok = SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
                           SUCCEEDED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, (void**)&ac));
            noteOutput(missing);
        }
        UINT32 frames = 0, target = 0;   // the buffer's size; how much of it we keep queued
        UINT32 rate = R;
        bool excl = false, busy = false;
        const bool wantExcl = exclusive_;
        int fmt = kFmt16;
        auto pcm16 = [](UINT32 hz) {
            WAVEFORMATEX wf{};
            wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = hz; wf.wBitsPerSample = 16;
            wf.nBlockAlign = 4; wf.nAvgBytesPerSec = hz * 4;
            return wf;
        };
        if (ok && wantExcl) {
            // Exclusive (the setting): the game has the device to itself, past Windows' mixer and its 10 ms period, at
            // the device's shortest period (3 ms on the owner's PC); other programs can't play sound meanwhile. It needs
            // a format the device takes as it is: at our rate or a common one (the mixer converts), 16-bit as plain PCM
            // (what it always tried first), else as WAVEFORMATEXTENSIBLE 32-bit float, 24 bits in 32 or 16-bit (many
            // devices take only those in exclusive mode; fill() converts).
            REFERENCE_TIME defPer = 0, minPer = 0;
            ac->GetDevicePeriod(&defPer, &minPer);
            if (minPer <= 0) minPer = defPer > 0 ? defPer : 30000;
            struct Try { int fmt; bool ext; };
            static const Try tries[] = {{kFmt16, false}, {kFmtFloat, true}, {kFmt24, true}, {kFmt16, true}};
            bool stop = false;   // (busy, or exclusive not allowed on this device: no format will do)
            for (UINT32 hz : {(UINT32)R, 44100u, 96000u, 88200u}) {   // (R is 48 kHz: tried once, first)
                for (const Try& t : tries) {
                    WAVEFORMATEXTENSIBLE x{};
                    if (!t.ext) x.Format = pcm16(hz);
                    else {
                        const WORD bits = t.fmt == kFmt16 ? 16 : 32;
                        x.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; x.Format.nChannels = 2; x.Format.nSamplesPerSec = hz;
                        x.Format.wBitsPerSample = bits; x.Format.nBlockAlign = 2 * bits / 8; x.Format.nAvgBytesPerSec = hz * x.Format.nBlockAlign;
                        x.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                        x.Samples.wValidBitsPerSample = t.fmt == kFmt24 ? 24 : bits;
                        x.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
                        x.SubFormat = t.fmt == kFmtFloat ? kSubtypeFloat : kSubtypePcm;
                    }
                    WAVEFORMATEX* wf = &x.Format;
                    HRESULT hr = ac->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, wf, nullptr);
                    if (hr == S_OK) {
                        hr = ac->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPer, minPer, wf, nullptr);
                        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {   // (the period, rounded to what the device can do)
                            UINT32 n = 0;
                            ac->GetBufferSize(&n);
                            releaseCom(ac);
                            minPer = (REFERENCE_TIME)(10000000.0 * n / hz + 0.5);
                            hr = SUCCEEDED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, (void**)&ac))
                                     ? ac->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPer, minPer, wf, nullptr) : E_FAIL;
                        }
                        excl = SUCCEEDED(hr) && SUCCEEDED(ac->SetEventHandle(ev)) && SUCCEEDED(ac->GetBufferSize(&frames)) && frames > 0 &&
                               SUCCEEDED(ac->GetService(kIidAudioRenderClient, (void**)&rc));
                        if (excl) { rate = hz; target = frames; fmt = t.fmt; break; }
                        // (it said yes, then failed: the next format, on a fresh client, as one can be initialized only once)
                        releaseCom(rc); releaseCom(ac);
                        if (FAILED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, (void**)&ac))) { ac = nullptr; stop = true; }
                    }
                    busy = busy || hr == AUDCLNT_E_DEVICE_IN_USE;
                    stop = stop || busy || hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED;
                    if (stop) break;
                }
                if (excl || stop) break;
            }
            if (!excl) {   // (busy, or no such format: shared, on a fresh client)
                releaseCom(rc); releaseCom(ac);
                ok = SUCCEEDED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, (void**)&ac));
            }
        }
        UINT32 chans = 2, lowPeriod = 0;   // the buffer's channels; shared at the engine's shortest period: it, in frames
        double periodMs = 0;               // (shared: the engine's period, for the log)
        const char* why = "";              // (shared at the default period: why not the shortest)
        if (ok && !excl) {
            // Shared at the engine's shortest period (Windows 10+, IAudioClient3; where the driver allows it, e.g. 3 ms
            // against the usual 10): in the engine's own mix format (32-bit float at the device's rate, so nothing is
            // converted on the way in; the mixer steps through our 48 kHz as for exclusive). Else the 10 ms path below,
            // which QUARK_AUDIO_LEGACY=1 forces.
            const char* legacy = getenv("QUARK_AUDIO_LEGACY");
            AudioClient3* ac3 = nullptr;
            WAVEFORMATEX* mf = nullptr;
            UINT32 defF = 0, fundF = 0, minF = 0, maxF = 0;
            bool tried = false;   // (a failed initialize: the fallback needs a fresh client)
            if (legacy && *legacy && strcmp(legacy, "0") != 0) why = "QUARK_AUDIO_LEGACY";
            else if (FAILED(ac->QueryInterface(kIidAudioClient3, (void**)&ac3))) { ac3 = nullptr; why = "no IAudioClient3, before Windows 10"; }
            else if (FAILED(ac->GetMixFormat(&mf)) || !mf) { mf = nullptr; why = "no mix format"; }
            else if (!floatMix(mf)) why = "the mix format isn't float stereo";
            else if (FAILED(ac3->GetSharedModeEnginePeriod(mf, &defF, &fundF, &minF, &maxF)) || minF == 0 || minF >= defF)
                why = "the driver has no shorter period";
            else {
                tried = true;
                HRESULT hr = ac3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minF, mf, nullptr);
                if (SUCCEEDED(hr) && SUCCEEDED(ac->SetEventHandle(ev)) && SUCCEEDED(ac->GetBufferSize(&frames)) && frames > 0 &&
                    SUCCEEDED(ac->GetService(kIidAudioRenderClient, (void**)&rc))) {
                    // one period queued, as at 10 ms: the engine takes one per wake-up and we refill it straight away
                    lowPeriod = minF; rate = mf->nSamplesPerSec; chans = mf->nChannels; fmt = kFmtFloat;
                    target = std::min<UINT32>(frames, minF);
                    periodMs = minF * 1000.0 / rate;
                } else why = hr == AUDCLNT_E_ENGINE_PERIODICITY_LOCKED ? "another program holds the engine at its period" : "the shorter period failed to open";
            }
            if (mf) CoTaskMemFree(mf);
            releaseCom(ac3);
            if (!lowPeriod && tried) {
                releaseCom(rc); releaseCom(ac);
                frames = 0;
                ok = SUCCEEDED(dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, (void**)&ac));
            }
        }
        if (ok && !excl && !lowPeriod) {
            WAVEFORMATEX wf = pcm16(R);
            REFERENCE_TIME period = 0;   // the engine's period (10 ms usually)
            if (FAILED(ac->GetDevicePeriod(&period, nullptr)) || period <= 0) period = 100000;
            ok = SUCCEEDED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                          AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, period, 0, &wf, nullptr)) &&
                 SUCCEEDED(ac->SetEventHandle(ev)) && SUCCEEDED(ac->GetBufferSize(&frames)) && frames > 0 &&
                 SUCCEEDED(ac->GetService(kIidAudioRenderClient, (void**)&rc));
            // Windows gives a bigger buffer than asked (23 ms on the owner's PC): keeping it full would be that late. One
            // engine period queued: the engine takes exactly one per wake-up and we refill it straight away (the owner
            // tried 1-20 ms on 2026-10-23: 10 ms, one period, was clean; anything less got worse).
            target = std::min<UINT32>(frames, (UINT32)(period * R / 10000000));
            periodMs = period / 10000.0;
        }
        // (testing where a PC starts cutting out: QUARK_AUDIO_MS=<ms queued>, e.g. 10 / 12 / 13 / 15; shared, either
        // period, up to the buffer Windows gave)
        if (ok && !excl)
            if (const char* e = getenv("QUARK_AUDIO_MS")) {
                double ms = atof(e);
                if (ms >= 1 && ms <= 200) target = std::min<UINT32>(frames, (UINT32)(ms * rate / 1000));
            }
        rateScale_ = (float)R / (float)rate;   // (the mixer steps through our 48 kHz sounds at the device's rate)
        auto fill = [&](UINT32 n) {   // n frames of the mix into the device's buffer
            BYTE* p = nullptr;
            if (FAILED(rc->GetBuffer(n, &p))) return false;
            for (UINT32 left = n; left > 0;) {
                int k = (int)std::min<UINT32>(left, kMixFrames);
                if (fmt == kFmt16) { mix((int16_t*)p, k); p += k * 4; }
                else {   // (exclusive's 32-bit formats, and shared's float mix format: the mix before it's made 16-bit)
                    float f[kMixFrames * 2];
                    render(f, k);
                    if (fmt == kFmtFloat) {
                        float* o = (float*)p;
                        if (chans == 2) for (int i = 0; i < k * 2; i++) o[i] = clampf(f[i], -1, 1);
                        else for (int i = 0; i < k; i++, o += chans) {   // (a surround mix format: the rest silent)
                            o[0] = clampf(f[i * 2], -1, 1); o[1] = clampf(f[i * 2 + 1], -1, 1);
                            for (UINT32 c = 2; c < chans; c++) o[c] = 0;
                        }
                    }
                    else { int32_t* o = (int32_t*)p; for (int i = 0; i < k * 2; i++) o[i] = (int32_t)(clampf(f[i], -1, 1) * 8388607.0f) * 256; }
                    p += k * 4 * chans;
                }
                left -= k;
            }
            return SUCCEEDED(rc->ReleaseBuffer(n, 0));
        };
        if (ok) ok = fill(target) && SUCCEEDED(ac->Start());
        exclusiveState_ = !wantExcl ? kExclOff : ok && excl ? kExclOn : busy ? kExclBusy : kExclUnsupported;
        static const char* const fmtName[] = {"16-bit", "32-bit float", "24-bit"};
        char per[128] = "";   // (shared: the engine period it runs at, and why not a shorter one)
        if (!excl && lowPeriod) snprintf(per, sizeof per, ", %.2f ms engine period (its shortest)", periodMs);
        else if (!excl) snprintf(per, sizeof per, ", %.2f ms engine period (%s)", periodMs, why);
        if (ok) printf("audio: WASAPI %s, %u Hz %s, %.1f ms queued (%.1f ms buffer)%s%s%s\n", excl ? "exclusive" : "shared", (unsigned)rate,
                       fmtName[fmt], target * 1000.0 / rate, frames * 1000.0 / rate, per,
                       !wantExcl || excl ? "" : busy ? " (exclusive: the device is busy)" : " (exclusive: no format the device takes)", devNote);
        if (!reported) { reported = true; started->store(ok ? 1 : -1); if (!ok) { releaseCom(rc); releaseCom(ac); releaseCom(dev); releaseCom(en); break; } }
        int silent = 0;   // (exclusive: waits with no wake-up from the device; it's gone after a while)
        while (ok && !quit_ && !reopen_) {
            if (WaitForSingleObject(ev, 50) != WAIT_OBJECT_0) { if (excl && ++silent > 20) break; if (excl) continue; }
            silent = 0;
            if (excl) { if (!fill(frames)) break; continue; }   // (exclusive: the device hands back a whole buffer each time)
            UINT32 pad = 0;
            if (FAILED(ac->GetCurrentPadding(&pad))) break;   // the device went away
            if (pad < target && !fill(target - pad)) break;
        }
        if (ok) ac->Stop();
        if (exclusiveState_ == kExclOn) exclusiveState_ = kExclOff;   // (busy / unsupported stay until the next try)
        releaseCom(rc); releaseCom(ac); releaseCom(dev); releaseCom(en);
        if (!quit_ && !reopen_) Sleep(ok ? 100 : 500);   // (then the default device again)
    }
    if (ev) CloseHandle(ev);
    if (com) CoUninitialize();
}

bool Audio::start() {
    synthesize();
    running_ = true;
    quit_ = false;
    {   // WASAPI first
        std::atomic<int> started{0};
        thread_ = std::thread([this, &started] { wasapiThread(&started); });
        while (started.load() == 0) Sleep(1);
        if (started.load() == 1) return true;
        thread_.join();
    }
    running_ = false;
    printf("audio: no WASAPI output, using WinMM\n");
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = R;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = R * 4;
    HWAVEOUT h = nullptr;
    // the device signals this event whenever it finishes a buffer: the mixing thread wakes right then, instead of
    // polling with Sleep(1) (which Windows rounds up to ~15.6 ms, longer than the queue: gaps of silence)
    waveEvent_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&h, WAVE_MAPPER, &wf, (DWORD_PTR)waveEvent_, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { CloseHandle(waveEvent_); waveEvent_ = nullptr; return false; }
    backend_ = h;
    running_ = true;
    quit_ = false;
    thread_ = std::thread([this] { backendThread(); });
    return true;
}
void Audio::backendThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    timeBeginPeriod(1);   // (1 ms timer resolution for the wait's timeout)
    HWAVEOUT h = (HWAVEOUT)backend_;
    // The queue: 6 x 5 ms (30 ms). Shorter WinMM queues were tried on the owner's PC (2026-10-23): 3 x 5 ms growing on
    // underrun broke the sound up, and 2 x 10 ms worked once, then broke up on a later run. WinMM adds its own buffering
    // on top; lower latency needs WASAPI, not a shorter queue here.
    constexpr int kBufs = 6, kFrames = kPeriod;
    static int16_t data[kBufs][kFrames * 2];
    WAVEHDR hdr[kBufs] = {};
    for (int i = 0; i < kBufs; i++) {
        hdr[i].lpData = (LPSTR)data[i];
        hdr[i].dwBufferLength = sizeof data[i];
        waveOutPrepareHeader(h, &hdr[i], sizeof(WAVEHDR));
        hdr[i].dwFlags |= WHDR_DONE;
    }
    while (!quit_) {
        bool wrote = false;
        for (int i = 0; i < kBufs; i++) {
            if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
            mix(data[i], kFrames);
            hdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(h, &hdr[i], sizeof(WAVEHDR));
            wrote = true;
        }
        if (!wrote) WaitForSingleObject((HANDLE)waveEvent_, 5);
    }
    timeEndPeriod(1);
    waveOutReset(h);
    for (int i = 0; i < kBufs; i++) waveOutUnprepareHeader(h, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(h);
    CloseHandle((HANDLE)waveEvent_);
    waveEvent_ = nullptr;
}
void Audio::stop() {
    if (!running_) return;
    quit_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
    backend_ = nullptr;
}

#elif defined(__APPLE__)
static void aqCallback(void* user, AudioQueueRef q, AudioQueueBufferRef b) {
    Audio* a = (Audio*)user;
    a->mix((int16_t*)b->mAudioData, kPeriod);
    b->mAudioDataByteSize = kPeriod * 4;
    AudioQueueEnqueueBuffer(q, b, 0, nullptr);
}
namespace {
std::string utf8Of(CFStringRef s) {
    char buf[512];
    return s && CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8) ? std::string(buf) : std::string();
}
}
// Every device with streams that way, by its UID (stable across restarts) and name.
std::vector<DeviceEntry> Audio::listDevices(bool input) {
    std::vector<DeviceEntry> out;
    AudioObjectPropertyAddress all = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, 0};   // (0: the main element)
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &all, 0, nullptr, &size) != noErr || size == 0) return out;
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &all, 0, nullptr, &size, ids.data()) != noErr) return out;
    ids.resize(size / sizeof(AudioObjectID));
    for (AudioObjectID id : ids) {
        AudioObjectPropertyAddress streams = {kAudioDevicePropertyStreams, input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput, 0};
        UInt32 n = 0;
        if (AudioObjectGetPropertyDataSize(id, &streams, 0, nullptr, &n) != noErr || n == 0) continue;   // (none that way)
        AudioObjectPropertyAddress uidA = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, 0};
        AudioObjectPropertyAddress nameA = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, 0};
        CFStringRef uid = nullptr, name = nullptr;
        UInt32 sz = sizeof(CFStringRef);
        if (AudioObjectGetPropertyData(id, &uidA, 0, nullptr, &sz, &uid) != noErr || !uid) continue;
        sz = sizeof(CFStringRef);
        if (AudioObjectGetPropertyData(id, &nameA, 0, nullptr, &sz, &name) != noErr) name = nullptr;
        DeviceEntry e{utf8Of(uid), utf8Of(name)};
        CFRelease(uid);
        if (name) CFRelease(name);
        if (e.name.empty()) e.name = e.id;
        if (!e.id.empty()) out.push_back(e);
    }
    return out;
}
// The queue, on the chosen device when it's there (else the system's default output, with outputMissing())
bool Audio::openQueue() {
    AudioStreamBasicDescription f{};
    f.mSampleRate = R;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 16;
    AudioQueueRef q = nullptr;
    if (AudioQueueNewOutput(&f, aqCallback, this, nullptr, nullptr, 0, &q) != 0) return false;
    const std::string want = outputDevice();
    bool missing = false;
    if (!want.empty()) {
        bool there = false;
        for (const DeviceEntry& d : listDevices(false)) there = there || d.id == want;
        if (there) {
            CFStringRef uid = CFStringCreateWithCString(nullptr, want.c_str(), kCFStringEncodingUTF8);
            there = uid && AudioQueueSetProperty(q, kAudioQueueProperty_CurrentDevice, &uid, sizeof uid) == 0;
            if (uid) CFRelease(uid);
        }
        missing = !there;
    }
    noteOutput(missing);
    for (int i = 0; i < 3; i++) {
        AudioQueueBufferRef b;
        AudioQueueAllocateBuffer(q, kPeriod * 4, &b);
        aqCallback(this, q, b);
    }
    AudioQueueStart(q, nullptr);
    backend_ = q;
    return true;
}
void Audio::closeQueue() {
    if (!backend_) return;
    AudioQueueStop((AudioQueueRef)backend_, true);
    AudioQueueDispose((AudioQueueRef)backend_, true);
    backend_ = nullptr;
}
bool Audio::start() {
    synthesize();
    running_ = true;
    if (!openQueue()) { running_ = false; return false; }
    return true;
}
void Audio::backendThread() {}
void Audio::stop() {
    if (!running_) return;
    closeQueue();
    running_ = false;
}

#else // Linux: ALSA, loaded at runtime so the game still runs on systems without it
namespace {
typedef int (*p_open)(void**, const char*, int, int);
typedef int (*p_set)(void*, int, int, unsigned, unsigned, int, unsigned);
typedef long (*p_write)(void*, const void*, unsigned long);
typedef int (*p_recover)(void*, int, int);
typedef int (*p_close)(void*);
typedef int (*p_hint)(int, const char*, void***);
typedef char* (*p_gethint)(const void*, const char*);
typedef int (*p_freehint)(void**);
struct Alsa {
    void* lib; void* pcm; p_open open; p_set set; p_write write; p_recover recover; p_close close;
    // 2 = S16_LE, 3 = RW_INTERLEAVED, 20 ms total latency
    bool openPcm(const char* name) {
        void* p = nullptr;
        if (open(&p, name, 0, 0) < 0) return false;
        if (set(p, 2, 3, 2, R, 1, 20000) < 0) { close(p); return false; }
        pcm = p;
        return true;
    }
};
// The per-card entries worth offering (ALSA lists every plugin route of every card: front, surround51, dmix...)
bool alsaOffered(const std::string& n) {
    return n.rfind("sysdefault:", 0) == 0 || n.rfind("hdmi:", 0) == 0 || n == "pulse" || n == "pipewire" || n == "jack";
}
}
// ALSA's device hints (snd_device_name_hint), loaded at runtime like the output
std::vector<DeviceEntry> Audio::listDevices(bool input) {
    std::vector<DeviceEntry> out;
    void* lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!lib) return out;
    auto hint = (p_hint)dlsym(lib, "snd_device_name_hint");
    auto get = (p_gethint)dlsym(lib, "snd_device_name_get_hint");
    auto freeHint = (p_freehint)dlsym(lib, "snd_device_name_free_hint");
    void** hints = nullptr;
    if (hint && get && freeHint && hint(-1, "pcm", &hints) == 0 && hints) {
        for (void** h = hints; *h; h++) {
            char* name = get(*h, "NAME");
            char* desc = get(*h, "DESC");
            char* io = get(*h, "IOID");   // (none: both ways)
            const std::string n = name ? name : "";
            if ((!io || strcmp(io, input ? "Input" : "Output") == 0) && alsaOffered(n)) {
                std::string d = desc ? desc : n;
                for (size_t k; (k = d.find('\n')) != std::string::npos;) d.replace(k, 1, ", ");
                out.push_back({n, d});
            }
            free(name); free(desc); free(io);
        }
        freeHint(hints);
    }
    dlclose(lib);
    return out;
}
bool Audio::start() {
    synthesize();
    void* lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!lib) return false;
    Alsa a{lib, nullptr, (p_open)dlsym(lib, "snd_pcm_open"), (p_set)dlsym(lib, "snd_pcm_set_params"), (p_write)dlsym(lib, "snd_pcm_writei"),
           (p_recover)dlsym(lib, "snd_pcm_recover"), (p_close)dlsym(lib, "snd_pcm_close")};
    if (!a.open || !a.set || !a.write || !a.recover || !a.close) { dlclose(lib); return false; }
    // the chosen device (Settings > DEVICES), else the default
    const std::string want = outputDevice();
    const bool missing = !want.empty() && !a.openPcm(want.c_str());
    if (!a.pcm && !a.openPcm("default")) { dlclose(lib); return false; }
    noteOutput(missing);
    reopen_ = false;
    backend_ = new Alsa(a);
    running_ = true;
    quit_ = false;
    thread_ = std::thread([this] { backendThread(); });
    return true;
}
void Audio::backendThread() {
    Alsa* a = (Alsa*)backend_;
    int16_t data[kPeriod * 2];
    while (!quit_) {
        if (reopen_) {   // another device chosen: closed and opened again here, between two writes
            reopen_ = false;
            if (a->pcm) { a->close(a->pcm); a->pcm = nullptr; }
            const std::string want = outputDevice();
            const bool missing = !want.empty() && !a->openPcm(want.c_str());
            if (!a->pcm && !a->openPcm("default")) { reopen_ = true; std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
            noteOutput(missing);
        }
        mix(data, kPeriod);
        long r = a->write(a->pcm, data, kPeriod); // blocks until there's room: paces the loop
        if (r < 0) a->recover(a->pcm, (int)r, 1);
    }
}
void Audio::stop() {
    if (!running_) return;
    quit_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
    Alsa* a = (Alsa*)backend_;
    if (!a) return;
    backend_ = nullptr;
    if (a->pcm) a->close(a->pcm);
    dlclose(a->lib);
    delete a;
}
#endif

// ---- mods: anyone's own sounds, no rebuild needed. <game folder>/mods/sounds/<kit>/<SOUND>.wav replaces that sound
// in that kit (kit: default, heavy_metal, magical, retro, mouth; "all" = every kit); SOUND as in the list in
// mods/sounds/README.txt (ROCKET, STEP, ...). WAV only (8 / 16 / 24-bit PCM or 32-bit float, any rate, mono or
// stereo). Trimmed of leading silence, cut at 3 s, loudness matched to the sound it replaces (unless listed in
// mods/sounds/sound-levels.txt, e.g. "default/ROCKET": then it keeps its own level). Read once at startup.
static const char* kModSoundNames[] = {"HIT", "KILL", "HURT", "STEP", "JUMP", "LAND", "RIFLE", "ROCKET", "KNIFE", "SNIPER", "LASER",
    "SHOTGUN", "STICKY", "CROSSBOW", "BUBBLE", "SYRINGE", "EXPLOSION", "STICKY_ATTACH", "BOLT_HIT", "DASH", "GRAPPLE_FIRE",
    "GRAPPLE_HIT", "DEATH", "STREAK", "TYPING", "CHECKPOINT", "FINISH", "HIT_MED", "HIT_HEAVY", "HIT_BRUTAL",
    "SUPER", "HYPER", "LOCATOR",
    // the announcer's lines (SND_VOICE_*), as the asset export and the overrides name them
    "THREE", "TWO", "ONE", "GO", "BOMB_PLANTED", "BOMB_DEFUSED", "HILL_TAKEN", "HILL_LOST", "POINT_TAKEN", "POINT_LOST",
    "ROUND_WON", "ROUND_LOST", "OVERTIME", "YOU_WIN", "YOU_LOSE", "HIGH_SCORE", "NEW_LEADER", "YOU_DIED", "HEADSHOT",
    "AIRBORNE", "AIR_ROCKET", "SPREE", "RAMPAGE", "UNSTOPPABLE", "GODLIKE", "ONE_MINUTE", "THIRTY_SECONDS", "TEN_SECONDS",
    "FIVE_SECONDS", "ROUND_OVER"};
static_assert(sizeof(kModSoundNames) / sizeof(kModSoundNames[0]) == SND_COUNT, "one name per sound, in SoundId order");
static const char* kModKitDirs[] = {"default", "heavy_metal", "magical", "retro", "mouth"};
static_assert(sizeof(kModKitDirs) / sizeof(kModKitDirs[0]) == Audio::kPacks, "one folder per sound kit");
static const char* kVoiceDirs[kAnnouncerVoices] = {"male", "female", "british", "old_man", "e_girl", "india", "spy"};

const char* Audio::soundName(int id) { return kModSoundNames[id < 0 || id >= SND_COUNT ? 0 : id]; }
const char* Audio::kitDir(int pack) { return kModKitDirs[pack < 0 || pack >= kPacks ? 0 : pack]; }
const char* Audio::voiceDir(int v) { return kVoiceDirs[v < 0 || v >= kAnnouncerVoices ? 0 : v]; }

// sound-levels.txt (docs/ASSETS.md): the replacements that keep their own level. The sound editor levels groups of
// sounds by LUFS, and matching each one to the sound it replaces would undo that. One asset path a line without its
// extension ("sounds/default/ROCKET", "voices/male/GO"; in mods/sounds/ relative to it: "default/ROCKET"), any case,
// '#' starts a comment line. Kept lower case with '/' separators; a stray audio extension is dropped.
static std::set<std::string> keptLevels(const std::string& text) {
    std::set<std::string> out;
    for (size_t at = 0; at < text.size();) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(at, e - at);
        at = e + 1;
        size_t a = l.find_first_not_of(" \t\r"), b = l.find_last_not_of(" \t\r");
        if (a == std::string::npos || l[a] == '#') continue;
        l = l.substr(a, b - a + 1);
        for (char& c : l) c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
        while (l.rfind("./", 0) == 0) l.erase(0, 2);
        for (const char* ext : {".wav", ".ogg", ".flac", ".mp3"}) {
            size_t n = strlen(ext);
            if (l.size() > n && l.compare(l.size() - n, n, ext) == 0) { l.resize(l.size() - n); break; }
        }
        out.insert(l);
    }
    return out;
}
static bool kept(const std::set<std::string>& k, std::string path) {
    if (k.empty()) return false;
    for (char& c : path) c = (char)std::tolower((unsigned char)c);
    return k.count(path) != 0;
}

// A clip (mono, any rate) ready to play: leading silence off (it starts the moment it plays), at the mix rate, cut
// at maxSeconds with a short fade, as loud as `match` (the sound it replaces; peak 0.89 when that's silent; heard
// loudness for the hitsound's tiers, so a replacement keeps its place on the ladder). keep (sound-levels.txt): its
// own level instead, only brought down to peak 1.0 if it goes over (scaled, never clipped).
static std::vector<float> prepareClip(const std::vector<float>& x, int rate, float maxSeconds, const std::vector<float>& match, bool heard = false, bool keep = false) {
    float peak = 1e-6f; for (float v : x) peak = std::max(peak, std::fabs(v));
    size_t on = 0; while (on < x.size() && std::fabs(x[on]) < peak * 0.03f) on++;
    size_t n = std::min<size_t>((size_t)((double)(x.size() - on) * R / rate), (size_t)(R * maxSeconds));
    std::vector<float> v(n);
    for (size_t i = 0; i < n; i++) {
        double t = on + (double)i * rate / R; size_t a = (size_t)t; float f = (float)(t - a);
        float x0 = x[std::min(a, x.size() - 1)], x1 = x[std::min(a + 1, x.size() - 1)];
        v[i] = x0 + (x1 - x0) * f;
    }
    size_t fade = std::min<size_t>(n, R / 100);
    for (size_t i = 0; i < fade; i++) v[n - 1 - i] *= (float)i / fade;
    if (keep) {
        float m = 0; for (float s : v) m = std::max(m, std::fabs(s));
        if (m > 1.0f) for (auto& s : v) s /= m;
        return v;
    }
    if (heard && Audio::heardLoudness(match) > 1e-6f) { levelHit(v, match); return v; }
    float want = loudness(match), have = loudness(v);
    if (want > 1e-6f && have > 1e-6f) { float g = want / have; for (auto& s : v) s = std::clamp(s * g, -1.0f, 1.0f); }
    else { float m = 1e-6f; for (float s : v) m = std::max(m, std::fabs(s)); for (auto& s : v) s *= 0.89f / m; }
    return v;
}

void Audio::loadModSounds() {
    std::string root = findDataPath("mods/sounds");
    std::set<std::string> keep;
    if (FILE* f = u8fopen(root + "/sound-levels.txt", "rb")) {   // (the one extra file open without it)
        std::string text; char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
        fclose(f);
        keep = keptLevels(text);
    }
    for (int pass = 0; pass < 2; pass++)   // "all" first, a kit's own folder wins over it
    for (int kit = 0; kit < kPacks; kit++) {
        const char* sub = pass == 0 ? "all" : kModKitDirs[kit];
        std::string dir = root + "/" + sub;
        for (int id = 0; id < SND_VOICE_THREE; id++) {
            FILE* f = u8fopen(dir + "/" + kModSoundNames[id] + ".wav", "rb");
            if (!f) continue;
            std::vector<uint8_t> file;
            uint8_t buf[65536]; size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0 && file.size() < (64u << 20)) file.insert(file.end(), buf, buf + n);
            fclose(f);
            std::vector<float> x; int rate = 0;
            if (file.size() < 4 || memcmp(file.data(), "RIFF", 4) || !decodeSound(file.data(), file.size(), x, rate) || x.size() < 16) continue;
            sounds_[kit][id] = prepareClip(x, rate, 3.0f, sounds_[kit][id], hitTier(id) >= 0,
                                           kept(keep, std::string(sub) + "/" + kModSoundNames[id]));
        }
    }
}

// ---- the owner's overrides (docs/ASSETS.md): sounds/<kit>/<SOUND>.<wav|ogg|flac|mp3> replaces that sound in that
// kit, voices/<voice>/<LINE>.<ext> that announcer line; each as loud as the sound it replaces, unless sound-levels.txt
// lists it (then its own level, keptLevels). A DEFAULT override
// also becomes the sound of every kit that had DEFAULT's (and RETRO's chiptune of it). Nothing to do without any.
static bool overrideClip(const std::string& base, std::vector<float>& x, int& rate) {
    std::vector<uint8_t> file;
    std::string matched, err;
    if (!overrides::find(base, {".wav", ".ogg", ".flac", ".mp3"}, file, &matched)) return false;
    if (decodeSound(file.data(), file.size(), x, rate, &err) && x.size() >= 16) return true;
    fprintf(stderr, "override %s: %s\n", matched.c_str(), err.empty() ? "too short" : err.c_str());
    return false;
}

void Audio::loadOverrideSounds() {
    if (!overrides::any()) return;
    const std::set<std::string> keep = keptLevels(overrides::soundLevels());
    for (int kit = 0; kit < kPacks; kit++)   // (DEFAULT first: the other kits are matched to its loudness)
        for (int id = 0; id < SND_VOICE_THREE; id++) {
            std::vector<float> x; int rate = 0;
            std::string base = std::string("sounds/") + kModKitDirs[kit] + "/" + kModSoundNames[id];
            if (!overrideClip(base, x, rate)) continue;
            std::vector<float> v = prepareClip(x, rate, 3.0f, sounds_[0][id], hitTier(id) >= 0, kept(keep, base));
            if (kit == 0) {
                for (int p = 1; p < kPacks; p++)
                    if (p != kPackRetro && sounds_[p][id] == sounds_[0][id]) sounds_[p][id] = v;   // kits that had DEFAULT's sound
                sounds_[kPackRetro][id] = v;
                voice(sounds_[kPackRetro][id], kPackRetro);                                      // RETRO: DEFAULT, chiptuned
            }
            sounds_[kit][id].swap(v);
        }
}

void Audio::loadOverrideVoice(int set, std::vector<float>* lines, int count) {
    if (!overrides::any()) return;
    const std::set<std::string> keep = keptLevels(overrides::soundLevels());
    for (int k = 0; k < count && SND_VOICE_THREE + k < SND_COUNT; k++) {
        std::vector<float> x; int rate = 0;
        std::string base = std::string("voices/") + kVoiceDirs[set] + "/" + kModSoundNames[SND_VOICE_THREE + k];
        if (overrideClip(base, x, rate)) lines[k] = prepareClip(x, rate, 5.0f, lines[k], false, kept(keep, base));
    }
}
