#include "rawmouse.h"
#include "../common/net.h"   // timeNow

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
                        self->dx_.fetch_add(dx, std::memory_order_relaxed);
                        self->dy_.fetch_add(dy, std::memory_order_relaxed);
                        self->reports_.fetch_add(1, std::memory_order_release);
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

bool RawMouse::start() {
    if (active_) return true;
    std::atomic<int> ok{0};
    thread_ = std::thread([this, &ok] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
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
    return true;
}

void RawMouse::stop() {
    if (!thread_.joinable()) return;
    if (hwnd_) PostMessageW((HWND)hwnd_, WM_CLOSE, 0, 0);
    thread_.join();
    hwnd_ = nullptr;
    active_ = false;
}

void RawMouse::hold(GLFWwindow* w, bool on) {
    if (!on) { ClipCursor(nullptr); return; }
    HWND h = glfwGetWin32Window(w);
    RECT r;
    if (!h || !GetClientRect(h, &r)) return;
    POINT c = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    ClientToScreen(h, &c);
    RECT one = {c.x, c.y, c.x + 1, c.y + 1};
    SetCursorPos(c.x, c.y);
    ClipCursor(&one);
}

#else
bool RawMouse::start() { return false; }
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
