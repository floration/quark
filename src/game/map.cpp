#include "official_map_data.h"
#include "../common/u8file.h"
#include "map.h"
#include "gamedefs.h"
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
#include <unordered_map>
#include <cstdlib>
#include <cerrno>
#include <cfloat>
#include <climits>
#include <algorithm>

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

int resetOfficialMaps(const std::string& shippedDir) {
    std::error_code ec;
    std::string dir = shippedDir.empty() ? findDataPath("maps") : shippedDir, mine = userMapsDir(), cache = userDataPath("cache");
    fs::create_directories(dir, ec);
    int n = 0;
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) {
        fs::remove(mine + "/" + m->name + ".qmap", ec); // your edited copy shadows the shipped one: reset drops it
        fs::remove(cache + "/" + m->name + ".qlm", ec);
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
    return n;
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
    for (const std::string& dir : {findDataPath("maps"), userMapsDir()})
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".qmap") out.push_back(e.path().stem().string());
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

std::string downloadedMapPath(const std::string& name, uint32_t crc) {
    char hex[16];
    snprintf(hex, sizeof hex, "%08x", crc);
    return userMapsDir() + "/downloads/" + safeMapName(name) + "-" + hex + ".qmap";
}

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

// CRC-32 (zlib's), eight bytes a step ("slicing by 8": about 5x the byte-at-a-time loop; every map load hashes the
// whole text). prev continues an earlier CRC, as zlib's crc32(prev, ...) does.
uint32_t crc32(const void* data, size_t len, uint32_t prev) {
    struct Tables {
        uint32_t t[8][256];
        Tables() {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[0][i] = c;
            }
            for (int s = 1; s < 8; s++)
                for (uint32_t i = 0; i < 256; i++) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 255];
        }
    };
    static const Tables tables;   // (built once, thread-safe)
    const auto& t = tables.t;
    uint32_t c = prev ^ 0xFFFFFFFFu;
    const uint8_t* p = (const uint8_t*)data;
    for (; len >= 8; len -= 8, p += 8) {   // (byte loads: no alignment or endianness assumptions)
        uint32_t a = c ^ ((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
        c = t[7][a & 255] ^ t[6][(a >> 8) & 255] ^ t[5][(a >> 16) & 255] ^ t[4][a >> 24] ^ t[3][p[4]] ^ t[2][p[5]] ^ t[1][p[6]] ^ t[0][p[7]];
    }
    for (; len; len--) c = t[0][(c ^ *p++) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
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
    {   // every field back to a fresh map's (title, sun, palette, void, ceiling, hold fire, ...), but its name
        std::string keepName = std::move(name);
        *this = Map();
        name = std::move(keepName);
    }
    crc = crc32(text.data(), text.size());
    source = text;
    sourceStale = false;
    sourceCrc = crc;
    sourceCrcKnown = true;
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
        if (const char* h = (const char*)memchr(lineStart, '#', (size_t)(lineEnd - lineStart))) lineEnd = h;
        LineIn ls(lineStart, lineEnd);
        if (!(ls >> kw)) continue;
        bool ok = true;
        if (kw == "title") {
            ls.rest(title);
            ok = cleanMapTitle(title);   // (the same rule as the editor's title edit)
        } else if (kw == "box" || kw == "accent" || kw == "wallbang" || kw == "metal" || kw == "glow" || kw == "far" || kw == "water" || kw == "lava" || kw == "trim" || kw == "clip") {
            Brush b;
            ok = readBox(ls, b.box);
            b.material = kw == "accent" ? 1 : kw == "wallbang" ? 2 : kw == "metal" ? 3 : kw == "glow" ? 4 : kw == "far" ? 5 : kw == "water" ? 6 : kw == "lava" ? 7 : kw == "trim" ? 8 : kw == "clip" ? 9 : 0;
            hasWallbang |= b.material == 2;
            if (ok && brushes.size() >= kMaxMapBrushes) { if (err) *err = "too many boxes (limit " + std::to_string(kMaxMapBrushes) + ")"; return false; }
            if (ok) {
                brushes.push_back(b);
                if (b.material >= 5) continue; // far scenery / water / lava surfaces: not part of the playable bounds
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
            s.team = team == "a" ? SPAWN_A : team == "b" ? SPAWN_B : team == "race" ? SPAWN_RACE : SPAWN_ANY;
            if ((team.rfind("att", 0) == 0 || team.rfind("def", 0) == 0) && team.size() == 4 && team[3] >= '0' && team[3] <= '0' + kMaxSpawnStage)
                s.team = stagedSpawn(team[3] - '0', team[0] == 'd');
            if (ok) spawns.push_back(s);
        } else if (kw == "site") {
            AABB b; ok = readBox(ls, b) && bombSites.size() < kMaxMapSites; if (ok) bombSites.push_back(b);
        } else if (kw == "cartcheck") {   // a payload checkpoint this far along the track (0..1)
            float f; ok = (bool)(ls >> f) && f > 0 && f < 1 && cartChecks.size() < kMaxMapCartChecks;
            if (ok) { cartChecks.push_back(f); std::sort(cartChecks.begin(), cartChecks.end()); }
        } else if (kw == "point") {
            AABB b; ok = readBox(ls, b) && points.size() < kMaxMapPoints; if (ok) points.push_back(b);
        } else if (kw == "hill") {
            ok = readBox(ls, hill); hasHill = ok;
        } else if (kw == "ladder") {
            AABB b; ok = readBox(ls, b) && b.mx.y - b.mn.y <= kMaxObjectHeight && ladders.size() < kMaxMapLadders;
            std::string flag;
            if (ok) { ladders.push_back(b); ladderAuto.push_back(ls >> flag && flag == "auto"); }
        } else if (kw == "group") {
            std::string t; Group g;
            ok = (bool)(ls >> t >> g.material >> g.dir) && readBox(ls, g.box) && validMapMaterial(g.material) && g.dir >= 0 && g.dir < 4 && g.box.mx.y - g.box.mn.y <= kMaxObjectHeight && groups.size() < kMaxMapGroups;
            g.type = t == "room" ? GROUP_ROOM : t == "stairs" ? GROUP_STAIRS : GROUP_CUSTOM;
            ok = ok && (t == "room" || t == "stairs" || t == "custom");
            if (ok) groups.push_back(g);
        } else if (kw == "part") {
            Brush b; ok = !groups.empty() && groups.back().type == GROUP_CUSTOM && groups.back().parts.size() < kMaxGroupParts && (bool)(ls >> b.material) && validMapMaterial(b.material) && readBox(ls, b.box);
            if (ok) groups.back().parts.push_back(b);
        } else if (kw == "checkpoint") {
            AABB b; ok = readBox(ls, b) && checkpoints.size() < kMaxMapCheckpoints; if (ok) checkpoints.push_back(b);
        } else if (kw == "path") {
            Vec3 p; ok = (bool)(ls >> p.x >> p.y >> p.z) && okCoord(p.x) && okCoord(p.y) && okCoord(p.z) && payloadPath.size() < kMaxMapPath; if (ok) payloadPath.push_back(p);
        } else if (kw == "for") {
            std::string mode;
            ok = (bool)(ls >> mode);
            if (ok) preferredMode = modeFromName(mode.c_str());
        } else if (kw == "ranked") {
            std::string ln;
            while (ls >> ln) { int v = arenaVariantFromName(ln.c_str()); if (v >= 0) rankedLadders |= (uint16_t)(1u << v); else ok = false; }
        } else if (kw == "modes") {
            std::string mn;
            while (ls >> mn) { int m = modeFromName(mn.c_str()); if (m >= 0) allowedModes |= 1u << m; else ok = false; }
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
        } else if (kw == "void") {
            ok = (bool)(ls >> voidY) && okCoord(voidY); hasVoid = ok;
        } else if (kw == "palette") {
            std::string k; Vec3 c;
            while (ok && ls >> k) {
                ok = (bool)(ls >> c.x >> c.y >> c.z) && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
                if (k == "base") palBase = c; else if (k == "accent") palAccent = c; else if (k == "metal") palMetal = c;
                else if (k == "glow") palGlow = c; else if (k == "sky") palSky = c; else ok = false;
            }
            hasPalette = ok;
        } else if (kw == "sun") {
            Vec3 d; ok = (bool)(ls >> d.x >> d.y >> d.z) && okCoord(d.x) && okCoord(d.y) && okCoord(d.z) && length(d) > 1e-6f;
            if (ok) sunDir = std::fabs(length(d) - 1.0f) < 1e-6f ? d : normalize(d);   // (a saved, unit sun stays bit-exact)
        } else {
            ok = false;
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
    for (auto& b : brushes) if (b.generated && b.material < 5) { bounds.mn = vmin(bounds.mn, b.box.mn); bounds.mx = vmax(bounds.mx, b.box.mx); }
    if (!buildGrid()) { if (err) *err = "too much geometry for the collision grid"; return false; }
    return true;
}

uint32_t Map::sourceTextCrc() const {
    const std::string& t = sourceText();
    if (!sourceCrcKnown) { sourceCrc = crc32(t.data(), t.size()); sourceCrcKnown = true; }
    return sourceCrc;
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
    std::string o = "# made with the quark map editor\n";
    char b[320];
    char nb[6][24];
    auto box = [&](const char* kw, const AABB& a) {
        snprintf(b, sizeof b, "%s %s %s %s %s %s %s\n", kw, exactNum(nb[0], 24, a.mn.x), exactNum(nb[1], 24, a.mn.y), exactNum(nb[2], 24, a.mn.z),
                 exactNum(nb[3], 24, a.mx.x), exactNum(nb[4], 24, a.mx.y), exactNum(nb[5], 24, a.mx.z));
        o += b;
    };
    auto nums = [&](int count, const float* v) {   // " v0 v1 ..." (count <= 6)
        for (int i = 0; i < count; i++) { o += ' '; o += exactNum(nb[i], 24, v[i]); }
    };
    if (!title.empty()) o += "title " + title + "\n";
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
    if (hasPalette) {
        auto c = [&](const char* k, Vec3 v) { float f[3] = {v.x, v.y, v.z}; o += ' '; o += k; nums(3, f); };
        o += "palette"; c("base", palBase); c("accent", palAccent); c("metal", palMetal); c("glow", palGlow); c("sky", palSky); o += "\n\n";
    }
    static const char* matNames[] = {"box", "accent", "wallbang", "metal", "glow", "far", "water", "lava", "trim", "clip"};
    for (auto& br : brushes) if (!br.generated) box(matNames[br.material], br.box);
    o += "\n";
    static const char* teams[] = {"a", "b", "any", "race"};
    for (auto& sp : spawns) {
        std::string tn = isStagedSpawn(sp.team) ? std::string(spawnDefends(sp.team) ? "def" : "att") + char('0' + spawnStage(sp.team)) : teams[sp.team & 3];
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
        for (auto& p : g.parts) { snprintf(kw, sizeof kw, "part %d", p.material); box(kw, p.box); }
    }
    for (auto& a : checkpoints) box("checkpoint", a);
    for (auto& p : payloadPath) { float v[3] = {p.x, p.y, p.z}; o += "path"; nums(3, v); o += "\n"; }
    for (auto& l : lights) { float v[5] = {l.pos.x, l.pos.y, l.pos.z, l.radius, l.intensity}; o += "light"; nums(5, v); o += "\n"; }
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
        case 'b': snprintf(t, sizeof t, "b %s %d %d %s", verb, i, m.brushes[i].material, fmtBox(m.brushes[i].box).c_str()); break;
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
            if (verb == "ins") ok = i >= 0 && (size_t)i <= n && n < kMaxMapBrushes && m.insertBrush((size_t)i, b);
            else ok = i >= 0 && (size_t)i < n && m.setBrush((size_t)i, b);
        }
        if (ok) m.sourceStale = true;
        return ok;
    }
    if (kind == "g" && verb == "part") {
        Brush part;
        if (!(i >= 0 && (size_t)i < m.groups.size() && m.groups[i].type == Map::GROUP_CUSTOM && m.groups[i].parts.size() < kMaxGroupParts
              && (bool)(in >> part.material) && validMapMaterial(part.material) && readBox(part.box)))
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
        if (!(in >> sp.team) || sp.team < 0 || sp.team > stagedSpawn(kMaxSpawnStage, true) || !readVec(sp.pos) || !(in >> yaw) || !okNum(yaw)) return false;
        sp.yaw = deg2rad(yaw);
        return true;
    }, 1, kMaxMapSpawns);
    else if (kind == "l") ok = edit(m.lights, [&](Map::Light& l) {
        return readVec(l.pos) && (bool)(in >> l.radius >> l.intensity) && validMapLight(l.radius, l.intensity);
    }, 0, kMaxMapLights);
    else if (kind == "z") ok = edit(m.bombSites, readBox, 0, kMaxMapSites);
    else if (kind == "c") ok = edit(m.checkpoints, readBox, 0, kMaxMapCheckpoints);
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
    std::vector<Brush> parts;
    for (auto& g : groups) groupBoxes(g, parts);
    for (auto& b : parts) { b.generated = true; brushes.push_back(b); }
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
        if (b.material >= 5) continue; // far scenery, water / lava surfaces: never collide (clip: not part of the bounds)
        bounds.mn = vmin(bounds.mn, b.box.mn);
        bounds.mx = vmax(bounds.mx, b.box.mx);
        hasWallbang |= b.material == 2;
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

bool Map::buildGrid() {
    lava.clear();   // (touch it and die)
    for (auto& b : brushes) if (b.material == 7) lava.push_back(b.box);
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
    for (auto& br : brushes) {
        if (!gridSolid(br)) continue;
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
    bool wallbang = hasWallbang || (op != 2 && b.material == 2);
    bool oldGone = op == 1 || old.material >= 5 || inside(old.box, bounds, true), newIn = op == 2 || b.material >= 5 || inside(b.box, bounds, false);
    if (!oldGone || !newIn || (op != 1 && old.material == 2) || brushes.size() + (op == 1) - (op == 2) == 0) {
        nb = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
        wallbang = false;
        auto add = [&](const Brush& x) { if (x.material >= 5) return; nb.mn = vmin(nb.mn, x.box.mn); nb.mx = vmax(nb.mx, x.box.mx); wallbang |= x.material == 2; };
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
    if (old.material == 7 || (op != 2 && b.material == 7)) {
        lava.clear();
        for (auto& x : brushes) if (x.material == 7) lava.push_back(x.box);
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
    if (op != 0) {   // the brushes after i moved up or down one (the whole array, spare slots too: one vectorized pass)
        uint16_t* d = cellBrushes.data();
        size_t n = cellBrushes.size();
        if (op == 1) for (size_t k = 0; k < n; k++) d[k] = (uint16_t)(d[k] + (d[k] >= ix));
        else for (size_t k = 0; k < n; k++) d[k] = (uint16_t)(d[k] - (d[k] > ix));
    }
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
            for (uint32_t k = cells[c].start, e = cells[c].end; k < e; k++) f(brushes[cellBrushes[k]]);
        }
}

float Map::sweepAxis(const AABB& box, int axis, float delta) const {
    if (delta == 0.0f) return 0.0f;
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    constexpr float eps = 0.001f;
    AABB q = box;
    if (delta > 0) q.mx[axis] += delta; else q.mn[axis] += delta;
    auto clip = [&](const AABB& b) {
        if (box.mx[a1] <= b.mn[a1] || box.mn[a1] >= b.mx[a1]) return;
        if (box.mx[a2] <= b.mn[a2] || box.mn[a2] >= b.mx[a2]) return;
        if (delta > 0) {
            if (box.mx[axis] <= b.mn[axis] + eps) {
                float allowed = b.mn[axis] - box.mx[axis] - eps;
                if (allowed < delta) delta = std::max(allowed, 0.0f);
            }
        } else {
            if (box.mn[axis] >= b.mx[axis] - eps) {
                float allowed = b.mx[axis] - box.mn[axis] + eps;
                if (allowed > delta) delta = std::min(allowed, 0.0f);
            }
        }
    };
    forBox(q, [&](const Brush& br) { clip(br.box); });
    for (const AABB& b : dynSolids) clip(b);
    return delta;
}

bool Map::boxSolid(const AABB& box) const {
    bool hit = false;
    forBox(box, [&](const Brush& b) { hit = hit || box.overlaps(b.box); });
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

float Map::raycast(const Vec3& o, const Vec3& dir, float maxDist, Vec3* normal) const {
    Vec3 inv = safeInv(dir);
    float best = -1.0f;
    int bestAxis = -1;
    walkCells(*this, o, dir, maxDist, [&](size_t c, float tExit) {
        for (uint32_t k = cells[c].start, e = cells[c].end; k < e; k++) {
            if (brushes[cellBrushes[k]].material == 9) continue;   // clip: players only
            int axis;
            float t = rayAABB(o, inv, brushes[cellBrushes[k]].box, best < 0 ? maxDist : best, &axis);
            if (t >= 0 && (best < 0 || t < best)) { best = t; bestAxis = axis; }
        }
        return !(best >= 0 && best <= tExit); // nearest hit is inside this cell: done
    });
    for (const AABB& b : dynSolids) {
        int axis;
        float t = rayAABB(o, inv, b, best < 0 ? maxDist : best, &axis);
        if (t >= 0 && (best < 0 || t < best)) { best = t; bestAxis = axis; }
    }
    if (best >= 0 && normal) {
        *normal = Vec3(0, 0, 0);
        if (bestAxis >= 0) (*normal)[bestAxis] = dir[bestAxis] > 0 ? -1.0f : 1.0f;
    }
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
            if (br.material == 9) continue;   // clip: players only
            float t = rayAABB(o, inv, br.box, best < 0 ? maxDist : best);
            if (t < 0) continue;
            if (br.material == 2) { if (bang < 0 || t < bang) bang = t; }
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

bool Map::raycastAny(const Vec3& o, const Vec3& dir, float maxDist, bool hitClip) const {
    Vec3 inv = safeInv(dir);
    bool hit = false;
    walkCells(*this, o, dir, maxDist, [&](size_t c, float) {
        for (uint32_t k = cells[c].start, e = cells[c].end; k < e && !hit; k++) {
            const Brush& br = brushes[cellBrushes[k]];
            if (br.material == 9 && !hitClip) continue;   // clip: players only (light, sight and shots pass)
            hit = rayAABB(o, inv, br.box, maxDist) >= 0;
        }
        return !hit;
    });
    for (const AABB& b : dynSolids) hit = hit || rayAABB(o, inv, b, maxDist) >= 0;
    return hit;
}

bool Map::supportsMode(int mode) const {
    if (allowedModes && (mode < 0 || mode >= 32 || !(allowedModes >> mode & 1))) return false;
    return canSupportMode(mode);
}

bool Map::canSupportMode(int mode) const {
    auto has = [&](int t) { for (auto& s : spawns) if (s.team == t) return true; return false; };
    bool teams = has(SPAWN_A) && has(SPAWN_B);
    switch (mode) {
        case MODE_VS:
        case MODE_DUEL:
        case MODE_ARENA: return teams;
        case MODE_TOURNEY: return !spawns.empty();
        case MODE_BOMB: return teams && !bombSites.empty();
        case MODE_RACE: return has(SPAWN_RACE) && !checkpoints.empty();
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
