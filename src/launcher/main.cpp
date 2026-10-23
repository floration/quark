// quark launcher: lists every release of the chosen channel (production / testing), installs the newest (or the
// version the player picked) after checking its signature, keeps itself current, shows the changelog, starts the game.
// Releases from before signing (<= 1.16.x) are listed as UNSIGNED and install only after the player confirms.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../common/version.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "../client/gl.h"
#include "../client/renderer.h"
#include "../client/ui.h"
#include "../common/net.h"
#include "install.h"
#include "releases.h"
#include "selfupdate.h"
#include "../common/proc.h"
#include "../common/sign.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
// Releases of both channels, newest first, every page (releases::fetchAll): production releases stay listed however
// many test builds come after them, and every old version stays installable.
const char* kApi = "https://api.github.com/repos/floration/quark/releases?per_page=100"; // = releases::kPageSize
#if defined(_WIN32)
const char* kPlatform = "windows";
const char* kGameExe = "quark.exe";
#elif defined(__APPLE__)
const char* kPlatform = "macos";
const char* kGameExe = "quark.app";
#else
const char* kPlatform = "linux";
const char* kGameExe = "quark";
#endif

using Progress = std::function<void(uint64_t done, uint64_t total)>;

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    std::wstring w(s.size(), L' ');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], (int)w.size());
    w.resize(n > 0 ? n : 0);
    return w;
}
// WinHTTP: built into every Windows since XP. TLS 1.2 is requested explicitly (Win7 needs KB3140245).
bool httpGet(const std::string& url, std::ostream& out, const Progress& prog, std::string* err) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { *err = "bad url"; return false; }
    HINTERNET s = WinHttpOpen(L"quark-launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { *err = "WinHttpOpen failed"; return false; }
    DWORD protos = 0x00000800 /*TLS1_2*/ | 0x00002000 /*TLS1_3*/;
    if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof protos)) { // pre-1903 Windows rejects the 1.3 bit
        protos = 0x00000800;
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof protos);
    }
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = r && WinHttpSendRequest(r, L"Accept: */*\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(r, nullptr);
    DWORD status = 0, sz = sizeof status;
    if (ok) WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
    if (ok && status != 200) { ok = false; *err = "HTTP " + std::to_string(status); }
    else if (!ok) *err = "connection failed (error " + std::to_string(GetLastError()) + ")";
    if (ok) {
        wchar_t lenBuf[32]; DWORD lb = sizeof lenBuf;
        uint64_t total = 0;
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, lenBuf, &lb, nullptr)) total = _wtoi64(lenBuf);
        uint64_t done = 0;
        std::vector<char> buf(1 << 16);
        for (;;) {
            DWORD got = 0;
            if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &got)) { ok = false; *err = "download interrupted"; break; }
            if (got == 0) break;
            out.write(buf.data(), got);
            done += got;
            if (prog) prog(done, total);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
#else
// macOS and Linux ship curl; use it for TLS instead of bundling a TLS stack. Run without a shell
// (argv array), so nothing in the URL (from the GitHub JSON or QUARK_UPDATE_API) can become a command.
bool httpGet(const std::string& url, std::ostream& out, const Progress& prog, std::string* err) {
    for (char ch : url)
        if ((unsigned char)ch < 32) { *err = "bad url"; return false; }
    uint64_t done = 0;
    int rc = procRun({"curl", "-fsSL", "-A", "quark-launcher", "--proto", "=https", "--url", url}, "",
                     [&](const char* d, size_t n) {
                         out.write(d, n);
                         done += n;
                         if (prog) prog(done, 0);
                         return true;
                     });
    if (rc != 0) { *err = rc < 0 ? "could not run curl" : "download failed (curl exit " + std::to_string(rc) + ")"; return false; }
    return true;
}
#endif

// A qpak entry name is safe when it is a plain relative path inside the install: no root name
// (C:, \\server), no root directory (/ or \), no "..", nothing empty.
bool safeEntryName(const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos || name.find(':') != std::string::npos) return false;
    if (name[0] == '/' || name[0] == '\\') return false;
    if (name.find("..") != std::string::npos) return false; // also catches ..\ on Windows
    fs::path p = fs::u8path(name);
    return p.is_relative() && !p.has_root_name() && !p.has_root_directory();
}

// .qpak: "QPAK1\n", then per file: u32 nameLen, name, u32 mode, u64 size, bytes. Little endian.
// Everything is extracted into `stage` (a fresh directory); the caller swaps it into place on success,
// so a failed or interrupted install never leaves a half-updated game.
static bool extractPakImpl(const std::string& pak, const fs::path& stage, std::string* err);
bool extractPak(const std::string& pak, const fs::path& stage, std::string* err) {
    try { return extractPakImpl(pak, stage, err); }
    catch (const std::exception& e) { *err = std::string("corrupt archive (") + e.what() + ")"; return false; }   // (ENG-2: bad_alloc, a bad path)
}

static bool extractPakImpl(const std::string& pak, const fs::path& stage, std::string* err) {
    std::error_code ec;
    uint64_t remaining = fs::file_size(pak, ec);
    if (ec || remaining < 6) { *err = "corrupt download"; return false; }
    std::ifstream f(pak, std::ios::binary);
    char magic[6];
    f.read(magic, 6);
    if (!f || memcmp(magic, "QPAK1\n", 6) != 0) { *err = "corrupt download"; return false; }
    remaining -= 6;
    auto rd = [&](void* p, size_t n) { f.read((char*)p, n); if (f) remaining -= n; return (bool)f; };
    std::vector<char> buf;
    for (;;) {
        uint32_t nl, mode;
        uint64_t size;
        if (!rd(&nl, 4)) break;
        if (nl == 0 || nl > 4096) { *err = "corrupt archive"; return false; }
        std::string name(nl, 0);
        if (!rd(&name[0], nl) || !rd(&mode, 4) || !rd(&size, 8)) { *err = "corrupt archive"; return false; }
        if (!safeEntryName(name)) { *err = "unsafe path in archive"; return false; }
        if (size > remaining) { *err = "truncated archive"; return false; } // never trust size for the allocation
        fs::path out = stage / fs::u8path(name);
        fs::create_directories(out.parent_path(), ec);
        buf.resize((size_t)size);
        if (size && !rd(buf.data(), (size_t)size)) { *err = "truncated archive"; return false; }
        std::ofstream o(out, std::ios::binary | std::ios::trunc);
        if (!o) { *err = "cannot write " + out.string(); return false; }
        o.write(buf.data(), (std::streamsize)size);
        o.close();
        if (!o) { *err = "cannot write " + out.string() + " (disk full?)"; return false; }
#ifndef _WIN32
        chmod(out.string().c_str(), mode & 0777);
#endif
    }
    return true;
}

// Swaps a fully extracted `stage` into `dir`: files of the old install the package doesn't carry (the
// player's quark.cfg, demos, screenshots...) are copied into the stage first, quark.cfg always wins
// over a packaged one. Then dir -> dir.old, stage -> dir. On failure the old install is put back.
bool swapInstall(const fs::path& stage, const fs::path& dir, std::string* err) {
    std::error_code ec;
    fs::path old = dir.parent_path() / (dir.filename().u8string() + ".old");
    fs::remove_all(old, ec);
    if (fs::exists(dir, ec)) {
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            fs::path rel = fs::relative(it->path(), dir, e2);
            if (e2) continue;
            fs::path dst = stage / rel;
            if (it->is_directory(e2)) continue;
            if (fs::exists(dst, e2) && rel.filename() != "quark.cfg") continue;
            fs::create_directories(dst.parent_path(), e2);
            fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, e2);
        }
        ec.clear();
        fs::rename(dir, old, ec);
        if (ec) { *err = "cannot replace " + dir.string() + " (is the game running?)"; return false; }
    }
    fs::rename(stage, dir, ec);
    if (ec) {
        std::error_code e2;
        fs::rename(old, dir, e2);
        *err = "cannot move the new version into place";
        return false;
    }
    fs::remove_all(old, ec);
    return true;
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool launchGame(const fs::path& dir) {
#ifdef _WIN32
    std::string exe = (dir / kGameExe).string();
    std::string cmd = "\"" + exe + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.string().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#elif defined(__APPLE__)
    return procSpawnDetached({"open", (dir / kGameExe).string()});
#else
    return procSpawnDetached({"./" + std::string(kGameExe)}, dir.string());
#endif
}

using releases::Release;
using releases::Channel;

struct State {
    std::mutex mu;
    std::string status = "checking for updates...";
    std::vector<Release> all;          // every release fetched (both channels)
    std::vector<Release> releases;     // the channel's installable ones, newest first (unsigned ones included)
    releases::Config cfg;              // channel + pinned version (launcher.cfg)
    std::string installed;
    int selected = 0;                  // index into releases; latest = releases::latestIndex
    bool fetched = false;
    bool complete = false;             // every page of the release list was read
    bool awaitingConfirm = false;      // the pinned version is unsigned and not installed: the player has to confirm
    std::atomic<float> progress{-1};
    std::atomic<bool> busy{true}, ready{false};
    std::atomic<bool> relaunch{false}; // a newer launcher was swapped in: restart into it when idle
    bool noSelfUpdate = false;         // this process is that restart: never swap again (no loops)
};

fs::path launcherCfg() { return install::appDir() / "launcher.cfg"; }
void saveCfg(const releases::Config& c) { std::ofstream(launcherCfg(), std::ios::trunc) << releases::formatConfig(c); }

// Under st->mu: the visible list for the current channel and the selection the config asks for.
void applyChannel(State* st) {
    st->releases = releases::forChannel(st->all, st->cfg.channel);
    st->selected = releases::selectIndex(st->releases, st->cfg);
}

// Every release (both channels) from the GitHub releases list: all pages.
bool fetchReleases(State* st, std::string* err) {
    const char* api = getenv("QUARK_UPDATE_API"); // override for testing: one URL, one page
    std::vector<Release> all;
    bool complete = false;
    auto getPage = [&](int page, std::string* json, std::string* e) {
        if (api && page > 1) { *json = "[]"; return true; }
        std::ostringstream out;
        std::string url = api ? std::string(api) : std::string(kApi) + "&page=" + std::to_string(page);
        if (!httpGet(url, out, nullptr, e)) return false;
        *json = out.str();
        return true;
    };
    if (!releases::fetchAll(getPage, kPlatform, &all, &complete, err)) return false;
    std::lock_guard<std::mutex> l(st->mu);
    st->all = all;
    st->fetched = true;
    st->complete = complete;
    applyChannel(st);
    if (st->releases.empty()) {
        *err = std::string("no ") + kPlatform + " builds in the " + releases::channelName(st->cfg.channel) + " channel yet";
        return false;
    }
    return true;
}

// The package just installed (or already installed) carries a launcher: if it's newer than this one, swap it in
// over the installed launcher and ask the UI to restart into it.
void checkSelfUpdate(State* st, const fs::path& dir) {
    if (st->noSelfUpdate) return;
    fs::path packaged = dir / selfupdate::exeName(), target = install::installedLauncher();
    std::string pv = selfupdate::fileVersion(packaged);
    if (!selfupdate::isNewer(pv, selfupdate::ownVersion())) return;
    if (!selfupdate::isNewer(pv, selfupdate::fileVersion(target))) { st->relaunch = true; return; } // already swapped (by the game)
    std::string err;
    if (selfupdate::replaceFile(packaged, target, &err) && selfupdate::fileVersion(target) == pv) st->relaunch = true;
    else { std::lock_guard<std::mutex> l(st->mu); st->status += " (launcher update failed: " + err + ")"; }
}

// `allowUnsigned`: the player picked a release from before signing and confirmed the warning. Only then is a
// package without a .sig installed; a signed one is always verified.
void installRelease(std::shared_ptr<State> st, fs::path dir, Release r, bool allowUnsigned) {
    auto setStatus = [&](const std::string& s) { std::lock_guard<std::mutex> l(st->mu); st->status = s; };
    std::string err;
    setStatus("downloading version " + r.version + "...");
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path tmp = dir.parent_path() / "update.qpak";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        bool ok = httpGet(r.url, out, [&](uint64_t d, uint64_t t) { st->progress = t ? (float)d / t : -1.0f; }, &err);
        if (!ok) {
            out.close();
            fs::remove(tmp, ec);
            setStatus("download failed: " + err);
            st->busy = false;
            return;
        }
    }
    // Only install what the release key signed: a tampered or swapped package is refused before anything is
    // extracted. A release from before signing (no .sig) only when the player confirmed installing it unsigned.
    setStatus("verifying version " + r.version + "...");
    st->progress = -1;
    if (r.sigUrl.empty() && allowUnsigned) {
        setStatus("installing UNSIGNED version " + r.version + " (from before signed updates, not verified)...");
    } else {
        std::ostringstream sig;
        if (r.sigUrl.empty()) err = "it is unsigned (from before signed updates) - pick it and confirm to install it anyway";
        else if (!httpGet(r.sigUrl, sig, nullptr, &err)) err = "could not download its signature (" + err + ")";
        else if (!sign::verifyFile(tmp.string(), sig.str(), sign::kReleasePublicKey, &err)) err = "its signature does not verify";
        else err.clear();
        if (!err.empty()) {
            fs::remove(tmp, ec);
            setStatus("refused version " + r.version + ": " + err + " - the download may have been tampered with");
            st->busy = false;
            return;
        }
    }
    setStatus("installing version " + r.version + "...");
    st->progress = -1;
    fs::path stage = dir.parent_path() / (dir.filename().u8string() + ".new");
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    if (!extractPak(tmp.string(), stage, &err) || !swapInstall(stage, dir, &err)) {
        fs::remove_all(stage, ec);
        setStatus("install failed: " + err);
    } else {
        std::ofstream(dir / "version.txt") << r.version << "\n";
        install::setPrograms(true, install::appDir() / fs::path(
#ifdef _WIN32
            "quark_launcher.exe"
#else
            "quark_launcher"
#endif
            ), r.version); // always listed in "installed programs" (uninstall from there), version kept current
        {
            std::lock_guard<std::mutex> l(st->mu);
            st->installed = r.version;
            st->status = "installed version " + r.version + (r.isSigned() ? "" : " (unsigned)");
            st->ready = true;
            st->awaitingConfirm = false;
        }
        checkSelfUpdate(st.get(), dir);
    }
    fs::remove(tmp, ec);
    st->busy = false;
}

// Startup: get the channel's list, then install what the config asks for (the newest, or the pinned version), but
// never an older version than the installed one unless the player picked it.
void updater(std::shared_ptr<State> st, fs::path dir) {
    std::string installed = readFile(dir / "version.txt");
    {
        std::lock_guard<std::mutex> l(st->mu);
        st->installed = installed;
        st->ready = !installed.empty();
    }
    std::string err;
    if (!fetchReleases(st.get(), &err)) {
        {
            std::lock_guard<std::mutex> l(st->mu);
            if (!st->fetched) st->status = installed.empty() ? "offline - " + err : "offline - playing installed version " + installed;
            else st->status = installed.empty() ? err : err + " - installed: " + installed;
        }
        if (!installed.empty()) checkSelfUpdate(st.get(), dir);
        st->busy = false;
        return;
    }
    Release target;
    bool doInstall = false;
    {
        std::lock_guard<std::mutex> l(st->mu);
        releases::Plan plan = releases::startupPlan(st->releases, st->cfg, installed, st->complete);
        st->selected = plan.index;
        target = st->releases[plan.index];
        std::string ch = releases::channelName(st->cfg.channel);
        std::string tag = target.isSigned() ? "" : ", unsigned";
        if (plan.confirm) {
            st->awaitingConfirm = true;
            st->status = "pinned version " + target.version + " is UNSIGNED (from before signed updates) - press INSTALL to confirm";
        } else if (installed == target.version) {
            st->status = plan.pinned ? "version " + installed + " (pinned" + tag + ")" : "up to date - " + ch + " " + installed;
        } else if (plan.install) {
            doInstall = true;
        } else if (st->cfg.pinned() && !plan.pinned && !st->complete) {
            st->status = "pinned version " + st->cfg.pin + " not found (release list incomplete) - installed: " + (installed.empty() ? "none" : installed);
        } else if (!target.isSigned()) {
            st->status = std::string("no signed ") + kPlatform + " builds in the " + ch + " channel yet - older unsigned versions are listed";
        } else {
            st->status = "installed " + installed + " is newer than the latest " + ch + " release " + target.version + " - pick a version to change";
        }
    }
    if (doInstall) { installRelease(std::move(st), dir, target, false); return; }
    checkSelfUpdate(st.get(), dir);
    st->busy = false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--uninstall") {
#ifdef _WIN32
        std::wstring q = L"Remove quark, its shortcuts and game files?\n\nYour settings, ranked identity, demos and maps stay in\n" +
                         install::userDataDir().wstring();
        if (MessageBoxW(nullptr, q.c_str(), L"Uninstall quark", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
#endif
        install::uninstallAll();
        return 0;
    }
    fs::path dir = install::gameDir();
    std::thread cleanup;
    if (argc > 2 && std::string(argv[1]) == "--cleanup") cleanup = std::thread(install::cleanupDownloaded, fs::u8path(argv[2]));
    // Started from Downloads (or anywhere else): install into the program folder, then hand over
    // to that copy, which deletes the downloaded file.
    // One launcher at a time: a second start (double click, or an older build relaunching itself)
    // just brings the running one to the front.
    bool handedOver = argc > 2 && std::string(argv[1]) == "--cleanup";
    bool relaunched = argc > 1 && std::string(argv[1]) == "--relaunched"; // restarted into a swapped-in launcher
    if (!install::acquireSingleInstance(handedOver || relaunched)) return 0;
    // Started from Downloads (or anywhere else): install into the program folder, then hand over
    // to that copy, which deletes the downloaded file. Never hands over to itself, and a process
    // that was itself started by a hand-over never hands over again (no relaunch loops).
    fs::path self = install::selfPath(argv[0]);
    selfupdate::removeLeftovers(self); // the .old a previous self-update parked
    fs::path launcherPath = install::installSelf(argv[0]);
    // a desktop icon and a start menu / app menu entry, always (recreated if missing, repointed if they run an old copy)
    install::repairShortcuts(launcherPath, true);
    install::registerDataDir();
    std::error_code eqEc;
    if (!handedOver && !relaunched && install::runningFromOutside(argv[0]) && fs::exists(launcherPath) && !fs::equivalent(launcherPath, self, eqEc)) {
        // macOS: an old Quark.app held a launcher copy; it is a script now, never delete what's in the bundle
        bool inBundle = self.string().find(".app/Contents/MacOS/") != std::string::npos;
        if (install::relaunchInstalled(launcherPath, inBundle ? fs::path() : self)) return 0; // it waits for us to exit, then takes the lock
    }
    { // always listed in "installed programs" (Windows Apps & features); no option needed
        std::string ver = readFile(dir / "version.txt");
        while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r')) ver.pop_back();
        install::setPrograms(true, launcherPath, ver.empty() ? std::string(QUARK_VERSION) : ver);
    }
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(760, 480, "quark launcher", nullptr, nullptr);
    if (!win) return 1;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if (qglLoad((QGLGetProc)glfwGetProcAddress)) return 1;
    Renderer r;
    std::string err;
    if (!r.init(256, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }

    // Shared with the worker thread: if the window closes mid-download the detached worker holds its own
    // reference, so it never touches freed memory (and an unfinished install only leaves a stage dir).
    auto stp = std::make_shared<State>();
    State& st = *stp;
    st.cfg = releases::parseConfig(readFile(launcherCfg()));
    // only the installed launcher swaps itself, and never twice in a row
    st.noSelfUpdate = relaunched || !fs::equivalent(self, install::installedLauncher(), eqEc);
    std::thread worker(updater, stp, dir);
    Ui ui;
    UiInput in;
    static UiInput* g_in = &in;
    static double g_scroll = 0; // wheel steps since the last frame (version list)
    glfwSetScrollCallback(win, [](GLFWwindow*, double, double dy) { g_scroll += dy; });
    float listScroll = 0;       // version list offset in rows
    glfwSetMouseButtonCallback(win, [](GLFWwindow*, int b, int a, int) {
        if (b == GLFW_MOUSE_BUTTON_LEFT) { g_in->down = a == GLFW_PRESS; if (a == GLFW_PRESS) g_in->pressed = true; }
    });
    bool quit = false;
    double uninstallArmed = 0;
    std::string unsignedArmed;      // an unsigned version whose INSTALL was clicked once: the next click installs it
    double unsignedArmedUntil = 0;
    const Color kWarn = {1.0f, 0.62f, 0.2f, 1};
    bool autoPlay = false;
    for (int i = 1; i < argc; i++) autoPlay = autoPlay || std::string(argv[i]) == "--play";
    while (!glfwWindowShouldClose(win) && !quit) {
        glfwPollEvents();
        int fw, fh, ww, wh;
        double mx, my;
        glfwGetFramebufferSize(win, &fw, &fh);
        glfwGetWindowSize(win, &ww, &wh);
        glfwGetCursorPos(win, &mx, &my);
        in.mx = (float)(mx * fw / std::max(1, ww));
        in.my = (float)(my * fh / std::max(1, wh));
        r.resize(fw, fh);
        float s = fh / 480.0f;
        ui.begin(&r, in, s * 0.7f);
        r.rect(0, 0, (float)fw, (float)fh, {0.04f, 0.045f, 0.06f, 1});
        ui.label(24 * s, 20 * s, "QUARK", ui.accent, 2.5f);
        // a newer launcher was swapped in: restart into it (keeping a pending --play)
        if (st.relaunch && !st.busy) {
            std::vector<std::string> args = {"--relaunched"};
            if (autoPlay) args.push_back("--play");
            if (install::relaunch(install::installedLauncher(), args)) { quit = true; continue; }
            st.relaunch = false;
        }
        std::string status, latest, log, installed;
        std::vector<std::string> versionNames, versions;
        std::vector<bool> unsignedRow;
        int latestRow = -1;
        Release sel;
        Channel channel;
        {
            std::lock_guard<std::mutex> l(st.mu);
            channel = st.cfg.channel;
            status = st.status;
            installed = st.installed;
            if (st.awaitingConfirm) autoPlay = false; // the pinned version waits for the player: don't start the old one
            latestRow = releases::latestIndex(st.releases);
            for (size_t i = 0; i < st.releases.size(); i++) {
                versions.push_back(st.releases[i].version);
                unsignedRow.push_back(!st.releases[i].isSigned());
                versionNames.push_back((int)i == latestRow ? "LATEST (" + st.releases[i].version + ")" : st.releases[i].version);
            }
            if (!st.releases.empty()) {
                sel = st.releases[std::min<size_t>(st.selected, st.releases.size() - 1)];
                latest = sel.version;
                log = releases::asciiText(sel.body);
            }
        }
        ui.label(24 * s, 70 * s, status, {0.8f, 0.82f, 0.85f, 1});
        float p = st.progress;
        if (st.busy && p >= 0) {
            r.rect(24 * s, 92 * s, (fw - 48 * s), 6 * s, {0.15f, 0.15f, 0.2f, 1});
            r.rect(24 * s, 92 * s, (fw - 48 * s) * p, 6 * s, ui.accent);
        }
        // version list (right): scroll with the wheel; LATEST follows updates, older ones stay pinned
        float listW = 190 * s, listX = fw - 24 * s - listW, listY = 110 * s, listH = 222 * s, rowH = 24 * s;
        ui.panel(listX, listY, listW, listH, 0.6f);
        ui.label(listX + 10 * s, listY + 8 * s, channel == Channel::Testing ? "TESTING VERSIONS" : "VERSIONS", ui.accent);
        {
            float top = listY + 30 * s, viewH = listH - 36 * s;
            int n = (int)versionNames.size();
            float maxScroll = std::max(0.0f, n - viewH / rowH);
            bool over = in.mx >= listX && in.mx < listX + listW && in.my >= listY && in.my < listY + listH;
            if (over) listScroll -= (float)g_scroll * 2;
            listScroll = clampf(listScroll, 0, maxScroll);
            int first = (int)listScroll;
            for (int i = first; i < n; i++) {
                float ry = top + (i - listScroll) * rowH;
                if (ry + rowH > top + viewH + 0.5f) break;
                if (ry < top - 0.5f) continue;
                bool cur = i == st.selected;
                if (cur) r.rect(listX + 4 * s, ry, listW - 16 * s, rowH - 2 * s, {ui.accent.r * 0.35f, ui.accent.g * 0.35f, ui.accent.b * 0.35f, 1});
                bool hov = in.mx >= listX + 4 * s && in.mx < listX + listW - 12 * s && in.my >= ry && in.my < ry + rowH - 2 * s;
                if (hov && !cur) r.rect(listX + 4 * s, ry, listW - 16 * s, rowH - 2 * s, {0.16f, 0.18f, 0.22f, 1});
                std::string label = versionNames[i];
                const std::string& ver = versions[i];
                if (!installed.empty() && ver == installed) label += "  *";
                ui.label(listX + 10 * s, ry + 4 * s, label, cur ? Color{1, 1, 1, 1} : Color{0.8f, 0.82f, 0.85f, 1}, 0.85f);
                if (unsignedRow[i]) { // from before signed updates: marked, installed only after a confirmation
                    float ts = std::max(1.0f, std::round(ui.textScale() * 0.6f));
                    r.text(listX + listW - 18 * s - r.textWidth("UNSIGNED", ts), ry + 6 * s, ts, "UNSIGNED", kWarn);
                }
                if (hov && in.pressed) {
                    std::lock_guard<std::mutex> l(st.mu);
                    st.selected = i;
                    st.cfg.pin = i == latestRow ? std::string("latest") : versions[i];
                    st.awaitingConfirm = false;
                    saveCfg(st.cfg);
                }
            }
            if (maxScroll > 0) { // scrollbar
                float barH = viewH * (viewH / rowH) / n;
                float barY = top + (viewH - barH) * (listScroll / maxScroll);
                r.rect(listX + listW - 8 * s, top, 4 * s, viewH, {0.15f, 0.16f, 0.2f, 1});
                r.rect(listX + listW - 8 * s, barY, 4 * s, barH, ui.accent);
            }
            g_scroll = 0;
        }
        // changelog
        ui.panel(24 * s, 110 * s, fw - 48 * s - listW - 8 * s, 222 * s, 0.6f);
        float y = 120 * s;
        ui.label(36 * s, y, latest.empty() ? "CHANGELOG" : "VERSION " + latest + (latest == installed ? " (INSTALLED)" : ""), ui.accent);
        y += 22 * s;
        float ts = std::max(1.0f, std::round(ui.textScale() * 0.75f));
        float maxW = fw - 80 * s - listW - 8 * s;
        // one paragraph, wrapped by width (the UI font is proportional): break at the last space that fits
        auto wrapped = [&](std::string line, const Color& c) {
            while (r.textWidth(line, ts) > maxW && y < 322 * s) {
                size_t cut = std::max<size_t>(1, r.textFit(line, ts, maxW));
                size_t sp = line.rfind(' ', cut);
                if (sp != std::string::npos && sp > 0) cut = sp;
                r.text(36 * s, y, ts, line.substr(0, cut), c);
                line = line.substr(cut);
                while (!line.empty() && line[0] == ' ') line.erase(0, 1);
                y += 10 * ts;
            }
            r.text(36 * s, y, ts, line, c);
            y += 10 * ts;
        };
        if (!sel.version.empty() && !sel.isSigned()) { // the warning stays on the page for as long as it's selected
            wrapped("UNSIGNED: from before signed updates; only install if you trust it.", kWarn);
            wrapped("Its package can't be verified. Old versions may lack the current launcher and use older protocols.", kWarn);
            y += 4 * ts;
        }
        std::istringstream ls(log);
        std::string line;
        while (std::getline(ls, line) && y < 322 * s) {
            while (!line.empty() && (line[0] == '#' || line[0] == ' ')) line.erase(0, 1);
            wrapped(line, {0.85f, 0.85f, 0.88f, 1});
        }
        // the desktop icon and start menu entry are made automatically (at startup). Here: the release channel
        // (switches the version list; installing from it is the player's click) and UNINSTALL (never the player's data)
        {
            float bw = (fw - 56 * s) * 0.5f, bx = 24 * s, by = 344 * s, bh = 40 * s;
            bool armed = timeNow() < uninstallArmed;
            if (ui.button(bx + bw + 8 * s, by, bw, bh, channel == Channel::Testing ? "CHANNEL: TESTING" : "CHANNEL: PRODUCTION", !st.busy)) {
                std::lock_guard<std::mutex> l(st.mu);
                st.cfg.channel = channel == Channel::Testing ? Channel::Production : Channel::Testing;
                st.cfg.pin = "latest";
                st.awaitingConfirm = false;
                saveCfg(st.cfg);
                applyChannel(&st);
                listScroll = 0;
                std::string ch = releases::channelName(st.cfg.channel);
                int li = releases::latestIndex(st.releases);
                if (!st.fetched) st.status = ch + " channel - offline";
                else if (st.releases.empty()) st.status = "no " + ch + " builds published yet";
                else if (li < 0) st.status = "no signed " + ch + " builds yet - older unsigned versions are listed";
                else st.status = ch + " channel - latest " + st.releases[li].version + (st.installed.empty() ? "" : ", installed " + st.installed);
            }
            if (ui.button(bx, by, bw, bh, armed ? "CLICK TO CONFIRM" : "UNINSTALL", !st.busy)) {
                if (armed) {
                    install::uninstallAll();
                    quit = true;
                } else uninstallArmed = timeNow() + 3.0;
            }
        }
        bool needInstall = !sel.version.empty() && sel.version != installed;
        bool canPlay = st.ready && !st.busy;
        bool offerInstall = needInstall && !st.busy;
        // installing is always the player's click (or startup's newer-only rule); the installed version stays playable.
        // An unsigned (pre-signing) version takes two clicks: the first shows the warning, the second installs.
        bool armedNow = !sel.isSigned() && unsignedArmed == sel.version && timeNow() < unsignedArmedUntil;
        std::string installLabel = sel.isSigned() ? "INSTALL " + sel.version : armedNow ? "CONFIRM: INSTALL UNSIGNED" : "INSTALL " + sel.version + " (UNSIGNED)";
        if (offerInstall && ui.button(fw - (canPlay ? 432 : 224) * s, 414 * s, 200 * s, 48 * s, installLabel)) {
            if (!sel.isSigned() && !armedNow) {
                unsignedArmed = sel.version;
                unsignedArmedUntil = timeNow() + 10.0;
                std::lock_guard<std::mutex> l(st.mu);
                st.status = "UNSIGNED " + sel.version + ": from before signed updates; only install if you trust it - click again to confirm";
            } else {
                unsignedArmed.clear();
                st.busy = true;
                if (worker.joinable()) worker.join();
                worker = std::thread(installRelease, stp, dir, sel, !sel.isSigned());
            }
        } else if ((!offerInstall || canPlay) &&
                   (ui.button(fw - 224 * s, 414 * s, 200 * s, 48 * s, st.busy ? "WORKING..." : "PLAY", canPlay) || (autoPlay && canPlay))) {
            if (launchGame(dir)) quit = true;
            else { std::lock_guard<std::mutex> l(st.mu); st.status = "could not start the game in " + dir.string(); autoPlay = false; }
        }
        if (ui.button(24 * s, 414 * s, 140 * s, 48 * s, "QUIT")) quit = true;
        (void)installed;
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        r.flush2D();
        glfwSwapBuffers(win);
        in.pressed = false;
    }
    if (st.busy) worker.detach(); // don't hang the window on a slow download
    else if (worker.joinable()) worker.join();
    if (cleanup.joinable()) cleanup.join();
    glfwTerminate();
    return 0;
}
