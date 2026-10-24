// Small client-side rules for how the game feels: sounds and input (header-only so quark_tests can check them).
#pragma once
#include <algorithm>
#include <cmath>

// Your own hurt sound: at most one per kGap, its volume from the damage taken since the last one. Every health drop
// used to play it: under the laser (a hit every 10 ms), drain or vampire that was ~40 a second, ~9 voices on top of each
// other drowning your footsteps. Damage in between adds up and plays when the gap is over, so a burst is still heard.
struct HurtSoundGate {
    static constexpr double kGap = 0.15;
    double next = -1e9;
    int pending = 0;
    // a frame's damage (0: none): the volume to play the hurt sound at now, 0 = don't
    float frame(double now, int damage) {
        if (damage > 0) pending += damage;
        if (pending <= 0 || now < next) return 0;
        const float v = volumeFor(pending);
        pending = 0;
        next = now + kGap;
        return v;
    }
    void reset() { pending = 0; next = -1e9; }
    // 10 damage 0.56, 25 0.73, 50 or more 1.0 (it was 0.7 for any hit)
    static float volumeFor(int damage) { return std::min(1.0f, 0.45f + 0.011f * (float)damage); }
};

// A frame's mouse turn spread over that frame's ticks (F18): tick k of n (1..n) gets the yaw that much of the way from
// the last tick's to now's, so the last one has exactly now's (no added latency). All of them used to get now's: the
// whole frame's turn in its first tick, then none, which loses air-strafe speed the lower the fps (-3.6% at 144 fps,
// -9% at 60, -20% at 30 against 1000). `from`, `to` in radians, any wrap.
inline float spreadYaw(float from, float to, int k, int n) {
    if (n <= 1 || k >= n) return to;
    const float d = std::remainder(to - from, 6.28318530718f);
    return std::remainder(from + d * ((float)k / (float)n), 6.28318530718f);
}
