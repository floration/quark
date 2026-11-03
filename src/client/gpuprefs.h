// Windows display-timing helpers for the low-latency settings.
//  - RasterClock (PRESENT: LATENT SYNC): where the monitor's scanout is, from the display driver (D3DKMTGetScanLine),
//    so a frame can be handed over just as a refresh's vertical blank begins and its tear line falls off screen.
//  - nvMaxClocks (GPU MAX CLOCKS): NVIDIA's "Power management mode: Prefer maximum performance" in the driver profile
//    for this executable (NVAPI driver settings), so a GPU that's idle half of every frame doesn't clock down.
//  - setGamePriority (PRIORITIZE GAME): the process's GPU scheduling class (D3DKMTSetProcessSchedulingPriorityClass)
//    and CPU priority class raised while the game has focus, so a browser video or a hardware-accelerated Electron app
//    drawing at the same time waits for the game's frames instead of sharing the GPU queue with them evenly.
// Elsewhere (Linux, macOS) none is available: open() and nvMaxClocks() fail, setGamePriority() does nothing.
#pragma once
#include <string>
#include <vector>

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
// VIDEO + AUDIO > MORE 6 (1.19.4): in the same profile, NVIDIA's OpenGL "Threaded optimization: Off" and "Vulkan/OpenGL
// present method: Prefer layered on DXGI swapchain"; off removes each (the driver's default). Read at the next start.
bool nvThreadedOff(bool on, std::string* msg);
bool nvDxgiPresent(bool on, std::string* msg);

// VIDEO + AUDIO > MORE (LATENCY): Windows' "Hardware-accelerated GPU scheduling" (HAGS: the GPU schedules its own work
// queue, so a submitted frame starts sooner; the F3 gap between SWAP and GPU). A system setting: the registry value
// HwSchMode under HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers (2 on, 1 off), read at boot.
// hwSchedState: 1 on, 0 off, -1 unknown / not Windows / not supported (no value: the driver or GPU doesn't offer it).
int hwSchedState();
// LATENCY > PERFORMANCE CHECK (owner, 2026-11-02): the graphics driver's and Windows' settings that cost frames or
// latency, read (never changed): one finding a line, with where to change it. glRenderer: GL_RENDERER (the GPU the
// game runs on). Empty: nothing found (or not Windows).
struct PerfFinding { std::string what, fix; };
std::vector<PerfFinding> perfCheck(const std::string& glRenderer);
// "Disable fullscreen optimizations" (Properties > Compatibility) for this executable, for this user: the token
// DISABLEDXMAXIMIZEDWINDOWEDMODE in HKCU\...\AppCompatFlags\Layers under the exe's path (other flags kept). No admin;
// Windows reads it when the game starts. msg: for a notice.
bool fsoDisable(bool on, std::string* msg);
// Asks Windows (its admin prompt) to set it on, hidden reg.exe; takes effect after a restart. msg: for a notice.
bool hwSchedEnable(std::string* msg);

// high: GPU scheduling class HIGH (ABOVE_NORMAL if Windows refuses it) and CPU class ABOVE_NORMAL; else both NORMAL.
// Cheap and idempotent (only a change calls Windows): called on focus changes. REALTIME (admin only) is never asked for.
void setGamePriority(int level);   // 0 normal, 1 raised (CPU above normal), 2 CPU high
// For F3: the GPU scheduling class Windows reports for the game now (and the refusal's NTSTATUS, if the last set was
// refused) and the CPU class. Empty elsewhere.
std::string gamePriorityStatus();
