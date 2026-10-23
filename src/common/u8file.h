// UTF-8 file names everywhere (COR-5). Every path the game keeps is a UTF-8 std::string; on Windows the narrow
// C / iostream APIs (fopen, std::ifstream(const char*), getenv, argv) use the ANSI code page instead, so a user
// folder like C:\Users\Zoë or C:\Users\Дмитрий broke the data dir, configs, maps, the light cache and demos.
// These helpers go through the wide APIs there (libstdc++'s std::filesystem::path already reads narrow strings
// as UTF-8 on Windows, so fs:: calls on these strings are fine as they are).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

// A path for std::ifstream / std::ofstream / fs:: from a UTF-8 string (an invalid one: an empty path, so the open
// fails instead of throwing).
inline std::filesystem::path utf8Path(const std::string& s) {
    try { return std::filesystem::u8path(s); } catch (...) { return std::filesystem::path(); }
}

inline FILE* u8fopen(const std::string& path, const char* mode) {
#ifdef _WIN32
    std::filesystem::path p = utf8Path(path);
    if (p.empty()) return nullptr;
    std::wstring m(mode, mode + strlen(mode));
    return _wfopen(p.c_str(), m.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

// An environment variable as UTF-8 ("" if unset).
inline std::string u8getenv(const char* name) {
#ifdef _WIN32
    std::wstring n(name, name + strlen(name));
    const wchar_t* v = _wgetenv(n.c_str());
    if (!v || !*v) return "";
    try { return std::filesystem::path(v).u8string(); } catch (...) { return ""; }
#else
    const char* v = getenv(name);
    return v ? v : "";
#endif
}
