#include "map.h"
#include "gamedefs.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <type_traits>

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

std::string downloadedMapPath(const std::string& name, uint32_t crc) {
    char hex[16];
    snprintf(hex, sizeof hex, "%08x", crc);
    return findDataPath("maps") + "/downloads/" + name + "-" + hex + ".qmap";
}

bool saveMapFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), (std::streamsize)text.size());
        if (!f) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
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
    allowedModes = 0;
    rankedLadders = 0;
    crc = crc32(text.data(), text.size());
    source = text;
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
        } else if (kw == "ranked") {
            std::string ln;
            while (ls >> ln) { int v = arenaVariantFromName(ln.c_str()); if (v >= 0) rankedLadders |= (uint16_t)(1u << v); else ok = false; }
        } else if (kw == "modes") {
            std::string mn;
            while (ls >> mn) { int m = modeFromName(mn.c_str()); if (m >= 0) allowedModes |= 1u << m; else ok = false; }
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

std::string Map::serialize() const {
    std::string o = "# made with the quark map editor\n";
    char b[256];
    auto box = [&](const char* kw, const AABB& a) {
        snprintf(b, sizeof b, "%s %g %g %g %g %g %g\n", kw, a.mn.x, a.mn.y, a.mn.z, a.mx.x, a.mx.y, a.mx.z);
        o += b;
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
    snprintf(b, sizeof b, "sun %g %g %g\n\n", sunDir.x, sunDir.y, sunDir.z);
    o += b;
    for (auto& br : brushes) box(br.material == 1 ? "accent" : br.material == 2 ? "wallbang" : "box", br.box);
    o += "\n";
    static const char* teams[] = {"a", "b", "any", "race"};
    for (auto& sp : spawns) {
        snprintf(b, sizeof b, "spawn %s %g %g %g %g\n", teams[sp.team & 3], sp.pos.x, sp.pos.y, sp.pos.z, std::round(sp.yaw * 180.0f / PI));
        o += b;
    }
    for (auto& a : bombSites) box("site", a);
    if (hasHill) box("hill", hill);
    for (auto& a : checkpoints) box("checkpoint", a);
    for (auto& p : payloadPath) { snprintf(b, sizeof b, "path %g %g %g\n", p.x, p.y, p.z); o += b; }
    for (auto& l : lights) { snprintf(b, sizeof b, "light %g %g %g %g %g\n", l.pos.x, l.pos.y, l.pos.z, l.radius, l.intensity); o += b; }
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
        while (!title.empty() && title[0] == ' ') title.erase(0, 1);
        if (title.size() > 40) return false;
        for (char c : title) if ((unsigned char)c < 32 || (unsigned char)c > 126 || c == '#') return false;
        m.title = title;
        return true;
    }
    if (kind == "m") {
        uint32_t mask;
        if (!(in >> mask) || mask >= (1u << MODE_COUNT)) return false;
        m.allowedModes = mask;
        return true;
    }
    if (kind == "f") {
        int f;
        if (!(in >> f) || f < -1 || f >= MODE_COUNT) return false;
        m.preferredMode = f;
        return true;
    }
    int i;
    if (!(in >> verb >> i) || (verb != "ins" && verb != "set" && verb != "del")) return false;
    auto okNum = [](float v) { return std::isfinite(v) && std::fabs(v) <= 4096.0f; };
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
    if (kind == "b") ok = edit(m.brushes, [&](Brush& b) { return (bool)(in >> b.material) && b.material >= 0 && b.material <= 2 && readBox(b.box); }, 1, 60000);
    else if (kind == "s") ok = edit(m.spawns, [&](SpawnPoint& sp) {
        float yaw;
        if (!(in >> sp.team) || sp.team < 0 || sp.team > 3 || !readVec(sp.pos) || !(in >> yaw) || !okNum(yaw)) return false;
        sp.yaw = deg2rad(yaw);
        return true;
    }, 1, 256);
    else if (kind == "l") ok = edit(m.lights, [&](Map::Light& l) {
        return readVec(l.pos) && (bool)(in >> l.radius >> l.intensity) && l.radius >= 0.5f && l.radius <= 100 && l.intensity > 0 && l.intensity <= 10;
    }, 0, 512);
    else if (kind == "z") ok = edit(m.bombSites, readBox, 0, 16);
    else if (kind == "c") ok = edit(m.checkpoints, readBox, 0, 128);
    else if (kind == "p") ok = edit(m.payloadPath, readVec, 0, 512);
    else if (kind == "h") {
        int has;
        AABB b;
        ok = verb == "set" && (bool)(in >> has) && readBox(b);
        if (ok) { m.hasHill = has != 0; m.hill = b; }
    }
    if (ok && kind == "b") m.rebuild();
    return ok;
}

void Map::rebuild() {
    bounds = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    hasWallbang = false;
    for (auto& b : brushes) {
        bounds.mn = vmin(bounds.mn, b.box.mn);
        bounds.mx = vmax(bounds.mx, b.box.mx);
        hasWallbang |= b.material == 2;
    }
    if (brushes.empty()) bounds = {{-1, -1, -1}, {1, 1, 1}};
    buildGrid();
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
