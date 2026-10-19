// Map editor: fly around, place and resize boxes (base / accent / wallbang), spawns, lights and the
// gameplay zones, then save to maps/<name>.qmap or play-test it right away. Everything snaps to a grid.
// The view draws the boxes directly (no bake) so edits show instantly; BAKE LIGHT previews real lighting.
#include "game.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kRed = {1, 0.35f, 0.3f, 1};
const float kGrids[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
const char* kKindNames[] = {"BOX", "SPAWN", "LIGHT", "BOMB SITE", "HILL", "CHECKPOINT", "PAYLOAD PATH POINT"};
const Vec3 kTeamColors[] = {{0.25f, 0.6f, 1.0f}, {1.0f, 0.35f, 0.25f}, {0.9f, 0.9f, 0.9f}, {0.3f, 1.0f, 0.4f}};

float snapTo(float v, float g) { return std::round(v / g) * g; }
Vec3 snapTo(const Vec3& v, float g) { return {snapTo(v.x, g), snapTo(v.y, g), snapTo(v.z, g)}; }
AABB around(const Vec3& p, const Vec3& half) { return {p - half, p + half}; }
}

void Game::edNotify(const std::string& s) { edMsg_ = s; edMsgUntil_ = time_ + 3; }

void Game::openEditor(const std::string& name) {
    rescanMaps();
    ed_ = Map();
    edUndo_.clear();
    edSel_ = EdSel();
    edDirty_ = edBaked_ = false;
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
        edName_ = "mymap";
    }
    for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == edName_) edLoadIdx_ = (int)i;
    Vec3 c = ed_.bounds.center();
    float r = std::max(8.0f, length(ed_.bounds.mx - ed_.bounds.mn) * 0.35f);
    edCam_ = c + Vec3(0, r * 0.5f, r);
    yaw_ = 0;
    pitch_ = -0.45f;
    r_.unloadWorld(ed_.sunDir);
    screen_ = Screen::Editor;
    ui_.clearFocus();
    edLook_ = false;
    captureMouse(false);
}

void Game::closeEditor() {
    edLook_ = false;
    rescanMaps();
    setScreen(Screen::Main);
    loadMenuBackdrop();
}

void Game::edPush(bool coalesce) {
    if (coalesce && time_ - edLastPush_ < 0.75) { edLastPush_ = time_; return; } // one undo step per slider drag
    edLastPush_ = coalesce ? time_ : 0;
    edUndo_.push_back(ed_);
    if (edUndo_.size() > 200) edUndo_.erase(edUndo_.begin());
}

void Game::edChanged() {
    ed_.rebuild();
    edDirty_ = true;
    if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
}

AABB Game::edBounds(EdSel sel) const {
    switch (sel.kind) {
        case ED_BRUSH: return ed_.brushes[sel.idx].box;
        case ED_SPAWN: { Vec3 p = ed_.spawns[sel.idx].pos; return {p - Vec3(0.4f, 0, 0.4f), p + Vec3(0.4f, 1.8f, 0.4f)}; }
        case ED_LIGHT: return around(ed_.lights[sel.idx].pos, {0.3f, 0.3f, 0.3f});
        case ED_SITE: return ed_.bombSites[sel.idx];
        case ED_HILL: return ed_.hill;
        case ED_CHECK: return ed_.checkpoints[sel.idx];
        case ED_PATH: return around(ed_.payloadPath[sel.idx], {0.3f, 0.3f, 0.3f});
    }
    return {};
}

void Game::edMove(EdSel sel, const Vec3& d) {
    switch (sel.kind) {
        case ED_BRUSH: ed_.brushes[sel.idx].box.mn += d; ed_.brushes[sel.idx].box.mx += d; break;
        case ED_SPAWN: ed_.spawns[sel.idx].pos += d; break;
        case ED_LIGHT: ed_.lights[sel.idx].pos += d; break;
        case ED_SITE: ed_.bombSites[sel.idx].mn += d; ed_.bombSites[sel.idx].mx += d; break;
        case ED_HILL: ed_.hill.mn += d; ed_.hill.mx += d; break;
        case ED_CHECK: ed_.checkpoints[sel.idx].mn += d; ed_.checkpoints[sel.idx].mx += d; break;
        case ED_PATH: ed_.payloadPath[sel.idx] += d; break;
    }
}

// Moves one face of a box along its axis: sign picks the face (+ = max side), amount > 0 pushes it outward.
void Game::edResize(EdSel sel, int axis, int sign, float amount) {
    AABB* b = sel.kind == ED_BRUSH ? &ed_.brushes[sel.idx].box : sel.kind == ED_SITE ? &ed_.bombSites[sel.idx]
            : sel.kind == ED_HILL ? &ed_.hill : sel.kind == ED_CHECK ? &ed_.checkpoints[sel.idx] : nullptr;
    if (sel.kind == ED_LIGHT) { // lights: the size keys change the radius
        auto& l = ed_.lights[sel.idx];
        l.radius = clampf(l.radius + (amount > 0 ? 1.0f : -1.0f), 1, 60);
        return;
    }
    if (!b) return;
    const float g = kGrids[edGridIdx_];
    if (sign > 0) b->mx[axis] = std::max(b->mn[axis] + g, b->mx[axis] + amount);
    else b->mn[axis] = std::min(b->mx[axis] - g, b->mn[axis] - amount);
}

void Game::edDelete() {
    if (!edSel_.valid()) return;
    if (edSel_.kind == ED_BRUSH && ed_.brushes.size() <= 1) { edNotify("a map needs at least one box"); return; }
    if (edSel_.kind == ED_SPAWN && ed_.spawns.size() <= 1) { edNotify("a map needs at least one spawn"); return; }
    edPush();
    int i = edSel_.idx;
    switch (edSel_.kind) {
        case ED_BRUSH: ed_.brushes.erase(ed_.brushes.begin() + i); break;
        case ED_SPAWN: ed_.spawns.erase(ed_.spawns.begin() + i); break;
        case ED_LIGHT: ed_.lights.erase(ed_.lights.begin() + i); break;
        case ED_SITE: ed_.bombSites.erase(ed_.bombSites.begin() + i); break;
        case ED_HILL: ed_.hasHill = false; break;
        case ED_CHECK: ed_.checkpoints.erase(ed_.checkpoints.begin() + i); break;
        case ED_PATH: ed_.payloadPath.erase(ed_.payloadPath.begin() + i); break;
    }
    edSel_ = EdSel();
    edChanged();
}

// Copies the selection and puts the copy right next to it (along the axis you're looking down the least).
void Game::edDuplicate() {
    if (!edSel_.valid() || edSel_.kind == ED_HILL) return;
    edPush();
    AABB b = edBounds(edSel_);
    Vec3 f = angleForward(yaw_, 0);
    Vec3 off = std::fabs(f.x) > std::fabs(f.z) ? Vec3(0, 0, b.mx.z - b.mn.z) : Vec3(b.mx.x - b.mn.x, 0, 0);
    int i = edSel_.idx;
    switch (edSel_.kind) {
        case ED_BRUSH: ed_.brushes.push_back(ed_.brushes[i]); edSel_.idx = (int)ed_.brushes.size() - 1; break;
        case ED_SPAWN: ed_.spawns.push_back(ed_.spawns[i]); edSel_.idx = (int)ed_.spawns.size() - 1; break;
        case ED_LIGHT: ed_.lights.push_back(ed_.lights[i]); edSel_.idx = (int)ed_.lights.size() - 1; break;
        case ED_SITE: ed_.bombSites.push_back(ed_.bombSites[i]); edSel_.idx = (int)ed_.bombSites.size() - 1; break;
        case ED_CHECK: ed_.checkpoints.push_back(ed_.checkpoints[i]); edSel_.idx = (int)ed_.checkpoints.size() - 1; break;
        case ED_PATH: ed_.payloadPath.push_back(ed_.payloadPath[i]); edSel_.idx = (int)ed_.payloadPath.size() - 1; break;
    }
    edMove(edSel_, off);
    edChanged();
}

// New object at the spot under the screen center (on top of whatever you're looking at), snapped to the grid.
void Game::edAdd(int kind, int variant) {
    edPush();
    const float g = kGrids[edGridIdx_];
    Vec3 dir = angleForward(yaw_, pitch_), n;
    float t = ed_.raycast(edCam_, dir, 200, &n);
    Vec3 at = t >= 0 ? edCam_ + dir * t + n * 0.001f : edCam_ + dir * 8.0f;
    at = snapTo(at, g);
    if (t >= 0 && n.y < -0.5f) at.y -= 1; // hit a ceiling: hang below it
    switch (kind) {
        case ED_BRUSH: {
            Vec3 half(1, 0.5f, 1);
            AABB b{at - Vec3(half.x, 0, half.z), at + Vec3(half.x, 2 * half.y, half.z)};
            ed_.brushes.push_back({b, variant});
            edSel_ = {ED_BRUSH, (int)ed_.brushes.size() - 1};
            break;
        }
        case ED_SPAWN: {
            SpawnPoint sp;
            sp.pos = at;
            sp.team = variant;
            sp.yaw = std::round(yaw_ / (PI / 4)) * (PI / 4);
            ed_.spawns.push_back(sp);
            edSel_ = {ED_SPAWN, (int)ed_.spawns.size() - 1};
            break;
        }
        case ED_LIGHT: ed_.lights.push_back({at + Vec3(0, 2, 0), 10, 1}); edSel_ = {ED_LIGHT, (int)ed_.lights.size() - 1}; break;
        case ED_SITE: ed_.bombSites.push_back({at - Vec3(3, 0, 3), at + Vec3(3, 3, 3)}); edSel_ = {ED_SITE, (int)ed_.bombSites.size() - 1}; break;
        case ED_HILL: ed_.hasHill = true; ed_.hill = {at - Vec3(4, 0, 4), at + Vec3(4, 3, 4)}; edSel_ = {ED_HILL, 0}; break;
        case ED_CHECK: ed_.checkpoints.push_back({at - Vec3(2, 0, 2), at + Vec3(2, 4, 2)}); edSel_ = {ED_CHECK, (int)ed_.checkpoints.size() - 1}; break;
        case ED_PATH: ed_.payloadPath.push_back(at + Vec3(0, 0.1f, 0)); edSel_ = {ED_PATH, (int)ed_.payloadPath.size() - 1}; break;
    }
    edChanged();
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
    for (size_t i = 0; i < ed_.spawns.size(); i++) test(ED_SPAWN, (int)i, edBounds({ED_SPAWN, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.lights.size(); i++) test(ED_LIGHT, (int)i, edBounds({ED_LIGHT, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.payloadPath.size(); i++) test(ED_PATH, (int)i, edBounds({ED_PATH, (int)i}), -0.5f);
    for (size_t i = 0; i < ed_.brushes.size(); i++) test(ED_BRUSH, (int)i, ed_.brushes[i].box, 0);
    for (size_t i = 0; i < ed_.bombSites.size(); i++) test(ED_SITE, (int)i, ed_.bombSites[i], 0.25f);
    for (size_t i = 0; i < ed_.checkpoints.size(); i++) test(ED_CHECK, (int)i, ed_.checkpoints[i], 0.25f);
    if (ed_.hasHill) test(ED_HILL, 0, ed_.hill, 0.25f);
    return best;
}

// World-space direction under the mouse (the screen center while mouse-looking).
Vec3 Game::edMouseRay() const {
    Vec3 f = angleForward(yaw_, pitch_);
    if (edLook_) return f;
    Vec3 right = normalize(cross(f, {0, 1, 0})), up = cross(right, f);
    float W = (float)r_.width(), H = (float)r_.height();
    float tx = std::tan(deg2rad(cfg_.fov) * 0.5f), ty = tx * H / W;
    float nx = uiIn_.mx / W * 2 - 1, ny = 1 - uiIn_.my / H * 2;
    return normalize(f + right * (nx * tx) + up * (ny * ty));
}

bool Game::edSave() {
    bool okName = !edName_.empty();
    for (char c : edName_) okName &= (bool)(isalnum((unsigned char)c) || c == '_' || c == '-');
    if (!okName) { edNotify("name: letters, digits, - and _ only"); return false; }
    std::string text = ed_.serialize();
    Map check;
    std::string err;
    if (!check.parse(text, &err)) { edNotify("can't save: " + err); return false; }
    if (!saveMapFile(findDataPath("maps") + "/" + edName_ + ".qmap", text)) { edNotify("could not write maps/" + edName_ + ".qmap"); return false; }
    edDirty_ = false;
    rescanMaps();
    edNotify("saved maps/" + edName_ + ".qmap");
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
    edLook_ = false;
    startHost(false);
    if (!host_) testMap_.clear();
}

void Game::editorKey(int key, int mods) {
    bool ctrl = (mods & GLFW_MOD_CONTROL) || (mods & GLFW_MOD_SUPER), shift = mods & GLFW_MOD_SHIFT;
    const float g = kGrids[edGridIdx_];
    if (key == GLFW_KEY_ESCAPE) {
        if (edLook_) { edLook_ = false; captureMouse(false); return; }
        if (edSel_.valid()) { edSel_ = EdSel(); return; }
        if (edDirty_ && time_ > edLeaveArmed_) { edLeaveArmed_ = time_ + 2; edNotify("unsaved changes - press ESC again to leave"); return; }
        closeEditor();
        return;
    }
    if (ctrl && key == GLFW_KEY_Z) {
        if (edUndo_.empty()) return;
        ed_ = edUndo_.back();
        edUndo_.pop_back();
        edSel_ = EdSel();
        edChanged();
        return;
    }
    if (ctrl && key == GLFW_KEY_S) { edSave(); return; }
    if (ctrl && key == GLFW_KEY_D) { edDuplicate(); return; }
    if (key == GLFW_KEY_LEFT_BRACKET) { edGridIdx_ = std::max(0, edGridIdx_ - 1); return; }
    if (key == GLFW_KEY_RIGHT_BRACKET) { edGridIdx_ = std::min(4, edGridIdx_ + 1); return; }
    if (!edSel_.valid()) return;
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) { edDelete(); return; }
    if (key == GLFW_KEY_M) { // material / team
        edPush();
        if (edSel_.kind == ED_BRUSH) { auto& m = ed_.brushes[edSel_.idx].material; m = (m + 1) % 3; }
        else if (edSel_.kind == ED_SPAWN) { auto& t = ed_.spawns[edSel_.idx].team; t = (t + 1) % 4; }
        edChanged();
        return;
    }
    if ((key == GLFW_KEY_Q || key == GLFW_KEY_E) && edSel_.kind == ED_SPAWN && !edLook_) {
        edPush();
        ed_.spawns[edSel_.idx].yaw += key == GLFW_KEY_Q ? PI / 4 : -PI / 4;
        edChanged();
        return;
    }
    // arrows move along the world axis closest to where you're looking; PgUp/PgDn move up/down.
    // Shift pushes that face of the box out, Ctrl pulls it in.
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
    edPush(true);
    if (shift || ctrl) edResize(edSel_, axis, sign > 0 ? 1 : -1, shift ? g : -g);
    else { Vec3 d; d[axis] = sign * g; edMove(edSel_, d); }
    edChanged();
}

void Game::editorFrame(double dt) {
    const float W = (float)r_.width(), H = (float)r_.height();
    const float s = H / 720.0f, g = kGrids[edGridIdx_];
    const float panelW = 320 * s, panelX = W - panelW;
    // ---- fly camera (right mouse held to look; WASD, Space up, C down, Shift fast, wheel = speed)
    if (!ui_.typing()) {
        Vec3 f = angleForward(yaw_, pitch_), right = normalize(cross(angleForward(yaw_, 0), {0, 1, 0}));
        Vec3 mv;
        if (glfwGetKey(win_, GLFW_KEY_W) == GLFW_PRESS) mv += f;
        if (glfwGetKey(win_, GLFW_KEY_S) == GLFW_PRESS && !(glfwGetKey(win_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)) mv -= f;
        if (glfwGetKey(win_, GLFW_KEY_D) == GLFW_PRESS && !(glfwGetKey(win_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)) mv += right;
        if (glfwGetKey(win_, GLFW_KEY_A) == GLFW_PRESS) mv -= right;
        if (glfwGetKey(win_, GLFW_KEY_SPACE) == GLFW_PRESS) mv.y += 1;
        if (glfwGetKey(win_, GLFW_KEY_C) == GLFW_PRESS) mv.y -= 1;
        if (edLook_ && glfwGetKey(win_, GLFW_KEY_E) == GLFW_PRESS) mv.y += 1;
        if (edLook_ && glfwGetKey(win_, GLFW_KEY_Q) == GLFW_PRESS) mv.y -= 1;
        float sp = edSpeed_ * (glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 3.0f : 1.0f);
        if (length(mv) > 0) edCam_ += normalize(mv) * sp * (float)dt;
    }
    // ---- click to select (outside the panel)
    if (uiIn_.pressed && uiIn_.mx < panelX) {
        edSel_ = edPick(edCam_, edMouseRay());
        ui_.clearFocus();
    }

    // ---- 3D view
    r_.setCamera(edCam_, yaw_, pitch_, cfg_.fov);
    const Vec3 matColor[3] = {r_.baseColor, r_.accentColor, {0.22f, 0.22f, 0.24f}};
    if (!edBaked_)
        for (auto& b : ed_.brushes) r_.box(b.box.center(), (b.box.mx - b.box.mn) * 0.5f, 0, matColor[b.material], 1.0f, false);
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
    if (edSel_.valid()) {
        AABB b = edBounds(edSel_);
        b.mn -= Vec3(0.02f, 0.02f, 0.02f); b.mx += Vec3(0.02f, 0.02f, 0.02f);
        Color c = {1.0f, 0.85f, 0.1f, 1};
        for (int e = 0; e < 12; e++) { // the 12 edges of the box
            int ax = e / 4, a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
            Vec3 p = b.mn, q;
            if (e & 1) p[a1] = b.mx[a1];
            if (e & 2) p[a2] = b.mx[a2];
            q = p; q[ax] = b.mx[ax];
            r_.line(p, q, 0.04f, c);
        }
        if (edSel_.kind == ED_LIGHT) { auto& l = ed_.lights[edSel_.idx]; r_.translucentBox(around(l.pos, {l.radius, l.radius, l.radius}), {1, 0.9f, 0.4f, 0.06f}); }
    }
    { // where new things go: the snapped point under the screen center
        Vec3 dir = angleForward(yaw_, pitch_), n;
        float t = ed_.raycast(edCam_, dir, 200, &n);
        if (t >= 0) r_.box(snapTo(edCam_ + dir * t + n * 0.001f, g), {0.06f, 0.06f, 0.06f}, 0, {1, 0.85f, 0.1f}, 1.0f, false);
    }
    r_.drawScene(false);
    if (edLook_) r_.rect(W * 0.5f - 1, H * 0.5f - 1, 2, 2, kWhite);

    // ---- panel
    ui_.begin(&r_, uiIn_, s);
    ui_.panel(panelX, 0, panelW, H, 0.85f);
    float x = panelX + 12 * s, w = panelW - 24 * s, y = 10 * s, h = 26 * s, gap = 5 * s;
    float w2 = (w - gap) * 0.5f, w3 = (w - 2 * gap) / 3;
    auto row = [&] { y += h + gap; };
    ui_.label(x, y, std::string("MAP EDITOR") + (edDirty_ ? " *" : ""), ui_.accent);
    y += 22 * s;
    ui_.label(x, y, "FILE NAME / TITLE", kGrey, 0.5f);
    y += 12 * s;
    ui_.textField(40, x, y, w2, h, edName_, 24);
    std::string title = ed_.title;
    if (ui_.textField(41, x + w2 + gap, y, w2, h, title, 32) && title != ed_.title) { ed_.title = title; edDirty_ = true; }
    row();
    {
        static std::vector<std::string> modeNames;
        if (modeNames.empty()) { modeNames.push_back("ANY"); for (int m = 0; m < MODE_COUNT; m++) modeNames.push_back(modeName(m)); }
        int idx = ed_.preferredMode + 1;
        if (ui_.cycle(x, y, w, h, "BUILT FOR", idx, MODE_COUNT + 1, modeNames)) { edPush(); ed_.preferredMode = idx - 1; edDirty_ = true; }
        row();
    }
    if (ui_.button(x, y, w3, h, "NEW")) { openEditor(""); return; }
    if (ui_.button(x + w3 + gap, y, w3, h, "SAVE")) edSave();
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "TEST")) { edTest(); if (screen_ != Screen::Editor) return; }
    row();
    if (!maps_.empty()) {
        if (ui_.cycle(x, y, w - w3 - gap, h, "MAP", edLoadIdx_, (int)maps_.size(), maps_)) {}
        if (ui_.button(x + w - w3, y, w3, h, "OPEN")) { openEditor(maps_[edLoadIdx_ % maps_.size()]); return; }
        row();
    }
    if (ui_.button(x, y, w2, h, edBaked_ ? "UNBAKE" : "BAKE LIGHT")) {
        if (edBaked_) { edBaked_ = false; r_.unloadWorld(ed_.sunDir); }
        else { r_.loadWorld(ed_, [&](float p) { drawBakeProgress(edName_, p); }); edBaked_ = true; }
    }
    if (ui_.button(x + w2 + gap, y, w2, h, "EXIT")) {
        if (edDirty_ && time_ > edLeaveArmed_) { edLeaveArmed_ = time_ + 2; edNotify("unsaved changes - click EXIT again to leave"); }
        else { closeEditor(); return; }
    }
    row();
    y += 6 * s;
    ui_.label(x, y, "ADD (AT THE SCREEN CENTER)", kGrey, 0.5f);
    y += 12 * s;
    if (ui_.button(x, y, w3, h, "BOX")) edAdd(ED_BRUSH, 0);
    if (ui_.button(x + w3 + gap, y, w3, h, "ACCENT")) edAdd(ED_BRUSH, 1);
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "WALLBANG")) edAdd(ED_BRUSH, 2);
    row();
    if (ui_.button(x, y, w3, h, "SPAWN")) edAdd(ED_SPAWN, SPAWN_ANY);
    if (ui_.button(x + w3 + gap, y, w3, h, "LIGHT")) edAdd(ED_LIGHT, 0);
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "PATH PT")) edAdd(ED_PATH, 0);
    row();
    if (ui_.button(x, y, w3, h, "BOMB SITE")) edAdd(ED_SITE, 0);
    if (ui_.button(x + w3 + gap, y, w3, h, "HILL")) edAdd(ED_HILL, 0);
    if (ui_.button(x + 2 * (w3 + gap), y, w3, h, "CHECKPT")) edAdd(ED_CHECK, 0);
    row();
    {
        static const std::vector<std::string> grids = {"0.25 M", "0.5 M", "1 M", "2 M", "4 M"};
        ui_.cycle(x, y, w, h, "GRID ([ ])", edGridIdx_, 5, grids);
        row();
    }
    y += 6 * s;
    if (edSel_.valid()) {
        AABB b = edBounds(edSel_);
        std::string what = kKindNames[edSel_.kind];
        if (edSel_.kind == ED_CHECK) what += " " + std::to_string(edSel_.idx + 1) + (edSel_.idx + 1 == (int)ed_.checkpoints.size() ? " (FINISH)" : "");
        if (edSel_.kind == ED_PATH) what += " " + std::to_string(edSel_.idx + 1);
        ui_.label(x, y, "SELECTED: " + what, ui_.accent, 0.6f);
        y += 16 * s;
        char buf[128];
        if (edSel_.kind == ED_SPAWN || edSel_.kind == ED_LIGHT || edSel_.kind == ED_PATH) {
            Vec3 p = edSel_.kind == ED_SPAWN ? ed_.spawns[edSel_.idx].pos : edSel_.kind == ED_LIGHT ? ed_.lights[edSel_.idx].pos : ed_.payloadPath[edSel_.idx];
            snprintf(buf, sizeof buf, "AT %g %g %g", p.x, p.y, p.z);
        } else snprintf(buf, sizeof buf, "%g %g %g  TO  %g %g %g", b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
        ui_.label(x, y, buf, kGrey, 0.5f);
        y += 14 * s;
        if (edSel_.kind == ED_BRUSH) {
            int m = ed_.brushes[edSel_.idx].material;
            static const std::vector<std::string> mats = {"BASE", "ACCENT", "WALLBANG"};
            if (ui_.cycle(x, y, w, h, "MATERIAL (M)", m, 3, mats)) { edPush(); ed_.brushes[edSel_.idx].material = m; edChanged(); }
            row();
        } else if (edSel_.kind == ED_SPAWN) {
            auto& sp = ed_.spawns[edSel_.idx];
            static const std::vector<std::string> teams = {"TEAM A", "TEAM B", "ANY", "RACE"};
            int t = sp.team;
            if (ui_.cycle(x, y, w, h, "TEAM (M)", t, 4, teams)) { edPush(); sp.team = t; edChanged(); }
            row();
            float deg = std::remainder(sp.yaw * 180.0f / PI, 360.0f);
            if (ui_.slider(x, y, w, h, "FACING (Q E)", deg, -180, 180, "%.0f")) { edPush(true); sp.yaw = std::round(deg / 15) * 15 * PI / 180.0f; edChanged(); }
            row();
        } else if (edSel_.kind == ED_LIGHT) {
            auto& l = ed_.lights[edSel_.idx];
            if (ui_.slider(x, y, w, h, "RADIUS", l.radius, 1, 60, "%.0f")) { edPush(true); l.radius = std::round(l.radius); edChanged(); }
            row();
            if (ui_.slider(x, y, w, h, "BRIGHTNESS", l.intensity, 0.1f, 4, "%.1f")) { edPush(true); l.intensity = std::round(l.intensity * 10) / 10; edChanged(); }
            row();
        }
        if (ui_.button(x, y, w2, h, "DUPLICATE")) edDuplicate();
        if (ui_.button(x + w2 + gap, y, w2, h, "DELETE")) edDelete();
        row();
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
        "CLICK: SELECT    ARROWS / PGUP / PGDN: MOVE BY GRID (ALONG YOUR VIEW)",
        "SHIFT + ARROWS: GROW THAT SIDE    CTRL + ARROWS: SHRINK IT    M: MATERIAL / TEAM",
        "CTRL+D: DUPLICATE    DEL: DELETE    CTRL+Z: UNDO    CTRL+S: SAVE    ESC: DESELECT / LEAVE",
    };
    float hy = H - 12 * s - 4 * 13 * s;
    r_.rect(0, hy - 6 * s, panelX, H - hy + 6 * s, {0, 0, 0, 0.45f});
    for (const char* t : help) { ui_.label(10 * s, hy, t, kGrey, 0.5f); hy += 13 * s; }
    if (time_ < edMsgUntil_) ui_.labelCentered(panelX * 0.5f, 20 * s, edMsg_, kWhite);
}
