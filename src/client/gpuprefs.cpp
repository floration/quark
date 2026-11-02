#include "gpuprefs.h"
#include "../common/net.h"   // timeNow

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cctype>
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

// ---- PERFORMANCE CHECK: the driver profile's values (read only) -------------------------------------------------------

// The NVIDIA settings that apply to this executable: its profile's value, else the global profile's. ids -> values; a
// missing id: the driver's default. false: no NVIDIA driver.
static bool nvReadSettings(const std::vector<NvU32>& ids, std::vector<long long>* out) {
    out->assign(ids.size(), -1);
    HMODULE dll = LoadLibraryW(L"nvapi64.dll");
    if (!dll) return false;
    using Query = void*(__cdecl*)(NvU32);
    const auto q = (Query)GetProcAddress(dll, "nvapi_QueryInterface");
    if (!q) return false;
    const auto init = (int(__cdecl*)())q(0x0150e828);
    const auto createSession = (int(__cdecl*)(void**))q(0x0694d52e);
    const auto destroySession = (int(__cdecl*)(void*))q(0xdad9cff8);
    const auto load = (int(__cdecl*)(void*))q(0x375dbd6b);
    const auto findApp = (int(__cdecl*)(void*, NvU16*, void**, NvApp*))q(0xeee566b2);
    const auto globalProfile = (int(__cdecl*)(void*, void**))q(0x617bff9f);
    const auto getSetting = (int(__cdecl*)(void*, void*, NvU32, NvSetting*))q(0x73bba1fa);
    if (!init || !createSession || !destroySession || !load || !findApp || !globalProfile || !getSetting || init() != 0) return false;
    void* s = nullptr;
    if (createSession(&s) != 0 || !s) return false;
    if (load(s) == 0) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const wchar_t* exe = path;
        for (const wchar_t* p = path; *p; p++) if (*p == L'\\' || *p == L'/') exe = p + 1;
        auto name = std::make_unique<NvName>();
        copyStr(name->s, exe);
        auto app = std::make_unique<NvApp>();
        app->version = (NvU32)(sizeof(NvApp) | (4u << 16));
        void* appProf = nullptr; void* glob = nullptr;
        if (findApp(s, name->s, &appProf, app.get()) != 0) appProf = nullptr;
        if (globalProfile(s, &glob) != 0) glob = nullptr;
        auto set = std::make_unique<NvSetting>();
        for (size_t i = 0; i < ids.size(); i++)
            for (void* prof : {appProf, glob}) {
                if (!prof) continue;
                *set = NvSetting{};
                set->version = (NvU32)(sizeof(NvSetting) | (1u << 16));
                if (getSetting(s, prof, ids[i], set.get()) == 0) { (*out)[i] = set->current.u32; break; }
            }
    }
    destroySession(s);
    return true;
}

static bool guidIs(const GUID& g, unsigned long d1, unsigned short d2, unsigned short d3) { return g.Data1 == d1 && g.Data2 == d2 && g.Data3 == d3; }

// ---- AMD: ADLX (amdadlx64.dll, installed with the Adrenalin driver; also AMD's integrated Radeon Graphics) -----------
// Read only. ADLX is a set of COM-like interfaces; only the few calls below are used, each by its slot in the interface's
// table as AMD's ADLX SDK lays it out (ADLX 1.0's IADLXSystem, IADLXGPUList, IADLX3DSettingsServices and the 3D setting
// interfaces: slots 0-2 of the last three are Acquire / Release / QueryInterface). No AMD driver: the DLL isn't there.
namespace {
using AdlxRes = int;
using AdlxSlot = void*;
template <class F> F adlxFn(void* obj, int slot) { return (F)(*(AdlxSlot**)obj)[slot]; }
void adlxRelease(void* obj) { if (obj) adlxFn<long(__stdcall*)(void*)>(obj, 1)(obj); }
// IADLX3DSettingsServices: 3 GetAntiLag, 4 GetChill, 5 GetBoost, 7 GetEnhancedSync, 8 GetWaitForVerticalRefresh,
// 9 GetFrameRateTargetControl; each (this, IADLXGPU*, IADLX3D...**)
void* adlxSetting(void* svc, int slot, void* gpu) {
    void* out = nullptr;
    if (adlxFn<AdlxRes(__stdcall*)(void*, void*, void**)>(svc, slot)(svc, gpu, &out) != 0) return nullptr;
    return out;
}
bool adlxBool(void* obj, int slot) {   // IsSupported (3) / IsEnabled (4): adlx_bool is a C++ bool
    bool b = false;
    return obj && adlxFn<AdlxRes(__stdcall*)(void*, bool*)>(obj, slot)(obj, &b) == 0 && b;
}
}  // namespace

static bool amdReadSettings(std::vector<PerfFinding>* f) {
    HMODULE dll = LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) return false;
    using Init = AdlxRes(__cdecl*)(unsigned long long, void**);
    using Term = AdlxRes(__cdecl*)();
    auto init = (Init)GetProcAddress(dll, "ADLXInitialize");
    auto initAny = (Init)GetProcAddress(dll, "ADLXInitializeWithIncompatibleDriver");
    auto term = (Term)GetProcAddress(dll, "ADLXTerminate");
    void* sys = nullptr;
    const unsigned long long ver = (1ull << 48);   // (ADLX 1.0: the interfaces used here)
    if (!(init && init(ver, &sys) == 0 && sys) && !(initAny && initAny(ver, &sys) == 0 && sys)) { FreeLibrary(dll); return false; }
    bool read = false;
    void *gpus = nullptr, *svc = nullptr;
    // IADLXSystem (no Acquire / Release of its own): 1 GetGPUs, 7 Get3DSettingsServices
    if (adlxFn<AdlxRes(__stdcall*)(void*, void**)>(sys, 1)(sys, &gpus) == 0 && gpus &&
        adlxFn<AdlxRes(__stdcall*)(void*, void**)>(sys, 7)(sys, &svc) == 0 && svc) {
        // IADLXGPUList: 3 Size, 11 At_GPUList(index, IADLXGPU**)
        const unsigned n = adlxFn<unsigned(__stdcall*)(void*)>(gpus, 3)(gpus);
        bool antiLagOff = false, chill = false, boost = false, esync = false, vsync = false, frtc = false;
        const unsigned first = adlxFn<unsigned(__stdcall*)(void*)>(gpus, 5)(gpus);   // (5 Begin)
        for (unsigned i = first; i < first + n && i < first + 8; i++) {
            void* gpu = nullptr;
            if (adlxFn<AdlxRes(__stdcall*)(void*, unsigned, void**)>(gpus, 11)(gpus, i, &gpu) != 0 || !gpu) continue;
            read = true;
            if (void* s = adlxSetting(svc, 3, gpu)) { antiLagOff |= adlxBool(s, 3) && !adlxBool(s, 4); adlxRelease(s); }
            if (void* s = adlxSetting(svc, 4, gpu)) { chill |= adlxBool(s, 3) && adlxBool(s, 4); adlxRelease(s); }
            if (void* s = adlxSetting(svc, 5, gpu)) { boost |= adlxBool(s, 3) && adlxBool(s, 4); adlxRelease(s); }
            if (void* s = adlxSetting(svc, 7, gpu)) { esync |= adlxBool(s, 3) && adlxBool(s, 4); adlxRelease(s); }
            if (void* s = adlxSetting(svc, 8, gpu)) {   // 5 GetMode: 3 = always on
                int mode = 0;
                if (adlxBool(s, 3) && adlxFn<AdlxRes(__stdcall*)(void*, int*)>(s, 5)(s, &mode) == 0) vsync |= mode == 3;
                adlxRelease(s);
            }
            if (void* s = adlxSetting(svc, 9, gpu)) { frtc |= adlxBool(s, 3) && adlxBool(s, 4); adlxRelease(s); }
            adlxRelease(gpu);
        }
        if (antiLagOff) f->push_back({"AMD RADEON ANTI-LAG IS OFF", "ADRENALIN > GAMING > GRAPHICS: RADEON ANTI-LAG ON"});
        if (chill) f->push_back({"AMD RADEON CHILL CAPS YOUR FRAME RATE", "ADRENALIN > GAMING > GRAPHICS: RADEON CHILL OFF"});
        if (boost) f->push_back({"AMD RADEON BOOST LOWERS THE RESOLUTION IN MOTION", "ADRENALIN > GAMING > GRAPHICS: RADEON BOOST OFF"});
        if (esync) f->push_back({"AMD ENHANCED SYNC IS ON", "ADRENALIN > GAMING > GRAPHICS: ENHANCED SYNC OFF"});
        if (vsync) f->push_back({"AMD WAIT FOR VERTICAL REFRESH IS ALWAYS ON", "ADRENALIN > GAMING > GRAPHICS: WAIT FOR VERTICAL REFRESH: OFF, UNLESS APPLICATION SPECIFIES"});
        if (frtc) f->push_back({"AMD FRAME RATE TARGET CONTROL CAPS YOUR FRAME RATE", "ADRENALIN > GAMING > GRAPHICS: FRAME RATE TARGET CONTROL OFF"});
    }
    adlxRelease(svc);
    adlxRelease(gpus);
    if (term) term();
    FreeLibrary(dll);
    return read;
}

// ---- Intel: the Graphics Control Library (ControlLib.dll, installed with the driver; integrated and Arc GPUs) ---------
// Read only. The few calls and structures below are laid out as Intel's IGCL API 1.1 declares them (igcl_api.h).
namespace {
struct CtlInitArgs { uint32_t size; uint8_t version; uint32_t appVersion; uint32_t flags; uint32_t supportedVersion; uint8_t uid[16]; };
union CtlProp { struct { bool enable; } b; struct { bool enable; float value; } fl; struct { bool enable; int32_t value; } i; struct { uint32_t type; } e; struct { bool enable; uint32_t value; } u; };
struct Ctl3D { uint32_t size; uint8_t version; uint32_t feature; char* app; int8_t appLen; bool set; uint32_t valueType; CtlProp value; int32_t customSize; void* custom; };
struct CtlEndurance { uint32_t control, mode; };
enum { kCtlFrameLimit = 2, kCtlEndurance = 1, kCtlFlip = 9, kCtlLowLatency = 16 };
enum { kCtlInt32 = 2, kCtlEnum = 4, kCtlCustom = 5 };
}  // namespace

static bool intelReadSettings(std::vector<PerfFinding>* f) {
    HMODULE dll = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) return false;
    using Init = uint32_t(__cdecl*)(CtlInitArgs*, void**);
    using Enum = uint32_t(__cdecl*)(void*, uint32_t*, void**);
    using Get3D = uint32_t(__cdecl*)(void*, Ctl3D*);
    using Close = uint32_t(__cdecl*)(void*);
    auto init = (Init)GetProcAddress(dll, "ctlInit");
    auto enumDev = (Enum)GetProcAddress(dll, "ctlEnumerateDevices");
    auto get3d = (Get3D)GetProcAddress(dll, "ctlGetSet3DFeature");
    auto close = (Close)GetProcAddress(dll, "ctlClose");
    bool read = false;
    void* api = nullptr;
    CtlInitArgs ia{};
    ia.size = sizeof ia;
    ia.appVersion = (1u << 16) | 1u;   // (CTL_MAKE_VERSION(1, 1))
    if (init && enumDev && get3d && init(&ia, &api) == 0 && api) {
        uint32_t n = 0;
        void* dev[8] = {};
        if (enumDev(api, &n, nullptr) == 0 && n > 0) {
            n = std::min<uint32_t>(n, 8);
            if (enumDev(api, &n, dev) != 0) n = 0;
        }
        bool endurance = false, flip = false, lowLatOff = false, cap = false;
        char app[] = "quark.exe";
        for (uint32_t d = 0; d < n; d++) {
            if (!dev[d]) continue;
            auto get = [&](uint32_t feature, uint32_t type, void* custom, int32_t customSize, Ctl3D* out) {
                for (int pass = 0; pass < 2; pass++) {   // (the game's own profile, then the global one)
                    Ctl3D q{};
                    q.size = sizeof q; q.feature = feature; q.valueType = type; q.custom = custom; q.customSize = customSize;
                    if (pass == 0) { q.app = app; q.appLen = (int8_t)strlen(app); }
                    if (get3d(dev[d], &q) == 0) { *out = q; return true; }
                }
                return false;
            };
            Ctl3D q{};
            CtlEndurance eg{};
            if (get(kCtlEndurance, kCtlCustom, &eg, sizeof eg, &q)) { read = true; endurance |= eg.control != 0 && eg.mode != 0; }
            if (get(kCtlFlip, kCtlEnum, nullptr, 0, &q)) { read = true; flip |= q.value.e.type != 0 && !(q.value.e.type & (1u | 2u | 64u)); }   // (not the app's choice, nor off)
            if (get(kCtlLowLatency, kCtlEnum, nullptr, 0, &q)) { read = true; lowLatOff |= q.value.e.type == 0; }
            if (get(kCtlFrameLimit, kCtlInt32, nullptr, 0, &q)) { read = true; cap |= q.value.i.enable && q.value.i.value > 0; }
        }
        if (endurance) f->push_back({"INTEL ENDURANCE GAMING SAVES POWER OVER FRAMES", "INTEL GRAPHICS SOFTWARE > GRAPHICS: ENDURANCE GAMING OFF"});
        if (flip) f->push_back({"INTEL FORCES ITS OWN VSYNC MODE", "INTEL GRAPHICS SOFTWARE > GRAPHICS: VERTICAL SYNC: APPLICATION CHOICE"});
        if (lowLatOff) f->push_back({"INTEL LOW LATENCY IS OFF", "INTEL GRAPHICS SOFTWARE > GRAPHICS: LOW LATENCY ON"});
        if (cap) f->push_back({"INTEL FRAME RATE LIMITER CAPS YOUR FRAME RATE", "INTEL GRAPHICS SOFTWARE > GRAPHICS: FRAME RATE LIMITER OFF"});
        if (close) close(api);
    }
    FreeLibrary(dll);
    return read;
}

std::vector<PerfFinding> perfCheck(const std::string& glRenderer) {
    std::vector<PerfFinding> f;
    std::string ren = glRenderer;
    for (char& c : ren) c = (char)toupper((unsigned char)c);
    const bool nvidia = ren.find("NVIDIA") != std::string::npos || ren.find("GEFORCE") != std::string::npos || ren.find("RTX") != std::string::npos;
    const bool amd = ren.find("AMD") != std::string::npos || ren.find("RADEON") != std::string::npos;
    const bool intel = ren.find("INTEL") != std::string::npos;
    // a laptop on its integrated GPU while a dedicated one is there
    if (intel || ren.find("RADEON(TM) GRAPHICS") != std::string::npos || ren.find("VEGA") != std::string::npos) {
        DISPLAY_DEVICEW dd{}; dd.cb = sizeof dd;
        bool dedicated = false;
        for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); i++) {
            std::wstring n = dd.DeviceString;
            if (n.find(L"NVIDIA") != std::wstring::npos || n.find(L"Radeon RX") != std::wstring::npos || n.find(L"Arc") != std::wstring::npos) dedicated = true;
            dd = DISPLAY_DEVICEW{}; dd.cb = sizeof dd;
        }
        if (dedicated) f.push_back({"THE GAME RUNS ON YOUR INTEGRATED GPU", "WINDOWS SETTINGS > SYSTEM > DISPLAY > GRAPHICS > QUARK: HIGH PERFORMANCE"});
    }
    if (nvidia) {
        enum { kPState = 0x1057EB71, kTriple = 0x20FDD1F9, kVsync = 0x00A879CF, kPrerender = 0x007BA09E, kThreaded = 0x20C1221E, kFrl = 0x10835002 };
        std::vector<long long> v;
        if (nvReadSettings({kPState, kTriple, kVsync, kPrerender, kThreaded, kFrl}, &v)) {
            if (v[0] != 1) f.push_back({"NVIDIA POWER MANAGEMENT ISN'T MAXIMUM PERFORMANCE", "NVIDIA CONTROL PANEL > MANAGE 3D SETTINGS > POWER MANAGEMENT MODE (OR GPU MAX CLOCKS)"});
            if (v[1] == 1) f.push_back({"NVIDIA TRIPLE BUFFERING IS ON", "NVIDIA CONTROL PANEL > MANAGE 3D SETTINGS > TRIPLE BUFFERING: OFF"});
            if (v[2] == 0x47814940) f.push_back({"NVIDIA VERTICAL SYNC IS FORCED ON", "NVIDIA CONTROL PANEL > MANAGE 3D SETTINGS > VERTICAL SYNC: USE THE 3D APPLICATION SETTING"});
            if (v[3] > 1) f.push_back({"NVIDIA LETS FRAMES QUEUE UP (MAX PRE-RENDERED FRAMES)", "NVIDIA CONTROL PANEL > MANAGE 3D SETTINGS > LOW LATENCY MODE: ON"});
            if (v[4] == 1) f.push_back({"NVIDIA THREADED OPTIMIZATION IS ON", "VIDEO + AUDIO > MORE: NV THREADED OFF (OR THE NVIDIA CONTROL PANEL)"});
            if (v[5] > 0) f.push_back({"NVIDIA CAPS YOUR FRAME RATE (MAX FRAME RATE)", "NVIDIA CONTROL PANEL > MANAGE 3D SETTINGS > MAX FRAME RATE: OFF"});
        }
    }
    if (amd && !amdReadSettings(&f)) f.push_back({"AMD DRIVER SETTINGS COULDN'T BE READ", "ADRENALIN: ANTI-LAG ON; CHILL, BOOST, ENHANCED SYNC, WAIT FOR VERTICAL REFRESH OFF"});
    if (intel && !intelReadSettings(&f)) f.push_back({"INTEL DRIVER SETTINGS COULDN'T BE READ", "INTEL GRAPHICS SOFTWARE: ENDURANCE GAMING OFF, LOW LATENCY ON, VSYNC: APPLICATION CHOICE"});
    // Windows: the power plan and Windows 11's power mode, battery, GPU scheduling, Game Mode
    if (HMODULE pp = LoadLibraryW(L"powrprof.dll")) {
        using Active = DWORD(WINAPI*)(HKEY, GUID**);
        using Overlay = DWORD(WINAPI*)(GUID*);
        if (auto active = (Active)GetProcAddress(pp, "PowerGetActiveScheme")) {
            GUID* g = nullptr;
            if (active(nullptr, &g) == 0 && g) {
                if (guidIs(*g, 0xa1841308, 0x3541, 0x4fab)) f.push_back({"WINDOWS IS ON THE POWER SAVER PLAN", "CONTROL PANEL > POWER OPTIONS: HIGH PERFORMANCE"});
                LocalFree(g);
            }
        }
        if (auto overlay = (Overlay)GetProcAddress(pp, "PowerGetEffectiveOverlayScheme")) {
            GUID g{};
            if (overlay(&g) == 0 && guidIs(g, 0x961cc777, 0x2547, 0x4f9d)) f.push_back({"WINDOWS POWER MODE IS BEST POWER EFFICIENCY", "WINDOWS SETTINGS > SYSTEM > POWER: POWER MODE BEST PERFORMANCE"});
        }
    }
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps) && ps.ACLineStatus == 0) f.push_back({"THIS PC IS RUNNING ON BATTERY", "PLUG IT IN: ON BATTERY THE GPU AND CPU CLOCK DOWN"});
    if (hwSchedState() == 0) f.push_back({"HARDWARE GPU SCHEDULING IS OFF", "LATENCY: GPU SCHED: TURN ON (THEN RESTART)"});
    DWORD gm = 1, n = sizeof gm;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", RRF_RT_REG_DWORD, nullptr, &gm, &n) == ERROR_SUCCESS && gm == 0)
        f.push_back({"WINDOWS GAME MODE IS OFF", "WINDOWS SETTINGS > GAMING > GAME MODE: ON"});
    return f;
}

// ---- hardware-accelerated GPU scheduling -----------------------------------------------------------------------------

int hwSchedState() {
    DWORD v = 0, n = sizeof v;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", RRF_RT_REG_DWORD,
                     nullptr, &v, &n) != ERROR_SUCCESS) return -1;
    return v == 2 ? 1 : 0;
}

bool hwSchedEnable(std::string* msg) {
    // (reg.exe through the "runas" verb: Windows' own admin prompt; hidden, so no console window opens)
    SHELLEXECUTEINFOW ex{};
    ex.cbSize = sizeof ex;
    ex.fMask = SEE_MASK_NOCLOSEPROCESS;
    ex.lpVerb = L"runas";
    ex.lpFile = L"reg.exe";
    ex.lpParameters = L"add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\" /v HwSchMode /t REG_DWORD /d 2 /f";
    ex.nShow = SW_HIDE;
    if (!ShellExecuteExW(&ex)) { *msg = "GPU SCHEDULING: NOT CHANGED (THE ADMIN PROMPT WAS DECLINED)"; return false; }
    DWORD code = 1;
    if (ex.hProcess) { WaitForSingleObject(ex.hProcess, 10000); GetExitCodeProcess(ex.hProcess, &code); CloseHandle(ex.hProcess); }
    if (code != 0) { *msg = "GPU SCHEDULING: WINDOWS REFUSED THE CHANGE"; return false; }
    *msg = "GPU SCHEDULING ON: RESTART YOUR PC FOR IT TO TAKE EFFECT";
    return true;
}

bool fsoDisable(bool on, std::string* msg) {
    wchar_t exe[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
    if (n == 0 || n >= sizeof exe / sizeof exe[0]) { *msg = "FULLSCREEN OPTIMIZATIONS: NOT CHANGED"; return false; }
    const wchar_t* key = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    const std::wstring token = L"DISABLEDXMAXIMIZEDWINDOWEDMODE";
    wchar_t buf[1024] = {0};
    DWORD sz = sizeof buf;
    std::wstring cur;
    if (RegGetValueW(HKEY_CURRENT_USER, key, exe, RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) cur = buf;
    // the value is "~ FLAG FLAG ..." ("~": not inherited by child processes): ours added or removed, the rest kept
    std::wstring out;
    size_t i = 0;
    while (i < cur.size()) {
        while (i < cur.size() && cur[i] == L' ') i++;
        size_t j = i;
        while (j < cur.size() && cur[j] != L' ') j++;
        const std::wstring t = cur.substr(i, j - i);
        if (!t.empty() && t != L"~" && t != token) out += L" " + t;
        i = j;
    }
    if (on) out += L" " + token;
    LONG r;
    if (out.empty()) {
        r = RegDeleteKeyValueW(HKEY_CURRENT_USER, key, exe);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    } else {
        out = L"~" + out;
        r = RegSetKeyValueW(HKEY_CURRENT_USER, key, exe, REG_SZ, out.c_str(), (DWORD)((out.size() + 1) * sizeof(wchar_t)));
    }
    if (r != ERROR_SUCCESS) { *msg = "FULLSCREEN OPTIMIZATIONS: WINDOWS REFUSED THE CHANGE"; return false; }
    *msg = on ? "FULLSCREEN OPTIMIZATIONS OFF: FROM THE NEXT START" : "FULLSCREEN OPTIMIZATIONS BACK ON: FROM THE NEXT START";
    return true;
}

#else
std::vector<PerfFinding> perfCheck(const std::string&) { return {}; }
bool fsoDisable(bool, std::string* msg) { *msg = "FULLSCREEN OPTIMIZATIONS: WINDOWS ONLY"; return false; }
int hwSchedState() { return -1; }
bool hwSchedEnable(std::string* msg) { *msg = "GPU SCHEDULING: WINDOWS ONLY"; return false; }
bool RasterClock::open(GLFWmonitor*) { return false; }
void RasterClock::close() {}
bool RasterClock::nextBlank(double, double*) { return false; }
bool nvMaxClocks(bool, std::string* msg) { *msg = "GPU MAX CLOCKS: WINDOWS ONLY"; return false; }
bool nvThreadedOff(bool, std::string* msg) { *msg = "THREADED OPTIMIZATION: WINDOWS ONLY"; return false; }
bool nvDxgiPresent(bool, std::string* msg) { *msg = "DXGI PRESENT: WINDOWS ONLY"; return false; }
void setGamePriority(int) {}
std::string gamePriorityStatus() { return ""; }
#endif
