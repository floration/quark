#include "gamefiles.h"
#include <cctype>
#include <fstream>
#include <sstream>
#include "../common/sha256.h"

namespace fs = std::filesystem;

namespace gamefiles {

namespace {
const char* const kHeader = "quark installed files 1";

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!isxdigit((unsigned char)c)) return false;
    return true;
}
std::string fileSha(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return f ? sha256StreamHex(f) : std::string();
}
} // namespace

std::string format(const List& files) {
    std::string out = std::string(kHeader) + "\n";
    for (auto& f : files) out += f.second + " " + f.first + "\n";
    return out;
}

bool parse(const std::string& text, List* out) {
    out->clear();
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (first) {
            if (line != kHeader) return false;
            first = false;
            continue;
        }
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string sha = lower(line.substr(0, sp)), path = line.substr(sp + 1);
        if (!isHex64(sha) || path.empty()) continue;
        (*out)[path] = sha;
    }
    return !first;
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

bool swapInstall(const fs::path& stage, const fs::path& dir, std::string* err, SwapReport* report) {
    std::error_code ec;
    fs::path old = dir.parent_path() / (dir.filename().u8string() + ".old");
    fs::remove_all(old, ec);
    if (fs::exists(dir, ec)) {
        // what the installed package put here (none: nothing is known to be a game file, everything stays)
        List installed;
        {
            std::ifstream f(dir / kListName, std::ios::binary);
            std::stringstream ss;
            if (f) ss << f.rdbuf();
            if (!parse(ss.str(), &installed)) installed.clear();
        }
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
            fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, e2);
            if (report && !e2) report->kept++;
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

} // namespace gamefiles
