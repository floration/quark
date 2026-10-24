// quark_maptool: the registry, the map <-> JSON form, request map loading.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include "common/u8file.h"
#include "maptool.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/pmove.h"

static std::vector<Command>& registry() { static std::vector<Command> r; return r; }
const std::vector<Command>& maptoolCommands() { return registry(); }
CommandRegistrar::CommandRegistrar(const char* name, CmdFn fn, const char* help) { registry().push_back({name, fn, help}); }

void CmdCtx::emit(const Json& line) const {
    std::string s = line.dump();
    s += '\n';
    fwrite(s.data(), 1, s.size(), stdout);
    fflush(stdout);
}

int cmdError(Json& out, const std::string& msg) { out = Json::object(); out.set("error", msg); return 1; }
double maptoolNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

Json jvec(const Vec3& v) { Json a = Json::array(); a.push(v.x); a.push(v.y); a.push(v.z); return a; }
Json jbox(const AABB& b) { Json o = Json::object(); o.set("min", jvec(b.mn)); o.set("max", jvec(b.mx)); return o; }
Vec3 toVec(const Json& j, Vec3 def) { return j.isArr() && j.size() >= 3 ? Vec3(j[0].f(), j[1].f(), j[2].f()) : def; }
AABB toBox(const Json& j) {
    Vec3 a = toVec(j["min"]), b = toVec(j["max"]);
    return {vmin(a, b), vmax(a, b)};
}

const char* spawnTeamName(int t, char* buf) {
    static const char* teams[] = {"a", "b", "any", "race"};
    if (isStagedSpawn(t)) { snprintf(buf, 16, "%s%d", spawnDefends(t) ? "def" : "att", spawnStage(t) % 10); return buf; }
    return t >= 0 && t < 4 ? teams[t] : "any";
}
int spawnTeamFromName(const std::string& s) {
    if (s == "a") return SPAWN_A;
    if (s == "b") return SPAWN_B;
    if (s == "any") return SPAWN_ANY;
    if (s == "race") return SPAWN_RACE;
    if (s.size() == 4 && (s.compare(0, 3, "att") == 0 || s.compare(0, 3, "def") == 0) && s[3] >= '0' && s[3] <= '0' + kMaxSpawnStage)
        return stagedSpawn(s[3] - '0', s[0] == 'd');
    return -1;
}
int modeFromJson(const Json& j, int def) {
    if (j.isNum()) { int v = j.i(); return v >= 0 && v < MODE_COUNT ? v : def; }
    if (j.isStr()) { int v = modeFromName(j.str().c_str()); return v >= 0 ? v : def; }
    return def;
}

static const char* kMatNames[] = {"box", "accent", "wallbang", "metal", "glow", "far", "water", "lava", "trim", "clip"};

Json playerJson() {
    Json p = Json::object();
    p.set("halfWidth", kPlayerHalfWidth);
    p.set("height", kPlayerHeight);
    p.set("crouchHeight", kPlayerCrouchHeight);
    p.set("eye", kEyeHeight);
    p.set("crouchEye", kCrouchEyeHeight);
    p.set("stepUp", 0.55f);                 // pmove.cpp kStepHeight (file-local there)
    p.set("jumpHeight", 1.22f);             // a standing jump: kJumpSpeed 7.34 m/s up, kGravity 22 m/s^2 (pmove.cpp)
    p.set("runSpeed", 9.5f);                // pmove.cpp kGroundSpeed
    p.set("headFraction", kHeadFraction);   // the head box: the top 20% of the hull's height...
    p.set("headWidthFraction", kHeadWidthFraction);   // ...and 45% of its width, centred (pmove.cpp headBox)
    AABB hb = headBox(playerBox(Vec3(), false));
    p.set("headBox", jbox(hb));             // standing at the origin (times the body scale under size modifiers)
    p.set("grappleRange", kGrappleRange);
    Json scales = Json::object();
    scales.set("big", kBigScale); scales.set("tiny", kTinyScale);
    p.set("bodyScales", scales);
    return p;
}

Json mapToJson(const Map& m) {
    Json j = Json::object();
    char tb[16];
    j.set("name", m.name);
    j.set("title", m.title);
    j.set("formatVersion", m.formatVersion);
    j.set("handmade", m.handmade);
    j.set("humanMade", m.humanMade());
    j.set("setting", m.setting);
    j.set("preferredMode", m.preferredMode >= 0 ? Json(modeName(m.preferredMode)) : Json());
    Json am = Json::array(), sup = Json::array(), can = Json::array();
    for (int k = 0; k < MODE_COUNT; k++) {
        if (m.allowedModes >> k & 1) am.push(modeName(k));
        if (m.supportsMode(k)) sup.push(modeName(k));
        if (m.canSupportMode(k)) can.push(modeName(k));
    }
    j.set("allowedModes", am);
    j.set("modesSupported", sup);     // allowed and has what the mode needs (Map::supportsMode): what menus offer
    j.set("modesPossible", can);      // has what the mode needs (Map::canSupportMode), allowed or not
    Json rk = Json::array();
    for (int v = 0; v < 16; v++) if (m.rankedLadders >> v & 1) rk.push(arenaVariant(v).name);
    j.set("ranked", rk);
    j.set("sun", jvec(m.sunDir));
    if (m.hasPalette) {
        Json p = Json::object();
        p.set("base", jvec(m.palBase)); p.set("accent", jvec(m.palAccent)); p.set("metal", jvec(m.palMetal));
        p.set("glow", jvec(m.palGlow)); p.set("sky", jvec(m.palSky));
        j.set("palette", p);
    } else j.set("palette", Json());
    j.set("void", m.hasVoid ? Json(m.voidY) : Json());
    j.set("killY", m.killY());
    j.set("indoor", m.indoor ? Json(m.ceilingY) : Json());
    j.set("holdfire", m.holdFire);
    Json br = Json::array(), gen = Json::array();
    for (auto& b : m.brushes) {
        Json o = Json::object();
        o.set("mat", b.material); o.set("min", jvec(b.box.mn)); o.set("max", jvec(b.box.mx));
        (b.generated ? gen : br).push(std::move(o));
    }
    j.set("brushes", std::move(br));
    j.set("generated", std::move(gen));
    static const char* gt[] = {"custom", "room", "stairs"};
    Json gs = Json::array();
    for (auto& g : m.groups) {
        Json o = Json::object();
        o.set("type", gt[std::clamp(g.type, 0, 2)]); o.set("mat", g.material); o.set("dir", g.dir);
        o.set("min", jvec(g.box.mn)); o.set("max", jvec(g.box.mx));
        Json parts = Json::array();
        for (auto& p : g.parts) { Json q = Json::object(); q.set("mat", p.material); q.set("min", jvec(p.box.mn)); q.set("max", jvec(p.box.mx)); parts.push(std::move(q)); }
        o.set("parts", std::move(parts));
        gs.push(std::move(o));
    }
    j.set("groups", std::move(gs));
    Json sp = Json::array();
    for (auto& s : m.spawns) {
        Json o = Json::object();
        o.set("pos", jvec(s.pos)); o.set("yaw", s.yaw); o.set("team", spawnTeamName(s.team, tb));
        sp.push(std::move(o));
    }
    j.set("spawns", std::move(sp));
    auto boxes = [](const std::vector<AABB>& v) { Json a = Json::array(); for (auto& b : v) a.push(jbox(b)); return a; };
    j.set("sites", boxes(m.bombSites));
    j.set("hill", m.hasHill ? jbox(m.hill) : Json());
    j.set("points", boxes(m.points));
    j.set("checkpoints", boxes(m.checkpoints));
    Json path = Json::array();
    for (auto& p : m.payloadPath) path.push(jvec(p));
    j.set("path", std::move(path));
    Json cc = Json::array();
    for (float f : m.cartChecks) cc.push(f);
    j.set("cartChecks", std::move(cc));
    j.set("payloadLength", m.payloadPath.size() >= 2 ? m.payloadLength() : 0.0f);
    Json ld = Json::array();
    for (size_t k = 0; k < m.ladders.size(); k++) {
        Json o = jbox(m.ladders[k]);
        o.set("auto", k < m.ladderAuto.size() && m.ladderAuto[k] != 0);
        ld.push(std::move(o));
    }
    j.set("ladders", std::move(ld));
    Json st = Json::array();
    for (auto& s : m.stairs) {
        Json o = Json::object();
        o.set("x0", s.x0); o.set("z0", s.z0); o.set("x1", s.x1); o.set("z1", s.z1); o.set("base", s.base); o.set("top", s.top);
        o.set("axis", std::string(1, s.axis)); o.set("rising", s.rising); o.set("solid", s.solid);
        st.push(std::move(o));
    }
    j.set("stairs", std::move(st));
    Json li = Json::array();
    for (auto& l : m.lights) { Json o = Json::object(); o.set("pos", jvec(l.pos)); o.set("radius", l.radius); o.set("intensity", l.intensity); li.push(std::move(o)); }
    j.set("lights", std::move(li));
    j.set("bounds", jbox(m.bounds));
    Json mats = Json::array();
    for (auto* n : kMatNames) mats.push(n);
    j.set("materials", std::move(mats));
    Json w = Json::array();
    for (auto& s : m.warnings) w.push(s);
    j.set("warnings", std::move(w));
    j.set("player", playerJson());
    return j;
}

bool mapFromJson(const Json& j, Map& m, std::string& err) {
    if (!j.isObj()) { err = "map: not an object"; return false; }
    m = Map();
    m.name = j["name"].str();
    m.title = j["title"].str();
    m.handmade = j["handmade"].boolean(true);
    m.setting = j["setting"].str();
    m.preferredMode = j["preferredMode"].isNull() ? -1 : modeFromJson(j["preferredMode"], -1);
    for (auto& v : j["allowedModes"].items()) { int k = modeFromJson(v, -1); if (k >= 0) m.allowedModes |= 1u << k; }
    for (auto& v : j["ranked"].items()) {
        int k = v.isNum() ? v.i() : arenaVariantFromName(v.str().c_str());
        if (k >= 0 && k < 16) m.rankedLadders |= (uint16_t)(1u << k);
    }
    if (j.has("sun")) m.sunDir = toVec(j["sun"], m.sunDir);
    const Json& pal = j["palette"];
    if (pal.isObj()) {
        m.hasPalette = true;
        m.palBase = toVec(pal["base"]); m.palAccent = toVec(pal["accent"]); m.palMetal = toVec(pal["metal"]);
        m.palGlow = toVec(pal["glow"]); m.palSky = toVec(pal["sky"]);
    }
    if (j["void"].isNum()) { m.hasVoid = true; m.voidY = j["void"].f(); }
    if (j["indoor"].isNum()) { m.indoor = true; m.ceilingY = j["indoor"].f(); }
    m.holdFire = j["holdfire"].f(0);
    for (auto& b : j["brushes"].items()) {
        int mat = b["mat"].i(0);
        if (!validMapMaterial(mat)) { err = "brush material " + std::to_string(mat) + " isn't 0..9"; return false; }
        Brush br; br.material = mat; br.box = toBox(b);
        m.brushes.push_back(br);
    }
    for (auto& g : j["groups"].items()) {
        Map::Group gr;
        std::string t = g["type"].str("custom");
        gr.type = t == "room" ? Map::GROUP_ROOM : t == "stairs" ? Map::GROUP_STAIRS : Map::GROUP_CUSTOM;
        gr.material = g["mat"].i(0); gr.dir = g["dir"].i(0); gr.box = toBox(g);
        for (auto& p : g["parts"].items()) { Brush b; b.material = p["mat"].i(0); b.box = toBox(p); gr.parts.push_back(b); }
        m.groups.push_back(std::move(gr));
    }
    for (auto& s : j["spawns"].items()) {
        SpawnPoint sp;
        sp.pos = toVec(s["pos"]); sp.yaw = s["yaw"].f(0);
        int t = spawnTeamFromName(s["team"].str("any"));
        if (t < 0) { err = "spawn team '" + s["team"].str() + "' isn't a, b, any, race, att<N> or def<N>"; return false; }
        sp.team = t;
        m.spawns.push_back(sp);
    }
    for (auto& b : j["sites"].items()) m.bombSites.push_back(toBox(b));
    if (j["hill"].isObj()) { m.hasHill = true; m.hill = toBox(j["hill"]); }
    for (auto& b : j["points"].items()) m.points.push_back(toBox(b));
    for (auto& b : j["checkpoints"].items()) m.checkpoints.push_back(toBox(b));
    for (auto& p : j["path"].items()) m.payloadPath.push_back(toVec(p));
    for (auto& f : j["cartChecks"].items()) m.cartChecks.push_back(f.f());
    for (auto& l : j["ladders"].items()) { m.ladders.push_back(toBox(l)); m.ladderAuto.push_back(l["auto"].boolean() ? 1 : 0); }
    for (auto& s : j["stairs"].items()) {
        Map::Stairs st;
        st.x0 = s["x0"].f(); st.z0 = s["z0"].f(); st.x1 = s["x1"].f(); st.z1 = s["z1"].f(); st.base = s["base"].f(); st.top = s["top"].f();
        std::string ax = s["axis"].str("x");
        st.axis = ax == "z" ? 'z' : 'x';
        st.rising = s["rising"].f(1); st.solid = s["solid"].boolean(true);
        m.stairs.push_back(st);
    }
    for (auto& l : j["lights"].items()) { Map::Light L; L.pos = toVec(l["pos"]); L.radius = l["radius"].f(10); L.intensity = l["intensity"].f(1); m.lights.push_back(L); }
    return true;
}

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(utf8Path(path), std::ios::binary);   // (a UTF-8 path: the wide API on Windows)
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool loadRequestMap(const Json& req, Map& m, std::string& err) {
    std::string text, name = req["mapName"].str();
    if (name.empty() && (req["text"].isStr() || req["map"].isObj()) && req["name"].isStr()) name = req["name"].str();   // (text + name: the text, called that)
    if (req["text"].isStr()) text = req["text"].str();
    else if (req["file"].isStr()) {
        const std::string& path = req["file"].str();
        if (!readFile(path, text)) { err = "can't read " + path; return false; }
        if (name.empty()) {
            size_t s = path.find_last_of("/\\");
            name = path.substr(s == std::string::npos ? 0 : s + 1);
            if (name.size() > 5 && name.compare(name.size() - 5, 5, ".qmap") == 0) name.resize(name.size() - 5);
        }
    } else if (req["name"].isStr()) {
        m = Map();
        if (!m.load(req["name"].str(), &err)) { if (err.empty()) err = "no map named " + req["name"].str(); return false; }
        return true;
    } else if (req["map"].isObj()) {
        Map f;
        if (!mapFromJson(req["map"], f, err)) return false;
        text = f.serialize();
        if (name.empty()) name = f.name;
    } else { err = "no map: give \"text\", \"file\", \"name\" or \"map\""; return false; }
    m = Map();
    m.name = name.empty() ? "untitled" : name;
    if (!m.parse(text, &err)) { if (err.empty()) err = "the map doesn't parse"; return false; }
    return true;
}
