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
const Color kRed = {1, 0.35f, 0.3f, 1};
const Color kClipColor = {0.75f, 0.35f, 1.0f, 0.22f};   // clip boxes (invisible in play) in the editor
const float kGrids[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
const char kKindChar[] = "bslzhcpdg"; // ED_* -> edit op kind
const char* kKindNames[] = {"BOX", "SPAWN", "LIGHT", "BOMB SITE", "HILL", "CHECKPOINT", "PAYLOAD PATH POINT", "LADDER", "GROUP"};
const char* kMatNames[] = {"box", "accent", "wallbang", "metal", "glow", "far", "water", "lava", "trim", "clip"};   // (group files)
static_assert(sizeof kMatNames / sizeof *kMatNames == kMapMaterials, "a name per material");
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

void moveObject(Map& m, int kind, int i, const Vec3& d) {
    if (AABB* b = kindBox(m, kind, i)) { b->mn += d; b->mx += d; return; }
    if (kind == 1) m.spawns[i].pos += d;
    else if (kind == 2) m.lights[i].pos += d;
    else if (kind == 6) m.payloadPath[i] += d;
}

// The "ins" op for object i, plus (a custom group) the ops that add its parts back.
std::string insertOps(const Map& m, char k, int i) {
    std::string o = describeMapObject(m, k, "ins", i);
    if (k == 'g')
        for (auto& p : m.groups[i].parts) {
            char t[200];
            snprintf(t, sizeof t, "\ng part %d %d %.7g %.7g %.7g %.7g %.7g %.7g", i, p.material, p.box.mn.x, p.box.mn.y, p.box.mn.z, p.box.mx.x, p.box.mx.y, p.box.mx.z);
            o += t;
        }
    return o;
}

// ---- group files: "quark-group 1", "size sx sy sz", "type custom|room|stairs material dir", then (custom) one
// "<material> x0 y0 z0 x1 y1 z1" per box, in metres from the group's low corner (so it pastes anywhere)
std::string groupDir() { return userDataPath("groups"); }

std::string groupText(const Map::Group& g) {
    Vec3 sz = g.box.mx - g.box.mn;
    char t[200];
    snprintf(t, sizeof t, "quark-group 1\nsize %g %g %g\ntype %s %d %d\n", sz.x, sz.y, sz.z, kGroupTypes[g.type], g.material, g.dir);
    std::string o = t;
    for (auto& p : g.parts) {
        Vec3 a(p.box.mn.x * sz.x, p.box.mn.y * sz.y, p.box.mn.z * sz.z), c(p.box.mx.x * sz.x, p.box.mx.y * sz.y, p.box.mx.z * sz.z);
        snprintf(t, sizeof t, "%s %g %g %g %g %g %g\n", kMatNames[p.material], a.x, a.y, a.z, c.x, c.y, c.z);
        o += t;
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
        g.parts.push_back({{{lo.x / sz.x, lo.y / sz.y, lo.z / sz.z}, {hi.x / sz.x, hi.y / sz.y, hi.z / sz.z}}, mat});
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
// the rest), exactly as the server and the session clients rely on, so there's nothing to rebuild here.
void Game::edChanged() {
    edDirty_ = true;
    edMeshDirty_ = true;
    if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
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
            Map tmp = ed_;
            for (auto& one : ops) if (!(ok = applyMapEdit(tmp, one))) break;
            if (ok) ed_ = std::move(tmp);
        }
        if (!ok) { edNotify("can't do that"); return; }   // (and no undo step for what didn't happen)
        edChanged();
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
    Map tmp = ed_;
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
    tmp = m;
    for (auto& e : sel) {
        switch (e.kind) {
            case ED_BRUSH: tmp.brushes[e.idx].box = box(m.brushes[e.idx].box, c); break;
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
                for (auto& p : g.parts) p.box = box(p.box, Vec3(0.5f, 0.5f, 0.5f));   // (parts: 0-1 fractions of the box)
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
    Map tmp = ed_;
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

// New object at the spot under the screen center (on top of whatever you're looking at), snapped to the grid.
void Game::edAdd(int kind, int variant) {
    const float g = kGrids[edGridIdx_];
    Vec3 dir = angleForward(yaw_, pitch_), n;
    float t = ed_.raycast(edCam_, dir, 200, &n);
    Vec3 at = t >= 0 ? edCam_ + dir * t + n * 0.001f : edCam_ + dir * 8.0f;
    at = snapTo(at, g);
    if (t >= 0 && n.y < -0.5f) at.y -= 1; // hit a ceiling: hang below it
    Map tmp = ed_;
    switch (kind) {
        case ED_BRUSH: tmp.brushes.insert(tmp.brushes.begin() + tmp.editableBrushes(), Brush{{at - Vec3(1, 0, 1), at + Vec3(1, 1, 1)}, edMat_}); break;
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
        g.parts.push_back({{{a.x / sz.x, a.y / sz.y, a.z / sz.z}, {c.x / sz.x, c.y / sz.y, c.z / sz.z}}, p.material});   // (every material, trim and clip too)
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
    Map tmp = ed_;
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
        snprintf(t, sizeof t, "b ins %d %d %.9g %.9g %.9g %.9g %.9g %.9g\n", n + (int)k, parts[k].material, b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
        op += t;
        undo = "b del " + std::to_string(n + (int)k) + "\n" + undo;
    }
    op += "g del " + std::to_string(gi);
    edDo(op, undo);
    edSel_ = EdSel();
    edMulti_.clear();
    for (size_t k = 0; k < parts.size(); k++) (k ? edMulti_.push_back({ED_BRUSH, n + (int)k}) : (void)(edSel_ = {ED_BRUSH, n}));
    edNotify("ungrouped into " + std::to_string(parts.size()) + " boxes");
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
    Map tmp = ed_;
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

// IMPORT GROUP's list (newest first, click one to place it) / EXPORT's question, over the 3D view
void Game::drawEditorGroupMenu(float x, float y, float w) {
    float s = ui_.s(), h = 28 * s, gap = 5 * s;
    if (edExportAsk_) {
        ui_.panel(x - 12 * s, y - 12 * s, w + 24 * s, 2 * h + 3 * gap + 40 * s, 0.92f);
        ui_.labelCentered(x + w * 0.5f, y, "NOT A GROUP - MAKE IT A GROUP FIRST?", kWhite, 0.7f);
        y += 24 * s;
        float bw = (w - gap) * 0.5f;
        if (ui_.button(x, y, bw, h, "YES", UiExp::state("groups"))) edExportGroup();
        if (ui_.button(x + bw + gap, y, bw, h, "NO", UiExp::nav(edPath()))) edExportAsk_ = false;
        return;
    }
    int rows = std::min<int>((int)edGroupFiles_.size(), 12);
    ui_.panel(x - 12 * s, y - 12 * s, w + 24 * s, (rows + 2) * (h + gap) + (edGroupFiles_.empty() ? 50 : 34) * s, 0.92f);
    ui_.labelCentered(x + w * 0.5f, y, "CUSTOM GROUPS", kWhite);
    y += 24 * s;
    if (edGroupFiles_.empty()) {
        ui_.labelCentered(x + w * 0.5f, y, "NONE YET - EXPORT ONE, OR COPY A GROUP AND IMPORT", kGrey, 0.5f);
        y += 16 * s;
    }
    for (int i = 0; i < rows; i++, y += h + gap) {
        std::string name = std::filesystem::path(edGroupFiles_[i]).stem().string();
        for (auto& c : name) c = (char)toupper((unsigned char)c);
        if (ui_.button(x, y, w, h, name, UiExp::edit().withId("GROUP FILE"))) {
            edGroupMenu_ = false;
            if (!edPlaceGroup(readFile(edGroupFiles_[i]))) edNotify("not a quark group file");
            return;
        }
    }
    float bw = (w - gap) * 0.5f;
    if (ui_.button(x, y, bw, h, "OPEN FOLDER", UiExp::url())) openGroupFolder();
    if (ui_.button(x + bw + gap, y, bw, h, "CLOSE", UiExp::nav(edPath()))) edGroupMenu_ = false;
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
    if (!edSelValid()) return hs;
    AABB b = edBounds(ed_, edSel_);
    Vec3 c = b.center();
    bool point = edSel_.kind == ED_SPAWN || edSel_.kind == ED_LIGHT || edSel_.kind == ED_PATH;
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
    if (key == GLFW_KEY_RIGHT_BRACKET) { edGridIdx_ = std::min(4, edGridIdx_ + 1); return; }
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
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f;
    const bool inEditor = screen_ == Screen::Editor;
    float w = 500 * s, h = 34 * s, x = (inEditor ? W - 320 * s : W) * 0.5f - w * 0.5f, y = H * 0.35f, cx = x + w * 0.5f;
    std::string title = edPromptMap_.title.empty() ? edPromptMap_.name : edPromptMap_.title;
    for (auto& c : title) c = (char)toupper((unsigned char)c);
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 206 * s, 0.94f);
    if (edPromptOfficial_) {
        ui_.labelCentered(cx, y, "KEEP YOUR EDITS AS A NEW MAP?", kWhite);
        y += 26 * s;
        ui_.label(x, y, title + " IS A BUILT-IN MAP: IT STAYS AS IT IS. YES SAVES YOUR EDITS AS YOUR", kGrey, 0.5f);
        y += 12 * s;
        ui_.label(x, y, "OWN MAP, IN YOUR MAPS FOLDER UNDER THIS NAME. NO DROPS THEM.", kGrey, 0.5f);
    } else {
        ui_.labelCentered(cx, y, "SAVE YOUR COPY OF " + title + "?", kWhite);
        y += 26 * s;
        ui_.label(x, y, "SAVES TO MAPS/<NAME>.QMAP - YOUR OWN COPY, THE HOST KEEPS THEIRS", kGrey, 0.5f);
        y += 12 * s;
    }
    y += 16 * s;
    if (ui_.textField(42, x, y, w, h, edPromptName_, 40, UiExp::local().withId("COPY NAME"))) edPromptName_ = typedMapName(edPromptName_);
    y += h + 10 * s;
    const bool cancel = inEditor && edPromptThen_ != ED_THEN_NONE;   // (something to go back to)
    const int nb = cancel ? 3 : 2;
    float bw = (w - (nb - 1) * 10 * s) / nb;
    // (what each answer leads to, for the UI test: yes writes the new map - your own session carries on under it -,
    // no / cancel go where the question was asked from)
    const int then = edPromptThen_;
    static const char* kThen[] = {"after a session", "save", "leave", "test", "host", "open"};
    if (ui_.probing()) ui_.scope = kThen[std::min(std::max(edPromptThen_, 0), 5)];   // (the UI test: each "then" leads elsewhere)
    const UiExp yes = inEditor && then == ED_THEN_SAVE && edOnline_ && host_ ? UiExp::cmd("editmode saveas") : UiExp::state("usermaps");
    const UiExp no = !inEditor || then == ED_THEN_LEAVE ? UiExp::nav("main") : then == ED_THEN_TEST || then == ED_THEN_HOST ? UiExp::state("hosting", "1")
                   : then == ED_THEN_OPEN ? UiExp::state("edopen") : UiExp::nav(edPath());
    if (ui_.button(x, y, bw, h, edPromptOfficial_ ? "YES, SAVE AS NEW MAP" : "SAVE", yes.withId("YES"))) edPromptAnswer(1);
    else if (ui_.button(x + bw + 10 * s, y, bw, h, edPromptOfficial_ && !edOnline_ ? "NO, DROP MY EDITS" : "DON'T SAVE", no.withId("NO"))) edPromptAnswer(0);
    else if (cancel && ui_.button(x + 2 * (bw + 10 * s), y, bw, h, "CANCEL", UiExp::nav(edPath()))) edPromptAnswer(-1);
    y += h + 8 * s;
    if (time_ < edMsgUntil_) ui_.labelCentered(cx, y, edMsg_, kGrey, 0.6f);
}

void Game::editorFrame(double dt) {
    const float W = (float)r_.width(), H = (float)r_.height();
    const float s = H / 720.0f, g = kGrids[edGridIdx_];
    const float panelW = 320 * s, panelX = W - panelW;
    if (edOnline_) {
        // follow the session's map; keep the connection fed and tell the others where our camera is
        if (net_.mapRevision() != edRev_) {
            ed_ = net_.map();
            edRev_ = net_.mapRevision();
            edMeshDirty_ = true;
            if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
        }
        for (auto& e : net_.takeEvents()) {
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
    // the save question, when it's up, is the only thing the mouse and typing reach (drawn last, over everything)
    const bool modal = edPrompt_;
    UiInput realIn = uiIn_;
    if (modal) { uiIn_.pressed = uiIn_.rpressed = uiIn_.down = false; uiIn_.chars.clear(); uiIn_.backspace = uiIn_.enter = uiIn_.tab = false; }
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
    auto closestOnAxis = [&](const Vec3& p0, int axis, const Vec3& d) { // param along the axis nearest the mouse ray
        Vec3 a;
        a[axis] = 1;
        Vec3 w = p0 - edCam_;
        float bb = dot(a, d), den = 1 - bb * bb;
        return den < 1e-4f ? 0.0f : (bb * dot(d, w) - dot(a, w)) / den;
    };
    // ---- click: grab a handle of the selection, else select what's under the mouse
    if (uiIn_.pressed && uiIn_.mx < panelX && !edGroupMenu_ && !edExportAsk_ && !edPrompt_) {
        ui_.clearFocus();
        bool shiftHeld = glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        // the spot the last selecting click was on, same view: this click steps along the ray (below), even where
        // the selection's handle now sits under the mouse
        bool again = !shiftHeld && std::hypot(uiIn_.mx - edLastClickX_, uiIn_.my - edLastClickY_) < 4 * s && length(edCam_ - edLastClickCam_) < 1e-4f &&
                     std::fabs(yaw_ - edLastClickYaw_) < 1e-5f && std::fabs(pitch_ - edLastClickPitch_) < 1e-5f;
        int best = -1;
        float bestD = 14 * s;
        auto hs = edHandles();
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
            edDragBase_ = ed_;
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
    if (edDragAxis_ >= 0 && (size_t)edSel_.idx < kindCount(edDragBase_, edSel_.kind)) {
        float delta = snapTo(closestOnAxis(edDragP0_, edDragAxis_, edMouseRay()) - edDragT0_, g);
        delta = clampf(delta, -500, 500);
        Map tmp = edDragBase_;
        int kind = edSel_.kind, i = edSel_.idx, a = edDragAxis_;
        if (edDragMove_) {
            Vec3 d;
            d[a] = delta;
            moveObject(tmp, kind, i, d);
        } else if (AABB* b = kindBox(tmp, kind, i)) {
            if (edDragSign_ > 0) b->mx[a] = std::max(b->mn[a] + g, b->mx[a] + delta);
            else b->mn[a] = std::min(b->mx[a] - g, b->mn[a] + delta);
        }
        char k = kKindChar[kind];
        std::string op = describeMapObject(tmp, k, "set", i);
        if (op != edDragLast_ && !(edDragLast_.empty() && op == describeMapObject(edDragBase_, k, "set", i))) {
            if (!edDragPushed_) { edUndo_.push_back(describeMapObject(edDragBase_, k, "set", i)); edDragPushed_ = true; }
            edDo(op, "", false, false);
            edDragLast_ = op;
        }
    }
    if (edSel_.valid() && !edSelValid() && !edOnline_) edSel_ = EdSel(); // (online: a new object may not have arrived yet)
    if (!edOnline_) edMulti_.erase(std::remove_if(edMulti_.begin(), edMulti_.end(), [&](EdSel e) { return (size_t)e.idx >= kindCount(ed_, e.kind); }), edMulti_.end());

    // ---- 3D view
    const Vec3 matColor[6] = {r_.baseColor, r_.accentColor, {0.22f, 0.22f, 0.24f}, r_.metalColor, r_.glowColor, r_.metalColor * 0.7f};
    r_.worldOutline = cfg_.outlineWorld;   // (the map's boxes, baked or not, take the world outline width)
    if (!edBaked_) {   // the map's boxes: one static batch, rebuilt only when they change (it was streamed every frame)
        if (edMeshDirty_ || edMeshOutline_ != cfg_.outlineWorld) {
            r_.outlineWidth = cfg_.outlineWorld;
            r_.setStaticBoxes([&] {
                for (auto& b : ed_.brushes) { // (water, lava and trim, 6-8, past the table: blue, orange, accent; clip below)
                    if (b.material == 9) continue;
                    Vec3 c = b.material < 6 ? matColor[b.material] : b.material == 6 ? Vec3(0.2f, 0.4f, 0.8f) : b.material == 7 ? Vec3(1.0f, 0.4f, 0.1f) : r_.accentColor;
                    r_.box(b.box.center(), (b.box.mx - b.box.mn) * 0.5f, 0, c, 1.0f, false);
                }
            });
            r_.outlineWidth = 1.0f;
            edMeshDirty_ = false;
            edMeshOutline_ = cfg_.outlineWorld;
        }
        r_.useStaticBoxes();
    }
    for (auto& b : ed_.brushes) // clip: invisible in play (the baked world leaves it out too), see-through violet here so it can be found and edited
        if (b.material == 9) r_.translucentBox(b.box, kClipColor);
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
        for (auto& hd : edHandles()) {
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
    std::vector<std::pair<Vec3, std::string>> others;
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

    // ---- panel
    ui_.begin(&r_, uiIn_, s);
    if (ui_.probing()) ui_.where = edPath();   // (the UI test: the panel is the editor's, whatever is open over the view)
    ui_.muted = modal;   // (under the save question nothing in the panel takes a click)
    ui_.panel(panelX, 0, panelW, H, 0.85f);
    // (rows of 23 + 4: with a spawn, light or stairs selected, 26 + 5 ran ROTATE / MIRROR off the bottom at 720p - UI test)
    float x = panelX + 12 * s, w = panelW - 24 * s, y = 10 * s, h = 23 * s, gap = 4 * s;
    float w2 = (w - gap) * 0.5f, w3 = (w - 2 * gap) / 3;
    auto row = [&] { y += h + gap; };
    if (edOnline_) {
        int n = 0;
        const auto& ps = framePlayers();
        for (auto& p : ps) n += p.valid;
        ui_.label(x, y, "EDITING TOGETHER (" + std::to_string(n) + ")", ui_.accent);
        y += 22 * s;
        bool temp = net_.hasSnapshot() && (net_.mode().flags & MS_TEMP_EDIT);
        ui_.label(x, y, "MAP " + net_.map().name + (temp ? " - REVERTS WHEN THE MAP CHANGES" : host_ ? " - AUTOSAVED" : " - THE HOST SAVES IT"), kGrey, 0.5f);
        y += 12 * s;
    } else {
        ui_.label(x, y, std::string("MAP EDITOR") + (edDirty_ ? " *" : ""), ui_.accent);
        y += 22 * s;
        ui_.label(x, y, "FILE NAME / TITLE", kGrey, 0.5f);
        y += 12 * s;
    }
    if (!ui_.typing()) edTitle_ = ed_.title;
    {
        // the file name, in a session too (SAVE A COPY / the host's SAVE AS use it). Kept a valid file name as you
        // type: a space becomes _, other characters are dropped (they used to stay, and every save was refused)
        if (ui_.textField(40, x, y, w2, h, edName_, 24, UiExp::local().withId("FILE NAME"))) edName_ = typedMapName(edName_);
        std::string before = edTitle_;
        if (ui_.textField(41, x + w2 + gap, y, w2, h, edTitle_, 32, UiExp::edit().withId("TITLE")) && edTitle_ != before) edDo("t " + edTitle_, "t " + ed_.title, true);
    }
    row();
    {
        static std::vector<std::string> modeNames;
        if (modeNames.empty()) { modeNames.push_back("ANY"); for (int m = 0; m < MODE_COUNT; m++) modeNames.push_back(modeName(m)); }
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
    } else {
        // (a built-in map with your edits: leaving it, saving or testing asks first whether to keep them as a new map)
        const bool ask = edDirty_ && edOfficial(), builtin = isOfficialMapName(edName_);
        if (ui_.button(x, y, w3, h, "NEW", ask ? UiExp::nav(edKeepPath()) : UiExp::state("edopen"))) { if (ask) edAskKeep(ED_THEN_OPEN, ""); else { openEditor(""); return; } }
        if (ui_.button(x + w3 + gap, y, w3, h, "SAVE", builtin ? (edDirty_ ? UiExp::nav(edKeepPath()) : UiExp::state("ednotice")) : UiExp::state("usermaps"))) edSave();
        if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "TEST", builtin && edDirty_ ? UiExp::nav(edKeepPath()) : UiExp::state("hosting", "1"))) { edTest(); if (screen_ != Screen::Editor) return; }
        row();
        if (!maps_.empty()) {
            ui_.cycle(x, y, w - w3 - gap, h, "MAP", edLoadIdx_, (int)maps_.size(), maps_, UiExp::local());
            if (ui_.button(x + w - w3, y, w3, h, "OPEN", ask ? UiExp::nav(edKeepPath()) : UiExp::state("edopen"))) {
                std::string next = maps_[edLoadIdx_ % maps_.size()];
                if (edDirty_ && edOfficial()) edAskKeep(ED_THEN_OPEN, next);
                else { openEditor(next); return; }
            }
            row();
        }
        if (ui_.button(x, y, w, h, "HOST EDIT SESSION", builtin && edDirty_ ? UiExp::nav(edKeepPath()) : UiExp::state("hosting", "1"))) { edHostSession(); if (screen_ != Screen::Editor) return; }
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
    if (ui_.button(x, y, edOnline_ ? w : w2, h, edBaked_ ? "UNBAKE" : "BAKE LIGHT", UiExp::state("edbaked").withId("BAKE LIGHT"))) {
        if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
        else { r_.loadWorld(ed_, [&](float p) { drawBakeProgress(edName_, p); }); edBaked_ = true; }
        edMeshDirty_ = true;   // (the bake sets the map's palette: the boxes' colours)
    }
    if (!edOnline_ && ui_.button(x + w2 + gap, y, w2, h, "EXIT", edDirty_ && edOfficial() ? UiExp::nav(edKeepPath())
                                                                : edDirty_ && time_ > edLeaveArmed_ ? UiExp::state("ednotice") : UiExp::nav("main"))) {
        if (edDirty_ && edOfficial()) edAskKeep(ED_THEN_LEAVE);   // (B3)
        else if (edDirty_ && time_ > edLeaveArmed_) { edLeaveArmed_ = time_ + 2; edNotify("unsaved changes - click EXIT again to leave"); }
        else { closeEditor(); return; }
    }
    row();
    y += 6 * s;
    ui_.label(x, y, "ADD (AT THE SCREEN CENTER)", kGrey, 0.5f);
    y += 12 * s;
    static const std::vector<std::string> mats = {"BASE", "ACCENT", "WALLBANG", "METAL", "GLOW", "FAR", "WATER (WADE IN IT)", "LAVA (KILLS)", "TRIM (NOT SOLID)", "CLIP (INVISIBLE WALL)"};
    {   // rooms, stairs and ladders are one object each: stretch it and its pieces follow (UNGROUP to split a room / stairs)
        float w4 = (w - 3 * gap) / 4;
        if (ui_.button(x, y, w4, h, "BOX", UiExp::edit())) edAdd(ED_BRUSH, 0);
        if (ui_.button(x + w4 + gap, y, w4, h, "ROOM", UiExp::edit())) edAdd(ED_GROUP, Map::GROUP_ROOM);
        if (ui_.button(x + 2 * (w4 + gap), y, w4, h, "STAIRS", UiExp::edit())) edAdd(ED_GROUP, Map::GROUP_STAIRS);
        if (ui_.button(x + 3 * (w4 + gap), y, w4, h, "LADDER", UiExp::edit())) edAdd(ED_LADDER, 0);
        row();
        static const std::vector<std::string> newMats = {"NEW ONES: BASE", "NEW ONES: ACCENT", "NEW ONES: WALLBANG", "NEW ONES: METAL", "NEW ONES: GLOW", "NEW ONES: FAR", "NEW ONES: WATER", "NEW ONES: LAVA", "NEW ONES: TRIM", "NEW ONES: CLIP"};
        ui_.cycle(x, y, w, h, "MATERIAL", edMat_, kMapMaterials, newMats, UiExp::local());
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
        // GROUP / EXPORT GROUP work on selected boxes and groups (greyed out with nothing of the kind selected)
        bool groupSel = edSelValid() && (edSel_.kind == ED_BRUSH || edSel_.kind == ED_GROUP);
        bool oneGroup = edSelValid() && edSel_.kind == ED_GROUP && edMulti_.empty();
        // (grouping every box the map has is refused: a map keeps a box of its own; it says so)
        bool allBoxes = groupSel && !oneGroup && edGroupTakesEveryBox();
        if (ui_.button(x, y, w3, h, oneGroup ? "UNGROUP" : "GROUP", (allBoxes ? UiExp::state("ednotice") : UiExp::edit()).withId("GROUP"), groupSel)) { if (oneGroup) edUngroup(); else edGroupSelection(); }
        if (ui_.button(x + w3 + gap, y, w3, h, "IMPORT GROUP", edGroupMenu_ ? UiExp::nav(edGroupsPath()) : UiExp::state("edimport"))) edImportGroup();
        if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "EXPORT GROUP", oneGroup || edExportAsk_ ? UiExp::state("groups") : UiExp::nav(edExportAskPath()), groupSel)) edExportGroup();
        row();
    }
    {
        static const std::vector<std::string> grids = {"GRID 0.25 M", "GRID 0.5 M", "GRID 1 M", "GRID 2 M", "GRID 4 M"};
        ui_.cycle(x, y, w, h, "GRID ([ ])", edGridIdx_, 5, grids, UiExp::local());
        row();
    }
    {   // which modes this map is offered for: click to allow / disallow (dim = missing pieces, e.g. team spawns)
        ui_.label(x, y, "MODES (CLICK TO ALLOW / BLOCK)", kGrey, 0.5f);
        y += 12 * s;
        const uint32_t all = (1u << MODE_COUNT) - 1;
        uint32_t mask = ed_.allowedModes ? ed_.allowedModes : all;
        float cw = (w - 3 * gap) / 4, ch = 16 * s, cts = std::max(1.0f, std::round(ui_.textScale() * 0.5f));
        for (int m = 0; m < MODE_COUNT; m++) {
            float cx = x + (m % 4) * (cw + gap), cy = y + (m / 4) * (ch + 3 * s);
            bool on = mask >> m & 1, can = ed_.canSupportMode(m);
            bool hv = uiIn_.mx >= cx && uiIn_.mx < cx + cw && uiIn_.my >= cy && uiIn_.my < cy + ch;
            Color bg = on ? Color{ui_.accent.r * 0.55f, ui_.accent.g * 0.55f, ui_.accent.b * 0.55f, can ? 0.95f : 0.45f} : Color{0.12f, 0.13f, 0.16f, 0.9f};
            r_.rect(cx, cy, cw, ch, bg);
            if (hv) r_.rect(cx, cy + ch - 2 * s, cw, 2 * s, ui_.accent);
            std::string t = modeName(m);
            for (auto& c : t) c = (char)toupper((unsigned char)c);
            r_.text(cx + (cw - r_.textWidth(t, cts)) * 0.5f, cy + (ch - r_.textHeight(cts)) * 0.5f, cts, t, on && can ? kWhite : kGrey);
            if (ui_.probing()) ui_.custom(cx, cy, cw, ch, t, (mask ^ (1u << m)) == 0 ? UiExp::state("ednotice") : UiExp::edit());
            if (hv && uiIn_.pressed) {
                uint32_t nm = mask ^ (1u << m);
                if (nm == 0) edNotify("a map needs at least one mode");
                else edDo("m " + std::to_string(nm == all ? 0 : nm), "m " + std::to_string(ed_.allowedModes));
            }
        }
        y += ((MODE_COUNT + 3) / 4) * (ch + 3 * s) + gap;
    }
    y += 6 * s;
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
        ui_.label(x, y, "SELECTED: " + what, ui_.accent, 0.6f);
        y += 16 * s;
        char buf[128];
        if (kind == ED_SPAWN || kind == ED_LIGHT || kind == ED_PATH) {
            Vec3 p = kind == ED_SPAWN ? ed_.spawns[i].pos : kind == ED_LIGHT ? ed_.lights[i].pos : ed_.payloadPath[i];
            snprintf(buf, sizeof buf, "AT %g %g %g", p.x, p.y, p.z);
        } else snprintf(buf, sizeof buf, "%g %g %g  TO  %g %g %g", b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
        ui_.label(x, y, buf, kGrey, 0.5f);
        y += 14 * s;
        if (kind == ED_BRUSH) {
            int m = ed_.brushes[i].material;
            if (ui_.cycle(x, y, w, h, "MATERIAL (M)", m, kMapMaterials, mats, UiExp::edit())) { edMat_ = m; edModify([&](Map& mm) { mm.brushes[i].material = m; }, false); }
            row();
        } else if (kind == ED_GROUP) {
            auto& gr = ed_.groups[i];
            if (gr.type == Map::GROUP_CUSTOM) {
                ui_.label(x, y, std::to_string(gr.parts.size()) + " BOXES, STRETCHED AS ONE - UNGROUP TO EDIT THEM", kGrey, 0.5f);
                y += 14 * s;
            } else {
                int m = gr.material;
                if (ui_.cycle(x, y, w, h, "MATERIAL (M)", m, kMapMaterials, mats, UiExp::edit())) { edMat_ = m; edModify([&](Map& mm) { mm.groups[i].material = m; }, false); }
                row();
            }
            if (gr.type == Map::GROUP_STAIRS) {
                static const std::vector<std::string> dirs = {"+X", "-X", "+Z", "-Z"};
                int d = gr.dir;
                if (ui_.cycle(x, y, w, h, "RISING TOWARD", d, 4, dirs, UiExp::edit())) edModify([&](Map& mm) { mm.groups[i].dir = d; }, false);
                row();
            }
        } else if (kind == ED_SPAWN) {
            static const std::vector<std::string> teams = {"TEAM A", "TEAM B", "ANY TEAM", "RACE"};
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
        {   // rotate 90 degrees about each axis, mirror across each plane (everything selected, about its middle)
            float w3 = (w - 2 * gap) / 3;
            // (the Shift / Ctrl shortcuts are in the help lines: "MIRROR Y (CT+F)" didn't fit its button)
            static const char* rot[] = {"ROTATE Y (R)", "ROTATE X", "ROTATE Z"};
            static const char* mir[] = {"MIRROR X (F)", "MIRROR Z", "MIRROR Y"};
            static const int rotOp[] = {ED_ROT_Y, ED_ROT_X, ED_ROT_Z}, mirOp[] = {ED_MIR_X, ED_MIR_Z, ED_MIR_Y};
            for (int k = 0; k < 3; k++) if (ui_.button(x + k * (w3 + gap), y, w3, h, rot[k], edTransformExp(rotOp[k]))) edTransform(rotOp[k]);
            row();
            for (int k = 0; k < 3; k++) if (ui_.button(x + k * (w3 + gap), y, w3, h, mir[k], edTransformExp(mirOp[k]))) edTransform(mirOp[k]);
            row();
        }
    } else {
        ui_.label(x, y, "CLICK SOMETHING TO SELECT IT (AGAIN: THE ONE BEHIND)", kGrey, 0.5f);
        y += 16 * s;
        // clip boxes are invisible in play and easy to lose in a big map: step through them (selects one and
        // flies the camera back from it, keeping the view direction)
        int nClip = 0;
        for (auto& br : ed_.brushes) nClip += !br.generated && br.material == 9;
        if (nClip && ui_.button(x, y, w, h, "FIND CLIP BOXES (INVISIBLE WALLS): " + std::to_string(nClip), UiExp::state("edsel").withId("FIND CLIP BOXES"))) {
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
        y += h + gap;
    }
    ui_.scope = nullptr;
    // what this map can be played as
    y = std::max(y + 6 * s, H - 150 * s);
    std::string modes;
    for (int m = 0; m < MODE_COUNT; m++) if (ed_.supportsMode(m)) modes += std::string(modes.empty() ? "" : " ") + modeName(m);
    ui_.label(x, y, "PLAYABLE AS:", kGrey, 0.5f);
    y += 12 * s;
    ui_.label(x, y, modes.empty() ? "NOTHING YET (NEEDS SPAWNS)" : modes, modes.empty() ? kRed : kWhite, 0.5f);
    y += 12 * s;
    bool room = y + 32 * s <= H;   // (a long selection panel leaves room for the two notes or none)
    if (room) ui_.label(x, y, "(TEAM MODES NEED A + B SPAWNS)", kGrey, 0.5f);
    y += 18 * s;
    char stats[96];
    snprintf(stats, sizeof stats, "%d BOXES  %d SPAWNS  %d LIGHTS", (int)ed_.brushes.size(), (int)ed_.spawns.size(), (int)ed_.lights.size());
    if (room) ui_.label(x, y, stats, kGrey, 0.5f);

    // help, bottom left
    static const char* help[] = {
        "RIGHT MOUSE + MOVE: LOOK    WASD: FLY    SPACE / C: UP / DOWN    SHIFT: FAST    WHEEL: SPEED",
        "CLICK: SELECT (AGAIN: THE ONE BEHIND, SHIFT: ADD MORE, THEN GROUP)    ARROWS / PGUP / PGDN: MOVE BY GRID (ALONG YOUR VIEW)",
        "SHIFT + ARROWS: GROW THAT SIDE    CTRL + ARROWS: SHRINK IT    M: MATERIAL / TEAM",
        "CTRL+D: DUPLICATE    DEL: DELETE    CTRL+Z: UNDO    CTRL+S: SAVE    ESC: DESELECT / LEAVE",
        "R: ROTATE 90 (SHIFT: ABOUT X, CTRL: ABOUT Z)    F: MIRROR LEFT / RIGHT (SHIFT: FRONT / BACK, CTRL: UPSIDE DOWN)",
    };
    float hy = H - 12 * s - 5 * 13 * s;
    r_.rect(0, hy - 6 * s, panelX, H - hy + 6 * s, {0, 0, 0, 0.45f});
    for (const char* t : help) { ui_.label(10 * s, hy, t, kGrey, 0.5f); hy += 13 * s; }
    ui_.where = nullptr;
    if (edGroupMenu_ || edExportAsk_) drawEditorGroupMenu(panelX * 0.5f - 200 * s, 70 * s, 400 * s);
    if (time_ < edMsgUntil_) ui_.labelCentered(panelX * 0.5f, 20 * s, edMsg_, kWhite);
    if (edOnline_) {   // editing together: the chat (Enter) and any running vote (F1 / F2)
        float ts = std::max(2.0f, std::round(H / 360.0f));
        drawChatBox(panelX, H, s, ts);
        if (net_.hasSnapshot() && net_.mode().voteActive) drawVotePanel(panelX * 0.5f, s, ts);
    }
    if (ui_.probing() && !modal) uiTestActions();   // (the UI test: the 3D view, the selection's handles, the keys)
    if (modal) {
        uiIn_ = realIn;
        ui_.begin(&r_, uiIn_, s);   // (and the UI test sees the question's widgets again: begin() unmutes)
        if (edPrompt_) drawEditorSavePrompt();
    }
}
