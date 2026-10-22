// quark launcher: checks GitHub for the newest release, downloads and installs it,
// shows the changelog, and starts the game.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
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
#include "../common/proc.h"

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
const char* kApi = "https://api.github.com/repos/floration/quark/releases?per_page=50";
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

// Extracts a JSON string value starting at the opening quote; handles common escapes.
std::string jsonString(const std::string& j, size_t q) {
    std::string out;
    for (size_t i = q + 1; i < j.size(); i++) {
        char c = j[i];
        if (c == '"') break;
        if (c != '\\') { out += c; continue; }
        char e = j[++i];
        switch (e) {
            case 'n': out += '\n'; break;
            case 'r': break;
            case 't': out += "  "; break;
            case 'u': i += 4; out += '?'; break;
            default: out += e;
        }
    }
    return out;
}

std::string jsonField(const std::string& j, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    size_t k = j.find("\"" + key + "\"", from);
    if (k == std::string::npos) return "";
    size_t q = j.find('"', j.find(':', k) + 1);
    if (at) *at = q;
    return jsonString(j, q);
}

int versionCmp(const std::string& a, const std::string& b) {
    int va[3] = {0, 0, 0}, vb[3] = {0, 0, 0};
    sscanf(a.c_str(), "%d.%d.%d", &va[0], &va[1], &va[2]);
    sscanf(b.c_str(), "%d.%d.%d", &vb[0], &vb[1], &vb[2]);
    for (int i = 0; i < 3; i++) if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    return 0;
}

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
bool extractPak(const std::string& pak, const fs::path& stage, std::string* err) {
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

struct Release { std::string version, body, url; };

struct State {
    std::mutex mu;
    std::string status = "checking for updates...";
    std::vector<Release> releases;     // newest first (only ones with a build for this platform)
    std::string installed;
    int selected = 0;                  // index into releases; 0 = newest
    bool pinLatest = true;             // follow the newest version automatically
    std::atomic<float> progress{-1};
    std::atomic<bool> busy{true}, ready{false};
};

fs::path launcherCfg() { return install::appDir() / "launcher.cfg"; }

// Every release (tag, notes, our platform's .qpak) from the GitHub releases list.
bool fetchReleases(State* st, std::string* err) {
    std::ostringstream json;
    const char* api = getenv("QUARK_UPDATE_API"); // override for testing
    if (!httpGet(api ? api : kApi, json, nullptr, err)) return false;
    std::string j = json.str();
    std::string want = std::string("quark-") + kPlatform + ".qpak";
    std::vector<Release> list;
    size_t pos = 0, at;
    for (;;) {
        std::string tag = jsonField(j, "tag_name", pos, &at);
        if (tag.empty()) break;
        size_t next = j.find("\"tag_name\"", at + 1); // this release's fields end where the next one's tag starts
        std::string seg = j.substr(at, next == std::string::npos ? std::string::npos : next - at);
        pos = at + 1;
        Release r;
        r.version = tag.size() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
        r.body = jsonField(seg, "body");
        size_t p2 = 0, a2;
        for (;;) {
            std::string u = jsonField(seg, "browser_download_url", p2, &a2);
            if (u.empty()) break;
            p2 = a2 + 1;
            if (u.size() >= want.size() && u.compare(u.size() - want.size(), want.size(), want) == 0) { r.url = u; break; }
        }
        if (!r.url.empty() && isdigit((unsigned char)r.version[0])) list.push_back(r);
    }
    std::sort(list.begin(), list.end(), [](const Release& x, const Release& y) { return versionCmp(x.version, y.version) > 0; });
    std::lock_guard<std::mutex> l(st->mu);
    st->releases = list;
    if (list.empty()) { *err = "no " + std::string(kPlatform) + " builds published yet"; return false; }
    return true;
}

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
            setStatus("download failed: " + err);
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
        std::lock_guard<std::mutex> l(st->mu);
        st->installed = r.version;
        st->status = "installed version " + r.version;
        st->ready = true;
    }
    fs::remove(tmp, ec);
    st->busy = false;
}

// Startup: get the list, then install the pinned version (or the newest if following latest).
void updater(std::shared_ptr<State> st, fs::path dir) {
    std::string installed = readFile(dir / "version.txt");
    std::string pin = readFile(launcherCfg());
    if (pin.rfind("pin=", 0) == 0) pin = pin.substr(4);
    {
        std::lock_guard<std::mutex> l(st->mu);
        st->installed = installed;
        st->ready = !installed.empty();
    }
    std::string err;
    if (!fetchReleases(st.get(), &err)) {
        std::lock_guard<std::mutex> l(st->mu);
        st->status = installed.empty() ? "offline - " + err : "offline - playing installed version " + installed;
        st->busy = false;
        return;
    }
    Release target;
    {
        std::lock_guard<std::mutex> l(st->mu);
        st->pinLatest = pin.empty() || pin == "latest";
        st->selected = 0;
        if (!st->pinLatest)
            for (size_t i = 0; i < st->releases.size(); i++) if (st->releases[i].version == pin) st->selected = (int)i;
        target = st->releases[st->selected];
        if (installed == target.version) {
            st->status = st->pinLatest ? "up to date - version " + installed : "version " + installed + " (pinned)";
            st->busy = false;
            return;
        }
    }
    installRelease(std::move(st), dir, target);
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--uninstall") {
        bool wipe = false;
#ifdef _WIN32
        if (MessageBoxW(nullptr, L"Remove quark, its shortcuts and downloaded game files?", L"Uninstall quark",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        std::wstring q = L"Also delete your settings, ranked identity, demos and maps?\n\n" + install::userDataDir().wstring() +
                         L"\n\nChoose No to keep them for a later reinstall.";
        wipe = MessageBoxW(nullptr, q.c_str(), L"Uninstall quark", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
#endif
        install::uninstallAll(wipe);
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
    if (!install::acquireSingleInstance(handedOver)) return 0;
    // Started from Downloads (or anywhere else): install into the program folder, then hand over
    // to that copy, which deletes the downloaded file. Never hands over to itself, and a process
    // that was itself started by a hand-over never hands over again (no relaunch loops).
    fs::path self = install::selfPath(argv[0]);
    fs::path launcherPath = install::installSelf(argv[0]);
    // a desktop icon and a start menu / app menu entry, always (recreated if missing)
    if (!install::hasDesktop()) install::setDesktop(true, launcherPath);
    if (!install::hasMenu()) install::setMenu(true, launcherPath);
    std::error_code eqEc;
    if (!handedOver && install::runningFromOutside(argv[0]) && fs::exists(launcherPath) && !fs::equivalent(launcherPath, self, eqEc)) {
        if (install::relaunchInstalled(launcherPath, self)) return 0; // it waits for us to exit, then takes the lock
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
    bool uninstallWipe = false; // also delete the player's data (settings, ranked key, demos, maps)
    bool autoPlay = argc > 1 && std::string(argv[1]) == "--play";
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
        std::string status, latest, log, installed;
        std::vector<std::string> versionNames, versions;
        Release sel;
        {
            std::lock_guard<std::mutex> l(st.mu);
            status = st.status;
            installed = st.installed;
            for (size_t i = 0; i < st.releases.size(); i++) versions.push_back(st.releases[i].version);
            for (size_t i = 0; i < st.releases.size(); i++)
                versionNames.push_back(i == 0 ? "LATEST (" + st.releases[0].version + ")" : st.releases[i].version);
            if (!st.releases.empty()) {
                sel = st.releases[std::min<size_t>(st.selected, st.releases.size() - 1)];
                latest = sel.version;
                log = sel.body;
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
        ui.label(listX + 10 * s, listY + 8 * s, "VERSIONS", ui.accent);
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
                    st.pinLatest = i == 0;
                    std::ofstream(launcherCfg()) << "pin=" << (i == 0 ? std::string("latest") : versions[i]) << "\n";
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
        std::istringstream ls(log);
        std::string line;
        float ts = std::max(1.0f, std::round(ui.textScale() * 0.75f));
        size_t maxChars = (size_t)((fw - 80 * s - listW - 8 * s) / (6 * ts));
        while (std::getline(ls, line) && y < 322 * s) {
            while (!line.empty() && (line[0] == '#' || line[0] == ' ')) line.erase(0, 1);
            while (line.size() > maxChars && y < 322 * s) {
                r.text(36 * s, y, ts, line.substr(0, maxChars), {0.85f, 0.85f, 0.88f, 1});
                line = line.substr(maxChars);
                y += 10 * ts;
            }
            r.text(36 * s, y, ts, line, {0.85f, 0.85f, 0.88f, 1});
            y += 10 * ts;
        }
        // desktop integration: the desktop icon and start menu entry are made automatically (at startup);
        // only UNINSTALL is left here
        {
            float bw = (fw - 56 * s) * 0.5f, bx = 24 * s, by = 344 * s, bh = 40 * s;
            bool armed = timeNow() < uninstallArmed;
            if (ui.button(bx + bw + 8 * s, by, bw, bh, uninstallWipe ? "MY DATA: DELETE" : "MY DATA: KEEP", !st.busy)) uninstallWipe = !uninstallWipe;
            if (ui.button(bx, by, bw, bh, armed ? "CLICK TO CONFIRM" : "UNINSTALL", !st.busy)) {
                if (armed) {
                    install::uninstallAll(uninstallWipe);
                    quit = true;
                } else uninstallArmed = timeNow() + 3.0;
            }
        }
        bool needInstall = !sel.version.empty() && sel.version != installed;
        bool canPlay = st.ready && !st.busy;
        if (needInstall && !st.busy) {
            if (ui.button(fw - 224 * s, 414 * s, 200 * s, 48 * s, "INSTALL " + sel.version)) {
                st.busy = true;
                if (worker.joinable()) worker.join();
                worker = std::thread(installRelease, stp, dir, sel);
            }
        } else if (ui.button(fw - 224 * s, 414 * s, 200 * s, 48 * s, st.busy ? "WORKING..." : "PLAY", canPlay) || (autoPlay && canPlay)) {
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
