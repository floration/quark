#include "gpuprefs.h"
#include "../common/net.h"   // timeNow

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wcast-function-type"   // (GetProcAddress / nvapi_QueryInterface results cast to their real types)
#endif

// ---- RasterClock: gdi32's D3DKMT calls (declared here: MinGW's headers don't all have them) --------------------------

namespace {
struct KmtOpenFromHdc { HDC hDc; UINT hAdapter; LUID luid; UINT vidPnSourceId; };
struct KmtScanLine { UINT hAdapter; UINT vidPnSourceId; BOOLEAN inVerticalBlank; UINT scanLine; };
struct KmtClose { UINT hAdapter; };
using OpenFromHdcFn = LONG(APIENTRY*)(KmtOpenFromHdc*);
using GetScanLineFn = LONG(APIENTRY*)(KmtScanLine*);
using CloseFn = LONG(APIENTRY*)(KmtClose*);
HMODULE gdi() { static HMODULE m = LoadLibraryW(L"gdi32.dll"); return m; }
}

bool RasterClock::open(GLFWmonitor* m) {
    close();
    if (!m || !gdi()) return false;
    static const auto openFromHdc = (OpenFromHdcFn)GetProcAddress(gdi(), "D3DKMTOpenAdapterFromHdc");
    static const auto getScanLine = (GetScanLineFn)GetProcAddress(gdi(), "D3DKMTGetScanLine");
    const char* dev = glfwGetWin32Adapter(m);   // (e.g. \\.\DISPLAY1)
    const GLFWvidmode* vm = glfwGetVideoMode(m);
    if (!openFromHdc || !getScanLine || !dev || !vm) return false;
    HDC dc = CreateDCA(nullptr, dev, nullptr, nullptr);
    if (!dc) return false;
    KmtOpenFromHdc o{};
    o.hDc = dc;
    const LONG st = openFromHdc(&o);
    DeleteDC(dc);
    if (st != 0) return false;
    adapter_ = o.hAdapter;
    source_ = o.vidPnSourceId;
    height_ = vm->height;
    total_ = height_ + std::max(1, height_ / 30);   // (a typical blank until a longer one is seen)
    KmtScanLine s{adapter_, source_, 0, 0};
    if (getScanLine(&s) != 0) { close(); return false; }
    return true;
}

void RasterClock::close() {
    if (!adapter_) return;
    static const auto closeFn = gdi() ? (CloseFn)GetProcAddress(gdi(), "D3DKMTCloseAdapter") : nullptr;
    KmtClose c{adapter_};
    if (closeFn) closeFn(&c);
    adapter_ = 0;
}

bool RasterClock::nextBlank(double refresh, double* at) {
    if (!adapter_) return false;
    static const auto getScanLine = (GetScanLineFn)GetProcAddress(gdi(), "D3DKMTGetScanLine");
    KmtScanLine s{adapter_, source_, 0, 0};
    if (getScanLine(&s) != 0) return false;
    const double now = timeNow();
    const int line = (int)s.scanLine;
    if (line + 1 > total_) total_ = line + 1;   // (the blank's lines: drivers count on through it)
    const double lineTime = refresh / total_;
    if (!s.inVerticalBlank && line < height_) *at = now + (height_ - line) * lineTime;          // drawing: this refresh's blank
    else if (line >= height_) *at = now + (total_ - line + height_) * lineTime;                  // in the blank: the next one
    else *at = now + height_ * lineTime;                                                         // (in it, line not given)
    return true;
}

// ---- nvMaxClocks: NVAPI's driver settings (nvapi64.dll, functions by id: NVIDIA/nvapi nvapi_interface.h) ----------

namespace {
using NvU32 = uint32_t;
using NvU16 = uint16_t;
using NvStr = NvU16[2048];
#pragma pack(push, 4)
struct NvBinary { NvU32 len; uint8_t data[4096]; };
union NvValue { NvU32 u32; NvBinary bin; NvStr str; uint64_t u64; };
struct NvSetting {   // NVDRS_SETTING_V1
    NvU32 version; NvStr name; NvU32 id, type, location, isCurrentPredefined, isPredefinedValid;
    NvValue predefined, current;
};
#pragma pack(pop)
struct NvApp {       // NVDRS_APPLICATION_V4
    NvU32 version, isPredefined; NvStr appName, friendlyName, launcher, fileInFolder; NvU32 flags; NvStr commandLine;
};
struct NvProfile {   // NVDRS_PROFILE_V1
    NvU32 version; NvStr name; NvU32 gpuSupport, isPredefined, numOfApps, numOfSettings;
};
constexpr NvU32 kPreferredPState = 0x1057EB71, kPreferMax = 1;   // "Power management mode", "Prefer maximum performance"
constexpr int kExecutableNotFound = -166, kProfileNotFound = -163, kSettingNotFound = -160, kNoPrivilege = -137;
struct NvName { NvStr s; };
void copyStr(NvStr d, const wchar_t* s) { size_t i = 0; for (; s[i] && i < 2047; i++) d[i] = (NvU16)s[i]; d[i] = 0; }
}

// One DWORD setting in this executable's driver profile: on sets it to value, off removes it (the driver's default).
// label: the setting's name in the notices.
static bool nvProfileSetting(NvU32 id, NvU32 value, bool on, const std::string& label, std::string* msg) {
    HMODULE dll = LoadLibraryW(L"nvapi64.dll");
    if (!dll) { *msg = label + ": NVIDIA GPUS ONLY"; return false; }
    using Query = void*(__cdecl*)(NvU32);
    const auto q = (Query)GetProcAddress(dll, "nvapi_QueryInterface");
    if (!q) { *msg = label + ": NVIDIA DRIVER NOT FOUND"; return false; }
    const auto init = (int(__cdecl*)())q(0x0150e828);
    const auto createSession = (int(__cdecl*)(void**))q(0x0694d52e);
    const auto destroySession = (int(__cdecl*)(void*))q(0xdad9cff8);
    const auto load = (int(__cdecl*)(void*))q(0x375dbd6b);
    const auto save = (int(__cdecl*)(void*))q(0xfcbc7e14);
    const auto findApp = (int(__cdecl*)(void*, NvU16*, void**, NvApp*))q(0xeee566b2);
    const auto findProfile = (int(__cdecl*)(void*, NvU16*, void**))q(0x7e4a9a0b);
    const auto createProfile = (int(__cdecl*)(void*, NvProfile*, void**))q(0xcc176068);
    const auto createApp = (int(__cdecl*)(void*, void*, NvApp*))q(0x4347a9de);
    const auto setSetting = (int(__cdecl*)(void*, void*, NvSetting*))q(0x577dd202);
    const auto deleteSetting = (int(__cdecl*)(void*, void*, NvU32))q(0xe4a26362);
    if (!init || !createSession || !destroySession || !load || !save || !findApp || !findProfile || !createProfile ||
        !createApp || !setSetting || !deleteSetting || init() != 0) { *msg = label + ": NVIDIA DRIVER NOT FOUND"; return false; }
    void* s = nullptr;
    if (createSession(&s) != 0 || !s) { *msg = label + ": DRIVER SETTINGS UNAVAILABLE"; return false; }
    int st = load(s);
    // this executable's name (quark.exe): its profile, or a "Quark" one made for it
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* exe = path;
    for (const wchar_t* p = path; *p; p++) if (*p == L'\\' || *p == L'/') exe = p + 1;
    auto name = std::make_unique<NvName>();
    copyStr(name->s, exe);
    auto app = std::make_unique<NvApp>();
    void* prof = nullptr;
    if (st == 0) {
        app->version = (NvU32)(sizeof(NvApp) | (4u << 16));
        st = findApp(s, name->s, &prof, app.get());
        if (st == kExecutableNotFound && on) {
            auto pname = std::make_unique<NvName>();
            copyStr(pname->s, L"Quark");
            st = findProfile(s, pname->s, &prof);
            if (st == kProfileNotFound) {
                auto p = std::make_unique<NvProfile>();
                p->version = (NvU32)(sizeof(NvProfile) | (1u << 16));
                copyStr(p->name, L"Quark");
                st = createProfile(s, p.get(), &prof);
            }
            if (st == 0) {
                *app = NvApp{};
                app->version = (NvU32)(sizeof(NvApp) | (4u << 16));
                copyStr(app->appName, exe);
                st = createApp(s, prof, app.get());
            }
        } else if (st == kExecutableNotFound) st = 1;   // (off, and no profile: nothing to undo)
    }
    if (st == 0) {
        if (on) {
            auto set = std::make_unique<NvSetting>();
            set->version = (NvU32)(sizeof(NvSetting) | (1u << 16));
            set->id = id;
            set->type = 0;   // (a DWORD)
            set->current.u32 = value;
            st = setSetting(s, prof, set.get());
        } else {
            st = deleteSetting(s, prof, id);
            if (st == kSettingNotFound) st = 0;
        }
        if (st == 0) st = save(s);
    }
    destroySession(s);
    if (st == 1 || st == 0) { *msg = label + (on ? " ON: FROM THE NEXT START" : " OFF: FROM THE NEXT START"); return true; }
    *msg = st == kNoPrivilege ? label + ": NEEDS THE GAME RUN AS ADMINISTRATOR ONCE" : label + ": THE DRIVER REFUSED (" + std::to_string(st) + ")";
    return false;
}

bool nvMaxClocks(bool on, std::string* msg) { return nvProfileSetting(kPreferredPState, kPreferMax, on, "GPU MAX CLOCKS", msg); }
// OGL_THREAD_CONTROL: 2 = threaded optimization off (the driver's extra thread adds a frame of queue on some PCs)
bool nvThreadedOff(bool on, std::string* msg) { return nvProfileSetting(0x20C1221E, 2, on, "THREADED OPTIMIZATION OFF", msg); }
// OGL_CPL_PREFER_DXPRESENT: 1 = "Vulkan/OpenGL present method: prefer layered on DXGI swapchain" (flip model in a window)
bool nvDxgiPresent(bool on, std::string* msg) { return nvProfileSetting(0x20D690F8, 1, on, "DXGI PRESENT", msg); }

// ---- setGamePriority: gdi32's D3DKMTSetProcessSchedulingPriorityClass (Windows 7+; MinGW's headers lack it) ----------

static long g_gpuPrioStatus = 1;   // (the last set call's NTSTATUS: 0 done; 1 never called / not available)

void setGamePriority(int level) {
    static int now = -1;   // (what's set: -1 nothing yet, else the level)
    if (now == level) return;
    now = level;
    const bool high = level > 0;
    enum { kNormal = 2, kAboveNormal = 3, kHigh = 4 };   // (D3DKMT_SCHEDULINGPRIORITYCLASS)
    using SetClassFn = LONG(APIENTRY*)(HANDLE, int);
    static const auto setClass = gdi() ? (SetClassFn)GetProcAddress(gdi(), "D3DKMTSetProcessSchedulingPriorityClass") : nullptr;
    HANDLE self = GetCurrentProcess();
    if (setClass) {   // (0 = STATUS_SUCCESS)
        g_gpuPrioStatus = setClass(self, high ? kHigh : kNormal);
        if (g_gpuPrioStatus != 0 && high) g_gpuPrioStatus = setClass(self, kAboveNormal);
        if (g_gpuPrioStatus != 0) printf("prioritize: Windows refused the GPU scheduling class (NTSTATUS 0x%08lX)\n", (unsigned long)g_gpuPrioStatus);
    }
    // ABOVE_NORMAL by default, not HIGH: the hosted server's and audio threads already run raised, and a HIGH process at
    // thousands of frames a second can starve the rest of the desktop (input, the launcher, voice chat). VIDEO > MORE:
    // CPU PRIORITY HIGH asks for it anyway (level 2).
    SetPriorityClass(self, level >= 2 ? HIGH_PRIORITY_CLASS : high ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
}

std::string gamePriorityStatus() {
    using GetClassFn = LONG(APIENTRY*)(HANDLE, int*);
    static const auto getClass = gdi() ? (GetClassFn)GetProcAddress(gdi(), "D3DKMTGetProcessSchedulingPriorityClass") : nullptr;
    static const char* names[] = {"IDLE", "BELOW NORMAL", "NORMAL", "ABOVE NORMAL", "HIGH", "REALTIME"};
    std::string out = "GPU PRIORITY ";
    int c = -1;
    if (getClass && getClass(GetCurrentProcess(), &c) == 0 && c >= 0 && c <= 5) out += names[c];
    else out += "UNKNOWN";
    if (g_gpuPrioStatus != 0 && g_gpuPrioStatus != 1) {
        char b[48];
        snprintf(b, sizeof b, " (REFUSED 0X%08lX)", (unsigned long)g_gpuPrioStatus);
        out += b;
    }
    const DWORD cpu = GetPriorityClass(GetCurrentProcess());
    out += cpu == ABOVE_NORMAL_PRIORITY_CLASS ? "  CPU ABOVE NORMAL" : cpu == HIGH_PRIORITY_CLASS ? "  CPU HIGH" : cpu == NORMAL_PRIORITY_CLASS ? "  CPU NORMAL" : "  CPU OTHER";
    return out;
}

#else
bool RasterClock::open(GLFWmonitor*) { return false; }
void RasterClock::close() {}
bool RasterClock::nextBlank(double, double*) { return false; }
bool nvMaxClocks(bool, std::string* msg) { *msg = "GPU MAX CLOCKS: WINDOWS ONLY"; return false; }
bool nvThreadedOff(bool, std::string* msg) { *msg = "THREADED OPTIMIZATION: WINDOWS ONLY"; return false; }
bool nvDxgiPresent(bool, std::string* msg) { *msg = "DXGI PRESENT: WINDOWS ONLY"; return false; }
void setGamePriority(int) {}
std::string gamePriorityStatus() { return ""; }
#endif
