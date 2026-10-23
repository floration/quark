#include "selfupdate.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include "install.h"
#include "releases.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "../common/version.h"

namespace fs = std::filesystem;

namespace selfupdate {

// Found in a launcher file by fileVersion(). Defined in the generated version.cpp (one file recompiles on a version
// bump), and referenced by ownVersion(), so every launcher has it.
static const char* const kMarker = kQuarkLauncherMarker;
static const size_t kMarkerPrefix = 23; // strlen("QUARK-LAUNCHER-VERSION:")

const char* ownVersion() { return kMarker + kMarkerPrefix; }

const char* exeName() {
#ifdef _WIN32
    return "quark_launcher.exe";
#else
    return "quark_launcher";
#endif
}

namespace {
bool executableMagic(const unsigned char* m) {
    if (m[0] == 'M' && m[1] == 'Z') return true;                         // Windows PE
    if (m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F') return true; // ELF
    uint32_t be = (uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3];
    return be == 0xfeedface || be == 0xfeedfacf || be == 0xcefaedfe || be == 0xcffaedfe || be == 0xcafebabe; // Mach-O, fat
}
std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
bool sameFile(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::equivalent(a, b, ec)) return true;
    return a.lexically_normal() == b.lexically_normal();
}
} // namespace

fs::path sigPath(const fs::path& launcher) { return launcher.parent_path() / (launcher.filename().string() + ".sig"); }

bool launcherSigned(const fs::path& launcher, std::string* sigHex, std::string* err, const std::string& pubHex) {
    std::error_code ec;
    fs::path sp = sigPath(launcher);
    if (!fs::is_regular_file(sp, ec) || fs::file_size(sp, ec) > 1024) { *err = "the packaged launcher has no signature"; return false; }
    std::string sig = readText(sp), e;
    if (!sign::verifyFile(launcher.string(), sig, pubHex, &e)) { *err = "the packaged launcher's signature does not verify"; return false; }
    if (sigHex) *sigHex = sig;
    return true;
}

std::string fileVersion(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    unsigned char magic[4] = {};
    if (!f.read((char*)magic, 4) || !executableMagic(magic)) return "";
    f.seekg(0);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string data = ss.str();
    const std::string prefix(kMarker, kMarkerPrefix);
    for (size_t at = data.find(prefix); at != std::string::npos; at = data.find(prefix, at + 1)) {
        size_t s = at + kMarkerPrefix, e = s;
        while (e < data.size() && e - s < 64 && (isalnum((unsigned char)data[e]) || data[e] == '.' || data[e] == '-' || data[e] == '+')) e++;
        if (e > s && e < data.size() && data[e] == '\0' && isdigit((unsigned char)data[s])) return data.substr(s, e - s);
    }
    return "";
}

bool isNewer(const std::string& packaged, const std::string& installed) {
    if (packaged.empty()) return false;
    return installed.empty() || releases::versionCmp(packaged, installed) > 0;
}

std::vector<fs::path> launchersIn(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string n = e.path().filename().string();
        if (n.rfind("quark_launcher", 0) != 0 && n.rfind("quark-launcher", 0) != 0) continue;
        std::string ext = e.path().extension().string();
        for (auto& ch : ext) ch = (char)tolower((unsigned char)ch);
#ifdef _WIN32
        if (ext != ".exe") continue;
#else
        if (ext == ".old" || ext == ".new" || ext == ".sig" || ext == ".zip" || ext == ".exe" || ext == ".qpak") continue;
#endif
        std::ifstream f(e.path(), std::ios::binary);
        unsigned char magic[4] = {};
        if (!f.read((char*)magic, 4) || !executableMagic(magic)) continue;
        out.push_back(e.path());
    }
    return out;
}

SwapPaths swapPaths(const fs::path& target) {
    return {target.parent_path() / (target.filename().string() + ".new"), target.parent_path() / (target.filename().string() + ".old")};
}

bool replaceFile(const fs::path& newFile, const fs::path& target, std::string* err, ShutdownGate* gate, const SigCheck* check) {
    std::error_code ec;
    SwapPaths p = swapPaths(target);
    fs::create_directories(target.parent_path(), ec);
    fs::remove(p.staged, ec);
    ec.clear();
    fs::copy_file(newFile, p.staged, fs::copy_options::overwrite_existing, ec);
    if (ec || fs::file_size(p.staged, ec) != fs::file_size(newFile, ec)) {
        if (err) *err = "cannot write " + p.staged.string();
        fs::remove(p.staged, ec);
        return false;
    }
    if (check) { // the copy that goes in place is the one checked (not the source, which could change in between)
        std::string e;
        if (!sign::verifyFile(p.staged.string(), check->sigHex, check->pubHex, &e)) {
            if (err) *err = "the new launcher's signature does not verify";
            fs::remove(p.staged, ec);
            return false;
        }
    }
    // the renames: all of them or none, never cut off by the game quitting (finishFromGame)
    std::unique_lock<std::timed_mutex> lock;
    if (gate) {
        lock = std::unique_lock<std::timed_mutex>(gate->mu);
        if (gate->stop) {
            fs::remove(p.staged, ec);
            if (err) *err = "shutting down";
            return false;
        }
    }
#ifdef _WIN32
    fs::remove(p.old, ec); // a leftover from an earlier swap (fails if that copy still runs: then we can't swap now)
    bool had = fs::exists(target, ec);
    if (had) {
        ec.clear();
        fs::rename(target, p.old, ec); // Windows lets a running exe be renamed, not overwritten
        if (ec) { if (err) *err = "cannot move " + target.string() + " aside"; fs::remove(p.staged, ec); return false; }
    }
    ec.clear();
    fs::rename(p.staged, target, ec);
    if (ec) {
        std::error_code e2;
        if (had) fs::rename(p.old, target, e2);
        fs::remove(p.staged, e2);
        if (err) *err = "cannot move the new launcher into " + target.string();
        return false;
    }
    fs::remove(p.old, ec); // still running: removeLeftovers deletes it next start
#else
    chmod(p.staged.string().c_str(), 0755);
    ec.clear();
    fs::rename(p.staged, target, ec); // atomic: a running copy keeps its old inode
    if (ec) {
        std::error_code e2;
        fs::remove(p.staged, e2);
        if (err) *err = "cannot move the new launcher into " + target.string();
        return false;
    }
#endif
    return true;
}

void removeLeftovers(const fs::path& target) {
    std::error_code ec;
    SwapPaths p = swapPaths(target);
    fs::remove(p.old, ec);
    fs::remove(p.staged, ec);
}

fs::path gameRootFrom(const fs::path& exeDir) {
    std::error_code ec;
    fs::path d = exeDir;
    for (int i = 0; i < 4 && !d.empty(); i++) { // quark[.exe] sits in the root; macOS: <root>/quark.app/Contents/MacOS
        if (fs::is_regular_file(d / "version.txt", ec)) return d;
        if (d == d.parent_path()) break;
        d = d.parent_path();
    }
    return {};
}

namespace {
// One step that changes something the player runs or clicks: whole, and only while the game isn't shutting down.
template <class F> bool gated(ShutdownGate* gate, F f) {
    if (!gate) { f(); return true; }
    std::lock_guard<std::timed_mutex> l(gate->mu);
    if (gate->stop) return false;
    f();
    return true;
}
} // namespace

Report updateLaunchersFromGame(const fs::path& gameRoot, ShutdownGate* gate, const std::string& pubHex) {
    Report r;
    std::error_code ec;
    fs::path packaged = gameRoot / exeName();
    std::string pv = fileVersion(packaged);
    if (pv.empty()) { r.notes.push_back("no launcher in this package"); return r; }
    // signed, or no launcher anywhere is touched (B6: every launcher from 1.17.0 on is; a marker means >= 1.17.0)
    SigCheck check;
    check.pubHex = pubHex;
    std::string why;
    if (!launcherSigned(packaged, &check.sigHex, &why, pubHex)) { r.notes.push_back(why + ": not used"); return r; }
    // Every place any launcher version lived: the folder above its game folder (0.1.0 kept the game next to
    // wherever the launcher was unpacked), the install folder (0.5.0+), the pre-0.12 Windows folder.
    std::vector<fs::path> dirs = {gameRoot.parent_path(), install::appDir()};
    for (const fs::path& d : install::legacyAppDirs()) dirs.push_back(d);
    std::vector<fs::path> seen;
    for (const fs::path& d : dirs)
        for (const fs::path& f : launchersIn(d)) {
            bool dup = false;
            for (auto& s : seen) dup = dup || sameFile(s, f);
            if (dup || sameFile(f, packaged)) continue;
            if (gate && gate->stop) { r.notes.push_back("game quit: the rest next start"); return r; }
            seen.push_back(f);
            std::string v = fileVersion(f), err;
            if (!isNewer(pv, v)) continue;
            if (replaceFile(packaged, f, &err, gate, &check)) { r.replaced++; r.notes.push_back("updated " + f.string() + " (" + (v.empty() ? "old" : v) + " -> " + pv + ")"); }
            else r.notes.push_back(err);
        }
    fs::path canonical = install::installedLauncher();
    if (!fs::exists(canonical, ec)) {
        std::string err;
        if (replaceFile(packaged, canonical, &err, gate, &check)) { r.installedFresh = true; r.notes.push_back("installed " + canonical.string()); }
        else { r.notes.push_back(err); return r; }
    }
    bool fresh = r.installedFresh;
    bool ok = gated(gate, [] { install::writeIcon(); }) &&
              // shortcuts that point at an old launcher name / place go to the installed one; a fresh install gets them made
              gated(gate, [&] { install::repairShortcuts(canonical, fresh); });
    if (ok && (r.installedFresh || r.replaced) && install::hasPrograms()) {
        std::string ver = readText(gameRoot / "version.txt");
        ok = gated(gate, [&] { install::setPrograms(true, canonical, ver.empty() ? std::string(QUARK_VERSION) : ver); });
    }
    ok = ok && gated(gate, [] { install::registerDataDir(); });
    if (!ok) r.notes.push_back("game quit: shortcuts next start");
    return r;
}

namespace {
// The game's launcher-update thread. Heap-held and never freed while the thread may still run, so a thread left to
// the process exit never touches a destroyed object.
struct GameRun {
    ShutdownGate gate;
    std::thread th;
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
};
GameRun* g_run = nullptr;
} // namespace

void startFromGame(const char* argv0) {
    const char* off = getenv("QUARK_NO_LAUNCHER_UPDATE");
    if (off && *off && *off != '0') return;
    fs::path exe = install::selfPath(argv0);
    fs::path root = gameRootFrom(exe.parent_path());
    if (root.empty() || g_run) return; // not a launcher install (build folder, unpacked zip)
    GameRun* run = new GameRun;
    run->th = std::thread([run, root] {
        Report r = updateLaunchersFromGame(root, &run->gate);
        for (auto& n : r.notes) fprintf(stderr, "launcher update: %s\n", n.c_str());
        std::lock_guard<std::mutex> l(run->mu);
        run->done = true;
        run->cv.notify_all();
    });
    g_run = run;
}

bool finishFromGame(double maxWaitSeconds) {
    GameRun* run = g_run;
    if (!run) return true;
    g_run = nullptr;
    run->gate.stop = true;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                           std::chrono::duration<double>(maxWaitSeconds));
    // a step in progress ends (no new one starts after this); then the thread itself, both within the bound
    if (run->gate.mu.try_lock_until(deadline)) run->gate.mu.unlock();
    bool ended;
    {
        std::unique_lock<std::mutex> l(run->mu);
        ended = run->cv.wait_until(l, deadline, [run] { return run->done; });
    }
    if (!ended) { run->th.detach(); return false; } // `run` stays allocated: the thread may still read it
    run->th.join();
    delete run;
    return true;
}

} // namespace selfupdate
