// LOW LATENCY ULTRA's safety margin, learnt as it runs (VIDEO + AUDIO > MORE 8 > ULTRA SAFETY: AUTO, the slider's far left).
// Header-only so quark_tests can run it against a simulated GPU (testUltraMargin).
//
// ULTRA reads the input at  last swap + learnt GPU time - learnt CPU time - margin,  so the frame is handed to the GPU
// just as the GPU finishes the last one. The margin pays for the jitter of those two learnt times:
//  - too big: the CPU reads the input, runs its frame and then waits after the swap for the GPU (Game::afterSwap):
//    that wait is input read too early, pure latency (desktop FFA: a 93 us median wait with the fixed 150 us);
//  - too small: the GPU finishes before the next frame is handed over and idles (a bubble): lost frame rate.
// Every frame tells which way it went, so the margin follows it:
//  - waited(w): the swap found the last frame still on the GPU, and the CPU then waited w for it. The margin comes down
//    by kShrink of the wait over kAim (an EMA towards a wait of kAim: slow, ~100 frames' time constant);
//  - bubble(): the GPU was done before the frame was handed over, in a frame the pacing had held back (a frame that
//    started at once, CPU-bound, isn't the margin's doing and changes nothing). Up by kGrowAdd + kGrowMul of itself:
//    fast, a few frames, as a bubble costs frame rate and a little more margin only costs a few microseconds.
// So it settles where bubbles are rare (kShrink x the mean wait over kAim = the bubble rate x the step up: ~1-2 % of
// frames with a 20-40 us jittery GPU, each one small, the tail of the jitter past the margin), and a frame whose read
// wasn't held (CPU-bound) moves nothing. Never below kFloor, never above kMax. Simulated in quark_tests (benchmark).
#pragma once
#include <algorithm>

struct UltraMargin {
    // kFloor: the wait for the fence is measured with the 10 us poll of the pacing loop, the driver's wake from a blocking
    // wait (a few us to tens), the swap call itself (~6 us) and the timer; under 25 us the margin is inside that noise
    // and every gain is a guess. kAim: the after-swap wait aimed for, the same noise, just over zero.
    static constexpr double kFloor = 25e-6, kMax = 1000e-6, kStart = 150e-6;   // (seconds; kStart: the old fixed default)
    static constexpr double kAim = 15e-6;
    static constexpr double kShrink = 0.01;                       // per frame: of (wait - kAim)
    static constexpr double kGrowAdd = 15e-6, kGrowMul = 0.25;   // per bubble
    double margin = kStart;

    void waited(double wait) { margin = std::min(kMax, std::max(kFloor, margin - (wait - kAim) * kShrink)); }
    void bubble() { margin = std::min(kMax, margin + kGrowAdd + margin * kGrowMul); }
};
