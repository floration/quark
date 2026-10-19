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
struct Noise { uint32_t s = 0x12345678; float operator()() { s = s * 1664525u + 1013904223u; return ((s >> 9) & 0xFFFF) / 32768.0f - 1.0f; } };

std::vector<float> make(float seconds) { return std::vector<float>((size_t)(seconds * R), 0.0f); }
float env(float t, float attack, float decay) { return (t < attack ? t / attack : 1.0f) * std::exp(-t / decay); }

void addSine(std::vector<float>& b, float f0, float f1, float amp, float attack, float decay) {
    float ph = 0;
    for (size_t i = 0; i < b.size(); i++) {
        float t = i / (float)R, f = f0 + (f1 - f0) * (t / (b.size() / (float)R));
        ph += 2 * PI * f / R;
        b[i] += std::sin(ph) * amp * env(t, attack, decay);
    }
}
// noise through a one-pole low-pass (cut 0..1, smaller = darker)
void addNoise(std::vector<float>& b, float amp, float attack, float decay, float cut, uint32_t seed = 1) {
    Noise n; n.s ^= seed * 2654435761u;
    float lp = 0;
    for (size_t i = 0; i < b.size(); i++) {
        float t = i / (float)R;
        lp += (n() - lp) * cut;
        b[i] += lp * amp * env(t, attack, decay);
    }
}
void addSaw(std::vector<float>& b, float f, float amp, float attack, float decay) {
    float ph = 0;
    for (size_t i = 0; i < b.size(); i++) {
        float t = i / (float)R;
        ph += f / R;
        ph -= std::floor(ph);
        b[i] += (ph * 2 - 1) * amp * env(t, attack, decay);
    }
}
void normalizeTo(std::vector<float>& b, float peak) {
    float m = 1e-6f;
    for (float v : b) m = std::max(m, std::fabs(v));
    for (float& v : b) v *= peak / m;
}
} // namespace

void Audio::synthesize() {
    auto& s = sounds_;
    s[SND_HIT] = make(0.07f);      addSine(s[SND_HIT], 1900, 1700, 0.8f, 0.001f, 0.02f);  normalizeTo(s[SND_HIT], 0.5f);
    s[SND_KILL] = make(0.18f);     addSine(s[SND_KILL], 1500, 1500, 0.6f, 0.001f, 0.03f);
    { auto t = make(0.18f); addSine(t, 2300, 2300, 0.6f, 0.001f, 0.04f);                    // second, higher blip
      for (size_t i = 0; i + R / 14 < s[SND_KILL].size(); i++) s[SND_KILL][i + R / 14] += t[i]; }
    normalizeTo(s[SND_KILL], 0.6f);
    s[SND_HURT] = make(0.15f);     addSine(s[SND_HURT], 160, 70, 0.8f, 0.002f, 0.05f); addNoise(s[SND_HURT], 0.4f, 0.001f, 0.03f, 0.2f); normalizeTo(s[SND_HURT], 0.6f);
    s[SND_STEP] = make(0.06f);     addNoise(s[SND_STEP], 1, 0.002f, 0.012f, 0.12f, 3); addSine(s[SND_STEP], 110, 80, 0.4f, 0.001f, 0.015f); normalizeTo(s[SND_STEP], 0.3f);
    s[SND_JUMP] = make(0.08f);     addNoise(s[SND_JUMP], 1, 0.004f, 0.02f, 0.3f, 4); normalizeTo(s[SND_JUMP], 0.2f);
    s[SND_LAND] = make(0.1f);      addSine(s[SND_LAND], 120, 60, 1, 0.001f, 0.03f); addNoise(s[SND_LAND], 0.6f, 0.001f, 0.02f, 0.1f, 5); normalizeTo(s[SND_LAND], 0.4f);
    s[SND_RIFLE] = make(0.12f);    addNoise(s[SND_RIFLE], 1, 0.0005f, 0.025f, 0.6f, 6); addSine(s[SND_RIFLE], 900, 300, 0.5f, 0.0005f, 0.02f); normalizeTo(s[SND_RIFLE], 0.55f);
    s[SND_ROCKET] = make(0.5f);    addNoise(s[SND_ROCKET], 1, 0.02f, 0.15f, 0.08f, 7); addSine(s[SND_ROCKET], 90, 60, 0.6f, 0.005f, 0.12f); normalizeTo(s[SND_ROCKET], 0.6f);
    s[SND_KNIFE] = make(0.14f);    addNoise(s[SND_KNIFE], 1, 0.03f, 0.03f, 0.9f, 8); normalizeTo(s[SND_KNIFE], 0.35f);
    s[SND_SNIPER] = make(0.7f);    addNoise(s[SND_SNIPER], 1, 0.0003f, 0.02f, 0.8f, 9); addSine(s[SND_SNIPER], 70, 40, 0.8f, 0.001f, 0.18f); addNoise(s[SND_SNIPER], 0.4f, 0.001f, 0.25f, 0.03f, 10); normalizeTo(s[SND_SNIPER], 0.8f);
    s[SND_LASER] = make(0.06f);    addSaw(s[SND_LASER], 220, 0.5f, 0.003f, 0.05f); addSaw(s[SND_LASER], 331, 0.3f, 0.003f, 0.05f); normalizeTo(s[SND_LASER], 0.25f);
    s[SND_SHOTGUN] = make(0.35f);  addNoise(s[SND_SHOTGUN], 1, 0.0005f, 0.07f, 0.25f, 11); addSine(s[SND_SHOTGUN], 85, 50, 0.9f, 0.001f, 0.08f); normalizeTo(s[SND_SHOTGUN], 0.75f);
    s[SND_STICKY] = make(0.16f);   addSine(s[SND_STICKY], 220, 120, 1, 0.001f, 0.04f); addNoise(s[SND_STICKY], 0.4f, 0.001f, 0.02f, 0.3f, 12); normalizeTo(s[SND_STICKY], 0.45f);
    s[SND_CROSSBOW] = make(0.25f); addSine(s[SND_CROSSBOW], 340, 300, 1, 0.001f, 0.06f); addNoise(s[SND_CROSSBOW], 0.5f, 0.0005f, 0.01f, 0.7f, 13); normalizeTo(s[SND_CROSSBOW], 0.45f);
    s[SND_EXPLOSION] = make(0.9f); addNoise(s[SND_EXPLOSION], 1, 0.002f, 0.25f, 0.06f, 14); addSine(s[SND_EXPLOSION], 60, 30, 0.9f, 0.002f, 0.3f); normalizeTo(s[SND_EXPLOSION], 0.85f);
    s[SND_STICKY_ATTACH] = make(0.04f); addNoise(s[SND_STICKY_ATTACH], 1, 0.0005f, 0.006f, 0.5f, 15); normalizeTo(s[SND_STICKY_ATTACH], 0.3f);
    s[SND_BOLT_HIT] = make(0.08f); addSine(s[SND_BOLT_HIT], 260, 140, 1, 0.001f, 0.02f); addNoise(s[SND_BOLT_HIT], 0.5f, 0.001f, 0.01f, 0.4f, 16); normalizeTo(s[SND_BOLT_HIT], 0.4f);
    s[SND_DASH] = make(0.28f);     addNoise(s[SND_DASH], 1, 0.03f, 0.07f, 0.35f, 17); addSine(s[SND_DASH], 300, 900, 0.25f, 0.02f, 0.08f); normalizeTo(s[SND_DASH], 0.45f);
    s[SND_GRAPPLE_FIRE] = make(0.16f); addSine(s[SND_GRAPPLE_FIRE], 1400, 300, 0.7f, 0.001f, 0.06f); addNoise(s[SND_GRAPPLE_FIRE], 0.3f, 0.001f, 0.05f, 0.7f, 18); normalizeTo(s[SND_GRAPPLE_FIRE], 0.4f);
    s[SND_GRAPPLE_HIT] = make(0.25f);
    for (float f : {1230.0f, 1710.0f, 2490.0f}) addSine(s[SND_GRAPPLE_HIT], f, f, 0.4f, 0.0005f, 0.06f);
    normalizeTo(s[SND_GRAPPLE_HIT], 0.4f);
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
