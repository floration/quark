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
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kRed = {1, 0.35f, 0.3f, 1};
const float kGrids[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
const char kKindChar[] = "bslzhcpdg"; // ED_* -> edit op kind
const char* kKindNames[] = {"BOX", "SPAWN", "LIGHT", "BOMB SITE", "HILL", "CHECKPOINT", "PAYLOAD PATH POINT", "LADDER", "GROUP"};
const char* kMatNames[] = {"box", "accent", "wallbang", "metal", "glow", "far", "water", "lava"};
const char* kGroupTypes[] = {"custom", "room", "stairs"};
const Vec3 kTeamColors[] = {{0.25f, 0.6f, 1.0f}, {1.0f, 0.35f, 0.25f}, {0.9f, 0.9f, 0.9f}, {0.3f, 1.0f, 0.4f}};

float snapTo(float v, float g) { return std::round(v / g) * g; }
Vec3 snapTo(const Vec3& v, float g) { return {snapTo(v.x, g), snapTo(v.y, g), snapTo(v.z, g)}; }
AABB around(const Vec3& p, const Vec3& half) { return {p - half, p + half}; }

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
            if (!(ls >> t >> g.material >> g.dir) || g.material < 0 || g.material > 5 || g.dir < 0 || g.dir > 3) return false;
            g.type = t == "room" ? Map::GROUP_ROOM : t == "stairs" ? Map::GROUP_STAIRS : Map::GROUP_CUSTOM;
            hasType = t == "room" || t == "stairs" || t == "custom";
            if (!hasType) return false;
            continue;
        }
        int mat = -1;
        for (int k = 0; k < 8; k++) if (kw == kMatNames[k]) mat = k;
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
        std::ofstream f(path, std::ios::binary);
        if (!(f << text)) return "";
        return path;
    }
    return "";
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
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
    else if (cmd == "cam") { in >> edCam_.x >> edCam_.y >> edCam_.z >> yaw_ >> pitch_; yaw_ = deg2rad(yaw_); pitch_ = deg2rad(pitch_); }
    else if (cmd == "openidx") { int i = 0; in >> i; if (!maps_.empty()) openEditor(maps_[i % maps_.size()]); } // exactly what OPEN does
    else if (cmd == "print") {
        printf("edscript: name=%s map=%s title=%s brushes=%zu sel=%d/%d", edName_.c_str(), ed_.name.c_str(), ed_.title.c_str(), ed_.brushes.size(), edSel_.kind, edSel_.idx);
        if (edSelValid()) { AABB b = edBounds(ed_, edSel_); printf(" box=%g %g %g %g %g %g", b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z); }
        printf("\n");
        fflush(stdout);
    } else if (cmd == "quit") quit_ = true;
}

void Game::edNotify(const std::string& s) { edMsg_ = s; edMsgUntil_ = time_ + 3; }

bool Game::edSelValid() const { return edSel_.valid() && (size_t)edSel_.idx < kindCount(ed_, edSel_.kind); }

void Game::openEditor(std::string name) {
    rescanMaps();
    ed_ = Map();
    edUndo_.clear();
    edSel_ = EdSel();
    edMulti_.clear();
    edGroupMenu_ = edExportAsk_ = false;
    edDirty_ = edBaked_ = edOnline_ = false;
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
        // a new map keeps the name it was opened with (it used to fall back to "mymap", so saving overwrote that);
        // a file that's there but won't load (an older / over-limit map) isn't overwritten by the blank one
        if (!isValidMapName(name)) edName_ = "mymap";
        else edName_ = err.rfind("map not found", 0) == 0 ? name : typedMapName(name + "_new");
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
    if (edOnline_) { leaveGame(); return; }
    rescanMaps();
    setScreen(Screen::Main);
    loadMenuBackdrop();
}

void Game::edChanged() {
    ed_.rebuild();
    edDirty_ = true;
    if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
}

// Apply an edit (or send it to the session) and remember how to undo it. coalesce: repeated nudges and
// slider drags within 0.75 s share one undo step.
void Game::edDo(const std::string& op, const std::string& inverse, bool coalesce, bool undoable) {
    if (op.empty() || op == inverse) return;
    if (undoable) {
        if (!(coalesce && time_ - edLastPush_ < 0.75)) {
            edUndo_.push_back(inverse);
            if (edUndo_.size() > 500) edUndo_.erase(edUndo_.begin());
        }
        edLastPush_ = coalesce ? time_ : 0;
    }
    for (size_t a = 0; a < op.size();) {
        size_t e = op.find('\n', a);
        if (e == std::string::npos) e = op.size();
        std::string one = op.substr(a, e - a);
        a = e + 1;
        if (edOnline_) net_.sendCommand("ed " + one);
        else if (applyMapEdit(ed_, one)) edChanged();
        else { edNotify("can't do that"); return; }
    }
}

void Game::edUndoLast() {
    if (edUndo_.empty()) { edNotify("nothing to undo"); return; }
    std::string op = edUndo_.back();
    edUndo_.pop_back();
    edLastPush_ = 0;
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
void Game::edTransform(int op) {
    if (!edSelValid()) return;
    std::vector<EdSel> sel = {edSel_};
    for (auto& m : edMulti_) if (m.kind != edSel_.kind || m.idx != edSel_.idx) sel.push_back(m);
    Map& m = ed_;
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
    if (all.mn.x > all.mx.x) { edNotify("can't turn that"); return; }
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
    Map tmp = m;
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
    bool first = true;
    for (auto& e : sel) {
        char k = kKindChar[e.kind];
        if (e.kind == ED_GROUP && !tmp.groups[e.idx].parts.empty()) {   // a custom group: rewrite it whole (its parts follow)
            std::string del = "g del " + std::to_string(e.idx) + "\n";
            edDo(del + insertOps(tmp, k, e.idx), del + insertOps(m, k, e.idx), !first);
            first = false;
            continue;
        }
        if (e.kind == ED_GROUP || e.kind == ED_BRUSH || e.kind == ED_LADDER || e.kind == ED_CHECK || e.kind == ED_LIGHT || e.kind == ED_PATH || e.kind == ED_SPAWN) {
            edDo(describeMapObject(tmp, k, "set", e.idx), describeMapObject(m, k, "set", e.idx), !first);
            first = false;
        }
    }
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
        case ED_LIGHT: tmp.lights.push_back({at + Vec3(0, 2, 0), 10, 1}); break;
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
        g.parts.push_back({{{a.x / sz.x, a.y / sz.y, a.z / sz.z}, {c.x / sz.x, c.y / sz.y, c.z / sz.z}}, std::min(p.material, 5)});
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

void Game::edGroupSelection() {
    auto sel = groupable(ed_, edSel_, edMulti_);
    if (sel.size() == 1 && sel[0].kind == ED_GROUP) { edNotify("that's already a group"); return; }
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
        if (ui_.button(x, y, bw, h, "YES")) edExportGroup();
        if (ui_.button(x + bw + gap, y, bw, h, "NO")) edExportAsk_ = false;
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
        if (ui_.button(x, y, w, h, name)) {
            edGroupMenu_ = false;
            if (!edPlaceGroup(readFile(edGroupFiles_[i]))) edNotify("not a quark group file");
            return;
        }
    }
    float bw = (w - gap) * 0.5f;
    if (ui_.button(x, y, bw, h, "OPEN FOLDER")) openGroupFolder();
    if (ui_.button(x + bw + gap, y, bw, h, "CLOSE")) edGroupMenu_ = false;
}

Game::EdSel Game::edPick(const Vec3& o, const Vec3& dir) const {
    Vec3 inv = safeInv(dir);
    EdSel best;
    float bestT = 1e9f;
    auto test = [&](int kind, int idx, const AABB& b, float bias) {
        float t = rayAABB(o, inv, b, 1000);
        if (t >= 0 && t + bias < bestT) { bestT = t + bias; best = {kind, idx}; }
    };
    // markers win over the box they sit on / inside (zones are see-through, so they only win when nothing's closer)
    for (size_t i = 0; i < ed_.spawns.size(); i++) test(ED_SPAWN, (int)i, edBounds(ed_, {ED_SPAWN, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.lights.size(); i++) test(ED_LIGHT, (int)i, edBounds(ed_, {ED_LIGHT, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.payloadPath.size(); i++) test(ED_PATH, (int)i, edBounds(ed_, {ED_PATH, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.brushes.size(); i++) if (!ed_.brushes[i].generated) test(ED_BRUSH, (int)i, ed_.brushes[i].box, 0);
    for (size_t i = 0; i < ed_.ladders.size(); i++) test(ED_LADDER, (int)i, ed_.ladders[i], -0.3f);
    for (size_t i = 0; i < ed_.groups.size(); i++) { // a group: where you hit one of its boxes (a room's inside is open)
        std::vector<Brush> parts;
        Map::groupBoxes(ed_.groups[i], parts);
        for (auto& p : parts) test(ED_GROUP, (int)i, p.box, 0);
    }
    for (size_t i = 0; i < ed_.bombSites.size(); i++) test(ED_SITE, (int)i, ed_.bombSites[i], 0.25f);
    for (size_t i = 0; i < ed_.checkpoints.size(); i++) test(ED_CHECK, (int)i, ed_.checkpoints[i], 0.25f);
    if (ed_.hasHill) test(ED_HILL, 0, ed_.hill, 0.25f);
    return best;
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
    std::string text = m.serialize();
    Map check;
    std::string err;
    if (!check.parse(text, &err)) { edNotify("can't save: " + err); return false; }
    if (!saveMapFile(userMapsDir() + "/" + name + ".qmap", text)) { edNotify("could not write maps/" + name + ".qmap"); return false; }
    rescanMaps();
    edNotify("saved maps/" + name + ".qmap");
    return true;
}

bool Game::edSave() {
    if (!edSaveCopy(ed_, edName_)) return false;
    edDirty_ = false;
    return true;
}

void Game::edTest() {
    if (!edSave()) return;
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
    if (!edSave()) return;
    testMap_ = edName_;
    testMode_ = MODE_FFA;
    testEdit_ = true;
    edLook_ = false;
    startHost(false);
}

void Game::editorKey(int key, int mods) {
    bool ctrl = (mods & GLFW_MOD_CONTROL) || (mods & GLFW_MOD_SUPER), shift = mods & GLFW_MOD_SHIFT;
    const float g = kGrids[edGridIdx_];
    if (key == GLFW_KEY_ESCAPE) {
        if (edLook_) { edLook_ = false; captureMouse(false); return; }
        if (edGroupMenu_ || edExportAsk_) { edGroupMenu_ = edExportAsk_ = false; return; }
        if (edSel_.valid()) { edSel_ = EdSel(); edMulti_.clear(); return; }
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
            if (kind == ED_BRUSH) edMat_ = m.brushes[i].material = (m.brushes[i].material + 1) % 8;
            else if (kind == ED_GROUP && m.groups[i].type != Map::GROUP_CUSTOM) edMat_ = m.groups[i].material = (m.groups[i].material + 1) % 8;
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
            if (sign > 0) b->mx[axis] = std::max(b->mn[axis] + g, b->mx[axis] + amount);
            else b->mn[axis] = std::min(b->mx[axis] - g, b->mn[axis] - amount);
        } else {
            Vec3 d;
            d[axis] = sign * g;
            moveObject(m, kind, i, d);
        }
    }, true);
}

void Game::drawEditorSavePrompt() {
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f;
    float w = 460 * s, h = 34 * s, x = W * 0.5f - w * 0.5f, y = H * 0.35f;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 190 * s, 0.92f);
    ui_.labelCentered(W * 0.5f, y, "SAVE YOUR COPY OF " + edPromptMap_.title + "?", kWhite);
    y += 26 * s;
    ui_.label(x, y, "SAVES TO MAPS/<NAME>.QMAP - YOUR OWN COPY, THE HOST KEEPS THEIRS", kGrey, 0.5f);
    y += 16 * s;
    ui_.textField(42, x, y, w, h, edPromptName_, 40);
    y += h + 10 * s;
    float bw = (w - 10 * s) * 0.5f;
    if (ui_.button(x, y, bw, h, "SAVE")) { if (edSaveCopy(edPromptMap_, edPromptName_)) edPrompt_ = false; }
    if (ui_.button(x + bw + 10 * s, y, bw, h, "DON'T SAVE")) edPrompt_ = false;
    y += h + 8 * s;
    if (time_ < edMsgUntil_) ui_.labelCentered(W * 0.5f, y, edMsg_, kGrey, 0.6f);
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
        }
        if (time_ >= edNextCam_ && net_.pendingCommands() < 8) {
            char buf[96];
            snprintf(buf, sizeof buf, "edcam %.2f %.2f %.2f", edCam_.x, edCam_.y - kEyeHeight, edCam_.z);
            net_.sendCommand(buf);
            edNextCam_ = time_ + 0.15;
        }
    }
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
    r_.resize((int)panelX, (int)H);
    r_.setCamera(edCam_, yaw_, pitch_, cfg_.fov);
    auto closestOnAxis = [&](const Vec3& p0, int axis, const Vec3& d) { // param along the axis nearest the mouse ray
        Vec3 a;
        a[axis] = 1;
        Vec3 w = p0 - edCam_;
        float bb = dot(a, d), den = 1 - bb * bb;
        return den < 1e-4f ? 0.0f : (bb * dot(d, w) - dot(a, w)) / den;
    };
    // ---- click: grab a handle of the selection, else select what's under the mouse
    if (uiIn_.pressed && uiIn_.mx < panelX && !edGroupMenu_ && !edExportAsk_) {
        ui_.clearFocus();
        int best = -1;
        float bestD = 14 * s;
        auto hs = edHandles();
        for (size_t k = 0; k < hs.size(); k++) {
            float sx, sy;
            if (!r_.project(hs[k].pos, sx, sy)) continue;
            float dd = std::hypot(sx - uiIn_.mx, sy - uiIn_.my);
            if (dd < bestD) { bestD = dd; best = (int)k; }
        }
        if (best >= 0 && !edLook_) {
            edDragAxis_ = hs[best].axis;
            edDragSign_ = hs[best].sign;
            edDragMove_ = hs[best].move || glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            edDragP0_ = hs[best].pos;
            edDragT0_ = closestOnAxis(edDragP0_, edDragAxis_, edMouseRay());
            edDragBase_ = ed_;
            edDragLast_.clear();
            edDragPushed_ = false;
        } else { // shift+click adds boxes / groups to the selection (or takes one back out)
            EdSel p = edPick(edCam_, edMouseRay());
            auto multi = [](EdSel e) { return e.kind == ED_BRUSH || e.kind == ED_GROUP; };
            auto same = [&](EdSel e) { return e.kind == p.kind && e.idx == p.idx; };
            if (glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && multi(p) && edSelValid() && multi(edSel_)) {
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
    if (!edBaked_)
        for (auto& b : ed_.brushes) { // (water, lava and trim, 6-8, past the table: blue, orange, accent)
            Vec3 c = b.material < 6 ? matColor[b.material] : b.material == 6 ? Vec3(0.2f, 0.4f, 0.8f) : b.material == 7 ? Vec3(1.0f, 0.4f, 0.1f) : r_.accentColor;
            r_.box(b.box.center(), (b.box.mx - b.box.mn) * 0.5f, 0, c, 1.0f, false);
        }
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
        auto ps = net_.interpolatedPlayers();
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
    ui_.panel(panelX, 0, panelW, H, 0.85f);
    float x = panelX + 12 * s, w = panelW - 24 * s, y = 10 * s, h = 26 * s, gap = 5 * s;
    float w2 = (w - gap) * 0.5f, w3 = (w - 2 * gap) / 3;
    auto row = [&] { y += h + gap; };
    if (edOnline_) {
        int n = 0;
        auto ps = net_.interpolatedPlayers();
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
        if (ui_.textField(40, x, y, w2, h, edName_, 24)) edName_ = typedMapName(edName_);
        std::string before = edTitle_;
        if (ui_.textField(41, x + w2 + gap, y, w2, h, edTitle_, 32) && edTitle_ != before) edDo("t " + edTitle_, "t " + ed_.title, true);
    }
    row();
    {
        static std::vector<std::string> modeNames;
        if (modeNames.empty()) { modeNames.push_back("ANY"); for (int m = 0; m < MODE_COUNT; m++) modeNames.push_back(modeName(m)); }
        int idx = ed_.preferredMode + 1;
        if (ui_.cycle(x, y, w, h, "BUILT FOR", idx, MODE_COUNT + 1, modeNames))
            edDo("f " + std::to_string(idx - 1), "f " + std::to_string(ed_.preferredMode));
        row();
    }
    if (edOnline_) {
        bool temp = net_.hasSnapshot() && (net_.mode().flags & MS_TEMP_EDIT);
        if (host_) {
            if (ui_.button(x, y, w2, h, "PLAY MODE")) net_.sendCommand("editmode off"); // everyone plays it; ESC menu has EDIT MODE
        } else if (ui_.button(x, y, w2, h, "VOTE: PLAY IT")) net_.sendCommand("callvote edit off");
        if (ui_.button(x + w2 + gap, y, w2, h, host_ && !temp ? "END SESSION" : "LEAVE")) { closeEditor(); return; }
        row();
        if (temp || !host_) {
            if (ui_.button(x, y, w, h, "SAVE A COPY AS " + edName_)) edSaveCopy(ed_, edName_);
        } else if (ui_.button(x, y, w, h, "SAVE AS " + edName_)) {
            if (!isValidMapName(edName_)) edNotify("name: letters, digits, - and _ only");
            else net_.sendCommand("editmode saveas " + edName_); // your session's file: saved under this name from now on
        }
        row();
    } else {
        if (ui_.button(x, y, w3, h, "NEW")) { openEditor(""); return; }
        if (ui_.button(x + w3 + gap, y, w3, h, "SAVE")) edSave();
        if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "TEST")) { edTest(); if (screen_ != Screen::Editor) return; }
        row();
        if (!maps_.empty()) {
            ui_.cycle(x, y, w - w3 - gap, h, "MAP", edLoadIdx_, (int)maps_.size(), maps_);
            if (ui_.button(x + w - w3, y, w3, h, "OPEN")) { openEditor(maps_[edLoadIdx_ % maps_.size()]); return; }
            row();
        }
        if (ui_.button(x, y, w, h, "HOST EDIT SESSION")) { edHostSession(); if (screen_ != Screen::Editor) return; }
        row();
        if (ui_.button(x, y, w2, h, "MAPS FOLDER")) openUrl(mapsFolder());
        if (ui_.button(x + w2 + gap, y, w2, h, time_ < edResetArmed_ ? "CLICK TO CONFIRM" : "RESET MAPS")) {
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
    if (ui_.button(x, y, edOnline_ ? w : w2, h, edBaked_ ? "UNBAKE" : "BAKE LIGHT")) {
        if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
        else { r_.loadWorld(ed_, [&](float p) { drawBakeProgress(edName_, p); }); edBaked_ = true; }
    }
    if (!edOnline_ && ui_.button(x + w2 + gap, y, w2, h, "EXIT")) {
        if (edDirty_ && time_ > edLeaveArmed_) { edLeaveArmed_ = time_ + 2; edNotify("unsaved changes - click EXIT again to leave"); }
        else { closeEditor(); return; }
    }
    row();
    y += 6 * s;
    ui_.label(x, y, "ADD (AT THE SCREEN CENTER)", kGrey, 0.5f);
    y += 12 * s;
    static const std::vector<std::string> mats = {"BASE", "ACCENT", "WALLBANG", "METAL", "GLOW", "FAR", "WATER (WADE IN IT)", "LAVA (KILLS)"};
    {   // rooms, stairs and ladders are one object each: stretch it and its pieces follow (UNGROUP to split a room / stairs)
        float w4 = (w - 3 * gap) / 4;
        if (ui_.button(x, y, w4, h, "BOX")) edAdd(ED_BRUSH, 0);
        if (ui_.button(x + w4 + gap, y, w4, h, "ROOM")) edAdd(ED_GROUP, Map::GROUP_ROOM);
        if (ui_.button(x + 2 * (w4 + gap), y, w4, h, "STAIRS")) edAdd(ED_GROUP, Map::GROUP_STAIRS);
        if (ui_.button(x + 3 * (w4 + gap), y, w4, h, "LADDER")) edAdd(ED_LADDER, 0);
        row();
        static const std::vector<std::string> newMats = {"NEW ONES: BASE", "NEW ONES: ACCENT", "NEW ONES: WALLBANG", "NEW ONES: METAL", "NEW ONES: GLOW", "NEW ONES: FAR", "NEW ONES: WATER", "NEW ONES: LAVA"};
        ui_.cycle(x, y, w, h, "MATERIAL", edMat_, 8, newMats);
        row();
    }
    if (ui_.button(x, y, w3, h, "SPAWN")) edAdd(ED_SPAWN, SPAWN_ANY);
    if (ui_.button(x + w3 + gap, y, w3, h, "LIGHT")) edAdd(ED_LIGHT, 0);
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "PATH PT")) edAdd(ED_PATH, 0);
    row();
    if (ui_.button(x, y, w3, h, "BOMB SITE")) edAdd(ED_SITE, 0);
    if (ui_.button(x + w3 + gap, y, w3, h, "HILL")) edAdd(ED_HILL, 0);
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "CHECKPT")) edAdd(ED_CHECK, 0);
    row();
    {
        bool oneGroup = edSelValid() && edSel_.kind == ED_GROUP && edMulti_.empty();
        if (ui_.button(x, y, w3, h, oneGroup ? "UNGROUP" : "GROUP")) { if (oneGroup) edUngroup(); else edGroupSelection(); }
        if (ui_.button(x + w3 + gap, y, w3, h, "IMPORT GROUP")) edImportGroup();
        if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "EXPORT GROUP")) edExportGroup();
        row();
    }
    {
        static const std::vector<std::string> grids = {"GRID 0.25 M", "GRID 0.5 M", "GRID 1 M", "GRID 2 M", "GRID 4 M"};
        ui_.cycle(x, y, w, h, "GRID ([ ])", edGridIdx_, 5, grids);
        row();
    }
    {   // which modes this map is offered for: click to allow / disallow (dim = missing pieces, e.g. team spawns)
        ui_.label(x, y, "MODES (CLICK TO ALLOW / BLOCK)", kGrey, 0.5f);
        y += 12 * s;
        const uint32_t all = (1u << MODE_COUNT) - 1;
        uint32_t mask = ed_.allowedModes ? ed_.allowedModes : all;
        float cw = (w - 3 * gap) / 4, ch = 18 * s, cts = std::max(1.0f, std::round(ui_.textScale() * 0.5f));
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
            if (hv && uiIn_.pressed) {
                uint32_t nm = mask ^ (1u << m);
                if (nm == 0) edNotify("a map needs at least one mode");
                else edDo("m " + std::to_string(nm == all ? 0 : nm), "m " + std::to_string(ed_.allowedModes));
            }
        }
        y += ((MODE_COUNT + 3) / 4) * (ch + 3 * s) + gap;
    }
    y += 6 * s;
    if (edSelValid()) {
        AABB b = edBounds(ed_, edSel_);
        int kind = edSel_.kind, i = edSel_.idx;
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
            if (ui_.cycle(x, y, w, h, "MATERIAL (M)", m, 8, mats)) { edMat_ = m; edModify([&](Map& mm) { mm.brushes[i].material = m; }, false); }
            row();
        } else if (kind == ED_GROUP) {
            auto& gr = ed_.groups[i];
            if (gr.type == Map::GROUP_CUSTOM) {
                ui_.label(x, y, std::to_string(gr.parts.size()) + " BOXES, STRETCHED AS ONE - UNGROUP TO EDIT THEM", kGrey, 0.5f);
                y += 14 * s;
            } else {
                int m = gr.material;
                if (ui_.cycle(x, y, w, h, "MATERIAL (M)", m, 8, mats)) { edMat_ = m; edModify([&](Map& mm) { mm.groups[i].material = m; }, false); }
                row();
            }
            if (gr.type == Map::GROUP_STAIRS) {
                static const std::vector<std::string> dirs = {"+X", "-X", "+Z", "-Z"};
                int d = gr.dir;
                if (ui_.cycle(x, y, w, h, "RISING TOWARD", d, 4, dirs)) edModify([&](Map& mm) { mm.groups[i].dir = d; }, false);
                row();
            }
        } else if (kind == ED_SPAWN) {
            static const std::vector<std::string> teams = {"TEAM A", "TEAM B", "ANY TEAM", "RACE"};
            int t = ed_.spawns[i].team;
            if (ui_.cycle(x, y, w, h, "TEAM (M)", t, 4, teams)) edModify([&](Map& mm) { mm.spawns[i].team = t; }, false);
            row();
            float deg = std::remainder(ed_.spawns[i].yaw * 180.0f / PI, 360.0f);
            if (ui_.slider(x, y, w, h, "FACING (Q E)", deg, -180, 180, "%.0f"))
                edModify([&](Map& mm) { mm.spawns[i].yaw = std::round(deg / 15) * 15 * PI / 180.0f; }, true);
            row();
        } else if (kind == ED_LIGHT) {
            float r = ed_.lights[i].radius, in = ed_.lights[i].intensity;
            if (ui_.slider(x, y, w, h, "RADIUS", r, 1, 60, "%.0f")) edModify([&](Map& mm) { mm.lights[i].radius = std::round(r); }, true);
            row();
            if (ui_.slider(x, y, w, h, "BRIGHTNESS", in, 0.1f, 4, "%.1f")) edModify([&](Map& mm) { mm.lights[i].intensity = std::round(in * 10) / 10; }, true);
            row();
        }
        if (ui_.button(x, y, w2, h, "DUPLICATE")) edDuplicate();
        if (ui_.button(x + w2 + gap, y, w2, h, "DELETE")) edDelete();
        row();
        {   // rotate 90 degrees about each axis, mirror across each plane (everything selected, about its middle)
            float w3 = (w - 2 * gap) / 3;
            static const char* rot[] = {"ROTATE Y (R)", "ROTATE X (SH+R)", "ROTATE Z (CT+R)"};
            static const char* mir[] = {"MIRROR X (F)", "MIRROR Z (SH+F)", "MIRROR Y (CT+F)"};
            static const int rotOp[] = {ED_ROT_Y, ED_ROT_X, ED_ROT_Z}, mirOp[] = {ED_MIR_X, ED_MIR_Z, ED_MIR_Y};
            for (int k = 0; k < 3; k++) if (ui_.button(x + k * (w3 + gap), y, w3, h, rot[k])) edTransform(rotOp[k]);
            row();
            for (int k = 0; k < 3; k++) if (ui_.button(x + k * (w3 + gap), y, w3, h, mir[k])) edTransform(mirOp[k]);
            row();
        }
    } else {
        ui_.label(x, y, "CLICK SOMETHING TO SELECT IT", kGrey, 0.5f);
        y += 16 * s;
    }
    // what this map can be played as
    y = std::max(y + 6 * s, H - 150 * s);
    std::string modes;
    for (int m = 0; m < MODE_COUNT; m++) if (ed_.supportsMode(m)) modes += std::string(modes.empty() ? "" : " ") + modeName(m);
    ui_.label(x, y, "PLAYABLE AS:", kGrey, 0.5f);
    y += 12 * s;
    ui_.label(x, y, modes.empty() ? "NOTHING YET (NEEDS SPAWNS)" : modes, modes.empty() ? kRed : kWhite, 0.5f);
    y += 12 * s;
    ui_.label(x, y, "(TEAM MODES NEED A + B SPAWNS)", kGrey, 0.5f);
    y += 18 * s;
    char stats[96];
    snprintf(stats, sizeof stats, "%d BOXES  %d SPAWNS  %d LIGHTS", (int)ed_.brushes.size(), (int)ed_.spawns.size(), (int)ed_.lights.size());
    ui_.label(x, y, stats, kGrey, 0.5f);

    // help, bottom left
    static const char* help[] = {
        "RIGHT MOUSE + MOVE: LOOK    WASD: FLY    SPACE / C: UP / DOWN    SHIFT: FAST    WHEEL: SPEED",
        "CLICK: SELECT (SHIFT: ADD MORE, THEN GROUP)    ARROWS / PGUP / PGDN: MOVE BY GRID (ALONG YOUR VIEW)",
        "SHIFT + ARROWS: GROW THAT SIDE    CTRL + ARROWS: SHRINK IT    M: MATERIAL / TEAM",
        "CTRL+D: DUPLICATE    DEL: DELETE    CTRL+Z: UNDO    CTRL+S: SAVE    ESC: DESELECT / LEAVE",
        "R: ROTATE 90 (SHIFT: ABOUT X, CTRL: ABOUT Z)    F: MIRROR LEFT / RIGHT (SHIFT: FRONT / BACK, CTRL: UPSIDE DOWN)",
    };
    float hy = H - 12 * s - 5 * 13 * s;
    r_.rect(0, hy - 6 * s, panelX, H - hy + 6 * s, {0, 0, 0, 0.45f});
    for (const char* t : help) { ui_.label(10 * s, hy, t, kGrey, 0.5f); hy += 13 * s; }
    if (edGroupMenu_ || edExportAsk_) drawEditorGroupMenu(panelX * 0.5f - 200 * s, 70 * s, 400 * s);
    if (time_ < edMsgUntil_) ui_.labelCentered(panelX * 0.5f, 20 * s, edMsg_, kWhite);
    if (edOnline_) {   // editing together: the chat (Enter) and any running vote (F1 / F2)
        float ts = std::max(2.0f, std::round(H / 360.0f));
        drawChatBox(panelX, H, s, ts);
        if (net_.hasSnapshot() && net_.mode().voteActive) drawVotePanel(panelX * 0.5f, s, ts);
    }
}
