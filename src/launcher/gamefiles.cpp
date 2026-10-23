#include "gamefiles.h"
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include "../common/sha256.h"
#include "qpak.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace gamefiles {

namespace {
const char* const kHeader1 = "quark installed files 1";
const char* const kHeader2 = "quark installed files 2";

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!isxdigit((unsigned char)c)) return false;
    return true;
}

enum class Read { Ok, Missing, Blocked };
// Reads a whole file: its sha256 and size. Blocked: it is there but can't be opened or read (Windows refuses to open
// a file its antivirus flagged: ERROR_VIRUS_INFECTED / ERROR_VIRUS_DELETED; or it is locked).
Read hashFile(const fs::path& p, std::string* sha, uint64_t* size) {
    Sha256 h;
    uint64_t n = 0;
#ifdef _WIN32
    HANDLE f = CreateFileW(p.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? Read::Missing : Read::Blocked;
    }
    std::string buf(1 << 16, '\0');
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, &buf[0], (DWORD)buf.size(), &got, nullptr)) { ok = false; break; }
        if (got == 0) break;
        h.update(buf.data(), got);
        n += got;
    }
    CloseHandle(f);
    if (!ok) return Read::Blocked;
#else
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        std::error_code ec;
        return fs::exists(p, ec) ? Read::Blocked : Read::Missing;
    }
    std::string buf(1 << 16, '\0');
    while (f) {
        f.read(&buf[0], (std::streamsize)buf.size());
        if (f.gcount() > 0) { h.update(buf.data(), (size_t)f.gcount()); n += (uint64_t)f.gcount(); }
    }
    if (f.bad()) return Read::Blocked;
#endif
    *sha = h.hex();
    *size = n;
    return Read::Ok;
}
std::string fileSha(const fs::path& p) {
    std::string sha;
    uint64_t n;
    return hashFile(p, &sha, &n) == Read::Ok ? sha : std::string();
}

// fs::rename, retried for up to 3 s: right after files are written an antivirus scanning them holds them open, and
// Windows refuses to rename a folder with an open file in it.
void renameRetry(const fs::path& from, const fs::path& to, std::error_code& ec) {
    for (int attempt = 0;; attempt++) {
        ec.clear();
        fs::rename(from, to, ec);
        if (!ec || attempt >= 15) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
fs::path oldPath(const fs::path& dir) { return dir.parent_path() / (dir.filename().u8string() + ".old"); }
} // namespace

std::string format(const List& files, const Sizes* sizes) {
    std::string out = std::string(sizes ? kHeader2 : kHeader1) + "\n";
    for (auto& f : files) {
        out += f.second + " ";
        if (sizes) {
            auto s = sizes->find(f.first);
            out += std::to_string(s == sizes->end() ? 0 : s->second) + " ";
        }
        out += f.first + "\n";
    }
    return out;
}

bool parse(const std::string& text, List* out, Sizes* sizes) {
    out->clear();
    if (sizes) sizes->clear();
    std::istringstream in(text);
    std::string line;
    int version = 0;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!version) {
            if (line == kHeader1) version = 1;
            else if (line == kHeader2) version = 2;
            else return false;
            continue;
        }
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string sha = lower(line.substr(0, sp)), path = line.substr(sp + 1);
        uint64_t size = 0;
        if (version == 2) { // "<sha> <size> <path>"
            size_t sp2 = path.find(' ');
            if (sp2 == std::string::npos || sp2 == 0 || sp2 > 19) continue;
            std::string num = path.substr(0, sp2);
            bool digits = true;
            for (char c : num) digits = digits && isdigit((unsigned char)c);
            if (!digits) continue;
            size = std::stoull(num);
            path = path.substr(sp2 + 1);
        }
        if (!isHex64(sha) || path.empty()) continue;
        (*out)[path] = sha;
        if (sizes && version == 2) (*sizes)[path] = size;
    }
    return version != 0;
}

bool readList(const fs::path& dir, List* out, Sizes* sizes) {
    std::ifstream f(dir / kListName, std::ios::binary);
    std::stringstream ss;
    if (f) ss << f.rdbuf();
    if (f && parse(ss.str(), out, sizes)) return true;
    out->clear();
    if (sizes) sizes->clear();
    return false;
}

bool isPlayerFile(const std::string& relIn) {
    std::string rel = lower(relIn);
    for (char& c : rel) if (c == '\\') c = '/';
    for (const char* dir : {"demos/", "screenshots/", "groups/", "custom_modes/", "huds/", "controls/", "logs/", "maps/downloads/"})
        if (rel.rfind(dir, 0) == 0) return true;
    size_t dot = rel.rfind('.');
    std::string ext = dot == std::string::npos || rel.find('/', dot) != std::string::npos ? "" : rel.substr(dot);
    for (const char* e : {".cfg", ".key", ".qdemo", ".log", ".qlm"})
        if (ext == e) return true;
    return false;
}

bool recoverSwap(const fs::path& dir) {
    std::error_code ec;
    fs::path old = oldPath(dir);
    if (fs::exists(dir, ec) || !fs::is_directory(old, ec)) return false;
    renameRetry(old, dir, ec);
    return !ec;
}

bool swapInstall(const fs::path& stage, const fs::path& dir, std::string* err, SwapReport* report) {
    std::error_code ec;
    fs::path old = oldPath(dir);
    recoverSwap(dir); // (an earlier swap cut off between its renames: <dir>.old is the install, never delete it)
    fs::remove_all(old, ec);
    if (fs::exists(dir, ec)) {
        // what the installed package put here (none: nothing is known to be a game file, everything stays)
        List installed;
        readList(dir, &installed);
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2)) continue;
            fs::path rel = fs::relative(it->path(), dir, e2);
            if (e2) continue;
            fs::path dst = stage / rel;
            if (fs::exists(dst, e2) && rel.filename() != "quark.cfg") continue; // the new package's file wins
            std::string name = rel.generic_u8string();
            auto known = installed.find(name);
            if (known != installed.end() && !isPlayerFile(name) && fileSha(it->path()) == known->second) {
                if (report) report->retired.push_back(name); // a game file the new version doesn't have: retired
                continue;
            }
            fs::create_directories(dst.parent_path(), e2);
            e2.clear();
            // (over a packaged quark.cfg: removed first, as MinGW's copy_file can't overwrite on Windows: it takes any
            // two files of a drive for the same one)
            if (fs::exists(dst, e2)) fs::remove(dst, e2);
            if (!e2) fs::copy_file(it->path(), dst, e2);
            if (e2) { // never swap without it: the old folder (and this file) would be deleted
                *err = "cannot copy " + name + " into the new version (" + e2.message() + ") - is the game running?";
                return false;
            }
            if (report) report->kept++;
        }
        if (ec) { *err = "cannot read " + dir.u8string() + " (" + ec.message() + ")"; return false; }
        renameRetry(dir, old, ec);
        if (ec) { *err = "cannot replace " + dir.u8string() + " (is the game running?)"; return false; }
    }
    renameRetry(stage, dir, ec);
    if (ec) {
        std::error_code e2;
        if (fs::exists(old, e2)) renameRetry(old, dir, e2); // (if this fails too, recoverSwap puts it back next start)
        *err = "cannot move the new version into place (" + ec.message() + ")";
        return false;
    }
    fs::remove_all(old, ec);
    return true;
}

std::vector<Issue> verify(const fs::path& root, const List& list, const Sizes* sizes, bool full, bool skipPlayerFiles) {
    std::vector<Issue> out;
    for (auto& f : list) {
        if (f.first == kListName || (skipPlayerFiles && isPlayerFile(f.first))) continue;
        fs::path p = root / fs::u8path(f.first);
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) { out.push_back({f.first, Problem::Missing}); continue; }
        auto want = sizes ? sizes->find(f.first) : Sizes::const_iterator();
        bool sized = sizes && want != sizes->end();
        if (!full) {
            uint64_t n = fs::file_size(p, ec);
            if (ec) out.push_back({f.first, Problem::Blocked});
            else if (sized && n != want->second) out.push_back({f.first, Problem::Changed});
            continue;
        }
        std::string sha;
        uint64_t n = 0;
        Read r = hashFile(p, &sha, &n);
        if (r != Read::Ok) out.push_back({f.first, r == Read::Missing ? Problem::Missing : Problem::Blocked});
        else if (sha != f.second || (sized && n != want->second)) out.push_back({f.first, Problem::Changed});
    }
    return out;
}

std::string describe(const Issue& issue) {
    std::string name = fs::u8path(issue.path).filename().u8string();
    switch (issue.problem) {
    case Problem::Missing: return name + " is missing";
    case Problem::Changed: return name + " was changed";
    default: return name + " is blocked (it can't be read)";
    }
}

Health checkInstall(const fs::path& dir, const std::string& exe, bool full) {
    Health h;
    List list;
    Sizes sizes;
    h.hasList = readList(dir, &list, &sizes);
    h.issues = verify(dir, list, &sizes, full, true);
    h.checked = list.size();
    if (!list.count(exe)) { // no list (or one without it): the exe must still be there
        h.checked++;
        std::error_code ec;
        fs::path p = dir / fs::u8path(exe);
        if (!fs::is_regular_file(p, ec)) h.issues.push_back({exe, Problem::Missing});
        else if (full) {
            std::string sha;
            uint64_t n;
            if (hashFile(p, &sha, &n) != Read::Ok) h.issues.push_back({exe, Problem::Blocked});
        }
    }
    for (size_t i = 0; i < h.issues.size(); i++)
        if (h.issues[i].path == exe) {
            h.exeBad = true;
            h.exe = h.issues[i];
            std::swap(h.issues[0], h.issues[i]); // the exe's first
            break;
        }
    return h;
}

std::string antivirusHelp(const std::string& file, const fs::path& installRoot) {
#ifdef _WIN32
    return "This is almost always an antivirus false positive: quark isn't code-signed, so Windows Defender (or another "
           "antivirus) sometimes quarantines a new build. The launcher never deletes game files.\n"
           "To get it back: Windows Security > Virus & threat protection > Protection history, open the entry for " + file +
           ", then Actions > Restore (or Allow on device).\n"
           "To stop it happening again: Windows Security > Virus & threat protection > Manage settings > Exclusions > "
           "Add an exclusion > Folder:\n" + installRoot.u8string();
#else
    return "This is most likely an antivirus or cleanup tool removing it as a false positive (quark isn't code-signed). "
           "The launcher never deletes game files.\n"
           "Restore " + file + " from that tool's quarantine, and add an exclusion for the quark folder:\n" + installRoot.u8string();
#endif
}

fs::path stagePath(const fs::path& dir) { return dir.parent_path() / (dir.filename().u8string() + ".new"); }

bool installPackage(const fs::path& pak, const fs::path& dir, std::string* err, InstallReport* report, const StageHook& afterExtract) {
    InstallReport local;
    if (!report) report = &local;
    std::error_code ec;
    fs::path stage = stagePath(dir);
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    List hashes;
    Sizes sizes;
    auto fail = [&](const std::string& why) {
        std::error_code e2;
        fs::remove_all(stage, e2);
        *err = why;
        return false;
    };
    if (!qpak::extract(pak, stage, err, &hashes, &sizes)) {
        // a file the antivirus refused as it was written (or a full disk): name it, as a file problem
        const std::string kWrite = "cannot write ";
        if (err->rfind(kWrite, 0) == 0) {
            std::string path = err->substr(kWrite.size());
            size_t note = path.find(" (");
            if (note != std::string::npos) path.resize(note);
            std::error_code e2;
            std::string rel = fs::relative(fs::u8path(path), stage, e2).generic_u8string();
            if (!e2 && !rel.empty()) {
                report->stageIssues = {{rel, Problem::Blocked}};
                return fail(fs::u8path(rel).filename().u8string() + " could not be written (a full disk, or blocked as it was written)");
            }
        }
        return fail(*err);
    }
    hashes.erase(kListName); // (a package's own list: replaced below by what was extracted, with sizes)
    sizes.erase(kListName);
    if (afterExtract) afterExtract(stage);
    // every file read back from disk: one an antivirus took (or blocked, or changed) on its way in stops the install
    // here, before the working version is touched
    report->stageIssues = verify(stage, hashes, &sizes, true, false);
    if (!report->stageIssues.empty()) {
        const Issue& first = report->stageIssues[0];
        std::string what = fs::u8path(first.path).filename().u8string() +
                           (first.problem == Problem::Missing ? " was removed" : first.problem == Problem::Changed ? " was changed" : " was blocked") +
                           " right after it was written";
        if (report->stageIssues.size() > 1) what += " (and " + std::to_string(report->stageIssues.size() - 1) + " more)";
        return fail(what);
    }
    {
        std::ofstream list(stage / kListName, std::ios::binary | std::ios::trunc);
        list << format(hashes, &sizes);
        list.close();
        if (!list) return fail("cannot write " + (stage / kListName).u8string());
    }
    if (!swapInstall(stage, dir, err, &report->swap)) return fail(*err);
    return true;
}

} // namespace gamefiles
