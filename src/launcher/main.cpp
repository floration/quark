// quark launcher: lists every release of the chosen channel (production / testing), installs the newest (or the
// version the player picked) after checking its signature, keeps itself current, shows the changelog, starts the game.
// Every release from 1.17.0 on is signed and verified (packages, manifests, the launcher itself); releases from before
// signing (<= 1.16.x) stay listed and install like any other version the player picks (owner decision B6).
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
#include "gamefiles.h"
#include "install.h"
#include "qpak.h"
#include "releases.h"
#include "selfupdate.h"
#include "../common/proc.h"
#include "../common/sha256.h"
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

// Package reading and extraction: qpak.cpp (shared with the fuzzer, tests/fuzz). Swapping it in, deleting only
// retired game files (owner decision C13): gamefiles.cpp.

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// Starts the game: 0, or why not (Windows: the CreateProcess error, e.g. 225 ERROR_VIRUS_INFECTED when the antivirus
// refuses to run it; elsewhere -1). Wide paths on Windows: a user name outside the ANSI code page still works.
const int kErrVirusInfected = 225, kErrVirusDeleted = 226;
int launchGame(const fs::path& dir) {
#ifdef _WIN32
    fs::path exe = dir / kGameExe;
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.wstring().c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.wstring().c_str(), &si, &pi))
        return (int)GetLastError();
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
#elif defined(__APPLE__)
    return procSpawnDetached({"open", (dir / kGameExe).string()}) ? 0 : -1;
#else
    return procSpawnDetached({"./" + std::string(kGameExe)}, dir.string()) ? 0 : -1;
#endif
}

using releases::Release;
using releases::Channel;

// A problem the player has to act on, shown in place of the changelog with its buttons: a game file an antivirus took
// (or blocked) during an install (RETRY) or from the installed game (REPAIR). Never acted on without a click.
struct Notice {
    enum Action { None, Retry, Repair };
    std::string title, body;  // body: paragraphs separated by '\n'
    Action action = None;
    Release release;          // what RETRY installs; REPAIR: the version to install again (release.version)
    bool blocksPlay = false;  // the installed game can't start (its executable is missing / changed / blocked)
};

std::string upper(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}
std::string fileName(const std::string& rel) { return fs::u8path(rel).filename().u8string(); }

// The installed game's executable is missing, changed or blocked: why that most likely happened, how to get it back
// and keep it, and REPAIR.
Notice exeNotice(const gamefiles::Issue& exe, const std::string& version, const fs::path& dir) {
    Notice n;
    std::string file = fileName(exe.path), v = version.empty() ? std::string() : " of version " + version;
    switch (exe.problem) {
    case gamefiles::Problem::Missing:
        n.title = upper(file) + " IS MISSING";
        n.body = "The game can't start: " + file + v + " is gone from the game folder.";
        break;
    case gamefiles::Problem::Changed:
        n.title = upper(file) + " WAS CHANGED";
        n.body = "The game can't start: " + file + v + " isn't the file that was installed (something changed or replaced it).";
        break;
    default:
        n.title = upper(file) + " IS BLOCKED";
        n.body = "The game can't start: " + file + v + " can't be opened (Windows refuses to run a file its antivirus flagged).";
        break;
    }
    n.body += "\n" + gamefiles::antivirusHelp(file, dir.parent_path());
    n.body += "\nOr press REPAIR to download" + (version.empty() ? std::string(" the game") : " version " + version) +
              " again, check it and put it back (your settings and files stay).";
    n.action = Notice::Repair;
    n.release.version = version;
    n.blocksPlay = true;
    return n;
}

// An install stopped because a file of the new version vanished (or changed, or was blocked) in the stage.
Notice stageNotice(const std::string& what, const Release& r, const std::string& installed, const fs::path& dir,
                   const std::string& file) {
    Notice n;
    n.title = "VERSION " + upper(r.version) + " WAS NOT INSTALLED";
    n.body = "The update stopped: " + what + ". " +
             (installed.empty() ? std::string("Nothing was installed.") : "Nothing was changed: version " + installed + " is still installed and playable.");
    n.body += "\n" + gamefiles::antivirusHelp(file, dir.parent_path()) + "\nThen press RETRY.";
    n.action = Notice::Retry;
    n.release = r;
    return n;
}

struct State {
    std::mutex mu;
    std::string status = "checking for updates...";
    std::vector<Release> all;          // every release fetched (both channels)
    std::vector<Release> releases;     // the channel's installable ones, newest first (<= 1.16.x ones included)
    releases::Config cfg;              // channel + pinned version (launcher.cfg)
    std::string installed;
    int selected = 0;                  // index into releases; latest = releases::latestIndex
    bool fetched = false;
    bool complete = false;             // every page of the release list was read
    bool awaitingInstall = false;      // the pinned version isn't installed and only the player's INSTALL installs it
    std::atomic<float> progress{-1};
    std::atomic<bool> busy{true}, ready{false};
    std::atomic<bool> relaunch{false}; // a newer launcher was swapped in: restart into it when idle
    bool noSelfUpdate = false;         // this process is that restart: never swap again (no loops)
    Notice notice;                     // a file problem to show (title empty: none)
};

// Checks the installed game (existence and size; full: sha256 too) and shows what's wrong. The executable missing,
// changed or blocked: a notice that blocks PLAY until REPAIR (or the player restoring it). Other game files: a notice
// with REPAIR after a full check, a hint in the status line otherwise. True when the game can start.
bool checkGame(State* st, const fs::path& dir, bool full) {
    gamefiles::Health h = gamefiles::checkInstall(dir, gamefiles::kGameExe, full);
    std::lock_guard<std::mutex> l(st->mu);
    if (h.exeBad) {
        st->notice = exeNotice(h.exe, st->installed, dir);
        st->status = gamefiles::describe(h.exe) + " - see below";
        return false;
    }
    if (st->notice.action == Notice::Repair) st->notice = Notice(); // (it's back)
    if (h.issues.empty()) {
        if (full) st->status = "checked " + std::to_string(h.checked) + " game files of version " + st->installed + ": all fine";
        return true;
    }
    std::string first = gamefiles::describe(h.issues[0]);
    if (!full) {
        st->status += " - " + std::to_string(h.issues.size()) + " game file(s) missing or changed (" + first + "): CHECK FILES";
        return true;
    }
    Notice n;
    n.title = std::to_string(h.issues.size()) + " GAME FILE(S) MISSING OR CHANGED";
    for (size_t i = 0; i < h.issues.size() && i < 6; i++) n.body += (i ? ", " : "") + gamefiles::describe(h.issues[i]);
    if (h.issues.size() > 6) n.body += ", and " + std::to_string(h.issues.size() - 6) + " more";
    n.body += ".\nThe game can still start, but may miss maps or sounds. If you didn't change them yourself, an antivirus "
              "may have removed them.\nPress REPAIR to download version " + st->installed + " again and put them back "
              "(your settings and files stay).";
    n.action = Notice::Repair;
    n.release.version = st->installed;
    st->notice = n;
    st->status = "checked " + std::to_string(h.checked) + " game files: " + first;
    return true;
}

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
    std::string err;
    selfupdate::SigCheck check; // only a launcher the release key signed replaces this one (B6)
    check.pubHex = sign::kReleasePublicKey;
    if (!selfupdate::launcherSigned(packaged, &check.sigHex, &err)) {
        std::lock_guard<std::mutex> l(st->mu);
        st->status += " (launcher update skipped: " + err + ")";
        return;
    }
    if (!selfupdate::isNewer(pv, selfupdate::fileVersion(target))) { st->relaunch = true; return; } // already swapped (by the game)
    if (selfupdate::replaceFile(packaged, target, &err, nullptr, &check) && selfupdate::fileVersion(target) == pv) st->relaunch = true;
    else { std::lock_guard<std::mutex> l(st->mu); st->status += " (launcher update failed: " + err + ")"; }
}

// The package's .sig only proves the release key signed it, not which release it is: the signed manifest
// (quark-release.txt) must name this release's tag, version and channel and this package's sha256, or a validly
// signed package (an old or testing build) could be passed off as another release. Empty: fine; else why not.
std::string checkReleaseManifest(const Release& r, const fs::path& package) {
    if (r.manifestUrl.empty()) return "it has no signed release manifest";
    std::ostringstream text, sig;
    std::string err;
    if (!httpGet(r.manifestUrl, text, nullptr, &err) || !httpGet(r.manifestSigUrl, sig, nullptr, &err))
        return "could not download its release manifest (" + err + ")";
    std::ifstream in(package, std::ios::binary);
    std::string sha = in ? sha256StreamHex(in) : "";
    if (sha.empty()) return "could not read the download";
    return releases::verifyManifest(text.str(), sig.str(), sign::kReleasePublicKey, r, sha);
}

// A signed release is always verified (its .sig and its signed manifest). A release from before signing (<= 1.16.x,
// no .sig) installs as it comes from the GitHub release over HTTPS; one claiming 1.17.0 or later without a .sig is
// never listed (releases::forChannel) and refused here too.
void installRelease(std::shared_ptr<State> st, fs::path dir, Release r) {
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
    // extracted. A release from before signing (<= 1.16.x) has nothing to check.
    setStatus("verifying version " + r.version + "...");
    st->progress = -1;
    if (!releases::needsSignature(r)) {
        // (from before signing: installed as published)
    } else {
        std::ostringstream sig;
        if (r.sigUrl.empty()) err = "it is not signed";
        else if (!httpGet(r.sigUrl, sig, nullptr, &err)) err = "could not download its signature (" + err + ")";
        else if (!sign::verifyFile(tmp.string(), sig.str(), sign::kReleasePublicKey, &err)) err = "its signature does not verify";
        else err = checkReleaseManifest(r, tmp);
        if (!err.empty()) {
            fs::remove(tmp, ec);
            setStatus("refused version " + r.version + ": " + err + " - the download may have been tampered with");
            st->busy = false;
            return;
        }
    }
    setStatus("installing version " + r.version + "...");
    st->progress = -1;
    // extracted into a stage, every file checked against the package (an antivirus taking one stops it here, with
    // the installed version untouched), then swapped in; the list it writes is what the next update may retire
    gamefiles::InstallReport rep;
    if (!gamefiles::installPackage(tmp, dir, &err, &rep)) {
        std::lock_guard<std::mutex> l(st->mu);
        if (!rep.stageIssues.empty()) {
            st->notice = stageNotice(err, r, st->installed, dir, fileName(rep.stageIssues[0].path));
            st->status = "version " + r.version + " was not installed: " + err;
        } else st->status = "install failed: " + err;
    } else {
        for (auto& f : rep.swap.retired) fprintf(stderr, "update: removed retired game file %s\n", f.c_str());
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
            st->status = "installed version " + r.version;
            st->ready = true;
            st->awaitingInstall = false;
            st->notice = Notice();
        }
        checkGame(st.get(), dir, false); // still there right after the swap?
        checkSelfUpdate(st.get(), dir);
    }
    fs::remove(tmp, ec);
    st->busy = false;
}

// REPAIR: the installed version again, from the release list (fetched now if startup couldn't).
void repairInstall(std::shared_ptr<State> st, fs::path dir, std::string version) {
    std::string err;
    bool fetched;
    {
        std::lock_guard<std::mutex> l(st->mu);
        fetched = st->fetched;
        st->status = "looking up version " + version + "...";
    }
    if (!fetched && !fetchReleases(st.get(), &err)) {
        std::lock_guard<std::mutex> l(st->mu);
        if (!st->fetched) { // (fetchReleases fails on an empty channel too, with the list read)
            st->status = "REPAIR needs the internet: " + err;
            st->busy = false;
            return;
        }
    }
    Release found;
    {
        std::lock_guard<std::mutex> l(st->mu);
        for (const Release& r : st->all)
            if (r.version == version) { found = r; break; }
        if (found.version.empty()) {
            st->status = "cannot repair: version " + (version.empty() ? std::string("(unknown)") : version) +
                         " isn't published for " + kPlatform + " - pick a version and press INSTALL";
            st->busy = false;
            return;
        }
    }
    installRelease(std::move(st), dir, found);
}

// CHECK FILES: every installed game file's sha256.
void checkFilesWorker(std::shared_ptr<State> st, fs::path dir) {
    { std::lock_guard<std::mutex> l(st->mu); st->status = "checking game files..."; }
    checkGame(st.get(), dir, true);
    st->busy = false;
}

// Startup: get the channel's list, then install what the config asks for (the newest, or the pinned version), but
// never an older version than the installed one unless the player picked it.
void updater(std::shared_ptr<State> st, fs::path dir) {
    gamefiles::recoverSwap(dir); // (an update cut off between its two renames)
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
        if (!installed.empty()) {
            checkGame(st.get(), dir, false);
            checkSelfUpdate(st.get(), dir);
        }
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
        if (plan.offer) {
            st->awaitingInstall = true;
            st->status = "pinned version " + target.version + " (installed: " + installed + ") - press INSTALL to switch to it";
        } else if (installed == target.version) {
            st->status = plan.pinned ? "version " + installed + " (pinned)" : "up to date - " + ch + " " + installed;
        } else if (plan.install) {
            doInstall = true;
        } else if (st->cfg.pinned() && !plan.pinned && !st->complete) {
            st->status = "pinned version " + st->cfg.pin + " not found (release list incomplete) - installed: " + (installed.empty() ? "none" : installed);
        } else if (!target.isSigned()) {
            st->status = std::string("no ") + kPlatform + " builds from 1.17 on in the " + ch + " channel yet - pick a version to install it";
        } else {
            st->status = "installed " + installed + " is newer than the latest " + ch + " release " + target.version + " - pick a version to change";
        }
    }
    if (doInstall) { installRelease(std::move(st), dir, target); return; }
    if (!installed.empty()) checkGame(st.get(), dir, false);
    checkSelfUpdate(st.get(), dir);
    st->busy = false;
}
} // namespace

#ifdef _WIN32
// REVIEW SUP-7: the launcher is often started from the Downloads folder, where a planted DLL named like a system one
// would be loaded. DLLs loaded after this come from System32 only (SetDefaultDllDirectories exists on Windows 8+ and on
// an updated Windows 7, KB2533623: looked up, not linked, so an older Windows 7 still starts), and never from the
// current directory.
static void hardenDllSearch() {
    using SetDefaultDllDirectoriesFn = BOOL(WINAPI*)(DWORD);
    if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
        if (auto f = (SetDefaultDllDirectoriesFn)(void*)GetProcAddress(k, "SetDefaultDllDirectories")) f(0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    SetDllDirectoryW(L"");
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    hardenDllSearch();
#endif
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
        int latestRow = -1;
        Release sel;
        Channel channel;
        Notice notice;
        {
            std::lock_guard<std::mutex> l(st.mu);
            channel = st.cfg.channel;
            status = st.status;
            installed = st.installed;
            notice = st.notice;
            if (st.awaitingInstall) autoPlay = false; // the pinned version waits for the player: don't start another one
            latestRow = releases::latestIndex(st.releases);
            for (size_t i = 0; i < st.releases.size(); i++) {
                versions.push_back(st.releases[i].version);
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
                if (hov && in.pressed) {
                    std::lock_guard<std::mutex> l(st.mu);
                    st.selected = i;
                    st.cfg.pin = i == latestRow ? std::string("latest") : versions[i];
                    st.awaitingInstall = false;
                    if (st.notice.action == Notice::Retry) st.notice = Notice(); // (another version picked)
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
        // changelog (or a file problem to act on: what happened, what to do, its buttons)
        float panelW = fw - 48 * s - listW - 8 * s;
        ui.panel(24 * s, 110 * s, panelW, 222 * s, 0.6f);
        float y = 120 * s, yMax = notice.title.empty() ? 322 * s : 290 * s;
        const Color warn = {1.0f, 0.62f, 0.3f, 1};
        if (!notice.title.empty()) ui.label(36 * s, y, notice.title, warn);
        else ui.label(36 * s, y, latest.empty() ? "CHANGELOG" : "VERSION " + latest + (latest == installed ? " (INSTALLED)" : ""), ui.accent);
        y += 22 * s;
        float ts = std::max(1.0f, std::round(ui.textScale() * 0.75f));
        float maxW = fw - 80 * s - listW - 8 * s;
        // one paragraph, wrapped by width (the UI font is proportional): break at the last space that fits
        auto wrapped = [&](std::string line, const Color& c) {
            while (r.textWidth(line, ts) > maxW && y < yMax) {
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
        std::istringstream ls(notice.title.empty() ? log : notice.body);
        std::string line;
        while (std::getline(ls, line) && y < yMax) {
            while (!line.empty() && (line[0] == '#' || line[0] == ' ')) line.erase(0, 1);
            wrapped(line, {0.85f, 0.85f, 0.88f, 1});
            if (!notice.title.empty()) y += 3 * s; // (paragraphs)
        }
        if (!notice.title.empty()) {
            float bx = 36 * s, by = 298 * s, bh = 26 * s;
            const char* act = notice.action == Notice::Retry ? "RETRY" : notice.action == Notice::Repair ? "REPAIR" : nullptr;
            if (act && ui.button(bx, by, 110 * s, bh, act, !st.busy)) {
                st.busy = true;
                if (worker.joinable()) worker.join();
                if (notice.action == Notice::Retry) worker = std::thread(installRelease, stp, dir, notice.release);
                else worker = std::thread(repairInstall, stp, dir, notice.release.version);
            }
            if (ui.button(bx + 118 * s, by, 130 * s, bh, "OPEN FOLDER")) install::openFolder(dir.parent_path());
            if (!notice.blocksPlay && ui.button(bx + 256 * s, by, 90 * s, bh, "CLOSE", !st.busy)) {
                std::lock_guard<std::mutex> l(st.mu);
                st.notice = Notice();
            }
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
                st.awaitingInstall = false;
                saveCfg(st.cfg);
                applyChannel(&st);
                listScroll = 0;
                std::string ch = releases::channelName(st.cfg.channel);
                int li = releases::latestIndex(st.releases);
                if (!st.fetched) st.status = ch + " channel - offline";
                else if (st.releases.empty()) st.status = "no " + ch + " builds published yet";
                else if (li < 0) st.status = "no " + ch + " builds from 1.17 on yet - pick a version to install it";
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
        bool canPlay = st.ready && !st.busy && !notice.blocksPlay;
        if (notice.blocksPlay) autoPlay = false; // (a --play waits for the player: never a start that can't work)
        bool offerInstall = needInstall && !st.busy;
        // installing is always the player's click (or startup's newer-only rule); the installed version stays playable.
        if (offerInstall && ui.button(fw - (canPlay ? 432 : 224) * s, 414 * s, 200 * s, 48 * s, "INSTALL " + sel.version)) {
            st.busy = true;
            if (worker.joinable()) worker.join();
            worker = std::thread(installRelease, stp, dir, sel);
        } else if ((!offerInstall || canPlay) &&
                   (ui.button(fw - 224 * s, 414 * s, 200 * s, 48 * s, st.busy ? "WORKING..." : "PLAY", canPlay) || (autoPlay && canPlay))) {
            autoPlay = false;
            // never a start of a game that isn't there: checked (existence and size) before every start
            if (checkGame(&st, dir, false)) {
                int e = launchGame(dir);
                if (e == 0) quit = true;
                else if (e == kErrVirusInfected || e == kErrVirusDeleted) {
                    std::lock_guard<std::mutex> l(st.mu);
                    st.notice = exeNotice({gamefiles::kGameExe, gamefiles::Problem::Blocked}, st.installed, dir);
                    st.status = "Windows refused to start " + fileName(gamefiles::kGameExe) + " (error " + std::to_string(e) + ")";
                } else if (checkGame(&st, dir, false)) { // (gone in between: the notice says so)
                    std::lock_guard<std::mutex> l(st.mu);
                    st.status = "could not start the game in " + dir.u8string() + " (error " + std::to_string(e) + ")";
                }
            }
        }
        // a full check of the installed files (sha256), on demand
        if (!installed.empty() && ui.button(172 * s, 414 * s, 140 * s, 48 * s, "CHECK FILES", !st.busy)) {
            st.busy = true;
            if (worker.joinable()) worker.join();
            worker = std::thread(checkFilesWorker, stp, dir);
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
