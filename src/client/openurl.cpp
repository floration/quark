#include "openurl.h"
#include <cstdlib>
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
    std::string cmd = "open '" + url + "' &";
    if (system(cmd.c_str()) != 0) {}
#else
    std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    if (system(cmd.c_str()) != 0) {}
#endif
}
