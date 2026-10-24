// Test groups in their own processes (groupproc.h): fork on Linux / macOS, CreateProcess on Windows.
#include "groupproc.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>

// Every group process goes in this job, which kills them all when this process ends however it ends (the watchdog's
// _Exit, Ctrl+C, a crash): a hung group never outlives the run, as nothing outlives a killed process group on Linux.
static HANDLE groupJob() {
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (j && !SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof li)) { CloseHandle(j); j = nullptr; }
        return j;
    }();
    return job;
}

intptr_t groupStart(const char* name, const std::string& log, bool low, const std::function<int()>&) {
    // a group that crashes exits with its exception code instead of waiting on Windows Error Reporting's dialog (the
    // error mode is inherited)
    static const bool quiet = (SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX), true);
    (void)quiet;
    std::wstring exe(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &exe[0], (DWORD)exe.size());
    if (n == 0 || n >= exe.size()) return -1;
    exe.resize(n);
    std::wstring cmd = L"\"" + exe + L"\" group " + std::filesystem::path(name).wstring() + L" --child";
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};   // (inherited: the child's stdout and stderr)
    HANDLE out = CreateFileW(std::filesystem::path(log).wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return -1;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = out;
    PROCESS_INFORMATION pi{};
    // (the handles inherited are this log's only: groups start one at a time from this thread and each log's handle is
    //  closed here once its process has it)
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED | (low ? BELOW_NORMAL_PRIORITY_CLASS : 0),
                             nullptr, nullptr, &si, &pi);
    CloseHandle(out);
    if (!ok) return -1;
    if (HANDLE job = groupJob()) AssignProcessToJobObject(job, pi.hProcess);   // (before it runs; fails only inside a job on Windows 7)
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return (intptr_t)pi.hProcess;
}

int groupWait(const std::vector<intptr_t>& running, int* code, std::string* crash) {
    if (running.empty() || running.size() > MAXIMUM_WAIT_OBJECTS) return -1;
    std::vector<HANDLE> h;
    for (intptr_t r : running) h.push_back((HANDLE)r);
    DWORD w = WaitForMultipleObjects((DWORD)h.size(), h.data(), FALSE, INFINITE);
    if (w < WAIT_OBJECT_0 || w >= WAIT_OBJECT_0 + h.size()) return -1;
    int k = (int)(w - WAIT_OBJECT_0);
    DWORD ec = 1;
    GetExitCodeProcess(h[k], &ec);
    CloseHandle(h[k]);
    *code = (int)ec;
    crash->clear();
    if (ec > 100) {   // (a group exits with its failure count, at most 100: more is an exception's NTSTATUS)
        char b[64];
        snprintf(b, sizeof b, "crashed (exception 0x%08lX)", (unsigned long)ec);
        *crash = b;
    }
    return k;
}
#else
#include <sys/wait.h>
#include <unistd.h>

intptr_t groupStart(const char*, const std::string& log, bool low, const std::function<int()>& child) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {   // the child: one test group, its output to the file
        if (low && nice(10) == -1) {}
        if (!freopen(log.c_str(), "w", stdout)) std::_Exit(99);
        dup2(fileno(stdout), 2);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        std::_Exit(child());
    }
    return pid;   // (-1: fork failed)
}

int groupWait(const std::vector<intptr_t>& running, int* code, std::string* crash) {
    for (;;) {
        int st = 0;
        pid_t pid = wait(&st);
        if (pid <= 0) return -1;
        for (size_t k = 0; k < running.size(); k++) {
            if (running[k] != pid) continue;
            *code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
            *crash = WIFEXITED(st) ? "" : "crashed";
            return (int)k;
        }
    }
}
#endif
