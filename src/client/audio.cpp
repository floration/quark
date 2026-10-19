#include "audio.h"
#include <cmath>
#include <cstring>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <mmsystem.h>
#elif defined(__APPLE__)
  #include <AudioToolbox/AudioToolbox.h>
#else
  #include <dlfcn.h>
#endif

namespace {
constexpr int R = Audio::kRate;
constexpr int kPeriod = 240;        // frames per buffer: 5 ms at 48 kHz
constexpr int kMaxVoices = 64;

// --- synthesis helpers -------------------------------------------------------
// Sounds are built in layers: a pitch-dropping "body" (weight), band-limited noise (texture),
// soft attacks/fades (no clicks), then a short room reverb so nothing sounds dry or synthetic.
struct Noise { uint32_t s = 0x12345678; float operator()() { s = s * 1664525u + 1013904223u; return ((s >> 9) & 0xFFFF) / 32768.0f - 1.0f; } };

std::vector<float> make(float seconds) { return std::vector<float>((size_t)(seconds * R), 0.0f); }
float env(float t, float attack, float decay) { return (t < attack ? std::sin(0.5f * PI * t / attack) : 1.0f) * std::exp(-t / decay); }
float onePole(float cutoffHz) { return 1.0f - std::exp(-2.0f * PI * cutoffHz / R); }

// sine whose pitch falls exponentially from f0 to f1 (kicks, thumps, booms)
void addThump(std::vector<float>& b, float f0, float f1, float pitchTime, float amp, float attack, float decay, float start = 0) {
    float ph = 0;
    for (size_t i = (size_t)(start * R); i < b.size(); i++) {
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
    for (size_t i = (size_t)(start * R); i < b.size(); i++) {
        float t = i / (float)R - start;
        float h = hiEnd > 0 ? hi + (hiEnd - hi) * std::min(1.0f, t / (b.size() / (float)R)) : hi;
        float x = n();
        a += (x - a) * onePole(h);
        c += (x - c) * onePole(lo);
        b[i] += (a - c) * amp * env(t, attack, decay);
    }
}
void addTone(std::vector<float>& b, float f, float amp, float attack, float decay, float start = 0) {
    for (size_t i = (size_t)(start * R); i < b.size(); i++) {
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
        for (size_t i = 0; i < b.size(); i++) {
            float y = line[i % D];
            lp += (y - lp) * 0.35f;
            line[i % D] = b[i] + lp * 0.72f;
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

void Audio::synthesize() {
    auto& s = sounds_;
    // feedback: soft rounded ticks
    s[SND_HIT] = make(0.09f);  addThump(s[SND_HIT], 1500, 1100, 0.01f, 1, 0.0015f, 0.022f); addTone(s[SND_HIT], 2600, 0.15f, 0.001f, 0.01f); finish(s[SND_HIT], 0.32f);
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
    s[SND_CROSSBOW] = make(0.35f); addThump(s[SND_CROSSBOW], 420, 300, 0.05f, 1, 0.001f, 0.09f); addBand(s[SND_CROSSBOW], 1500, 5000, 0.4f, 0.001f, 0.012f, 13); addRoom(s[SND_CROSSBOW], 0.2f, 0.7f); finish(s[SND_CROSSBOW], 0.24f);
    s[SND_EXPLOSION] = make(1.2f); addThump(s[SND_EXPLOSION], 120, 32, 0.08f, 1, 0.002f, 0.3f); addBand(s[SND_EXPLOSION], 40, 2500, 1, 0.003f, 0.25f, 14, 300);
    addRoom(s[SND_EXPLOSION], 0.4f, 1.4f); finish(s[SND_EXPLOSION], 0.42f);
    s[SND_GRENADE] = make(0.3f); addThump(s[SND_GRENADE], 240, 90, 0.04f, 1, 0.002f, 0.07f); addBand(s[SND_GRENADE], 150, 1200, 0.35f, 0.002f, 0.04f, 14); addRoom(s[SND_GRENADE], 0.22f, 0.7f); finish(s[SND_GRENADE], 0.26f);
    s[SND_STICKY_ATTACH] = make(0.06f); addThump(s[SND_STICKY_ATTACH], 900, 500, 0.008f, 1, 0.001f, 0.01f); finish(s[SND_STICKY_ATTACH], 0.14f);
    s[SND_BOLT_HIT] = make(0.12f); addThump(s[SND_BOLT_HIT], 320, 140, 0.015f, 1, 0.001f, 0.03f); addBand(s[SND_BOLT_HIT], 300, 2500, 0.4f, 0.001f, 0.015f, 16); finish(s[SND_BOLT_HIT], 0.2f);
    // abilities
    s[SND_DASH] = make(0.35f); addBand(s[SND_DASH], 300, 900, 1, 0.05f, 0.09f, 17, 4000); finish(s[SND_DASH], 0.22f);
    s[SND_GRAPPLE_FIRE] = make(0.2f); addBand(s[SND_GRAPPLE_FIRE], 800, 5000, 1, 0.005f, 0.06f, 18, 1500); addThump(s[SND_GRAPPLE_FIRE], 700, 300, 0.04f, 0.4f, 0.002f, 0.05f); finish(s[SND_GRAPPLE_FIRE], 0.18f);
    s[SND_GRAPPLE_HIT] = make(0.35f);
    for (float f : {1180.0f, 1630.0f, 2360.0f}) addTone(s[SND_GRAPPLE_HIT], f, 0.35f, 0.001f, 0.07f);
    addThump(s[SND_GRAPPLE_HIT], 400, 200, 0.01f, 0.5f, 0.001f, 0.02f); addRoom(s[SND_GRAPPLE_HIT], 0.25f, 0.6f); finish(s[SND_GRAPPLE_HIT], 0.2f);
}

void Audio::voiceGains(Voice& v) {
    if (!v.spatial) { v.gainL = v.gainR = v.volume; return; }
    Vec3 d = v.at - listener_;
    float dist = length(d);
    float g = v.volume / (1.0f + dist / 10.0f);
    if (dist > 90.0f) g = 0;
    // pan: how far to the listener's right the sound is (yaw 0 looks down -Z, right = +X)
    Vec3 right = {std::cos(listenerYaw_), 0, -std::sin(listenerYaw_)};
    float pan = dist > 0.01f ? clampf(dot(d / dist, right), -1, 1) : 0;
    v.gainL = g * std::sqrt(0.5f * (1 - pan * 0.8f));
    v.gainR = g * std::sqrt(0.5f * (1 + pan * 0.8f));
}

void Audio::play(int id, float volume, float pitch) {
    if (!running_ || id < 0 || id >= SND_COUNT) return;
    std::lock_guard<std::mutex> l(mu_);
    if ((int)voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());
    Voice v{id, 0, pitch, 0, 0, false, {}, volume};
    voiceGains(v);
    voices_.push_back(v);
}

void Audio::play3D(int id, const Vec3& pos, float volume, float pitch) {
    if (!running_ || id < 0 || id >= SND_COUNT) return;
    std::lock_guard<std::mutex> l(mu_);
    if ((int)voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());
    Voice v{id, 0, pitch, 0, 0, true, pos, volume};
    voiceGains(v);
    if (v.gainL + v.gainR < 0.002f) return; // inaudible
    voices_.push_back(v);
}

void Audio::mix(int16_t* out, int frames) {
    float buf[kPeriod * 2 * 4];
    int n = std::min(frames, (int)(sizeof buf / sizeof buf[0] / 2));
    std::memset(buf, 0, sizeof(float) * n * 2);
    {
        std::lock_guard<std::mutex> l(mu_);
        for (size_t k = 0; k < voices_.size();) {
            Voice& v = voices_[k];
            const std::vector<float>& s = sounds_[v.id];
            for (int i = 0; i < n; i++) {
                size_t idx = (size_t)v.pos;
                if (idx + 1 >= s.size()) { v.pos = (float)s.size(); break; }
                float f = v.pos - idx, x = s[idx] + (s[idx + 1] - s[idx]) * f;
                buf[i * 2] += x * v.gainL;
                buf[i * 2 + 1] += x * v.gainR;
                v.pos += v.step;
            }
            if ((size_t)v.pos + 1 >= s.size()) voices_.erase(voices_.begin() + k);
            else k++;
        }
    }
    float m = master_;
    for (int i = 0; i < n * 2; i++) {
        float x = buf[i] * m;
        x = x / (1.0f + std::fabs(x) * 0.3f); // soft limiter: many overlapping sounds never clip harshly
        out[i] = (int16_t)clampf(x * 32767.0f, -32767, 32767);
    }
    for (int i = n * 2; i < frames * 2; i++) out[i] = 0;
}

// --- backends ------------------------------------------------------------------

#if defined(_WIN32)
bool Audio::start() {
    synthesize();
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = R;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = R * 4;
    HWAVEOUT h = nullptr;
    if (waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
    backend_ = h;
    running_ = true;
    quit_ = false;
    thread_ = std::thread([this] { backendThread(); });
    return true;
}
void Audio::backendThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HWAVEOUT h = (HWAVEOUT)backend_;
    constexpr int kBufs = 4; // 4 x 5 ms queued
    static int16_t data[kBufs][kPeriod * 2];
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
            mix(data[i], kPeriod);
            hdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(h, &hdr[i], sizeof(WAVEHDR));
            wrote = true;
        }
        if (!wrote) Sleep(1);
    }
    waveOutReset(h);
    for (int i = 0; i < kBufs; i++) waveOutUnprepareHeader(h, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(h);
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
bool Audio::start() {
    synthesize();
    AudioStreamBasicDescription f{};
    f.mSampleRate = R;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 16;
    AudioQueueRef q = nullptr;
    if (AudioQueueNewOutput(&f, aqCallback, this, nullptr, nullptr, 0, &q) != 0) return false;
    running_ = true;
    for (int i = 0; i < 3; i++) {
        AudioQueueBufferRef b;
        AudioQueueAllocateBuffer(q, kPeriod * 4, &b);
        aqCallback(this, q, b);
    }
    AudioQueueStart(q, nullptr);
    backend_ = q;
    return true;
}
void Audio::backendThread() {}
void Audio::stop() {
    if (!running_) return;
    if (!backend_) { running_ = false; return; }
    AudioQueueStop((AudioQueueRef)backend_, true);
    AudioQueueDispose((AudioQueueRef)backend_, true);
    running_ = false;
}

#else // Linux: ALSA, loaded at runtime so the game still runs on systems without it
namespace {
typedef int (*p_open)(void**, const char*, int, int);
typedef int (*p_set)(void*, int, int, unsigned, unsigned, int, unsigned);
typedef long (*p_write)(void*, const void*, unsigned long);
typedef int (*p_recover)(void*, int, int);
typedef int (*p_close)(void*);
struct Alsa { void* lib; void* pcm; p_write write; p_recover recover; p_close close; };
}
bool Audio::start() {
    synthesize();
    void* lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!lib) return false;
    auto open = (p_open)dlsym(lib, "snd_pcm_open");
    auto set = (p_set)dlsym(lib, "snd_pcm_set_params");
    auto write = (p_write)dlsym(lib, "snd_pcm_writei");
    auto recover = (p_recover)dlsym(lib, "snd_pcm_recover");
    auto close = (p_close)dlsym(lib, "snd_pcm_close");
    void* pcm = nullptr;
    // 2 = S16_LE, 3 = RW_INTERLEAVED, 20 ms total latency
    if (!open || !set || !write || !recover || !close || open(&pcm, "default", 0, 0) < 0) { dlclose(lib); return false; }
    if (set(pcm, 2, 3, 2, R, 1, 20000) < 0) { close(pcm); dlclose(lib); return false; }
    backend_ = new Alsa{lib, pcm, write, recover, close};
    running_ = true;
    quit_ = false;
    thread_ = std::thread([this] { backendThread(); });
    return true;
}
void Audio::backendThread() {
    Alsa* a = (Alsa*)backend_;
    int16_t data[kPeriod * 2];
    while (!quit_) {
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
    a->close(a->pcm);
    dlclose(a->lib);
    delete a;
}
#endif
