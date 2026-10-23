#include "openurl.h"
#include <cstdlib>
#include "../common/proc.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

void (*g_openUrlHook)(const std::string& url) = nullptr;

void openUrl(const std::string& url) {
    if (g_openUrlHook) { g_openUrlHook(url); return; }
#ifdef _WIN32
    // (wide: a folder under a non-ASCII user name, e.g. the maps or groups folder, is UTF-8 here)
    int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (n <= 0) return;
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &w[0], n);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    if (url.empty() || url[0] == '-') return; // never let a "URL" become an option
    procSpawnDetached({"open", url});
#else
    if (url.empty() || url[0] == '-') return;
    procSpawnDetached({"xdg-open", url});
#endif
}
