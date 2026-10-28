// The SSE4.2 build handing over to the AVX2 one (isa.h).
#include "isa.h"
#include <cstdlib>
#include <cstring>
#include <string>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif


namespace isa {

#if !defined(QUARK_ISA_AVX2) && defined(__x86_64__) && (defined(_WIN32) || defined(__linux__))
// Everything x86-64-v3 adds over v2 that the compiler may use (__builtin_cpu_supports reads CPUID once).
static bool cpuHasAvx2() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("fma") &&
           __builtin_cpu_supports("avx") && __builtin_cpu_supports("bmi");
}

int runAvx2Build(int argc, char** argv) {
    for (int i = 1; i < argc; i++) if (!std::strcmp(argv[i], "--no-avx2")) return -1;
    if (const char* e = std::getenv("QUARK_NO_AVX2"); e && *e && std::strcmp(e, "0")) return -1;
    if (!cpuHasAvx2()) return -1;
#if defined(_WIN32)
    static wchar_t exe[32768];
    DWORD n = GetModuleFileNameW(nullptr, exe, 32768);
    if (n == 0 || n >= 32768) return -1;
    std::wstring path(exe, n);
    size_t slash = path.find_last_of(L"\\/");
    path = path.substr(0, slash == std::wstring::npos ? 0 : slash + 1) + L"quark_avx2.exe";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return -1;
    std::wstring cmd = GetCommandLineW();   // (the same command line: argv[0] is only a name, the path is what runs)
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(path.c_str(), &cmd[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) return -1;
    // closing quark.exe (Task Manager, the launcher) closes the game too
    if (HANDLE job = CreateJobObjectW(nullptr, nullptr)) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
        AssignProcessToJobObject(job, pi.hProcess);   // (the handle stays open until this process exits)
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
#else
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return -1;
    std::string path(self, (size_t)n);
    size_t slash = path.find_last_of('/');
    path = path.substr(0, slash == std::string::npos ? 0 : slash + 1) + "quark_avx2";
    if (access(path.c_str(), X_OK) != 0) return -1;
    execv(path.c_str(), argv);
    return -1;
#endif
}
#else
int runAvx2Build(int, char**) { return -1; }   // (the AVX2 build itself, arm64, macOS)
#endif

} // namespace isa
