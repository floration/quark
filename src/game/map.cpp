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

const std::string& executableDir() { return g_exeDir; }

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
    brushes.clear(); spawns.clear(); bombSites.clear(); checkpoints.clear(); payloadPath.clear(); lights.clear();
    hasHill = false;
    hasWallbang = false;
    preferredMode = -1;
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
        } else if (kw == "box" || kw == "accent" || kw == "wallbang") {
            Brush b;
            ok = readBox(ls, b.box);
            b.material = kw == "accent" ? 1 : kw == "wallbang" ? 2 : 0;
            hasWallbang |= b.material == 2;
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
        } else if (kw == "for") {
            std::string mode;
            ok = (bool)(ls >> mode);
            if (ok) preferredMode = modeFromName(mode.c_str());
        } else if (kw == "light") {
            Light l;
            ok = (bool)(ls >> l.pos.x >> l.pos.y >> l.pos.z >> l.radius);
            ls >> l.intensity;
            if (ok) lights.push_back(l);
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
    buildGrid();
    return true;
}

void Map::buildGrid() {
    gridX0 = std::floor(bounds.mn.x / kCell) * kCell;
    gridZ0 = std::floor(bounds.mn.z / kCell) * kCell;
    gridW = std::max(1, (int)std::ceil((bounds.mx.x - gridX0) / kCell));
    gridH = std::max(1, (int)std::ceil((bounds.mx.z - gridZ0) / kCell));
    std::vector<std::vector<uint16_t>> cells((size_t)gridW * gridH);
    for (size_t i = 0; i < brushes.size(); i++) {
        const AABB& b = brushes[i].box;
        int x0 = std::max(0, (int)((b.mn.x - gridX0) / kCell)), x1 = std::min(gridW - 1, (int)((b.mx.x - gridX0) / kCell));
        int z0 = std::max(0, (int)((b.mn.z - gridZ0) / kCell)), z1 = std::min(gridH - 1, (int)((b.mx.z - gridZ0) / kCell));
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) cells[(size_t)z * gridW + x].push_back((uint16_t)i);
    }
    cellStart.assign(cells.size() + 1, 0);
    cellBrushes.clear();
    for (size_t c = 0; c < cells.size(); c++) {
        cellStart[c] = (uint32_t)cellBrushes.size();
        cellBrushes.insert(cellBrushes.end(), cells[c].begin(), cells[c].end());
    }
    cellStart[cells.size()] = (uint32_t)cellBrushes.size();
}

template <class F> void Map::forBox(const AABB& b, F f) const {
    int x0 = std::max(0, (int)std::floor((b.mn.x - gridX0) / kCell)), x1 = std::min(gridW - 1, (int)std::floor((b.mx.x - gridX0) / kCell));
    int z0 = std::max(0, (int)std::floor((b.mn.z - gridZ0) / kCell)), z1 = std::min(gridH - 1, (int)std::floor((b.mx.z - gridZ0) / kCell));
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            size_t c = (size_t)z * gridW + x;
            for (uint32_t k = cellStart[c]; k < cellStart[c + 1]; k++) f(brushes[cellBrushes[k]]);
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
        for (uint32_t k = cellStart[c]; k < cellStart[c + 1]; k++) {
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
        for (uint32_t k = cellStart[c]; k < cellStart[c + 1]; k++) {
            const Brush& br = brushes[cellBrushes[k]];
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

bool Map::raycastAny(const Vec3& o, const Vec3& dir, float maxDist) const {
    Vec3 inv = safeInv(dir);
    bool hit = false;
    walkCells(*this, o, dir, maxDist, [&](size_t c, float) {
        for (uint32_t k = cellStart[c]; k < cellStart[c + 1] && !hit; k++)
            hit = rayAABB(o, inv, brushes[cellBrushes[k]].box, maxDist) >= 0;
        return !hit;
    });
    for (const AABB& b : dynSolids) hit = hit || rayAABB(o, inv, b, maxDist) >= 0;
    return hit;
}

bool Map::supportsMode(int mode) const {
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
