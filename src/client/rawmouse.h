// Raw mouse input on its own thread (Windows). A message-only window owned by a small thread is registered for the
// mouse's raw input, so every report (8000 a second from an 8 kHz mouse) is handled there, not in the game's event
// poll, where each one was a message of its own (about 60 us of a frame on the owner's PC). The game takes the summed
// motion once a frame, in a few atomic exchanges. Elsewhere (Linux, macOS) start() fails and GLFW's raw motion is used.
#pragma once
#include <atomic>
#include <thread>

class RawMouse {
public:
    ~RawMouse() { stop(); }
    bool start();   // false: not available (then use GLFW_RAW_MOUSE_MOTION)
    void stop();
    bool active() const { return active_; }
    // The motion (counts) and number of reports since the last take, and when the first of them arrived (timeNow());
    // false when there were none.
    bool take(double* dx, double* dy, int* reports, double* firstAt);

private:
    void run();
    std::thread thread_;
    std::atomic<bool> active_{false};
    std::atomic<long long> dx_{0}, dy_{0};
    std::atomic<int> reports_{0};
    std::atomic<double> firstAt_{0};
    void* hwnd_ = nullptr;
    long lastAbsX_ = 0, lastAbsY_ = 0;
    bool haveAbs_ = false;
    friend struct RawMouseProc;
};
