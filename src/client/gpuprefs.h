// Windows display-timing helpers for the low-latency settings.
//  - RasterClock (PRESENT: LATENT SYNC): where the monitor's scanout is, from the display driver (D3DKMTGetScanLine),
//    so a frame can be handed over just as a refresh's vertical blank begins and its tear line falls off screen.
//  - nvMaxClocks (GPU MAX CLOCKS): NVIDIA's "Power management mode: Prefer maximum performance" in the driver profile
//    for this executable (NVAPI driver settings), so a GPU that's idle half of every frame doesn't clock down.
// Elsewhere (Linux, macOS) neither is available: open() and nvMaxClocks() fail.
#pragma once
#include <string>

struct GLFWmonitor;

class RasterClock {
public:
    ~RasterClock() { close(); }
    bool open(GLFWmonitor* m);   // false: not available (not Windows, or the driver doesn't report the scanline)
    void close();
    bool ok() const { return adapter_ != 0; }
    // From one scanline read: when the next vertical blank begins, as timeNow() (refresh: the period, seconds).
    bool nextBlank(double refresh, double* at);

private:
    unsigned adapter_ = 0, source_ = 0;
    int height_ = 0, total_ = 0;   // (active lines; lines a refresh including the blank: the most seen + 1)
};

// On: the setting in this executable's driver profile (made if there's none). Off: the setting removed from it
// (the driver's default again). The driver reads it when the game starts. msg: what happened, for a notice.
bool nvMaxClocks(bool on, std::string* msg);
