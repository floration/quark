#include "official_map_data.h"
#include "../common/u8file.h"
#include "map.h"
#include "gamedefs.h"
#include "../common/sha256.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <exception>
#include <type_traits>
#include <mutex>
#include <atomic>
#include "../common/version.h"
#include <unordered_map>
#include <cstdlib>
#include <cerrno>
#include <cfloat>
#include <climits>
#include <algorithm>
#include <set>
#include <vector>

namespace fs = std::filesystem;
static std::string g_exeDir;

void setExecutableDir(const char* argv0) {
    std::error_code ec;
    fs::path p = fs::absolute(utf8Path(argv0), ec);   // (argv0: UTF-8, see main)
    g_exeDir = p.parent_path().u8string();
}

const std::string& executableDir() { return g_exeDir; }

// rel in the install: next to the working directory or the executable (or a macOS bundle's Resources); found = it's there
static std::string findDataPathUncached(const std::string& rel, bool& found) {
    std::error_code ec;
    const std::string candidates[] = {
        rel,
        g_exeDir + "/" + rel,
        g_exeDir + "/../" + rel,
        g_exeDir + "/../../" + rel,
        g_exeDir + "/../Resources/" + rel, // macOS bundle
    };
    found = true;
    for (auto& c : candidates)
        if (!c.empty() && fs::exists(c, ec)) return c;
    found = false;
    return rel;
}

std::string findDataPath(const std::string& rel) {
    // (PERF-12) up to 5 fs::exists per call, for every map load and listing: where something was found is remembered
    // (the install doesn't move); what wasn't found is looked for again next time (it may be written meanwhile)
    static std::mutex mu;
    static std::unordered_map<std::string, std::string> cache;
    {
        std::lock_guard<std::mutex> l(mu);
        auto it = cache.find(rel);
        if (it != cache.end()) return it->second;
    }
    bool found = false;
    std::string hit = findDataPathUncached(rel, found);
    if (found) { std::lock_guard<std::mutex> l(mu); cache.emplace(rel, hit); }
    return hit;
}

// ---- per-user data -------------------------------------------------------------------------------------
static std::string g_userDir;       // set by setUserDataDir, else the platform default
static bool g_userMigrate = true;
static std::once_flag g_userOnce;

void setUserDataDir(const std::string& dir, bool migrate) { g_userDir = dir; g_userMigrate = migrate; }

static std::string defaultUserDir() {
    auto env = [](const char* n) -> std::string { return u8getenv(n); };   // (UTF-8: %APPDATA% under a non-ASCII user name)
#if defined(_WIN32)
    std::string base = env("APPDATA");
    if (!base.empty()) return base + "/quark";
#elif defined(__APPLE__)
    std::string home = env("HOME");
    if (!home.empty()) return home + "/Library/Application Support/quark";
#else
    std::string x = env("XDG_DATA_HOME"), home = env("HOME");
    if (!x.empty()) return x + "/quark";
    if (!home.empty()) return home + "/.local/share/quark";
#endif
    return findDataPath("maps") + "/.."; // nowhere better: the old place next to the install
}

// The shipped copy of an official map, or null.
static const OfficialMapData* officialData(const std::string& name) {
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) if (name == m->name) return m;
    return nullptr;
}
static std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static std::string noCR(std::string s) { s.erase(std::remove(s.begin(), s.end(), '\r'), s.end()); return s; }

// Copies one file unless the destination already exists; verified by comparing the bytes. Returns 1 if copied.
static int migrateFile(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    if (!fs::is_regular_file(src, ec) || fs::exists(dst, ec)) return 0;
    fs::create_directories(dst.parent_path(), ec);
    fs::path tmp = dst;
    tmp += ".migrating";
    fs::remove(tmp, ec);   // (MinGW's copy_file can't overwrite on Windows)
    if (!fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec) || readAll(tmp) != readAll(src)) { fs::remove(tmp, ec); return 0; }
    fs::rename(tmp, dst, ec);
    if (ec) { fs::remove(tmp, ec); return 0; }
    return 1;
}
static int migrateDir(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    int n = 0;
    if (!fs::is_directory(src, ec)) return 0;
    for (auto it = fs::recursive_directory_iterator(src, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) n += migrateFile(it->path(), dst / fs::relative(it->path(), src, ec));
    return n;
}

int migrateUserData(const std::string& oldRoot, const std::string& newRoot) {
    std::error_code ec;
    fs::path o = fs::u8path(oldRoot), d = fs::u8path(newRoot);
    if (fs::exists(o, ec) && fs::exists(d, ec) && fs::equivalent(o, d, ec)) return 0;
    int n = 0;
    for (const char* f : {"quark.cfg", "quark.key", "hud.cfg", "race_times.log"}) n += migrateFile(o / f, d / f);
    for (const char* sub : {"demos", "groups", "custom_modes", "huds", "controls", "logs"}) n += migrateDir(o / sub, d / sub);
    n += migrateDir(o / "maps" / "downloads", d / "maps" / "downloads");
    // your maps: everything that isn't an untouched shipped map (those stay in the install, read-only).
    // Light caches (.qlm) are rebuilt on demand, so they aren't carried over.
    for (auto& e : fs::directory_iterator(o / "maps", ec)) {
        if (e.path().extension() != ".qmap") continue;
        std::string name = e.path().stem().string();
        if (const OfficialMapData* m = officialData(name))
            if (noCR(readAll(e.path())) == noCR(std::string((const char*)m->data, m->size))) continue;
        n += migrateFile(e.path(), d / "maps" / e.path().filename());
    }
    return n;
}

std::string userDataPath(const std::string& rel) {
    std::call_once(g_userOnce, [] {
        if (g_userDir.empty()) {
            std::string e = u8getenv("QUARK_DATA_DIR");
            if (!e.empty()) { g_userDir = e; g_userMigrate = false; }
            else g_userDir = defaultUserDir();
        }
        std::error_code ec;
        fs::create_directories(fs::u8path(g_userDir), ec);
        if (g_userMigrate && !fs::exists(fs::u8path(g_userDir) / ".migrated", ec)) {
            std::string oldRoot = findDataPath("maps") + "/..";
            int n = migrateUserData(oldRoot, g_userDir);
            if (!executableDir().empty()) n += migrateDir(fs::u8path(executableDir()) / "logs", fs::u8path(g_userDir) / "logs");
            std::ofstream(fs::u8path(g_userDir) / ".migrated") << "copied " << n << " files from " << oldRoot << " (originals kept)\n";
        }
    });
    return rel.empty() ? g_userDir : g_userDir + "/" + rel;
}

std::string userMapsDir() { return userDataPath("maps"); }

static bool officialNamed(const std::string& name) {
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) if (name == m->name) return true;
    return false;
}

int resetOfficialMaps(const std::string& shippedDir) {
    std::error_code ec;
    std::string dir = shippedDir.empty() ? findDataPath("maps") : shippedDir, mine = userMapsDir(), cache = userDataPath("cache");
    fs::create_directories(dir, ec);
    int n = 0;
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) {
        fs::remove(mine + "/" + m->name + ".qmap", ec); // your edited copy shadows the shipped one: reset drops it
        fs::remove(cache + "/" + m->name + ".qlm", ec);   // (a light cache named after the map: older versions'; now they go by content)
        std::string path = dir + "/" + m->name + ".qmap";
        if (noCR(readAll(path)) != noCR(std::string((const char*)m->data, m->size))) { // install may be read-only: write only if needed
            std::ofstream f(utf8Path(path), std::ios::binary | std::ios::trunc);
            if (!f.write((const char*)m->data, (std::streamsize)m->size)) continue;
        }
        n++;
    }
    for (const char* old : {"arena", "tug", "duel", "sprint", "crucible"}) { // retired built-in maps left by older versions
        fs::remove(dir + "/" + old + ".qmap", ec);
        fs::remove(dir + "/" + old + ".qlm", ec);
    }
    // (1.19.6: the old pool, now shipped in maps/legacy/; an older install's copy beside the current maps goes)
    for (const char* old : kLegacyMapNames) {
        if (officialNamed(old)) continue;
        fs::remove(dir + "/" + old + ".qmap", ec);
        fs::remove(dir + "/" + old + ".qlm", ec);
    }
    return n;
}

// ---- legacy maps (1.19.6) ------------------------------------------------------------------------------
// The old map pool (before 1.19.6) ships in maps/legacy/: not official (not embedded, never ranked) and only listed
// with the LEGACY MAPS setting on. Asked for by name (a server's --map, a demo, an old config) one still loads.
static std::atomic<bool> g_legacyListed{false};
void setLegacyMapsListed(bool on) { g_legacyListed = on; }
bool legacyMapsListed() { return g_legacyListed; }
std::string userLegacyMapsDir() { return userMapsDir() + "/legacy"; }

// A copy make_maps.py wrote (its first line) that nobody edited since (no `handmade` line).
static bool untouchedGeneratedMap(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string line;
    if (!std::getline(f, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "# generated by tools/make_maps.py") return false;
    while (std::getline(f, line)) {
        size_t a = line.find_first_not_of(" \t");
        if (a != std::string::npos && line.compare(a, 8, "handmade") == 0 &&
            (a + 8 == line.size() || isspace((unsigned char)line[a + 8]))) return false;
    }
    return true;
}

LegacyMoveReport moveOldOfficialMaps(const std::string& userMaps, const std::string& shippedMaps,
                                     const std::string& stampPath, const std::string& version) {
    LegacyMoveReport rep;
    std::error_code ec;
    {
        std::ifstream st(utf8Path(stampPath));
        std::string done;
        if (st && std::getline(st, done) && done == version) return rep;   // (once per version: no scan at every start)
    }
    rep.ran = true;
    const fs::path mine = fs::u8path(userMaps), legacy = mine / "legacy", cache = mine.parent_path() / "cache";
    // your maps folder (its top level only: never downloads/, legacy/ or old/): the untouched copies of shipped maps
    // that older versions and launchers left there, which shadow the shipped map of that name, go to maps/legacy/
    std::vector<fs::path> found;
    for (auto& e : fs::directory_iterator(mine, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".qmap") found.push_back(e.path());
    ec.clear();
    std::sort(found.begin(), found.end());
    // (by name too: an untouched generated map saved under a name of your own, e.g. solarsystem12, is yours and stays:
    // owner, 2026-10-31, "i can only see up to solar system 9")
    auto shippedName = [](const std::string& n) {
        if (officialNamed(n)) return true;
        for (const char* old : kLegacyMapNames) if (n == old) return true;
        for (const char* old : {"arena", "tug", "duel", "sprint", "crucible"}) if (n == old) return true;   // (retired before 1.19.6)
        return false;
    };
    for (const fs::path& p : found) {
        const std::string stem = p.stem().u8string();
        if (!shippedName(stem) || !untouchedGeneratedMap(p)) continue;
        fs::create_directories(legacy, ec);
        fs::path dst = legacy / p.filename();
        std::string to = stem;
        bool same = fs::exists(dst, ec) && readAll(dst) == readAll(p);
        for (int k = 2; !same && fs::exists(dst, ec); k++) { to = stem + "_" + std::to_string(k); dst = legacy / (to + ".qmap"); }
        if (same) fs::remove(p, ec); else fs::rename(p, dst, ec);
        if (ec) { rep.log.push_back("couldn't move " + p.filename().u8string() + " (" + ec.message() + ")"); ec.clear(); continue; }
        rep.moved++;
        rep.log.push_back(p.filename().u8string() + (same ? ": already in legacy/, the copy removed" : ": moved to legacy/" + to + ".qmap"));
        fs::path side = mine / (stem + ".mapedit.json"), sideTo = legacy / (to + ".mapedit.json");
        if (fs::exists(side, ec)) { if (fs::exists(sideTo, ec)) fs::remove(side, ec); else fs::rename(side, sideTo, ec); ec.clear(); }
        fs::remove(mine / (stem + ".qlm"), ec);    // light caches named after it (rebuilt on demand)
        fs::remove(cache / (stem + ".qlm"), ec);
    }
    // the install's maps folder: an old official map an older install left beside the current ones (the launcher's
    // update drops those, an unzip over an old install doesn't): its copy in maps/legacy/ is the one kept
    if (!shippedMaps.empty()) {
        const fs::path dir = fs::u8path(shippedMaps), sl = dir / "legacy";
        std::vector<fs::path> ship;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".qmap") ship.push_back(e.path());
        ec.clear();
        for (const fs::path& p : ship) {
            const std::string stem = p.stem().u8string();
            if (officialNamed(stem) || !untouchedGeneratedMap(p)) continue;
            if (fs::exists(sl / p.filename(), ec)) fs::remove(p, ec);
            else { fs::create_directories(sl, ec); fs::rename(p, sl / p.filename(), ec); }
            if (ec) { ec.clear(); continue; }   // (a read-only install: left as it is)
            fs::remove(dir / (stem + ".qlm"), ec);
            rep.shipped++;
            rep.log.push_back("install's " + p.filename().u8string() + ": retired (maps/legacy/ has it)");
        }
    }
    fs::create_directories(fs::u8path(stampPath).parent_path(), ec);
    std::ofstream(utf8Path(stampPath), std::ios::trunc) << version << "\n";
    return rep;
}

LegacyMoveReport moveOldOfficialMaps() {
    return moveOldOfficialMaps(userMapsDir(), findDataPath("maps"), userDataPath("legacy_maps.stamp"), kQuarkVersion);
}

bool isLegacyMap(const std::string& name) {
    std::string p = mapFilePath(name);
    for (char& c : p) if (c == '\\') c = '/';
    size_t s = p.rfind('/');
    return s != std::string::npos && s >= 7 && p.compare(s - 7, 7, "/legacy") == 0;
}

std::vector<std::pair<std::string, std::string>> officialMapTexts() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) out.emplace_back(m->name, std::string((const char*)m->data, m->size));
    return out;
}

std::string mapsFolder() {
    std::error_code ec;
    fs::create_directories(userMapsDir(), ec);
    auto p = fs::absolute(userMapsDir(), ec);
    return ec ? userMapsDir() : p.lexically_normal().string();
}

std::vector<std::string> listMaps() {
    std::vector<std::string> out;
    std::error_code ec;
    std::vector<std::string> dirs = {findDataPath("maps"), userMapsDir()};
    if (g_legacyListed) { dirs.push_back(findDataPath("maps/legacy")); dirs.push_back(userLegacyMapsDir()); }   // (LEGACY MAPS on)
    for (const std::string& dir : dirs) {
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".qmap") out.push_back(e.path().stem().string());
        ec.clear();
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static bool mapNameChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }
static constexpr size_t kMaxMapName = 40;   // (what the editor lets you save)

bool validMapName(const std::string& name) {
    if (name.empty() || name.size() > kMaxMapName) return false;
    for (char c : name) if (!mapNameChar(c)) return false;
    std::string low;
    for (char c : name) low += (char)tolower((unsigned char)c);
    static const char* dev[] = {"con", "prn", "aux", "nul"};
    for (const char* d : dev) if (low == d) return false;
    if (low.size() == 4 && (low.rfind("com", 0) == 0 || low.rfind("lpt", 0) == 0) && low[3] >= '0' && low[3] <= '9') return false;
    return true;
}

bool plainFileName(const std::string& name) {
    if (name.empty() || name.size() > 128 || name[0] == '.') return false;
    for (char c : name) if ((unsigned char)c < 32 || (unsigned char)c > 126 || c == '/' || c == '\\' || c == ':') return false;
    return true;
}

std::string typedMapName(const std::string& typed) {
    std::string o;
    for (char c : typed) {
        if (c == ' ') c = '_';
        if (mapNameChar(c) && o.size() < kMaxMapName) o += c;
    }
    return o;
}
std::string safeMapName(const std::string& name) {
    std::string o = name.substr(0, kMaxMapName);
    for (char& c : o) if (!mapNameChar(c)) c = '_';
    return o.empty() ? "map" : o;
}

std::string downloadedMapPath(const MapId& id) { return userMapsDir() + "/downloads/" + id.hex() + ".qmap"; }

bool saveMapFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    return writeFileAtomic(path, text);
}

bool writeFileAtomic(const std::string& path, const std::string& data, bool privateFile) {
    std::error_code ec;
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(utf8Path(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        if (privateFile) fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        f.write(data.data(), (std::streamsize)data.size());
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, path, ec);   // (replaces the old file in one step)
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

// ---- map format converters ----
// A line's first word, comments dropped ("" for a blank or comment-only line).
static std::string firstWord(const std::string& line, size_t* rest = nullptr) {
    size_t e = line.find('#');
    if (e == std::string::npos) e = line.size();
    size_t a = line.find_first_not_of(" \t\r", 0);
    if (a == std::string::npos || a >= e) return "";
    size_t b = line.find_first_of(" \t\r", a);
    if (b == std::string::npos || b > e) b = e;
    if (rest) *rest = b;
    return line.substr(a, b - a);
}

static std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        v.push_back(text.substr(p, e - p));
        p = e + 1;
    }
    return v;
}

// 1 -> 2: the generator's staircases and the map's setting were comments ("# stairs ...", "# setting ..."); they are
// keywords now.
static void upgradeMap1to2(std::vector<std::string>& lines) {
    for (std::string& l : lines)
        if (l.rfind("# stairs ", 0) == 0 || l.rfind("# setting ", 0) == 0) l.erase(0, 2);
}

// Step k turns version k into k + 1. Changing the map format means: raise kMapFormatVersion and add its step here
// (with a test that converts a file of the old version); the static_assert below holds the two together.
using MapUpgradeStep = void (*)(std::vector<std::string>& lines);
// 2 -> 3: the nograpple material was added; a version 2 file has none, so nothing changes
static void upgradeMap2to3(std::vector<std::string>&) {}
// 3 -> 4: the acid material was added; a version 3 file has none, so nothing changes
static void upgradeMap3to4(std::vector<std::string>&) {}

// 4 -> 5: wedges and corners ("wedge" / "corner" lines) were added; a version 4 file has none, so nothing changes
static void upgradeMap4to5(std::vector<std::string>&) {}

// 5 -> 6: brick, cobblestone and glass never break now; the breakable ones are their own materials (owner, 2026-10-29).
// So that every map plays as it did: all glass becomes breakable glass, and brick / cobblestone boxes (a box line, a
// custom group's box part, a room whose walls come out that thin) no thicker than kBreakableThick become the breakable
// kind (only those could be blown open). Thicker ones and slopes never broke and stay as they are.
static void upgradeMap5to6(std::vector<std::string>& lines) {
    auto thin = [](float a, float b, float c) { return std::min(a, std::min(b, c)) <= kBreakableThick + 1e-4f; };
    auto breakable = [](int m) { return m == kMatGlass ? kMatGlassBreak : m == kMatBrick ? kMatBrickBreak : m == kMatCobble ? kMatCobbleBreak : m; };
    // the line with its word k (0: the keyword) replaced, the words joined by single spaces (numbers kept as written)
    auto setWord = [](const std::string& tail, const std::string& kw, size_t k, const std::string& w, const std::string& comment) {
        std::istringstream s(tail);
        std::vector<std::string> ws{kw};
        for (std::string x; s >> x;) ws.push_back(x);
        if (k < ws.size()) ws[k] = w;
        std::string o;
        for (auto& x : ws) { if (!o.empty()) o += ' '; o += x; }
        return comment.empty() ? o : o + " " + comment;
    };
    Vec3 groupSize(0, 0, 0);   // (the custom group the next part lines belong to)
    for (std::string& l : lines) {
        size_t rest = 0;
        const std::string kw = firstWord(l, &rest);
        if (kw.empty()) continue;
        std::string tail = l.substr(rest);
        const size_t hash = tail.find('#');
        const std::string comment = hash == std::string::npos ? "" : tail.substr(hash);
        if (hash != std::string::npos) tail.erase(hash);
        std::istringstream in(tail);
        float v[6];
        if (kw == "glass") { l = "breakableglass" + tail + comment; continue; }
        if (kw == "brick" || kw == "cobblestone") {
            if (in >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5] && thin(std::fabs(v[3] - v[0]), std::fabs(v[4] - v[1]), std::fabs(v[5] - v[2])))
                l = "breakable" + kw + tail + comment;
            continue;
        }
        int m = -1;
        if (kw == "wedge" || kw == "corner" || kw == "tetra" || kw == "cut") {   // (slopes: only glass broke)
            if (in >> m && m == kMatGlass) l = setWord(tail, kw, 1, std::to_string(kMatGlassBreak), comment);
        } else if (kw == "group") {
            std::string type; int dir = 0;
            if (!(in >> type >> m >> dir >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5])) continue;
            groupSize = {std::fabs(v[3] - v[0]), std::fabs(v[4] - v[1]), std::fabs(v[5] - v[2])};
            const float wall = std::min(0.25f, std::min(groupSize.x, std::min(groupSize.y, groupSize.z)) * 0.25f);   // (Map::groupBoxes)
            const bool brk = m == kMatGlass || (type == "room" && (m == kMatBrick || m == kMatCobble) && wall <= kBreakableThick + 1e-4f);
            if (brk) l = setWord(tail, kw, 2, std::to_string(breakable(m)), comment);
        } else if (kw == "part") {
            if (!(in >> m >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5])) continue;
            std::string shape;
            const bool box = !(in >> shape);
            const bool brk = m == kMatGlass || (box && (m == kMatBrick || m == kMatCobble) &&
                                                thin(std::fabs(v[3] - v[0]) * groupSize.x, std::fabs(v[4] - v[1]) * groupSize.y, std::fabs(v[5] - v[2]) * groupSize.z));
            if (brk) l = setWord(tail, kw, 1, std::to_string(breakable(m)), comment);
        }
    }
}

// 6 -> 7: the 333 new surface materials (139-471: new keywords only, so an older reader would fail on them; a format 6
// map has none, nothing to change)
static void upgradeMap6to7(std::vector<std::string>&) {}
// 7 -> 8: the 25 liquids (472-496: new keywords only)
static void upgradeMap7to8(std::vector<std::string>&) {}
// 8 -> 9: the 25 see-through looks (537-561: new keywords only)
static void upgradeMap8to9(std::vector<std::string>&) {}
// 9 -> 10: the 24 breakable blocks and 10 feature blocks (562-595: new keywords, a teleporter's "tp" after its box)
static void upgradeMap9to10(std::vector<std::string>&) {}
// 10 -> 11: mode spawn sets ("spawn koth-a ...": new spawn team words, which an older reader refuses; a format 10 map
// has none, nothing to change)
static void upgradeMap10to11(std::vector<std::string>&) {}
// 11 -> 12: a teleporter's speed ("tp <link> <face> walk", which an older reader refuses; a format 11 teleporter has
// none and carries all speed, as it still does without the word: nothing to change)
static void upgradeMap11to12(std::vector<std::string>&) {}
// 12 -> 13: a race map's default time ("racedefault <s>": a new keyword; a format 12 map has none, nothing to change)
static void upgradeMap12to13(std::vector<std::string>&) {}

static const MapUpgradeStep kMapUpgrades[] = {upgradeMap1to2, upgradeMap2to3, upgradeMap3to4, upgradeMap4to5, upgradeMap5to6, upgradeMap6to7, upgradeMap7to8, upgradeMap8to9, upgradeMap9to10, upgradeMap10to11, upgradeMap11to12, upgradeMap12to13};
static_assert(sizeof kMapUpgrades / sizeof kMapUpgrades[0] == kMapFormatVersion - 1,
              "a map format change needs its converter step in kMapUpgrades (owner rule)");

static_assert(kSpawnModes == MODE_COUNT, "a mode spawn set per mode (map.h)");
bool modeSetAllowed(int mode) { return mode >= 0 && mode < kSpawnModes && mode != MODE_RACE && mode != MODE_POINTS; }
std::string spawnTeamWord(int team) {
    static const char* sides[] = {"a", "b", "any", "race"};
    if (isModeSpawn(team)) return std::string(modeName(spawnSetMode(team))) + "-" + sides[spawnSide(team)];
    if (isStagedSpawn(team)) return std::string(spawnDefends(team) ? "def" : "att") + char('0' + spawnStage(team));
    return team >= 0 && team < 4 ? sides[team] : "any";
}
int spawnTeamFromWord(const std::string& w) {
    if (w == "a") return SPAWN_A;
    if (w == "b") return SPAWN_B;
    if (w == "any") return SPAWN_ANY;
    if (w == "race") return SPAWN_RACE;
    if (w.size() == 4 && (w.compare(0, 3, "att") == 0 || w.compare(0, 3, "def") == 0) && w[3] >= '0' && w[3] <= '0' + kMaxSpawnStage)
        return stagedSpawn(w[3] - '0', w[0] == 'd');
    const size_t dash = w.find('-');
    if (dash == std::string::npos) return -1;
    const std::string side = w.substr(dash + 1);
    const int mode = modeFromName(w.substr(0, dash).c_str());
    const int sd = side == "a" ? SPAWN_A : side == "b" ? SPAWN_B : side == "any" ? SPAWN_ANY : -1;
    if (sd < 0 || !modeSetAllowed(mode) || w.compare(0, dash, modeName(mode)) != 0) return -1;   // (lower case only, as written)
    return modeSpawn(mode, sd);
}

int mapTextVersion(const std::string& text) {
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const char* l = text.data() + p;
        while (l < text.data() + e && (*l == ' ' || *l == '\t')) l++;
        if (text.data() + e - l > 8 && !memcmp(l, "version", 7) && (l[7] == ' ' || l[7] == '\t')) {
            long v = strtol(l + 8, nullptr, 10);
            return v >= 1 && v <= 1000000 ? (int)v : 0;   // (0: a bad version line, left for the parser to refuse)
        }
        p = e + 1;
    }
    return 1;
}

bool upgradeMapText(const std::string& in, std::string* out) {
    int v = mapTextVersion(in);
    if (v < 1 || v >= kMapFormatVersion) return false;
    std::vector<std::string> lines = splitLines(in);
    for (int k = v; k < kMapFormatVersion; k++) kMapUpgrades[k - 1](lines);
    // the version line: replaced where it is, or added as the last line (so every line keeps its number: a parse error
    // in a converted map names the line of the file)
    std::string vl = "version " + std::to_string(kMapFormatVersion);
    bool placed = false;
    for (std::string& l : lines) if (firstWord(l) == "version") { l = vl; placed = true; break; }
    std::string o;
    o.reserve(in.size() + 16);
    for (size_t i = 0; i < lines.size(); i++) { o += lines[i]; if (i + 1 < lines.size() || (!in.empty() && in.back() == '\n')) o += '\n'; }
    if (!placed) { if (!o.empty() && o.back() != '\n') o += '\n'; o += vl + "\n"; }
    *out = std::move(o);
    return true;
}

MapUpgradeReport upgradeMapDir(const std::string& dirIn) {
    MapUpgradeReport rep;
    fs::path dir = utf8Path(dirIn);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return rep;
    std::set<std::string> failedBefore;   // "name size" of maps that didn't convert: not read again every start
    {
        std::ifstream f(dir / "old" / "failed.txt");
        std::string line;
        while (std::getline(f, line)) failedBefore.insert(line);
    }
    auto note = [&](const std::string& line) {
        rep.log.push_back(line);
        fs::create_directories(dir / "old", ec);
        std::ofstream(dir / "old" / "convert.log", std::ios::app) << line << "\n";
    };
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".qmap") continue;
        std::string name = e.path().filename().u8string();
        uint64_t size = e.file_size(ec);
        if (ec || size > (64u << 20)) { ec.clear(); continue; }   // (no map is that big: not ours to touch)
        std::string key = name + " " + std::to_string(size);
        if (failedBefore.count(key)) continue;
        std::ifstream f(e.path(), std::ios::binary);
        char part[4096];   // the version line is near the top (saved / generated maps) or the last line (converted ones)
        f.read(part, sizeof part);
        if (mapTextVersion(std::string(part, (size_t)f.gcount())) >= kMapFormatVersion) continue;
        if (size > sizeof part) {
            f.clear();
            f.seekg((std::streamoff)(size - sizeof part));
            f.read(part, sizeof part);
            if (mapTextVersion(std::string(part, (size_t)f.gcount())) >= kMapFormatVersion) continue;
        }
        f.clear();
        f.seekg(0);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        f.close();
        int was = mapTextVersion(text);
        std::string out, perr;
        Map m;
        if (!upgradeMapText(text, &out)) continue;
        if (!m.parse(out, &perr)) {
            rep.failed++;
            note(name + ": format " + std::to_string(was) + ", kept as it is: doesn't load after converting (" + perr + ")");
            std::ofstream(dir / "old" / "failed.txt", std::ios::app) << key << "\n";
            continue;
        }
        // the original into old/v<N>/ first (a copy: the map stays in place whatever happens), then the converted text
        fs::path backDir = dir / "old" / ("v" + std::to_string(was));
        fs::create_directories(backDir, ec);
        fs::path back = backDir / e.path().filename();
        for (int n = 1; fs::exists(back, ec); n++) back = backDir / (e.path().stem().u8string() + "-" + std::to_string(n) + ".qmap");
        fs::copy_file(e.path(), back, ec);
        if (ec || !writeFileAtomic(e.path().u8string(), out)) {
            rep.failed++;
            note(name + ": format " + std::to_string(was) + ", kept as it is: can't write (disk full?)");
            ec.clear();
            continue;
        }
        rep.converted++;
        note(name + ": converted from map format " + std::to_string(was) + " to " + std::to_string(kMapFormatVersion) + ", original in old/v" + std::to_string(was) + "/");
    }
    return rep;
}

std::string MapId::hex() const {
    static const char* d = "0123456789abcdef";
    std::string o(2 * sizeof b, '0');
    for (size_t i = 0; i < sizeof b; i++) { o[2 * i] = d[b[i] >> 4]; o[2 * i + 1] = d[b[i] & 15]; }
    return o;
}
std::string MapId::shortHex() const { return hex().substr(0, 2 * kShortBytes); }

MapId mapContentId(const std::string& text) {
    Sha256 h;
    const char* p = text.data();
    const char* e = p + text.size();
    while (p < e) {   // (the runs between CRs: a CRLF file is the same map as its LF copy)
        const char* cr = (const char*)memchr(p, '\r', (size_t)(e - p));
        const char* q = cr ? cr : e;
        h.update(p, (size_t)(q - p));
        p = cr ? q + 1 : e;
    }
    MapId id;
    h.digest(id.b);
    return id;
}

MapId mapEditId(const MapId& before, const std::string& op) {
    Sha256 h;
    h.update(before.b, sizeof before.b);
    h.update(op.data(), op.size());
    MapId id;
    h.digest(id.b);
    return id;
}

// renamed maps: old configs / votes for these still work
static std::string currentMapName(const std::string& requested) {
    return requested == "arena" ? "bastion" : requested == "tug" ? "canal" : requested;
}

std::string mapFilePath(const std::string& requested) {
    std::string mapName = currentMapName(requested);
    if (!validMapName(mapName)) return std::string();
    std::string path = userMapsDir() + "/" + mapName + ".qmap"; // yours (edited / made) first, then the shipped one
    std::error_code ec;
    if (!fs::exists(path, ec)) path = userDataPath("overrides/maps/" + mapName + ".qmap"); // your override (docs/ASSETS.md)
    if (!fs::exists(path, ec)) path = findDataPath("maps/" + mapName + ".qmap");
    if (!fs::exists(path, ec)) {   // a legacy map (the old pool), whatever LEGACY MAPS says: yours, then the shipped one
        std::string l = userLegacyMapsDir() + "/" + mapName + ".qmap";
        if (!fs::exists(l, ec)) l = findDataPath("maps/legacy/" + mapName + ".qmap");
        if (fs::exists(l, ec)) path = l;
    }
    return path;
}

bool Map::load(const std::string& requested, std::string* err) {
    std::string mapName = currentMapName(requested);
    if (!validMapName(mapName)) { if (err) *err = "bad map name: " + mapName; return false; }
    std::string path = mapFilePath(mapName);
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) { if (err) *err = "map not found: " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    name = mapName;
    return parse(ss.str(), err);
}

// Limits shared by the parser and the edit path: a map comes from untrusted servers too, so nothing in it may be
// big enough to blow up the collision grid, the bake or the brush index (cellBrushes is uint16_t).
static bool okCoord(float v) { return std::isfinite(v) && std::fabs(v) <= kMapExtent; }
// One line of a map, read the way std::istringstream reads it (same results, same failures) without the stream
// machinery: building a stream per line was most of the cost of parsing a big map (owner decision 1.5).
//   >> word    skips whitespace, takes up to the next whitespace
//   >> float   libstdc++'s num_get grammar ([sign] digits [. digits] [e [sign] digits], the longest such prefix),
//              converted by strtof (C locale: the game never changes it); nothing usable -> 0 and failed, out of range
//              -> +-FLT_MAX and failed
//   >> int     [sign] digits by strtoll; none -> 0 and failed, out of range -> INT_MAX / INT_MIN and failed
// As with a stream, a read at the end of the line fails and leaves its target alone, and once a read has failed every
// later one does too.
struct LineIn {
    const char* p;
    const char* e;
    bool failed = false;
    LineIn(const char* b, const char* end) : p(b), e(end) {}
    static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
    static bool digit(char c) { return c >= '0' && c <= '9'; }
    explicit operator bool() const { return !failed; }
    bool start() {   // the stream's sentry: skip whitespace; nothing left fails
        if (failed) return false;
        while (p < e && space(*p)) p++;
        if (p == e) { failed = true; return false; }
        return true;
    }
    LineIn& operator>>(std::string& w) {
        if (!start()) return *this;
        const char* b = p;
        while (p < e && !space(*p)) p++;
        w.assign(b, p);
        return *this;
    }
    // runs f (strtof / strtoll) on the accepted characters [b, q), terminated; second: it used all of them
    template <class F> static auto convert(const char* b, const char* q, F f) {
        char small[64];
        std::string big;
        size_t n = (size_t)(q - b);
        const char* s = small;
        if (n < sizeof small) { memcpy(small, b, n); small[n] = 0; } else { big.assign(b, q); s = big.c_str(); }
        char* end = nullptr;
        auto v = f(s, &end);
        return std::make_pair(v, n > 0 && end == s + n);
    }
    LineIn& operator>>(float& v) {
        if (!start()) return *this;
        const char* b = p;
        const char* q = p;
        bool neg = *q == '-';
        if (*q == '+' || *q == '-') q++;
        bool mantissa = false, exact = true;   // exact: the digits as an integer m < 2^24, and fewer than 11 decimals
        uint32_t m = 0;
        int decimals = 0;
        auto take = [&](char c) { m = m * 10 + (uint32_t)(c - '0'); exact = exact && m <= (1u << 24); };
        while (q < e && digit(*q)) { if (exact) take(*q); q++; mantissa = true; }
        if (q < e && *q == '.') { q++; while (q < e && digit(*q)) { if (exact) take(*q); decimals++; q++; mantissa = true; } }
        if (mantissa && q < e && (*q == 'e' || *q == 'E')) {
            exact = false;
            q++;
            if (q < e && (*q == '+' || *q == '-')) q++;
            while (q < e && digit(*q)) q++;
        }
        p = q;
        if (mantissa && exact && decimals <= 10) {
            // Clinger's fast path: m and 10^decimals are both exact floats, so one IEEE division rounds once, to the
            // same float strtof gives. (Nearly every number in a map takes it; strtof was most of the parse.)
            static const float pow10[] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f};
            float f = (float)m / pow10[decimals];
            v = neg ? -f : f;
            return *this;
        }
        auto r = convert(b, q, [](const char* s, char** end) { return strtof(s, end); });
        if (!r.second) { v = 0; failed = true; }
        else if (std::isinf(r.first)) { v = r.first > 0 ? FLT_MAX : -FLT_MAX; failed = true; }
        else v = r.first;
        return *this;
    }
    LineIn& operator>>(int& v) {
        if (!start()) return *this;
        const char* b = p;
        const char* q = p;
        if (*q == '+' || *q == '-') q++;
        const char* d = q;
        while (q < e && digit(*q)) q++;
        p = q;
        if (q == d) { v = 0; failed = true; return *this; }
        errno = 0;
        auto r = convert(b, q, [](const char* s, char** end) { return strtoll(s, end, 10); });
        if (errno == ERANGE || r.first > INT_MAX || r.first < INT_MIN) { v = *b == '-' ? INT_MIN : INT_MAX; failed = true; }
        else v = (int)r.first;
        return *this;
    }
    // std::getline(ls, s) after the keyword: the rest of the line as it is (at the end: fails, s left alone)
    bool rest(std::string& s) {
        if (failed || p == e) { failed = true; return false; }
        s.assign(p, e);
        p = e;
        return true;
    }
};

static bool readBox(LineIn& ls, AABB& b) {
    Vec3 a, c;
    if (!(ls >> a.x >> a.y >> a.z >> c.x >> c.y >> c.z)) return false;
    if (!okCoord(a.x) || !okCoord(a.y) || !okCoord(a.z) || !okCoord(c.x) || !okCoord(c.y) || !okCoord(c.z)) return false;
    b.mn = vmin(a, c);
    b.mx = vmax(a, c);
    return true;
}

bool Map::parse(const std::string& text, std::string* err) {
    try {
        if (parseText(text, err)) return true;
    } catch (const std::exception& e) { // a hostile map must fail cleanly, never take the game down
        if (err) *err = std::string("map rejected: ") + e.what();
    }
    std::string keepName = std::move(name);   // nothing of the bad text is left behind
    *this = Map();
    name = std::move(keepName);
    return false;
}

bool Map::parseText(const std::string& text, std::string* err) {
    std::string up;
    if (!upgradeMapText(text, &up)) return parseCurrent(text, err);
    int was = mapTextVersion(text);
    if (!parseCurrent(up, err)) return false;
    formatVersion = was;   // (what the file was written in; source is the converted text)
    return true;
}

bool Map::parseCurrent(const std::string& text, std::string* err) {
    {   // every field back to a fresh map's (title, sun, palette, void, ceiling, hold fire, ...), but its name
        std::string keepName = std::move(name);
        *this = Map();
        name = std::move(keepName);
    }
    formatVersion = 1;   // (a file without a "version" line)
    source = text;
    sourceStale = false;
    contentIdKnown_ = false;   // (hashed when it's asked for)
    editChained_ = false;
    int lineNo = 0;
    bounds = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    const char* next = text.data();
    const char* textEnd = text.data() + text.size();
    std::string kw;
    while (next < textEnd) {   // (as std::getline splits: lines end at '\n'; the last one needn't)
        const char* lineStart = next;
        const char* lineEnd = (const char*)memchr(lineStart, '\n', (size_t)(textEnd - lineStart));
        if (!lineEnd) lineEnd = textEnd;
        next = lineEnd < textEnd ? lineEnd + 1 : textEnd;
        lineNo++;
        const char* h = (const char*)memchr(lineStart, '#', (size_t)(lineEnd - lineStart));
        if (h) lineEnd = h;
        LineIn ls(lineStart, lineEnd);
        if (!(ls >> kw)) continue;
        bool ok = true;
        if (kw == "version") {   // "version N": the format the file is written in (none: 1). Newer ones still load:
            int v = 0;           // what this build doesn't know is skipped with a warning (below)
            ok = (bool)(ls >> v) && v >= 1;
            if (ok) formatVersion = v;
            if (ok && v > kMapFormatVersion)
                warnings.push_back("map format " + std::to_string(v) + " is newer than this build's (" + std::to_string(kMapFormatVersion) + "): parts it doesn't know are skipped");
        } else if (kw == "handmade") {   // made (or changed) by a person, not tools/make_maps.py: checked, never failed
            handmade = true;
        } else if (kw == "setting") {    // "setting <words>": the place the map is (every generated map has one)
            ok = ls.rest(setting);
            while (!setting.empty() && (setting[0] == ' ' || setting[0] == '\t')) setting.erase(0, 1);
            while (!setting.empty() && LineIn::space(setting.back())) setting.pop_back();
            ok = ok && !setting.empty() && setting.size() <= 80;
        } else if (kw == "stairs") {     // "stairs x0 z0 x1 z1 base top x|z rising solid": a staircase built of boxes
            Stairs st;                   // (tools/make_maps.py records each; quark_tests walks every one)
            std::string axis;
            int solid = 0;
            ok = (bool)(ls >> st.x0 >> st.z0 >> st.x1 >> st.z1 >> st.base >> st.top >> axis >> st.rising >> solid) &&
                 okCoord(st.x0) && okCoord(st.z0) && okCoord(st.x1) && okCoord(st.z1) && okCoord(st.base) && okCoord(st.top) &&
                 std::isfinite(st.rising) && (axis == "x" || axis == "z") && (solid == 0 || solid == 1) && stairs.size() < kMaxMapStairs;
            st.axis = axis == "x" ? 'x' : 'z';
            st.solid = solid == 1;
            if (ok) stairs.push_back(st);
        } else if (kw == "title") {
            ls.rest(title);
            ok = cleanMapTitle(title);   // (the same rule as the editor's title edit)
        } else if (materialKeyword(kw) >= 0) {
            Brush b;
            ok = readBox(ls, b.box);
            b.material = materialKeyword(kw);
            { std::string w; if (ok && b.material == kMatTeleport && (ls >> w)) ok = w == "tp" && readTp(ls, b); }   // (a teleporter's link and face)
            hasWallbang |= wallbangBrush(b);
            if (ok && brushes.size() >= kMaxMapBrushes) { if (err) *err = "too many boxes (limit " + std::to_string(kMaxMapBrushes) + ")"; return false; }
            if (ok) {
                brushes.push_back(b);
                if (!solidMaterial(b.material)) continue; // far scenery / water / lava surfaces: not part of the playable bounds
                bounds.mn = vmin(bounds.mn, b.box.mn);
                bounds.mx = vmax(bounds.mx, b.box.mx);
            }
        } else if (kw == "wedge" || kw == "corner" || kw == "tetra" || kw == "cut") {   // "wedge|corner|tetra <material> <dir + 4 * up> x0 y0 z0 x1 y1 z1", "cut <material> <n> <n planes: a b c d> x0 y0 z0 x1 y1 z1" (format 5; map.h's Brush)
            Brush b; int dir = -1;
            b.shape = (uint8_t)slopeFromWord(kw);
            if (b.shape == BRUSH_CUT) ok = (bool)(ls >> b.material) && validMapMaterial(b.material) && readCuts(ls, b) && readBox(ls, b.box);
            else {
                ok = (bool)(ls >> b.material >> dir) && validMapMaterial(b.material) && dir >= 0 && dir < 4 * kSlopeUps && readBox(ls, b.box);
                b.dir = (uint8_t)(std::max(0, dir) % 4);
                b.up = (uint8_t)(std::max(0, dir) / 4);
            }
            { std::string w; if (ok && b.material == kMatTeleport && (ls >> w)) ok = w == "tp" && readTp(ls, b); }
            if (ok && brushes.size() >= kMaxMapBrushes) { if (err) *err = "too many boxes (limit " + std::to_string(kMaxMapBrushes) + ")"; return false; }
            if (ok) {
                hasWallbang |= wallbangBrush(b);
                brushes.push_back(b);
                if (!solidMaterial(b.material)) continue;
                bounds.mn = vmin(bounds.mn, b.box.mn);
                bounds.mx = vmax(bounds.mx, b.box.mx);
            }
        } else if (kw == "spawn") {
            std::string team;
            SpawnPoint s;
            ok = (bool)(ls >> team >> s.pos.x >> s.pos.y >> s.pos.z) && okCoord(s.pos.x) && okCoord(s.pos.y) && okCoord(s.pos.z) && spawns.size() < kMaxMapSpawns;
            float yawDeg = 0;
            ls >> yawDeg;
            if (!std::isfinite(yawDeg)) ok = false;
            s.yaw = deg2rad(yawDeg);
            s.team = spawnTeamFromWord(team);
            if (s.team < 0) ok = false;   // (an unknown team used to become "any")
            if (ok) spawns.push_back(s);
        } else if (kw == "site") {
            AABB b; ok = readBox(ls, b) && bombSites.size() < kMaxMapSites; if (ok) bombSites.push_back(b);
        } else if (kw == "cartcheck") {   // a payload checkpoint this far along the track (0..1)
            float f; ok = (bool)(ls >> f) && f > 0 && f < 1 && cartChecks.size() < kMaxMapCartChecks;
            if (ok) { cartChecks.push_back(f); std::sort(cartChecks.begin(), cartChecks.end()); }
        } else if (kw == "point") {
            AABB b; ok = readBox(ls, b) && points.size() < kMaxMapPoints; if (ok) points.push_back(b);
        } else if (kw == "hill") {
            if (hasHill) { if (err) *err = "line " + std::to_string(lineNo) + ": a second hill (a map has one)"; return false; }
            ok = readBox(ls, hill); hasHill = ok;
        } else if (kw == "ladder") {
            AABB b; ok = readBox(ls, b) && b.mx.y - b.mn.y <= kMaxObjectHeight && ladders.size() < kMaxMapLadders;
            std::string flag;
            if (ok) { ladders.push_back(b); ladderAuto.push_back(ls >> flag && flag == "auto"); }
        } else if (kw == "group") {
            std::string t; Group g;   // (a group of a type this build doesn't know would lose its boxes: an error, not skipped)
            ok = (bool)(ls >> t >> g.material >> g.dir) && readBox(ls, g.box) && validMapMaterial(g.material) && g.dir >= 0 && g.dir < 4 && g.box.mx.y - g.box.mn.y <= kMaxObjectHeight && groups.size() < kMaxMapGroups;
            g.type = t == "room" ? GROUP_ROOM : t == "stairs" ? GROUP_STAIRS : GROUP_CUSTOM;
            ok = ok && (t == "room" || t == "stairs" || t == "custom");
            if (ok) groups.push_back(g);
        } else if (kw == "part") {   // "part <material> <6 fractions>", then "wedge|corner <dir + 4 * up>" for a slope (format 5)
            Brush b; ok = !groups.empty() && groups.back().type == GROUP_CUSTOM && groups.back().parts.size() < kMaxGroupParts && (bool)(ls >> b.material) && validMapMaterial(b.material) && readBox(ls, b.box);
            std::string shape;
            if (ok && ls >> shape) {
                int dir = -1;
                if (shape == "cut") { b.shape = BRUSH_CUT; ok = readCuts(ls, b); }
                else {
                    ok = (shape == "wedge" || shape == "corner" || shape == "tetra") && (bool)(ls >> dir) && dir >= 0 && dir < 4 * kSlopeUps;
                    if (ok) b.shape = (uint8_t)slopeFromWord(shape);
                    b.dir = (uint8_t)(std::max(0, dir) % 4);
                    b.up = (uint8_t)(std::max(0, dir) / 4);
                }
            }
            if (ok) groups.back().parts.push_back(b);
        } else if (kw == "checkpoint") {
            AABB b; ok = readBox(ls, b) && checkpoints.size() < kMaxMapCheckpoints; if (ok) checkpoints.push_back(b);
        } else if (kw == "path") {
            Vec3 p; ok = (bool)(ls >> p.x >> p.y >> p.z) && okCoord(p.x) && okCoord(p.y) && okCoord(p.z) && payloadPath.size() < kMaxMapPath; if (ok) payloadPath.push_back(p);
        } else if (kw == "for") {   // a mode this build doesn't have (a newer map): built for none in particular, warned
            std::string mode;
            ok = (bool)(ls >> mode);
            if (ok) preferredMode = modeFromName(mode.c_str());
            if (ok && preferredMode < 0) warnings.push_back("line " + std::to_string(lineNo) + ": built for '" + mode + "', a mode this build doesn't have");
        } else if (kw == "ranked") {   // (names this build doesn't know are skipped: never ranked here)
            std::string ln;
            while (ls >> ln) {
                int v = arenaVariantFromName(ln.c_str());
                if (v >= 0) rankedLadders |= (uint16_t)(1u << v);
                else warnings.push_back("line " + std::to_string(lineNo) + ": ranked '" + ln + "' isn't a ladder this build has (skipped)");
            }
        } else if (kw == "modes") {    // (a future mode in the list is skipped, so older builds can still join the map)
            std::string mn;
            while (ls >> mn) {
                int m = modeFromName(mn.c_str());
                if (m >= 0) allowedModes |= 1u << m;
                else warnings.push_back("line " + std::to_string(lineNo) + ": mode '" + mn + "' isn't one this build has (skipped)");
            }
        } else if (kw == "light") {
            Light l;
            ok = (bool)(ls >> l.pos.x >> l.pos.y >> l.pos.z >> l.radius) && okCoord(l.pos.x) && okCoord(l.pos.y) && okCoord(l.pos.z);
            ls >> l.intensity;
            ok = ok && validMapLight(l.radius, l.intensity) && lights.size() < kMaxMapLights;
            if (ok) lights.push_back(l);
        } else if (kw == "indoor") { // "indoor <ceiling y>": roofed all over (menu / shot cameras stay under the roof)
            ok = (bool)(ls >> ceilingY) && okCoord(ceilingY); indoor = ok;
        } else if (kw == "holdfire") {
            ok = (bool)(ls >> holdFire) && holdFire >= 0 && holdFire <= 30;
        } else if (kw == "racedefault") {   // (map format 13) a race map's default time on the ranked race total
            ok = (bool)(ls >> raceDefault) && raceDefault >= 1 && raceDefault <= 3600;
            if (!ok) raceDefault = 0;
        } else if (kw == "void") {
            ok = (bool)(ls >> voidY) && okCoord(voidY); hasVoid = ok;
        } else if (kw == "palette") {
            std::string k; Vec3 c;
            while (ok && ls >> k) {
                ok = (bool)(ls >> c.x >> c.y >> c.z) && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
                if (k == "base") palBase = c; else if (k == "accent") palAccent = c; else if (k == "metal") palMetal = c;
                else if (k == "glow") palGlow = c; else if (k == "sky") palSky = c;
                else if (ok) warnings.push_back("line " + std::to_string(lineNo) + ": palette colour '" + k + "' unknown (skipped)");
            }
            hasPalette = ok;
        } else if (kw == "sun") {
            Vec3 d; ok = (bool)(ls >> d.x >> d.y >> d.z) && okCoord(d.x) && okCoord(d.y) && okCoord(d.z) && length(d) > 1e-6f;
            if (ok) sunDir = std::fabs(length(d) - 1.0f) < 1e-6f ? d : normalize(d);   // (a saved, unit sun stays bit-exact)
        } else {   // a keyword this build doesn't know (a newer format's): skipped with a warning. The ones above are
                   // checked strictly: a bad box, spawn, zone or path is an error, never guessed at
            warnings.push_back("line " + std::to_string(lineNo) + ": unknown '" + kw + "' skipped");
        }
        if (!ok) {
            if (err) *err = "parse error on line " + std::to_string(lineNo) + ": " + std::string(lineStart, lineEnd);
            return false;
        }
    }
    if (!validate(err)) return false;   // (before ladders and groups make their boxes from these)
    genLadderBrushes();
    if (brushes.empty() || spawns.empty()) {
        if (err) *err = "map needs at least one box and one spawn";
        return false;
    }
    if (!validate(err)) return false;   // and what they made
    for (auto& b : brushes) if (b.generated && solidMaterial(b.material)) { bounds.mn = vmin(bounds.mn, b.box.mn); bounds.mx = vmax(bounds.mx, b.box.mx); }
    if (!buildGrid()) { if (err) *err = "too much geometry for the collision grid"; return false; }
    return true;
}

bool Map::humanMade() const {
    static const char kGenerated[] = "# generated by tools/make_maps.py";   // (make_maps.py's first line)
    return handmade || sourceText().compare(0, sizeof kGenerated - 1, kGenerated) != 0;
}

const MapId& Map::contentId() const {
    const std::string& t = sourceText();
    if (!contentIdKnown_) { contentId_ = mapContentId(t); contentIdKnown_ = true; }
    return contentId_;
}

bool Map::validate(std::string* err) const {
    auto fail = [&](const std::string& why) { if (err) *err = why; return false; };
    auto num = [](float v) { return std::isfinite(v) && std::fabs(v) <= kMapExtent; };
    auto vec = [&](const Vec3& v) { return num(v.x) && num(v.y) && num(v.z); };
    auto box = [&](const AABB& b) { return vec(b.mn) && vec(b.mx); };
    if (brushes.size() > kMaxMapBrushes)
        return fail("too many boxes: " + std::to_string(brushes.size()) + " with ladders' and groups' (limit " + std::to_string(kMaxMapBrushes) + ")");
    if (spawns.size() > kMaxMapSpawns || lights.size() > kMaxMapLights || ladders.size() > kMaxMapLadders || groups.size() > kMaxMapGroups ||
        bombSites.size() > kMaxMapSites || checkpoints.size() > kMaxMapCheckpoints || payloadPath.size() > kMaxMapPath ||
        points.size() > kMaxMapPoints || cartChecks.size() > kMaxMapCartChecks)
        return fail("too many spawns, lights, ladders, groups, sites, checkpoints, path points or capture points");
    for (auto& b : brushes) if (!box(b.box) || !validMapMaterial(b.material)) return fail("a box is out of range (coordinates within 4096 m)");
    for (auto& s : spawns) if (!vec(s.pos) || !std::isfinite(s.yaw)) return fail("a spawn is out of range");
    for (auto& l : lights)
        if (!vec(l.pos) || !validMapLight(l.radius, l.intensity)) return fail("a light is out of range (radius 0.5-100, intensity above 0, at most 10)");
    for (auto& l : ladders) if (!box(l) || l.mx.y - l.mn.y > kMaxObjectHeight) return fail("a ladder is out of range (at most 64 m tall)");
    for (auto& g : groups) {
        if (!box(g.box) || g.box.mx.y - g.box.mn.y > kMaxObjectHeight || g.parts.size() > kMaxGroupParts) return fail("a group is out of range (at most 64 m tall, 1024 parts)");
        if (g.type != GROUP_CUSTOM && !g.parts.empty()) return fail("only a custom group has parts");
    }
    for (auto* list : {&bombSites, &checkpoints, &points}) for (auto& b : *list) if (!box(b)) return fail("a bomb site, checkpoint or capture point is out of range");
    for (auto& p : payloadPath) if (!vec(p)) return fail("a payload path point is out of range");
    if ((hasHill && !box(hill)) || (hasVoid && !num(voidY)) || (indoor && !num(ceilingY)) || !vec(sunDir)) return fail("the hill, void, ceiling or sun is out of range");
    return true;
}

bool cleanMapTitle(std::string& t) {
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    size_t a = 0, e = t.size();
    while (a < e && space(t[a])) a++;
    while (e > a && space(t[e - 1])) e--;
    t = t.substr(a, e - a);
    if (t.size() > kMaxMapTitle) return false;
    for (char c : t) if ((unsigned char)c < 32 || (unsigned char)c > 126 || c == '#') return false;
    return true;
}

// v with the fewest digits (6 to 9 significant) that parse back to exactly v: "12.5", not "12.5000000", but an edit's
// 1000.125 stays 1000.125 (6 digits saved it as 1000.12, and a client that downloaded the saved map collided
// differently from one that applied the edit: FINAL_REVIEW 5.5). Map::parse reads numbers exactly as strtof does.
static const char* exactNum(char* out, size_t n, float v) {
    for (int digits = 6; digits < 9; digits++) {
        snprintf(out, n, "%.*g", digits, v);
        if (strtof(out, nullptr) == v) return out;
    }
    snprintf(out, n, "%.9g", v);   // (9 significant digits always round-trip a float)
    return out;
}
// A spawn's yaw in degrees, with the digits deg2rad needs to give back exactly these radians (when some do).
static const char* exactYawDeg(char* out, size_t n, float yaw) {
    float deg = yaw * (180.0f / PI);
    for (int digits = 6; digits <= 9; digits++) {
        snprintf(out, n, "%.*g", digits, deg);
        if (deg2rad(strtof(out, nullptr)) == yaw) return out;
    }
    for (float d : {std::nextafter(deg, -1e9f), std::nextafter(deg, 1e9f)}) {   // (radians -> degrees rounded off by one)
        snprintf(out, n, "%.9g", d);
        if (deg2rad(strtof(out, nullptr)) == yaw) return out;
    }
    snprintf(out, n, "%.9g", deg);
    return out;
}

std::string Map::serialize() const {
    std::string o = "# made with the quark map editor\nversion " + std::to_string(kMapFormatVersion) + "\nhandmade\n";
    char b[320];
    char nb[8][24];
    auto box = [&](const char* kw, const AABB& a) {
        snprintf(b, sizeof b, "%s %s %s %s %s %s %s\n", kw, exactNum(nb[0], 24, a.mn.x), exactNum(nb[1], 24, a.mn.y), exactNum(nb[2], 24, a.mn.z),
                 exactNum(nb[3], 24, a.mx.x), exactNum(nb[4], 24, a.mx.y), exactNum(nb[5], 24, a.mx.z));
        o += b;
    };
    auto nums = [&](int count, const float* v) {   // " v0 v1 ..." (count <= 6)
        for (int i = 0; i < count; i++) { o += ' '; o += exactNum(nb[i], 24, v[i]); }
    };
    if (!title.empty()) o += "title " + title + "\n";
    if (!setting.empty()) o += "setting " + setting + "\n";
    if (preferredMode >= 0) o += std::string("for ") + modeName(preferredMode) + "\n";
    if (rankedLadders) {
        o += "ranked";
        for (int v = 0; v < 16; v++) if (rankedLadders >> v & 1) o += std::string(" ") + arenaVariant(v).name;
        o += "\n";
    }
    if (allowedModes) {
        o += "modes";
        for (int m = 0; m < MODE_COUNT; m++) if (allowedModes >> m & 1) o += std::string(" ") + modeName(m);
        o += "\n";
    }
    { float v[3] = {sunDir.x, sunDir.y, sunDir.z}; o += "sun"; nums(3, v); o += "\n\n"; }
    if (hasVoid) { o += "void"; nums(1, &voidY); o += "\n"; }
    if (indoor) { o += "indoor"; nums(1, &ceilingY); o += "\n"; }
    if (holdFire > 0) { o += "holdfire"; nums(1, &holdFire); o += "\n"; }
    if (raceDefault > 0) { o += "racedefault"; nums(1, &raceDefault); o += "\n"; }
    if (hasPalette) {
        auto c = [&](const char* k, Vec3 v) { float f[3] = {v.x, v.y, v.z}; o += ' '; o += k; nums(3, f); };
        o += "palette"; c("base", palBase); c("accent", palAccent); c("metal", palMetal); c("glow", palGlow); c("sky", palSky); o += "\n\n";
    }
    auto tp = [&](const Brush& br) {   // (a teleporter: its link and face at the end of its line)
        if (br.material != kMatTeleport || o.empty() || o.back() != '\n') return;
        o.pop_back();
        o += " tp " + std::to_string((int)br.link) + " " + std::to_string((int)br.exitFace) + tpSpeedText(br) + "\n";
    };
    for (auto& br : brushes) {
        if (br.generated) continue;
        if (br.shape == BRUSH_BOX) { box(kMapMaterialWords[br.material], br.box); tp(br); continue; }
        if (br.shape == BRUSH_CUT) {   // (its planes can be long: built as a string)
            o += "cut " + std::to_string(br.material) + cutText(br);
            for (int k = 0; k < 6; k++) { o += ' '; o += exactNum(nb[k], 24, k < 3 ? br.box.mn[k] : br.box.mx[k - 3]); }
            o += '\n';
            tp(br);
            continue;
        }
        const std::string cuts = " " + std::to_string((int)br.dir + 4 * br.up);
        snprintf(b, sizeof b, "%s %d%s %s %s %s %s %s %s\n", slopeWord(br.shape), br.material, cuts.c_str(),
                 exactNum(nb[0], 24, br.box.mn.x), exactNum(nb[1], 24, br.box.mn.y), exactNum(nb[2], 24, br.box.mn.z),
                 exactNum(nb[3], 24, br.box.mx.x), exactNum(nb[4], 24, br.box.mx.y), exactNum(nb[5], 24, br.box.mx.z));
        o += b;
        tp(br);
    }
    o += "\n";
    for (auto& sp : spawns) {
        std::string tn = spawnTeamWord(sp.team);
        float p[3] = {sp.pos.x, sp.pos.y, sp.pos.z};
        o += "spawn " + tn;
        nums(3, p);
        o += ' ';
        o += exactYawDeg(nb[3], 24, sp.yaw);
        o += '\n';
    }
    for (auto& a : bombSites) box("site", a);
    if (hasHill) box("hill", hill);
    for (auto& a : points) box("point", a);
    for (float f : cartChecks) { o += "cartcheck"; nums(1, &f); o += "\n"; }
    for (size_t k = 0; k < ladders.size(); k++) {
        box("ladder", ladders[k]);
        if (k < ladderAuto.size() && ladderAuto[k]) { o.pop_back(); o += " auto\n"; }
    }
    static const char* groupTypes[] = {"custom", "room", "stairs"};
    for (auto& g : groups) {
        char kw[48];
        snprintf(kw, sizeof kw, "group %s %d %d", groupTypes[g.type], g.material, g.dir);
        box(kw, g.box);
        for (auto& p : g.parts) {
            snprintf(kw, sizeof kw, "part %d", p.material);
            box(kw, p.box);
            if (p.shape == BRUSH_CUT) { o.pop_back(); o += " cut" + cutText(p) + "\n"; }
            else if (p.shape != BRUSH_BOX) { o.pop_back(); snprintf(kw, sizeof kw, " %s %d\n", slopeWord(p.shape), (int)p.dir + 4 * p.up); o += kw; }
        }
    }
    for (auto& st : stairs) {   // (kept as they were: the editor doesn't move them with the boxes)
        snprintf(b, sizeof b, "stairs %s %s %s %s %s %s %c %s %d\n", exactNum(nb[0], 24, st.x0), exactNum(nb[1], 24, st.z0), exactNum(nb[2], 24, st.x1),
                 exactNum(nb[3], 24, st.z1), exactNum(nb[4], 24, st.base), exactNum(nb[5], 24, st.top), st.axis, exactNum(nb[6], 24, st.rising), st.solid ? 1 : 0);
        o += b;
    }
    for (auto& a : checkpoints) box("checkpoint", a);
    for (auto& p : payloadPath) { float v[3] = {p.x, p.y, p.z}; o += "path"; nums(3, v); o += "\n"; }
    for (auto& l : lights) { float v[5] = {l.pos.x, l.pos.y, l.pos.z, l.radius, l.intensity}; o += "light"; nums(5, v); o += "\n"; }
    return o;
}

std::string cutText(const Brush& b) {
    std::string o = " " + std::to_string((int)b.ncut);
    char nb[32];
    for (int k = 0; k < b.ncut && k < kMaxCuts; k++)
        for (int j = 0; j < 4; j++) { o += ' '; o += exactNum(nb, sizeof nb, b.cut[k][j]); }
    return o;
}

static std::string fmtBox(const AABB& b) {
    char t[160];
    snprintf(t, sizeof t, "%.9g %.9g %.9g %.9g %.9g %.9g", b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
    return t;
}

std::string describeMapObject(const Map& m, char kind, const char* verb, int i) {
    char t[220];
    switch (kind) {
        case 'b': {   // (a wedge or corner: its shape and dir + 4 * up after the box, "w 2" / "c 4")
            const Brush& br = m.brushes[i];
            snprintf(t, sizeof t, "b %s %d %d %s", verb, i, br.material, fmtBox(br.box).c_str());
            const std::string tp = br.material == kMatTeleport ? " p " + std::to_string((int)br.link) + " " + std::to_string((int)br.exitFace) + tpSpeedText(br) : "";
            if (br.shape == BRUSH_CUT) return std::string(t) + " x" + cutText(br) + tp;
            if (br.shape != BRUSH_BOX) { const size_t l = strlen(t); snprintf(t + l, sizeof t - l, " %c %d", slopeLetter(br.shape), (int)br.dir + 4 * br.up); }
            return std::string(t) + tp;
        }
        case 's': { auto& sp = m.spawns[i]; snprintf(t, sizeof t, "s %s %d %d %.9g %.9g %.9g %.9g", verb, i, sp.team, sp.pos.x, sp.pos.y, sp.pos.z, sp.yaw * 180.0f / PI); break; }
        case 'l': { auto& l = m.lights[i]; snprintf(t, sizeof t, "l %s %d %.9g %.9g %.9g %.9g %.9g", verb, i, l.pos.x, l.pos.y, l.pos.z, l.radius, l.intensity); break; }
        case 'z': snprintf(t, sizeof t, "z %s %d %s", verb, i, fmtBox(m.bombSites[i]).c_str()); break;
        case 'c': snprintf(t, sizeof t, "c %s %d %s", verb, i, fmtBox(m.checkpoints[i]).c_str()); break;
        case 'p': { auto& p = m.payloadPath[i]; snprintf(t, sizeof t, "p %s %d %.9g %.9g %.9g", verb, i, p.x, p.y, p.z); break; }
        case 'h': snprintf(t, sizeof t, "h set 0 %d %s", m.hasHill ? 1 : 0, fmtBox(m.hill).c_str()); break;
        case 'o': snprintf(t, sizeof t, "o %s %d %s", verb, i, fmtBox(m.points[i]).c_str()); break;
        case 'd': snprintf(t, sizeof t, "d %s %d %d %s", verb, i, i < (int)m.ladderAuto.size() ? m.ladderAuto[i] : 0, fmtBox(m.ladders[i]).c_str()); break;
        case 'g': { auto& g = m.groups[i]; snprintf(t, sizeof t, "g %s %d %d %d %d %s", verb, i, g.type, g.material, g.dir, fmtBox(g.box).c_str()); break; }
        default: t[0] = 0;
    }
    return t;
}

bool applyMapEdit(Map& m, const std::string& op) {
    m.resetDestruction();   // (edits name brushes by index: on the map as it was)
    std::istringstream in(op);
    std::string kind, verb;
    if (!(in >> kind)) return false;
    if (kind == "t") {
        std::string title;
        std::getline(in, title);
        if (!cleanMapTitle(title)) return false;
        m.title = title;
        m.sourceStale = true;
        return true;
    }
    if (kind == "m") {
        uint32_t mask;
        if (!(in >> mask) || mask >= (1u << MODE_COUNT)) return false;
        m.allowedModes = mask;
        m.sourceStale = true;
        return true;
    }
    if (kind == "f") {
        int f;
        if (!(in >> f) || f < -1 || f >= MODE_COUNT) return false;
        m.preferredMode = f;
        m.sourceStale = true;
        return true;
    }
    int i;
    if (!(in >> verb >> i) || (verb != "ins" && verb != "set" && verb != "del" && !(verb == "part" && kind == "g"))) return false;
    if (kind == "b") {   // generated boxes (ladders' rails, groups' parts) sit after the editable ones and aren't edited directly
        int editable = (int)m.editableBrushes();
        if (i < 0 || i > editable || (verb != "ins" && i == editable) || (verb == "del" && editable <= 1)) return false;
    }
    auto okNum = [](float v) { return std::isfinite(v) && std::fabs(v) <= kMapExtent; };
    auto readVec = [&](Vec3& v) { return (bool)(in >> v.x >> v.y >> v.z) && okNum(v.x) && okNum(v.y) && okNum(v.z); };
    auto readBox = [&](AABB& b) {
        Vec3 a, c;
        if (!readVec(a) || !readVec(c)) return false;
        b.mn = vmin(a, c); b.mx = vmax(a, c);
        return true;
    };
    // after a brush's or a group part's box: nothing (a box), or "w <dir + 4 * up>" / "c <dir + 4 * up>" (a wedge /
    // corner, map format 5)
    auto readShape = [&](Brush& b) {   // (then "p <link> <face> [walk]": a teleporter's, map format 10 / 12)
        std::string shape;
        if (!(in >> shape)) return true;
        if (shape == "p") return readTp(in, b);
        auto tp = [&] { std::string w; return !(in >> w) || (w == "p" && readTp(in, b)); };
        int dir = -1;
        if (shape == "x") { b.shape = BRUSH_CUT; return readCuts(in, b) && tp(); }
        if (!((shape == "w" || shape == "c" || shape == "t") && (bool)(in >> dir) && dir >= 0 && dir < 4 * kSlopeUps)) return false;
        b.shape = (uint8_t)slopeFromWord(shape);
        b.dir = (uint8_t)(dir % 4);
        b.up = (uint8_t)(dir / 4);
        return tp();
    };
    // generic ins / set / del on one of the lists
    auto edit = [&](auto& vec, auto read, size_t minSize, size_t maxSize) {
        using T = typename std::decay_t<decltype(vec)>::value_type;
        size_t n = vec.size();
        if (verb == "del") {
            if (i < 0 || (size_t)i >= n || n <= minSize) return false;
            vec.erase(vec.begin() + i);
            return true;
        }
        T v{};
        if (!read(v)) return false;
        if (verb == "ins") {
            if (i < 0 || (size_t)i > n || n >= maxSize) return false;
            vec.insert(vec.begin() + i, v);
        } else {
            if (i < 0 || (size_t)i >= n) return false;
            vec[i] = v;
        }
        return true;
    };
    bool ok = false;
    // Brushes and custom group parts (what the editor sends by the hundred) change the map in place: checked first,
    // then one brush and the grid cells it touches are updated (Map::changeBrush). The same checks as a full rebuild:
    // sizes, ranges, the grid's limit.
    if (kind == "b") {
        size_t n = m.brushes.size();
        if (verb == "del") ok = i >= 0 && (size_t)i < n && n > 1 && m.eraseBrush((size_t)i);
        else {
            Brush b;
            if (!((bool)(in >> b.material) && validMapMaterial(b.material) && readBox(b.box))) return false;
            if (!readShape(b)) return false;
            if (verb == "ins") ok = i >= 0 && (size_t)i <= n && n < kMaxMapBrushes && m.insertBrush((size_t)i, b);
            else ok = i >= 0 && (size_t)i < n && m.setBrush((size_t)i, b);
        }
        if (ok) m.sourceStale = true;
        return ok;
    }
    if (kind == "g" && verb == "part") {   // (a slope part: "w|c <dir + 4 * up>" after its box, as for a brush)
        Brush part;
        if (!(i >= 0 && (size_t)i < m.groups.size() && m.groups[i].type == Map::GROUP_CUSTOM && m.groups[i].parts.size() < kMaxGroupParts
              && (bool)(in >> part.material) && validMapMaterial(part.material) && readBox(part.box) && readShape(part)))
            return false;
        // its box in the world goes right after group i's others: the generated brushes are the ladders' rails and
        // rungs, then each group's boxes in order (genLadderBrushes)
        size_t after = 0;
        std::vector<Brush> tmp;
        for (size_t j = (size_t)i + 1; j < m.groups.size(); j++) {
            if (m.groups[j].type == Map::GROUP_CUSTOM) { after += m.groups[j].parts.size(); continue; }
            tmp.clear();
            Map::groupBoxes(m.groups[j], tmp);
            after += tmp.size();
        }
        Map::Group one = m.groups[i];
        one.parts.assign(1, part);
        tmp.clear();
        Map::groupBoxes(one, tmp);
        Brush w = tmp[0];
        w.generated = true;
        auto num = [](float v) { return std::isfinite(v) && std::fabs(v) <= kMapExtent; };   // (validate's range)
        if (!(num(w.box.mn.x) && num(w.box.mn.y) && num(w.box.mn.z) && num(w.box.mx.x) && num(w.box.mx.y) && num(w.box.mx.z))) return false;
        if (m.brushes.size() + 1 > kMaxMapBrushes || after > m.brushes.size()) return false;
        m.groups[i].parts.push_back(part);
        if (!m.insertBrush(m.brushes.size() - after, w)) { m.groups[i].parts.pop_back(); return false; }
        m.sourceStale = true;
        return true;
    }
    // Everything else either can't break the map's limits beyond what its reader checks (spawns, lights, sites,
    // checkpoints, path, points, the hill: each list's cap and each value's range are checked as it's read), or
    // remakes generated brushes (ladders, groups): those go through a full rebuild, undone from a copy if it fails.
    Map before;
    bool full = kind == "d" || kind == "g";
    if (full) { std::string keep = std::move(m.source); before = m; m.source = std::move(keep); }
    if (kind == "s") ok = edit(m.spawns, [&](SpawnPoint& sp) {
        float yaw;
        if (!(in >> sp.team) || sp.team < 0 || sp.team > kMaxSpawnTeam || (isModeSpawn(sp.team) && !modeSetAllowed(spawnSetMode(sp.team))) || !readVec(sp.pos) || !(in >> yaw) || !okNum(yaw)) return false;
        sp.yaw = deg2rad(yaw);
        return true;
    }, 1, kMaxMapSpawns);
    else if (kind == "l") ok = edit(m.lights, [&](Map::Light& l) {
        return readVec(l.pos) && (bool)(in >> l.radius >> l.intensity) && validMapLight(l.radius, l.intensity);
    }, 0, kMaxMapLights);
    else if (kind == "z") ok = edit(m.bombSites, readBox, 0, kMaxMapSites);
    else if (kind == "c") { ok = edit(m.checkpoints, readBox, 0, kMaxMapCheckpoints); m.buildRace(); }
    else if (kind == "p") ok = edit(m.payloadPath, readVec, 0, kMaxMapPath);
    else if (kind == "o") ok = edit(m.points, readBox, 0, kMaxMapPoints);
    else if (kind == "d") { // ladders: the volume and whether it draws its own rails / rungs (kept in step)
        m.ladderAuto.resize(m.ladders.size(), 0);
        if (verb == "del") {
            ok = i >= 0 && (size_t)i < m.ladders.size();
            if (ok) { m.ladders.erase(m.ladders.begin() + i); m.ladderAuto.erase(m.ladderAuto.begin() + i); }
        } else {
            int au = 0;
            AABB b;
            ok = (bool)(in >> au) && readBox(b) && b.mx.y - b.mn.y <= kMaxObjectHeight;
            if (ok && verb == "ins") { ok = i >= 0 && (size_t)i <= m.ladders.size() && m.ladders.size() < kMaxMapLadders; if (ok) { m.ladders.insert(m.ladders.begin() + i, b); m.ladderAuto.insert(m.ladderAuto.begin() + i, au != 0); } }
            else if (ok) { ok = i >= 0 && (size_t)i < m.ladders.size(); if (ok) { m.ladders[i] = b; m.ladderAuto[i] = au != 0; } }
        }
    }
    else if (kind == "g") { // groups: ins (empty) / set (keeps its parts) / del
        if (verb == "del") {
            ok = i >= 0 && (size_t)i < m.groups.size();
            if (ok) m.groups.erase(m.groups.begin() + i);
        } else {
            Map::Group g;   // (part: above)
            ok = (bool)(in >> g.type >> g.material >> g.dir) && g.type >= 0 && g.type <= 2 && validMapMaterial(g.material)
                 && g.dir >= 0 && g.dir < 4 && readBox(g.box) && g.box.mx.y - g.box.mn.y <= kMaxObjectHeight;
            if (ok && verb == "ins") { ok = i >= 0 && (size_t)i <= m.groups.size() && m.groups.size() < kMaxMapGroups; if (ok) m.groups.insert(m.groups.begin() + i, g); }
            else if (ok) {
                ok = i >= 0 && (size_t)i < m.groups.size();
                if (ok && g.type == Map::GROUP_CUSTOM && m.groups[i].type == Map::GROUP_CUSTOM) g.parts = std::move(m.groups[i].parts);   // (else none)
                if (ok) m.groups[i] = std::move(g);
            }
        }
    }
    else if (kind == "h") {
        int has;
        AABB b;
        ok = verb == "set" && (bool)(in >> has) && readBox(b);
        if (ok) { m.hasHill = has != 0; m.hill = b; }
    }
    if (ok && full) ok = m.rebuild();
    if (!ok && full) { std::string keep = std::move(m.source); m = std::move(before); m.source = std::move(keep); }
    if (ok) m.sourceStale = true;
    return ok;
}

// An editor ladder is one object: its climb volume. Its rails (both sides) and rungs (every 0.4 m) are made from
// it, so moving or stretching the ladder moves and re-spaces them. They're decoration (trim: not solid).
void Map::genLadderBrushes() {
    brushes.erase(std::remove_if(brushes.begin(), brushes.end(), [](const Brush& b) { return b.generated; }), brushes.end());
    for (size_t k = 0; k < ladders.size() && k < ladderAuto.size(); k++) {
        if (!ladderAuto[k]) continue;
        const AABB& l = ladders[k];
        bool alongX = (l.mx.x - l.mn.x) >= (l.mx.z - l.mn.z);   // the rungs run along the wider side
        float top = std::max(l.mn.y + 0.5f, l.mx.y - 1.0f);       // (the volume reaches a metre over the top)
        auto add = [&](float a0, float y0, float a1, float y1, float d0, float d1) {
            Brush b;
            b.material = 8;
            b.generated = true;
            b.box = alongX ? AABB{{a0, y0, d0}, {a1, y1, d1}} : AABB{{d0, y0, a0}, {d1, y1, a1}};
            brushes.push_back(b);
        };
        float w0 = alongX ? l.mn.x : l.mn.z, w1 = alongX ? l.mx.x : l.mx.z;
        float dc = alongX ? (l.mn.z + l.mx.z) * 0.5f : (l.mn.x + l.mx.x) * 0.5f;
        add(w0, l.mn.y, w0 + 0.08f, top, dc - 0.04f, dc + 0.04f);
        add(w1 - 0.08f, l.mn.y, w1, top, dc - 0.04f, dc + 0.04f);
        for (float y = l.mn.y + 0.35f; y < top; y += 0.4f) add(w0 + 0.08f, y, w1 - 0.08f, y + 0.06f, dc - 0.03f, dc + 0.03f);
    }
    for (size_t gi = 0; gi < groups.size(); gi++) {
        std::vector<Brush> parts;
        groupBoxes(groups[gi], parts);
        for (auto& b : parts) { b.generated = true; b.group = (int)gi; brushes.push_back(b); }
    }
}

void Map::groupBoxes(const Group& g, std::vector<Brush>& out) {
    const AABB& b = g.box;
    Vec3 sz = b.mx - b.mn;
    auto add = [&](Vec3 a, Vec3 c) { if (c.x - a.x > 1e-3f && c.y - a.y > 1e-3f && c.z - a.z > 1e-3f) out.push_back({{a, c}, g.material}); };
    if (g.type == GROUP_CUSTOM) {
        for (auto& p : g.parts) {
            Brush q = p;
            q.box = {b.mn + Vec3(p.box.mn.x * sz.x, p.box.mn.y * sz.y, p.box.mn.z * sz.z), b.mn + Vec3(p.box.mx.x * sz.x, p.box.mx.y * sz.y, p.box.mx.z * sz.z)};
            out.push_back(q);
        }
    } else if (g.type == GROUP_ROOM) { // walls / floor / ceiling 0.25 m (thinner in a tiny room); doors 1.6 x 2.4 m where they fit
        float t = std::min(0.25f, std::min(sz.x, std::min(sz.y, sz.z)) * 0.25f);
        add(b.mn, {b.mx.x, b.mn.y + t, b.mx.z});                  // floor
        add({b.mn.x, b.mx.y - t, b.mn.z}, b.mx);                  // ceiling
        float y0 = b.mn.y + t, y1 = b.mx.y - t, dh = std::min(2.4f, (y1 - y0) * 0.85f);
        for (int w = 0; w < 4; w++) { // walls along x (at -z, +z), then along z (at -x, +x, between those)
            bool alongX = w < 2;
            int a = alongX ? 0 : 2;
            float s0 = alongX ? b.mn.x : b.mn.z + t, s1 = alongX ? b.mx.x : b.mx.z - t;
            float d0 = alongX ? (w == 0 ? b.mn.z : b.mx.z - t) : (w == 2 ? b.mn.x : b.mx.x - t), d1 = d0 + t;
            auto wall = [&](float u0, float v0, float u1, float v1) {
                Vec3 p, q;
                p[a] = u0; q[a] = u1; p.y = v0; q.y = v1; p[2 - a] = d0; q[2 - a] = d1;
                add(p, q);
            };
            float len = s1 - s0, dw = std::min(1.6f, len - 2 * t - 0.5f);
            if (dw < 0.9f || dh < 1.0f) { wall(s0, y0, s1, y1); continue; } // too small for a door
            float m = (s0 + s1) * 0.5f;
            wall(s0, y0, m - dw * 0.5f, y1);
            wall(m + dw * 0.5f, y0, s1, y1);
            wall(m - dw * 0.5f, y0 + dh, m + dw * 0.5f, y1);           // over the door
        }
    } else { // stairs: solid steps, as many as keep each rise <= 0.5 m (pmove steps up 0.55 m)
        int a = g.dir < 2 ? 0 : 2;
        bool neg = g.dir & 1;
        int n = std::min(128, std::max(1, (int)std::ceil(sz.y / 0.5f - 1e-4f)));
        float run = sz[a] / n, rise = sz.y / n;
        for (int k = 0; k < n; k++) {
            Vec3 p = b.mn, q = b.mx;
            q.y = b.mn.y + rise * (k + 1);
            if (neg) { q[a] = b.mx[a] - run * k; p[a] = q[a] - run; }
            else { p[a] = b.mn[a] + run * k; q[a] = p[a] + run; }
            add(p, q);
        }
    }
}

bool Map::rebuild() {
    if (!validate()) return false;
    genLadderBrushes();
    if (!validate()) return false;
    bounds = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    hasWallbang = false;
    for (auto& b : brushes) {
        if (!solidMaterial(b.material)) continue; // far scenery, water / lava surfaces: never collide (clip: not part of the bounds)
        bounds.mn = vmin(bounds.mn, b.box.mn);
        bounds.mx = vmax(bounds.mx, b.box.mx);
        hasWallbang |= wallbangBrush(b);
    }
    if (brushes.empty()) bounds = {{-1, -1, -1}, {1, 1, 1}};
    return buildGrid();
}

// The cells a box covers (clamped to the grid, so boxes outside it land in the edge cells)
struct GridSpan { int x0, x1, z0, z1; size_t count() const { return (size_t)(x1 - x0 + 1) * (size_t)(z1 - z0 + 1); } };
static GridSpan gridSpan(const AABB& b, float gx0, float gz0, int gw, int gh) {
    return {(int)clampf((b.mn.x - gx0) / Map::kCell, 0, (float)(gw - 1)), (int)clampf((b.mx.x - gx0) / Map::kCell, 0, (float)(gw - 1)),
            (int)clampf((b.mn.z - gz0) / Map::kCell, 0, (float)(gh - 1)), (int)clampf((b.mx.z - gz0) / Map::kCell, 0, (float)(gh - 1))};
}
// The grid's placement for given bounds. validate() keeps the bounds within kMapExtent (at most 2048 x 2048 cells);
// with no solid brush the bounds are the empty sentinel and the grid is one cell.
static void gridShape(const AABB& bounds, float& gx0, float& gz0, int& gw, int& gh) {
    gx0 = std::floor(bounds.mn.x / Map::kCell) * Map::kCell;
    gz0 = std::floor(bounds.mn.z / Map::kCell) * Map::kCell;
    gw = std::max(1, (int)std::min(4096.0f, std::ceil((bounds.mx.x - gx0) / Map::kCell)));
    gh = std::max(1, (int)std::min(4096.0f, std::ceil((bounds.mx.z - gz0) / Map::kCell)));
}

void Map::rebuildFluids() {
    lava.clear(); acid.clear(); liquids.clear(); fields.clear(); hasSlow = false;
    for (auto& b : brushes) {
        if (b.gone) continue;
        if (b.material == kMatLowGrav) fields.push_back({b.box, FIELD_LOWGRAV});
        if (b.material == kMatSlowMo) { fields.push_back({b.box, FIELD_SLOW}); hasSlow = true; }
        if (b.material == kMatGravFlip) fields.push_back({{{b.box.mn.x, b.box.mx.y, b.box.mn.z}, {b.box.mx.x, b.box.mx.y + kGravFlipHeight, b.box.mx.z}}, FIELD_FLIP});
        if (lavaMaterial(b.material)) lava.push_back(b.box);
        if (b.material == kMatAcid) acid.push_back(b.box);
        if (liquidMaterial(b.material)) liquids.push_back({b.box, (uint16_t)(b.material - kMatLiquids)});
    }
}

bool Map::buildGrid() {
    rebuildFluids();   // (lava: touch it and die; acid: touch it and it hurts; liquids: pmove)
    hasIce = hasRubber = hasGripMats = false;
    glass.clear(); windows.clear(); blocks.clear(); crumbles.clear(); teleporters.clear();
    buildRace();
    for (size_t i = 0; i < brushes.size(); i++) {
        if (breakMaterial(brushes[i].material)) blocks.push_back((uint32_t)i);
        if (brushes[i].material == kMatCrumble) crumbles.push_back((uint32_t)i);
        if (brushes[i].material == kMatTeleport) teleporters.push_back((uint32_t)i);
        if (brushes[i].material == kMatGlassBreak) glass.push_back((uint32_t)i);
        else if (brushes[i].material == kMatGlass) windows.push_back((uint32_t)i);
    }
    for (auto& b : brushes) {
        hasIce |= b.material == kMatIce;
        hasRubber |= b.material == kMatRubber;
        hasGripMats |= b.material == kMatIce || b.material == kMatRubber || b.material == kMatAsphalt || b.material == kMatMud ||
                       b.material == kMatHealTile || b.material == kMatSpikes;
    }
    gridShape(bounds, gridX0, gridZ0, gridW, gridH);
    size_t ncells = (size_t)gridW * gridH;
    // count per cell, then fill: one allocation, refused before it's made if it's too big
    size_t total = 0;
    for (auto& br : brushes) {
        if (!gridSolid(br)) continue;
        total += gridSpan(br.box, gridX0, gridZ0, gridW, gridH).count();
        if (total > kMaxMapGridRefs) { cells.assign(1, GridCell()); cellBrushes.clear(); gridRefs = 0; gridW = gridH = 1; return false; }
    }
    cells.assign(ncells, GridCell());
    gridYLo = 1e30f; gridYHi = -1e30f;
    for (auto& br : brushes) {
        if (!gridSolid(br)) continue;
        gridYLo = std::min(gridYLo, br.box.mn.y); gridYHi = std::max(gridYHi, br.box.mx.y);
        GridSpan sp = gridSpan(br.box, gridX0, gridZ0, gridW, gridH);
        for (int z = sp.z0; z <= sp.z1; z++)
            for (int x = sp.x0; x <= sp.x1; x++) cells[(size_t)z * gridW + x].end++;   // (end holds the count for now)
    }
    // spare room for edits: a quarter more plus two in every used cell, and two in empty ones unless the grid is huge
    size_t at = 0;
    for (auto& c : cells) {
        size_t n = c.end;
        c.start = c.end = (uint32_t)at;
        at += n + (n ? n / 4 + 2 : ncells <= (1u << 20) ? 2 : 0);
    }
    cellBrushes.assign(at, 0);
    for (size_t i = 0; i < brushes.size(); i++) {   // (in brush order within each cell)
        if (!gridSolid(brushes[i])) continue;
        GridSpan sp = gridSpan(brushes[i].box, gridX0, gridZ0, gridW, gridH);
        for (int z = sp.z0; z <= sp.z1; z++)
            for (int x = sp.x0; x <= sp.x1; x++) { GridCell& c = cells[(size_t)z * gridW + x]; cellBrushes[c.end++] = (uint16_t)i; }
    }
    gridRefs = total;
    return true;
}

// Edits: op 0 sets brush i to b, 1 inserts b at i, 2 removes brush i. Everything rebuild() derives from the brushes is
// derived again, the grid in place when its placement doesn't change and the cells have room: the result is the
// same as a full rebuild (same cells, same brush order in each), so a client that downloads the edited map and one
// that applied the edits collide alike.
bool Map::changeBrush(int op, size_t i, const Brush& b) {
    if (op == 1 ? i > brushes.size() : i >= brushes.size()) return false;
    if (op == 1 && brushes.size() >= kMaxMapBrushes) return false;   // (cell indices are 16 bits)
    const Brush old = op == 1 ? Brush() : brushes[i];
    // bounds and the wallbang flag over the brushes as they will be (as rebuild: far scenery, water, lava, clip aren't in the bounds)
    // Usually neither changes and nobody has to look at every brush: the old brush wasn't on the edge of the bounds (so
    // the others make them), the new one is inside them, and no wallbang brush went.
    auto inside = [](const AABB& x, const AABB& in, bool strict) {
        return strict ? x.mn.x > in.mn.x && x.mn.y > in.mn.y && x.mn.z > in.mn.z && x.mx.x < in.mx.x && x.mx.y < in.mx.y && x.mx.z < in.mx.z
                      : x.mn.x >= in.mn.x && x.mn.y >= in.mn.y && x.mn.z >= in.mn.z && x.mx.x <= in.mx.x && x.mx.y <= in.mx.y && x.mx.z <= in.mx.z;
    };
    AABB nb = bounds;
    bool wallbang = hasWallbang || (op != 2 && wallbangBrush(b));
    bool oldGone = op == 1 || !solidMaterial(old.material) || inside(old.box, bounds, true), newIn = op == 2 || !solidMaterial(b.material) || inside(b.box, bounds, false);
    if (!oldGone || !newIn || (op != 1 && wallbangBrush(old)) || brushes.size() + (op == 1) - (op == 2) == 0) {
        nb = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
        wallbang = false;
        auto add = [&](const Brush& x) { if (!solidMaterial(x.material)) return; nb.mn = vmin(nb.mn, x.box.mn); nb.mx = vmax(nb.mx, x.box.mx); wallbang |= wallbangBrush(x); };
        for (size_t k = 0; k < brushes.size(); k++) if (k != i || op == 1) add(brushes[k]);
        if (op != 2) add(b);
        if (brushes.size() + (op == 1) - (op == 2) == 0) nb = {{-1, -1, -1}, {1, 1, 1}};
    }
    float gx0, gz0;
    int gw, gh;
    gridShape(nb, gx0, gz0, gw, gh);
    bool oldSolid = op != 1 && gridSolid(old), newSolid = op != 2 && gridSolid(b);
    bool inPlace = gx0 == gridX0 && gz0 == gridZ0 && gw == gridW && gh == gridH && cells.size() == (size_t)gw * gh;
    GridSpan so{}, sn{};
    if (oldSolid) so = gridSpan(old.box, gridX0, gridZ0, gridW, gridH);
    if (newSolid) sn = gridSpan(b.box, gridX0, gridZ0, gridW, gridH);
    auto inSpan = [](const GridSpan& sp, int x, int z) { return x >= sp.x0 && x <= sp.x1 && z >= sp.z0 && z <= sp.z1; };
    if (inPlace) {
        size_t refs = gridRefs - (oldSolid ? so.count() : 0) + (newSolid ? sn.count() : 0);
        if (refs > kMaxMapGridRefs) return false;   // (what buildGrid would say)
        if (newSolid)   // room in every cell it goes into (all or nothing)
            for (int z = sn.z0; z <= sn.z1 && inPlace; z++)
                for (int x = sn.x0; x <= sn.x1; x++) {
                    size_t c = (size_t)z * gridW + x;
                    uint32_t cap = (c + 1 < cells.size() ? cells[c + 1].start : (uint32_t)cellBrushes.size()) - cells[c].start;
                    uint32_t used = cells[c].end - cells[c].start - (oldSolid && inSpan(so, x, z) ? 1 : 0);
                    if (used + 1 > cap) { inPlace = false; break; }
                }
    }
    Brush keep = old;
    if (op == 0) brushes[i] = b;
    else if (op == 1) brushes.insert(brushes.begin() + i, b);
    else brushes.erase(brushes.begin() + i);
    AABB oldBounds = bounds;
    bool oldWallbang = hasWallbang;
    bounds = nb;
    hasWallbang = wallbang;
    if (!inPlace) {
        if (buildGrid()) return true;
        if (op == 0) brushes[i] = keep;   // over the limit: put it back as it was
        else if (op == 1) brushes.erase(brushes.begin() + i);
        else brushes.insert(brushes.begin() + i, keep);
        bounds = oldBounds;
        hasWallbang = oldWallbang;
        buildGrid();
        return false;
    }
    auto fluid = [](int m) { return lavaMaterial(m) || m == kMatAcid || liquidMaterial(m) || m == kMatLowGrav || m == kMatSlowMo || m == kMatGravFlip; };
    if (fluid(old.material) || (op != 2 && fluid(b.material))) rebuildFluids();
    if (op != 2) {
        hasIce |= b.material == kMatIce; hasRubber |= b.material == kMatRubber;
        hasGripMats |= b.material == kMatIce || b.material == kMatRubber || b.material == kMatAsphalt || b.material == kMatMud ||
                       b.material == kMatHealTile || b.material == kMatSpikes;
    }   // (kept until a rebuild: only costs a probe)
    if (op != 0 || old.material == kMatGate || b.material == kMatGate) buildRace();
    if (op != 0 || glassMaterial(old.material) || glassMaterial(b.material) || blockDrawn(old.material) || blockDrawn(b.material) ||
        old.material == kMatTeleport || b.material == kMatTeleport) {
        glass.clear(); windows.clear(); blocks.clear(); crumbles.clear(); teleporters.clear();
        for (size_t k = 0; k < brushes.size(); k++) {
            if (breakMaterial(brushes[k].material)) blocks.push_back((uint32_t)k);
            if (brushes[k].material == kMatCrumble) crumbles.push_back((uint32_t)k);
            if (brushes[k].material == kMatTeleport) teleporters.push_back((uint32_t)k);
            if (brushes[k].material == kMatGlassBreak) glass.push_back((uint32_t)k);
            else if (brushes[k].material == kMatGlass) windows.push_back((uint32_t)k);
        }
    }
    const uint16_t ix = (uint16_t)i;
    if (oldSolid)   // out of the cells it was in
        for (int z = so.z0; z <= so.z1; z++)
            for (int x = so.x0; x <= so.x1; x++) {
                GridCell& c = cells[(size_t)z * gridW + x];
                uint16_t* d = cellBrushes.data();
                uint32_t k = c.start;
                while (k < c.end && d[k] != ix) k++;
                if (k < c.end) { memmove(d + k, d + k + 1, (c.end - k - 1) * sizeof *d); c.end--; }
            }
    if (op != 0 && !(op == 1 && i + 1 == brushes.size())) {   // the brushes after i moved up or down one (none after one added at the end) (the whole array, spare slots too: one vectorized pass)
        uint16_t* d = cellBrushes.data();
        size_t n = cellBrushes.size();
        if (op == 1) for (size_t k = 0; k < n; k++) d[k] = (uint16_t)(d[k] + (d[k] >= ix));
        else for (size_t k = 0; k < n; k++) d[k] = (uint16_t)(d[k] - (d[k] > ix));
    }
    if (newSolid) { gridYLo = std::min(gridYLo, b.box.mn.y); gridYHi = std::max(gridYHi, b.box.mx.y); }
    if (newSolid)   // into its cells, keeping each cell in brush order
        for (int z = sn.z0; z <= sn.z1; z++)
            for (int x = sn.x0; x <= sn.x1; x++) {
                GridCell& c = cells[(size_t)z * gridW + x];
                uint16_t* d = cellBrushes.data();
                uint32_t k = c.start;
                while (k < c.end && d[k] < ix) k++;
                memmove(d + k + 1, d + k, (c.end - k) * sizeof *d);
                d[k] = ix;
                c.end++;
            }
    gridRefs = gridRefs - (oldSolid ? so.count() : 0) + (newSolid ? sn.count() : 0);
    return true;
}

template <class F> void Map::forBox(const AABB& b, F f) const {
    int x0 = std::max(0, (int)std::floor((b.mn.x - gridX0) / kCell)), x1 = std::min(gridW - 1, (int)std::floor((b.mx.x - gridX0) / kCell));
    int z0 = std::max(0, (int)std::floor((b.mn.z - gridZ0) / kCell)), z1 = std::min(gridH - 1, (int)std::floor((b.mx.z - gridZ0) / kCell));
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            size_t c = (size_t)z * gridW + x;
            for (uint32_t k = cells[c].start, e = cells[c].end; k < e; k++)
                if (stopsPlayers(brushes[cellBrushes[k]].material)) f(brushes[cellBrushes[k]]);   // (a laser barrier, smoke: walk through)
        }
}

bool Map::breakGlass(size_t i) {
    if (i >= brushes.size() || (brushes[i].material != kMatGlassBreak && !blockDrawn(brushes[i].material)) || brushes[i].gone) return false;
    if (pristineBrushes.empty()) pristineBrushes = brushes;
    Brush b = brushes[i];
    b.gone = true;
    if (!setBrush(i, b)) return false;
    destructSerial++;
    return true;
}

bool Map::restoreBrush(size_t i) {
    if (i >= brushes.size() || !brushes[i].gone) return false;
    Brush b = brushes[i];
    b.gone = false;
    if (!setBrush(i, b)) return false;
    destructSerial++;
    return true;
}

int Map::crumbleAt(const Vec3& p, float eps) const {
    for (uint32_t i : crumbles) {
        const Brush& b = brushes[i];
        if (!b.gone && brushNear(b, p, eps)) return (int)i;   // (a sloped one: its shape, not its box)
    }
    return -1;
}

int Map::teleporterAt(const AABB& box) const {
    for (uint32_t i : teleporters) {
        const AABB& t = brushes[i].box;
        if (!brushes[i].gone && box.mn.x < t.mx.x && box.mx.x > t.mn.x && box.mn.y < t.mx.y && box.mx.y > t.mn.y && box.mn.z < t.mx.z && box.mx.z > t.mn.z) return (int)i;
    }
    return -1;
}

int Map::teleportTarget(int from) const {
    if (from < 0 || (size_t)from >= brushes.size()) return -1;
    const size_t n = teleporters.size();
    size_t at = 0;
    while (at < n && (int)teleporters[at] != from) at++;
    for (size_t k = 1; k < n; k++) {
        const uint32_t j = teleporters[(at + k) % n];
        if ((int)j != from && !brushes[j].gone && brushes[j].link == brushes[from].link) return (int)j;
    }
    return -1;
}

Vec3 Map::teleportExit(int to, float out, Vec3* n) const {
    const Brush& b = brushes[(size_t)to];
    const int a = b.exitFace / 2;
    const float sg = (b.exitFace & 1) ? -1.0f : 1.0f;   // (kFacePX, kFaceNX, kFacePY, ...: axis x y z, + then -)
    Vec3 nn;
    nn[a] = sg;
    Vec3 c = b.box.center();
    c[a] = (sg > 0 ? b.box.mx[a] : b.box.mn[a]) + sg * out;
    if (n) *n = nn;
    return c;
}

void Map::buildRace() {
    raceCheckpoints = checkpoints;
    for (const Brush& b : brushes)
        if (b.material == kMatGate && !b.generated && raceCheckpoints.size() < kMaxMapCheckpoints) raceCheckpoints.push_back(b.box);
}

void Map::flyProjectile(Vec3& pos, Vec3& vel, const ProjFlight& f, float t) const {
    if (!hasSlow || t <= 0) { projectileExtrapolate(pos, vel, f, t); return; }
    constexpr float kStep = 0.002f;
    for (float left = t; left > 0; left -= kStep) {
        const float h = std::min(kStep, left) * projectileRate(pos);
        projectileStepVel(vel, f, h);
        pos += vel * h;
    }
}

int Map::blockAt(const Vec3& p, float eps) const {
    for (uint32_t i : blocks) {
        const Brush& b = brushes[i];
        if (!b.gone && brushNear(b, p, eps)) return (int)i;   // (a sloped one: its shape, not its box)
    }
    return -1;
}

int Map::glassAt(const Vec3& p, float eps) const {
    for (uint32_t i : glass) {
        const Brush& b = brushes[i];
        if (!b.gone && brushNear(b, p, eps)) return (int)i;   // (a sloped pane: its shape, not its box, which overlaps its neighbours')
    }
    return -1;
}

int Map::punchHole(const Vec3& at, std::vector<AABB>* crumbled) {
    std::vector<Brush> pieces;
    std::vector<size_t> cut;
    std::vector<Hole> made;
    const size_t n = brushes.size();
    for (size_t i = 0; i < n; i++) {
        const Brush& b = brushes[i];
        if (b.gone || b.shape != BRUSH_BOX || !holeMaterial(b.material)) continue;
        const Vec3 ext = b.box.mx - b.box.mn;
        const int a = ext.x <= ext.y && ext.x <= ext.z ? 0 : ext.y <= ext.z ? 1 : 2;
        if (ext[a] > kBreakableThick + 1e-4f) continue;
        const int ua = holeUAxis(a), va = holeVAxis(a);
        // the distance to the wall's plane (a direct hit, the blast a few cm off the face, counts as on it)
        const float d = std::max(0.0f, std::max(b.box.mn[a] - at[a], at[a] - b.box.mx[a]) - 0.1f);
        const float r = 0.5f * (kHoleSize - d);
        if (r < 0.1f) continue;
        const float reach = r * 1.2f;   // (the nudge can take a cell a little past r)
        const float u0 = b.box.mn[ua], u1 = b.box.mx[ua], v0 = b.box.mn[va], v1 = b.box.mx[va];
        if (at[ua] + reach <= u0 || at[ua] - reach >= u1 || at[va] + reach <= v0 || at[va] - reach >= v1) continue;
        Hole h{at[ua], at[va], r, 0, 0, a, b.box.mn[a], b.box.mx[a]};
        holeCell(b.material, h.cw, h.ch);
        // the box's rows through the hole, each cut where cells break out; the rows above and below it stay whole
        std::vector<std::pair<Vec3, Vec3>> parts;   // (u0 v0 _, u1 v1 _) in the wall's own (u, v)
        auto emit = [&](float pu0, float pu1, float pv0, float pv1) {
            if (pu1 - pu0 > 1e-4f && pv1 - pv0 > 1e-4f) parts.push_back({{pu0, pv0, 0}, {pu1, pv1, 0}});
        };
        const int r0 = (int)std::floor(std::max(v0, h.v - reach) / h.ch), r1 = (int)std::floor((std::min(v1, h.v + reach) - 1e-5f) / h.ch);
        bool any = false;
        float whole = v0;   // rows from here up are whole so far (merged into one box when a cut row or the top ends them)
        for (int row = r0; row <= r1; row++) {
            const float rv0 = std::max(v0, row * h.ch), rv1 = std::min(v1, (row + 1) * h.ch);
            if (rv1 - rv0 < 1e-5f) continue;
            const float off = 0.5f * (float)(row & 1);
            const int c0 = (int)std::floor(std::max(u0, h.u - reach) / h.cw + off), c1 = (int)std::floor(std::min(u1, h.u + reach) / h.cw + off);
            std::vector<std::pair<float, float>> keep;
            float cur = u0;
            for (int c = c0; c <= c1; c++) {
                if (!holeTakes(h.u, h.v, h.r, h.cw, h.ch, row, c)) continue;
                const float cu0 = std::max(u0, (c - off) * h.cw), cu1 = std::min(u1, (c + 1 - off) * h.cw);
                if (cu1 <= cu0) continue;
                if (cu0 > cur) keep.push_back({cur, cu0});
                cur = std::max(cur, cu1);
            }
            if (cur == u0) continue;   // nothing broken in this row: still whole
            any = true;
            emit(u0, u1, whole, rv0);
            whole = rv1;
            if (u1 > cur) keep.push_back({cur, u1});
            for (auto& k : keep) emit(k.first, k.second, rv0, rv1);
        }
        if (!any) continue;
        emit(u0, u1, whole, v1);
        cut.push_back(i);
        for (auto& pp : parts) {
            Brush q = b;
            q.generated = true;
            q.origin = b.origin >= 0 ? b.origin : (int)i;
            q.box.mn[ua] = pp.first.x; q.box.mx[ua] = pp.second.x;
            q.box.mn[va] = pp.first.y; q.box.mx[va] = pp.second.y;
            pieces.push_back(q);
        }
        bool dup = false;
        for (const Hole& o : made) dup |= o.axis == h.axis && o.lo == h.lo && o.hi == h.hi && o.u == h.u && o.v == h.v && o.r == h.r && o.cw == h.cw;
        if (!dup) made.push_back(h);
    }
    if (cut.empty() || n + pieces.size() > kMaxMapBrushes || holes.size() + made.size() > (size_t)kMaxHoles) return 0;
    if (pristineBrushes.empty()) pristineBrushes = brushes;
    const std::vector<Brush> before = brushes;
    for (size_t i : cut) brushes[i].gone = true;
    brushes.insert(brushes.end(), pieces.begin(), pieces.end());
    if (!buildGrid()) { brushes = before; buildGrid(); return 0; }   // (over the grid's limit: as it was)
    holes.insert(holes.end(), made.begin(), made.end());
    // a wall mostly gone: the rest falls (what's left of its face, its pieces' faces, against the whole of it)
    std::vector<int> walls;
    for (size_t i : cut) {
        const int o = brushes[i].origin >= 0 ? brushes[i].origin : (int)i;
        if (std::find(walls.begin(), walls.end(), o) == walls.end()) walls.push_back(o);
    }
    bool fell = false;
    for (int o : walls) {
        const AABB& w = brushes[(size_t)o].box;
        const Vec3 ext = w.mx - w.mn;
        const int a = ext.x <= ext.y && ext.x <= ext.z ? 0 : ext.y <= ext.z ? 1 : 2;
        const int ua = holeUAxis(a), va = holeVAxis(a);
        const float whole = ext[ua] * ext[va];
        float left = 0;
        for (const Brush& q : brushes)
            if (q.origin == o && !q.gone) left += (q.box.mx[ua] - q.box.mn[ua]) * (q.box.mx[va] - q.box.mn[va]);
        if (whole <= 0 || left > whole * (1.0f - kCrumble)) continue;
        for (Brush& q : brushes) if (q.origin == o) q.gone = true;
        if (holes.size() < (size_t)kMaxHoles)
            holes.push_back({0.5f * (w.mn[ua] + w.mx[ua]), 0.5f * (w.mn[va] + w.mx[va]), -1.0f, 0.5f * ext[ua] + 0.01f, 0.5f * ext[va] + 0.01f, a, w.mn[a], w.mx[a]});
        if (crumbled) crumbled->push_back(w);
        fell = true;
    }
    if (fell) buildGrid();
    destructSerial++;
    return (int)cut.size();
}

void Map::resetDestruction() {
    if (pristineBrushes.empty() && holes.empty()) return;
    if (!pristineBrushes.empty()) brushes.swap(pristineBrushes);
    pristineBrushes.clear();
    holes.clear();
    buildGrid();
    destructSerial++;
}

float Map::sweepAxis(const AABB& box, int axis, float delta, const Brush** hit, int* glassOut) const {
    if (hit) *hit = nullptr;
    if (glassOut) *glassOut = -1;
    if (delta == 0.0f) return 0.0f;
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    constexpr float eps = 0.001f;
    AABB q = box;
    if (delta > 0) q.mx[axis] += delta; else q.mn[axis] += delta;
    float glassAt = delta;   // (how far the move got to the nearest pane it passes)
    auto clip = [&](const AABB& b, const Brush* who) {
        if (glassOut && who && who->material == kMatGlassBreak) {   // (passed through: only noted)
            if (box.mx[a1] <= b.mn[a1] || box.mn[a1] >= b.mx[a1] || box.mx[a2] <= b.mn[a2] || box.mn[a2] >= b.mx[a2]) return;
            const float allowed = delta > 0 ? b.mn[axis] - box.mx[axis] - eps : b.mx[axis] - box.mn[axis] + eps;
            const bool ahead = delta > 0 ? box.mx[axis] <= b.mn[axis] + eps : box.mn[axis] >= b.mx[axis] - eps;
            if (ahead && (delta > 0 ? allowed < glassAt : allowed > glassAt)) { glassAt = allowed; *glassOut = (int)(who - brushes.data()); }
            return;
        }
        if (box.mx[a1] <= b.mn[a1] || box.mn[a1] >= b.mx[a1]) return;
        if (box.mx[a2] <= b.mn[a2] || box.mn[a2] >= b.mx[a2]) return;
        if (delta > 0) {
            if (box.mx[axis] <= b.mn[axis] + eps) {
                float allowed = b.mn[axis] - box.mx[axis] - eps;
                if (allowed < delta) { delta = std::max(allowed, 0.0f); if (hit) *hit = who; }
            }
        } else {
            if (box.mn[axis] >= b.mx[axis] - eps) {
                float allowed = b.mx[axis] - box.mn[axis] + eps;
                if (allowed > delta) { delta = std::min(allowed, 0.0f); if (hit) *hit = who; }
            }
        }
    };
    forBox(q, [&](const Brush& br) {
        // (a slope, map.h: the part of it in the way, a box)
        clip(br.shape == BRUSH_BOX ? br.box : brushObstacle(br, box, axis, delta), &br);
    });
    for (const AABB& b : dynSolids) clip(b, nullptr);
    return delta;
}

bool Map::boxSolid(const AABB& box) const {
    bool hit = false;
    forBox(box, [&](const Brush& b) { hit = hit || brushOverlaps(b, box); });
    for (const AABB& b : dynSolids) hit = hit || box.overlaps(b);
    return hit;
}

// Walks the XZ grid cells along the ray (2D DDA). visit(cellIndex, tExit) returns false to stop.
template <class V> static void walkCells(const Map& m, const Vec3& o, const Vec3& d, float maxT, V visit) {
    // Clip the ray to the grid's XZ extent first.
    float t0 = 0, t1 = maxT;
    const float gx1 = m.gridX0 + m.gridW * Map::kCell, gz1 = m.gridZ0 + m.gridH * Map::kCell;
    const float lo[2] = {m.gridX0, m.gridZ0}, hi[2] = {gx1, gz1}, oo[2] = {o.x, o.z}, dd[2] = {d.x, d.z};
    for (int i = 0; i < 2; i++) {
        if (std::fabs(dd[i]) < 1e-9f) { if (oo[i] < lo[i] || oo[i] > hi[i]) return; continue; }
        float a = (lo[i] - oo[i]) / dd[i], b = (hi[i] - oo[i]) / dd[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
        if (t0 > t1) return;
    }
    // ...and stop where it leaves the grid's solid Y slab for good (half a metre past it, so rounding never loses a
    // hit on its top or bottom face): no brush can be hit beyond, and an escaping ray stops after a cell or two.
    if (std::fabs(d.y) < 1e-9f) { if (o.y < m.gridYLo - 0.5f || o.y > m.gridYHi + 0.5f) return; }
    else t1 = std::min(t1, ((d.y > 0 ? m.gridYHi + 0.5f : m.gridYLo - 0.5f) - o.y) / d.y);
    if (t0 > t1) return;
    float px = o.x + d.x * t0, pz = o.z + d.z * t0;
    int cx = std::min(m.gridW - 1, std::max(0, (int)((px - m.gridX0) / Map::kCell)));
    int cz = std::min(m.gridH - 1, std::max(0, (int)((pz - m.gridZ0) / Map::kCell)));
    int sx = d.x > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    float tdx = std::fabs(d.x) > 1e-9f ? Map::kCell / std::fabs(d.x) : 1e30f;
    float tdz = std::fabs(d.z) > 1e-9f ? Map::kCell / std::fabs(d.z) : 1e30f;
    float nx = m.gridX0 + (cx + (sx > 0 ? 1 : 0)) * Map::kCell, nz = m.gridZ0 + (cz + (sz > 0 ? 1 : 0)) * Map::kCell;
    float tmx = std::fabs(d.x) > 1e-9f ? (nx - o.x) / d.x : 1e30f;
    float tmz = std::fabs(d.z) > 1e-9f ? (nz - o.z) / d.z : 1e30f;
    for (;;) {
        float tExit = std::min(std::min(tmx, tmz), t1);
        if (!visit((size_t)cz * m.gridW + cx, tExit)) return;
        if (tExit >= t1) return;
        if (tmx < tmz) { cx += sx; tmx += tdx; if (cx < 0 || cx >= m.gridW) return; }
        else { cz += sz; tmz += tdz; if (cz < 0 || cz >= m.gridH) return; }
    }
}

float Map::raycast(const Vec3& o, const Vec3& dir, float maxDist, Vec3* normal, int* material, bool hitClip) const {
    Vec3 inv = safeInv(dir);
    float best = -1.0f;
    int bestMat = -1;
    Vec3 bestN;
    walkCells(*this, o, dir, maxDist, [&](size_t c, float tExit) {
        for (uint32_t k = cells[c].start, e = cells[c].end; k < e; k++) {
            if (!stopsShots(brushes[cellBrushes[k]].material) && !(hitClip && brushes[cellBrushes[k]].material == 9)) continue;   // clip: players only (a force field, a net too), and the grapple
            Vec3 n;
            float t = rayBrush(o, dir, inv, brushes[cellBrushes[k]], best < 0 ? maxDist : best, &n);
            if (t >= 0 && (best < 0 || t < best)) { best = t; bestN = n; bestMat = brushes[cellBrushes[k]].material; }
        }
        return !(best >= 0 && best <= tExit); // nearest hit is inside this cell: done
    });
    for (const AABB& b : dynSolids) {
        int axis;
        float t = rayAABB(o, inv, b, best < 0 ? maxDist : best, &axis);
        if (t >= 0 && (best < 0 || t < best)) { best = t; bestN = Vec3(); if (axis >= 0) bestN[axis] = dir[axis] > 0 ? -1.0f : 1.0f; bestMat = -1; }
    }
    if (material) *material = best >= 0 ? bestMat : -1;
    if (best >= 0 && normal) *normal = bestN;
    return best;
}

float Map::raycastBullet(const Vec3& o, const Vec3& dir, float maxDist, float* bangDist) const {
    *bangDist = -1;
    if (!hasWallbang) return raycast(o, dir, maxDist);
    Vec3 inv = safeInv(dir);
    float best = -1.0f, bang = -1.0f; // nearest solid / nearest wallbang
    walkCells(*this, o, dir, maxDist, [&](size_t c, float tExit) {
        for (uint32_t k = cells[c].start, e = cells[c].end; k < e; k++) {
            const Brush& br = brushes[cellBrushes[k]];
            if (!stopsShots(br.material)) continue;   // clip: players only
            float t = rayBrush(o, dir, inv, br, best < 0 ? maxDist : best);
            if (t < 0) continue;
            if (wallbangBrush(br)) { if (bang < 0 || t < bang) bang = t; }
            else if (best < 0 || t < best) best = t;
        }
        return !(best >= 0 && best <= tExit);
    });
    for (const AABB& b : dynSolids) {
        float t = rayAABB(o, inv, b, best < 0 ? maxDist : best);
        if (t >= 0 && (best < 0 || t < best)) best = t;
    }
    if (bang >= 0 && (best < 0 || bang < best)) *bangDist = bang;
    return best;
}

// raycastAny's cast, shared with raycastAnyHinted (one filter, one box test, one walk: the hint's pretest leans on
// them being the same). Learn: note in *learn where the blocker was found.
// clip: players only (light, sight and shots pass); glass: sight and light pass (shots break it: they use raycast)
static bool skipsSight(const Brush& br, bool hitClip) { return (br.material == 9 && !hitClip) || (br.material != 9 && !stopsSight(br.material)); }
template <bool Learn> static bool castAny(const Map& m, const Vec3& o, const Vec3& dir, const Vec3& inv, float maxDist, bool hitClip, Map::RayHint* learn) {
    bool hit = false;
    walkCells(m, o, dir, maxDist, [&](size_t c, float) {
        for (uint32_t k = m.cells[c].start, e = m.cells[c].end; k < e && !hit; k++) {
            const Brush& br = m.brushes[m.cellBrushes[k]];
            if (skipsSight(br, hitClip)) continue;
            hit = rayBrush(o, dir, inv, br, maxDist) >= 0;
            if (Learn && hit) *learn = {(uint32_t)c, k, -1};
        }
        return !hit;
    });
    for (size_t j = 0; j < m.dynSolids.size() && !hit; j++)
        if (rayAABB(o, inv, m.dynSolids[j], maxDist) >= 0) { hit = true; if (Learn) *learn = {UINT32_MAX, 0, (int32_t)j}; }
    return hit;
}

bool Map::raycastAny(const Vec3& o, const Vec3& dir, float maxDist, bool hitClip) const {
    return castAny<false>(*this, o, dir, safeInv(dir), maxDist, hitClip, nullptr);
}

bool Map::raycastAnyHinted(const Vec3& o, const Vec3& dir, float maxDist, RayHint& hint, bool hitClip) const {
    Vec3 inv = safeInv(dir);
    // the hint first (see map.h): each test here is one castAny makes, on the same numbers, so "blocked" here is its answer
    if (hint.dyn >= 0) {
        if ((size_t)hint.dyn < dynSolids.size() && rayAABB(o, inv, dynSolids[hint.dyn], maxDist) >= 0) return true;
    } else if (hint.cell < cells.size() && hint.ref >= cells[hint.cell].start && hint.ref < cells[hint.cell].end) {
        const Brush& br = brushes[cellBrushes[hint.ref]];
        if (!skipsSight(br, hitClip) && rayBrush(o, dir, inv, br, maxDist) >= 0) {
            bool reached = false;
            walkCells(*this, o, dir, maxDist, [&](size_t c, float) { reached = c == hint.cell; return !reached; });
            if (reached) return true;
        }
    }
    return castAny<true>(*this, o, dir, inv, maxDist, hitClip, &hint);   // (a clear ray keeps its hint: the next may be blocked again)
}

bool Map::supportsMode(int mode) const {
    if (allowedModes && (mode < 0 || mode >= 32 || !(allowedModes >> mode & 1))) return false;
    return canSupportMode(mode);
}

bool Map::canSupportMode(int mode) const {
    // (a side counts with the mode's own set's spawns too: modeSpawnType starts the mode on those)
    auto has = [&](int t) { for (auto& s : spawns) if (s.team == t || (mode >= 0 && mode < kSpawnModes && s.team == modeSpawn(mode, t))) return true; return false; };
    bool teams = has(SPAWN_A) && has(SPAWN_B);
    switch (mode) {
        case MODE_VS:
        case MODE_DUEL:
        case MODE_ARENA:
        case MODE_POWER: return teams;
        case MODE_TOURNEY: return !spawns.empty();
        case MODE_BOMB: return teams && !bombSites.empty();
        // (a map with no race spawn starts racers on its "any" spawns: q's Surfy has one, 2026-10-30)
        case MODE_RACE: case MODE_SURF: return (has(SPAWN_RACE) || has(SPAWN_ANY)) && !raceCheckpoints.empty();
        case MODE_KOTH: return teams && hasHill;
        case MODE_PAYLOAD:
        case MODE_NPAYLOAD: return teams && payloadPath.size() >= 2;
        case MODE_FFA: return !spawns.empty();
        case MODE_HIDE:
        case MODE_TAG: return spawns.size() >= 2;
        case MODE_POINTS: { // capture zones, and attacker and defender spawns for the start
            bool att = false, def = false;
            for (auto& s : spawns) if (isStagedSpawn(s.team) && spawnStage(s.team) == 0) (spawnDefends(s.team) ? def : att) = true;
            return points.size() >= 2 && att && def;
        }
    }
    return false;
}

AABB Map::payloadCartBox(float d) const {
    Vec3 c = payloadPoint(d), ahead = payloadPoint(d + 0.5f), behind = payloadPoint(d - 0.5f);
    Vec3 dir = ahead - behind;
    bool alongX = std::fabs(dir.x) > std::fabs(dir.z);
    Vec3 half = alongX ? Vec3(1.2f, 0.75f, 0.9f) : Vec3(0.9f, 0.75f, 1.2f);
    return {{c.x - half.x, c.y, c.z - half.z}, {c.x + half.x, c.y + 1.5f, c.z + half.z}};
}

float Map::payloadLength() const {
    float l = 0;
    for (size_t i = 1; i < payloadPath.size(); i++) l += length(payloadPath[i] - payloadPath[i - 1]);
    return l;
}

Vec3 Map::payloadPoint(float d) const {
    if (payloadPath.empty()) return {};
    for (size_t i = 1; i < payloadPath.size(); i++) {
        float seg = length(payloadPath[i] - payloadPath[i - 1]);
        if (d <= seg) return vlerp(payloadPath[i - 1], payloadPath[i], seg > 0 ? d / seg : 0);
        d -= seg;
    }
    return payloadPath.back();
}

float Map::cartTrack(int mode, int attackers, float progress) const {
    if (mode != MODE_PAYLOAD || attackers != TEAM_B) return progress;
    for (auto& sp : spawns) if (isStagedSpawn(sp.team)) return progress;   // (staged: the attackers always start at the start)
    return payloadLength() - progress;
}

// ---- editor clean-ups (see MapFix)
static MapFix fixStart(const Map& m) {
    MapFix f;
    const size_t n = m.editableBrushes();
    f.box.resize(n);
    f.del.assign(n, false);
    for (size_t i = 0; i < n; i++) f.box[i] = m.brushes[i].box;
    return f;
}

// brushes by 4 m cells, for the clean-ups' neighbour lookups (Midtown's 11 000 boxes pair by pair took 2 s)
struct FixGrid {
    std::unordered_map<int64_t, std::vector<int>> cells;
    std::vector<int> stamp;
    int now = 0;
    static int64_t key(int x, int y, int z) { return ((int64_t)(x & 0x1fffff) << 42) | ((int64_t)(y & 0x1fffff) << 21) | (int64_t)(z & 0x1fffff); }
    template <class F> static void span(const AABB& b, F f) {
        const int x0 = (int)std::floor(b.mn.x / 4), x1 = (int)std::floor(b.mx.x / 4), y0 = (int)std::floor(b.mn.y / 4),
                  y1 = (int)std::floor(b.mx.y / 4), z0 = (int)std::floor(b.mn.z / 4), z1 = (int)std::floor(b.mx.z / 4);
        if ((int64_t)(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 4096) { f(key(INT_MIN / 2, 0, 0)); return; }   // (a huge box: one shared list)
        for (int x = x0; x <= x1; x++) for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) f(key(x, y, z));
    }
    std::vector<int> huge;
    void add(int i, const AABB& b) {
        if (stamp.size() <= (size_t)i) stamp.resize(i + 1, 0);
        span(b, [&](int64_t k) { if (k == key(INT_MIN / 2, 0, 0)) huge.push_back(i); else cells[k].push_back(i); });
    }
    template <class F> void near(const AABB& b, F f) {   // each brush listed in a cell b touches, once
        now++;
        auto one = [&](int i) { if (stamp[i] != now) { stamp[i] = now; f(i); } };
        for (int i : huge) one(i);
        span(b, [&](int64_t k) {
            if (k == key(INT_MIN / 2, 0, 0)) { for (auto& c : cells) for (int i : c.second) one(i); return; }
            auto it = cells.find(k);
            if (it != cells.end()) for (int i : it->second) one(i);
        });
    }
};

static bool thinBreakable(const Brush& b) {
    const Vec3 d = b.box.mx - b.box.mn;
    return holeMaterial(b.material) && std::min(d.x, std::min(d.y, d.z)) <= kBreakableThick + 1e-4f;
}

MapFix mapFixRedundant(const Map& m) {
    MapFix f = fixStart(m);
    const size_t n = f.box.size();
    constexpr float e = 1e-4f;
    auto mergeable = [&](size_t i) {
        const Brush& b = m.brushes[i];
        return !f.del[i] && b.shape == BRUSH_BOX && !glassMaterial(b.material) && !holeMaterial(b.material);
    };
    auto same = [&](float a, float b) { return std::fabs(a - b) <= e; };
    // 1. merges, until nothing more joins (a row of boxes joins one at a time)
    FixGrid grid;
    for (size_t i = 0; i < n; i++) grid.add((int)i, f.box[i]);
    const Vec3 pad(2 * e, 2 * e, 2 * e);
    for (bool again = true; again;) {
        again = false;
        for (size_t i = 0; i < n; i++) {
            if (!mergeable(i)) continue;
            std::vector<int> close;
            grid.near({f.box[i].mn - pad, f.box[i].mx + pad}, [&](int j) { close.push_back(j); });
            for (int jj : close) {
                const size_t j = (size_t)jj;
                if (j == i || !mergeable(j) || m.brushes[j].material != m.brushes[i].material) continue;
                AABB& a = f.box[i];
                const AABB& b = f.box[j];
                bool inside = true;
                for (int k = 0; k < 3; k++) inside &= b.mn[k] >= a.mn[k] - e && b.mx[k] <= a.mx[k] + e;
                int equal = 0, along = -1;
                for (int k = 0; k < 3; k++) {
                    if (same(a.mn[k], b.mn[k]) && same(a.mx[k], b.mx[k])) equal++;
                    else along = k;
                }
                const bool joins = equal == 2 && b.mn[along] <= a.mx[along] + e && b.mx[along] >= a.mn[along] - e;
                if (!inside && !joins) continue;
                if (joins) { a.mn = vmin(a.mn, b.mn); a.mx = vmax(a.mx, b.mx); grid.add((int)i, a); }
                f.del[j] = true;
                f.merged++;
                again = true;
            }
        }
    }
    // 2. brushes wholly inside solid opaque boxes (any brush can hide one, generated ones too; one already gone can't)
    auto hides = [&](size_t k) {
        const Brush& b = m.brushes[k];
        return (k >= n || !f.del[k]) && b.shape == BRUSH_BOX && !b.gone && solidMaterial(b.material) && b.material != 2 &&
               !glassMaterial(b.material) && !thinBreakable(b);
    };
    FixGrid all;
    for (size_t k = 0; k < m.brushes.size(); k++) all.add((int)k, k < n ? f.box[k] : m.brushes[k].box);
    for (size_t i = 0; i < n; i++) {
        if (f.del[i]) continue;
        const AABB c = f.box[i];
        std::vector<AABB> by;
        float axis[3][64];
        int cnt[3] = {0, 0, 0};
        for (int k = 0; k < 3; k++) { axis[k][cnt[k]++] = c.mn[k]; axis[k][cnt[k]++] = c.mx[k]; }
        bool tooMany = false;
        std::vector<int> close;
        all.near(c, [&](int k) { close.push_back(k); });
        for (int kk : close) {
            const size_t k = (size_t)kk;
            if (tooMany) break;
            if (k == i || !hides(k)) continue;
            const AABB& b = k < n ? f.box[k] : m.brushes[k].box;
            if (!b.overlaps(c)) continue;
            by.push_back(b);
            for (int a = 0; a < 3; a++)
                for (float v : {b.mn[a], b.mx[a]})
                    if (v > c.mn[a] && v < c.mx[a]) { if (cnt[a] == 64) { tooMany = true; break; } axis[a][cnt[a]++] = v; }
        }
        if (by.empty() || tooMany) continue;   // (too finely cut up to check: kept)
        for (int a = 0; a < 3; a++) std::sort(axis[a], axis[a] + cnt[a]);
        bool covered = true;
        for (int x = 0; x + 1 < cnt[0] && covered; x++)
            for (int y = 0; y + 1 < cnt[1] && covered; y++)
                for (int z = 0; z + 1 < cnt[2] && covered; z++) {
                    if (axis[0][x + 1] - axis[0][x] < e || axis[1][y + 1] - axis[1][y] < e || axis[2][z + 1] - axis[2][z] < e) continue;
                    const Vec3 p((axis[0][x] + axis[0][x + 1]) * 0.5f, (axis[1][y] + axis[1][y + 1]) * 0.5f, (axis[2][z] + axis[2][z + 1]) * 0.5f);
                    covered = std::any_of(by.begin(), by.end(), [&](const AABB& b) { return b.contains(p); });
                }
        if (covered) { f.del[i] = true; f.hidden++; }
    }
    return f;
}

MapFix mapFixGaps(const Map& m) {
    MapFix f = fixStart(m);
    const size_t n = f.box.size();
    constexpr float g = 0.125f, e = 1e-4f;
    auto onGrid = [&](float v) { return std::fabs(v / g - std::round(v / g)) * g <= e; };
    auto plain = [&](size_t i) { return m.brushes[i].shape == BRUSH_BOX; };
    FixGrid grid;
    for (size_t k = 0; k < m.brushes.size(); k++) grid.add((int)k, k < n ? f.box[k] : m.brushes[k].box);
    const Vec3 reach(g, g, g);
    for (size_t i = 0; i < n; i++) {
        if (!plain(i)) continue;
        std::vector<int> close;
        grid.near({f.box[i].mn - reach, f.box[i].mx + reach}, [&](int j) { close.push_back(j); });
        for (int jj : close) {
            const size_t j = (size_t)jj;
            if (j == i || j >= n || !plain(j)) continue;
            for (int a = 0; a < 3; a++) {
                AABB& A = f.box[i];   // A's high side faces B's low side across the gap
                AABB& B = f.box[j];
                const float gap = B.mn[a] - A.mx[a];
                if (gap <= e || gap >= g - e) continue;
                bool facing = true;   // (they overlap across the other two axes)
                for (int k = 0; k < 3; k++) if (k != a) facing &= A.mn[k] < B.mx[k] - e && B.mn[k] < A.mx[k] - e;
                if (!facing) continue;
                const bool ga = onGrid(A.mx[a]), gb = onGrid(B.mn[a]);
                if (ga && gb) continue;
                float na = A.mx[a], nb = B.mn[a];
                if (ga) nb = na;
                else if (gb) na = nb;
                else {
                    const float line = std::ceil(A.mx[a] / g) * g;
                    if (line >= B.mn[a] - e) continue;   // (no grid line between them)
                    na = nb = line;
                }
                // never into a third box: the strips the two sides grow over must be clear
                auto clear = [&](const AABB& grow) {
                    if (grow.mx[a] - grow.mn[a] <= e) return true;
                    bool ok = true;
                    grid.near(grow, [&](int kk) {
                        const size_t k = (size_t)kk;
                        if (k != i && k != j && !m.brushes[k].gone && (k < n ? f.box[k] : m.brushes[k].box).overlaps(grow)) ok = false;
                    });
                    return ok;
                };
                AABB sa = A, sb = B;
                sa.mn[a] = A.mx[a]; sa.mx[a] = na;
                sb.mx[a] = B.mn[a]; sb.mn[a] = nb;
                if (!clear(sa) || !clear(sb)) continue;
                A.mx[a] = na;
                B.mn[a] = nb;
                f.gaps++;
            }
        }
    }
    return f;
}
