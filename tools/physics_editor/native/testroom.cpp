// The physics editor's built-in test room: made here as .qmap text (the game's own map format) and parsed with the
// game's Map::parse, so collision, ladders and steps behave exactly as in any map. The `physroom` command.
//
// Lanes side by side along z, every floor's top at y = 0, everything faced along +x (yaw -90):
//   flat    z -60..60    a 440 x 120 m floor: jumps, run-up, stopping, dashes, bhop chains (strafing curves wide)
//   wall    z 70..90     a wall across the lane at x 20: dash into it (bounce), run into it
//   ladder  z 100..120   an 8 m tower (x 20..30) with a ladder up its -x face
//   drop    z 130..150   a 20 m ledge (x -20..10) over the floor: falls
//   gaps    z 160..180   platforms over a pit 4 m down, gaps 2, 3 ... 16 m wide
//   stairs  z 190..210   0.5 m risers to 3 m, a landing, then 0.75 m risers to 6 m (over the 0.55 m step-up)
//   grapple z 220..260   a ceiling at 12 m over a 140 m floor
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>
#include "maptool.h"
#include "physics.h"

namespace phys {
namespace {

struct Builder {
    std::string o;
    char b[256];
    void box(float x0, float y0, float z0, float x1, float y1, float z1, const char* mat = "box") {
        snprintf(b, sizeof b, "%s %g %g %g %g %g %g\n", mat, x0, y0, z0, x1, y1, z1);
        o += b;
    }
    void line(const std::string& s) { o += s; o += "\n"; }
};

const float kGapWidths[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16};
constexpr float kPlatform = 6.0f;   // between gaps

std::vector<Gap> makeGaps() {
    std::vector<Gap> g;
    float x = 20;   // the start platform ends here
    for (float w : kGapWidths) { g.push_back({x, x + w}); x += w + kPlatform; }
    return g;
}

std::string makeRoom() {
    Builder B;
    B.line("# the physics editor's test room (tools/physics_editor/native/testroom.cpp): made in code, not a shipped map");
    B.line("version " + std::to_string(kMapFormatVersion));
    B.line("title Physics test room");
    B.line("setting a movement test hall");
    B.line("for ffa");
    B.line("sun 0.4 1 0.25");
    B.line("");
    // flat
    B.box(-20, -1, -60, 420, 0, 60);
    // wall
    B.box(-20, -1, 70, 40, 0, 90);
    B.box(20, 0, 70, 21, 6, 90, "accent");
    // ladder: a tower with the ladder on its -x face (the volume reaches a metre over the top, as prefabs.py makes them)
    B.box(-20, -1, 100, 40, 0, 120);
    B.box(20, 0, 100, 30, 8, 120, "metal");
    B.line("ladder 19.3 0 109.55 20 9 110.45 auto");
    // drop: a 20 m ledge
    B.box(-20, 0, 130, 10, 20, 150, "metal");
    B.box(-20, -1, 130, 80, 0, 150);
    // gaps: a pit 4 m down under the whole course, platforms at 0
    std::vector<Gap> gaps = makeGaps();
    B.box(-20, -5, 160, gaps.back().x1 + kPlatform + 20, -4, 180, "accent");
    B.box(-20, -1, 160, gaps.front().x0, 0, 180);
    for (size_t i = 0; i < gaps.size(); i++) {
        float x1 = i + 1 < gaps.size() ? gaps[i + 1].x0 : gaps[i].x1 + kPlatform + 20;
        B.box(gaps[i].x1, -1, 160, x1, 0, 180, i % 2 ? "box" : "metal");
    }
    // stairs: 6 x 0.5 m (x 10..16) to 3 m, a landing to x 20, 4 x 0.75 m (x 20..24) to 6 m, a top to x 32
    B.box(-20, -1, 190, 60, 0, 210);
    for (int i = 0; i < 6; i++) B.box(10 + i, 0, 190, 11 + i, 0.5f * (i + 1), 210, i % 2 ? "box" : "accent");
    B.box(16, 0, 190, 20, 3, 210);
    for (int i = 0; i < 4; i++) B.box(20 + i, 0, 190, 21 + i, 3 + 0.75f * (i + 1), 210, i % 2 ? "box" : "accent");
    B.box(24, 0, 190, 32, 6, 210);
    B.line("stairs 10 190 16 210 0 3 x 1 1");
    B.line("stairs 20 190 24 210 3 6 x 1 1");
    // grapple: a ceiling at 12 m
    B.box(-20, -1, 220, 120, 0, 260);
    B.box(-20, 12, 220, 120, 13, 260, "metal");
    B.line("");
    B.line("spawn any 0 0 0 -90");
    B.line("light 0 6 0 30 1");
    return B.o;
}

struct Room {
    std::string text;
    Map map;
    std::vector<Gap> gaps;
    std::vector<RoomPart> parts;
};
Room makeRoomData() {
    Room r;
    r.text = makeRoom();
    r.gaps = makeGaps();
    r.map.name = "physics_room";
    std::string err;
    if (!r.map.parse(r.text, &err)) fprintf(stderr, "physics room: %s\n", err.c_str());
    r.parts = {
        {"flat", "Flat floor", "440 x 120 m of open floor: jumps, run-up, stopping, dashes, bhop chains",
         {{-20, 0, -60}, {420, 0, 60}}, {0, 0, 0}, -90},
        {"wall", "Wall", "a 6 m wall across the lane at x 20, 1.6 m ahead of the start: dash or run into it",
         {{-20, 0, 70}, {40, 0, 90}}, {18, 0, 80}, -90},
        {"ladder", "Ladder", "an 8 m tower at x 20..30 with a ladder up its -x face, 0.5 m ahead of the start",
         {{-20, 0, 100}, {40, 0, 120}}, {18.4f, 0, 110}, -90},
        {"drop", "Drop", "a 20 m ledge ending at x 10 over the floor: walk off it",
         {{-20, 0, 130}, {80, 0, 150}}, {5, 20, 140}, -90},
        {"gaps", "Gap course", "platforms over a pit 4 m down; gaps 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16 m wide, the first at x 20",
         {{-20, -4, 160}, {r.gaps.back().x1 + kPlatform + 20, 0, 180}}, {6, 0, 170}, -90},
        {"stairs", "Stairs", "0.5 m risers from x 10 to 3 m, a landing, then 0.75 m risers (over the 0.55 m step-up) to 6 m",
         {{-20, 0, 190}, {60, 6, 210}}, {5, 0, 200}, -90},
        {"grapple", "Grapple ceiling", "a ceiling at 12 m over a 140 m floor",
         {{-20, 0, 220}, {120, 12, 260}}, {0, 0, 240}, -90},
    };
    return r;
}
Room& room() { static Room r = makeRoomData(); return r; }   // (made once, thread-safe)

int runRoom(const Json&, Json& out, CmdCtx&) {
    double t0 = maptoolNow();
    Room& r = room();
    out = Json::object();
    out.set("name", "room");
    out.set("text", r.text);
    Json parts = Json::array();
    for (auto& p : r.parts) {
        Json o = Json::object();
        o.set("id", p.id); o.set("map", std::string("room:") + p.id); o.set("label", p.label); o.set("desc", p.desc);
        o.set("box", jbox(p.box));
        o.set("start", jvec(p.start));   // (feet, on the floor)
        o.set("yaw", p.yawDeg);
        parts.push(std::move(o));
    }
    out.set("parts", std::move(parts));
    Json gaps = Json::array();
    for (auto& g : r.gaps) { Json o = Json::object(); o.set("x0", g.x0); o.set("x1", g.x1); o.set("width", g.x1 - g.x0); gaps.push(std::move(o)); }
    out.set("gaps", std::move(gaps));
    out.set("bounds", jbox(r.map.bounds));
    out.set("brushes", (int)r.map.brushes.size());
    out.set("ms", (maptoolNow() - t0) * 1000);
    return 0;
}

} // namespace

const std::vector<RoomPart>& roomParts() { return room().parts; }
const std::string& roomText() { return room().text; }
const Map& roomMap() { return room().map; }
const std::vector<Gap>& roomGaps() { return room().gaps; }
const RoomPart* roomPartAt(const Vec3& p) {
    for (auto& r : room().parts)
        if (p.x >= r.box.mn.x - 0.5f && p.x <= r.box.mx.x + 0.5f && p.z >= r.box.mn.z - 0.5f && p.z <= r.box.mx.z + 0.5f) return &r;
    return nullptr;
}

namespace {
struct CachedMap { std::string key; std::unique_ptr<Map> map; };
bool g_mapCache = false;
std::vector<CachedMap> g_maps;   // (most recent first, 4 at most: the maps one editing session switches between)
} // namespace

void setMapCache(bool on) { g_mapCache = on; if (!on) g_maps.clear(); }

bool requestMap(const Json& j, const Map*& out, Map& holder, bool& isRoom, std::string& label, std::string& err) {
    if (g_mapCache && j.isObj() && j["text"].isStr() && !j.has("file") && !j.has("map")) {
        std::string key = j["name"].str() + '\n' + j["text"].str();
        for (size_t i = 0; i < g_maps.size(); i++)
            if (g_maps[i].key == key) {
                std::rotate(g_maps.begin(), g_maps.begin() + i, g_maps.begin() + i + 1);
                out = g_maps[0].map.get(); isRoom = false; label = out->name;
                return true;
            }
        auto m = std::make_unique<Map>();
        if (!loadRequestMap(j, *m, err)) return false;
        g_maps.insert(g_maps.begin(), CachedMap{std::move(key), std::move(m)});
        if (g_maps.size() > 4) g_maps.pop_back();
        out = g_maps[0].map.get(); isRoom = false; label = out->name;
        return true;
    }
    isRoom = false;
    if (j.isNull() || (j.isStr() && (j.str() == "room" || j.str().rfind("room:", 0) == 0))) {
        if (j.isStr() && j.str().size() > 5) {
            std::string part = j.str().substr(5);
            bool found = false;
            for (auto& p : roomParts()) if (part == p.id) found = true;
            if (!found) { err = "the test room has no part '" + part + "' (physroom lists them)"; return false; }
        }
        out = &roomMap();
        isRoom = true;
        label = j.isStr() ? j.str() : "room";
        return true;
    }
    Json req = Json::object();
    if (j.isStr()) req.set("name", j.str());
    else if (j.isObj()) req = j;
    else { err = "map must be \"room\", \"room:<part>\", a map name or {text|file|name|map}"; return false; }
    if (!loadRequestMap(req, holder, err)) return false;
    out = &holder;
    label = holder.name;
    return true;
}

MAPTOOL_COMMAND("physroom", runRoom, "physics editor: the built-in test room {text (.qmap), parts:[{id,label,desc,box,start}], gaps}");

} // namespace phys
