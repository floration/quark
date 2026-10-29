// Map editor: fly around, place and resize boxes, rooms, stairs and ladders (in the material picked last), spawns,
// lights and the gameplay zones, then save to maps/<name>.qmap or play-test it right away. Everything snaps to a grid.
// The view draws the boxes directly (no bake) so edits show instantly; BAKE LIGHT previews real lighting.
//
// Every change is an edit op (see applyMapEdit) with its inverse kept for undo. Editing alone, ops apply
// to ed_ directly; in an edit session they go through the server, which applies them in one order and
// sends them to everyone, and ed_ follows the client's copy of the map. A step that takes several ops (grouping,
// a custom group's parts) is one string of '\n'-separated ops, sent one by one, undone as one.
// Groups (rooms, stairs, your own) save to / load from groups/ next to demos/ as "quark-group 1" text.
#include "game.h"
#include "../common/u8file.h"
#include "openurl.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
constexpr float kNewLightRadius = 10, kNewLightIntensity = 1;   // a new light (and where RADIUS / BRIGHTNESS go back to on a right-click)
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kPick = {0.24f, 0.76f, 0.92f, 1};   // (the material picker's current one)
const Color kRed = {1, 0.35f, 0.3f, 1};
const Color kClipColor = {0.75f, 0.35f, 1.0f, 0.22f};   // clip boxes (invisible in play) in the editor
const float kGrids[] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};   // (1/8 m up to 4 m: [ and ] step through them)
const int kGridCount = (int)(sizeof kGrids / sizeof kGrids[0]);
const char kKindChar[] = "bslzhcpdg"; // ED_* -> edit op kind
const char* kKindNames[] = {"BOX", "SPAWN", "LIGHT", "BOMB SITE", "HILL", "CHECKPOINT", "PAYLOAD PATH POINT", "LADDER", "GROUP"};
const char* const* kMatNames = kMapMaterialWords;   // (group files: the map files' keywords)
const char* kGroupTypes[] = {"custom", "room", "stairs"};
const Vec3 kTeamColors[] = {{0.25f, 0.6f, 1.0f}, {1.0f, 0.35f, 0.25f}, {0.9f, 0.9f, 0.9f}, {0.3f, 1.0f, 0.4f}};

float snapTo(float v, float g) { return std::round(v / g) * g; }
Vec3 snapTo(const Vec3& v, float g) { return {snapTo(v.x, g), snapTo(v.y, g), snapTo(v.z, g)}; }
AABB around(const Vec3& p, const Vec3& half) { return {p - half, p + half}; }

// A map name nothing uses yet (neither your maps folder nor the shipped maps): base itself (if tryBase), else
// base + suffix, base + suffix + "2", ... with base shortened so every candidate stays within the 40-character limit.
std::string freeMapName(const std::string& base, const std::string& suffix, bool tryBase) {
    auto taken = [](const std::string& n) {
        std::error_code ec;
        return std::filesystem::exists(std::filesystem::u8path(userMapsDir() + "/" + n + ".qmap"), ec) ||
               std::filesystem::exists(std::filesystem::u8path(findDataPath("maps/" + n + ".qmap")), ec);
    };
    if (tryBase && validMapName(base) && !taken(base)) return base;
    for (int i = 1; i < 1000; i++) {
        std::string tail = suffix + (i > 1 ? std::to_string(i) : "");
        std::string n = typedMapName(base.substr(0, std::max(0, 40 - (int)tail.size())) + tail);
        if (validMapName(n) && !taken(n)) return n;
    }
    return typedMapName(base + "_" + std::to_string((long long)time(nullptr)));
}

size_t kindCount(const Map& m, int kind) {
    switch (kind) {
        case 0: return m.editableBrushes();   // (an editor ladder's rails and rungs come after these)
        case 1: return m.spawns.size();
        case 2: return m.lights.size();
        case 3: return m.bombSites.size();
        case 4: return m.hasHill ? 1 : 0;
        case 5: return m.checkpoints.size();
        case 6: return m.payloadPath.size();
        case 7: return m.ladders.size();
        case 8: return m.groups.size();
    }
    return 0;
}

AABB* kindBox(Map& m, int kind, int i) {
    switch (kind) {
        case 0: return &m.brushes[i].box;
        case 3: return &m.bombSites[i];
        case 4: return &m.hill;
        case 5: return &m.checkpoints[i];
        case 7: return &m.ladders[i];
        case 8: return &m.groups[i].box;
    }
    return nullptr;
}

// a point object's position (spawn, light, path point; nullptr for the rest)
Vec3* kindPoint(Map& m, int kind, int i) { return kind == 1 ? &m.spawns[i].pos : kind == 2 ? &m.lights[i].pos : kind == 6 ? &m.payloadPath[i] : nullptr; }

void moveObject(Map& m, int kind, int i, const Vec3& d) {
    if (AABB* b = kindBox(m, kind, i)) { b->mn += d; b->mx += d; return; }
    if (Vec3* p = kindPoint(m, kind, i)) *p += d;
}

// A copy of m to work an edit out on (only its ops are applied to the map): without the .qmap text, which none of
// that reads and which is a big map's largest part (Midtown's ~470 KB), moved out and back as applyMapEdit does
Map copyNoSource(const Map& m) {
    std::string keep = std::move(m.source);
    Map c = m;
    m.source = std::move(keep);
    c.sourceStale = true;   // (asked for, its text is made from what's there)
    return c;
}

// object i's "set" op without its index ("<k> set <i> ..."): the same object, wherever an insert or delete moved it
std::string objectKey(const Map& m, char k, int i) {
    std::string d = describeMapObject(m, k, "set", i);
    size_t c = d.find(' ', 6);   // (the space after the index)
    return c == std::string::npos ? std::string() : d.substr(c);
}

bool sameBox(const AABB& a, const AABB& b) {
    return a.mn.x == b.mn.x && a.mn.y == b.mn.y && a.mn.z == b.mn.z && a.mx.x == b.mx.x && a.mx.y == b.mx.y && a.mx.z == b.mx.z;
}

// the boxes the editor's static batch is made of (material and box: all it draws) are the same
bool sameBrushes(const std::vector<Brush>& a, const std::vector<Brush>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) if (a[i].material != b[i].material || !sameBox(a[i].box, b[i].box)) return false;
    return true;
}

// Grab points on a selection with bounds b (see Game::edHandles), into hs
template <class H> void handlesOf(const AABB& b, bool point, std::vector<H>& hs) {
    Vec3 c = b.center();
    for (int a = 0; a < 3; a++)
        for (int sg = -1; sg <= 1; sg += 2) {
            Vec3 p = c;
            if (point) p[a] += sg * ((b.mx[a] - b.mn[a]) * 0.5f + 0.6f);
            else p[a] = sg > 0 ? b.mx[a] : b.mn[a];
            hs.push_back({p, a, sg, point});
        }
    if (!point) // boxes, zones, ladders: a move arrow out past each of the 6 faces (the face squares resize that side)
        for (int a = 0; a < 3; a++)
            for (int sg = -1; sg <= 1; sg += 2) {
                Vec3 p = c;
                float out = std::max(1.0f, (b.mx[a] - b.mn[a]) * 0.25f);
                p[a] = sg > 0 ? b.mx[a] + out : b.mn[a] - out;
                hs.push_back({p, a, sg, true});
            }
}

// After a box in an edit op or a group file: "" for a box, " w <dir + 4 * up>" / " c ..." for a wedge / corner.
static std::string shapeSuffix(const Brush& b) {
    if (b.shape == BRUSH_CUT) return " x" + cutText(b);
    return b.shape == BRUSH_BOX ? "" : (std::string(" ") + slopeLetter(b.shape) + " ") + std::to_string((int)b.dir + 4 * b.up);
}

// The "ins" op for object i, plus (a custom group) the ops that add its parts back.
std::string insertOps(const Map& m, char k, int i) {
    std::string o = describeMapObject(m, k, "ins", i);
    if (k == 'g')
        for (auto& p : m.groups[i].parts) {
            char t[200];
            snprintf(t, sizeof t, "\ng part %d %d %.7g %.7g %.7g %.7g %.7g %.7g", i, p.material, p.box.mn.x, p.box.mn.y, p.box.mn.z, p.box.mx.x, p.box.mx.y, p.box.mx.z);
            o += t + shapeSuffix(p);
        }
    return o;
}

// ---- group files: "quark-group 1", "size sx sy sz", "type custom|room|stairs material dir", then (custom) one
// "<material> x0 y0 z0 x1 y1 z1" per box, in metres from the group's low corner (so it pastes anywhere), then
// "w|c <dir + 4 * up>" for a wedge / corner
std::string groupDir() { return userDataPath("groups"); }

std::string groupText(const Map::Group& g) {
    Vec3 sz = g.box.mx - g.box.mn;
    char t[200];
    snprintf(t, sizeof t, "quark-group 1\nsize %g %g %g\ntype %s %d %d\n", sz.x, sz.y, sz.z, kGroupTypes[g.type], g.material, g.dir);
    std::string o = t;
    for (auto& p : g.parts) {
        Vec3 a(p.box.mn.x * sz.x, p.box.mn.y * sz.y, p.box.mn.z * sz.z), c(p.box.mx.x * sz.x, p.box.mx.y * sz.y, p.box.mx.z * sz.z);
        snprintf(t, sizeof t, "%s %g %g %g %g %g %g", kMatNames[p.material], a.x, a.y, a.z, c.x, c.y, c.z);
        o += t + shapeSuffix(p) + "\n";
    }
    return o;
}

// Fills g (box from the origin, size big) from a group text; false if it isn't one.
bool parseGroupText(const std::string& text, Map::Group& g) {
    std::istringstream in(text);
    std::string line, kw;
    if (!std::getline(in, line) || line.rfind("quark-group 1", 0) != 0) return false;
    auto okNum = [](float v) { return std::isfinite(v) && std::fabs(v) <= 4096.0f; };
    Vec3 sz;
    bool hasSize = false, hasType = false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        if (!(ls >> kw) || kw[0] == '#') continue;
        if (kw == "size") {
            hasSize = (bool)(ls >> sz.x >> sz.y >> sz.z) && sz.x > 0.01f && sz.y > 0.01f && sz.z > 0.01f && okNum(sz.x) && okNum(sz.z) && sz.y <= 64;
            if (!hasSize) return false;
            continue;
        }
        if (kw == "type") {
            std::string t;
            if (!(ls >> t >> g.material >> g.dir) || !validMapMaterial(g.material) || g.dir < 0 || g.dir > 3) return false;
            g.type = t == "room" ? Map::GROUP_ROOM : t == "stairs" ? Map::GROUP_STAIRS : Map::GROUP_CUSTOM;
            hasType = t == "room" || t == "stairs" || t == "custom";
            if (!hasType) return false;
            continue;
        }
        int mat = -1;
        for (int k = 0; k < kMapMaterials; k++) if (kw == kMatNames[k]) mat = k;
        Vec3 a, c;
        if (mat < 0 || !hasSize || !(ls >> a.x >> a.y >> a.z >> c.x >> c.y >> c.z) || g.parts.size() >= 1024) return false;
        Vec3 lo = vmin(a, c), hi = vmax(a, c);
        Brush part{{{lo.x / sz.x, lo.y / sz.y, lo.z / sz.z}, {hi.x / sz.x, hi.y / sz.y, hi.z / sz.z}}, mat};
        std::string shape;
        if (ls >> shape) {
            int dir = -1;
            if (shape == "x") { part.shape = BRUSH_CUT; if (!readCuts(ls, part)) return false; g.parts.push_back(part); continue; }
            if (!((shape == "w" || shape == "c" || shape == "t") && (ls >> dir) && dir >= 0 && dir < 4 * kSlopeUps)) return false;
            part.shape = (uint8_t)slopeFromWord(shape);
            part.dir = (uint8_t)(dir % 4);
            part.up = (uint8_t)(dir / 4);
        }
        g.parts.push_back(part);
    }
    if (!hasSize || !hasType || (g.type == Map::GROUP_CUSTOM) == g.parts.empty()) return false;
    for (auto& p : g.parts) if (!okNum(p.box.mn.x) || !okNum(p.box.mx.x) || !okNum(p.box.mn.y) || !okNum(p.box.mx.y) || !okNum(p.box.mn.z) || !okNum(p.box.mx.z)) return false;
    g.box = {{0, 0, 0}, sz};
    return true;
}

// groups/group-<date>-<n>.txt, the first n not taken; "" if it can't be written
std::string writeGroupFile(const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(groupDir(), ec);
    time_t now = time(nullptr);
    char stamp[16];
    strftime(stamp, sizeof stamp, "%Y-%m-%d", localtime(&now));
    for (int n = 1; n < 10000; n++) {
        std::string path = groupDir() + "/group-" + stamp + "-" + std::to_string(n) + ".txt";
        if (std::filesystem::exists(path, ec)) continue;
        std::ofstream f(utf8Path(path), std::ios::binary);
        if (!(f << text)) return "";
        return path;
    }
    return "";
}

std::string readFile(const std::string& path) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void openGroupFolder() {
    std::error_code ec;
    std::filesystem::create_directories(groupDir(), ec);
    openUrl(std::filesystem::absolute(groupDir(), ec).lexically_normal().string());
}

void resetEditorView(const Map& m, Vec3& cam, float& yaw, float& pitch) {
    Vec3 c = m.bounds.center();
    float r = std::max(8.0f, length(m.bounds.mx - m.bounds.mn) * 0.35f);
    cam = c + Vec3(0, r * 0.5f, r);
    yaw = 0;
    pitch = -0.45f;
}
}

// --edscript: one step per frame, so the editor's own UI code handles every click / key (headless tests).
// Positions are in 720p units; a negative x counts from the right edge (the panel).
void Game::edScriptStep() {
    std::string st = edScript_.front();
    edScript_.erase(edScript_.begin());
    std::istringstream in(st);
    std::string cmd;
    in >> cmd;
    const float s = (float)r_.height() / 720.0f;
    auto pos = [&] {
        float x = 0, y = 0;
        in >> x >> y;
        uiIn_.mx = x < 0 ? (float)r_.width() + x * s : x * s;
        uiIn_.my = y * s;
    };
    if (cmd == "click") { pos(); uiIn_.pressed = uiIn_.down = true; edScript_.insert(edScript_.begin(), "up"); }
    else if (cmd == "down") { pos(); uiIn_.pressed = uiIn_.down = true; }
    else if (cmd == "move") pos();
    else if (cmd == "up") uiIn_.down = false;
    else if (cmd == "type") { std::string t; std::getline(in >> std::ws, t); uiIn_.chars += t; }
    else if (cmd == "enter") uiIn_.enter = true;
    else if (cmd == "bs") { int n = 0; in >> n; uiIn_.backspace = true; if (n > 1) edScript_.insert(edScript_.begin(), "bs " + std::to_string(n - 1)); }
    else if (cmd == "sel") { int k = 0, i = 0; in >> k >> i; edSel_ = {k, i}; }
    else if (cmd == "add") { int k = 0, i = 0; in >> k >> i; if (edSelValid()) edMulti_.push_back(edSel_); edSel_ = {k, i}; }   // (shift+click)
    else if (cmd == "boxop") { int op = 0; in >> op; edBoxOp(op); }   // SPLIT X / Y / Z, COMBINE, SUBTRACT (0-4)
    else if (cmd == "do") { std::string t; std::getline(in >> std::ws, t); for (auto& c : t) if (c == '|') c = '\n'; edDo(t, "", false, false); } // an edit step ('|' between ops)
    else if (cmd == "key") { int k = 0, mods = 0; in >> k >> mods; editorKey(k, mods); }   // a key press (GLFW key code), as the editor gets it
    else if (cmd == "cam") { in >> edCam_.x >> edCam_.y >> edCam_.z >> yaw_ >> pitch_; yaw_ = deg2rad(yaw_); pitch_ = deg2rad(pitch_); }
    else if (cmd == "openidx") { int i = 0; in >> i; if (!maps_.empty()) openEditor(maps_[i % maps_.size()]); } // exactly what OPEN does
    else if (cmd == "print") {
        printf("edscript: name=%s map=%s title=%s brushes=%zu sel=%d/%d", edName_.c_str(), ed_.name.c_str(), ed_.title.c_str(), ed_.brushes.size(), edSel_.kind, edSel_.idx);
        if (edSelValid()) { AABB b = edBounds(ed_, edSel_); printf(" box=%g %g %g %g %g %g", b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z); }
        if (edSelValid() && edSel_.kind == ED_BRUSH) printf(" material=%d", ed_.brushes[edSel_.idx].material);
        if (edDragAxis_ >= 0) printf(" drag=%d/%d/%d", edDragAxis_, edDragSign_, edDragMove_ ? 1 : 0);   // the handle being dragged: axis / side / move
        if (edPrompt_) printf(" prompt=%s", edPromptName_.c_str());   // (the save question is up, offering this name)
        printf(" dirty=%d\n", edDirty_ ? 1 : 0);
        fflush(stdout);
    } else if (cmd == "quit") quit_ = true;
}

void Game::edNotify(const std::string& s) { edMsg_ = s; edMsgUntil_ = time_ + 3; }

bool Game::edSelValid() const { return edSel_.valid() && (size_t)edSel_.idx < kindCount(ed_, edSel_.kind); }

void Game::openEditor(std::string name) {
    edOpens_++;
    rescanMaps();
    ed_ = Map();
    edUndo_.clear();
    edSel_ = EdSel();
    edMulti_.clear();
    edGroupMenu_ = edExportAsk_ = false;
    edDirty_ = edBaked_ = edOnline_ = false;
    edMeshDirty_ = true;
    edGen_++;
    std::string err;
    if (!name.empty() && ed_.load(name, &err)) {
        edName_ = name;
    } else {
        if (!name.empty()) edNotify("could not open " + name + ": " + err);
        ed_ = Map();
        ed_.title = "New map";
        ed_.brushes.push_back({{{-20, -1, -20}, {20, 0, 20}}, 0});
        ed_.spawns.push_back({{0, 0, 0}, 0, SPAWN_ANY});
        ed_.rebuild();
        ed_.sourceStale = true;   // (its text, and so its id and light cache, are made from what's here)
        // a new map keeps the name it was opened with (it used to fall back to "mymap", so saving overwrote that);
        // a file that's there but won't load (an older / over-limit map) isn't overwritten by the blank one
        // (an empty / invalid name gets a free "mymap", "mymap2"...; a broken file a free "<name>_new", "<name>_new2"...,
        // shortened so the suffix survives the 40-character limit)
        if (!validMapName(name)) edName_ = freeMapName("mymap", "", true);
        else edName_ = err.rfind("map not found", 0) == 0 ? name : freeMapName(name, "_new", false);
    }
    for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == edName_) edLoadIdx_ = (int)i;
    resetEditorView(ed_, edCam_, yaw_, pitch_);
    r_.unloadWorld(ed_.sunDir);
    screen_ = Screen::Editor;
    ui_.clearFocus();
    edLook_ = false;
    captureMouse(false);
}

// Joined (or switched back into) an edit session: ed_ mirrors the connection's map from now on.
void Game::enterOnlineEditor() {
    bool fromGame = screen_ == Screen::InGame && net_.selfAlive();
    edOnline_ = edSession_ = true;
    ed_ = net_.map();
    edRev_ = net_.mapRevision();
    edName_ = ed_.name;
    edUndo_.clear();
    edSel_ = EdSel();
    edMulti_.clear();
    edGroupMenu_ = edExportAsk_ = false;
    edBaked_ = edDirty_ = false;
    edMeshDirty_ = true;
    edGen_++;
    if (fromGame) edCam_ = net_.predicted().pos + Vec3(0, kEyeHeight, 0); // keep looking from where you were playing
    else resetEditorView(ed_, edCam_, yaw_, pitch_);
    r_.unloadWorld(ed_.sunDir);
    paused_ = chatting_ = settingsInGame_ = false;
    screen_ = Screen::Editor;
    ui_.clearFocus();
    edLook_ = false;
    captureMouse(false);
}

void Game::closeEditor() {
    edLook_ = false;
    r_.clearStaticBoxes();
    edMeshDirty_ = true;
    if (edOnline_) { leaveGame(); return; }
    rescanMaps();
    setScreen(Screen::Main);
    loadMenuBackdrop();
}

// After a local edit: applyMapEdit already updated the map's grid (in place for boxes, a full rebuild inside it for
// the rest), exactly as the server and the session clients rely on, so there's nothing to rebuild here. (The static
// batch is rebuilt by edDo, and only when the boxes changed.)
void Game::edChanged() {
    edDirty_ = true;
    edGen_++;
    if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); edMeshDirty_ = true; }
}

// Apply an edit (or send it to the session) and remember how to undo it. coalesce: repeated nudges and
// slider drags within 0.75 s share one undo step.
void Game::edDo(const std::string& op, const std::string& inverse, bool coalesce, bool undoable) {
    if (op.empty() || op == inverse) return;
    std::vector<std::string> ops;
    for (size_t a = 0; a < op.size();) {
        size_t e = op.find('\n', a);
        if (e == std::string::npos) e = op.size();
        ops.push_back(op.substr(a, e - a));
        a = e + 1;
    }
    if (edOnline_) for (auto& one : ops) net_.sendCommand("ed " + one);
    else {   // all of the step or none of it (COR-4): one op is atomic already (applyMapEdit leaves the map as it was
             // if it fails); a step of several (grouping, a group's parts, an undo of those) goes onto a copy that
             // replaces the map only if every op applied, so a failure part-way never leaves half a step behind
        bool ok = true;
        if (ops.size() == 1) ok = applyMapEdit(ed_, ops[0]);
        else {
            Map tmp = copyNoSource(ed_);   // (its text goes stale with the first op anyway)
            for (auto& one : ops) if (!(ok = applyMapEdit(tmp, one))) break;
            if (ok) ed_ = std::move(tmp);
        }
        if (!ok) { edNotify("can't do that"); return; }   // (and no undo step for what didn't happen)
        edChanged();
        // only boxes, groups and ladders (b, g, d ops) make the static batch's boxes: a title, a slider, a spawn moved
        // doesn't rebuild it (Midtown's rebuild is a 10-30 ms hitch)
        for (auto& one : ops) if (!one.empty() && (one[0] == 'b' || one[0] == 'g' || one[0] == 'd')) { edMeshDirty_ = true; break; }
    }
    if (undoable) {
        if (!(coalesce && time_ - edLastPush_ < 0.75)) {   // (no join: -1e9, not 0 - time_ starts near 0 in a session, and the first edit there lost its undo step)
            edUndo_.push_back(inverse);
            if (edUndo_.size() > 500) edUndo_.erase(edUndo_.begin());
        }
        edLastPush_ = coalesce ? time_ : -1e9;
    }
}

void Game::edUndoLast() {
    if (edUndo_.empty()) { edNotify("nothing to undo"); return; }
    std::string op = edUndo_.back();
    edUndo_.pop_back();
    edLastPush_ = -1e9;
    edDo(op, "", false, false);
}

AABB Game::edBounds(const Map& m, EdSel sel) const {
    switch (sel.kind) {
        case ED_BRUSH: return m.brushes[sel.idx].box;
        case ED_SPAWN: { Vec3 p = m.spawns[sel.idx].pos; return {p - Vec3(0.4f, 0, 0.4f), p + Vec3(0.4f, 1.8f, 0.4f)}; }
        case ED_LIGHT: return around(m.lights[sel.idx].pos, {0.3f, 0.3f, 0.3f});
        case ED_SITE: return m.bombSites[sel.idx];
        case ED_HILL: return m.hill;
        case ED_CHECK: return m.checkpoints[sel.idx];
        case ED_PATH: return around(m.payloadPath[sel.idx], {0.3f, 0.3f, 0.3f});
        case ED_LADDER: return m.ladders[sel.idx];
        case ED_GROUP: return m.groups[sel.idx].box;
    }
    return {};
}

// Change the selected object: f edits a copy of the map, and the difference becomes a "set" op.
void Game::edModify(const std::function<void(Map&)>& f, bool coalesce) {
    if (!edSelValid()) return;
    Map tmp = copyNoSource(ed_);
    f(tmp);
    char k = kKindChar[edSel_.kind];
    edDo(describeMapObject(tmp, k, "set", edSel_.idx), describeMapObject(ed_, k, "set", edSel_.idx), coalesce);
}

// Rotate (90 degrees, about Y / X / Z) or mirror (across X / Y / Z) everything selected, about the middle of
// it all: boxes, groups (a room's or stairs' direction turns with it; a custom group's parts too), ladders,
// spawns (their facing too), lights, and the other point / box objects. One undo step.
bool Game::edTransformed(int op, Map& tmp, std::vector<EdSel>& sel) const {
    if (!edSelValid()) return false;
    sel = {edSel_};
    for (auto& m : edMulti_) if (m.kind != edSel_.kind || m.idx != edSel_.idx) sel.push_back(m);
    const Map& m = ed_;
    auto boxOf = [&](const EdSel& e, AABB& b) -> bool {
        switch (e.kind) {
            case ED_BRUSH: b = m.brushes[e.idx].box; return true;
            case ED_GROUP: b = m.groups[e.idx].box; return true;
            case ED_LADDER: b = m.ladders[e.idx]; return true;
            case ED_CHECK: b = m.checkpoints[e.idx]; return true;
            case ED_SPAWN: b = {m.spawns[e.idx].pos, m.spawns[e.idx].pos}; return true;
            case ED_LIGHT: b = {m.lights[e.idx].pos, m.lights[e.idx].pos}; return true;
            case ED_PATH: b = {m.payloadPath[e.idx], m.payloadPath[e.idx]}; return true;
            default: return false;
        }
    };
    AABB all = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    for (auto& e : sel) { AABB b; if (boxOf(e, b)) { all.mn = vmin(all.mn, b.mn); all.mx = vmax(all.mx, b.mx); } }
    if (all.mn.x > all.mx.x) return false;
    const Vec3 c = (all.mn + all.mx) * 0.5f;
    auto pt = [&](Vec3 p, const Vec3& o) {   // about o
        Vec3 d = p - o;
        switch (op) {
            case ED_ROT_Y: d = {-d.z, d.y, d.x}; break;
            case ED_ROT_X: d = {d.x, -d.z, d.y}; break;
            case ED_ROT_Z: d = {-d.y, d.x, d.z}; break;
            case ED_MIR_X: d.x = -d.x; break;
            case ED_MIR_Y: d.y = -d.y; break;
            case ED_MIR_Z: d.z = -d.z; break;
        }
        return o + d;
    };
    auto box = [&](const AABB& b, const Vec3& o) { Vec3 p = pt(b.mn, o), q = pt(b.mx, o); return AABB{vmin(p, q), vmax(p, q)}; };
    tmp = copyNoSource(m);
    for (auto& e : sel) {
        switch (e.kind) {
            case ED_BRUSH: {
                Brush& b = tmp.brushes[e.idx];
                b.box = box(m.brushes[e.idx].box, c);
                // a wedge / corner: the dir and up that make it the same shape turned (on its side, upside down: any)
                if (b.shape == BRUSH_CUT) cutTurned(m.brushes[e.idx], b, [&](const Vec3& p) { return pt(p, c); });
                else if (b.shape != BRUSH_BOX && !slopeTurned(m.brushes[e.idx], b, [&](const Vec3& p) { return pt(p, c); })) return false;
                break;
            }
            case ED_LADDER: tmp.ladders[e.idx] = box(m.ladders[e.idx], c); break;
            case ED_CHECK: tmp.checkpoints[e.idx] = box(m.checkpoints[e.idx], c); break;
            case ED_LIGHT: tmp.lights[e.idx].pos = pt(m.lights[e.idx].pos, c); break;
            case ED_PATH: tmp.payloadPath[e.idx] = pt(m.payloadPath[e.idx], c); break;
            case ED_SPAWN: {
                auto& sp = tmp.spawns[e.idx];
                sp.pos = pt(m.spawns[e.idx].pos, c);
                float y = m.spawns[e.idx].yaw;   // facing = (-sin yaw, -cos yaw)
                sp.yaw = op == ED_ROT_Y ? y - PI * 0.5f : op == ED_MIR_X ? -y : op == ED_MIR_Z ? PI - y : y;
                break;
            }
            case ED_GROUP: {
                auto& g = tmp.groups[e.idx];
                g.box = box(m.groups[e.idx].box, c);
                static const int rotY[4] = {2, 3, 1, 0};   // stairs rise toward +X, -X, +Z, -Z
                if (op == ED_ROT_Y) g.dir = rotY[g.dir];
                else if (op == ED_MIR_X && g.dir < 2) g.dir ^= 1;
                else if (op == ED_MIR_Z && g.dir >= 2) g.dir ^= 1;
                for (size_t k = 0; k < g.parts.size(); k++) {   // (parts: 0-1 fractions of the box; a slope turns as a brush does)
                    const Vec3 mid(0.5f, 0.5f, 0.5f);
                    Brush& p = g.parts[k];
                    p.box = box(p.box, mid);
                    if (p.shape == BRUSH_CUT) cutTurned(m.groups[e.idx].parts[k], p, [&](const Vec3& q) { return pt(q, mid); });
                    else if (p.shape != BRUSH_BOX && !slopeTurned(m.groups[e.idx].parts[k], p, [&](const Vec3& q) { return pt(q, mid); })) return false;
                }
                break;
            }
        }
    }
    return true;
}

UiExp Game::edTransformExp(int op) {
    if (!ui_.probing()) return UiExp::edit();
    Map tmp;
    std::vector<EdSel> sel;
    // (a lone symmetric box mirrored about its own middle comes out the same: then the notice is all that happens)
    return edTransformed(op, tmp, sel) && tmp.serialize() != ed_.serialize() ? UiExp::edit() : UiExp::state("ednotice");
}

void Game::edTransform(int op) {
    Map tmp;
    std::vector<EdSel> sel;
    if (!edSelValid()) return;
    if (!edTransformed(op, tmp, sel)) { edNotify("can't turn that"); return; }
    // every object's op in one step, so one undo puts them all back (one edDo per object made an undo step each: the
    // coalescing only joins repeats of one change, so a multi-selection took an undo per object)
    const Map& m = ed_;
    std::vector<std::string> ops, inv;
    for (auto& e : sel) {
        char k = kKindChar[e.kind];
        if (e.kind == ED_GROUP && !tmp.groups[e.idx].parts.empty()) {   // a custom group: rewrite it whole (its parts follow)
            std::string del = "g del " + std::to_string(e.idx) + "\n";
            ops.push_back(del + insertOps(tmp, k, e.idx));
            inv.push_back(del + insertOps(m, k, e.idx));
            continue;
        }
        if (e.kind == ED_GROUP || e.kind == ED_BRUSH || e.kind == ED_LADDER || e.kind == ED_CHECK || e.kind == ED_LIGHT || e.kind == ED_PATH || e.kind == ED_SPAWN) {
            std::string o = describeMapObject(tmp, k, "set", e.idx), u = describeMapObject(m, k, "set", e.idx);
            if (o != u) { ops.push_back(o); inv.push_back(u); }
        }
    }
    std::string ops1, undo;
    for (size_t i = 0; i < ops.size(); i++) { ops1 += (i ? "\n" : "") + ops[i]; undo += (i ? "\n" : "") + inv[inv.size() - 1 - i]; }
    edDo(ops1, undo);
    static const char* names[] = {"rotated about Y", "rotated about X", "rotated about Z", "mirrored left / right", "mirrored upside down", "mirrored front / back"};
    edNotify(names[op]);
}

void Game::edDelete() {
    if (!edSelValid()) return;
    if (edSel_.kind == ED_BRUSH && ed_.brushes.size() <= 1) { edNotify("a map needs at least one box"); return; }
    if (edSel_.kind == ED_SPAWN && ed_.spawns.size() <= 1) { edNotify("a map needs at least one spawn"); return; }
    char k = kKindChar[edSel_.kind];
    if (edSel_.kind == ED_HILL) edModify([](Map& m) { m.hasHill = false; }, false);
    else edDo(std::string(1, k) + " del " + std::to_string(edSel_.idx), insertOps(ed_, k, edSel_.idx));
    edSel_ = EdSel();
    edMulti_.clear();
}

// tmp is ed_ plus one new object at the end of `kind`'s list (or a new hill): insert it and select it.
static int addFrom(const Map& tmp, const Map& cur, int kind, const std::function<void(const std::string&, const std::string&)>& doOp) {
    char k = kKindChar[kind];
    if (k == 'h') {
        doOp(describeMapObject(tmp, 'h', "set", 0), describeMapObject(cur, 'h', "set", 0));
        return 0;
    }
    int i = (int)kindCount(tmp, kind) - 1;
    doOp(insertOps(tmp, k, i), std::string(1, k) + " del " + std::to_string(i));
    return i; // index of the new object
}

// Copies the selection and puts the copy right next to it (sideways from where you're looking).
void Game::edDuplicate() {
    if (!edSelValid() || edSel_.kind == ED_HILL) return;
    AABB b = edBounds(ed_, edSel_);
    Vec3 f = angleForward(yaw_, 0);
    Vec3 off = std::fabs(f.x) > std::fabs(f.z) ? Vec3(0, 0, b.mx.z - b.mn.z) : Vec3(b.mx.x - b.mn.x, 0, 0);
    Map tmp = copyNoSource(ed_);
    int i = edSel_.idx;
    switch (edSel_.kind) {
        case ED_BRUSH: tmp.brushes.insert(tmp.brushes.begin() + tmp.editableBrushes(), tmp.brushes[i]); break;
        case ED_LADDER: tmp.ladders.push_back(tmp.ladders[i]); tmp.ladderAuto.resize(tmp.ladders.size() - 1, 0); tmp.ladderAuto.push_back(tmp.ladderAuto[i]); break;
        case ED_GROUP: tmp.groups.push_back(tmp.groups[i]); break;
        case ED_SPAWN: tmp.spawns.push_back(tmp.spawns[i]); break;
        case ED_LIGHT: tmp.lights.push_back(tmp.lights[i]); break;
        case ED_SITE: tmp.bombSites.push_back(tmp.bombSites[i]); break;
        case ED_CHECK: tmp.checkpoints.push_back(tmp.checkpoints[i]); break;
        case ED_PATH: tmp.payloadPath.push_back(tmp.payloadPath[i]); break;
    }
    edMulti_.clear();
    moveObject(tmp, edSel_.kind, (int)kindCount(tmp, edSel_.kind) - 1, off);
    edSel_.idx = addFrom(tmp, ed_, edSel_.kind, [&](const std::string& op, const std::string& inv) { edDo(op, inv); });
}

// Coat (owner, 2026-10-28: "select faces of blocks and then it will build a 1/8m surface against the sides you picked"):
// K lays a 1/8 m layer of the current material (M) on the side of the selected box you're looking at; with more boxes
// selected (shift+click), on that same side of each of them. Each layer is a box of its own (undo takes them back one
// at a time). Slopes are left out: their sides aren't whole rectangles.
void Game::edCoat() {
    if (!edSelValid() || edSel_.kind != ED_BRUSH) { edNotify("select a box, look at one of its sides and press K"); return; }
    const Vec3 dir = angleForward(yaw_, pitch_);
    const Brush& b = ed_.brushes[edSel_.idx];
    Vec3 n;
    const float t = b.shape == BRUSH_BOX ? rayBrush(edCam_, dir, safeInv(dir), b, 500, &n) : -1.0f;
    int axis = -1;
    for (int a = 0; a < 3; a++) if (std::fabs(n[a]) > 0.99f) axis = a;
    if (t < 0 || axis < 0) { edNotify(b.shape != BRUSH_BOX ? "slopes can't be coated: pick a box" : "look at a side of the selected box"); return; }
    const bool plus = n[axis] > 0;
    std::vector<AABB> coats;
    auto coat = [&](int i) {
        if (i < 0 || i >= ed_.editableBrushes() || ed_.brushes[i].shape != BRUSH_BOX) return;
        const AABB& a = ed_.brushes[i].box;
        AABB c = a;
        if (plus) { c.mn[axis] = a.mx[axis]; c.mx[axis] = a.mx[axis] + kCoatThick; }
        else { c.mx[axis] = a.mn[axis]; c.mn[axis] = a.mn[axis] - kCoatThick; }
        coats.push_back(c);
    };
    coat(edSel_.idx);
    for (const EdSel& s : edMulti_) if (s.kind == ED_BRUSH && s.idx != edSel_.idx) coat(s.idx);
    for (const AABB& c : coats) {
        Map tmp = copyNoSource(ed_);
        tmp.brushes.insert(tmp.brushes.begin() + tmp.editableBrushes(), Brush{c, edMat_});
        addFrom(tmp, ed_, ED_BRUSH, [&](const std::string& op, const std::string& inv) { edDo(op, inv); });
    }
    static const char* sides[6] = {"-X", "+X", "-Y (under)", "+Y (top)", "-Z", "+Z"};
    edNotify("coated " + std::to_string(coats.size()) + (coats.size() == 1 ? " side " : " sides ") + sides[axis * 2 + (plus ? 1 : 0)] +
             " with " + std::string(kMapMaterialWords[edMat_]));
}

// FIX MAP / FIX GAPS (owner, 2026-10-29: "combine redundant shapes, also delete any object that you can never see"; "if
// there is a gap less than 1/8 between 2 object, expand whichever one is not on grid"): the changed boxes' sets, then
// the deletes from the last one down, in one step; undo puts them back in the reverse order.
void Game::edFix(bool gaps) {
    const MapFix f = gaps ? mapFixGaps(ed_) : mapFixRedundant(ed_);
    const size_t n = f.box.size();
    std::vector<bool> del = f.del;
    if (n && std::all_of(del.begin(), del.end(), [](bool d) { return d; })) del[0] = false;   // (a map keeps a box)
    Map after = copyNoSource(ed_);
    std::string ops, undo;
    auto add = [](std::string& to, const std::string& o) { to += (to.empty() ? "" : "\n") + o; };
    std::vector<std::string> sets, unsets;
    for (size_t i = 0; i < n; i++) {
        if (del[i] || sameBox(f.box[i], ed_.brushes[i].box)) continue;
        after.brushes[i].box = f.box[i];
        sets.push_back(describeMapObject(after, 'b', "set", (int)i));
        unsets.push_back(describeMapObject(ed_, 'b', "set", (int)i));
    }
    for (auto& o : sets) add(ops, o);
    for (size_t i = n; i-- > 0;) if (del[i]) add(ops, "b del " + std::to_string(i));
    for (size_t i = 0; i < n; i++) if (del[i]) add(undo, insertOps(ed_, 'b', (int)i));
    for (size_t k = unsets.size(); k-- > 0;) add(undo, unsets[k]);
    if (ops.empty()) { edNotify(gaps ? "no small gaps to close" : "nothing to fix"); return; }
    edSel_ = EdSel();
    edMulti_.clear();
    edDo(ops, undo);
    if (gaps) edNotify("closed " + std::to_string(f.gaps) + (f.gaps == 1 ? " gap" : " gaps"));
    else edNotify("merged " + std::to_string(f.merged) + (f.merged == 1 ? " box" : " boxes") + " into others, removed " + std::to_string(f.hidden) + " hidden");
}

// New object at the spot under the screen center (on top of whatever you're looking at), snapped to the grid.
void Game::edAdd(int kind, int variant) {
    const float g = kGrids[edGridIdx_];
    Vec3 dir = angleForward(yaw_, pitch_), n;
    float t = ed_.raycast(edCam_, dir, 200, &n);
    Vec3 at = t >= 0 ? edCam_ + dir * t + n * 0.001f : edCam_ + dir * 8.0f;
    at = snapTo(at, g);
    if (t >= 0 && n.y < -0.5f) at.y -= kind == ED_BRUSH ? g : 1; // hit a ceiling: hang below it
    Map tmp = copyNoSource(ed_);
    switch (kind) {
        case ED_BRUSH: {   // one grid cell (owner, 2026-10-28: "the same scale as your grid size"), the cell under the crosshair
            Vec3 lo = snapTo(at - Vec3(g * 0.5f, 0, g * 0.5f), g);
            lo.y = at.y;
            tmp.brushes.insert(tmp.brushes.begin() + tmp.editableBrushes(), Brush{{lo, lo + Vec3(g, g, g)}, edMat_});
            break;
        }
        case ED_GROUP: { // variant: GROUP_ROOM (8 x 3.5 x 8, a door each side) or GROUP_STAIRS (2 wide, 2 up over 4, rising away from you)
            Map::Group gr;
            gr.type = variant;
            gr.material = edMat_;
            if (variant == Map::GROUP_ROOM) gr.box = {at - Vec3(4, 0, 4), at + Vec3(4, 3.5f, 4)};
            else {
                Vec3 f = angleForward(yaw_, 0);
                int a = std::fabs(f.x) > std::fabs(f.z) ? 0 : 2;
                bool neg = f[a] < 0;
                gr.dir = (a == 0 ? 0 : 2) + (neg ? 1 : 0);
                Vec3 lo = at - Vec3(1, 0, 1), hi = at + Vec3(1, 2, 1);
                lo[a] = neg ? at[a] - 4 : at[a];
                hi[a] = neg ? at[a] : at[a] + 4;
                gr.box = {lo, hi};
            }
            tmp.groups.push_back(gr);
            break;
        }
        case ED_LADDER: { // a ladder facing you, 4 m tall: one object (drag its sides to stretch it; rungs follow)
            Vec3 f = angleForward(yaw_, 0);
            Vec3 half = std::fabs(f.x) > std::fabs(f.z) ? Vec3(0.35f, 0, 0.45f) : Vec3(0.45f, 0, 0.35f);
            tmp.ladders.push_back({at - half, at + half + Vec3(0, 5, 0)});
            tmp.ladderAuto.resize(tmp.ladders.size() - 1, 0);
            tmp.ladderAuto.push_back(1);
            break;
        }
        case ED_SPAWN: tmp.spawns.push_back({at, std::round(yaw_ / (PI / 4)) * (PI / 4), variant}); break;
        case ED_LIGHT: tmp.lights.push_back({at + Vec3(0, 2, 0), kNewLightRadius, kNewLightIntensity}); break;
        case ED_SITE: tmp.bombSites.push_back({at - Vec3(3, 0, 3), at + Vec3(3, 3, 3)}); break;
        case ED_HILL: tmp.hasHill = true; tmp.hill = {at - Vec3(4, 0, 4), at + Vec3(4, 3, 4)}; break;
        case ED_CHECK: tmp.checkpoints.push_back({at - Vec3(2, 0, 2), at + Vec3(2, 4, 2)}); break;
        case ED_PATH: tmp.payloadPath.push_back(at + Vec3(0, 0.1f, 0)); break;
    }
    edMulti_.clear();
    edSel_ = {kind, addFrom(tmp, ed_, kind, [&](const std::string& op, const std::string& inv) { edDo(op, inv); })};
}

// The selected boxes / groups (edSel_ + edMulti_) as one custom group: parts are 0..1 fractions of their bounds.
template <class Sel> static bool groupOf(const Map& m, const std::vector<Sel>& sel, Map::Group& g, std::string& why) {
    std::vector<Brush> all;
    for (auto& e : sel) {
        if (e.kind == 0) all.push_back(m.brushes[e.idx]);
        else Map::groupBoxes(m.groups[e.idx], all);
    }
    if (all.empty()) { why = "select boxes / groups (shift+click to add more)"; return false; }
    AABB b = all[0].box;
    for (auto& p : all) { b.mn = vmin(b.mn, p.box.mn); b.mx = vmax(b.mx, p.box.mx); }
    Vec3 sz = b.mx - b.mn;
    if (sz.y > 64 || all.size() > 1024) { why = "too big for one group"; return false; }
    g = Map::Group();
    g.box = b;
    for (auto& p : all) {
        Vec3 a = p.box.mn - b.mn, c = p.box.mx - b.mn;
        Brush part = p;   // (every material, trim and clip too; a wedge / corner keeps its shape)
        part.box = {{a.x / sz.x, a.y / sz.y, a.z / sz.z}, {c.x / sz.x, c.y / sz.y, c.z / sz.z}};
        part.generated = false; part.group = -1;
        g.parts.push_back(part);
    }
    return true;
}

// edSel_ + edMulti_, only the boxes and groups that still exist, each once
template <class Sel> static std::vector<Sel> groupable(const Map& m, Sel first, const std::vector<Sel>& more) {
    std::vector<Sel> out;
    auto add = [&](Sel e) {
        bool ok = (e.kind == 0 && (size_t)e.idx < m.brushes.size() && !m.brushes[e.idx].generated) || (e.kind == 8 && (size_t)e.idx < m.groups.size());
        for (auto& o : out) ok &= !(o.kind == e.kind && o.idx == e.idx);
        if (ok && e.idx >= 0) out.push_back(e);
    };
    add(first);
    for (auto& e : more) add(e);
    return out;
}

bool Game::edGroupTakesEveryBox() const {
    int boxes = 0;
    for (auto& e : groupable(ed_, edSel_, edMulti_)) boxes += e.kind == ED_BRUSH;
    return boxes > 0 && boxes >= (int)ed_.editableBrushes();
}

void Game::edGroupSelection() {
    auto sel = groupable(ed_, edSel_, edMulti_);
    if (sel.size() == 1 && sel[0].kind == ED_GROUP) { edNotify("that's already a group"); return; }
    if (edGroupTakesEveryBox()) { edNotify("a map keeps at least one box outside groups - add another box first"); return; }
    Map::Group g;
    std::string why;
    if (!groupOf(ed_, sel, g, why)) { edNotify(why); return; }
    // add the group first (a map always keeps a box), then take away what went into it, highest index first
    int gi = (int)ed_.groups.size();
    Map tmp = copyNoSource(ed_);
    tmp.groups.push_back(g);
    std::vector<std::string> ops = {insertOps(tmp, 'g', gi)}, inv = {"g del " + std::to_string(gi)};
    std::sort(sel.begin(), sel.end(), [](const EdSel& a, const EdSel& b) { return a.kind != b.kind ? a.kind < b.kind : a.idx > b.idx; });
    int groupsGone = 0;
    for (auto& e : sel) {
        char k = kKindChar[e.kind];
        ops.push_back(std::string(1, k) + " del " + std::to_string(e.idx));
        inv.push_back(insertOps(ed_, k, e.idx));
        groupsGone += e.kind == ED_GROUP;
    }
    std::string op, undo;
    for (size_t k = 0; k < ops.size(); k++) { op += (k ? "\n" : "") + ops[k]; undo += (k ? "\n" : "") + inv[inv.size() - 1 - k]; }
    edDo(op, undo);
    edSel_ = {ED_GROUP, gi - groupsGone};
    edMulti_.clear();
    edNotify("grouped " + std::to_string(sel.size()) + " - it moves and stretches as one");
}

void Game::edUngroup() {
    if (!edSelValid() || edSel_.kind != ED_GROUP) return;
    int gi = edSel_.idx, n = (int)ed_.editableBrushes();
    std::vector<Brush> parts;
    Map::groupBoxes(ed_.groups[gi], parts);
    std::string op, undo = insertOps(ed_, 'g', gi);
    for (size_t k = 0; k < parts.size(); k++) {
        char t[220];
        const AABB& b = parts[k].box;
        snprintf(t, sizeof t, "b ins %d %d %.9g %.9g %.9g %.9g %.9g %.9g", n + (int)k, parts[k].material, b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
        op += t + shapeSuffix(parts[k]) + "\n";
        undo = "b del " + std::to_string(n + (int)k) + "\n" + undo;
    }
    op += "g del " + std::to_string(gi);
    edDo(op, undo);
    edSel_ = EdSel();
    edMulti_.clear();
    for (size_t k = 0; k < parts.size(); k++) (k ? edMulti_.push_back({ED_BRUSH, n + (int)k}) : (void)(edSel_ = {ED_BRUSH, n}));
    edNotify("ungrouped into " + std::to_string(parts.size()) + " boxes");
}

// SPLIT / COMBINE / SUBTRACT (owner, 2026-10-29: "split, combine, and subtract buttons"), on the selected boxes, one undo
// step each. The selection in click order is edMulti_ then edSel_ (shift+click puts the newest in edSel_).
//   split X / Y / Z (the button: the longest side, edSplitOp): each plain box the plane crosses becomes two; the plane
//   is the selection's middle on that axis, on the grid.
//   combine: boxes of one material that fill exactly one box (no gaps, no overlap) become that box; anything else
//   is grouped (GROUP's MAKE), so it moves and stretches as one.
//   subtract: the first box selected is the cutter: its volume is carved out of the others (each leaves up to 6
//   boxes round the hole), then the cutter goes.
// Slopes and cut boxes are left out of split and subtract (their pieces wouldn't be boxes).
bool Game::edBoxOpPlan(int op, std::vector<int>& gone, std::vector<Brush>& made, std::string& why) const {
    gone.clear(); made.clear(); why.clear();
    std::vector<int> sel;
    auto add = [&](const EdSel& e) {
        if (e.kind != ED_BRUSH || e.idx < 0 || (size_t)e.idx >= ed_.editableBrushes()) return;
        if (std::find(sel.begin(), sel.end(), e.idx) == sel.end()) sel.push_back(e.idx);
    };
    for (auto& e : edMulti_) add(e);
    if (edSelValid()) add(edSel_);
    if (sel.empty()) { why = "select boxes first"; return false; }
    auto vol = [](const AABB& b) { return std::max(0.0f, b.mx.x - b.mn.x) * std::max(0.0f, b.mx.y - b.mn.y) * std::max(0.0f, b.mx.z - b.mn.z); };
    auto overlap = [](const AABB& a, const AABB& b) { return AABB{vmax(a.mn, b.mn), vmin(a.mx, b.mx)}; };
    constexpr float eps = 1e-4f;
    if (op <= ED_SPLIT_Z) {
        const int a = op - ED_SPLIT_X;
        float lo = 1e9f, hi = -1e9f;
        for (int i : sel) { lo = std::min(lo, ed_.brushes[i].box.mn[a]); hi = std::max(hi, ed_.brushes[i].box.mx[a]); }
        const float g = kGrids[edGridIdx_];
        float c = std::round((lo + hi) * 0.5f / g) * g;
        bool slopes = false;
        for (int pass = 0; pass < 2 && made.empty(); pass++) {   // (on the grid it can land on an edge: then the exact middle)
            if (pass) c = (lo + hi) * 0.5f;
            for (int i : sel) {
                const Brush& b = ed_.brushes[i];
                if (b.box.mn[a] >= c - eps || b.box.mx[a] <= c + eps) continue;
                if (b.shape != BRUSH_BOX) { slopes = true; continue; }
                Brush p = b, q = b;
                p.box.mx[a] = c; q.box.mn[a] = c;
                gone.push_back(i); made.push_back(p); made.push_back(q);
            }
        }
        if (made.empty()) why = slopes ? "slopes can't be split: pick plain boxes" : "nothing to split there";
        return !made.empty();
    }
    if (op == ED_COMBINE) {
        if (sel.size() < 2) { why = "select two or more boxes (shift+click) to combine"; return false; }
        AABB all = ed_.brushes[sel[0]].box;
        float sum = 0;
        bool box = true;
        for (size_t k = 0; k < sel.size(); k++) {
            const Brush& b = ed_.brushes[sel[k]];
            box &= b.shape == BRUSH_BOX && b.material == ed_.brushes[sel[0]].material;   // (mixed materials: a group, as Quarkology's)
            all.mn = vmin(all.mn, b.box.mn); all.mx = vmax(all.mx, b.box.mx);
            sum += vol(b.box);
            for (size_t j = 0; j < k; j++) box &= vol(overlap(b.box, ed_.brushes[sel[j]].box)) <= eps * std::max(1.0f, vol(b.box));
        }
        if (!box || std::fabs(sum - vol(all)) > eps * std::max(1.0f, vol(all))) { why = "group"; return false; }
        Brush b = ed_.brushes[sel[0]];
        b.box = all;
        gone = sel;
        made.push_back(b);
        return true;
    }
    // ED_SUBTRACT
    if (sel.size() < 2) { why = "select the cutter first, then shift+click the boxes to cut"; return false; }
    const Brush& cut = ed_.brushes[sel[0]];
    if (cut.shape != BRUSH_BOX) { why = "the cutter (the first box selected) has to be a plain box"; return false; }
    const AABB& c = cut.box;
    bool slopes = false;
    for (size_t k = 1; k < sel.size(); k++) {
        const Brush& t = ed_.brushes[sel[k]];
        if (vol(overlap(t.box, c)) <= eps) continue;
        if (t.shape != BRUSH_BOX) { slopes = true; continue; }
        AABB r = t.box;
        for (int a = 0; a < 3; a++) {
            if (r.mn[a] < c.mn[a] - eps) { Brush p = t; p.box = r; p.box.mx[a] = c.mn[a]; made.push_back(p); r.mn[a] = c.mn[a]; }
            if (r.mx[a] > c.mx[a] + eps) { Brush p = t; p.box = r; p.box.mn[a] = c.mx[a]; made.push_back(p); r.mx[a] = c.mx[a]; }
        }
        gone.push_back(sel[k]);
    }
    if (gone.empty()) { why = slopes ? "slopes can't be cut: pick plain boxes" : "the cutter (the first box selected) doesn't touch the others"; return false; }
    gone.push_back(sel[0]);
    if (ed_.editableBrushes() - gone.size() + made.size() < 1) { why = "a map needs at least one box"; return false; }
    return true;
}

// SPLIT's axis: the selected boxes' longest side (a wall splits along its length, a pillar across its height)
int Game::edSplitOp() const {
    AABB all = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    auto add = [&](const EdSel& e) {
        if (e.kind == ED_BRUSH && e.idx >= 0 && (size_t)e.idx < ed_.editableBrushes()) { all.mn = vmin(all.mn, ed_.brushes[e.idx].box.mn); all.mx = vmax(all.mx, ed_.brushes[e.idx].box.mx); }
    };
    for (auto& e : edMulti_) add(e);
    if (edSelValid()) add(edSel_);
    const Vec3 d = all.mx - all.mn;
    return d.y > d.x && d.y > d.z ? ED_SPLIT_Y : d.z > d.x ? ED_SPLIT_Z : ED_SPLIT_X;
}

UiExp Game::edBoxOpExp(int op) const {
    if (!ui_.probing()) return UiExp::edit();
    std::vector<int> gone;
    std::vector<Brush> made;
    std::string why;
    return edBoxOpPlan(op, gone, made, why) || (why == "group" && !edGroupTakesEveryBox()) ? UiExp::edit() : UiExp::state("ednotice");
}

void Game::edBoxOp(int op) {
    std::vector<int> gone;
    std::vector<Brush> made;
    std::string why;
    if (!edBoxOpPlan(op, gone, made, why)) {
        if (why == "group") { edGroupSelection(); return; }
        edNotify(why);
        return;
    }
    // take the old ones away (highest index first), then add the new ones after the other boxes
    std::sort(gone.begin(), gone.end(), std::greater<int>());
    std::vector<std::string> ops, inv;
    for (int i : gone) { ops.push_back("b del " + std::to_string(i)); inv.push_back(insertOps(ed_, 'b', i)); }
    const int n = (int)ed_.editableBrushes() - (int)gone.size();
    for (size_t k = 0; k < made.size(); k++) {
        char t[220];
        const AABB& b = made[k].box;
        snprintf(t, sizeof t, "b ins %d %d %.9g %.9g %.9g %.9g %.9g %.9g", n + (int)k, made[k].material, b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
        ops.push_back(t + shapeSuffix(made[k]));
        inv.push_back("b del " + std::to_string(n + (int)k));
    }
    std::string o, undo;
    for (size_t k = 0; k < ops.size(); k++) { o += (k ? "\n" : "") + ops[k]; undo += (k ? "\n" : "") + inv[inv.size() - 1 - k]; }
    edDo(o, undo);
    edSel_ = EdSel();
    edMulti_.clear();
    for (size_t k = 0; k < made.size(); k++) (k + 1 < made.size() ? edMulti_.push_back({ED_BRUSH, n + (int)k}) : (void)(edSel_ = {ED_BRUSH, n + (int)k}));
    edNotify(op == ED_COMBINE ? "combined " + std::to_string(gone.size()) + " boxes into one"
             : op == ED_SUBTRACT ? "cut " + std::to_string(gone.size() - 1) + " boxes into " + std::to_string(made.size())
             : "split " + std::to_string(gone.size()) + (gone.size() == 1 ? " box" : " boxes"));
}

bool Game::edPlaceGroup(const std::string& text) {
    Map::Group g;
    if (!parseGroupText(text, g)) return false;
    const float gr = kGrids[edGridIdx_];
    Vec3 dir = angleForward(yaw_, pitch_), n;
    float t = ed_.raycast(edCam_, dir, 200, &n);
    Vec3 at = snapTo(t >= 0 ? edCam_ + dir * t + n * 0.001f : edCam_ + dir * 8.0f, gr);
    Vec3 sz = g.box.mx;
    if (t >= 0 && n.y < -0.5f) at.y -= sz.y; // hit a ceiling: hang below it
    g.box.mn = snapTo(at - Vec3(sz.x * 0.5f, 0, sz.z * 0.5f), gr);
    g.box.mx = g.box.mn + sz;
    Map tmp = copyNoSource(ed_);
    tmp.groups.push_back(g);
    int gi = (int)ed_.groups.size();
    edDo(insertOps(tmp, 'g', gi), "g del " + std::to_string(gi));
    edSel_ = {ED_GROUP, gi};
    edMulti_.clear();
    return true;
}

void Game::edImportGroup() {
    const char* clip = glfwGetClipboardString(win_);
    Map::Group g;
    if (clip && parseGroupText(clip, g)) { // a group on the clipboard: keep it in groups/ (once) and place it
        std::string text = clip;
        bool have = false;
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(groupDir(), ec)) have |= e.path().extension() == ".txt" && readFile(e.path().string()) == text;
        std::string path = have ? "" : writeGroupFile(text);
        edPlaceGroup(text);
        edNotify(have ? "placed the group from the clipboard" : "placed the group from the clipboard (saved as groups/" + std::filesystem::path(path).filename().string() + ")");
        return;
    }
    edGroupFiles_.clear();
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> fs;
    for (auto& e : std::filesystem::directory_iterator(groupDir(), ec))
        if (e.path().extension() == ".txt") fs.push_back({e.last_write_time(ec), e.path().string()});
    std::sort(fs.begin(), fs.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& f : fs) edGroupFiles_.push_back(f.second);
    edGroupMenu_ = true;
    edExportAsk_ = false;
}

void Game::edExportGroup() {
    Map::Group g;
    if (edSelValid() && edSel_.kind == ED_GROUP && groupable(ed_, edSel_, edMulti_).size() == 1) g = ed_.groups[edSel_.idx];
    else if (edExportAsk_) { // YES: group it, then export that
        edExportAsk_ = false;
        std::string why;
        if (!groupOf(ed_, groupable(ed_, edSel_, edMulti_), g, why)) { edNotify(why); return; }
        edGroupSelection();
    } else if (!groupable(ed_, edSel_, edMulti_).empty()) { edExportAsk_ = true; edGroupMenu_ = false; return; }
    else { edNotify("select a group to export"); return; }
    std::string text = groupText(g), path = writeGroupFile(text);
    if (path.empty()) { edNotify("could not write to the groups folder"); return; }
    glfwSetClipboardString(win_, text.c_str());
    openGroupFolder();
    edNotify("exported groups/" + std::filesystem::path(path).filename().string() + " (and copied it) - rename it there");
}

// The key help under the 3D view (six lines, as the redesign split them), packed into lines at most maxW wide at text
// scale ts: each line's parts ("KEY: WHAT", four spaces apart) stay on it while they fit, the rest go on the next line
// (a narrower view - a 4:3 window - takes more lines). Packed again only when the width, the size or the font changes.
static const std::vector<std::string>& edHelpLines(const Renderer& r, float maxW, float ts) {
    static const char* kHelp[] = {
        "RIGHT MOUSE + MOVE: LOOK    WASD: FLY    SPACE / C: UP / DOWN    SHIFT: FAST    WHEEL: SPEED",
        "CLICK: SELECT (AGAIN: THE ONE BEHIND, SHIFT: ADD MORE, THEN GROUP)",
        "ARROWS / PGUP / PGDN: MOVE BY GRID (ALONG YOUR VIEW)",
        "SHIFT + ARROWS: GROW THAT SIDE    CTRL + ARROWS: SHRINK IT    M: MATERIAL / TEAM    N: SHAPE    K: COAT THE SIDE YOU LOOK AT",
        "CTRL+D: DUPLICATE    DEL: DELETE    CTRL+Z: UNDO    CTRL+S: SAVE    ESC: DESELECT / LEAVE",
        "R: ROTATE 90 (SHIFT: ABOUT X, CTRL: ABOUT Z)    F: MIRROR LEFT / RIGHT (SHIFT: FRONT / BACK, CTRL: UPSIDE DOWN)",
    };
    static std::vector<std::string> lines;
    static float lastW = -1, lastTs = -1;
    static int lastGen = -1;
    if (lastW == maxW && lastTs == ts && lastGen == r.fontGeneration()) return lines;
    lastW = maxW; lastTs = ts; lastGen = r.fontGeneration();
    lines.clear();
    for (const char* l : kHelp) {
        std::string t = l, cur;
        for (size_t a = 0; a <= t.size();) {
            size_t b = t.find("    ", a);
            if (b == std::string::npos) b = t.size();
            std::string part = t.substr(a, b - a), joined = cur.empty() ? part : cur + "    " + part;
            if (!cur.empty() && r.textWidth(joined, ts) > maxW) { lines.push_back(cur); cur = part; }
            else cur = joined;
            a = b + 4;
        }
        lines.push_back(cur);
    }
    return lines;
}

// The editor's dialogs (EXPORT's question, IMPORT's list, the keep-edits / save-a-copy question) share one template
// (ui-redesign-spec, the editor pages): a panel pw x ph centred in the 3D view over its key bar, the title at text 2
// 16 below its top, everything 16 inside its sides, one row of buttons 36 tall (8 apart) 16 above its bottom.
static void edDialogPanel(Ui& ui, float viewW, float barTop, float pw, float ph, float& px, float& py) {
    px = std::round(viewW * 0.5f - pw * 0.5f);
    py = std::round(std::max(8 * ui.s(), barTop * 0.5f - ph * 0.5f));
    ui.panel(px, py, pw, ph, 0.94f);
}

void Game::edUseMaterial(int m) {
    edRecentMats_.erase(std::remove(edRecentMats_.begin(), edRecentMats_.end(), m), edRecentMats_.end());
    edRecentMats_.insert(edRecentMats_.begin(), m);
    if (edRecentMats_.size() > 10) edRecentMats_.resize(10);
    if (FILE* f = fopen(userDataPath("editor-recent-materials.txt").c_str(), "w")) {   // (kept for the next session)
        for (int r : edRecentMats_) fprintf(f, "%d\n", r);
        fclose(f);
    }
}

void Game::edSetMaterial(int m) {
    std::vector<EdSel> sel;
    if (edSelValid()) sel.push_back(edSel_);
    for (const EdSel& e : edMulti_) if (!(e.kind == edSel_.kind && e.idx == edSel_.idx)) sel.push_back(e);
    Map tmp = copyNoSource(ed_);
    std::string ops, undo;
    for (const EdSel& e : sel) {
        if (e.kind == ED_BRUSH && (size_t)e.idx < ed_.editableBrushes()) tmp.brushes[e.idx].material = m;
        else if (e.kind == ED_GROUP && (size_t)e.idx < ed_.groups.size() && ed_.groups[e.idx].type != Map::GROUP_CUSTOM) tmp.groups[e.idx].material = m;
        else continue;
        const char k = kKindChar[e.kind];
        const std::string o = describeMapObject(tmp, k, "set", e.idx), u = describeMapObject(ed_, k, "set", e.idx);
        if (o == u) continue;
        ops += (ops.empty() ? "" : "\n") + o;
        undo = u + (undo.empty() ? "" : "\n") + undo;
    }
    if (!ops.empty()) edDo(ops, undo);
}

// The material picker: every material as a swatch drawn by the world shader (Renderer::swatch), a tab at a time (the
// ones this map uses, the last 10 picked, each kind, all), or what SEARCH finds. A click picks it (for new boxes, or
// for what's selected).
// the editor's name for each material (the panel's button; the picker names one by it, less "MATERIAL: ")
static const char* const matLabels[] = {"MATERIAL: BASE", "MATERIAL: ACCENT", "MATERIAL: WALLBANG", "MATERIAL: METAL", "MATERIAL: GLOW", "MATERIAL: FAR",
                                        "MATERIAL: WATER (WADE IN IT)", "MATERIAL: LAVA (KILLS)", "MATERIAL: TRIM (NOT SOLID)", "MATERIAL: CLIP (INVISIBLE WALL)",
                                        "MATERIAL: NOGRAPPLE (NO GRAPPLE)", "MATERIAL: ACID (HURTS)", "MATERIAL: ICE (SLIPPERY)", "MATERIAL: SAND",
                                        "MATERIAL: WOOD", "MATERIAL: PLASTIC", "MATERIAL: GRASS", "MATERIAL: RUBBER (BOUNCY)", "MATERIAL: STEEL",
                                        "MATERIAL: DIAMOND PLATE", "MATERIAL: BRICK",
                                        "MATERIAL: GLASS", "MATERIAL: ASPHALT (FAST)", "MATERIAL: MUD (STICKY)",
                                        "MATERIAL: COBBLESTONE", "MATERIAL: CONCRETE", "MATERIAL: TILE", "MATERIAL: MARBLE",
                                        "MATERIAL: CORRUGATED METAL", "MATERIAL: HAZARD STRIPES", "MATERIAL: SNOW", "MATERIAL: STONE BLOCKS",
                                        "MATERIAL: CRYSTAL", "MATERIAL: CIRCUIT", "MATERIAL: RUST", "MATERIAL: TERRACOTTA",
                                        "MATERIAL: BREAKABLE BRICK (THIN: BLOWS OPEN)", "MATERIAL: BREAKABLE GLASS", "MATERIAL: BREAKABLE COBBLESTONE (THIN: BLOWS OPEN)",
                                        "MATERIAL: LONDON STOCK",
                                        "MATERIAL: RED ENGINEERING BRICK",
                                        "MATERIAL: BLUE ENGINEERING BRICK",
                                        "MATERIAL: ORANGE WIRE-CUT",
                                        "MATERIAL: CLINKER BRICK",
                                        "MATERIAL: WHITEWASHED BRICK",
                                        "MATERIAL: BLACK BRICK",
                                        "MATERIAL: CREAM BRICK",
                                        "MATERIAL: FLEMISH BOND",
                                        "MATERIAL: ENGLISH BOND",
                                        "MATERIAL: STACK BOND",
                                        "MATERIAL: SOLDIER COURSE",
                                        "MATERIAL: HERRINGBONE BRICK",
                                        "MATERIAL: BASKET-WEAVE BRICK",
                                        "MATERIAL: ROMAN BRICK",
                                        "MATERIAL: TUDOR BRICK",
                                        "MATERIAL: GLAZED BRICK",
                                        "MATERIAL: WHITE GLAZED BRICK",
                                        "MATERIAL: MOSSY BRICK",
                                        "MATERIAL: EFFLORESCENT BRICK",
                                        "MATERIAL: RECLAIMED BRICK",
                                        "MATERIAL: BROWN RUSTIC BRICK",
                                        "MATERIAL: PINK BRICK",
                                        "MATERIAL: RECESSED JOINTS",
                                        "MATERIAL: FLUSH POINTED BRICK",
                                        "MATERIAL: BURNT BRICK",
                                        "MATERIAL: POLYCHROME BRICK",
                                        "MATERIAL: DIAPER BRICK",
                                        "MATERIAL: PAINTED RED BRICK",
                                        "MATERIAL: FIRE BRICK",
                                        "MATERIAL: FRESH GREY CONCRETE",
                                        "MATERIAL: DARK CONCRETE",
                                        "MATERIAL: WHITE CONCRETE",
                                        "MATERIAL: TINTED RED CONCRETE",
                                        "MATERIAL: EXPOSED AGGREGATE",
                                        "MATERIAL: BROOM-FINISHED",
                                        "MATERIAL: STAMPED STONE PATTERN",
                                        "MATERIAL: CONCRETE PAVERS",
                                        "MATERIAL: GRID-SCORED SLAB",
                                        "MATERIAL: RIBBED CONCRETE",
                                        "MATERIAL: BUSH-HAMMERED",
                                        "MATERIAL: FIBRE CEMENT BOARD",
                                        "MATERIAL: CAST-IN-PLACE WALLS",
                                        "MATERIAL: WEATHERED CONCRETE",
                                        "MATERIAL: BREEZE-BLOCK SCREEN",
                                        "MATERIAL: SPLIT-FACE BLOCK",
                                        "MATERIAL: GROUND-FACE BLOCK",
                                        "MATERIAL: CONCRETE ROOF TILES",
                                        "MATERIAL: LIGHTWEIGHT FOAM BLOCK",
                                        "MATERIAL: PAINTED CONCRETE STRIPES",
                                        "MATERIAL: WHITE SQUARE TILE",
                                        "MATERIAL: BLACK SQUARE TILE",
                                        "MATERIAL: CHECKERBOARD TILE",
                                        "MATERIAL: SUBWAY TILE",
                                        "MATERIAL: GREEN SUBWAY TILE",
                                        "MATERIAL: LARGE FORMAT PORCELAIN",
                                        "MATERIAL: HEX TILE WHITE",
                                        "MATERIAL: HEX TILE TERRACOTTA",
                                        "MATERIAL: DIAGONAL TILE",
                                        "MATERIAL: HERRINGBONE TILE",
                                        "MATERIAL: FISH-SCALE TILE",
                                        "MATERIAL: PENNY ROUND TILE",
                                        "MATERIAL: BLUE POOL TILE",
                                        "MATERIAL: MOROCCAN ZELLIGE",
                                        "MATERIAL: ENCAUSTIC TILE",
                                        "MATERIAL: QUARRY TILE",
                                        "MATERIAL: SLATE TILE",
                                        "MATERIAL: TRAVERTINE TILE",
                                        "MATERIAL: MOSAIC SPECKLE",
                                        "MATERIAL: BATHROOM MINT",
                                        "MATERIAL: GYM RUBBER TILE",
                                        "MATERIAL: HOSPITAL TILE",
                                        "MATERIAL: VICTORIAN BORDER",
                                        "MATERIAL: CRACKLE GLAZE TILE",
                                        "MATERIAL: MIRROR TILE",
                                        "MATERIAL: LIMESTONE ASHLAR",
                                        "MATERIAL: SANDSTONE ASHLAR",
                                        "MATERIAL: RED SANDSTONE",
                                        "MATERIAL: GRANITE BLOCKS",
                                        "MATERIAL: PINK GRANITE",
                                        "MATERIAL: BLACK BASALT BLOCKS",
                                        "MATERIAL: RANDOM ASHLAR",
                                        "MATERIAL: RUSTIC RANDOM ASHLAR",
                                        "MATERIAL: RUBBLE WALL",
                                        "MATERIAL: DRY-STONE WALL",
                                        "MATERIAL: FLINT AND BRICK",
                                        "MATERIAL: RUSTICATED BLOCKS",
                                        "MATERIAL: VERMICULATED STONE",
                                        "MATERIAL: CASTLE STONE",
                                        "MATERIAL: TEMPLE STONE",
                                        "MATERIAL: MARBLE BLOCKS",
                                        "MATERIAL: POLYGONAL STONE",
                                        "MATERIAL: COBBLED SETTS",
                                        "MATERIAL: DIAGONAL SETTS",
                                        "MATERIAL: FLAGSTONE PAVING",
                                        "MATERIAL: SLATE CLADDING",
                                        "MATERIAL: RIVER STONE WALL",
                                        "MATERIAL: GABBRO BLOCKS",
                                        "MATERIAL: TUFA BLOCKS",
                                        "MATERIAL: MOSSY STONE BLOCKS",
                                        "MATERIAL: DIRT",
                                        "MATERIAL: GRAVEL",
                                        "MATERIAL: GRANITE ROCK",
                                        "MATERIAL: SANDSTONE STRATA",
                                        "MATERIAL: SLATE",
                                        "MATERIAL: BASALT COLUMNS",
                                        "MATERIAL: MOSSY STONE",
                                        "MATERIAL: CRACKED EARTH",
                                        "MATERIAL: RED CLAY",
                                        "MATERIAL: OBSIDIAN",
                                        "MATERIAL: RIVER PEBBLES",
                                        "MATERIAL: TREE BARK",
                                        "MATERIAL: HEDGE",
                                        "MATERIAL: FOREST FLOOR",
                                        "MATERIAL: CORAL",
                                        "MATERIAL: LIMESTONE",
                                        "MATERIAL: SALT FLAT",
                                        "MATERIAL: SWAMP SLIME",
                                        "MATERIAL: FROST",
                                        "MATERIAL: TUNDRA",
                                        "MATERIAL: LICHEN ROCK",
                                        "MATERIAL: AMBER",
                                        "MATERIAL: JADE",
                                        "MATERIAL: BONE",
                                        "MATERIAL: HONEYCOMB",
                                        "MATERIAL: STUCCO",
                                        "MATERIAL: STRIPED WALLPAPER",
                                        "MATERIAL: PAINTED DRYWALL",
                                        "MATERIAL: BASKET-WEAVE PARQUET",
                                        "MATERIAL: WAINSCOT PANELLING",
                                        "MATERIAL: TERRAZZO",
                                        "MATERIAL: OFFICE CARPET TILES",
                                        "MATERIAL: LINOLEUM",
                                        "MATERIAL: PAINTED BRICK",
                                        "MATERIAL: CINDER BLOCK",
                                        "MATERIAL: ROOF SHINGLES",
                                        "MATERIAL: STANDING SEAM ROOF",
                                        "MATERIAL: THATCH",
                                        "MATERIAL: LOG CABIN",
                                        "MATERIAL: BAMBOO",
                                        "MATERIAL: TATAMI",
                                        "MATERIAL: SHOJI SCREEN",
                                        "MATERIAL: HEX FLOOR TILES",
                                        "MATERIAL: SIDEWALK SLABS",
                                        "MATERIAL: POLISHED GRANITE",
                                        "MATERIAL: CORK",
                                        "MATERIAL: PERFORATED METAL",
                                        "MATERIAL: RIVETED PLATES",
                                        "MATERIAL: CHIPPED PAINT",
                                        "MATERIAL: GALVANIZED STEEL",
                                        "MATERIAL: COPPER AND PATINA",
                                        "MATERIAL: BRASS",
                                        "MATERIAL: GOLD",
                                        "MATERIAL: CHROME",
                                        "MATERIAL: ALUMINIUM TREAD PLATE",
                                        "MATERIAL: CARBON FIBRE",
                                        "MATERIAL: COIN RUBBER FLOOR",
                                        "MATERIAL: SHIPPING CONTAINER",
                                        "MATERIAL: OIL-STAINED CONCRETE",
                                        "MATERIAL: CHAIN-LINK FENCE",
                                        "MATERIAL: SCAFFOLD BOARDS",
                                        "MATERIAL: NEON MAZE",
                                        "MATERIAL: FORCE FIELD",
                                        "MATERIAL: SERVER RACK",
                                        "MATERIAL: DATA RAIN",
                                        "MATERIAL: ALIEN BIOMASS",
                                        "MATERIAL: STARFIELD",
                                        "MATERIAL: SCI-FI PANELS",
                                        "MATERIAL: HEAT VENTS",
                                        "MATERIAL: POOL TABLE FELT",
                                        "MATERIAL: QUILTED LEATHER",
                                        "MATERIAL: SPORTS COURT",
                                        "MATERIAL: ASTROTURF",
                                        "MATERIAL: CARDBOARD",
                                        "MATERIAL: NEWSPAPER",
                                        "MATERIAL: CAMOUFLAGE",
                                        "MATERIAL: COOLED MAGMA",
                                        "MATERIAL: SHALE",
                                        "MATERIAL: QUARTZITE",
                                        "MATERIAL: TRAVERTINE",
                                        "MATERIAL: PUMICE",
                                        "MATERIAL: FLINT",
                                        "MATERIAL: RED CANYON ROCK",
                                        "MATERIAL: CHALK",
                                        "MATERIAL: GNEISS",
                                        "MATERIAL: MALACHITE",
                                        "MATERIAL: GEODE",
                                        "MATERIAL: WET CAVE ROCK",
                                        "MATERIAL: BOULDER FIELD",
                                        "MATERIAL: SCREE",
                                        "MATERIAL: JASPER",
                                        "MATERIAL: MICA SCHIST",
                                        "MATERIAL: PETRIFIED WOOD",
                                        "MATERIAL: DESERT VARNISH",
                                        "MATERIAL: CONGLOMERATE",
                                        "MATERIAL: GLACIER-POLISHED GRANITE",
                                        "MATERIAL: OPAL",
                                        "MATERIAL: ROOTY LOAM",
                                        "MATERIAL: PEAT",
                                        "MATERIAL: QUICKSAND",
                                        "MATERIAL: RIVERBANK CLAY",
                                        "MATERIAL: RUTTED DIRT ROAD",
                                        "MATERIAL: VOLCANIC ASH",
                                        "MATERIAL: MYCELIUM",
                                        "MATERIAL: OCHRE EARTH",
                                        "MATERIAL: RED DESERT SOIL",
                                        "MATERIAL: TILLED FIELD",
                                        "MATERIAL: TERMITE MOUND",
                                        "MATERIAL: SEABIRD CLIFF",
                                        "MATERIAL: FROZEN MUD",
                                        "MATERIAL: AUTUMN LEAVES",
                                        "MATERIAL: BLACK SAND",
                                        "MATERIAL: CLOVER",
                                        "MATERIAL: FLOWER MEADOW",
                                        "MATERIAL: IVY ON BRICK",
                                        "MATERIAL: MOSS CARPET",
                                        "MATERIAL: FERNS",
                                        "MATERIAL: PINE BARK",
                                        "MATERIAL: BIRCH BARK",
                                        "MATERIAL: PALM TRUNK",
                                        "MATERIAL: LOG END GRAIN",
                                        "MATERIAL: LILY PADS",
                                        "MATERIAL: REEDS",
                                        "MATERIAL: CACTUS",
                                        "MATERIAL: HAY",
                                        "MATERIAL: VINES ON STONE",
                                        "MATERIAL: MUSHROOM GILLS",
                                        "MATERIAL: TOADSTOOL",
                                        "MATERIAL: KELP",
                                        "MATERIAL: JUNGLE LEAVES",
                                        "MATERIAL: WHEAT",
                                        "MATERIAL: PINE BOUGHS",
                                        "MATERIAL: GLACIER ICE",
                                        "MATERIAL: WIND-PACKED SNOW",
                                        "MATERIAL: ICICLES",
                                        "MATERIAL: FROSTED PLANKS",
                                        "MATERIAL: RAIN-WET PAVING",
                                        "MATERIAL: LAKE ICE",
                                        "MATERIAL: SEA FOAM",
                                        "MATERIAL: WATERFALL",
                                        "MATERIAL: PUDDLED GROUND",
                                        "MATERIAL: HAIL",
                                        "MATERIAL: DEWY GRASS",
                                        "MATERIAL: SNOWFLAKE FROST",
                                        "MATERIAL: TIDAL MUD FLATS",
                                        "MATERIAL: CLOUD",
                                        "MATERIAL: ICE CAVE",
                                        "MATERIAL: RED DUNES",
                                        "MATERIAL: DESERT PAVEMENT",
                                        "MATERIAL: SULFUR CRUST",
                                        "MATERIAL: ROPY LAVA ROCK",
                                        "MATERIAL: SCORIA",
                                        "MATERIAL: FUMAROLE CRUST",
                                        "MATERIAL: ROCK SALT",
                                        "MATERIAL: BADLANDS",
                                        "MATERIAL: DESERT CRUST",
                                        "MATERIAL: BANDED IRON",
                                        "MATERIAL: TUFF",
                                        "MATERIAL: SHELL BEACH",
                                        "MATERIAL: HARDPAN",
                                        "MATERIAL: EMBER BED",
                                        "MATERIAL: CROSS-BEDDED SANDSTONE",
                                        "MATERIAL: BARNACLES",
                                        "MATERIAL: MUSSEL BED",
                                        "MATERIAL: MOTHER OF PEARL",
                                        "MATERIAL: SEA GLASS",
                                        "MATERIAL: FLOWSTONE",
                                        "MATERIAL: GLOWWORM CAVE",
                                        "MATERIAL: GLOWING ALGAE",
                                        "MATERIAL: QUARTZ CRYSTALS",
                                        "MATERIAL: COBWEBS",
                                        "MATERIAL: BEETLE-CARVED WOOD",
                                        "MATERIAL: SNAKE SKIN",
                                        "MATERIAL: FUR",
                                        "MATERIAL: TURTLE SHELL",
                                        "MATERIAL: FEATHERS",
                                        "MATERIAL: WASP NEST PAPER",
                                        "MATERIAL: BLUED STEEL",
                                        "MATERIAL: HEAT-TINTED TITANIUM",
                                        "MATERIAL: CAST IRON",
                                        "MATERIAL: WROUGHT IRON",
                                        "MATERIAL: BRONZE",
                                        "MATERIAL: PEWTER",
                                        "MATERIAL: ZINC",
                                        "MATERIAL: ANODIZED BLACK",
                                        "MATERIAL: GUNMETAL",
                                        "MATERIAL: HAMMERED COPPER",
                                        "MATERIAL: ENGINE-TURNED STEEL",
                                        "MATERIAL: MILL SCALE",
                                        "MATERIAL: POWDER-COAT GREEN",
                                        "MATERIAL: POWDER-COAT YELLOW",
                                        "MATERIAL: ELECTROPLATED NICKEL",
                                        "MATERIAL: TIN",
                                        "MATERIAL: LEAD SHEET",
                                        "MATERIAL: DAMASCUS STEEL",
                                        "MATERIAL: QUILTED STAINLESS",
                                        "MATERIAL: EXPANDED METAL",
                                        "MATERIAL: BOLTED ACCESS PANELS",
                                        "MATERIAL: DENTED STEEL",
                                        "MATERIAL: WELDED PATCHWORK",
                                        "MATERIAL: SHIP HULL PLATING",
                                        "MATERIAL: BOILER PLATE",
                                        "MATERIAL: LOCKER DOORS",
                                        "MATERIAL: ROLLER SHUTTER",
                                        "MATERIAL: SHEET PILING",
                                        "MATERIAL: TRAPEZOID CLADDING",
                                        "MATERIAL: INSULATED PANEL",
                                        "MATERIAL: ELECTRICAL CABINET",
                                        "MATERIAL: STEEL DOOR SKIN",
                                        "MATERIAL: VAULT DOOR",
                                        "MATERIAL: HEX BOLT GRID",
                                        "MATERIAL: TANK ARMOUR",
                                        "MATERIAL: AIRCRAFT SKIN",
                                        "MATERIAL: STEEL GRID WALL",
                                        "MATERIAL: PRESSED TIN CEILING",
                                        "MATERIAL: BAR GRATING",
                                        "MATERIAL: GALVANIZED GRATING",
                                        "MATERIAL: FIBREGLASS GRATING",
                                        "MATERIAL: EPOXY FLOOR",
                                        "MATERIAL: STEEL CHECKER FLOOR",
                                        "MATERIAL: INTERLOCKING RUBBER TILES",
                                        "MATERIAL: RAISED ACCESS FLOOR",
                                        "MATERIAL: LOADING DOCK FLOOR",
                                        "MATERIAL: POLISHED CONCRETE",
                                        "MATERIAL: STEEL DECK PLATE",
                                        "MATERIAL: MESH CATWALK",
                                        "MATERIAL: INDUSTRIAL TILES",
                                        "MATERIAL: PEELING GREEN PAINT",
                                        "MATERIAL: RUST STREAKS",
                                        "MATERIAL: HEAVY SCALE RUST",
                                        "MATERIAL: OXIDIZED STEEL",
                                        "MATERIAL: SOOTY STEEL",
                                        "MATERIAL: GREASE-STAINED METAL",
                                        "MATERIAL: SANDBLASTED STEEL",
                                        "MATERIAL: WATER-STAINED CONCRETE",
                                        "MATERIAL: SPALLED CONCRETE",
                                        "MATERIAL: BATTERED YELLOW PAINT",
                                        "MATERIAL: GRAFFITI-BUFFED WALL",
                                        "MATERIAL: BIRD-SPOTTED ROOF",
                                        "MATERIAL: SALT-CRUSTED STEEL",
                                        "MATERIAL: RADIATION WARNING",
                                        "MATERIAL: VENTILATION DUCT",
                                        "MATERIAL: RADIATOR FINS",
                                        "MATERIAL: CONTROL PANEL",
                                        "MATERIAL: GAUGE CLUSTER",
                                        "MATERIAL: FURNACE GLOW",
                                        "MATERIAL: MOLTEN SLAG",
                                        "MATERIAL: WARNING LIGHTS",
                                        "MATERIAL: RAILWAY SLEEPERS",
                                        "MATERIAL: RAIL TRACK",
                                        "MATERIAL: MANHOLE COVERS",
                                        "MATERIAL: STORM DRAIN GRATE",
                                        "MATERIAL: ROAD MARKINGS",
                                        "MATERIAL: ZEBRA CROSSING",
                                        "MATERIAL: PARKING BAY LINES",
                                        "MATERIAL: TACTILE PAVING",
                                        "MATERIAL: KERB STONES",
                                        "MATERIAL: PRECAST CONCRETE",
                                        "MATERIAL: BOARD-FORMED CONCRETE",
                                        "MATERIAL: GABION WALL",
                                        "MATERIAL: RETAINING WALL BLOCKS",
                                        "MATERIAL: TUNNEL LINING",
                                        "MATERIAL: BRIDGE GIRDER",
                                        "MATERIAL: RUBBLE PILE",
                                        "MATERIAL: CRACKED SLAB",
                                        "MATERIAL: WEEDS THROUGH CONCRETE",
                                        "MATERIAL: SHATTERED TILES",
                                        "MATERIAL: EXPOSED REBAR",
                                        "MATERIAL: BULLET-POCKED WALL",
                                        "MATERIAL: COLLAPSED BRICK",
                                        "MATERIAL: BURNT-OUT RUIN WALL",
                                        "MATERIAL: TOPPLED COLUMN",
                                        "MATERIAL: RUINED PARQUET",
                                        "MATERIAL: CRUMBLING PLASTER",
                                        "MATERIAL: SANDBAG WALL",
                                        "MATERIAL: TANK TRAP FIELD",
                                        "MATERIAL: DUSTY LINOLEUM",
                                        "MATERIAL: CHARRED TIMBER",
                                        "MATERIAL: SCRAP HEAP",
                                        "MATERIAL: PALLET STACK",
                                        "MATERIAL: GUTTED MACHINERY",
                                        "MATERIAL: AIRCRAFT WRECKAGE",
                                        "MATERIAL: FRIDGE GRAVEYARD",
                                        "MATERIAL: RUSTED SHEET FENCE",
                                        "MATERIAL: ENGINE BLOCK",
                                        "MATERIAL: CHAIN HEAP",
                                        "MATERIAL: BENT REBAR BUNDLE",
                                        "MATERIAL: DUST STORM HAZE",
                                        "MATERIAL: SALT FLAT (WASTELAND)",
                                        "MATERIAL: BLEACHED BONES",
                                        "MATERIAL: SUN-BAKED HARDPAN",
                                        "MATERIAL: RIPPLE SAND",
                                        "MATERIAL: DRIED RIVERBED",
                                        "MATERIAL: SANDBLASTED SIGN",
                                        "MATERIAL: RUST DUNES",
                                        "MATERIAL: DUSTY GLASS",
                                        "MATERIAL: TUMBLEWEED BRUSH",
                                        "MATERIAL: DRIED BLOOD EARTH",
                                        "MATERIAL: SAND-BURIED ROAD",
                                        "MATERIAL: CALICHE",
                                        "MATERIAL: WIND-SCOURED ROCK",
                                        "MATERIAL: ASH DUNES",
                                        "MATERIAL: RADIOACTIVE PUDDLES",
                                        "MATERIAL: MUTANT GROWTH",
                                        "MATERIAL: ACID-ETCHED METAL",
                                        "MATERIAL: HAZMAT TARP",
                                        "MATERIAL: GLOWING FISSURES",
                                        "MATERIAL: OILY WATER",
                                        "MATERIAL: SULFUR PIT CRUST",
                                        "MATERIAL: CONTAMINATED SOIL",
                                        "MATERIAL: BUBBLING TAR",
                                        "MATERIAL: WARNING TAPE MESH",
                                        "MATERIAL: IRRADIATED GLASS",
                                        "MATERIAL: BIO-SLUDGE DRAIN",
                                        "MATERIAL: SCRAP METAL PLATING",
                                        "MATERIAL: CRATE WOOD",
                                        "MATERIAL: MUD AND STRAW DAUB",
                                        "MATERIAL: LEAN-TO CANVAS",
                                        "MATERIAL: FIRE-CRACKED EARTH",
                                        "MATERIAL: SMOULDERING ASH",
                                        "MATERIAL: BLISTERED PAINT",
                                        "MATERIAL: MELTED PLASTIC",
                                        "MATERIAL: ROTTING WOOD",
                                        "MATERIAL: MOULD BLOOM",
                                        "MATERIAL: CORRODED COPPER ROOF",
                                        "MATERIAL: PEELING WALLPAPER",
                                        "MATERIAL: TERMITE-EATEN BEAM",
                                        "MATERIAL: CHARCOAL FLOOR",
                                        "MATERIAL: LICHEN-CRUSTED STONE",
                                        "MATERIAL: DEAD FOREST FLOOR",
                                        "MATERIAL: WITHERED VINES",
                                        "MATERIAL: BONE-DRY WOOD",
                                        "MATERIAL: SCORCHED TILES",
                                        "MATERIAL: CLEAR LAGOON",
                                        "MATERIAL: SWAMP WATER",
                                        "MATERIAL: OIL SLICK",
                                        "MATERIAL: TAR PIT",
                                        "MATERIAL: HONEY",
                                        "MATERIAL: MERCURY",
                                        "MATERIAL: MOLTEN GOLD",
                                        "MATERIAL: MAGMA RIVER",
                                        "MATERIAL: TOXIC SLUDGE",
                                        "MATERIAL: BLOOD POOL",
                                        "MATERIAL: HOT SPRING",
                                        "MATERIAL: QUICKSAND (LIQUIDS)",
                                        "MATERIAL: ICE SLUSH",
                                        "MATERIAL: PLASMA POOL",
                                        "MATERIAL: INK",
                                        "MATERIAL: GLOWING TIDE",
                                        "MATERIAL: SEWAGE",
                                        "MATERIAL: WET CEMENT",
                                        "MATERIAL: PINK SLIME",
                                        "MATERIAL: COOLANT",
                                        "MATERIAL: WHITE RAPIDS",
                                        "MATERIAL: DEEP OCEAN",
                                        "MATERIAL: FIZZING SODA",
                                        "MATERIAL: LIQUID VOID",
                                        "MATERIAL: MUD BATH",
                                        "MATERIAL: WIDE PLANKS",
                                        "MATERIAL: HUGE PLANKS",
                                        "MATERIAL: LARGE BRICK",
                                        "MATERIAL: HUGE BRICK",
                                        "MATERIAL: LARGE COBBLES",
                                        "MATERIAL: HUGE COBBLES",
                                        "MATERIAL: LARGE TILE",
                                        "MATERIAL: HUGE TILE",
                                        "MATERIAL: LARGE STONE",
                                        "MATERIAL: HUGE STONE",
                                        "MATERIAL: LARGE LONDON STOCK",
                                        "MATERIAL: LARGE RED ENGINEERING BRICK",
                                        "MATERIAL: LARGE HERRINGBONE BRICK",
                                        "MATERIAL: LARGE BASKET-WEAVE BRICK",
                                        "MATERIAL: LARGE WHITE SQUARE TILE",
                                        "MATERIAL: LARGE CHECKERBOARD TILE",
                                        "MATERIAL: LARGE SUBWAY TILE",
                                        "MATERIAL: LARGE HEX TILE, TERRACOTTA",
                                        "MATERIAL: LARGE HERRINGBONE TILE",
                                        "MATERIAL: LARGE SLATE TILE",
                                        "MATERIAL: LARGE TRAVERTINE TILE",
                                        "MATERIAL: LARGE LIMESTONE ASHLAR",
                                        "MATERIAL: LARGE SANDSTONE ASHLAR",
                                        "MATERIAL: LARGE GRANITE BLOCKS",
                                        "MATERIAL: LARGE RANDOM ASHLAR",
                                        "MATERIAL: LARGE DRY-STONE WALL",
                                        "MATERIAL: LARGE CASTLE STONE",
                                        "MATERIAL: LARGE POLYGONAL STONE",
                                        "MATERIAL: LARGE COBBLED SETTS",
                                        "MATERIAL: LARGE DIAGONAL SETTS",
                                        "MATERIAL: LARGE FLAGSTONE PAVING",
                                        "MATERIAL: LARGE RIVER STONE WALL",
                                        "MATERIAL: LARGE MOSSY STONE BLOCKS",
                                        "MATERIAL: LARGE BASKET-WEAVE PARQUET",
                                        "MATERIAL: LARGE LOG CABIN",
                                        "MATERIAL: LARGE SCAFFOLD BOARDS",
                                        "MATERIAL: LARGE FROSTED PLANKS",
                                        "MATERIAL: LARGE CRATE WOOD",
                                        "MATERIAL: LARGE TEMPLE STONE",
                                        "MATERIAL: LARGE KERB STONES",
                                        "MATERIAL: BLUE TINTED GLASS",
                                        "MATERIAL: SMOKED GLASS",
                                        "MATERIAL: AMBER GLASS",
                                        "MATERIAL: FROSTED GLASS",
                                        "MATERIAL: REEDED GLASS",
                                        "MATERIAL: STAINED GLASS",
                                        "MATERIAL: GLASS BLOCK",
                                        "MATERIAL: SCRATCHED PLEXIGLASS",
                                        "MATERIAL: ICE WALL",
                                        "MATERIAL: QUARTZ CRYSTAL",
                                        "MATERIAL: GREEN GEL",
                                        "MATERIAL: HEX FORCE FIELD",
                                        "MATERIAL: RED LASER BARRIER",
                                        "MATERIAL: HOLOGRAM PANEL",
                                        "MATERIAL: CHAIN-LINK MESH",
                                        "MATERIAL: CARGO NET",
                                        "MATERIAL: SOAP FILM",
                                        "MATERIAL: WATER CURTAIN",
                                        "MATERIAL: SMOKE WALL",
                                        "MATERIAL: RAIN WINDOW",
                                        "MATERIAL: PRISM GLASS",
                                        "MATERIAL: JELLY CUBE",
                                        "MATERIAL: GHOST GLASS",
                                        "MATERIAL: SCANNER BEAM",
                                        "MATERIAL: BOTTLE GREEN GLASS",
                                        "MATERIAL: WOODEN CRATE",
                                        "MATERIAL: PLYWOOD BOARD-UP",
                                        "MATERIAL: DRYWALL",
                                        "MATERIAL: CINDER BLOCK WALL",
                                        "MATERIAL: PAPER SCREEN",
                                        "MATERIAL: BAMBOO FENCE",
                                        "MATERIAL: CARDBOARD BOXES",
                                        "MATERIAL: PICKET FENCE",
                                        "MATERIAL: CHIPBOARD",
                                        "MATERIAL: RUSTED SHEET",
                                        "MATERIAL: THIN CONCRETE PANEL",
                                        "MATERIAL: CRYSTAL CLUSTER",
                                        "MATERIAL: SANDSTONE SLAB",
                                        "MATERIAL: SUGAR GLASS",
                                        "MATERIAL: HAY BALES",
                                        "MATERIAL: ROTTEN PLANKS",
                                        "MATERIAL: CERAMIC TILE WALL",
                                        "MATERIAL: MIRROR",
                                        "MATERIAL: BARRICADE",
                                        "MATERIAL: FROZEN WATERFALL",
                                        "MATERIAL: STAINED GLASS WINDOW",
                                        "MATERIAL: TYRE STACK",
                                        "MATERIAL: LATH AND PLASTER",
                                        "MATERIAL: GLASS BOTTLES",
                                        "MATERIAL: HEALING TILE",
                                        "MATERIAL: SPIKE FLOOR",
                                        "MATERIAL: LOW GRAVITY FIELD",
                                        "MATERIAL: SLOW-MO FIELD",
                                        "MATERIAL: GRAVITY FLIP",
                                        "MATERIAL: CHECKPOINT GATE",
                                        "MATERIAL: LIGHT BLOCK",
                                        "MATERIAL: SHIELD WALL",
                                        "MATERIAL: CRUMBLE PLATFORM",
                                        "MATERIAL: TELEPORTER"};
static_assert(sizeof matLabels / sizeof *matLabels == kMapMaterials, "a label per material (the cycles below index it by material)");

// the picker's tabs: the ones this map uses, the last 10 picked, then by kind, then all of them. kMatTabs: the kinds,
// each a run of materials (the look rounds' categories)
static const char* const kMatTabNames[] = {"IN THIS MAP", "RECENT", "BASIC", "BRICK", "CONCRETE", "TILE", "STONE", "BUILDINGS",
                                           "NATURE", "INDUSTRIAL", "SCI-FI / DECOR", "WASTELAND", "LIQUIDS", "LARGE", "SEE-THROUGH", "BREAKABLE", "FEATURES", "ALL"};
constexpr int kMatTabCount = (int)(sizeof kMatTabNames / sizeof *kMatTabNames), kMatTabAll = kMatTabCount - 1;
static const struct { int tab, first, last; } kMatTabs[] = {
    {2, 0, kMatBlocks - 1}, {3, 39, 68}, {4, 69, 88}, {5, 89, 113}, {6, 114, 138}, {8, 139, 163}, {7, 164, 184}, {9, 185, 199},
    {10, 200, 215}, {8, 216, 315}, {9, 316, 401}, {11, 402, 471}, {12, kMatLiquids, kMatLiquids + kLiquidCount - 1}, {13, kMatLarge, kMatSemi - 1}, {14, kMatSemi, kMatSemi + kSemiCount - 1}, {15, kMatBreak, kMatBreak + kBreakCount - 1}, {16, kMatFeat, kMatFeat + kFeatCount - 1}};
static_assert(kMapMaterials == kMatFeat + kFeatCount && kMatFeat == kMatBreak + kBreakCount && kMatBreak == kMatSemi + kSemiCount && kMatSemi == kMatLarge + kLargeCount && kMatLarge == kMatLiquids + kLiquidCount, "a picker tab for every material (kMatTabs)");

void Game::drawEditorMatMenu() {
    const float s = ui_.s(), pad = 16 * s, gap = 8 * s, bh = 36 * s, th = 30 * s;
    const float pw = (float)r_.width() - 16 * s;   // (the whole window, owner 2026-10-29: "take up the entire screen")
    static bool loaded = false;
    if (!loaded) {   // (the last session's recent list)
        loaded = true;
        if (FILE* f = fopen(userDataPath("editor-recent-materials.txt").c_str(), "r")) {
            int r;
            while (edRecentMats_.size() < 10 && fscanf(f, "%d", &r) == 1)
                if (validMapMaterial(r) && std::find(edRecentMats_.begin(), edRecentMats_.end(), r) == edRecentMats_.end()) edRecentMats_.push_back(r);
            fclose(f);
        }
    }
    // the list: what SEARCH matches (every word in the name), else the tab's
    std::vector<int> list;
    std::string find = edMatSearch_;
    for (auto& c : find) c = (char)toupper((unsigned char)c);
    if (!find.empty()) {
        std::vector<std::string> words;
        for (size_t a = 0, b; a < find.size(); a = b + 1) {
            b = find.find(' ', a);
            if (b == std::string::npos) b = find.size();
            if (b > a) words.push_back(find.substr(a, b - a));
        }
        for (int m = 0; m < kMapMaterials; m++) {
            const std::string name = matLabels[m] + 10;
            bool all = true;
            for (auto& w : words) all = all && name.find(w) != std::string::npos;
            if (all) list.push_back(m);
        }
    } else if (edMatTab_ == 0) {
        bool in[kMapMaterials] = {};
        for (size_t i = 0; i < ed_.editableBrushes(); i++) if (validMapMaterial(ed_.brushes[i].material)) in[ed_.brushes[i].material] = true;
        for (auto& g : ed_.groups) {
            if (g.type != Map::GROUP_CUSTOM && validMapMaterial(g.material)) in[g.material] = true;
            for (auto& p : g.parts) if (validMapMaterial(p.material)) in[p.material] = true;
        }
        for (int m = 0; m < kMapMaterials; m++) if (in[m]) list.push_back(m);
    } else if (edMatTab_ == 1) list = edRecentMats_;
    else for (auto& t : kMatTabs)
        if (t.tab == edMatTab_ || edMatTab_ == kMatTabAll)
            for (int m = t.first; m <= t.last; m++) list.push_back(m);
    // swatches 200 across (owner, 2026-10-29: "100x100 pixels each, so I can see details", then "twice as big"), named
    // under; as many columns and rows as fit the window: the wheel scrolls the rest (edMatScroll_, in rows)
    const float sw = 200 * s, cell = sw + 18 * s + gap;
    const int cols = std::max(1, (int)((pw - 2 * pad + gap) / (sw + gap)));
    const int tabCols = std::min(kMatTabCount, std::max(1, (int)((pw - 2 * pad + gap) / (118 * s + gap))));
    const int tabRows = (kMatTabCount + tabCols - 1) / tabCols;
    const float maxH = (float)r_.height() - 16 * s, head = pad + bh + gap + tabRows * (th + gap) + 4 * s, foot = gap + bh + pad;
    const int rowsAll = std::max(1, (int)((list.size() + (size_t)cols - 1) / (size_t)cols));
    const int fitRows = std::max(1, (int)((maxH - head - foot) / cell));
    const int rows = std::min(rowsAll, fitRows);
    edMatScroll_ = std::clamp(edMatScroll_, 0, rowsAll - rows);
    const float ph = maxH, px = 8 * s, py = 8 * s;
    ui_.panel(px, py, pw, ph, 1.0f);
    const int cur = edMatMenu_ == 2 && edSelValid() && edSel_.kind == ED_BRUSH ? ed_.brushes[edSel_.idx].material
                  : edMatMenu_ == 2 && edSelValid() && edSel_.kind == ED_GROUP ? ed_.groups[edSel_.idx].material : edMat_;
    // SEARCH, then the tabs
    const float iw = pw - 2 * pad;
    ui_.label(px + pad, py + pad + bh * 0.5f - 9 * s, "SEARCH", kGrey, 0.9f);
    if (ui_.textField(44, px + pad + 90 * s, py + pad, std::min(iw - 90 * s, 480 * s), bh, edMatSearch_, 32, UiExp::local().withId("SEARCH MATERIALS")))
        edMatScroll_ = 0;
    const float tw = (iw - (tabCols - 1) * gap) / tabCols;
    for (int t = 0; t < kMatTabCount; t++) {
        const float x = px + pad + (float)(t % tabCols) * (tw + gap), y = py + pad + bh + gap + (float)(t / tabCols) * (th + gap);
        if (ui_.tab(x, y, tw, th, kMatTabNames[t], find.empty() && t == edMatTab_, (find.empty() && t == edMatTab_ ? UiExp::inert() : UiExp::state("edmattab")).withId(kMatTabNames[t]), true)) {
            edMatTab_ = t; edMatScroll_ = 0; edMatSearch_.clear();
        }
    }
    float y = py + head;
    int hovered = -1, picked = -1;
    if (list.empty()) ui_.hint(px + pad, y, find.empty() ? "NONE YET" : "NO MATERIAL NAMED THAT", kGrey, iw);
    const size_t first = (size_t)edMatScroll_ * (size_t)cols, end = std::min(list.size(), first + (size_t)rows * (size_t)cols);
    for (size_t k = first; k < end; k++) {
        const int m = list[k];
        const float x = px + pad + (float)((k - first) % cols) * (sw + gap), yy = y + (float)((k - first) / cols) * cell;
        // (the button is the frame round the swatch: its hover and the current one's ring show past the swatch's edge)
        static std::string ids[kMapMaterials];   // (UiExp keeps the id's pointer)
        std::string& id = ids[m];
        if (id.empty()) id = std::string("MATERIAL ") + kMapMaterialWords[m];
        const UiExp e = edMatMenu_ == 1 || m == cur ? UiExp::nav(edPath()) : UiExp::edit();   // (new ones: the menu closes, edMat_ set)
        if (ui_.button(x - 3 * s, yy - 3 * s, sw + 6 * s, sw + 6 * s, "", e.withId(id.c_str()))) picked = m;
        if (m == cur) r_.rect(x - 3 * s, yy - 3 * s, sw + 6 * s, sw + 6 * s, kPick);
        r_.swatch(x, yy, sw, sw, m);
        ui_.hint(x, yy + sw + 3 * s, matLabels[m] + 10, m == cur ? kPick : kWhite, sw + gap);
        if (ui_.over(x - 3 * s, yy - 3 * s, sw + 6 * s, sw + 6 * s)) hovered = m;
    }
    // CLOSE; the scroll position; the name of the one under the mouse (the names under the swatches get cut)
    const float closeY = py + ph - pad - bh;
    if (ui_.button(px + pad, closeY, 160 * s, bh, "CLOSE", UiExp::nav(edPath()))) edMatMenu_ = 0;
    float nx = px + pad + 176 * s;
    if (rowsAll > rows) {
        char b[64];
        snprintf(b, sizeof b, "ROWS %d-%d OF %d (WHEEL)", edMatScroll_ + 1, edMatScroll_ + rows, rowsAll);
        ui_.label(nx, closeY + bh * 0.5f - 9 * s, b, kGrey, 0.8f);
        nx += 300 * s;
    }
    if (hovered >= 0 || cur >= 0) ui_.label(nx, closeY + bh * 0.5f - 9 * s, matLabels[hovered >= 0 ? hovered : cur] + 10, hovered >= 0 ? kWhite : kPick, 0.9f);
    if (picked >= 0) {
        if (edMatMenu_ == 1) edMat_ = picked;
        else { edMat_ = picked; edSetMaterial(picked); }
        edUseMaterial(picked);
        edMatMenu_ = 0;
    }
}

// IMPORT's list (newest first, click one to place it) / EXPORT's question, over the 3D view
void Game::drawEditorGroupMenu() {
    const float s = ui_.s(), pad = 16 * s, bh = 36 * s, gap = 8 * s;
    const float barTop = edBarTop_ > 0 ? edBarTop_ : (float)r_.height();
    float px, py;
    if (edExportAsk_) {
        const float pw = 392 * s, iw = pw - 2 * pad, bw = (iw - gap) * 0.5f;
        edDialogPanel(ui_, edPanelX_, barTop, pw, 100 * s, px, py);
        ui_.labelCentered(px + pw * 0.5f, py + pad, "NOT A GROUP - MAKE IT A GROUP FIRST?", kWhite, 1.0f, iw);
        if (ui_.button(px + pad, py + 48 * s, bw, bh, "NO", UiExp::nav(edPath()))) { edExportAsk_ = false; return; }
        if (ui_.button(px + pad + bw + gap, py + 48 * s, bw, bh, "YES", UiExp::state("groups"))) edExportGroup();
        return;
    }
    // the files 32 tall, 36 apart, as many as fit over the key bar (at most 12); none: a note in their place
    const float pw = 432 * s, iw = pw - 2 * pad, bw = (iw - gap) * 0.5f, step = 36 * s;
    const int fit = std::max(1, std::min(12, (int)((barTop - 16 * s - 108 * s) / step)));
    const int rows = std::min<int>((int)edGroupFiles_.size(), fit);
    const float listH = rows ? rows * step - 4 * s : 12 * s;
    edDialogPanel(ui_, edPanelX_, barTop, pw, 48 * s + listH + 12 * s + bh + pad, px, py);
    ui_.labelCentered(px + pw * 0.5f, py + pad, "CUSTOM GROUPS", kWhite, 1.0f, iw);
    float y = py + 48 * s;
    if (!rows) ui_.hintCentered(px + pw * 0.5f, y, "NONE YET - EXPORT ONE, OR COPY A GROUP AND IMPORT", kGrey, iw);
    for (int i = 0; i < rows; i++, y += step) {
        std::string name = std::filesystem::path(edGroupFiles_[i]).stem().string();
        for (auto& c : name) c = (char)toupper((unsigned char)c);
        if (ui_.button(px + pad, y, iw, 32 * s, name, UiExp::edit().withId("GROUP FILE"))) {
            edGroupMenu_ = false;
            if (!edPlaceGroup(readFile(edGroupFiles_[i]))) edNotify("not a quark group file");
            return;
        }
    }
    y = py + 48 * s + listH + 12 * s;
    if (ui_.button(px + pad, y, bw, bh, "CLOSE", UiExp::nav(edPath()))) { edGroupMenu_ = false; return; }
    if (ui_.button(px + pad + bw + gap, y, bw, bh, "OPEN FOLDER", UiExp::url())) openGroupFolder();
}

// Everything under the ray, nearest first (each object once). A box the camera is inside (a big clip volume,
// an invisible ceiling you're flying in, a zone) comes after everything else: it's all around you, so a click
// means what's in front; clicking again on the same spot steps back through the list to reach it.
std::vector<Game::EdSel> Game::edPickAll(const Vec3& o, const Vec3& dir) const {
    Vec3 inv = safeInv(dir);
    std::vector<std::pair<float, EdSel>> hits;
    auto test = [&](int kind, int idx, const AABB& b, float bias) {
        float t = rayAABB(o, inv, b, 1000);
        if (t < 0) return;
        bool inside = o.x > b.mn.x && o.x < b.mx.x && o.y > b.mn.y && o.y < b.mx.y && o.z > b.mn.z && o.z < b.mx.z;
        hits.push_back({inside ? 1e6f + length(b.mx - b.mn) : t + bias, EdSel{kind, idx}});
    };
    // markers win over the box they sit on / inside (zones are see-through, so they only win when nothing's closer)
    for (size_t i = 0; i < ed_.spawns.size(); i++) test(ED_SPAWN, (int)i, edBounds(ed_, {ED_SPAWN, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.lights.size(); i++) test(ED_LIGHT, (int)i, edBounds(ed_, {ED_LIGHT, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.payloadPath.size(); i++) test(ED_PATH, (int)i, edBounds(ed_, {ED_PATH, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.brushes.size(); i++) // (clip is drawn see-through, so like a zone it only wins when nothing's closer)
        if (!ed_.brushes[i].generated) test(ED_BRUSH, (int)i, ed_.brushes[i].box, ed_.brushes[i].material == 9 ? 0.25f : 0);
    for (size_t i = 0; i < ed_.ladders.size(); i++) test(ED_LADDER, (int)i, ed_.ladders[i], -0.3f);
    for (size_t i = 0; i < ed_.groups.size(); i++) { // a group: where you hit one of its boxes (a room's inside is open)
        std::vector<Brush> parts;
        Map::groupBoxes(ed_.groups[i], parts);
        for (auto& p : parts) test(ED_GROUP, (int)i, p.box, p.material == 9 ? 0.25f : 0);
    }
    for (size_t i = 0; i < ed_.bombSites.size(); i++) test(ED_SITE, (int)i, ed_.bombSites[i], 0.25f);
    for (size_t i = 0; i < ed_.checkpoints.size(); i++) test(ED_CHECK, (int)i, ed_.checkpoints[i], 0.25f);
    if (ed_.hasHill) test(ED_HILL, 0, ed_.hill, 0.25f);
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<EdSel> out;
    for (auto& h : hits)
        if (std::none_of(out.begin(), out.end(), [&](EdSel e) { return e.kind == h.second.kind && e.idx == h.second.idx; })) out.push_back(h.second);
    return out;
}


// Grab points on the selection: one per face of a box (drag = resize that side, Shift+drag = move), or
// arrows out of a point object along each axis (drag = move).
std::vector<Game::EdHandle> Game::edHandles() const {
    std::vector<EdHandle> hs;
    if (edSelValid()) handlesOf(edBounds(ed_, edSel_), edSel_.kind == ED_SPAWN || edSel_.kind == ED_LIGHT || edSel_.kind == ED_PATH, hs);
    return hs;
}

// World-space direction under the mouse (the screen center while mouse-looking).
Vec3 Game::edMouseRay() const {
    Vec3 f = angleForward(yaw_, pitch_);
    if (edLook_) return f;
    Vec3 right = normalize(cross(f, {0, 1, 0})), up = cross(right, f);
    // the 3D view stops at the panel (while the view is drawn the renderer is already that wide: don't take the
    // panel off twice, or the ray only matches the mouse at the centre of the view)
    float H = (float)r_.height(), W = std::min((float)r_.width(), (float)edFullW_ - 320 * (H / 720.0f));
    float tx = std::tan(deg2rad(cfg_.fov) * 0.5f), ty = tx * H / W;
    float nx = uiIn_.mx / W * 2 - 1, ny = 1 - uiIn_.my / H * 2;
    return normalize(f + right * (nx * tx) + up * (ny * ty));
}

bool Game::edSaveCopy(const Map& m, const std::string& name) {
    if (!validMapName(name)) { edNotify("name: letters, digits, - and _ only (not a device name like con)"); return false; }
    // a built-in map's name is never written to (your copy would hide the real map from you): edits to one are kept
    // under a new name, when you say so (B3)
    if (isOfficialMapName(name)) { edNotify(name + " is a built-in map - pick a new name for yours"); return false; }
    std::string text = m.serialize();
    Map check;
    std::string err;
    if (!check.parse(text, &err)) { edNotify("can't save: " + err); return false; }
    if (!saveMapFile(userMapsDir() + "/" + name + ".qmap", text)) { edNotify("could not write maps/" + name + ".qmap"); return false; }
    rescanMaps();
    edNotify("saved maps/" + name + ".qmap");
    return true;
}

bool Game::edSave(int then) {
    if (isOfficialMapName(edName_)) {   // a built-in map: its file stays as it is; your edits, if any, go to a new map
        if (edDirty_) { edAskKeep(then); return false; }
        if (then == ED_THEN_SAVE) edNotify(edName_ + " is a built-in map - nothing changed to save");
        return then != ED_THEN_SAVE;   // (test / host it as it is)
    }
    if (!edSaveCopy(ed_, edName_)) return false;
    edDirty_ = false;
    return true;
}

// B3: you edited a built-in map and are saving or leaving it: ask whether to keep the edits as a new map of your own
void Game::edAskKeep(int then, const std::string& open) {
    edPrompt_ = true;
    edPromptThen_ = then;
    edPromptOpen_ = open;
    edPromptOfficial_ = true;
    edPromptMap_ = ed_;
    edPromptName_ = validMapName(edName_) && !isOfficialMapName(edName_) ? edName_ : edFreshName(isOfficialMapName(edName_) ? edName_ : ed_.name);
    edLook_ = false;
    captureMouse(false);
    ui_.clearFocus();
}

std::string Game::edFreshName(const std::string& base) const {
    std::string stem = typedMapName(base);
    if (stem.empty()) stem = "mymap";
    if (stem.size() > 32) stem.resize(32);
    std::error_code ec;
    for (int k = 1; k < 1000; k++) {
        std::string n = stem + "_edit" + (k > 1 ? std::to_string(k) : std::string());
        if (validMapName(n) && !isOfficialMapName(n) && !std::filesystem::exists(userMapsDir() + "/" + n + ".qmap", ec)) return n;
    }
    return stem + "_edit";
}

void Game::edPromptAnswer(int answer) {
    const int then = edPromptThen_;
    const std::string open = edPromptOpen_;
    const bool inEditor = screen_ == Screen::Editor;
    if (answer < 0) { edPrompt_ = false; return; }   // cancel: back to editing, nothing saved or dropped
    std::string saved;
    if (answer > 0) {
        const std::string name = typedMapName(edPromptName_);
        if (then == ED_THEN_SAVE && edOnline_ && host_) {   // your own session: it carries on as the new map (autosaved)
            if (!validMapName(name) || isOfficialMapName(name)) { edNotify("pick a new name for your copy"); return; }
            net_.sendCommand("editmode saveas " + name);
            edName_ = name;
            edPrompt_ = false;
            return;
        }
        if (!edSaveCopy(edPromptMap_, name)) return;   // (the question stays up, with the reason)
        saved = name;
        edPrompt_ = false;
        if (inEditor && !edOnline_) { edName_ = ed_.name = name; edDirty_ = false; }
    } else {
        edPrompt_ = false;
        if (inEditor && !edOnline_ && (then == ED_THEN_SAVE || then == ED_THEN_TEST || then == ED_THEN_HOST)) {
            Vec3 cam = edCam_;   // no: the edits are dropped, the built-in map is back as it is (where you were looking)
            float yaw = yaw_, pitch = pitch_;
            openEditor(isOfficialMapName(edName_) ? edName_ : ed_.name);
            edCam_ = cam; yaw_ = yaw; pitch_ = pitch;
            edNotify("edits dropped");
        }
    }
    if (!inEditor) return;   // (after a session, on the main menu: saved or not, that's all)
    switch (then) {
        case ED_THEN_LEAVE: closeEditor(); break;
        case ED_THEN_TEST: edTest(); break;
        case ED_THEN_HOST: edHostSession(); break;
        case ED_THEN_OPEN: openEditor(open); break;
        case ED_THEN_NONE: if (!saved.empty() && !edOnline_) openEditor(saved); break;   // (back from your session: your new map)
        default: break;
    }
}

void Game::edTest() {
    if (!edSave(ED_THEN_TEST)) return;
    int mode = -1;
    if (ed_.preferredMode >= 0 && ed_.supportsMode(ed_.preferredMode)) mode = ed_.preferredMode;
    else if (ed_.supportsMode(MODE_FFA)) mode = MODE_FFA;
    else for (int m = 0; m < MODE_COUNT && mode < 0; m++) if (ed_.supportsMode(m)) mode = m;
    if (mode < 0) { edNotify("no mode can run on this map yet"); return; }
    testMap_ = edName_;
    testMode_ = mode;
    testEdit_ = false;
    edLook_ = false;
    startHost(false);
}

// Host this map as an edit session others can join (Join screen / your address), using your host settings.
void Game::edHostSession() {
    if (!edSave(ED_THEN_HOST)) return;
    testMap_ = edName_;
    testMode_ = MODE_FFA;
    testEdit_ = true;
    edLook_ = false;
    startHost(false);
}

void Game::editorKey(int key, int mods) {
    bool ctrl = (mods & GLFW_MOD_CONTROL) || (mods & GLFW_MOD_SUPER), shift = mods & GLFW_MOD_SHIFT;
    const float g = kGrids[edGridIdx_];
    if (edPrompt_) {   // the save question is up: it takes the keys (ESC: cancel, or "no" when there's nothing to go back to)
        if (key == GLFW_KEY_ESCAPE) edPromptAnswer(edPromptThen_ == ED_THEN_NONE ? 0 : -1);
        else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) edPromptAnswer(1);
        return;
    }
    if (key == GLFW_KEY_ESCAPE) {
        if (edLook_) { edLook_ = false; captureMouse(false); return; }
        if (edGroupMenu_ || edExportAsk_) { edGroupMenu_ = edExportAsk_ = false; return; }
        if (edMatMenu_) { edMatMenu_ = 0; return; }
        if (edSel_.valid()) { edSel_ = EdSel(); edMulti_.clear(); return; }
        if (!edOnline_ && edDirty_ && edOfficial()) { edAskKeep(ED_THEN_LEAVE); return; }   // (B3)
        if (!edOnline_ && edDirty_ && time_ > edLeaveArmed_) { edLeaveArmed_ = time_ + 2; edNotify("unsaved changes - press ESC again to leave"); return; }
        if (edOnline_ && time_ > edLeaveArmed_) { edLeaveArmed_ = time_ + 2; edNotify("press ESC again to leave the session"); return; }
        closeEditor();
        return;
    }
    if (ctrl && key == GLFW_KEY_Z) { edUndoLast(); return; }
    if (key == GLFW_KEY_R && edSelValid()) { edTransform(ctrl ? ED_ROT_Z : shift ? ED_ROT_X : ED_ROT_Y); return; }
    if (key == GLFW_KEY_F && edSelValid()) { edTransform(ctrl ? ED_MIR_Y : shift ? ED_MIR_Z : ED_MIR_X); return; }
    if (ctrl && key == GLFW_KEY_S) {
        if (!edOnline_) edSave();
        else if (host_ && !(net_.hasSnapshot() && (net_.mode().flags & MS_TEMP_EDIT))) edNotify("your map is autosaved");
        else if (edOfficial()) edAskKeep(ED_THEN_SAVE);   // a built-in map's session: your copy gets a name of its own
        else edSaveCopy(ed_, edName_);
        return;
    }
    if (ctrl && key == GLFW_KEY_D) { edDuplicate(); return; }
    if (key == GLFW_KEY_LEFT_BRACKET) { edGridIdx_ = std::max(0, edGridIdx_ - 1); return; }
    if (key == GLFW_KEY_RIGHT_BRACKET) { edGridIdx_ = std::min(kGridCount - 1, edGridIdx_ + 1); return; }
    if (!edSelValid()) return;
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) { edDelete(); return; }
    int kind = edSel_.kind, i = edSel_.idx;
    if (key == GLFW_KEY_M) { // material / team
        edModify([&](Map& m) {
            if (kind == ED_BRUSH) edMat_ = m.brushes[i].material = nextMapMaterial(m.brushes[i].material);
            else if (kind == ED_GROUP && m.groups[i].type != Map::GROUP_CUSTOM) edMat_ = m.groups[i].material = nextMapMaterial(m.groups[i].material);
            else if (kind == ED_SPAWN) m.spawns[i].team = (m.spawns[i].team + 1) % 4;
        }, false);
        return;
    }
    if (key == GLFW_KEY_K) { edCoat(); return; }
    if (key == GLFW_KEY_N && kind == ED_BRUSH) {   // shape: box, wedge rising +u / -u / +v / -v, corner high at each corner, tetra at each corner, box (up kept: turn it with the rotate / mirror keys)
        edModify([&](Map& m) {
            Brush& b = m.brushes[i];
            const int k = b.shape == BRUSH_CUT ? 13 : (b.shape == BRUSH_BOX ? 0 : b.shape == BRUSH_WEDGE ? 1 + b.dir : b.shape == BRUSH_CORNER ? 5 + b.dir : 9 + b.dir) + 1;   // (a cut: back to a box)
            b.ncut = 0;
            b.shape = k == 13 ? BRUSH_BOX : k <= 4 ? BRUSH_WEDGE : k <= 8 ? BRUSH_CORNER : BRUSH_TETRA;
            b.dir = (uint8_t)(k == 13 ? 0 : (k - 1) % 4);
        }, false);
        const Brush& b = ed_.brushes[i];
        const SlopeFrame f = slopeFrame(b);
        static const char* ax = "XYZ";
        auto side = [&](int axis, bool plus) { return std::string(plus ? "+" : "-") + ax[axis]; };
        static const char* ups[kSlopeUps] = {"", ", upside down", ", on its side (+X)", ", on its side (-X)", ", on its side (+Z)", ", on its side (-Z)"};
        const bool cu = b.dir == 1 || b.dir == 2, cv = b.dir >= 2;
        edNotify(b.shape == BRUSH_BOX ? std::string("box")
                 : b.shape == BRUSH_WEDGE ? "wedge rising toward " + side(b.dir < 2 ? f.u : f.v, b.dir % 2 == 0) + ups[b.up]
                 : std::string(b.shape == BRUSH_TETRA ? "tetra" : "corner") + " high at " + side(f.u, cu) + " " + side(f.v, cv) + ups[b.up]);
        return;
    }
    if ((key == GLFW_KEY_Q || key == GLFW_KEY_E) && kind == ED_SPAWN && !edLook_) {
        edModify([&](Map& m) { m.spawns[i].yaw += key == GLFW_KEY_Q ? PI / 4 : -PI / 4; }, true);
        return;
    }
    // arrows move along the world axis closest to where you're looking; PgUp/PgDn move up/down.
    // Shift pushes that face of the box out, Ctrl pulls it in (lights: radius).
    Vec3 f = angleForward(yaw_, 0);
    int fa = std::fabs(f.x) > std::fabs(f.z) ? 0 : 2, ra = 2 - fa;
    float fs = f[fa] > 0 ? 1.0f : -1.0f;
    Vec3 rv = normalize(cross(f, {0, 1, 0}));
    float rs = rv[ra] > 0 ? 1.0f : -1.0f;
    int axis = -1;
    float sign = 0;
    if (key == GLFW_KEY_UP) { axis = fa; sign = fs; }
    else if (key == GLFW_KEY_DOWN) { axis = fa; sign = -fs; }
    else if (key == GLFW_KEY_RIGHT) { axis = ra; sign = rs; }
    else if (key == GLFW_KEY_LEFT) { axis = ra; sign = -rs; }
    else if (key == GLFW_KEY_PAGE_UP) { axis = 1; sign = 1; }
    else if (key == GLFW_KEY_PAGE_DOWN) { axis = 1; sign = -1; }
    if (axis < 0) return;
    edModify([&](Map& m) {
        if (shift || ctrl) {
            float amount = shift ? g : -g;
            if (kind == ED_LIGHT) { auto& l = m.lights[i]; l.radius = clampf(l.radius + (amount > 0 ? 1.0f : -1.0f), 1, 60); return; }
            AABB* b = kindBox(m, kind, i);
            if (!b) return;
            // (never thinner than a grid step - or than it is: CTRL+ARROW on a box already thinner than the grid grew it)
            float minSize = std::min(g, b->mx[axis] - b->mn[axis]);
            if (sign > 0) b->mx[axis] = std::max(b->mn[axis] + minSize, b->mx[axis] + amount);
            else b->mn[axis] = std::min(b->mx[axis] - minSize, b->mn[axis] - amount);
        } else {
            Vec3 d;
            d[axis] = sign * g;
            moveObject(m, kind, i, d);
        }
    }, true);
}

void Game::drawEditorSavePrompt() {
    const bool inEditor = screen_ == Screen::Editor;
    // (ui-redesign-spec: title at text 2, the body wrapped in grey at text 2, the name, then the answers; 528 wide in a
    // 560 panel. Title and body rebuilt when the map changes, not every frame)
    static std::string title, body, shownFor;
    {
        const std::string& nm = edPromptMap_.title.empty() ? edPromptMap_.name : edPromptMap_.title;
        if (shownFor.size() != nm.size() + 1 || shownFor.compare(0, nm.size(), nm) != 0 || shownFor.back() != (edPromptOfficial_ ? '1' : '0')) {
            shownFor = nm + (edPromptOfficial_ ? "1" : "0");
            std::string up = nm;
            for (auto& c : up) c = (char)toupper((unsigned char)c);
            title = edPromptOfficial_ ? "KEEP YOUR EDITS AS A NEW MAP?" : "SAVE YOUR COPY OF " + up + "?";
            body = edPromptOfficial_ ? up + " IS A BUILT-IN MAP: IT STAYS AS IT IS. SAVE YOUR EDITS AS YOUR OWN MAP (IN YOUR MAPS FOLDER, UNDER THIS NAME) OR DROP THEM."
                                     : "SAVES TO MAPS/<NAME>.QMAP - YOUR OWN COPY, THE HOST KEEPS THEIRS.";
        }
    }
    if (!inEditor) {   // after someone else's edit session (main/save-copy): the menus' dialog, centred on the screen
        // ("drop" left and "save" right)
        const SetLayout L = menuLayout(560);
        const float ts = ui_.textScale(), bw = L.L(528);
        const std::vector<std::string>& lines = ui_.wrap(body, ts, bw);
        const int nl = (int)lines.size();
        const float kH = 16 + 8 + nl * 20 - 4 + 12 + 36 + 12 + 36;
        float y = std::round(360 - (kH + 32) * 0.5f) + 16;
        const float x = L.X(376);
        ui_.panel(L.X(360), L.Y(y - 16), L.L(560), L.L(kH + 32), 0.94f);
        ui_.labelCentered(L.X(640), L.Y(y), title, kWhite, 1.0f, bw);
        y += 24;
        for (int i = 0; i < nl; i++) ui_.label(x, L.Y(y + 20 * i), lines[i], kGrey, 1.0f, bw);
        y += nl * 20 - 4 + 12;
        if (ui_.textField(42, x, L.Y(y), bw, L.L(36), edPromptName_, 40, UiExp::local().withId("COPY NAME"))) edPromptName_ = typedMapName(edPromptName_);
        y += 48;
        if (ui_.probing()) ui_.scope = "after a session";   // (as the editor's prompt: each "then" leads elsewhere)
        if (ui_.button(x, L.Y(y), L.L(260), L.L(36), edPromptOfficial_ && !edOnline_ ? "DROP MY EDITS" : "DON'T SAVE", UiExp::nav("main").withId("NO"))) edPromptAnswer(0);
        else if (ui_.button(L.X(644), L.Y(y), L.L(260), L.L(36), edPromptOfficial_ ? "SAVE AS NEW MAP" : "SAVE", UiExp::state("usermaps").withId("YES"))) edPromptAnswer(1);
        if (time_ < edMsgUntil_) ui_.hint(L.X(640) - r_.textWidth(edMsg_, ui_.hintScale()) * 0.5f, L.Y(y + 36 + 24), edMsg_, kGrey, L.L(560));
        return;
    }
    // in the editor: the editor's dialog template (drawEditorGroupMenu), centred in the 3D view over its key bar; CANCEL
    // first when there's something to go back to, then "drop", then "save"
    const float s = ui_.s(), ts = ui_.textScale(), pad = 16 * s, pw = 560 * s, w = pw - 2 * pad, h = 36 * s;
    const std::vector<std::string>& lines = ui_.wrap(body, ts, w);
    const int nl = (int)lines.size();
    float px, py;
    edDialogPanel(ui_, edPanelX_, edBarTop_ > 0 ? edBarTop_ : (float)r_.height(), pw, (40 + nl * 20 - 4 + 12 + 36 + 12 + 36 + 16) * s, px, py);
    const float x = px + pad, cx = px + pw * 0.5f;
    float y = py + pad;
    ui_.labelCentered(cx, y, title, kWhite, 1.0f, w);
    y += 24 * s;
    for (int i = 0; i < nl; i++) ui_.label(x, y + 20 * s * i, lines[i], kGrey, 1.0f, w);
    y += (nl * 20 - 4 + 12) * s;
    if (ui_.textField(42, x, y, w, h, edPromptName_, 40, UiExp::local().withId("COPY NAME"))) edPromptName_ = typedMapName(edPromptName_);
    y += h + 12 * s;
    const bool cancel = edPromptThen_ != ED_THEN_NONE;   // (something to go back to)
    const int nb = cancel ? 3 : 2;
    const float bgap = 8 * s, bw = (w - (nb - 1) * bgap) / nb;
    // (what each answer leads to, for the UI test: yes writes the new map - your own session carries on under it -,
    // no / cancel go where the question was asked from)
    const int then = edPromptThen_;
    static const char* kThen[] = {"after a session", "save", "leave", "test", "host", "open"};
    if (ui_.probing()) ui_.scope = kThen[std::min(std::max(edPromptThen_, 0), 5)];   // (the UI test: each "then" leads elsewhere)
    const UiExp yes = then == ED_THEN_SAVE && edOnline_ && host_ ? UiExp::cmd("editmode saveas") : UiExp::state("usermaps");
    const UiExp no = then == ED_THEN_LEAVE ? UiExp::nav("main") : then == ED_THEN_TEST || then == ED_THEN_HOST ? UiExp::state("hosting", "1")
                   : then == ED_THEN_OPEN ? UiExp::state("edopen") : UiExp::nav(edPath());
    float bx = x;
    if (cancel) {
        if (ui_.button(bx, y, bw, h, "CANCEL", UiExp::nav(edPath()))) { edPromptAnswer(-1); return; }
        bx += bw + bgap;
    }
    if (ui_.button(bx, y, bw, h, edPromptOfficial_ && !edOnline_ ? "DROP MY EDITS" : "DON'T SAVE", no.withId("NO"))) { edPromptAnswer(0); return; }
    if (ui_.button(bx + bw + bgap, y, bw, h, edPromptOfficial_ ? "SAVE AS NEW MAP" : "SAVE", yes.withId("YES"))) { edPromptAnswer(1); return; }
    // (why a save was refused - the question stays up - under the panel)
    if (time_ < edMsgUntil_) ui_.hintCentered(cx, py + (40 + nl * 20 - 4 + 12 + 36 + 12 + 36 + 16 + 8) * s, edMsg_, kGrey, edPanelX_ - 32 * s);
}

void Game::editorFrame(double dt) {
    const float W = (float)r_.width(), H = (float)r_.height();
    const float s = H / 720.0f, g = kGrids[edGridIdx_];
    const float panelW = 320 * s, panelX = W - panelW;
    r_.cullPad = 0;   // (the match's late-latch pad stays set after leaving it: the editor's camera isn't late-latched)
    if (edOnline_) {
        // follow the session's map; keep the connection fed and tell the others where our camera is
        if (net_.mapRevision() != edRev_) {
            const Map& nm = net_.map();
            // only a change to the boxes rebuilds the static batch (a remote title / spawn / light edit doesn't)
            if (!sameBrushes(ed_.brushes, nm.brushes)) edMeshDirty_ = true;
            // a remote insert / delete moves the indices after it: the selection follows its object (or is dropped) and
            // a drag ends, so a stale index never edits another object. (A selection past the end is our own new
            // object, not here yet: it's kept.)
            size_t had[9], has[9];
            for (int k = 0; k < 9; k++) { had[k] = kindCount(ed_, k); has[k] = kindCount(nm, k); }
            auto follow = [&](EdSel& e) {   // false: it's gone
                if (!e.valid() || (size_t)e.idx >= had[e.kind] || had[e.kind] == has[e.kind]) return true;
                const char k = kKindChar[e.kind];
                const std::string want = objectKey(ed_, k, e.idx);
                const AABB box = edBounds(ed_, e);
                for (int d = 0; d <= (int)std::max(had[e.kind], has[e.kind]); d++)   // nearest first (one insert / delete before it: one step)
                    for (int j : {e.idx - d, e.idx + d})
                        if (j >= 0 && (size_t)j < has[e.kind] && sameBox(edBounds(nm, {e.kind, j}), box) && objectKey(nm, k, j) == want) { e.idx = j; return true; }
                return false;
            };
            if (edSel_.valid() && had[edSel_.kind] != has[edSel_.kind]) edDragAxis_ = -1;
            if (!follow(edSel_)) edSel_ = EdSel();
            size_t kept = 0;
            for (auto& e : edMulti_) if (follow(e)) edMulti_[kept++] = e;
            edMulti_.resize(kept);
            ed_ = nm;
            edRev_ = net_.mapRevision();
            edGen_++;
            if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); edMeshDirty_ = true; }
        }
        net_.takeEvents(evScratch_);
        for (auto& e : evScratch_) {
            if (e.type == EV_MESSAGE) edNotify(e.text);
            else if (e.type == EV_CHAT) edNotify(net_.playerName(e.a) + ": " + e.text);
        }
        edTickAcc_ += dt;
        if (edTickAcc_ >= 1.0 / 30) {
            edTickAcc_ = 0;
            UserCmd c;
            c.yaw = yaw_; c.pitch = pitch_;
            net_.tick(c);
            net_.flushInput();
            framePlayersOk_ = false;
        }
        if (time_ >= edNextCam_ && net_.pendingCommands() < 8) {
            char buf[96];
            snprintf(buf, sizeof buf, "edcam %.2f %.2f %.2f", edCam_.x, edCam_.y - kEyeHeight, edCam_.z);
            net_.sendCommand(buf);
            edNextCam_ = time_ + 0.15;
        }
    }
    // the save question, or the material picker (it fills the window), when it's up, is the only thing the mouse and
    // typing reach (drawn last, over everything)
    const bool matModal = edMatMenu_ && !edGroupMenu_ && !edExportAsk_;
    const bool modal = edPrompt_ || matModal;
    UiInput realIn;   // (copied only when one is up: it holds the typed text)
    if (modal) { realIn = uiIn_; uiIn_.pressed = uiIn_.rpressed = uiIn_.down = false; uiIn_.chars.clear(); uiIn_.backspace = uiIn_.enter = uiIn_.tab = false; }
    // ---- fly camera (right mouse held to look; WASD, Space up, C down, Shift fast, wheel = speed)
    if (!ui_.typing()) {
        Vec3 f = angleForward(yaw_, pitch_), right = normalize(cross(angleForward(yaw_, 0), {0, 1, 0}));
        bool ctrlHeld = glfwGetKey(win_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        Vec3 mv;
        if (glfwGetKey(win_, GLFW_KEY_W) == GLFW_PRESS) mv += f;
        if (glfwGetKey(win_, GLFW_KEY_S) == GLFW_PRESS && !ctrlHeld) mv -= f;
        if (glfwGetKey(win_, GLFW_KEY_D) == GLFW_PRESS && !ctrlHeld) mv += right;
        if (glfwGetKey(win_, GLFW_KEY_A) == GLFW_PRESS) mv -= right;
        if (glfwGetKey(win_, GLFW_KEY_SPACE) == GLFW_PRESS) mv.y += 1;
        if (glfwGetKey(win_, GLFW_KEY_C) == GLFW_PRESS) mv.y -= 1;
        if (edLook_ && glfwGetKey(win_, GLFW_KEY_E) == GLFW_PRESS) mv.y += 1;
        if (edLook_ && glfwGetKey(win_, GLFW_KEY_Q) == GLFW_PRESS) mv.y -= 1;
        float sp = edSpeed_ * (glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 3.0f : 1.0f);
        if (length(mv) > 0) edCam_ += normalize(mv) * sp * (float)dt;
    }
    // ---- the 3D view covers the screen left of the panel (so its center, where things get added, is
    // the middle of what you see)
    const int fullW = r_.width();
    edFullW_ = fullW;
    edPanelX_ = panelX;
    r_.resize((int)panelX, (int)H);
    r_.setCamera(edCam_, yaw_, pitch_, cfg_.fov);
    if (ui_.probing()) uiTestHandles();   // (the UI test: where the selection's handles are on screen)
    // param along the axis nearest the mouse ray; ahead: false when that nearest point of the ray is behind the camera
    // (the mouse is past the axis's vanishing point - a handle on a far face dragged toward the horizon)
    auto closestOnAxis = [&](const Vec3& p0, int axis, const Vec3& d, bool* ahead = nullptr) {
        Vec3 a;
        a[axis] = 1;
        Vec3 w = p0 - edCam_;
        float bb = dot(a, d), den = 1 - bb * bb;
        float t = den < 1e-4f ? 0.0f : (bb * dot(d, w) - dot(a, w)) / den;
        if (ahead) *ahead = den >= 1e-4f && dot(d, w) + bb * t > 0;
        return t;
    };
    auto handles = [&]() -> const std::vector<EdHandle>& {   // edHandles(), into a buffer kept from frame to frame
        edHandleBuf_.clear();
        if (edSelValid()) handlesOf(edBounds(ed_, edSel_), edSel_.kind == ED_SPAWN || edSel_.kind == ED_LIGHT || edSel_.kind == ED_PATH, edHandleBuf_);
        return edHandleBuf_;
    };
    // what only changes with the map (each O(boxes) a frame on a big one): the clip boxes, the FIND CLIP count, the
    // modes it can be / is played as. Worked out again when edGen_ moves on (asked for before each use, so an edit
    // made earlier in the frame shows in the same frame)
    auto mapCache = [&] {
        if (edCacheGen_ == edGen_) return;
        edCacheGen_ = edGen_;
        edClipBoxes_.clear();
        edClipCount_ = 0;
        for (auto& b : ed_.brushes) if (b.material == 9) { edClipBoxes_.push_back(b.box); edClipCount_ += !b.generated; }
        edCanModes_ = 0;
        edModesText_.clear();
        for (int m = 0; m < MODE_COUNT; m++) {
            if (ed_.canSupportMode(m)) edCanModes_ |= 1u << m;
            if (ed_.supportsMode(m)) edModesText_ += std::string(edModesText_.empty() ? "" : " ") + modeName(m);
        }
    };
    // ---- click: grab a handle of the selection, else select what's under the mouse
    if (uiIn_.pressed && uiIn_.mx < panelX && !edGroupMenu_ && !edExportAsk_ && !edPrompt_ && !edMatMenu_) {
        ui_.clearFocus();
        bool shiftHeld = glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        // the spot the last selecting click was on, same view: this click steps along the ray (below), even where
        // the selection's handle now sits under the mouse
        bool again = !shiftHeld && std::hypot(uiIn_.mx - edLastClickX_, uiIn_.my - edLastClickY_) < 4 * s && length(edCam_ - edLastClickCam_) < 1e-4f &&
                     std::fabs(yaw_ - edLastClickYaw_) < 1e-5f && std::fabs(pitch_ - edLastClickPitch_) < 1e-5f;
        int best = -1;
        float bestD = 14 * s;
        const auto& hs = handles();
        std::vector<float> hd(hs.size(), 1e9f);
        for (size_t k = 0; k < hs.size(); k++) {
            float sx, sy;
            if (!r_.project(hs[k].pos, sx, sy)) continue;
            hd[k] = std::hypot(sx - uiIn_.mx, sy - uiIn_.my);
            if (hd[k] < bestD) { bestD = hd[k]; best = (int)k; }
        }
        // handles that overlap on screen (a near-axis view lines a far face's handle up behind a near one): the
        // one nearest the camera wins, as it's the one drawn on top
        if (best >= 0)
            for (size_t k = 0; k < hs.size(); k++)
                if (hd[k] < 14 * s && hd[k] <= bestD + 8 * s && length(hs[k].pos - edCam_) < length(hs[best].pos - edCam_)) best = (int)k;
        if (best >= 0 && !edLook_ && !again) {
            edDragAxis_ = hs[best].axis;
            edDragSign_ = hs[best].sign;
            edDragMove_ = hs[best].move || glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            edDragP0_ = hs[best].pos;
            edDragT0_ = closestOnAxis(edDragP0_, edDragAxis_, edMouseRay());
            edDragBase_ = copyNoSource(ed_);
            edDragSel_ = edSel_;
            edDragBaseOp_ = describeMapObject(edDragBase_, kKindChar[edSel_.kind], "set", edSel_.idx);   // (what undo puts back)
            edDragGrid_ = 0;   // (worked out on the first frame)
            edDragLast_.clear();
            edDragPushed_ = false;
        } else { // shift+click adds boxes / groups to the selection (or takes one back out)
            // a click on the spot clicked last (same view) steps to the next thing along the ray: what's behind a
            // see-through clip box or zone, or the clip / zone behind a wall, or the volume you're inside
            auto hitsAll = edPickAll(edCam_, edMouseRay());
            EdSel p = hitsAll.empty() ? EdSel() : hitsAll[0];
            if (again && hitsAll.size() > 1)
                for (size_t k = 0; k < hitsAll.size(); k++)
                    if (hitsAll[k].kind == edSel_.kind && hitsAll[k].idx == edSel_.idx) { p = hitsAll[(k + 1) % hitsAll.size()]; break; }
            edLastClickX_ = uiIn_.mx; edLastClickY_ = uiIn_.my; edLastClickCam_ = edCam_; edLastClickYaw_ = yaw_; edLastClickPitch_ = pitch_;
            auto multi = [](EdSel e) { return e.kind == ED_BRUSH || e.kind == ED_GROUP; };
            auto same = [&](EdSel e) { return e.kind == p.kind && e.idx == p.idx; };
            if (shiftHeld && multi(p) && edSelValid() && multi(edSel_)) {
                auto it = std::find_if(edMulti_.begin(), edMulti_.end(), same);
                if (it != edMulti_.end()) edMulti_.erase(it);
                else if (same(edSel_)) { edSel_ = edMulti_.empty() ? EdSel() : edMulti_.back(); if (!edMulti_.empty()) edMulti_.pop_back(); }
                else { edMulti_.push_back(edSel_); edSel_ = p; }
            } else {
                edSel_ = p;
                edMulti_.clear();
            }
        }
    }
    if (edDragAxis_ >= 0 && (!uiIn_.down || !edSelValid())) edDragAxis_ = -1;
    // (past the vanishing point the nearest param jumps to the axis's other end - a face pulled out flipped in, or
    // stuck at its smallest: UI test, a group's far face - so the drag holds where it was until the mouse comes back)
    bool dragAhead = true;
    float dragT = edDragAxis_ >= 0 ? closestOnAxis(edDragP0_, edDragAxis_, edMouseRay(), &dragAhead) : 0;
    if (edDragAxis_ >= 0 && dragAhead && (size_t)edSel_.idx < kindCount(edDragBase_, edSel_.kind)) {
        float delta = snapTo(dragT - edDragT0_, g);
        delta = clampf(delta, -500, 500);
        int kind = edSel_.kind, i = edSel_.idx, a = edDragAxis_;
        char k = kKindChar[kind];
        if (kind != edDragSel_.kind || i != edDragSel_.idx) { edDragSel_ = edSel_; edDragBaseOp_ = describeMapObject(edDragBase_, k, "set", i); edDragGrid_ = 0; }
        // (the same step on the same grid as last frame: the same op, already applied or not needed - held still, nothing to do)
        if (delta != edDragDelta_ || g != edDragGrid_) {
            edDragDelta_ = delta;
            edDragGrid_ = g;
            // the object's box / position changed on the base map in place, then put back as it was (copying the whole map
            // every frame was 80-200 us on a big one)
            AABB* b = kindBox(edDragBase_, kind, i);
            Vec3* p = kindPoint(edDragBase_, kind, i);
            const AABB keepBox = b ? *b : AABB();
            const Vec3 keepPos = p ? *p : Vec3();
            if (edDragMove_) {
                Vec3 d;
                d[a] = delta;
                moveObject(edDragBase_, kind, i, d);
            } else if (b) {
                if (edDragSign_ > 0) b->mx[a] = std::max(b->mn[a] + g, b->mx[a] + delta);
                else b->mn[a] = std::min(b->mx[a] - g, b->mn[a] + delta);
            }
            std::string op = describeMapObject(edDragBase_, k, "set", i);
            if (b) *b = keepBox;
            if (p) *p = keepPos;
            if (op != edDragLast_ && !(edDragLast_.empty() && op == edDragBaseOp_)) {
                if (!edDragPushed_) { edUndo_.push_back(edDragBaseOp_); edDragPushed_ = true; }
                edDo(op, "", false, false);
                edDragLast_ = op;
            }
        }
    }
    if (edSel_.valid() && !edSelValid() && !edOnline_) edSel_ = EdSel(); // (online: a new object may not have arrived yet)
    if (!edOnline_) edMulti_.erase(std::remove_if(edMulti_.begin(), edMulti_.end(), [&](EdSel e) { return (size_t)e.idx >= kindCount(ed_, e.kind); }), edMulti_.end());

    // ---- 3D view
    static const Vec3 kNewMatColors[] = {{0.70f, 0.85f, 0.95f}, {0.80f, 0.70f, 0.48f}, {0.55f, 0.36f, 0.20f}, {0.85f, 0.85f, 0.88f},
                                         {0.30f, 0.58f, 0.20f}, {0.12f, 0.12f, 0.13f}, {0.62f, 0.64f, 0.68f}, {0.50f, 0.52f, 0.55f}, {0.62f, 0.24f, 0.17f},
                                         {0.85f, 0.90f, 0.92f}, {0.20f, 0.20f, 0.21f}, {0.32f, 0.22f, 0.13f}, {0.50f, 0.48f, 0.45f}, {0.58f, 0.58f, 0.56f},
                                         {0.88f, 0.86f, 0.80f}, {0.88f, 0.87f, 0.85f}, {0.55f, 0.55f, 0.58f}, {0.90f, 0.72f, 0.08f}, {0.93f, 0.95f, 1.00f},
                                         {0.60f, 0.58f, 0.54f}, {0.35f, 0.40f, 0.75f}, {0.08f, 0.30f, 0.16f}, {0.52f, 0.33f, 0.20f}, {0.75f, 0.38f, 0.20f},
                                         {0.62f, 0.24f, 0.17f}, {0.85f, 0.90f, 0.92f}, {0.50f, 0.48f, 0.45f},
                                         {0.67f, 0.60f, 0.42f},
                                         {0.43f, 0.13f, 0.11f},
                                         {0.18f, 0.20f, 0.24f},
                                         {0.75f, 0.44f, 0.27f},
                                         {0.35f, 0.22f, 0.23f},
                                         {0.73f, 0.66f, 0.64f},
                                         {0.18f, 0.19f, 0.20f},
                                         {0.73f, 0.69f, 0.57f},
                                         {0.56f, 0.26f, 0.21f},
                                         {0.59f, 0.29f, 0.22f},
                                         {0.60f, 0.30f, 0.23f},
                                         {0.54f, 0.25f, 0.20f},
                                         {0.56f, 0.27f, 0.20f},
                                         {0.58f, 0.29f, 0.21f},
                                         {0.68f, 0.52f, 0.40f},
                                         {0.58f, 0.31f, 0.23f},
                                         {0.25f, 0.44f, 0.34f},
                                         {0.77f, 0.77f, 0.77f},
                                         {0.45f, 0.24f, 0.14f},
                                         {0.56f, 0.29f, 0.24f},
                                         {0.55f, 0.36f, 0.29f},
                                         {0.38f, 0.25f, 0.21f},
                                         {0.76f, 0.57f, 0.53f},
                                         {0.50f, 0.20f, 0.14f},
                                         {0.63f, 0.45f, 0.39f},
                                         {0.33f, 0.20f, 0.18f},
                                         {0.55f, 0.31f, 0.23f},
                                         {0.53f, 0.27f, 0.21f},
                                         {0.48f, 0.11f, 0.11f},
                                         {0.75f, 0.66f, 0.49f},
                                         {0.61f, 0.61f, 0.60f},
                                         {0.25f, 0.25f, 0.27f},
                                         {0.77f, 0.77f, 0.76f},
                                         {0.53f, 0.25f, 0.22f},
                                         {0.53f, 0.50f, 0.46f},
                                         {0.58f, 0.57f, 0.55f},
                                         {0.55f, 0.49f, 0.42f},
                                         {0.55f, 0.55f, 0.54f},
                                         {0.60f, 0.59f, 0.58f},
                                         {0.58f, 0.56f, 0.55f},
                                         {0.61f, 0.60f, 0.57f},
                                         {0.52f, 0.53f, 0.56f},
                                         {0.53f, 0.52f, 0.51f},
                                         {0.57f, 0.56f, 0.55f},
                                         {0.68f, 0.67f, 0.65f},
                                         {0.60f, 0.57f, 0.53f},
                                         {0.58f, 0.57f, 0.55f},
                                         {0.38f, 0.38f, 0.40f},
                                         {0.76f, 0.76f, 0.74f},
                                         {0.52f, 0.55f, 0.59f},
                                         {0.79f, 0.80f, 0.80f},
                                         {0.13f, 0.13f, 0.14f},
                                         {0.46f, 0.46f, 0.47f},
                                         {0.77f, 0.77f, 0.77f},
                                         {0.43f, 0.59f, 0.48f},
                                         {0.68f, 0.67f, 0.67f},
                                         {0.78f, 0.78f, 0.78f},
                                         {0.67f, 0.36f, 0.20f},
                                         {0.76f, 0.74f, 0.71f},
                                         {0.61f, 0.44f, 0.29f},
                                         {0.33f, 0.61f, 0.67f},
                                         {0.55f, 0.55f, 0.56f},
                                         {0.22f, 0.45f, 0.70f},
                                         {0.16f, 0.43f, 0.41f},
                                         {0.67f, 0.67f, 0.68f},
                                         {0.58f, 0.22f, 0.15f},
                                         {0.20f, 0.22f, 0.26f},
                                         {0.69f, 0.64f, 0.55f},
                                         {0.56f, 0.49f, 0.42f},
                                         {0.61f, 0.71f, 0.67f},
                                         {0.12f, 0.11f, 0.12f},
                                         {0.71f, 0.78f, 0.78f},
                                         {0.55f, 0.46f, 0.44f},
                                         {0.78f, 0.76f, 0.69f},
                                         {0.68f, 0.70f, 0.73f},
                                         {0.75f, 0.71f, 0.63f},
                                         {0.68f, 0.54f, 0.37f},
                                         {0.58f, 0.27f, 0.20f},
                                         {0.52f, 0.51f, 0.52f},
                                         {0.67f, 0.49f, 0.46f},
                                         {0.15f, 0.16f, 0.17f},
                                         {0.58f, 0.57f, 0.55f},
                                         {0.67f, 0.61f, 0.51f},
                                         {0.55f, 0.52f, 0.49f},
                                         {0.47f, 0.45f, 0.44f},
                                         {0.30f, 0.29f, 0.30f},
                                         {0.65f, 0.62f, 0.57f},
                                         {0.57f, 0.53f, 0.47f},
                                         {0.52f, 0.51f, 0.49f},
                                         {0.74f, 0.64f, 0.41f},
                                         {0.79f, 0.79f, 0.79f},
                                         {0.59f, 0.58f, 0.54f},
                                         {0.44f, 0.43f, 0.43f},
                                         {0.44f, 0.42f, 0.41f},
                                         {0.50f, 0.47f, 0.44f},
                                         {0.19f, 0.21f, 0.25f},
                                         {0.54f, 0.52f, 0.51f},
                                         {0.15f, 0.17f, 0.16f},
                                         {0.73f, 0.69f, 0.54f},
                                         {0.46f, 0.50f, 0.41f},
                                         {0.38f, 0.29f, 0.24f},
                                         {0.47f, 0.44f, 0.40f},
                                         {0.49f, 0.49f, 0.48f},
                                         {0.74f, 0.58f, 0.40f},
                                         {0.26f, 0.29f, 0.33f},
                                         {0.22f, 0.23f, 0.24f},
                                         {0.52f, 0.51f, 0.49f},
                                         {0.65f, 0.56f, 0.43f},
                                         {0.63f, 0.35f, 0.26f},
                                         {0.20f, 0.18f, 0.22f},
                                         {0.43f, 0.41f, 0.40f},
                                         {0.36f, 0.29f, 0.24f},
                                         {0.22f, 0.37f, 0.21f},
                                         {0.35f, 0.26f, 0.21f},
                                         {0.82f, 0.56f, 0.44f},
                                         {0.76f, 0.74f, 0.67f},
                                         {0.80f, 0.79f, 0.78f},
                                         {0.22f, 0.29f, 0.20f},
                                         {0.73f, 0.77f, 0.81f},
                                         {0.47f, 0.43f, 0.40f},
                                         {0.52f, 0.51f, 0.49f},
                                         {0.80f, 0.56f, 0.20f},
                                         {0.35f, 0.60f, 0.46f},
                                         {0.76f, 0.74f, 0.68f},
                                         {0.69f, 0.52f, 0.25f},
                                         {0.78f, 0.77f, 0.74f},
                                         {0.64f, 0.69f, 0.62f},
                                         {0.75f, 0.74f, 0.71f},
                                         {0.56f, 0.38f, 0.26f},
                                         {0.67f, 0.63f, 0.57f},
                                         {0.76f, 0.75f, 0.73f},
                                         {0.32f, 0.35f, 0.44f},
                                         {0.63f, 0.71f, 0.65f},
                                         {0.76f, 0.74f, 0.73f},
                                         {0.54f, 0.54f, 0.55f},
                                         {0.27f, 0.26f, 0.28f},
                                         {0.23f, 0.35f, 0.32f},
                                         {0.60f, 0.50f, 0.29f},
                                         {0.48f, 0.33f, 0.24f},
                                         {0.60f, 0.63f, 0.33f},
                                         {0.61f, 0.60f, 0.41f},
                                         {0.77f, 0.75f, 0.72f},
                                         {0.73f, 0.73f, 0.73f},
                                         {0.62f, 0.62f, 0.61f},
                                         {0.40f, 0.36f, 0.38f},
                                         {0.65f, 0.49f, 0.34f},
                                         {0.51f, 0.52f, 0.55f},
                                         {0.43f, 0.46f, 0.51f},
                                         {0.30f, 0.39f, 0.59f},
                                         {0.62f, 0.64f, 0.67f},
                                         {0.47f, 0.55f, 0.45f},
                                         {0.75f, 0.64f, 0.30f},
                                         {0.84f, 0.73f, 0.26f},
                                         {0.26f, 0.27f, 0.30f},
                                         {0.68f, 0.70f, 0.73f},
                                         {0.19f, 0.19f, 0.21f},
                                         {0.20f, 0.21f, 0.23f},
                                         {0.69f, 0.31f, 0.20f},
                                         {0.56f, 0.56f, 0.56f},
                                         {0.36f, 0.37f, 0.39f},
                                         {0.62f, 0.54f, 0.41f},
                                         {0.22f, 0.28f, 0.29f},
                                         {0.49f, 0.44f, 0.75f},
                                         {0.20f, 0.21f, 0.21f},
                                         {0.20f, 0.27f, 0.24f},
                                         {0.51f, 0.25f, 0.42f},
                                         {0.20f, 0.20f, 0.25f},
                                         {0.75f, 0.76f, 0.78f},
                                         {0.66f, 0.57f, 0.35f},
                                         {0.18f, 0.43f, 0.25f},
                                         {0.33f, 0.20f, 0.20f},
                                         {0.77f, 0.64f, 0.43f},
                                         {0.25f, 0.62f, 0.24f},
                                         {0.63f, 0.49f, 0.33f},
                                         {0.75f, 0.75f, 0.73f},
                                         {0.30f, 0.31f, 0.24f},
                                         {0.30f, 0.25f, 0.22f},
                                         {0.32f, 0.33f, 0.36f},
                                         {0.80f, 0.78f, 0.78f},
                                         {0.69f, 0.65f, 0.56f},
                                         {0.67f, 0.67f, 0.65f},
                                         {0.31f, 0.30f, 0.30f},
                                         {0.70f, 0.40f, 0.26f},
                                         {0.81f, 0.80f, 0.80f},
                                         {0.45f, 0.45f, 0.45f},
                                         {0.22f, 0.54f, 0.36f},
                                         {0.59f, 0.52f, 0.63f},
                                         {0.31f, 0.29f, 0.29f},
                                         {0.46f, 0.42f, 0.40f},
                                         {0.47f, 0.46f, 0.45f},
                                         {0.66f, 0.36f, 0.25f},
                                         {0.49f, 0.48f, 0.47f},
                                         {0.64f, 0.48f, 0.40f},
                                         {0.67f, 0.44f, 0.28f},
                                         {0.65f, 0.58f, 0.46f},
                                         {0.54f, 0.53f, 0.53f},
                                         {0.80f, 0.80f, 0.84f},
                                         {0.27f, 0.23f, 0.21f},
                                         {0.22f, 0.20f, 0.20f},
                                         {0.69f, 0.61f, 0.45f},
                                         {0.46f, 0.49f, 0.52f},
                                         {0.46f, 0.37f, 0.29f},
                                         {0.45f, 0.44f, 0.44f},
                                         {0.51f, 0.49f, 0.46f},
                                         {0.72f, 0.53f, 0.25f},
                                         {0.64f, 0.32f, 0.24f},
                                         {0.31f, 0.25f, 0.22f},
                                         {0.66f, 0.42f, 0.27f},
                                         {0.45f, 0.45f, 0.45f},
                                         {0.40f, 0.40f, 0.42f},
                                         {0.65f, 0.38f, 0.21f},
                                         {0.20f, 0.21f, 0.22f},
                                         {0.24f, 0.43f, 0.22f},
                                         {0.33f, 0.56f, 0.25f},
                                         {0.24f, 0.35f, 0.21f},
                                         {0.34f, 0.54f, 0.21f},
                                         {0.20f, 0.35f, 0.21f},
                                         {0.53f, 0.33f, 0.25f},
                                         {0.77f, 0.76f, 0.75f},
                                         {0.46f, 0.36f, 0.26f},
                                         {0.58f, 0.46f, 0.33f},
                                         {0.21f, 0.33f, 0.24f},
                                         {0.42f, 0.40f, 0.24f},
                                         {0.24f, 0.44f, 0.26f},
                                         {0.71f, 0.63f, 0.36f},
                                         {0.51f, 0.51f, 0.49f},
                                         {0.71f, 0.65f, 0.57f},
                                         {0.72f, 0.22f, 0.23f},
                                         {0.28f, 0.28f, 0.20f},
                                         {0.19f, 0.37f, 0.22f},
                                         {0.73f, 0.65f, 0.37f},
                                         {0.19f, 0.27f, 0.21f},
                                         {0.56f, 0.78f, 0.85f},
                                         {0.78f, 0.80f, 0.82f},
                                         {0.55f, 0.60f, 0.67f},
                                         {0.71f, 0.67f, 0.66f},
                                         {0.33f, 0.33f, 0.36f},
                                         {0.26f, 0.31f, 0.38f},
                                         {0.57f, 0.67f, 0.69f},
                                         {0.56f, 0.69f, 0.78f},
                                         {0.44f, 0.37f, 0.32f},
                                         {0.37f, 0.37f, 0.40f},
                                         {0.26f, 0.51f, 0.23f},
                                         {0.58f, 0.71f, 0.80f},
                                         {0.37f, 0.32f, 0.29f},
                                         {0.83f, 0.84f, 0.87f},
                                         {0.43f, 0.76f, 0.87f},
                                         {0.73f, 0.46f, 0.27f},
                                         {0.45f, 0.35f, 0.29f},
                                         {0.79f, 0.76f, 0.33f},
                                         {0.21f, 0.21f, 0.22f},
                                         {0.28f, 0.20f, 0.20f},
                                         {0.80f, 0.75f, 0.55f},
                                         {0.85f, 0.73f, 0.71f},
                                         {0.64f, 0.58f, 0.58f},
                                         {0.66f, 0.60f, 0.47f},
                                         {0.47f, 0.33f, 0.34f},
                                         {0.72f, 0.66f, 0.61f},
                                         {0.79f, 0.75f, 0.65f},
                                         {0.77f, 0.75f, 0.70f},
                                         {0.26f, 0.22f, 0.20f},
                                         {0.78f, 0.64f, 0.45f},
                                         {0.36f, 0.37f, 0.38f},
                                         {0.20f, 0.21f, 0.27f},
                                         {0.73f, 0.73f, 0.75f},
                                         {0.76f, 0.74f, 0.64f},
                                         {0.71f, 0.64f, 0.52f},
                                         {0.20f, 0.20f, 0.22f},
                                         {0.19f, 0.26f, 0.29f},
                                         {0.80f, 0.82f, 0.84f},
                                         {0.33f, 0.33f, 0.33f},
                                         {0.63f, 0.50f, 0.36f},
                                         {0.44f, 0.40f, 0.25f},
                                         {0.45f, 0.35f, 0.26f},
                                         {0.49f, 0.41f, 0.24f},
                                         {0.32f, 0.38f, 0.54f},
                                         {0.58f, 0.56f, 0.52f},
                                         {0.16f, 0.15f, 0.30f},
                                         {0.41f, 0.62f, 0.78f},
                                         {0.28f, 0.29f, 0.31f},
                                         {0.16f, 0.16f, 0.18f},
                                         {0.55f, 0.34f, 0.16f},
                                         {0.50f, 0.51f, 0.54f},
                                         {0.61f, 0.65f, 0.69f},
                                         {0.11f, 0.11f, 0.12f},
                                         {0.16f, 0.17f, 0.21f},
                                         {0.74f, 0.39f, 0.20f},
                                         {0.62f, 0.63f, 0.67f},
                                         {0.23f, 0.24f, 0.27f},
                                         {0.18f, 0.35f, 0.23f},
                                         {0.81f, 0.67f, 0.12f},
                                         {0.70f, 0.67f, 0.62f},
                                         {0.72f, 0.73f, 0.75f},
                                         {0.30f, 0.31f, 0.35f},
                                         {0.45f, 0.46f, 0.49f},
                                         {0.64f, 0.65f, 0.69f},
                                         {0.38f, 0.39f, 0.43f},
                                         {0.37f, 0.40f, 0.44f},
                                         {0.52f, 0.53f, 0.57f},
                                         {0.39f, 0.40f, 0.44f},
                                         {0.23f, 0.26f, 0.32f},
                                         {0.18f, 0.17f, 0.17f},
                                         {0.41f, 0.48f, 0.54f},
                                         {0.52f, 0.53f, 0.57f},
                                         {0.26f, 0.24f, 0.22f},
                                         {0.31f, 0.41f, 0.50f},
                                         {0.82f, 0.82f, 0.83f},
                                         {0.64f, 0.65f, 0.62f},
                                         {0.27f, 0.32f, 0.35f},
                                         {0.57f, 0.58f, 0.62f},
                                         {0.38f, 0.40f, 0.44f},
                                         {0.26f, 0.29f, 0.19f},
                                         {0.67f, 0.69f, 0.71f},
                                         {0.22f, 0.23f, 0.25f},
                                         {0.75f, 0.73f, 0.71f},
                                         {0.20f, 0.20f, 0.22f},
                                         {0.34f, 0.35f, 0.37f},
                                         {0.14f, 0.30f, 0.17f},
                                         {0.48f, 0.51f, 0.55f},
                                         {0.51f, 0.53f, 0.58f},
                                         {0.12f, 0.13f, 0.14f},
                                         {0.62f, 0.63f, 0.64f},
                                         {0.48f, 0.47f, 0.47f},
                                         {0.56f, 0.55f, 0.54f},
                                         {0.34f, 0.35f, 0.39f},
                                         {0.40f, 0.42f, 0.45f},
                                         {0.51f, 0.17f, 0.14f},
                                         {0.29f, 0.29f, 0.21f},
                                         {0.61f, 0.58f, 0.58f},
                                         {0.42f, 0.19f, 0.13f},
                                         {0.35f, 0.16f, 0.13f},
                                         {0.33f, 0.34f, 0.36f},
                                         {0.47f, 0.49f, 0.53f},
                                         {0.57f, 0.58f, 0.61f},
                                         {0.56f, 0.56f, 0.55f},
                                         {0.58f, 0.57f, 0.56f},
                                         {0.78f, 0.65f, 0.15f},
                                         {0.56f, 0.56f, 0.58f},
                                         {0.44f, 0.47f, 0.51f},
                                         {0.45f, 0.42f, 0.41f},
                                         {0.77f, 0.69f, 0.12f},
                                         {0.58f, 0.61f, 0.64f},
                                         {0.26f, 0.28f, 0.31f},
                                         {0.18f, 0.20f, 0.18f},
                                         {0.41f, 0.41f, 0.41f},
                                         {0.53f, 0.47f, 0.26f},
                                         {0.20f, 0.17f, 0.14f},
                                         {0.39f, 0.39f, 0.39f},
                                         {0.34f, 0.30f, 0.28f},
                                         {0.38f, 0.35f, 0.33f},
                                         {0.16f, 0.16f, 0.17f},
                                         {0.45f, 0.45f, 0.45f},
                                         {0.16f, 0.17f, 0.18f},
                                         {0.44f, 0.44f, 0.45f},
                                         {0.16f, 0.16f, 0.16f},
                                         {0.78f, 0.67f, 0.13f},
                                         {0.51f, 0.51f, 0.53f},
                                         {0.67f, 0.66f, 0.65f},
                                         {0.60f, 0.59f, 0.59f},
                                         {0.48f, 0.47f, 0.46f},
                                         {0.58f, 0.54f, 0.49f},
                                         {0.49f, 0.49f, 0.49f},
                                         {0.18f, 0.34f, 0.39f},
                                         {0.39f, 0.38f, 0.36f},
                                         {0.53f, 0.53f, 0.51f},
                                         {0.54f, 0.54f, 0.51f},
                                         {0.63f, 0.59f, 0.55f},
                                         {0.43f, 0.40f, 0.38f},
                                         {0.70f, 0.67f, 0.62f},
                                         {0.50f, 0.19f, 0.15f},
                                         {0.25f, 0.14f, 0.13f},
                                         {0.69f, 0.66f, 0.60f},
                                         {0.26f, 0.18f, 0.14f},
                                         {0.70f, 0.62f, 0.57f},
                                         {0.50f, 0.43f, 0.30f},
                                         {0.42f, 0.35f, 0.31f},
                                         {0.66f, 0.49f, 0.43f},
                                         {0.13f, 0.13f, 0.13f},
                                         {0.38f, 0.31f, 0.26f},
                                         {0.40f, 0.37f, 0.31f},
                                         {0.29f, 0.23f, 0.19f},
                                         {0.54f, 0.55f, 0.58f},
                                         {0.75f, 0.73f, 0.70f},
                                         {0.45f, 0.44f, 0.45f},
                                         {0.19f, 0.19f, 0.19f},
                                         {0.14f, 0.12f, 0.12f},
                                         {0.27f, 0.15f, 0.12f},
                                         {0.69f, 0.56f, 0.36f},
                                         {0.81f, 0.80f, 0.79f},
                                         {0.74f, 0.63f, 0.43f},
                                         {0.74f, 0.56f, 0.36f},
                                         {0.75f, 0.67f, 0.49f},
                                         {0.50f, 0.43f, 0.35f},
                                         {0.65f, 0.58f, 0.37f},
                                         {0.56f, 0.23f, 0.15f},
                                         {0.58f, 0.53f, 0.45f},
                                         {0.64f, 0.55f, 0.39f},
                                         {0.29f, 0.13f, 0.13f},
                                         {0.75f, 0.67f, 0.49f},
                                         {0.69f, 0.49f, 0.40f},
                                         {0.62f, 0.47f, 0.32f},
                                         {0.37f, 0.36f, 0.36f},
                                         {0.38f, 0.56f, 0.28f},
                                         {0.36f, 0.31f, 0.33f},
                                         {0.50f, 0.52f, 0.54f},
                                         {0.78f, 0.68f, 0.13f},
                                         {0.13f, 0.17f, 0.15f},
                                         {0.31f, 0.25f, 0.29f},
                                         {0.82f, 0.78f, 0.26f},
                                         {0.36f, 0.40f, 0.26f},
                                         {0.10f, 0.11f, 0.12f},
                                         {0.21f, 0.21f, 0.20f},
                                         {0.46f, 0.81f, 0.38f},
                                         {0.40f, 0.48f, 0.33f},
                                         {0.46f, 0.44f, 0.46f},
                                         {0.60f, 0.48f, 0.30f},
                                         {0.50f, 0.39f, 0.25f},
                                         {0.61f, 0.56f, 0.45f},
                                         {0.22f, 0.13f, 0.12f},
                                         {0.32f, 0.29f, 0.27f},
                                         {0.37f, 0.13f, 0.13f},
                                         {0.25f, 0.29f, 0.31f},
                                         {0.28f, 0.24f, 0.20f},
                                         {0.58f, 0.59f, 0.53f},
                                         {0.40f, 0.41f, 0.32f},
                                         {0.60f, 0.55f, 0.44f},
                                         {0.52f, 0.38f, 0.25f},
                                         {0.13f, 0.13f, 0.13f},
                                         {0.58f, 0.49f, 0.40f},
                                         {0.49f, 0.44f, 0.37f},
                                         {0.52f, 0.51f, 0.48f},
                                         {0.60f, 0.58f, 0.55f},
                                         {0.28f, 0.29f, 0.29f},
                                         {0.49f, 0.74f, 0.71f},
                                         {0.19f, 0.22f, 0.12f},
                                         {0.13f, 0.13f, 0.15f},
                                         {0.09f, 0.09f, 0.10f},
                                         {0.82f, 0.62f, 0.12f},
                                         {0.59f, 0.61f, 0.64f},
                                         {0.98f, 0.95f, 0.55f},
                                         {0.89f, 0.81f, 0.37f},
                                         {0.46f, 0.73f, 0.18f},
                                         {0.29f, 0.09f, 0.09f},
                                         {0.19f, 0.62f, 0.67f},
                                         {0.64f, 0.53f, 0.33f},
                                         {0.51f, 0.57f, 0.64f},
                                         {0.82f, 0.54f, 0.93f},
                                         {0.09f, 0.09f, 0.11f},
                                         {0.09f, 0.10f, 0.16f},
                                         {0.24f, 0.20f, 0.14f},
                                         {0.50f, 0.50f, 0.50f},
                                         {0.84f, 0.44f, 0.71f},
                                         {0.32f, 0.90f, 0.93f},
                                         {0.38f, 0.53f, 0.69f},
                                         {0.09f, 0.11f, 0.22f},
                                         {0.55f, 0.25f, 0.12f},
                                         {0.28f, 0.09f, 0.40f},
                                         {0.29f, 0.19f, 0.13f},
                                         {0.32f, 0.41f, 0.53f},
                                         {0.17f, 0.18f, 0.20f},
                                         {0.54f, 0.41f, 0.26f},
                                         {0.71f, 0.71f, 0.74f},
                                         {0.56f, 0.60f, 0.63f},
                                         {0.44f, 0.43f, 0.49f},
                                         {0.51f, 0.56f, 0.59f},
                                         {0.42f, 0.44f, 0.47f},
                                         {0.52f, 0.62f, 0.68f},
                                         {0.50f, 0.50f, 0.55f},
                                         {0.29f, 0.58f, 0.35f},
                                         {0.36f, 0.43f, 0.51f},
                                         {0.55f, 0.39f, 0.42f},
                                         {0.37f, 0.48f, 0.51f},
                                         {0.39f, 0.41f, 0.44f},
                                         {0.36f, 0.35f, 0.36f},
                                         {0.36f, 0.34f, 0.44f},
                                         {0.36f, 0.49f, 0.59f},
                                         {0.52f, 0.52f, 0.55f},
                                         {0.44f, 0.46f, 0.49f},
                                         {0.44f, 0.45f, 0.47f},
                                         {0.64f, 0.47f, 0.23f},
                                         {0.45f, 0.48f, 0.51f},
                                         {0.38f, 0.50f, 0.44f},
                                         {0.20f, 0.38f, 0.26f},
                                         {0.52f, 0.36f, 0.20f},
                                         {0.71f, 0.58f, 0.36f},
                                         {0.79f, 0.79f, 0.78f},
                                         {0.51f, 0.52f, 0.51f},
                                         {0.79f, 0.76f, 0.72f},
                                         {0.49f, 0.45f, 0.22f},
                                         {0.64f, 0.49f, 0.29f},
                                         {0.65f, 0.65f, 0.65f},
                                         {0.65f, 0.51f, 0.31f},
                                         {0.43f, 0.38f, 0.38f},
                                         {0.62f, 0.62f, 0.60f},
                                         {0.61f, 0.31f, 0.80f},
                                         {0.73f, 0.63f, 0.41f},
                                         {0.45f, 0.46f, 0.49f},
                                         {0.75f, 0.66f, 0.30f},
                                         {0.37f, 0.34f, 0.31f},
                                         {0.81f, 0.81f, 0.80f},
                                         {0.73f, 0.74f, 0.77f},
                                         {0.38f, 0.25f, 0.15f},
                                         {0.75f, 0.82f, 0.86f},
                                         {0.45f, 0.48f, 0.49f},
                                         {0.09f, 0.09f, 0.10f},
                                         {0.71f, 0.67f, 0.62f},
                                         {0.49f, 0.49f, 0.36f},
                                         {0.80f, 0.83f, 0.82f},
                                         {0.30f, 0.30f, 0.33f},
                                         {0.33f, 0.36f, 0.44f},
                                         {0.27f, 0.27f, 0.32f},
                                         {0.25f, 0.17f, 0.28f},
                                         {0.58f, 0.80f, 0.64f},
                                         {0.98f, 0.09f, 0.98f},
                                         {0.45f, 0.44f, 0.37f},
                                         {0.60f, 0.53f, 0.44f},
                                         {0.42f, 0.32f, 0.58f}};
    // ice .. diamond plate, brick, glass, asphalt, mud, cobblestone, concrete, tile, marble, corrugated, hazard, snow, stone,
    // crystal, circuit, rust, terracotta, then breakable brick, glass and cobblestone (their plain ones' colours), then
    // the 100 block looks and the 333 picks of the earlier rounds (each one's mean colour in its preview render)
    static_assert(sizeof kNewMatColors / sizeof *kNewMatColors == kMatLarge - kMatIce + kSemiCount + kBreakCount + kFeatCount, "a colour per material from ice on (a large one: its base's), then the see-through, breakable and feature ones");
    const Vec3 matColor[6] = {r_.baseColor, r_.accentColor, {0.22f, 0.22f, 0.24f}, r_.metalColor, r_.glowColor, r_.metalColor * 0.7f};
    r_.worldOutline = cfg_.outlineWorld;   // (the map's boxes, baked or not, take the world outline width)
    if (!edBaked_) {   // the map's boxes: one static batch, rebuilt only when they change (it was streamed every frame)
        if (edMeshDirty_ || edMeshOutline_ != cfg_.outlineWorld) {
            r_.outlineWidth = cfg_.outlineWorld;
            r_.setStaticBoxes([&] {
                for (auto& b : ed_.brushes) { // (water, lava, trim, nograpple, acid past the table: blue, orange, accent, slate, green; clip below)
                    if (b.material == 9) continue;
                    Vec3 c = b.material < 6 ? matColor[b.material] : b.material == 6 ? Vec3(0.2f, 0.4f, 0.8f) : b.material == 7 ? Vec3(1.0f, 0.4f, 0.1f) : b.material == kMatNoGrapple ? Vec3(0.3f, 0.33f, 0.42f) : b.material == kMatAcid ? Vec3(0.36f, 1.0f, 0.23f) : b.material >= kMatIce ? kNewMatColors[b.material >= kMatSemi ? kMatLarge - kMatIce + (b.material - kMatSemi) : (b.material >= kMatLarge ? kLarge[b.material - kMatLarge].base : b.material) - kMatIce] : r_.accentColor;
                    if (b.shape != BRUSH_BOX) r_.slope(b, c);
                    else r_.box(b.box.center(), (b.box.mx - b.box.mn) * 0.5f, 0, c, 1.0f, false);
                }
            });
            r_.outlineWidth = 1.0f;
            edMeshDirty_ = false;
            edMeshOutline_ = cfg_.outlineWorld;
        }
        r_.useStaticBoxes();
    }
    mapCache();
    if (edBaked_) for (auto& b : ed_.brushes) if (glassMaterial(b.material)) r_.translucentBox(b.box, kGlassTint);   // (the bake leaves glass out)
    for (auto& b : edClipBoxes_) r_.translucentBox(b, kClipColor);   // clip: invisible in play (the baked world leaves it out too), see-through violet here so it can be found and edited
    for (auto& sp : ed_.spawns) {
        Vec3 c = kTeamColors[sp.team & 3];
        r_.box(sp.pos + Vec3(0, 0.9f, 0), {0.3f, 0.9f, 0.3f}, sp.yaw, c, 1.0f, false);
        r_.line(sp.pos + Vec3(0, 1.5f, 0), sp.pos + Vec3(0, 1.5f, 0) + angleForward(sp.yaw, 0) * 1.2f, 0.06f, rgba(c));
    }
    for (auto& l : ed_.lights) r_.box(l.pos, {0.2f, 0.2f, 0.2f}, 0, {1.0f, 0.9f, 0.4f}, 1.0f, false);
    for (size_t i = 0; i < ed_.payloadPath.size(); i++) {
        r_.box(ed_.payloadPath[i], {0.2f, 0.2f, 0.2f}, 0, {0.3f, 1.0f, 0.5f}, 1.0f, false);
        if (i) r_.line(ed_.payloadPath[i - 1], ed_.payloadPath[i], 0.08f, {0.3f, 1.0f, 0.5f, 0.9f});
    }
    for (auto& b : ed_.bombSites) r_.translucentBox(b, {1.0f, 0.5f, 0.2f, 0.25f});
    for (size_t i = 0; i < ed_.checkpoints.size(); i++)
        r_.translucentBox(ed_.checkpoints[i], i + 1 == ed_.checkpoints.size() ? Color{1, 1, 1, 0.3f} : Color{0.3f, 1.0f, 0.4f, 0.22f});
    if (ed_.hasHill) r_.translucentBox(ed_.hill, {1.0f, 0.9f, 0.3f, 0.22f});
    for (auto& l : ed_.ladders) r_.translucentBox(l, {0.4f, 0.8f, 1.0f, 0.18f});
    for (auto& e : edMulti_) if ((size_t)e.idx < kindCount(ed_, e.kind)) { AABB b = edBounds(ed_, e); b.mn -= Vec3(0.03f, 0.03f, 0.03f); b.mx += Vec3(0.03f, 0.03f, 0.03f); r_.translucentBox(b, {1.0f, 0.85f, 0.1f, 0.15f}); }
    if (edSelValid()) {
        AABB b = edBounds(ed_, edSel_);
        b.mn -= Vec3(0.02f, 0.02f, 0.02f); b.mx += Vec3(0.02f, 0.02f, 0.02f);
        Color c = {1.0f, 0.85f, 0.1f, 1};
        for (int e = 0; e < 12; e++) { // the 12 edges of the box
            int ax = e / 4, a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
            Vec3 p = b.mn, q;
            if (e & 1) p[a1] = b.mx[a1];
            if (e & 2) p[a2] = b.mx[a2];
            q = p; q[ax] = b.mx[ax];
            r_.line(p, q, std::max(0.04f, length((p + q) * 0.5f - edCam_) * 0.004f), c);
        }
        if (edSel_.kind == ED_LIGHT) { auto& l = ed_.lights[edSel_.idx]; r_.translucentBox(around(l.pos, {l.radius, l.radius, l.radius}), {1, 0.9f, 0.4f, 0.06f}); }
        const Vec3 axisCol[3] = {{1.0f, 0.3f, 0.25f}, {0.35f, 1.0f, 0.35f}, {0.3f, 0.55f, 1.0f}};
        Vec3 bc = b.center();
        // handles: drawn over everything (the overlay pass clears depth after the world), so the ones on the far
        // faces and inside walls show too; sized to stay grabbable at any distance
        for (auto& hd : handles()) {
            float sz = clampf(length(hd.pos - edCam_) * 0.018f, 0.05f, 1.2f);
            bool active = edDragAxis_ == hd.axis && edDragSign_ == hd.sign && edDragMove_ == hd.move;
            Vec3 col = axisCol[hd.axis] * (active ? 1.3f : 1.0f);
            if (hd.move) { // the arrow's shaft: from the face it sticks out of
                Vec3 from = bc, half = {sz * 0.15f, sz * 0.15f, sz * 0.15f};
                from[hd.axis] = hd.sign > 0 ? b.mx[hd.axis] : b.mn[hd.axis];
                half[hd.axis] = std::fabs(hd.pos[hd.axis] - from[hd.axis]) * 0.5f;
                r_.overlayBox((from + hd.pos) * 0.5f, half, col * 0.8f);
                r_.overlayBox(hd.pos, {sz * 0.8f, sz * 0.8f, sz * 0.8f}, col);
            } else r_.overlayBox(hd.pos, {sz, sz, sz}, col);
        }
    }
    // the other editors: a camera box each, labeled after the scene is drawn
    auto& others = edOthers_;   // (kept from frame to frame)
    others.clear();
    if (edOnline_) {
        const auto& ps = framePlayers();
        Vec3 col(ui_.accent.r, ui_.accent.g, ui_.accent.b);
        for (int i = 0; i < kMaxPlayers; i++) {
            if (!ps[i].valid || i == net_.clientId()) continue;
            Vec3 p = ps[i].snap.pos + Vec3(0, kEyeHeight, 0);
            if (length(p - edCam_) < 1.5f) continue; // right on top of us: would fill the screen
            r_.box(p, {0.25f, 0.18f, 0.3f}, ps[i].snap.yaw, col, 1.0f, false);
            r_.line(p, p + angleForward(ps[i].snap.yaw, ps[i].snap.pitch) * 0.8f, 0.04f, ui_.accent);
            others.push_back({p + Vec3(0, 0.45f, 0), net_.playerName(i)});
        }
    }
    { // where new things go: the snapped point under the screen center
        Vec3 dir = angleForward(yaw_, pitch_), n;
        float t = ed_.raycast(edCam_, dir, 200, &n);
        if (t >= 0) r_.box(snapTo(edCam_ + dir * t + n * 0.001f, g), {0.06f, 0.06f, 0.06f}, 0, {1, 0.85f, 0.1f}, 1.0f, false);
    }
    sceneSettings();   // (fullbright and its colour, brightness, outlines: as in the game)
    r_.drawScene(false);
    for (auto& o : others) {
        float sx, sy, ts = std::max(1.0f, std::round(s));
        if (r_.project(o.first, sx, sy) && sx < panelX) r_.text(sx - r_.textWidth(o.second, ts) * 0.5f, sy, ts, o.second, kWhite);
    }
    // crosshair: where things get added, the middle of the view
    r_.rect(panelX * 0.5f - 6 * s, H * 0.5f - 1, 12 * s, 2, {1, 1, 1, 0.8f});
    r_.rect(panelX * 0.5f - 1, H * 0.5f - 6 * s, 2, 12 * s, {1, 1, 1, 0.8f});
    r_.resize(fullW, (int)H);

    // ---- panel (ui-redesign-spec, the editor pages; 720p units x s): docked on the right, everything 12 from its edges,
    // in sections - FILE (or the session's), ADD, GROUPS, MODES, then the selection, which starts at the same height
    // whatever is selected. Section headers at text 2 in the accent, captions and notes at 1.5 (nothing smaller); rows
    // of 23 + 4 (with a spawn, light or stairs selected the panel ends at 695: ROTATE / MIRROR stay on screen at 720p).
    ui_.begin(&r_, uiIn_, s);
    if (ui_.probing()) ui_.where = edPath();   // (the UI test: the panel is the editor's, whatever is open over the view)
    ui_.muted = modal;   // (under the save question / the picker nothing in the panel takes a click)
    ui_.panel(panelX, 0, panelW, H, 0.85f);
    float x = panelX + 12 * s, w = panelW - 24 * s, y = 12 * s, h = 23 * s, gap = 4 * s;
    const float w2 = (w - gap) * 0.5f, w3 = (w - 2 * gap) / 3, w4 = (w - 3 * gap) / 4;
    ui_.box(x, x + w);   // (labels fit the panel's inside)
    auto row = [&] { y += h + gap; };
    auto header = [&](const std::string& t) { ui_.label(x, y, t, ui_.accent, 1.0f, w); y += 20 * s; };
    // GROUPS / ROTATE / MIRROR: a caption 60 wide, then three short buttons
    const float capW = 60 * s, bw3 = (w - capW - 2 * gap) / 3;
    auto caption = [&](const char* t) { ui_.hint(x, y + (h - r_.textHeight(ui_.hintScale())) * 0.5f, t, kGrey, capW - gap); };
    if (edOnline_) {
        int n = 0;
        const auto& ps = framePlayers();
        for (auto& p : ps) n += p.valid;
        ui_.label(x, y, "EDITING TOGETHER (" + std::to_string(n) + ")", ui_.accent, 1.0f, w);
        y += 20 * s;
        bool temp = net_.hasSnapshot() && (net_.mode().flags & MS_TEMP_EDIT);
        int nl = ui_.hintWrapped(x, y, "MAP " + net_.map().name + (temp ? " - REVERTS WHEN THE MAP CHANGES" : host_ ? " - AUTOSAVED" : " - THE HOST SAVES IT"), kGrey, w, 14 * s);
        y += (nl * 14 + 2) * s;
    } else {
        ui_.label(x, y, edDirty_ ? "MAP EDITOR *" : "MAP EDITOR", ui_.accent, 1.0f, w);
        y += 24 * s;
        header("FILE: NAME / TITLE");
    }
    if (!ui_.typing()) edTitle_ = ed_.title;
    {
        // the file name, in a session too (SAVE A COPY / the host's SAVE AS use it). Kept a valid file name as you
        // type: a space becomes _, other characters are dropped (they used to stay, and every save was refused)
        const float nameW = 116 * s;
        if (ui_.textField(40, x, y, nameW, h, edName_, 24, UiExp::local().withId("FILE NAME"))) edName_ = typedMapName(edName_);
        std::string before = edTitle_;
        if (ui_.textField(41, x + nameW + gap, y, w - nameW - gap, h, edTitle_, 32, UiExp::edit().withId("TITLE")) && edTitle_ != before) edDo("t " + edTitle_, "t " + ed_.title, true);
    }
    row();
    {
        static std::vector<std::string> modeNames;
        if (modeNames.empty()) { modeNames.push_back("BUILT FOR: ANY"); for (int m = 0; m < MODE_COUNT; m++) modeNames.push_back(std::string("BUILT FOR: ") + modeName(m)); }
        int idx = ed_.preferredMode + 1;
        if (ui_.cycle(x, y, w, h, "BUILT FOR", idx, MODE_COUNT + 1, modeNames, UiExp::edit()))
            edDo("f " + std::to_string(idx - 1), "f " + std::to_string(ed_.preferredMode));
        row();
    }
    if (edOnline_) {
        bool temp = net_.hasSnapshot() && (net_.mode().flags & MS_TEMP_EDIT);
        if (host_) {
            if (ui_.button(x, y, w2, h, "PLAY MODE", UiExp::cmd("editmode off"))) net_.sendCommand("editmode off"); // everyone plays it; ESC menu has EDIT MODE
        } else if (ui_.button(x, y, w2, h, "VOTE: PLAY IT", UiExp::cmd("callvote edit off"))) net_.sendCommand("callvote edit off");
        // leaving: the host goes back to the editor it started the session from; a guest is asked about keeping a copy
        if (ui_.button(x + w2 + gap, y, w2, h, host_ && !temp ? "END SESSION" : "LEAVE",
                       UiExp::nav(host_ ? (testMap_.empty() ? "main" : "editor") : "main*").withId("LEAVE"))) { closeEditor(); return; }
        row();
        if (temp || !host_) {
            if (ui_.button(x, y, w, h, "SAVE A COPY AS " + edName_, (edOfficial() ? UiExp::nav(edKeepPath()) : UiExp::state("usermaps")).withId("SAVE A COPY"))) { if (edOfficial()) edAskKeep(ED_THEN_SAVE); else edSaveCopy(ed_, edName_); }
        } else if (ui_.button(x, y, w, h, "SAVE AS " + edName_,
                              (!validMapName(edName_) || isOfficialMapName(edName_) ? UiExp::state("ednotice") : UiExp::cmd("editmode saveas")).withId("SAVE AS"))) {
            if (!validMapName(edName_)) edNotify("name: letters, digits, - and _ only (not a device name like con)");
            else if (isOfficialMapName(edName_)) edNotify(edName_ + " is a built-in map - pick a new name for yours");
            else net_.sendCommand("editmode saveas " + edName_); // your session's file: saved under this name from now on
        }
        row();
    }
    // BAKE LIGHT: its own row in a session, beside HOST EDIT SESSION offline (FILE folded: decision 13)
    auto bakeButton = [&](float bx, float bwid) {
        if (ui_.button(bx, y, bwid, h, edBaked_ ? "UNBAKE" : "BAKE LIGHT", UiExp::state("edbaked").withId("BAKE LIGHT"))) {
            if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
            else { r_.loadWorld(ed_, [&](float p) { drawBakeProgress(edName_, p); }); edBaked_ = true; }
            edMeshDirty_ = true;   // (the bake sets the map's palette: the boxes' colours)
        }
    };
    if (edOnline_) {
        bakeButton(x, w);
        row();
    } else {
        // NEW | SAVE | EXIT | TEST (a built-in map with your edits: leaving it, saving or testing asks first whether to
        // keep them as a new map)
        const bool ask = edDirty_ && edOfficial(), builtin = isOfficialMapName(edName_);
        if (ui_.button(x, y, w4, h, "NEW", ask ? UiExp::nav(edKeepPath()) : UiExp::state("edopen"))) { if (ask) edAskKeep(ED_THEN_OPEN, ""); else { openEditor(""); return; } }
        if (ui_.button(x + w4 + gap, y, w4, h, "SAVE", builtin ? (edDirty_ ? UiExp::nav(edKeepPath()) : UiExp::state("ednotice")) : UiExp::state("usermaps"))) edSave();
        if (ui_.button(x + 2 * (w4 + gap), y, w4, h, "EXIT", ask ? UiExp::nav(edKeepPath())
                                                                : edDirty_ && time_ > edLeaveArmed_ ? UiExp::state("ednotice") : UiExp::nav("main"))) {
            if (ask) { edAskKeep(ED_THEN_LEAVE); }   // (B3)
            else if (edDirty_ && time_ > edLeaveArmed_) { edLeaveArmed_ = time_ + 2; edNotify("unsaved changes - click EXIT again to leave"); }
            else { closeEditor(); return; }
        }
        if (ui_.button(x + 3 * (w4 + gap), y, w4, h, "TEST", builtin && edDirty_ ? UiExp::nav(edKeepPath()) : UiExp::state("hosting", "1"))) { edTest(); if (screen_ != Screen::Editor) return; }
        row();
        if (!maps_.empty()) {
            static std::vector<std::string> mapNames, mapSrc;   // ("MAP: name", redone when the list changes)
            if (mapSrc != maps_) { mapSrc = maps_; mapNames.clear(); for (auto& m : maps_) mapNames.push_back("MAP: " + m); }
            const float openW = 92 * s;
            ui_.cycle(x, y, w - openW - gap, h, "MAP", edLoadIdx_, (int)maps_.size(), mapNames, UiExp::local());
            if (ui_.button(x + w - openW, y, openW, h, "OPEN", ask ? UiExp::nav(edKeepPath()) : UiExp::state("edopen"))) {
                std::string next = maps_[edLoadIdx_ % maps_.size()];
                if (ask) edAskKeep(ED_THEN_OPEN, next);
                else { openEditor(next); return; }
            }
            row();
        }
        const float hostW = 180 * s;
        if (ui_.button(x, y, hostW, h, "HOST EDIT SESSION", builtin && edDirty_ ? UiExp::nav(edKeepPath()) : UiExp::state("hosting", "1"))) { edHostSession(); if (screen_ != Screen::Editor) return; }
        bakeButton(x + hostW + gap, w - hostW - gap);
        row();
        if (ui_.button(x, y, w2, h, "MAPS FOLDER", UiExp::url())) openUrl(mapsFolder());
        if (ui_.button(x + w2 + gap, y, w2, h, time_ < edResetArmed_ ? "CLICK TO CONFIRM" : "RESET MAPS", UiExp::state("ednotice").withId("RESET MAPS"))) {
            if (time_ >= edResetArmed_) { edResetArmed_ = time_ + 3; edNotify("puts every built-in map back to its original version and removes retired ones (your own maps are kept) - click again"); }
            else {
                edResetArmed_ = 0;
                int n = resetOfficialMaps();
                rescanMaps();
                edNotify(std::to_string(n) + " built-in maps reset to the original version");
            }
        }
        row();
    }
    y += 4 * s;
    header("ADD (AT THE SCREEN CENTER)");
    static const std::vector<std::string> mats(std::begin(matLabels), std::end(matLabels));
    {   // rooms, stairs and ladders are one object each: stretch it and its pieces follow (UNGROUP to split a room / stairs)
        if (ui_.button(x, y, w4, h, "BOX", UiExp::edit())) edAdd(ED_BRUSH, 0);
        if (ui_.button(x + w4 + gap, y, w4, h, "ROOM", UiExp::edit())) edAdd(ED_GROUP, Map::GROUP_ROOM);
        if (ui_.button(x + 2 * (w4 + gap), y, w4, h, "STAIRS", UiExp::edit())) edAdd(ED_GROUP, Map::GROUP_STAIRS);
        if (ui_.button(x + 3 * (w4 + gap), y, w4, h, "LADDER", UiExp::edit())) edAdd(ED_LADDER, 0);
        row();
        if (ui_.button(x, y, w, h, "NEW ONES: " + std::string(mats[edMat_]).substr(10), UiExp::nav(edMatsPath()).withId("MATERIAL"))) edMatMenu_ = 1;
        row();
    }
    if (ui_.button(x, y, w3, h, "SPAWN", UiExp::edit())) edAdd(ED_SPAWN, SPAWN_ANY);
    if (ui_.button(x + w3 + gap, y, w3, h, "LIGHT", UiExp::edit())) edAdd(ED_LIGHT, 0);
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "PATH PT", UiExp::edit())) edAdd(ED_PATH, 0);
    row();
    if (ui_.button(x, y, w3, h, "BOMB SITE", UiExp::edit())) edAdd(ED_SITE, 0);
    if (ui_.button(x + w3 + gap, y, w3, h, "HILL", UiExp::edit())) edAdd(ED_HILL, 0);
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "CHECKPT", UiExp::edit())) edAdd(ED_CHECK, 0);
    row();
    {
        // GROUPS: MAKE (UNGROUP) / EXPORT work on selected boxes and groups (greyed out with nothing of the kind selected)
        bool groupSel = edSelValid() && (edSel_.kind == ED_BRUSH || edSel_.kind == ED_GROUP);
        bool oneGroup = edSelValid() && edSel_.kind == ED_GROUP && edMulti_.empty();
        // (grouping every box the map has is refused: a map keeps a box of its own; it says so)
        bool allBoxes = groupSel && !oneGroup && edGroupTakesEveryBox();
        caption("GROUPS");
        const float gx = x + capW;
        if (ui_.button(gx, y, bw3, h, oneGroup ? "UNGROUP" : "MAKE", (allBoxes ? UiExp::state("ednotice") : UiExp::edit()).withId("GROUP"), groupSel)) { if (oneGroup) edUngroup(); else edGroupSelection(); }
        if (ui_.button(gx + bw3 + gap, y, bw3, h, "IMPORT", (edGroupMenu_ ? UiExp::nav(edGroupsPath()) : UiExp::state("edimport")).withId("IMPORT GROUP"))) edImportGroup();
        if (ui_.button(gx + 2 * (bw3 + gap), y, bw3, h, "EXPORT", (oneGroup || edExportAsk_ ? UiExp::state("groups") : UiExp::nav(edExportAskPath())).withId("EXPORT GROUP"), groupSel)) edExportGroup();
        row();
    }
    {
        static const std::vector<std::string> grids = {"GRID: 1/8 M", "GRID: 1/4 M", "GRID: 1/2 M", "GRID: 1 M", "GRID: 2 M", "GRID: 4 M"};
        ui_.cycle(x, y, w, h, "GRID ([ ])", edGridIdx_, kGridCount, grids, UiExp::local());
        row();
    }
    if (!edSelValid()) {   // clean-ups over the whole map (one undo step each; see MapFix). Only with nothing selected:
                           // a selection's rows (down to MIRROR) need the room, and these work on the whole map anyway
        const float hw = (w - gap) * 0.5f;
        if (ui_.button(x, y, hw, h, "FIX MAP", UiExp::state("ednotice"))) edFix(false);
        if (ui_.button(x + hw + gap, y, hw, h, "FIX GAPS", UiExp::state("ednotice"))) edFix(true);
        row();
    }
    y += 4 * s;
    {   // which modes this map is offered for: click to allow / disallow (dim = missing pieces, e.g. team spawns). Chips 20
        // tall in rows of four, each as wide as its name plus an even share of the row (the last row: a quarter each)
        header("MODES (CLICK: ALLOW / BLOCK)");
        static std::string names[MODE_COUNT];
        static float tws[MODE_COUNT], twKey = -1;
        static int twGen = -1;
        const float cts = ui_.hintScale(), ch = 20 * s;
        if (names[0].empty()) for (int m = 0; m < MODE_COUNT; m++) { names[m] = modeName(m); for (auto& c : names[m]) c = (char)toupper((unsigned char)c); }
        if (twKey != cts || twGen != r_.fontGeneration()) {   // (the names' widths: once per size and font)
            twKey = cts; twGen = r_.fontGeneration();
            for (int m = 0; m < MODE_COUNT; m++) tws[m] = r_.textWidth(names[m], cts);
        }
        const uint32_t all = (1u << MODE_COUNT) - 1;
        uint32_t mask = ed_.allowedModes ? ed_.allowedModes : all;
        mapCache();
        for (int r0 = 0; r0 < MODE_COUNT; r0 += 4) {
            const int n = std::min(4, MODE_COUNT - r0);
            float sum = 0;
            for (int k = 0; k < n; k++) sum += tws[r0 + k];
            const float share = n == 4 ? (w - 3 * gap - sum) / 4 : 0;
            float cx = x, cy = y + (r0 / 4) * 24 * s;
            for (int k = 0; k < n; k++) {
                const int m = r0 + k;
                const float cw = n == 4 ? tws[m] + share : w4;
                bool on = mask >> m & 1, can = edCanModes_ >> m & 1;
                bool hv = uiIn_.mx >= cx && uiIn_.mx < cx + cw && uiIn_.my >= cy && uiIn_.my < cy + ch;
                Color bg = on ? Color{ui_.accent.r * 0.55f, ui_.accent.g * 0.55f, ui_.accent.b * 0.55f, can ? 0.95f : 0.45f} : Color{0.12f, 0.13f, 0.16f, 0.9f};
                r_.rect(cx, cy, cw, ch, bg);
                if (hv) r_.rect(cx, cy + ch - 2 * s, cw, 2 * s, ui_.accent);
                ui_.hintCentered(cx + cw * 0.5f, cy + (ch - r_.textHeight(cts)) * 0.5f, names[m], on && can ? kWhite : kGrey, cw - 4 * s);
                if (ui_.probing()) ui_.custom(cx, cy, cw, ch, names[m], (mask ^ (1u << m)) == 0 ? UiExp::state("ednotice") : UiExp::edit());
                if (hv && uiIn_.pressed) {
                    uint32_t nm = mask ^ (1u << m);
                    if (nm == 0) edNotify("a map needs at least one mode");
                    else edDo("m " + std::to_string(nm == all ? 0 : nm), "m " + std::to_string(ed_.allowedModes));
                }
                cx += cw + gap;
            }
        }
        y += (((MODE_COUNT + 3) / 4) * 24 - 4 + 8) * s;
    }
    // ---- the selection: a header (what it is), a line under it (where it is), then its controls 36 below the header
    static const char* kScopes[] = {"box", "spawn", "light", "site", "hill", "checkpoint", "path", "ladder", "group"};
    if (edSelValid()) {
        AABB b = edBounds(ed_, edSel_);
        int kind = edSel_.kind, i = edSel_.idx;
        if (ui_.probing()) ui_.scope = kScopes[kind];   // (the UI test: these act on the selection: once per kind)
        std::string what = kKindNames[kind];
        if (kind == ED_GROUP) what = ed_.groups[i].type == Map::GROUP_ROOM ? "ROOM" : ed_.groups[i].type == Map::GROUP_STAIRS ? "STAIRS" : "GROUP";
        if (!edMulti_.empty()) what += " + " + std::to_string(edMulti_.size()) + " MORE";
        if (kind == ED_CHECK) what += " " + std::to_string(i + 1) + (i + 1 == (int)ed_.checkpoints.size() ? " (FINISH)" : "");
        if (kind == ED_PATH) what += " " + std::to_string(i + 1);
        header("SELECTED: " + what);
        char buf[128];
        if (kind == ED_SPAWN || kind == ED_LIGHT || kind == ED_PATH) {
            Vec3 p = kind == ED_SPAWN ? ed_.spawns[i].pos : kind == ED_LIGHT ? ed_.lights[i].pos : ed_.payloadPath[i];
            snprintf(buf, sizeof buf, "AT %g %g %g", p.x, p.y, p.z);
        } else snprintf(buf, sizeof buf, "%g %g %g TO %g %g %g", b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
        ui_.hint(x, y, buf, kGrey, w);
        y += 16 * s;
        if (kind == ED_BRUSH) {
            int m = ed_.brushes[i].material;
            if (ui_.button(x, y, w, h, mats[m] + "  (M)", UiExp::nav(edMatsPath()).withId("MATERIAL (M)"))) edMatMenu_ = 2;
            row();
            if (m == kMatTeleport) {   // (owner, 2026-10-29: linked by the same value; the face it lets you out of)
                float link = ed_.brushes[i].link;
                if (ui_.slider(x, y, w, h, "LINK", link, 0, 255, "%.0f", UiExp::edit(), 1))
                    edModify([&](Map& mm) { mm.brushes[i].link = (uint8_t)clampf(std::round(link), 0, 255); }, true);
                row();
                static const std::vector<std::string> faces = {"EXIT FACE: +X", "EXIT FACE: -X", "EXIT FACE: TOP (+Y)", "EXIT FACE: BOTTOM (-Y)", "EXIT FACE: +Z", "EXIT FACE: -Z"};
                int f = ed_.brushes[i].exitFace;
                if (ui_.cycle(x, y, w, h, "EXIT FACE", f, kFaces, faces, UiExp::edit())) edModify([&](Map& mm) { mm.brushes[i].exitFace = (uint8_t)f; }, false);
                row();
            }
        } else if (kind == ED_GROUP) {
            auto& gr = ed_.groups[i];
            if (gr.type == Map::GROUP_CUSTOM) {
                int nl = ui_.hintWrapped(x, y, std::to_string(gr.parts.size()) + " BOXES, STRETCHED AS ONE - UNGROUP TO EDIT THEM", kGrey, w, 14 * s);
                y += (nl * 14 + 4) * s;
            } else {
                int m = gr.material;
                if (ui_.button(x, y, w, h, mats[m] + "  (M)", UiExp::nav(edMatsPath()).withId("MATERIAL (M)"))) edMatMenu_ = 2;
                row();
            }
            if (gr.type == Map::GROUP_STAIRS) {
                static const std::vector<std::string> dirs = {"RISING TOWARD: +X", "RISING TOWARD: -X", "RISING TOWARD: +Z", "RISING TOWARD: -Z"};
                int d = gr.dir;
                if (ui_.cycle(x, y, w, h, "RISING TOWARD", d, 4, dirs, UiExp::edit())) edModify([&](Map& mm) { mm.groups[i].dir = d; }, false);
                row();
            }
        } else if (kind == ED_SPAWN) {
            static const std::vector<std::string> teams = {"TEAM: TEAM A", "TEAM: TEAM B", "TEAM: ANY TEAM", "TEAM: RACE"};
            int t = ed_.spawns[i].team;
            if (ui_.cycle(x, y, w, h, "TEAM (M)", t, 4, teams, UiExp::edit())) edModify([&](Map& mm) { mm.spawns[i].team = t; }, false);
            row();
            float deg = std::remainder(ed_.spawns[i].yaw * 180.0f / PI, 360.0f);
            if (ui_.slider(x, y, w, h, "FACING (Q E)", deg, -180, 180, "%.0f", UiExp::edit(), 0))
                edModify([&](Map& mm) { mm.spawns[i].yaw = (ui_.typed() ? std::round(deg) : std::round(deg / 15) * 15) * PI / 180.0f; }, true);   // (dragged: 15 degree steps; typed: any degree)
            row();
        } else if (kind == ED_LIGHT) {
            float r = ed_.lights[i].radius, in = ed_.lights[i].intensity;
            // (typed: within what a map file allows - validMapLight)
            if (ui_.slider(x, y, w, h, "RADIUS", r, 1, 60, "%.0f", UiExp::edit(), kNewLightRadius)) edModify([&](Map& mm) { mm.lights[i].radius = clampf(std::round(r), 1, 100); }, true);
            row();
            if (ui_.slider(x, y, w, h, "BRIGHTNESS", in, 0.1f, 4, "%.1f", UiExp::edit(), kNewLightIntensity)) edModify([&](Map& mm) { mm.lights[i].intensity = clampf(std::round(in * 10) / 10, 0.1f, 10); }, true);
            row();
        }
        // (the hill is one per map: no DUPLICATE; the last box / spawn can't go: DELETE says so instead)
        if (ui_.button(x, y, w2, h, "DUPLICATE", UiExp::edit(), kind != ED_HILL)) edDuplicate();
        bool lastOne = (kind == ED_BRUSH && ed_.brushes.size() <= 1) || (kind == ED_SPAWN && ed_.spawns.size() <= 1);
        if (ui_.button(x + w2 + gap, y, w2, h, "DELETE", lastOne ? UiExp::state("ednotice") : UiExp::edit())) edDelete();
        row();
        {   // rotate 90 degrees about each axis, mirror across each plane (everything selected, about its middle); the
            // Shift / Ctrl shortcuts are in the key help
            static const char* rot[] = {"Y (R)", "X", "Z"};
            static const char* mir[] = {"X (F)", "Z", "Y"};
            static const char* rotId[] = {"ROTATE Y", "ROTATE X", "ROTATE Z"};
            static const char* mirId[] = {"MIRROR X", "MIRROR Z", "MIRROR Y"};
            static const int rotOp[] = {ED_ROT_Y, ED_ROT_X, ED_ROT_Z}, mirOp[] = {ED_MIR_X, ED_MIR_Z, ED_MIR_Y};
            caption("ROTATE");
            for (int k = 0; k < 3; k++) if (ui_.button(x + capW + k * (bw3 + gap), y, bw3, h, rot[k], edTransformExp(rotOp[k]).withId(rotId[k]))) edTransform(rotOp[k]);
            row();
            caption("MIRROR");
            for (int k = 0; k < 3; k++) if (ui_.button(x + capW + k * (bw3 + gap), y, bw3, h, mir[k], edTransformExp(mirOp[k]).withId(mirId[k]))) edTransform(mirOp[k]);
            row();
            if (kind == ED_BRUSH) {   // SPLIT (along the selection's longest side), COMBINE, SUBTRACT (edBoxOpPlan); one row, so
                // the panel still ends on screen at 720p
                const int sop = edSplitOp();
                if (ui_.button(x, y, w3, h, "SPLIT", edBoxOpExp(sop))) edBoxOp(sop);
                if (ui_.button(x + w3 + gap, y, w3, h, "COMBINE", edBoxOpExp(ED_COMBINE))) edBoxOp(ED_COMBINE);
                if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "SUBTRACT", edBoxOpExp(ED_SUBTRACT))) edBoxOp(ED_SUBTRACT);
                row();
            }
        }
    } else {
        header("NOTHING SELECTED");
        ui_.hint(x, y, "CLICK ON SOMETHING TO SELECT IT", kGrey, w);
        y += 16 * s;
        // clip boxes are invisible in play and easy to lose in a big map: step through them (selects one and
        // flies the camera back from it, keeping the view direction)
        mapCache();
        int nClip = edClipCount_;
        if (nClip && ui_.button(x, y, w, h, "FIND INVISIBLE WALLS: " + std::to_string(nClip), UiExp::state("edsel").withId("FIND CLIP BOXES"))) {
            for (size_t k = 1; k <= ed_.brushes.size(); k++) {
                int j = (int)((edClipFind_ + k + ed_.brushes.size()) % ed_.brushes.size());
                if (ed_.brushes[j].generated || ed_.brushes[j].material != 9) continue;
                edClipFind_ = j;
                edSel_ = {ED_BRUSH, j};
                edMulti_.clear();
                const AABB& cb = ed_.brushes[j].box;
                edCam_ = cb.center() - angleForward(yaw_, pitch_) * clampf(length(cb.mx - cb.mn) * 0.8f, 6, 120);
                break;
            }
        }
    }
    ui_.scope = nullptr;
    ui_.noBox();

    // ---- the 3D view's key bar (decision 13): a status line - what the map can be played as, its counts - over the
    // key help, all at 1.5, 16 from the view's left edge, lines 14 apart; whatever doesn't fit the view's width goes
    // on another line (a 4:3 window takes one more), and the bar grows to fit
    {
        const float hs = ui_.hintScale(), lh = 14 * s, lx = 16 * s, maxW = panelX - 32 * s;
        mapCache();
        static const std::string kNothing = "NOTHING YET (NEEDS SPAWNS)";
        const bool none = edModesText_.empty();
        const std::string& modes = none ? kNothing : edModesText_;
        char stats[96];
        snprintf(stats, sizeof stats, "%d BOXES  %d SPAWNS  %d LIGHTS", (int)ed_.brushes.size(), (int)ed_.spawns.size(), (int)ed_.lights.size());
        static const std::string kPlayable = "PLAYABLE AS:", kTeams = "(TEAM MODES NEED A + B SPAWNS)";
        const std::string statsS = stats;
        const std::string* seg[4] = {&kPlayable, &modes, &kTeams, &statsS};
        const Color segC[4] = {kGrey, none ? kRed : kWhite, kGrey, kGrey};
        float segX[4], at = 0;
        int segL[4], line = 0;
        for (int k = 0; k < 4; k++) {   // (12 apart; one that doesn't fit starts the next line)
            const float tw = r_.textWidth(*seg[k], hs);
            if (at > 0 && at + tw > maxW) { line++; at = 0; }
            segX[k] = at; segL[k] = line;
            at += tw + 12 * s;
        }
        const int nStatus = line + 1;
        const std::vector<std::string>& help = edHelpLines(r_, maxW, hs);
        const int n = nStatus + (int)help.size();
        const float top = H - (n * 14 + 14) * s;
        edBarTop_ = top;
        r_.rect(0, top, panelX, H - top, {0, 0, 0, 0.45f});
        float ly = top + 8 * s;
        for (int k = 0; k < 4; k++) ui_.hint(lx + segX[k], ly + segL[k] * lh, *seg[k], segC[k], maxW - segX[k]);
        ly += nStatus * lh;
        for (const std::string& t : help) { ui_.hint(lx, ly, t, kGrey, maxW); ly += lh; }
    }
    ui_.where = nullptr;
    if (edGroupMenu_ || edExportAsk_) drawEditorGroupMenu();
    if (time_ < edMsgUntil_ && !matModal) ui_.labelCentered(panelX * 0.5f, 20 * s, edMsg_, kWhite, 1.0f, panelX - 32 * s);
    if (edOnline_) {   // editing together: the chat (Enter) over the key bar, and any running vote (F1 / F2)
        float ts = std::max(2.0f, std::round(H / 360.0f));
        drawChatBox(panelX, edBarTop_ + 76 * s, s, ts);   // (its SAY line sits 100 above the bottom it's given: 24 over the bar)
        if (net_.hasSnapshot() && net_.mode().voteActive) drawVotePanel(panelX * 0.5f, s, ts);
    }
    if (ui_.probing() && !modal) uiTestActions();   // (the UI test: the 3D view, the selection's handles, the keys)
    if (modal) {
        uiIn_ = std::move(realIn);
        ui_.begin(&r_, uiIn_, s);   // (and the UI test sees the question's widgets again: begin() unmutes)
        if (edPrompt_) drawEditorSavePrompt();
        else if (matModal) drawEditorMatMenu();
    }
}
