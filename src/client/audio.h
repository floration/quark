// Audio: a small low-latency mixer on top of the OS audio API (WinMM on Windows, CoreAudio on
// macOS, ALSA on Linux loaded at runtime). All sounds are synthesized at startup (sounds.cpp),
// so there are no sound files.
#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include "../common/qmath.h"

enum SoundId {
    SND_HIT, SND_KILL, SND_HURT,
    SND_STEP, SND_JUMP, SND_LAND,
    SND_RIFLE, SND_ROCKET, SND_KNIFE, SND_SNIPER, SND_LASER, SND_SHOTGUN, SND_STICKY, SND_CROSSBOW, SND_GRENADE,
    SND_EXPLOSION, SND_STICKY_ATTACH, SND_BOLT_HIT,
    SND_DASH, SND_GRAPPLE_FIRE, SND_GRAPPLE_HIT,
    SND_COUNT
};

class Audio {
public:
    static constexpr int kRate = 48000;
    bool start();                 // opens the device; false = no sound (game keeps running)
    void startOffline() { synthesize(); running_ = true; } // tests: mix() without a device
    void stop();
    ~Audio() { stop(); }

    // 2D (UI / your own sounds) and 3D (positioned, panned and attenuated from the listener).
    void play(int id, float volume = 1.0f, float pitch = 1.0f);
    void play3D(int id, const Vec3& pos, float volume = 1.0f, float pitch = 1.0f);
    void setListener(const Vec3& pos, float yaw) { std::lock_guard<std::mutex> l(mu_); listener_ = pos; listenerYaw_ = yaw; }
    void setMasterVolume(float v) { master_ = v; }
    bool ok() const { return running_; }

    // Mixes `frames` stereo frames into out (interleaved int16). Used by the backends and tests.
    void mix(int16_t* out, int frames);

private:
    struct Voice { int id; float pos; float step; float gainL, gainR; bool spatial; Vec3 at; float volume; };
    void synthesize();
    void voiceGains(Voice& v);
    void backendThread();

    std::vector<float> sounds_[SND_COUNT];
    std::vector<Voice> voices_;
    std::mutex mu_;
    Vec3 listener_;
    float listenerYaw_ = 0;
    std::atomic<float> master_{0.8f};
    std::atomic<bool> running_{false}, quit_{false};
    std::thread thread_;
    void* backend_ = nullptr;
};
