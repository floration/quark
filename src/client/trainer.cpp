// AIM TRAINER (main menu; owner, 2026-11-01; `quark --trainer ID[:DIFF]`): the scenario menu (mode, category,
// difficulty, then a card per scenario with your best score), the 30 s run itself (a client-only scene like the map
// editor: no server, the room and targets drawn here), and its result. The simulation: src/game/trainer.h.
// 3D STATIC: you can't move, only look and shoot. 2D: the view never moves; the mouse moves the crosshair over the wall.
#include "game.h"
#include "../common/u8file.h"
#include "../game/trainer.h"
#include "../common/rng.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kGold = {1.0f, 0.8f, 0.3f, 1};

// a unit sphere for the targets (12 rings of 20)
void buildSphere(ModelMesh& m) {
    const int rings = 12, segs = 20;
    auto pt = [&](int i, int j) {
        const float th = 3.14159265f * i / rings, ph = 2 * 3.14159265f * j / segs;
        return Vec3{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
    };
    auto put = [&](const Vec3& p) {
        ModelMesh::V v{};
        v.pos[0] = v.nrm[0] = p.x; v.pos[1] = v.nrm[1] = p.y; v.pos[2] = v.nrm[2] = p.z;
        v.col[0] = v.col[1] = v.col[2] = v.col[3] = 1;
        v.slot = TINT_TEAM;
        m.opaque.push_back(v);
    };
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            const Vec3 a = pt(i, j), b = pt(i + 1, j), c = pt(i + 1, j + 1), d = pt(i, j + 1);
            if (i > 0) { put(a); put(b); put(d); }
            if (i < rings - 1) { put(b); put(c); put(d); }
        }
    m.center = {0, 0, 0};
    m.radius = 1;
}

std::string fmt1(float v) { char b[32]; snprintf(b, sizeof b, "%.1f", v); return b; }
bool tilted(const Settings::HudElem& e) { return e.pitch != 0 || e.yaw != 0 || e.roll != 0; }
std::string pct(float v) { return std::to_string((int)std::lround(v * 100)) + "%"; }
} // namespace

std::string Game::trKey(const char* id, int diff) const { return std::string(id) + ":" + std::to_string(diff); }

void Game::trainerLoadScores() {
    trScoresLoaded_ = true;
    trScores_.clear();
    std::ifstream in(utf8Path(userDataPath("trainer-scores.txt")));
    std::string id;
    int diff, best, last, plays;
    while (in >> id >> diff >> best >> last >> plays) trScores_[trKey(id.c_str(), diff)] = {best, last, plays};
}

void Game::trainerSaveScores() {
    std::ostringstream o;
    for (auto& kv : trScores_) {
        const size_t c = kv.first.rfind(':');
        if (c == std::string::npos) continue;
        o << kv.first.substr(0, c) << " " << kv.first.substr(c + 1) << " " << kv.second.best << " " << kv.second.last << " " << kv.second.plays << "\n";
    }
    std::ofstream out(utf8Path(userDataPath("trainer-scores.txt")), std::ios::binary | std::ios::trunc);
    out << o.str();
}

// 2D drawing (the 2D mode draws nothing in 3D): a filled disc as a fan of triangles, a thick line as one quad
static void disc2D(Renderer& r, float x, float y, float rad, const Color& c) {
    const int n = std::clamp((int)(rad * 1.5f), 32, 96);
    for (int i = 0; i < n; i++) {
        const float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        const float q[8] = {x, y, x + rad * std::cos(a0), y + rad * std::sin(a0), x + rad * std::cos(a1), y + rad * std::sin(a1), x, y};
        r.quad2D(q, c);
    }
}
static void line2D(Renderer& r, float x0, float y0, float x1, float y1, float w, const Color& c) {
    float dx = x1 - x0, dy = y1 - y0;
    const float l = std::sqrt(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    dx *= w * 0.5f / l; dy *= w * 0.5f / l;
    const float q[8] = {x0 - dy, y0 + dx, x1 - dy, y1 + dx, x1 + dy, y1 - dx, x0 + dy, y0 - dx};
    r.quad2D(q, c);
}

void Game::openTrainer() {
    if (screen_ == Screen::TrainerRun) { trainerExit(); return; }   // (from a run: as its menu button)
    if (!trScoresLoaded_) trainerLoadScores();
    setScreen(Screen::Trainer);
}

void Game::trainerStart(const trainer::Scenario* sc) {
    if (!sc) return;
    if (!trScoresLoaded_) trainerLoadScores();
    trSc_ = sc;
    trMode_ = sc->mode;
    trCat_ = sc->cat;
    trRun_.start(*sc, trDiff_, (uint32_t)(timeNow() * 1000.0) ^ 0x51ED5EEDu);
    trResults_ = trPaused_ = false;
    settingsInGame_ = false;
    trNewBest_ = false;
    trClick_ = false;
    trTracerUntil_ = trHitUntil_ = 0;
    if (screen_ != Screen::TrainerRun) {
        trSavedYaw_ = yaw_; trSavedPitch_ = pitch_;
        r_.unloadWorld(normalize(Vec3(0.4f, 1.0f, 0.25f)));
        trSavedSky_ = r_.skyColor;
    }
    r_.setFog(0);   // (no haze: every target as sharp at 24 m as at 10)
    r_.skyColor = {0.05f, 0.055f, 0.07f};
    trRoomDirty_ = true;
    yaw_ = pitch_ = 0;
    heldWeapon_ = sc->laser ? WPN_LASER : WPN_RIFLE;   // (its own sensitivity: CONTROLS > MORE 2)
    scoped_ = false;
    setScreen(Screen::TrainerRun);
    captureMouse(true);
}

void Game::trainerExit() {
    r_.clearStaticBoxes();
    r_.setFog(cfg_.fog);
    r_.skyColor = trSavedSky_;
    trResults_ = trPaused_ = false;
    if (settingsInGame_) { settingsInGame_ = false; cfg_.save(); }
    yaw_ = trSavedYaw_; pitch_ = trSavedPitch_;
    setScreen(Screen::Trainer);
    loadMenuBackdrop();
}

void Game::trainerFinish() {
    trResults_ = true;
    const int score = trRun_.score();
    auto& s = trScores_[trKey(trSc_->id, trRun_.diff())];
    trNewBest_ = score > s.best;
    s.best = std::max(s.best, score);
    s.last = score;
    s.plays++;
    trainerSaveScores();
    captureMouse(false);
}

// keys while a run or its result is up: Esc pauses (the pause menu: RESUME, SETTINGS, RESTART, MENU), R again; FIRE
// bound to a key clicks too. F3 / F4 / the screenshot key pass on to the game's own handling, and so does every key
// while SETTINGS is open (its Esc closes what's open on the page first, then the page: back to the pause menu).
bool Game::trainerKey(int key, int action) {
    if (screen_ != Screen::TrainerRun) return false;
    const Binds& b = binds();
    if (b.has(A_PERF, key) || b.has(A_PERFMINI, key) || b.has(A_SCREENSHOT, key)) return false;
    if (trPaused_ && settingsInGame_) {
        if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE &&
            !(ui_.popupOpen() || filePick_ || widgetEdit_ || hudEdit_ || fontsOpen_ || devicesOpen_ || moreOpen_)) { leaveSettings(); return true; }
        return false;
    }
    if (action != GLFW_PRESS) return true;
    if (key == GLFW_KEY_ESCAPE) {
        if (trResults_) { trainerExit(); return true; }
        trPaused_ = !trPaused_;
        trClick_ = false;
        captureMouse(!trPaused_);
        return true;
    }
    if (key == GLFW_KEY_R) { trainerStart(trSc_); return true; }
    if (!trResults_ && !trPaused_ && b.has(A_FIRE, key)) trClick_ = true;
    return true;
}

void Game::trainerFrame(double dt) {
    using namespace trainer;
    if (!trSc_) { setScreen(Screen::Trainer); return; }
    const bool flat = trRun_.flat();
    const float W = (float)r_.width(), H = (float)r_.height();
    const float fov = hfov();
    // the camera: 3D, where you look. 2D draws nothing in 3D (owner, 2026-11-02: "straight up real 2d only"): the target
    // wall is the screen, mapped linearly (the view's width at 24 m), and the mouse moves the crosshair over it at the
    // 3D aim's sensitivity at the centre (a radian of turn = 24 m on the wall), held inside the screen.
    r_.setCamera({0, 0, 0}, flat ? 0.0f : yaw_, flat ? 0.0f : pitch_, fov);   // (2D: only for the screen's extent, tanX / tanY)
    const float vx = -kWallZ * r_.tanX(), vy = -kWallZ * r_.tanY();          // the screen's half width / height on the wall, m
    if (flat) {
        yaw_ = clampf(yaw_, -vx * 0.99f / -kWallZ, vx * 0.99f / -kWallZ);
        pitch_ = clampf(pitch_, -vy * 0.99f / -kWallZ, vy * 0.99f / -kWallZ);
    }
    const Vec3 wallAim(yaw_ * kWallZ, pitch_ * -kWallZ, kWallZ);   // (2D: the cursor's point on the wall; yaw left = -x)
    const Vec3 aim = flat ? normalize(wallAim) : angleForward(yaw_, pitch_);
    auto toScreen = [&](float x, float y, float& sx, float& sy) { sx = W * 0.5f * (1.0f + x / vx); sy = H * 0.5f * (1.0f - y / vy); };
    // ---- the step: the shot against what the last frame showed, then the targets move
    if (!trResults_ && !trPaused_) {
        const bool fire = isDown(A_FIRE) && mouseCaptured_;
        const StepEvents ev = trRun_.step((float)dt, aim, fire, trClick_);
        trClick_ = false;
        if (ev.shot) {
            audio_.play(SND_RIFLE + WPN_RIFLE, 0.8f * cfg_.volWeapons, 0.97f + (qrand() % 7) * 0.01f);
            trTracerUntil_ = time_ + 0.06;
            trTracerEnd_ = ev.shotEnd;
            recoil_ = 1.0;
            muzzleUntil_ = time_ + 0.05;
        }
        if (fire && trSc_->laser && trRun_.countdown() <= 0 && time_ >= nextLaserSound_) {
            audio_.play(SND_RIFLE + WPN_LASER, 0.6f * cfg_.volWeapons, 0.97f + (qrand() % 7) * 0.01f);
            nextLaserSound_ = time_ + 0.05;
        }
        trLaserOn_ = fire && trSc_->laser && trRun_.countdown() <= 0;
        trLaserEnd_ = ev.laserEnd;
        if (ev.laserOn && time_ >= trHitSoundAt_) { audio_.play(SND_HIT, 0.5f * cfg_.hitVolume); trHitSoundAt_ = time_ + 0.1; }
        if (ev.shotHit || ev.laserOn) trHitUntil_ = time_ + 0.12;
        if (ev.kills > 0 && trSc_->cat != CAT_TRACK) audio_.play(ev.shot ? SND_HIT_MED : SND_KILL, 0.8f * cfg_.hitVolume);
        if (trRun_.finished()) trainerFinish();
    } else trLaserOn_ = false;   // (paused: the run holds still)
    recoil_ = std::max(0.0, recoil_ - 8.0 * dt);

    const int w = trSc_->laser ? WPN_LASER : WPN_RIFLE;
    const Color fx = rgba(cfg_.ownFx ? cfg_.ownFxColor : cfg_.colorFriendly, 0.9f);
    if (flat) {   // ---- 2D: flat colour, the target area's frame, discs, the shot as a line from the screen's bottom
        r_.rect(0, 0, W, H, Color{0.11f, 0.12f, 0.15f, 1});
        const float hx = trSc_->motion == MO_CROSS ? kCrossHX : trSc_->motion == MO_AIR ? kAir2HX : kWallHX;
        const float hy = trSc_->motion == MO_CROSS ? kCrossHY : trSc_->motion == MO_AIR ? kAir2HY : kWallHY;
        float x0, y0, x1, y1;
        toScreen(-hx, hy, x0, y0); toScreen(hx, -hy, x1, y1);
        const float lw = std::max(1.0f, std::round(H / 720.0f * 2));
        const Color frame{0.4f, 0.42f, 0.5f, 1};
        r_.rect(x0, y0, x1 - x0, lw, frame); r_.rect(x0, y1 - lw, x1 - x0, lw, frame);
        r_.rect(x0, y0, lw, y1 - y0, frame); r_.rect(x1 - lw, y0, lw, y1 - y0, frame);
        const Vec3 tcol = cfg_.colorEnemy;
        for (const Target& t : trRun_.targets()) {
            if (t.hide > 0) continue;
            Vec3 c = tcol;
            if (trSc_->hp > 0) c = tcol * (0.45f + 0.55f * clampf(t.hp / trSc_->hp, 0, 1));
            float sx, sy;
            toScreen(t.pos.x, t.pos.y, sx, sy);
            disc2D(r_, sx, sy, t.r / vx * W * 0.5f, rgba(c));
        }
        float cx, cy, ex, ey;
        toScreen(wallAim.x, wallAim.y, cx, cy);
        const bool tracer = time_ < trTracerUntil_;
        if (trLaserOn_ || tracer) {
            if (tracer) toScreen(trTracerEnd_.x * (kWallZ / std::min(trTracerEnd_.z, -0.01f)), trTracerEnd_.y * (kWallZ / std::min(trTracerEnd_.z, -0.01f)), ex, ey);
            else { ex = cx; ey = cy; }
            line2D(r_, W * 0.5f, H, ex, ey, (trLaserOn_ ? 4.0f : 2.5f) * H / 720.0f, fx);
        }
    } else {
        // ---- the room: a static batch, built when the scenario changes
        sceneSettings();
        r_.setFog(0);   // (again each frame: a setting changed in SETTINGS may have set the game's)
        r_.skyColor = {0.05f, 0.055f, 0.07f};
        if (trRoomDirty_) {
            const Room rm = trRun_.room();
            const bool wallRoom = rm.mn.z == kWallZ;
            r_.setStaticBoxes([&] {
                const float t = 0.5f;
                const Vec3 c = (rm.mn + rm.mx) * 0.5f, e = (rm.mx - rm.mn) * 0.5f;
                const Vec3 wall(0.17f, 0.18f, 0.21f), floor(0.11f, 0.12f, 0.14f), front(0.21f, 0.22f, 0.26f);   // (no grid lines: owner, "distracting")
                r_.box({c.x, rm.mn.y - t, c.z}, {e.x + t, t, e.z + t}, 0, floor, 1.0f, false);
                r_.box({c.x, rm.mx.y + t, c.z}, {e.x + t, t, e.z + t}, 0, wall * 0.8f, 1.0f, false);
                r_.box({rm.mn.x - t, c.y, c.z}, {t, e.y + t, e.z + t}, 0, wall, 1.0f, false);
                r_.box({rm.mx.x + t, c.y, c.z}, {t, e.y + t, e.z + t}, 0, wall, 1.0f, false);
                r_.box({c.x, c.y, rm.mn.z - t}, {e.x + t, e.y + t, t}, 0, wallRoom ? front : wall, 1.0f, false);
                r_.box({c.x, c.y, rm.mx.z + t}, {e.x + t, e.y + t, t}, 0, wall, 1.0f, false);
                if (wallRoom) {   // the target area's frame on the wall
                    const Vec3 a(0.4f, 0.42f, 0.5f);
                    const float hx = trSc_->motion == MO_CROSS ? kCrossHX : trSc_->motion == MO_AIR ? kAir2HX : kWallHX;
                    const float hy = trSc_->motion == MO_CROSS ? kCrossHY : trSc_->motion == MO_AIR ? kAir2HY : kWallHY;
                    r_.box({0, hy, kWallZ + 0.02f}, {hx, 0.05f, 0.02f}, 0, a, 1.0f, false);
                    r_.box({0, -hy, kWallZ + 0.02f}, {hx, 0.05f, 0.02f}, 0, a, 1.0f, false);
                    r_.box({hx, 0, kWallZ + 0.02f}, {0.05f, hy, 0.02f}, 0, a, 1.0f, false);
                    r_.box({-hx, 0, kWallZ + 0.02f}, {0.05f, hy, 0.02f}, 0, a, 1.0f, false);
                }
                if (rm.pillar && !flat) r_.box({0, (rm.mn.y - 1.7f) * 0.5f, 0}, {0.5f, (-1.7f - rm.mn.y) * 0.5f, 0.5f}, 0, front, 1.0f, false);   // (what you stand on)
            });
            trRoomDirty_ = false;
        }
        r_.useStaticBoxes();
        // ---- the targets (switching: dimmer as their health goes)
        if (trSphere_.opaque.empty()) buildSphere(trSphere_);
        const Vec3 tcol = cfg_.colorEnemy;
        for (const Target& t : trRun_.targets()) {
            if (t.hide > 0) continue;
            Vec3 c = tcol;
            if (trSc_->hp > 0) c = tcol * (0.45f + 0.55f * clampf(t.hp / trSc_->hp, 0, 1));
            Vec3 tint[TINT_COUNT];
            for (auto& v : tint) v = c;
            Mat4 xf = Mat4::translate(t.pos);
            xf.m[0] = xf.m[5] = xf.m[10] = t.r;
            r_.mesh(trSphere_, xf, tint, 1.0f, false);
        }
        // ---- the gun (3D), your shots
        const Vec3 eye(0, 0, 0);
        if (!flat && !vmHidden(w)) {
            r_.viewOpacity = cfg_.viewmodelOpacity;
            ModelPalette vpal;
            vpal.c[TINT_METAL] = cfg_.gunColor == 1 ? cfg_.colorFriendly * 0.55f : cfg_.gunColor == 2 ? cfg_.gunCustom : viewmodelMetal(r_.metalColor);
            vpal.c[TINT_BASE] = viewmodelBase(r_.baseColor);
            vpal.c[TINT_TEAM] = cfg_.colorFriendly;
            vpal.c[TINT_GLOW] = r_.glowColor;
            const auto& vm = cfg_.hud[Settings::HUD_VIEWMODEL];
            const Mat4 wf = weaponFrame(cfg_.viewRecoil ? (float)recoil_ * 0.06f * cfg_.recoilAmount : 0.0f, w);
            const Vec3 at = {wf.m[12], wf.m[13], wf.m[14]};
            viewmodelParts(w, vpal, parts_);
            for (const ModelPart& q : parts_) {
                if (tilted(vm)) r_.viewBoxXf(wf * Mat4::translate(q.c * vm.scale), q.h * vm.scale, q.color);
                else r_.viewBox(at + q.c * vm.scale, q.h * vm.scale, q.color);
            }
        }
        const Vec3 from = muzzlePoint(w, eye);
        if (trLaserOn_ && !flat) r_.line(from, trLaserEnd_, 0.05f, fx);
        if (time_ < trTracerUntil_ && !flat) r_.line(from, trTracerEnd_, 0.03f, fx);
        r_.drawScene(false);
    }

    // ---- the HUD: time, score, accuracy; the countdown; the crosshair (2D: where the mouse has it)
    const float s = H / 720.0f, ts = std::max(1.0f, std::round(s * 1.5f));
    ui_.begin(&r_, uiIn_, s);
    const Stats& st = trRun_.stats();
    {
        const std::string tl = fmt1(trRun_.timeLeft());
        r_.text(W * 0.5f - r_.textWidth(tl, ts * 2) * 0.5f, 20 * s, ts * 2, tl, kWhite);
        const std::string sc = "SCORE " + std::to_string(trRun_.score());
        const std::string ac = (trSc_->cat == CAT_TRACK ? "DAMAGE " + std::to_string((int)st.damage) : "KILLS " + std::to_string(st.kills)) + "   ACC " + pct(trRun_.accuracy());
        r_.text(W * 0.5f - r_.textWidth(sc, ts) * 0.5f, 20 * s + ts * 20, ts, sc, kGold);
        r_.text(W * 0.5f - r_.textWidth(ac, ts) * 0.5f, 20 * s + ts * 34, ts, ac, kGrey);
        const std::string nm = std::string(trSc_->name) + "  -  " + trainer::modeName(trSc_->mode) + " " + catName(trSc_->cat) + " - " + diffName(trRun_.diff());
        r_.text(16 * s, 16 * s, std::max(1.0f, std::round(s)), nm, kGrey);
        r_.text(16 * s, 16 * s + 16 * s, std::max(1.0f, std::round(s)), "ESC: PAUSE / SETTINGS   R: RESTART", kGrey);
    }
    if (trRun_.countdown() > 0 && !trResults_) {
        const std::string c = std::to_string((int)std::ceil(trRun_.countdown()));
        r_.text(W * 0.5f - r_.textWidth(c, ts * 4) * 0.5f, H * 0.32f, ts * 4, c, kWhite);
    }
    if (trPaused_ && !trResults_) {   // the pause menu, or SETTINGS over the run
        if (settingsInGame_) { drawSettings(); hudBuilt_ = false; return; }
        const float pw = 320 * s, bh = 40 * s, gap = 10 * s, ph = 4 * bh + 3 * gap + 80 * s, px = W * 0.5f - pw * 0.5f, py = H * 0.5f - ph * 0.5f;
        ui_.panel(px, py, pw, ph, 0.92f);
        ui_.labelCentered(W * 0.5f, py + 16 * s, "PAUSED", kWhite, 1.5f, pw - 20 * s);
        float y = py + 60 * s;
        const float bx = px + 20 * s, bw = pw - 40 * s;
        if (ui_.button(bx, y, bw, bh, "RESUME (ESC)", UiExp::nav("trainer/run"))) { trPaused_ = false; captureMouse(true); }
        y += bh + gap;
        if (ui_.button(bx, y, bw, bh, "SETTINGS", UiExp::nav("trainer/run/pause/settings"))) { settingsInGame_ = true; openSettingsTab(settingsStartTab()); }
        y += bh + gap;
        if (ui_.button(bx, y, bw, bh, "RESTART (R)", UiExp::nav("trainer/run"))) { trainerStart(trSc_); return; }
        y += bh + gap;
        if (ui_.button(bx, y, bw, bh, "TRAINER MENU", UiExp::nav("trainer"))) { trainerExit(); return; }
    } else if (!trResults_) {
        float cx = W * 0.5f, cy = H * 0.5f;
        if (flat) toScreen(wallAim.x, wallAim.y, cx, cy);
        drawCrosshair(cx, cy, s, time_ < trHitUntil_, w);
    } else {   // the result
        const float pw = 420 * s, ph = 300 * s, px = W * 0.5f - pw * 0.5f, py = H * 0.5f - ph * 0.5f;
        ui_.panel(px, py, pw, ph, 0.9f);
        ui_.labelCentered(W * 0.5f, py + 16 * s, std::string(trSc_->name) + " - " + diffName(trRun_.diff()), kWhite, 1.5f, pw - 20 * s);
        ui_.labelCentered(W * 0.5f, py + 56 * s, std::to_string(trRun_.score()), kGold, 3.0f, pw - 20 * s);
        if (trNewBest_) ui_.labelCentered(W * 0.5f, py + 112 * s, "NEW BEST", ui_.accent, 1.0f, pw);
        const auto it = trScores_.find(trKey(trSc_->id, trRun_.diff()));
        const int best = it != trScores_.end() ? it->second.best : trRun_.score();
        const std::string l1 = trSc_->cat == CAT_TRACK ? "DAMAGE " + std::to_string((int)st.damage) : "KILLS " + std::to_string(st.kills);
        ui_.labelCentered(W * 0.5f, py + 140 * s, l1 + "   ACCURACY " + pct(trRun_.accuracy()), kWhite, 1.0f, pw);
        ui_.labelCentered(W * 0.5f, py + 164 * s, "BEST " + std::to_string(best), kGrey, 1.0f, pw);
        const float bw = 180 * s, bh = 40 * s, by = py + ph - 60 * s;
        if (ui_.button(W * 0.5f - bw - 6 * s, by, bw, bh, "AGAIN (R)", UiExp::nav("trainer/run"))) trainerStart(trSc_);
        else if (ui_.button(W * 0.5f + 6 * s, by, bw, bh, "MENU (ESC)", UiExp::nav("trainer"))) trainerExit();
    }
    if (ui_.probing()) uiTestActions();   // (the UI test: Esc, F3)
    hudBuilt_ = false;
}

// The menu: mode and difficulty along the top, the category under them, then the six scenarios as cards
void Game::drawTrainerMenu(const SetLayout& L) {
    using namespace trainer;
    if (!trScoresLoaded_) trainerLoadScores();
    ui_.panel(L.X(160), L.Y(166), L.L(960), L.L(522), 0.85f);
    ui_.labelCentered(L.X(640), L.Y(178), "AIM TRAINER", kWhite, 1.5f, L.L(928));
    const float th = L.L(32);
    for (int m = 0; m < TR_MODES; m++)
        if (ui_.tab(L.X(176 + m * 160), L.Y(216), L.L(152), th, trainer::modeName(m), trMode_ == m, trMode_ == m ? UiExp::inert() : UiExp::state("trainer"))) trMode_ = m;
    for (int d = 0; d < kDiffs; d++)
        if (ui_.tab(L.X(640 + d * 160), L.Y(216), L.L(152), th, diffName(d), trDiff_ == d, trDiff_ == d ? UiExp::inert() : UiExp::state("trainer"))) trDiff_ = d;
    for (int c = 0; c < CAT_COUNT; c++)
        if (ui_.tab(L.X(176 + c * 312), L.Y(256), L.L(304), th, catName(c), trCat_ == c, trCat_ == c ? UiExp::inert() : UiExp::state("trainer"), true)) trCat_ = c;
    const auto scs = list(trMode_, trCat_);
    for (size_t i = 0; i < scs.size(); i++) {
        const Scenario* sc = scs[i];
        const float x = 176 + (i % 3) * 312, y = 300 + (i / 3) * 160, cw = 304, ch = 152;
        r_.rect(L.X(x), L.Y(y), L.L(cw), L.L(ch), {0.08f, 0.09f, 0.11f, 0.95f});
        ui_.label(L.X(x + 10), L.Y(y + 8), sc->name, kWhite, 1.2f, L.L(cw - 20));
        ui_.hint(L.X(x + 10), L.Y(y + 32), sc->desc, kGrey, L.L(cw - 20));
        const auto it = trScores_.find(trKey(sc->id, trDiff_));
        const bool played = it != trScores_.end() && it->second.plays > 0;
        ui_.label(L.X(x + 10), L.Y(y + 52), played ? "BEST " + std::to_string(it->second.best) : "NOT PLAYED YET", played ? kGold : kGrey, 1.0f, L.L(cw - 20));
        if (played) ui_.hint(L.X(x + 10), L.Y(y + 74), "LAST " + std::to_string(it->second.last) + "   " + std::to_string(it->second.plays) + (it->second.plays == 1 ? " PLAY" : " PLAYS"), kGrey, L.L(cw - 20));
        ui_.hint(L.X(x + 10), L.Y(y + 90), "LEADERBOARD: SOON", kGrey, L.L(cw - 20));
        if (ui_.button(L.X(x + 10), L.Y(y + ch - 44), L.L(cw - 20), L.L(34), "PLAY", UiExp::nav("trainer/run").withId(sc->id))) { trainerStart(sc); return; }
    }
    ui_.hint(L.X(176), L.Y(622), trainer::modeName(trMode_) == std::string("2D") ? "2D: THE VIEW STAYS STILL, YOUR MOUSE MOVES THE CROSSHAIR" : "3D STATIC: YOU CAN'T MOVE - LOOK AND SHOOT",
             kGrey, L.L(600));
    ui_.hint(L.X(176), L.Y(638), trCat_ == CAT_CLICK ? "RIFLE, ONE SHOT A CLICK. SCORE: KILLS X 100 X ACCURACY" : trCat_ == CAT_SWITCH ? "LASER. SCORE: KILLS X 100 X ACCURACY" : "LASER. SCORE: DAMAGE X 10 X ACCURACY",
             kGrey, L.L(600));
    if (ui_.button(L.X(944), L.Y(632), L.L(176), L.L(40), "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
}
