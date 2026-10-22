#include "openurl.h"
#include <cstdlib>
#include "../common/proc.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

void openUrl(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    if (url.empty() || url[0] == '-') return; // never let a "URL" become an option
    procSpawnDetached({"open", url});
#else
    if (url.empty() || url[0] == '-') return;
    procSpawnDetached({"xdg-open", url});
#endif
}
