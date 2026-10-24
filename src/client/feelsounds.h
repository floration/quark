// Small sound rules the client applies to what it hears (header-only so quark_tests can check them).
#pragma once
#include <algorithm>

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
