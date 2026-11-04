#include "rawmouse.h"
#include "../common/net.h"   // timeNow

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#include <algorithm>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

struct RawMouseProc {
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        RawMouse* self = (RawMouse*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (msg == WM_INPUT && self) {
            alignas(8) BYTE buf[sizeof(RAWINPUT) + 16];
            UINT size = sizeof buf;
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                const RAWINPUT* ri = (const RAWINPUT*)buf;
                if (ri->header.dwType == RIM_TYPEMOUSE) {
                    const RAWMOUSE& m = ri->data.mouse;
                    long dx = m.lLastX, dy = m.lLastY;
                    if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {   // (remote desktop, tablets: positions)
                        dx = self->haveAbs_ ? m.lLastX - self->lastAbsX_ : 0;
                        dy = self->haveAbs_ ? m.lLastY - self->lastAbsY_ : 0;
                        self->lastAbsX_ = m.lLastX; self->lastAbsY_ = m.lLastY; self->haveAbs_ = true;
                    }
                    if (dx || dy) {
                        double zero = 0;
                        self->firstAt_.compare_exchange_strong(zero, timeNow());
                        const long long tx = self->dx_.fetch_add(dx, std::memory_order_relaxed) + dx;
                        const long long ty = self->dy_.fetch_add(dy, std::memory_order_relaxed) + dy;
                        self->reports_.fetch_add(1, std::memory_order_release);
                        // (GPU LATE LATCH: the counts since the game's last read, straight to the GPU's view)
                        if (auto f = self->onMotion.load(std::memory_order_acquire)) f(self->onMotionCtx, tx, ty);
                    }
                }
            }
            return DefWindowProcW(h, msg, wp, lp);   // (lets Windows free the input)
        }
        if (msg == WM_CLOSE) { DestroyWindow(h); return 0; }
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

// The logical processor for the mouse's thread: the last one (group 0) of the fastest efficiency class, so on a
// hybrid CPU it's a performance core, never an efficiency one; -1 when there are fewer than 4 (nothing to spare)
static int pickMouseCore() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const int n = (int)std::min<DWORD>(si.dwNumberOfProcessors, 64);
    if (n < 4) return -1;
    int best = n - 1, bestClass = -1;
    typedef BOOL(WINAPI * GetSets)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
    auto get = (GetSets)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemCpuSetInformation");
    ULONG len = 0;
    if (get && !get(nullptr, 0, &len, GetCurrentProcess(), 0) && len) {   // (Windows 10+: the efficiency classes)
        std::vector<unsigned char> buf(len);
        if (get((PSYSTEM_CPU_SET_INFORMATION)buf.data(), len, &len, GetCurrentProcess(), 0)) {
            for (ULONG at = 0; at < len;) {
                const SYSTEM_CPU_SET_INFORMATION* c = (const SYSTEM_CPU_SET_INFORMATION*)(buf.data() + at);
                if (!c->Size) break;
                if (c->Type == CpuSetInformation && c->CpuSet.Group == 0 && c->CpuSet.LogicalProcessorIndex < n && !c->CpuSet.Parked) {
                    const int cls = c->CpuSet.EfficiencyClass, idx = c->CpuSet.LogicalProcessorIndex;
                    if (cls > bestClass || (cls == bestClass && idx > best)) { bestClass = cls; best = idx; }
                }
                at += c->Size;
            }
        }
    }
    return best;
}

bool RawMouse::start(bool ownCore) {
    if (active_) return true;
    std::atomic<int> ok{0};
    const int core = ownCore ? pickMouseCore() : -1;
    thread_ = std::thread([this, &ok, core] {
        // OWN CORE: time-critical (above the game's HIGHEST) on its own core, never power-throttled; off: as before
        SetThreadPriority(GetCurrentThread(), core >= 0 ? THREAD_PRIORITY_TIME_CRITICAL : THREAD_PRIORITY_HIGHEST);
        if (core >= 0) {
            SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core);
            typedef BOOL(WINAPI * SetInfo)(HANDLE, int, LPVOID, DWORD);
            if (auto set = (SetInfo)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadInformation")) {
                struct { ULONG version, mask, state; } t = {1, 1, 0};   // (THREAD_POWER_THROTTLING_STATE: execution speed, not throttled)
                set(GetCurrentThread(), 3 /*ThreadPowerThrottling*/, &t, sizeof t);
            }
        }
        WNDCLASSW wc{};
        wc.lpfnWndProc = RawMouseProc::proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"QuarkRawMouse";
        RegisterClassW(&wc);
        HWND h = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; rid.usUsage = 0x02;   // generic desktop, mouse
        rid.dwFlags = RIDEV_INPUTSINK;                 // (a message-only window is never in the foreground)
        rid.hwndTarget = h;
        if (!h || !RegisterRawInputDevices(&rid, 1, sizeof rid)) { if (h) DestroyWindow(h); ok = -1; return; }
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)this);
        hwnd_ = h;
        active_ = true;
        ok = 1;
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) DispatchMessageW(&m);
        rid.dwFlags = RIDEV_REMOVE; rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof rid);
        active_ = false;
    });
    while (ok == 0) Sleep(1);
    if (ok < 0) { thread_.join(); return false; }
    core_ = core;
    if (core >= 0) {   // the game's thread off that core (any other thread of ours may still go there)
        DWORD_PTR proc = 0, sys = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && (proc & ~((DWORD_PTR)1 << core))) {
            const DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), proc & ~((DWORD_PTR)1 << core));
            mainMask_ = prev ? prev : proc;
        }
    }
    return true;
}

void RawMouse::stop() {
    if (!thread_.joinable()) return;
    if (hwnd_) PostMessageW((HWND)hwnd_, WM_CLOSE, 0, 0);
    thread_.join();
    hwnd_ = nullptr;
    active_ = false;
    if (mainMask_) { SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)mainMask_); mainMask_ = 0; }
    core_ = -1;
}

void RawMouse::hold(GLFWwindow* w, bool on) {
    if (!on) { ClipCursor(nullptr); return; }
    HWND h = glfwGetWin32Window(w);
    RECT r;
    if (!h || !GetClientRect(h, &r)) return;
    POINT c = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    ClientToScreen(h, &c);
    RECT one = {c.x, c.y, c.x + 1, c.y + 1};
    // (called again every 0.5 s in case the window moved or Windows let the cursor go: when it's still held where it
    // should be, nothing is set. SetCursorPos posts a mouse-move the next poll handles, and the two calls cost the
    // frame ~8 us and the one after it ~20 us: a pair of late frames twice a second, owner's 1000 FPS cap runs 2026-11-03)
    RECT cur;
    POINT at;
    if (GetClipCursor(&cur) && GetCursorPos(&at) && EqualRect(&cur, &one) && at.x == c.x && at.y == c.y) return;
    SetCursorPos(c.x, c.y);
    ClipCursor(&one);
}

#else
bool RawMouse::start(bool) { return false; }
void RawMouse::stop() {}
void RawMouse::hold(GLFWwindow*, bool) {}
#endif

bool RawMouse::take(double* dx, double* dy, int* reports, double* firstAt) {
    const int n = reports_.exchange(0, std::memory_order_acquire);
    if (n == 0) return false;
    *dx = (double)dx_.exchange(0, std::memory_order_relaxed);
    *dy = (double)dy_.exchange(0, std::memory_order_relaxed);
    *reports = n;
    *firstAt = firstAt_.exchange(0);
    return true;
}
