#include "map.h"
#include "gamedefs.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
static std::string g_exeDir;

void setExecutableDir(const char* argv0) {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(argv0), ec);
    g_exeDir = p.parent_path().string();
}

std::string findDataPath(const std::string& rel) {
    std::error_code ec;
    const std::string candidates[] = {
        rel,
        g_exeDir + "/" + rel,
        g_exeDir + "/../" + rel,
        g_exeDir + "/../../" + rel,
        g_exeDir + "/../Resources/" + rel, // macOS bundle
    };
    for (auto& c : candidates)
        if (!c.empty() && fs::exists(c, ec)) return c;
    return rel;
}

std::vector<std::string> listMaps() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(findDataPath("maps"), ec))
        if (e.path().extension() == ".qmap") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

uint32_t crc32(const void* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool Map::load(const std::string& mapName, std::string* err) {
    std::string path = findDataPath("maps/" + mapName + ".qmap");
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "map not found: " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    name = mapName;
    return parse(ss.str(), err);
}

static bool readBox(std::istringstream& ls, AABB& b) {
    Vec3 a, c;
    if (!(ls >> a.x >> a.y >> a.z >> c.x >> c.y >> c.z)) return false;
    b.mn = vmin(a, c);
    b.mx = vmax(a, c);
    return true;
}

bool Map::parse(const std::string& text, std::string* err) {
    brushes.clear(); spawns.clear(); bombSites.clear(); checkpoints.clear(); payloadPath.clear();
    hasHill = false;
    crc = crc32(text.data(), text.size());
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    bounds = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    while (std::getline(in, line)) {
        lineNo++;
        size_t h = line.find('#');
        if (h != std::string::npos) line.resize(h);
        std::istringstream ls(line);
        std::string kw;
        if (!(ls >> kw)) continue;
        bool ok = true;
        if (kw == "title") {
            std::getline(ls, title);
            while (!title.empty() && title[0] == ' ') title.erase(0, 1);
        } else if (kw == "box" || kw == "accent") {
            Brush b;
            ok = readBox(ls, b.box);
            b.material = kw == "accent" ? 1 : 0;
            if (ok) {
                brushes.push_back(b);
                bounds.mn = vmin(bounds.mn, b.box.mn);
                bounds.mx = vmax(bounds.mx, b.box.mx);
            }
        } else if (kw == "spawn") {
            std::string team;
            SpawnPoint s;
            ok = (bool)(ls >> team >> s.pos.x >> s.pos.y >> s.pos.z);
            float yawDeg = 0;
            ls >> yawDeg;
            s.yaw = deg2rad(yawDeg);
            s.team = team == "a" ? SPAWN_A : team == "b" ? SPAWN_B : team == "race" ? SPAWN_RACE : SPAWN_ANY;
            if (ok) spawns.push_back(s);
        } else if (kw == "site") {
            AABB b; ok = readBox(ls, b); if (ok) bombSites.push_back(b);
        } else if (kw == "hill") {
            ok = readBox(ls, hill); hasHill = ok;
        } else if (kw == "checkpoint") {
            AABB b; ok = readBox(ls, b); if (ok) checkpoints.push_back(b);
        } else if (kw == "path") {
            Vec3 p; ok = (bool)(ls >> p.x >> p.y >> p.z); if (ok) payloadPath.push_back(p);
        } else if (kw == "sun") {
            Vec3 d; ok = (bool)(ls >> d.x >> d.y >> d.z); if (ok) sunDir = normalize(d);
        } else {
            ok = false;
        }
        if (!ok) {
            if (err) *err = "parse error on line " + std::to_string(lineNo) + ": " + line;
            return false;
        }
    }
    if (brushes.empty() || spawns.empty()) {
        if (err) *err = "map needs at least one box and one spawn";
        return false;
    }
    return true;
}

float Map::sweepAxis(const AABB& box, int axis, float delta) const {
    if (delta == 0.0f) return 0.0f;
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    constexpr float eps = 0.001f;
    for (const Brush& br : brushes) {
        const AABB& b = br.box;
        if (box.mx[a1] <= b.mn[a1] || box.mn[a1] >= b.mx[a1]) continue;
        if (box.mx[a2] <= b.mn[a2] || box.mn[a2] >= b.mx[a2]) continue;
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
    }
    return delta;
}

bool Map::boxSolid(const AABB& box) const {
    for (const Brush& b : brushes)
        if (box.overlaps(b.box)) return true;
    return false;
}

float Map::raycast(const Vec3& o, const Vec3& dir, float maxDist, Vec3* normal) const {
    Vec3 inv = safeInv(dir);
    float best = -1.0f;
    int bestAxis = -1;
    for (const Brush& b : brushes) {
        int axis;
        float t = rayAABB(o, inv, b.box, best < 0 ? maxDist : best, &axis);
        if (t >= 0 && (best < 0 || t < best)) { best = t; bestAxis = axis; }
    }
    if (best >= 0 && normal) {
        *normal = Vec3(0, 0, 0);
        if (bestAxis >= 0) (*normal)[bestAxis] = dir[bestAxis] > 0 ? -1.0f : 1.0f;
    }
    return best;
}

bool Map::supportsMode(int mode) const {
    auto has = [&](int t) { for (auto& s : spawns) if (s.team == t) return true; return false; };
    bool teams = has(SPAWN_A) && has(SPAWN_B);
    switch (mode) {
        case MODE_VS: return teams;
        case MODE_BOMB: return teams && !bombSites.empty();
        case MODE_RACE: return has(SPAWN_RACE) && !checkpoints.empty();
        case MODE_KOTH: return teams && hasHill;
        case MODE_PAYLOAD: return teams && payloadPath.size() >= 2;
        case MODE_FFA: return !spawns.empty();
    }
    return false;
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
