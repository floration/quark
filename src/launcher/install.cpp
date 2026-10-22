#include "install.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "icon_data.h"
#include "../common/proc.h"
#include "../game/map.h"
#include "releases.h"
#include "selfupdate.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace fs = std::filesystem;

namespace install {
namespace {
std::error_code ec;

#ifdef _WIN32
fs::path knownFolder(int csidl) {
    wchar_t p[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, 0, p);
    return fs::path(p);
}
fs::path desktopLink() { return knownFolder(CSIDL_DESKTOPDIRECTORY) / L"Quark.lnk"; }
fs::path menuLink() { return knownFolder(CSIDL_PROGRAMS) / L"Quark" / L"Quark.lnk"; }
const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\quark";

bool makeShortcut(const fs::path& link, const fs::path& target) {
    fs::create_directories(link.parent_path(), ec);
    CoInitialize(nullptr);
    IShellLinkW* sl = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(target.wstring().c_str());
        sl->SetWorkingDirectory(target.parent_path().wstring().c_str());
        sl->SetDescription(L"quark - fast multiplayer FPS");
        sl->SetIconLocation(target.wstring().c_str(), 0);
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->Save(link.wstring().c_str(), TRUE));
            pf->Release();
        }
        sl->Release();
    }
    CoUninitialize();
    return ok;
}
// The file a .lnk points at ("" if it can't be read).
fs::path shortcutTarget(const fs::path& link) {
    if (!fs::exists(link, ec)) return {};
    CoInitialize(nullptr);
    IShellLinkW* sl = nullptr;
    fs::path out;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            wchar_t buf[MAX_PATH] = {};
            if (SUCCEEDED(pf->Load(link.wstring().c_str(), STGM_READ)) && SUCCEEDED(sl->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH)))
                out = fs::path(buf);
            pf->Release();
        }
        sl->Release();
    }
    CoUninitialize();
    return out;
}
bool pointsAt(const fs::path& link, const fs::path& target) {
    fs::path t = shortcutTarget(link);
    if (t.empty()) return false;
    std::error_code e2;
    if (fs::equivalent(t, target, e2)) return true;
    std::wstring a = t.lexically_normal().wstring(), b = target.lexically_normal().wstring();
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
void regStr(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
void regDword(HKEY k, const wchar_t* name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v); }
#else
fs::path home() { const char* h = getenv("HOME"); return fs::path(h ? h : "."); }
#ifndef __APPLE__
fs::path desktopDir() {
    // xdg-user-dir knows localized desktop folder names
    if (FILE* p = popen("xdg-user-dir DESKTOP 2>/dev/null", "r")) {
        char buf[1024] = {};
        if (fgets(buf, sizeof buf, p)) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            pclose(p);
            if (!s.empty() && fs::is_directory(s, ec)) return fs::path(s);
        } else pclose(p);
    }
    return home() / "Desktop";
}
fs::path dataHome() { const char* x = getenv("XDG_DATA_HOME"); return x && *x ? fs::path(x) : home() / ".local/share"; }
fs::path menuEntry() { return dataHome() / "applications/quark.desktop"; }
fs::path desktopEntry() { return desktopDir() / "quark.desktop"; }
bool writeDesktopFile(const fs::path& path, const fs::path& launcher) {
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    f << "[Desktop Entry]\nType=Application\nName=Quark\nComment=Fast multiplayer FPS\n"
      << "Exec=\"" << launcher.string() << "\"\nPath=" << launcher.parent_path().string() << "\nIcon="
      << (appDir() / "quark.png").string() << "\nTerminal=false\nCategories=Game;ActionGame;\n";
    f.close();
    chmod(path.string().c_str(), 0755);
    procRun({"gio", "set", path.string(), "metadata::trusted", "true"}, "", [](const char*, size_t) { return true; }); // best effort: marks the desktop icon as launchable on GNOME
    return (bool)f;
}
bool desktopFileRuns(const fs::path& path, const fs::path& launcher) {
    std::ifstream f(path);
    std::string line, want = "Exec=\"" + launcher.string() + "\"";
    while (std::getline(f, line)) if (line == want) return true;
    return false;
}
#else
fs::path appBundle() { return home() / "Applications/Quark.app"; }
fs::path desktopAlias() { return home() / "Desktop/Quark.app"; }
// The Quark.app executable: a script that runs the installed launcher. (Before 1.17 it was a copy of the launcher,
// which then never got updated and, run from there, handed over to the installed one and deleted itself.)
std::string bundleScript(const fs::path& launcher) {
    std::string q;
    for (char c : launcher.string()) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') q += '\\';
        q += c;
    }
    return "#!/bin/sh\nexec \"" + q + "\" \"$@\"\n";
}
#endif
#endif
} // namespace

fs::path appDir() {
#ifdef _WIN32
    // Per-user Programs folder (like VS Code / Discord): no admin rights needed, so updates stay silent.
    return knownFolder(CSIDL_LOCAL_APPDATA) / L"Programs" / L"quark";
#elif defined(__APPLE__)
    return home() / "Library/Application Support/quark";
#else
    return dataHome() / "quark";
#endif
}

fs::path userDataDir() {
#ifdef _WIN32
    return knownFolder(CSIDL_APPDATA) / L"quark";
#elif defined(__APPLE__)
    return home() / "Library/Application Support/quark";
#else
    return dataHome() / "quark";
#endif
}

namespace {
// Before anything in a game folder is removed: the player's files an old version kept there (quark.cfg, quark.key,
// demos, maps...) are copied to the per-user folder if they aren't there yet. Copies, never moves.
void keepUserData(const fs::path& game) {
    if (fs::exists(game, ec)) migrateUserData(game.u8string(), userDataDir().u8string());
}
// Only what the launcher put there: the game build (and its update leftovers), the launcher, its settings and icon.
[[maybe_unused]] void removeInstallFiles() {
    fs::path a = appDir();
    keepUserData(a / "game");
    for (const char* f : {"game", "game.old", "game.new", "update.qpak", "launcher.cfg", "quark.png", ".launcher.lock"})
        fs::remove_all(a / f, ec);
    for (const fs::path& l : selfupdate::launchersIn(a)) fs::remove(l, ec);
    selfupdate::removeLeftovers(installedLauncher());
    fs::remove(installedLauncher(), ec);
}
} // namespace

std::vector<fs::path> legacyAppDirs() {
#ifdef _WIN32
    return {knownFolder(CSIDL_LOCAL_APPDATA) / L"quark"};
#else
    return {};
#endif
}

void writeIcon() {
    fs::create_directories(appDir(), ec);
    fs::path p = appDir() / "quark.png";
    if (fs::file_size(p, ec) == sizeof kIconPng) return;
    std::ofstream(p, std::ios::binary).write((const char*)kIconPng, sizeof kIconPng);
}

void registerDataDir() {
#ifdef _WIN32
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\quark", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    regStr(k, L"DataDir", userDataDir().wstring());
    regStr(k, L"InstallDir", appDir().wstring());
    RegCloseKey(k);
#endif
}

// Moves an install from the old location (%LOCALAPPDATA%\quark, before 0.12) to the new one.
void migrateOldInstall() {
#ifdef _WIN32
    fs::path old = knownFolder(CSIDL_LOCAL_APPDATA) / L"quark";
    if (!fs::exists(old / L"game", ec) || fs::exists(appDir() / L"game", ec)) return;
    fs::create_directories(appDir(), ec);
    fs::rename(old / L"game", appDir() / L"game", ec);
    for (const char* f : {"launcher.cfg", "quark.png"}) fs::rename(old / f, appDir() / f, ec);
#endif
}

fs::path selfPath(const char* argv0) {
    fs::path self = fs::absolute(fs::path(argv0), ec);
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH)) self = fs::path(buf);
#else
    if (fs::exists("/proc/self/exe", ec)) self = fs::read_symlink("/proc/self/exe", ec);
#endif
    return self;
}

fs::path installedLauncher() {
#ifdef _WIN32
    return appDir() / "quark_launcher.exe";
#else
    return appDir() / "quark_launcher";
#endif
}

bool acquireSingleInstance(bool waitForPrevious) {
    for (int attempt = 0; attempt < (waitForPrevious ? 50 : 1); attempt++) {
        if (attempt) {
#ifdef _WIN32
            Sleep(100);
#else
            usleep(100000);
#endif
        }
#ifdef _WIN32
        HANDLE m = CreateMutexW(nullptr, TRUE, L"Local\\quark_launcher_single_instance");
        if (m && GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(m); continue; }
        return true; // handle stays open for the life of the process
#else
        static int fd = -1;
        fs::create_directories(appDir(), ec);
        if (fd < 0) fd = open((appDir() / ".launcher.lock").string().c_str(), O_CREAT | O_RDWR, 0644);
        if (fd < 0 || lockf(fd, F_TLOCK, 0) == 0) return true;
#endif
    }
#ifdef _WIN32
    if (HWND w = FindWindowW(nullptr, L"quark launcher")) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
#endif
    return false;
}

bool runningFromOutside(const char* argv0) {
    return !fs::equivalent(selfPath(argv0), installedLauncher(), ec);
}

bool relaunchInstalled(const fs::path& installed, const fs::path& original) {
    if (original.empty()) return relaunch(installed, {"--relaunched"});
#ifdef _WIN32
    std::wstring cmd = L"\"" + installed.wstring() + L"\" --cleanup \"" + original.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(installed.wstring().c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr,
                        installed.parent_path().wstring().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    return procSpawnDetached({installed.string(), "--cleanup", original.string()});
#endif
}

bool relaunch(const fs::path& exe, const std::vector<std::string>& args) {
#ifdef _WIN32
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    for (const std::string& a : args) cmd += L" \"" + fs::u8path(a).wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.wstring().c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr,
                        exe.parent_path().wstring().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    std::vector<std::string> argv = {exe.string()};
    argv.insert(argv.end(), args.begin(), args.end());
    return procSpawnDetached(argv);
#endif
}

void cleanupDownloaded(const fs::path& original) {
    // the original process exits right after starting us: retry for a few seconds
    for (int i = 0; i < 50; i++) {
        if (!fs::exists(original, ec) || fs::remove(original, ec)) return;
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
}

fs::path installSelf(const char* argv0) {
    migrateOldInstall();
    fs::path self = selfPath(argv0);
    fs::create_directories(appDir(), ec);
    writeIcon();
    fs::path dst = installedLauncher();
    if (fs::equivalent(self, dst, ec)) return dst;
    // leftovers from older versions that installed under the download's name ("quark_launcher (1).exe",
    // "quark-launcher-windows.exe", "quark-launcher-linux")
    for (const fs::path& l : selfupdate::launchersIn(appDir()))
        if (l != dst && !fs::equivalent(l, self, ec)) fs::remove(l, ec);
    // an older download never replaces a newer installed launcher (that one is used instead)
    if (fs::exists(dst, ec) && releases::versionCmp(selfupdate::fileVersion(dst), selfupdate::ownVersion()) > 0) return dst;
    std::error_code cec;
    fs::copy_file(self, dst, fs::copy_options::overwrite_existing, cec);
#ifndef _WIN32
    if (!cec) fs::permissions(dst, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec, fs::perm_options::add, ec);
#endif
    return dst; // may be the previous copy if this one couldn't be written (in use)
}

#ifdef _WIN32
const char* desktopLabel() { return "DESKTOP ICON"; }
const char* menuLabel() { return "START MENU"; }
const char* programsLabel() { return "INSTALLED PROGRAMS"; }
bool hasDesktop() { return fs::exists(desktopLink(), ec); }
bool hasMenu() { return fs::exists(menuLink(), ec); }
bool hasPrograms() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}
bool setDesktop(bool on, const fs::path& l) { if (!on) return fs::remove(desktopLink(), ec), true; return makeShortcut(desktopLink(), l); }
bool setMenu(bool on, const fs::path& l) {
    if (!on) { fs::remove_all(menuLink().parent_path(), ec); return true; }
    return makeShortcut(menuLink(), l);
}
bool setPrograms(bool on, const fs::path& l, const std::string& version) {
    if (!on) { RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey); return true; }
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    std::wstring exe = l.wstring();
    regStr(k, L"DisplayName", L"Quark");
    regStr(k, L"DisplayVersion", std::wstring(version.begin(), version.end()));
    regStr(k, L"Publisher", L"floration");
    regStr(k, L"InstallLocation", appDir().wstring());
    regStr(k, L"DisplayIcon", exe);
    regStr(k, L"UninstallString", L"\"" + exe + L"\" --uninstall");
    regStr(k, L"URLInfoAbout", L"https://floration.github.io/quark");
    regDword(k, L"NoModify", 1);
    regDword(k, L"NoRepair", 1);
    uintmax_t size = 0;
    for (auto& e : fs::recursive_directory_iterator(appDir(), ec)) if (e.is_regular_file(ec)) size += e.file_size(ec);
    regDword(k, L"EstimatedSize", (DWORD)(size / 1024));
    RegCloseKey(k);
    return true;
}
void repairShortcuts(const fs::path& l, bool create) {
    if (hasDesktop() ? !pointsAt(desktopLink(), l) : create) setDesktop(true, l);
    if (hasMenu() ? !pointsAt(menuLink(), l) : create) setMenu(true, l);
}
void uninstallAll() {
    fs::path launcher;
    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH)) launcher = fs::path(buf);
    setDesktop(false, launcher);
    setMenu(false, launcher);
    setPrograms(false, launcher, "");
    keepUserData(gameDir());
    for (const fs::path& d : legacyAppDirs()) keepUserData(d / L"game");
    fs::remove_all(gameDir(), ec);
    HKEY k; // the data location stays registered (DataDir); only the install is gone
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\quark", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, L"InstallDir");
        RegCloseKey(k);
    }
    // The running exe can't delete itself: a detached cmd removes the folder once we've exited.
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"" + appDir().wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}
#elif defined(__APPLE__)
const char* desktopLabel() { return "DESKTOP ICON"; }
const char* menuLabel() { return "APPLICATIONS FOLDER"; }
const char* programsLabel() { return ""; }
bool hasDesktop() { return fs::exists(fs::symlink_status(desktopAlias(), ec)); }
bool hasMenu() { return fs::exists(appBundle(), ec); }
bool hasPrograms() { return false; }
bool setMenu(bool on, const fs::path& l) {
    if (!on) { fs::remove_all(appBundle(), ec); return true; }
    fs::path c = appBundle() / "Contents";
    fs::create_directories(c / "MacOS", ec);
    fs::create_directories(c / "Resources", ec);
    fs::path exe = c / "MacOS/quark_launcher", tmp = c / "MacOS/quark_launcher.new";
    std::ofstream(tmp, std::ios::binary | std::ios::trunc) << bundleScript(l);
    chmod(tmp.string().c_str(), 0755);
    fs::rename(tmp, exe, ec); // atomic, also over an old launcher copy that is running
    std::ofstream(c / "Info.plist") << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>"
        "<key>CFBundleName</key><string>Quark</string><key>CFBundleExecutable</key><string>quark_launcher</string>"
        "<key>CFBundleIdentifier</key><string>io.github.floration.quark</string><key>CFBundlePackageType</key><string>APPL</string>"
        "<key>LSMinimumSystemVersion</key><string>10.15</string></dict></plist>\n";
    return !ec;
}
bool setDesktop(bool on, const fs::path& l) {
    fs::remove(desktopAlias(), ec);
    if (!on) return true;
    if (!hasMenu()) setMenu(true, l);
    fs::create_directory_symlink(appBundle(), desktopAlias(), ec);
    return !ec;
}
bool setPrograms(bool, const fs::path&, const std::string&) { return false; }
void repairShortcuts(const fs::path& l, bool create) {
    bool menu = hasMenu();
    if (menu) {
        std::ifstream f(appBundle() / "Contents/MacOS/quark_launcher", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        if (ss.str() != bundleScript(l)) setMenu(true, l);
    } else if (create) setMenu(true, l);
    if (!hasDesktop() && create) setDesktop(true, l);
}
void uninstallAll() {
    setDesktop(false, {});
    setMenu(false, {});
    removeInstallFiles();
    fs::remove(appDir(), ec); // only if nothing is left
}
#else
const char* desktopLabel() { return "DESKTOP ICON"; }
const char* menuLabel() { return "APP MENU"; }
const char* programsLabel() { return ""; }
bool hasDesktop() { return fs::exists(desktopEntry(), ec); }
bool hasMenu() { return fs::exists(menuEntry(), ec); }
bool hasPrograms() { return false; }
bool setDesktop(bool on, const fs::path& l) { if (!on) return fs::remove(desktopEntry(), ec), true; return writeDesktopFile(desktopEntry(), l); }
bool setMenu(bool on, const fs::path& l) { if (!on) return fs::remove(menuEntry(), ec), true; return writeDesktopFile(menuEntry(), l); }
bool setPrograms(bool, const fs::path&, const std::string&) { return false; }
void repairShortcuts(const fs::path& l, bool create) {
    if (hasDesktop() ? !desktopFileRuns(desktopEntry(), l) : create) setDesktop(true, l);
    if (hasMenu() ? !desktopFileRuns(menuEntry(), l) : create) setMenu(true, l);
}
void uninstallAll() {
    setDesktop(false, {});
    setMenu(false, {});
    removeInstallFiles();
    fs::remove(appDir(), ec); // only if nothing is left
}
#endif

} // namespace install
