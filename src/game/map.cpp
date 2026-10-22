#include "official_map_data.h"
#include "map.h"
#include "gamedefs.h"
#include <cctype>
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

int resetOfficialMaps() {
    std::error_code ec;
    std::string dir = findDataPath("maps");
    fs::create_directories(dir, ec);
    int n = 0;
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) {
        std::ofstream f(dir + "/" + m->name + ".qmap", std::ios::binary | std::ios::trunc);
        if (!f.write((const char*)m->data, (std::streamsize)m->size)) continue;
        fs::remove(dir + "/" + m->name + ".qlm", ec);
        n++;
    }
    for (const char* old : {"arena", "tug", "duel", "sprint"}) { // retired built-in maps left by older versions
        fs::remove(dir + "/" + old + ".qmap", ec);
        fs::remove(dir + "/" + old + ".qlm", ec);
    }
    return n;
}

std::string mapsFolder() {
    std::error_code ec;
    auto p = fs::absolute(findDataPath("maps"), ec);
    return ec ? findDataPath("maps") : p.string();
}

std::vector<std::string> listMaps() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(findDataPath("maps"), ec))
        if (e.path().extension() == ".qmap") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

bool validMapName(const std::string& name) {
    if (name.empty() || name.size() > 40) return false;
    for (char c : name) if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
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

bool Map::load(const std::string& requested, std::string* err) {
    // renamed maps: old configs / votes for these still work
    std::string mapName = requested == "arena" ? "bastion" : requested == "tug" ? "canal" : requested;
    if (!validMapName(mapName)) { if (err) *err = "bad map name: " + mapName; return false; }
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
    try {
        return parseText(text, err);
    } catch (const std::exception& e) {   // (bad_alloc and friends: a hostile file is an error, not the end of the game)
        if (err) *err = std::string("map rejected: ") + e.what();
        return false;
    }
}

bool Map::parseText(const std::string& text, std::string* err) {
    brushes.clear(); spawns.clear(); bombSites.clear(); checkpoints.clear(); ladders.clear(); ladderAuto.clear(); groups.clear(); payloadPath.clear(); lights.clear(); points.clear(); cartChecks.clear();
    hasHill = false;
    hasWallbang = false;
    hasPalette = false;
    hasVoid = false;
    holdFire = 0;
    indoor = false;
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
            ok = (bool)(ls >> team >> s.pos.x >> s.pos.y >> s.pos.z);
            float yawDeg = 0;
            ls >> yawDeg;
            s.yaw = deg2rad(yawDeg);
            s.team = team == "a" ? SPAWN_A : team == "b" ? SPAWN_B : team == "race" ? SPAWN_RACE : SPAWN_ANY;
            if ((team.rfind("att", 0) == 0 || team.rfind("def", 0) == 0) && team.size() == 4 && team[3] >= '0' && team[3] <= '0' + kMaxSpawnStage)
                s.team = stagedSpawn(team[3] - '0', team[0] == 'd');
            if (ok) spawns.push_back(s);
        } else if (kw == "site") {
            AABB b; ok = readBox(ls, b); if (ok) bombSites.push_back(b);
        } else if (kw == "cartcheck") {   // a payload checkpoint this far along the track (0..1)
            float f; ok = (bool)(ls >> f) && f > 0 && f < 1;
            if (ok) { cartChecks.push_back(f); std::sort(cartChecks.begin(), cartChecks.end()); }
        } else if (kw == "point") {
            AABB b; ok = readBox(ls, b); if (ok) points.push_back(b);
        } else if (kw == "hill") {
            ok = readBox(ls, hill); hasHill = ok;
        } else if (kw == "ladder") {
            AABB b; ok = readBox(ls, b);
            std::string flag;
            if (ok) { ladders.push_back(b); ladderAuto.push_back(ls >> flag && flag == "auto"); }
        } else if (kw == "group") {
            std::string t; Group g;
            ok = (bool)(ls >> t >> g.material >> g.dir) && readBox(ls, g.box) && g.material >= 0 && g.material <= 7 && g.dir >= 0 && g.dir < 4;
            g.type = t == "room" ? GROUP_ROOM : t == "stairs" ? GROUP_STAIRS : GROUP_CUSTOM;
            ok = ok && (t == "room" || t == "stairs" || t == "custom");
            if (ok) groups.push_back(g);
        } else if (kw == "part") {
            Brush b; ok = !groups.empty() && groups.back().type == GROUP_CUSTOM && groups.back().parts.size() < kMaxGroupParts && (bool)(ls >> b.material) && b.material >= 0 && b.material <= 7 && readBox(ls, b.box);
            if (ok) groups.back().parts.push_back(b);
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
        } else if (kw == "indoor") { // "indoor <ceiling y>": roofed all over (menu / shot cameras stay under the roof)
            ok = (bool)(ls >> ceilingY); indoor = ok;
        } else if (kw == "holdfire") {
            ok = (bool)(ls >> holdFire) && holdFire >= 0 && holdFire <= 30;
        } else if (kw == "void") {
            ok = (bool)(ls >> voidY); hasVoid = ok;
        } else if (kw == "palette") {
            std::string k; Vec3 c;
            while (ok && ls >> k) {
                ok = (bool)(ls >> c.x >> c.y >> c.z);
                if (k == "base") palBase = c; else if (k == "accent") palAccent = c; else if (k == "metal") palMetal = c;
                else if (k == "glow") palGlow = c; else if (k == "sky") palSky = c; else ok = false;
            }
            hasPalette = ok;
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
    for (auto& b : brushes) if (!box(b.box) || b.material < 0 || b.material > 9) return fail("a box is out of range (coordinates within 4096 m)");
    for (auto& s : spawns) if (!vec(s.pos) || !std::isfinite(s.yaw)) return fail("a spawn is out of range");
    for (auto& l : lights)
        if (!vec(l.pos) || !(l.radius >= 0.5f && l.radius <= 100) || !(l.intensity >= 0 && l.intensity <= 10)) return fail("a light is out of range (radius 0.5-100, intensity 0-10)");
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
    if (hasVoid) { snprintf(b, sizeof b, "void %g\n", voidY); o += b; }
    if (indoor) { snprintf(b, sizeof b, "indoor %g\n", ceilingY); o += b; }
    if (holdFire > 0) { snprintf(b, sizeof b, "holdfire %g\n", holdFire); o += b; }
    if (hasPalette) {
        auto c = [&](const char* k, Vec3 v) { snprintf(b, sizeof b, " %s %g %g %g", k, v.x, v.y, v.z); o += b; };
        o += "palette"; c("base", palBase); c("accent", palAccent); c("metal", palMetal); c("glow", palGlow); c("sky", palSky); o += "\n\n";
    }
    static const char* matNames[] = {"box", "accent", "wallbang", "metal", "glow", "far", "water", "lava", "trim", "clip"};
    for (auto& br : brushes) if (!br.generated) box(matNames[br.material], br.box);
    o += "\n";
    static const char* teams[] = {"a", "b", "any", "race"};
    for (auto& sp : spawns) {
        std::string tn = isStagedSpawn(sp.team) ? std::string(spawnDefends(sp.team) ? "def" : "att") + char('0' + spawnStage(sp.team)) : teams[sp.team & 3];
        snprintf(b, sizeof b, "spawn %s %g %g %g %g\n", tn.c_str(), sp.pos.x, sp.pos.y, sp.pos.z, std::round(sp.yaw * 180.0f / PI));
        o += b;
    }
    for (auto& a : bombSites) box("site", a);
    if (hasHill) box("hill", hill);
    for (auto& a : points) box("point", a);
    for (float f : cartChecks) { snprintf(b, sizeof b, "cartcheck %g\n", f); o += b; }
    for (size_t k = 0; k < ladders.size(); k++) {
        snprintf(b, sizeof b, "ladder %g %g %g %g %g %g%s\n", ladders[k].mn.x, ladders[k].mn.y, ladders[k].mn.z, ladders[k].mx.x, ladders[k].mx.y, ladders[k].mx.z,
                 k < ladderAuto.size() && ladderAuto[k] ? " auto" : "");
        o += b;
    }
    static const char* groupTypes[] = {"custom", "room", "stairs"};
    for (auto& g : groups) {
        snprintf(b, sizeof b, "group %s %d %d %g %g %g %g %g %g\n", groupTypes[g.type], g.material, g.dir, g.box.mn.x, g.box.mn.y, g.box.mn.z, g.box.mx.x, g.box.mx.y, g.box.mx.z);
        o += b;
        for (auto& p : g.parts) { snprintf(b, sizeof b, "part %d %.6g %.6g %.6g %.6g %.6g %.6g\n", p.material, p.box.mn.x, p.box.mn.y, p.box.mn.z, p.box.mx.x, p.box.mx.y, p.box.mx.z); o += b; }
    }
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
    if (!(in >> verb >> i) || (verb != "ins" && verb != "set" && verb != "del" && !(verb == "part" && kind == "g"))) return false;
    if (kind == "b") {   // generated boxes (ladders' rails, groups' parts) sit after the editable ones and aren't edited directly
        int editable = (int)m.editableBrushes();
        if (i < 0 || i > editable || (verb != "ins" && i == editable) || (verb == "del" && editable <= 1)) return false;
    }
    // the edit happens on m itself; on any failure m goes back to this copy (the source text isn't part of it)
    std::string source = std::move(m.source);
    Map before = m;
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
    if (kind == "b") ok = edit(m.brushes, [&](Brush& b) { return (bool)(in >> b.material) && b.material >= 0 && b.material <= 7 && readBox(b.box); }, 1, 60000);
    else if (kind == "s") ok = edit(m.spawns, [&](SpawnPoint& sp) {
        float yaw;
        if (!(in >> sp.team) || sp.team < 0 || sp.team > stagedSpawn(kMaxSpawnStage, true) || !readVec(sp.pos) || !(in >> yaw) || !okNum(yaw)) return false;
        sp.yaw = deg2rad(yaw);
        return true;
    }, 1, 256);
    else if (kind == "l") ok = edit(m.lights, [&](Map::Light& l) {
        return readVec(l.pos) && (bool)(in >> l.radius >> l.intensity) && l.radius >= 0.5f && l.radius <= 100 && l.intensity > 0 && l.intensity <= 10;
    }, 0, 512);
    else if (kind == "z") ok = edit(m.bombSites, readBox, 0, 16);
    else if (kind == "c") ok = edit(m.checkpoints, readBox, 0, 128);
    else if (kind == "p") ok = edit(m.payloadPath, readVec, 0, 512);
    else if (kind == "o") ok = edit(m.points, readBox, 0, 16);
    else if (kind == "d") { // ladders: the volume and whether it draws its own rails / rungs (kept in step)
        m.ladderAuto.resize(m.ladders.size(), 0);
        if (verb == "del") {
            ok = i >= 0 && (size_t)i < m.ladders.size();
            if (ok) { m.ladders.erase(m.ladders.begin() + i); m.ladderAuto.erase(m.ladderAuto.begin() + i); }
        } else {
            int au = 0;
            AABB b;
            ok = (bool)(in >> au) && readBox(b) && b.mx.y - b.mn.y <= 64;
            if (ok && verb == "ins") { ok = i >= 0 && (size_t)i <= m.ladders.size() && m.ladders.size() < 256; if (ok) { m.ladders.insert(m.ladders.begin() + i, b); m.ladderAuto.insert(m.ladderAuto.begin() + i, au != 0); } }
            else if (ok) { ok = i >= 0 && (size_t)i < m.ladders.size(); if (ok) { m.ladders[i] = b; m.ladderAuto[i] = au != 0; } }
        }
    }
    else if (kind == "g") { // groups: ins (empty) / set (keeps its parts) / del, and part to fill a custom one
        if (verb == "del") {
            ok = i >= 0 && (size_t)i < m.groups.size();
            if (ok) m.groups.erase(m.groups.begin() + i);
        } else {
            Map::Group g;
            ok = verb == "part" ? i >= 0 && (size_t)i < m.groups.size() && m.groups[i].type == Map::GROUP_CUSTOM && m.groups[i].parts.size() < 1024
                                  && (bool)(in >> g.material) && g.material >= 0 && g.material <= 7 && readBox(g.box)
                                : (bool)(in >> g.type >> g.material >> g.dir) && g.type >= 0 && g.type <= 2 && g.material >= 0 && g.material <= 7
                                  && g.dir >= 0 && g.dir < 4 && readBox(g.box) && g.box.mx.y - g.box.mn.y <= 64;
            if (ok && verb == "part") m.groups[i].parts.push_back({g.box, g.material});
            else if (ok && verb == "ins") { ok = i >= 0 && (size_t)i <= m.groups.size() && m.groups.size() < 256; if (ok) m.groups.insert(m.groups.begin() + i, g); }
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
    if (ok) ok = kind == "b" || kind == "d" || kind == "g" ? m.rebuild() : m.validate();
    if (!ok) m = std::move(before);
    m.source = std::move(source);
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

bool Map::buildGrid() {
    lava.clear();   // (touch it and die)
    for (auto& b : brushes) if (b.material == 7) lava.push_back(b.box);
    // validate() keeps the bounds within kMapExtent (the grid is at most 2048 x 2048 cells) and brushes under 65536
    // (16-bit indices); with no solid brush the bounds are the empty sentinel and the grid is one cell
    gridX0 = std::floor(bounds.mn.x / kCell) * kCell;
    gridZ0 = std::floor(bounds.mn.z / kCell) * kCell;
    gridW = std::max(1, (int)std::min(4096.0f, std::ceil((bounds.mx.x - gridX0) / kCell)));
    gridH = std::max(1, (int)std::min(4096.0f, std::ceil((bounds.mx.z - gridZ0) / kCell)));
    size_t cells = (size_t)gridW * gridH;
    auto span = [&](const AABB& b, int& x0, int& x1, int& z0, int& z1) {
        x0 = (int)clampf((b.mn.x - gridX0) / kCell, 0, (float)(gridW - 1)); x1 = (int)clampf((b.mx.x - gridX0) / kCell, 0, (float)(gridW - 1));
        z0 = (int)clampf((b.mn.z - gridZ0) / kCell, 0, (float)(gridH - 1)); z1 = (int)clampf((b.mx.z - gridZ0) / kCell, 0, (float)(gridH - 1));
    };
    auto solid = [&](const Brush& b) { return b.material < 5 || b.material == 9; }; // far scenery, water / lava: not solid, not hit by shots (clip: solid)
    // count per cell, then fill: one allocation, refused before it's made if it's too big
    size_t total = 0;
    for (auto& br : brushes) {
        if (!solid(br)) continue;
        int x0, x1, z0, z1;
        span(br.box, x0, x1, z0, z1);
        total += (size_t)(x1 - x0 + 1) * (z1 - z0 + 1);
        if (total > kMaxMapGridRefs) { cellStart.assign(2, 0); cellBrushes.clear(); gridW = gridH = 1; return false; }
    }
    cellStart.assign(cells + 1, 0);
    cellBrushes.assign(total, 0);
    for (auto& br : brushes) {
        if (!solid(br)) continue;
        int x0, x1, z0, z1;
        span(br.box, x0, x1, z0, z1);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) cellStart[(size_t)z * gridW + x + 1]++;
    }
    for (size_t c = 0; c < cells; c++) cellStart[c + 1] += cellStart[c];
    std::vector<uint32_t> fill(cellStart.begin(), cellStart.end() - 1);
    for (size_t i = 0; i < brushes.size(); i++) {   // (in brush order within each cell, as before)
        if (!solid(brushes[i])) continue;
        int x0, x1, z0, z1;
        span(brushes[i].box, x0, x1, z0, z1);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) cellBrushes[fill[(size_t)z * gridW + x]++] = (uint16_t)i;
    }
    return true;
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
        for (uint32_t k = cellStart[c]; k < cellStart[c + 1]; k++) {
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
