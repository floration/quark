#include "selfupdate.h"
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

bool replaceFile(const fs::path& newFile, const fs::path& target, std::string* err) {
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

Report updateLaunchersFromGame(const fs::path& gameRoot) {
    Report r;
    std::error_code ec;
    fs::path packaged = gameRoot / exeName();
    std::string pv = fileVersion(packaged);
    if (pv.empty()) { r.notes.push_back("no launcher in this package"); return r; }
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
            seen.push_back(f);
            std::string v = fileVersion(f), err;
            if (!isNewer(pv, v)) continue;
            if (replaceFile(packaged, f, &err)) { r.replaced++; r.notes.push_back("updated " + f.string() + " (" + (v.empty() ? "old" : v) + " -> " + pv + ")"); }
            else r.notes.push_back(err);
        }
    fs::path canonical = install::installedLauncher();
    if (!fs::exists(canonical, ec)) {
        std::string err;
        if (replaceFile(packaged, canonical, &err)) { r.installedFresh = true; r.notes.push_back("installed " + canonical.string()); }
        else { r.notes.push_back(err); return r; }
    }
    install::writeIcon();
    // shortcuts that point at an old launcher name / place go to the installed one; a fresh install gets them made
    install::repairShortcuts(canonical, r.installedFresh);
    if ((r.installedFresh || r.replaced) && install::hasPrograms()) {
        std::string ver = readText(gameRoot / "version.txt");
        install::setPrograms(true, canonical, ver.empty() ? std::string(QUARK_VERSION) : ver);
    }
    install::registerDataDir();
    return r;
}

void startFromGame(const char* argv0) {
    const char* off = getenv("QUARK_NO_LAUNCHER_UPDATE");
    if (off && *off && *off != '0') return;
    fs::path exe = install::selfPath(argv0);
    fs::path root = gameRootFrom(exe.parent_path());
    if (root.empty()) return; // not a launcher install (build folder, unpacked zip)
    std::thread([root] {
        Report r = updateLaunchersFromGame(root);
        for (auto& n : r.notes) fprintf(stderr, "launcher update: %s\n", n.c_str());
    }).detach();
}

} // namespace selfupdate
