// The FPS benchmark (main menu > BENCHMARK, `quark --benchmark [demo] [seconds]`): the statistics of the frame times
// it measures. Header-only, so quark_tests checks the very same maths.
//
// The run: a demo played back through the normal demo playback (the shipped one, maps/benchmark/hamlet.qdemo: a
// Hamlet free for all full of bots, recorded once by the owner), seen as the demo has it, the player's HUD off. A
// demo is the same frames every run, so two runs of one build differ only by the machine; the measuring starts after
// a short warm-up (the world uploaded, caches warm) and ends with the demo.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace bench {

constexpr const char* kDemo = "maps/benchmark/hamlet.qdemo";   // (findDataPath: shipped next to the maps)
constexpr double kWarmup = 3;                   // seconds of the demo first, not measured
constexpr size_t kMaxFrames = size_t(1) << 21;  // the frame-time buffer, allocated before the run (60 s at 35,000 fps)

// What a run's frame times come to. Lows as the F3 overlay shows them: the 1% low is 1 / the 99th percentile frame
// time (the frame 99% of frames are no slower than), the 0.1% low the same at 99.9%; min FPS is 1 / the slowest frame.
struct Stats {
    size_t frames = 0;
    double seconds = 0;              // the frame times' sum
    double avgFps = 0, low1 = 0, low01 = 0, minFps = 0;
    double avgMs = 0, stdevMs = 0;   // stdev: of the frame times (population), in ms
    double p99Ms = 0, p999Ms = 0, worstMs = 0;
};

// Percentile index as the F3 overlay and --bench take it: element floor(n * p) of the sorted times (the last one at most).
inline size_t pctIndex(size_t n, double p) { return std::min(n - 1, (size_t)((double)n * p)); }

// `scratch` is reused (no allocation once it has the capacity): nth_element, not a sort, so the F3 overlay can run
// this twice a second while it measures frame spikes without causing one.
inline Stats compute(const float* t, size_t n, std::vector<float>& scratch) {
    Stats s;
    s.frames = n;
    if (!n) return s;
    scratch.assign(t, t + n);
    double sum = 0;
    for (float f : scratch) sum += f;
    const double mean = sum / n;
    double var = 0;
    for (float f : scratch) var += (f - mean) * (f - mean);
    s.seconds = sum;
    s.avgFps = sum > 0 ? n / sum : 0;
    s.avgMs = mean * 1000.0;
    s.stdevMs = std::sqrt(var / n) * 1000.0;
    // the 99th, then the 99.9th within what's above it, then the worst within what's above that
    const size_t k1 = pctIndex(n, 0.99), k2 = pctIndex(n, 0.999);
    std::nth_element(scratch.begin(), scratch.begin() + k1, scratch.end());
    const float p99 = scratch[k1];
    std::nth_element(scratch.begin() + k1, scratch.begin() + k2, scratch.end());
    const float p999 = scratch[k2], worst = *std::max_element(scratch.begin() + k2, scratch.end());
    s.p99Ms = p99 * 1000.0; s.p999Ms = p999 * 1000.0; s.worstMs = worst * 1000.0;
    s.low1 = p99 > 0 ? 1.0 / p99 : 0;
    s.low01 = p999 > 0 ? 1.0 / p999 : 0;
    s.minFps = worst > 0 ? 1.0 / worst : 0;
    return s;
}

// The graph's columns: each the slowest and the mean frame of its share of the run (frame order), `cols` of them
// (fewer when there are fewer frames). Worked out once when the run ends.
inline void graphColumns(const float* t, size_t n, size_t cols, std::vector<float>& maxMs, std::vector<float>& meanMs) {
    cols = std::min(cols, n);
    maxMs.assign(cols, 0.0f);
    meanMs.assign(cols, 0.0f);
    for (size_t c = 0; c < cols; c++) {
        const size_t a = n * c / cols, b = std::max(a + 1, n * (c + 1) / cols);
        double sum = 0;
        float mx = 0;
        for (size_t i = a; i < b; i++) { sum += t[i]; mx = std::max(mx, t[i]); }
        maxMs[c] = mx * 1000.0f;
        meanMs[c] = (float)(sum / (b - a) * 1000.0);
    }
}

// The graph's top (ms): the smallest of 5 / 10 / 20 / 40 / 80 / 160 / 320 that holds the 0.1% low frame with room
// over it, so the line isn't flattened by one hitch (frames over it are marked at the top). Never under 5, so the
// 240 fps line (4.2 ms) is always on it.
inline float graphTopMs(const Stats& s) {
    const double want = std::max(s.p999Ms * 1.25, 4.6);
    float top = 5;
    while (top < want && top < 320) top *= 2;
    return top;
}

} // namespace bench
