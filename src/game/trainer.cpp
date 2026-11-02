// AIM TRAINER's simulation (trainer.h): the scenario table (owner, 2026-11-01) and the targets' motion, hits and score.
#include "trainer.h"
#include <algorithm>
#include <cmath>

namespace trainer {
namespace {
constexpr float kPi = 3.14159265f;
constexpr float kGrav = 9.8f;            // the bouncy balls' fall
constexpr float kReactorBack = 1.0f;     // reactor: back after a kill (owner: "takes 1s to spawn")
constexpr float kCrossGone = 0.5f;       // crossing guard: gone at the far edge (owner)
constexpr float kCrazyTurn = 0.3f;       // crazy ball: a new diagonal (owner)
constexpr float kGridStep = 8.0f;        // the 3 x 3 grid's spacing
constexpr float kDashAccel = 60.0f;      // the 180 wall's bursts: "accelerating quickly"
constexpr float kDashArc = 80.0f * kPi / 180.0f, kDashHY = 10.0f;   // how far round and up the 180 wall goes
constexpr float kRingR = 18.0f;          // the ring's radius
constexpr float kShellMin = 12, kShellMax = 24, kShellLow = -8, kShellHigh = 12;   // the 360 tracker's bounds (owner: 12..24 m)

// tracking: one ball, bigger than clicking's (it's held, not flicked to), faster each difficulty
#define TRACK_W {1.5f, 1.0f, 0.75f}
#define SPEEDS(a, b) {a, a * 1.25f, a * 1.5f}, {b, b * 1.25f, b * 1.5f}
#define FIXED(a, b) {a, a, a}, {b, b, b}
#define STILL {0, 0, 0}, {0, 0, 0}

const std::vector<Scenario> kScenarios = {
    // ---- 3D STATIC: clicking (rifle, one shot a click)
    {"3d-click-wallball", "WALL BALL", "BALLS ON THE WALL, BACK AT ONCE", TR_3D, CAT_CLICK, MO_WALL, {5, 4, 3}, {1, 0.75f, 0.5f}, 0, STILL, false},
    {"3d-click-airball", "AIR BALL", "BALLS ANYWHERE IN A 24 M ROOM", TR_3D, CAT_CLICK, MO_AIR, {5, 4, 3}, {1, 0.75f, 0.5f}, 0, STILL, false},
    {"3d-click-reactor", "REACTOR", "ONE BIG BALL, BACK AFTER 1 S", TR_3D, CAT_CLICK, MO_REACTOR, {1, 1, 1}, {2, 1.5f, 1}, 0, STILL, false},
    {"3d-click-bouncy", "BOUNCY BALLS", "BOUNCING ROUND A 24 M CIRCLE", TR_3D, CAT_CLICK, MO_BOUNCY, {5, 4, 3}, {1, 0.75f, 0.5f}, 0, FIXED(6, 12), false},
    {"3d-click-crossing", "CROSSING GUARD", "ACROSS THE WALL, SIDE TO SIDE", TR_3D, CAT_CLICK, MO_CROSS, {3, 3, 3}, {1, 0.75f, 0.5f}, 0, {12, 16, 24}, {24, 30, 36}, false},
    {"3d-click-crazy", "CRAZY BALL", "A NEW DIAGONAL EVERY 0.3 S", TR_3D, CAT_CLICK, MO_CRAZY, {7, 5, 3}, {1, 0.75f, 0.5f}, 0, FIXED(6, 6), false},
    // ---- 2D: clicking
    {"2d-click-wallball", "WALL BALL", "BALLS ON THE WALL, BACK AT ONCE", TR_2D, CAT_CLICK, MO_WALL, {5, 4, 3}, {1, 0.75f, 0.5f}, 0, STILL, false},
    {"2d-click-airball", "AIR BALL", "BALLS ANYWHERE ON A WIDE WALL", TR_2D, CAT_CLICK, MO_AIR, {5, 4, 3}, {1, 0.75f, 0.5f}, 0, STILL, false},
    {"2d-click-crossing", "CROSSING GUARD", "ACROSS THE WALL, SIDE TO SIDE", TR_2D, CAT_CLICK, MO_CROSS, {3, 3, 3}, {1, 0.75f, 0.5f}, 0, {12, 16, 24}, {24, 30, 36}, false},
    {"2d-click-crazy", "CRAZY BALL", "A NEW DIAGONAL EVERY 0.3 S", TR_2D, CAT_CLICK, MO_CRAZY, {7, 5, 3}, {1, 0.75f, 0.5f}, 0, FIXED(6, 6), false},
    {"2d-click-2ball", "2 BALL", "TWO TINY BALLS", TR_2D, CAT_CLICK, MO_WALL, {2, 2, 2}, {0.25f, 0.175f, 0.1f}, 0, STILL, false},
    {"2d-click-frenzy", "FRENZY", "THREE HUGE BALLS, AS FAST AS YOU CAN", TR_2D, CAT_CLICK, MO_WALL, {3, 3, 3}, {4, 3, 2}, 0, STILL, false},
    // ---- 3D STATIC: switching (laser, the balls take a moment)
    {"3d-switch-wallball", "WALL BALL", "BALLS DRIFTING SIDE TO SIDE, 10 HP", TR_3D, CAT_SWITCH, MO_SIDE_WALL, {5, 4, 3}, {1, 0.75f, 0.5f}, 10, FIXED(1, 1), true},
    {"3d-switch-reactor", "REACTOR", "TWO BALLS ON 9 SPOTS, 20 HP", TR_3D, CAT_SWITCH, MO_REACTOR9, {2, 2, 2}, {2, 1.5f, 1}, 20, STILL, true},
    {"3d-switch-airball", "AIR BALL", "BALLS DRIFTING IN THE ROOM, 10 HP", TR_3D, CAT_SWITCH, MO_SIDE_AIR, {5, 4, 3}, {1, 0.75f, 0.5f}, 10, FIXED(1, 1), true},
    {"3d-switch-crossing", "CROSSING GUARD", "ACROSS THE WALL, 10 HP", TR_3D, CAT_SWITCH, MO_CROSS, {3, 3, 3}, {1, 0.75f, 0.5f}, 10, {12, 16, 24}, {24, 30, 36}, true},
    {"3d-switch-crazy", "CRAZY BALL", "A NEW DIAGONAL EVERY 0.3 S, 10 HP", TR_3D, CAT_SWITCH, MO_CRAZY, {7, 5, 3}, {1, 0.75f, 0.5f}, 10, FIXED(6, 6), true},
    {"3d-switch-bouncy", "BOUNCY BALLS", "BOUNCING ROUND A 24 M CIRCLE, 20 HP", TR_3D, CAT_SWITCH, MO_BOUNCY, {5, 4, 3}, {1, 0.75f, 0.5f}, 20, FIXED(6, 12), true},
    // ---- 2D: switching
    {"2d-switch-wallball", "WALL BALL", "BALLS DRIFTING SIDE TO SIDE, 10 HP", TR_2D, CAT_SWITCH, MO_SIDE_WALL, {5, 4, 3}, {1, 0.75f, 0.5f}, 10, FIXED(1, 1), true},
    {"2d-switch-reactor", "REACTOR", "TWO BALLS ON 9 SPOTS, 20 HP", TR_2D, CAT_SWITCH, MO_REACTOR9, {2, 2, 2}, {2, 1.5f, 1}, 20, STILL, true},
    {"2d-switch-2ball", "2 BALL", "TWO TINY DRIFTING BALLS, 10 HP", TR_2D, CAT_SWITCH, MO_SIDE_WALL, {2, 2, 2}, {0.25f, 0.175f, 0.1f}, 10, FIXED(1, 1), true},
    {"2d-switch-crossing", "CROSSING GUARD", "ACROSS THE WALL, 10 HP", TR_2D, CAT_SWITCH, MO_CROSS, {3, 3, 3}, {1, 0.75f, 0.5f}, 10, {12, 16, 24}, {24, 30, 36}, true},
    {"2d-switch-crazy", "CRAZY BALL", "A NEW DIAGONAL EVERY 0.3 S, 10 HP", TR_2D, CAT_SWITCH, MO_CRAZY, {7, 5, 3}, {1, 0.75f, 0.5f}, 10, FIXED(6, 6), true},
    {"2d-switch-frenzy", "FRENZY", "THREE HUGE BALLS, JUST HOLD FIRE, 10 HP", TR_2D, CAT_SWITCH, MO_WALL, {3, 3, 3}, {4, 3, 2}, 10, STILL, true},
    // ---- 3D STATIC: tracking (laser, one ball that never dies)
    {"3d-track-bouncy", "BOUNCY BALL", "BOUNCING ROUND A 24 M CIRCLE", TR_3D, CAT_TRACK, MO_BOUNCY, {1, 1, 1}, TRACK_W, -1, SPEEDS(6, 12), true},
    {"3d-track-wander", "WALL WANDER", "RANDOM TURNS ON THE WALL", TR_3D, CAT_TRACK, MO_WANDER, {1, 1, 1}, TRACK_W, -1, SPEEDS(6, 10), true},
    {"3d-track-ring", "RING", "ROUND YOU AT ONE HEIGHT, CHANGING SPEED", TR_3D, CAT_TRACK, MO_RING, {1, 1, 1}, TRACK_W, -1, SPEEDS(4, 12), true},
    {"3d-track-360", "360", "ANYWHERE AROUND YOU, 12-24 M AWAY", TR_3D, CAT_TRACK, MO_SHELL, {1, 1, 1}, TRACK_W, -1, SPEEDS(5, 10), true},
    {"3d-track-grid", "GRID", "ALONG A 3 X 3 GRID'S LINES", TR_3D, CAT_TRACK, MO_GRID, {1, 1, 1}, TRACK_W, -1, SPEEDS(6, 10), true},
    {"3d-track-dash", "DASH", "BURSTS UP TO 24 M/S ON A 180 WALL", TR_3D, CAT_TRACK, MO_DASH, {1, 1, 1}, TRACK_W, -1, {1, 1, 1}, {16, 20, 24}, true},
    // ---- 2D: tracking (the 3D ones, flattened onto the wall)
    {"2d-track-bouncy", "BOUNCY BALL", "BOUNCING ROUND THE WALL", TR_2D, CAT_TRACK, MO_RECT, {1, 1, 1}, TRACK_W, -1, SPEEDS(6, 12), true},
    {"2d-track-wander", "WALL WANDER", "RANDOM TURNS ON THE WALL", TR_2D, CAT_TRACK, MO_WANDER, {1, 1, 1}, TRACK_W, -1, SPEEDS(6, 10), true},
    {"2d-track-strafe", "STRAFE", "LEFT AND RIGHT, CHANGING SPEED", TR_2D, CAT_TRACK, MO_STRAFE, {1, 1, 1}, TRACK_W, -1, SPEEDS(4, 12), true},
    {"2d-track-pulse", "PULSE", "WANDERING, SWELLING AND SHRINKING", TR_2D, CAT_TRACK, MO_PULSE, {1, 1, 1}, TRACK_W, -1, SPEEDS(5, 10), true},
    {"2d-track-grid", "GRID", "ALONG A 3 X 3 GRID'S LINES", TR_2D, CAT_TRACK, MO_GRID, {1, 1, 1}, TRACK_W, -1, SPEEDS(6, 10), true},
    {"2d-track-dash", "DASH", "BURSTS UP TO 24 M/S ON THE WALL", TR_2D, CAT_TRACK, MO_DASH, {1, 1, 1}, TRACK_W, -1, {1, 1, 1}, {16, 20, 24}, true},
};
#undef TRACK_W
#undef SPEEDS
#undef FIXED
#undef STILL

Vec3 onWall(float x, float y, float r) { return {x, y, kWallZ + r}; }
Vec3 gridNode(int n) { return {((n % 3) - 1) * kGridStep, ((n / 3) - 1) * kGridStep, 0}; }
float sgn(float v) { return v < 0 ? -1.0f : 1.0f; }
} // namespace

const std::vector<Scenario>& scenarios() { return kScenarios; }
const Scenario* findScenario(const std::string& id) {
    for (auto& s : kScenarios) if (id == s.id) return &s;
    return nullptr;
}
std::vector<const Scenario*> list(int mode, int cat) {
    std::vector<const Scenario*> out;
    for (auto& s : kScenarios) if (s.mode == mode && s.cat == cat) out.push_back(&s);
    return out;
}
const char* modeName(int m) { return m == TR_2D ? "2D" : "3D STATIC"; }
const char* catName(int c) { return c == CAT_SWITCH ? "SWITCHING" : c == CAT_TRACK ? "TRACKING" : "CLICKING"; }
const char* diffName(int d) { return d == 2 ? "HARD" : d == 1 ? "MEDIUM" : "EASY"; }

float Run::rnd() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (rng_ >> 8) * (1.0f / 16777216.0f);
}

void Run::start(const Scenario& s, int diff, uint32_t seed) {
    sc_ = &s;
    diff_ = std::max(0, std::min(kDiffs - 1, diff));
    rng_ = seed ? seed : 0x9E3779B9u;
    countdown_ = kCountdown;
    timeLeft_ = kRunTime;
    refire_ = 0;
    clickQueued_ = -1;
    finished_ = false;
    stats_ = Stats();
    targets_.assign(std::max(1, s.count[diff_]), Target());
    for (size_t i = 0; i < targets_.size(); i++) spawn((int)i, true);
}

bool Run::overlaps(int i, const Vec3& p, float r) const {
    for (size_t k = 0; k < targets_.size(); k++) {
        if ((int)k == i || targets_[k].hide > 0) continue;
        const Vec3 d = targets_[k].pos - p;
        const float m = targets_[k].r + r + 0.2f;
        if (d.x * d.x + d.y * d.y + d.z * d.z < m * m) return true;
    }
    return false;
}

// (re)place target i: a new spot, speed and heading for its motion
void Run::spawn(int i, bool first) {
    Target& t = targets_[i];
    const float r = sc_->width[diff_] * 0.5f;
    t.r = t.baseR = r;
    t.hp = sc_->hp;
    t.hide = 0;
    t.vel = {0, 0, 0};
    t.timer = 0;
    const int mo = sc_->motion;
    for (int tries = 0; tries < 30; tries++) {
        switch (mo) {
        case MO_WALL: case MO_REACTOR: case MO_CRAZY: case MO_SIDE_WALL: case MO_WANDER: case MO_PULSE:
            t.pos = onWall(rnd(-kWallHX + r, kWallHX - r), rnd(-kWallHY + r, kWallHY - r), r);
            break;
        case MO_AIR: case MO_SIDE_AIR:
            if (flat()) t.pos = onWall(rnd(-kAir2HX + r, kAir2HX - r), rnd(-kAir2HY + r, kAir2HY - r), r);
            else {   // anywhere in the cube room, at least 10 m away (owner)
                do t.pos = {rnd(-kRoomHalf + r, kRoomHalf - r), rnd(-kRoomHalf + r, kRoomHalf - r), rnd(-2 * kRoomHalf + r, -r)};
                while (length(t.pos) < 10.0f);
            }
            break;
        case MO_REACTOR9: {   // one of the 3 x 3 spots no other ball holds
            int free[9], n = 0;
            for (int s = 0; s < 9; s++) {
                bool held = false;
                for (size_t k = 0; k < targets_.size(); k++) held |= (int)k != i && targets_[k].slot == s;
                if (!held) free[n++] = s;
            }
            t.slot = n ? free[std::min(n - 1, (int)(rnd() * n))] : 0;
            const Vec3 g = gridNode(t.slot);
            t.pos = onWall(g.x, g.y, r);
            break;
        }
        case MO_BOUNCY: case MO_RECT: {
            if (mo == MO_BOUNCY) {
                t.angle = rnd(-kPi, kPi) * (first ? 0.6f : 1.0f);   // (the first ones start in front of you)
                t.pos = {std::sin(t.angle) * kArenaR, kArenaFloor + rnd(8, 20), -std::cos(t.angle) * kArenaR};
            } else t.pos = onWall(rnd(-kWallHX + r, kWallHX - r), rnd(-kWallHY + r, kWallHY - r), r);
            t.speed = speedRoll() * (rnd() < 0.5f ? -1.0f : 1.0f);
            t.vel = {mo == MO_RECT ? t.speed : 0, rnd(-4, 4), 0};
            break;
        }
        case MO_CROSS: {   // from one edge of the 48 x 24 wall to the other
            const float side = rnd() < 0.5f ? -1.0f : 1.0f;
            t.pos = onWall(side * (kCrossHX - r), rnd(-kCrossHY + r, kCrossHY - r), r);
            t.vel = {-side * speedRoll(), 0, 0};
            break;
        }
        case MO_RING:
            t.angle = rnd(-0.3f, 0.3f);
            t.pos = {std::sin(t.angle) * kRingR, 0, -std::cos(t.angle) * kRingR};
            t.speed = 0;
            t.goal = speedRoll() * (rnd() < 0.5f ? -1.0f : 1.0f);
            break;
        case MO_STRAFE:
            t.pos = onWall(rnd(-4, 4), 0, r);
            t.speed = 0;
            t.goal = speedRoll() * (rnd() < 0.5f ? -1.0f : 1.0f);
            break;
        case MO_SHELL: {
            const float a = rnd(-0.4f, 0.4f), d = rnd(kShellMin + 2, kShellMax - 2);
            t.pos = {std::sin(a) * d, rnd(-2, 4), -std::cos(a) * d};
            break;
        }
        case MO_GRID:
            t.from = 4;
            t.slot = 4;
            t.pos = onWall(0, 0, r);
            break;
        case MO_DASH:
            t.angle = 0;   // (where it is round the 180 wall; flat: x along the wall)
            t.phase = 0;   // (its height)
            t.pos = flat() ? onWall(0, 0, r) : Vec3{0, 0, -kArenaR + r};
            t.speed = 1;
            break;
        }
        if (!overlaps(i, t.pos, r)) break;
    }
    if (mo != MO_GRID && mo != MO_REACTOR9) t.slot = -1;
}

void Run::kill(int i) {
    stats_.kills++;
    Target& t = targets_[i];
    if (sc_->motion == MO_REACTOR || sc_->motion == MO_REACTOR9) { t.slot = -1; spawn(i, false); t.hide = kReactorBack; }
    else spawn(i, false);
}

// one target's motion over dt (the step splits a frame into short substeps)
void Run::move(Target& t, float dt) {
    const float r = t.r;
    auto bounceWall = [&](float hx, float hy) {   // keep it on the wall rectangle, turning back off its edges
        if (t.pos.x < -hx + r) { t.pos.x = -hx + r; t.vel.x = std::fabs(t.vel.x); }
        if (t.pos.x > hx - r) { t.pos.x = hx - r; t.vel.x = -std::fabs(t.vel.x); }
        if (t.pos.y < -hy + r) { t.pos.y = -hy + r; t.vel.y = std::fabs(t.vel.y); }
        if (t.pos.y > hy - r) { t.pos.y = hy - r; t.vel.y = -std::fabs(t.vel.y); }
    };
    switch (sc_->motion) {
    case MO_SIDE_WALL: case MO_SIDE_AIR: {
        if (t.vel.x == 0) t.vel.x = sc_->speedMax[diff_] * (rnd() < 0.5f ? -1.0f : 1.0f);
        t.pos.x += t.vel.x * dt;
        const float hx = sc_->motion == MO_SIDE_WALL ? kWallHX : flat() ? kAir2HX : kRoomHalf;
        if (t.pos.x < -hx + r) { t.pos.x = -hx + r; t.vel.x = std::fabs(t.vel.x); }
        if (t.pos.x > hx - r) { t.pos.x = hx - r; t.vel.x = -std::fabs(t.vel.x); }
        break;
    }
    case MO_BOUNCY: {   // round the circle; up and down between 6 m and an apex up to 24 m above the floor (owner)
        t.angle += t.speed / kArenaR * dt;
        t.vel.y -= kGrav * dt;
        float y = t.pos.y + t.vel.y * dt;
        const float low = kArenaFloor + 6.0f;
        if (y < low) { y = low; t.vel.y = std::sqrt(2 * kGrav * rnd(6.0f, 18.0f)); }   // (apex 12..24 m)
        t.pos = {std::sin(t.angle) * kArenaR, y, -std::cos(t.angle) * kArenaR};
        break;
    }
    case MO_RECT: {
        t.vel.y -= kGrav * dt;
        t.pos.x += t.vel.x * dt;
        t.pos.y += t.vel.y * dt;
        if (t.pos.y < -kWallHY + r) { t.pos.y = -kWallHY + r; t.vel.y = std::sqrt(2 * kGrav * rnd(kWallHY, 2 * kWallHY - 2 * r)); }
        if (t.pos.y > kWallHY - r) { t.pos.y = kWallHY - r; t.vel.y = -std::fabs(t.vel.y); }
        if (t.pos.x < -kWallHX + r) { t.pos.x = -kWallHX + r; t.vel.x = std::fabs(t.vel.x); }
        if (t.pos.x > kWallHX - r) { t.pos.x = kWallHX - r; t.vel.x = -std::fabs(t.vel.x); }
        break;
    }
    case MO_CROSS:
        t.pos.x += t.vel.x * dt;
        if (std::fabs(t.pos.x) > kCrossHX - r) {   // the far edge: gone 0.5 s, then a new run (owner)
            const float hide = kCrossGone;
            const int i = (int)(&t - targets_.data());
            spawn(i, false);
            targets_[i].hide = hide;
        }
        break;
    case MO_CRAZY:
        t.timer -= dt;
        if (t.timer <= 0) {   // one of the 4 diagonals (owner)
            t.timer += kCrazyTurn;
            const float s = sc_->speedMax[diff_] * 0.70710678f;
            t.vel = {rnd() < 0.5f ? -s : s, rnd() < 0.5f ? -s : s, 0};
        }
        t.pos.x += t.vel.x * dt; t.pos.y += t.vel.y * dt;
        bounceWall(kWallHX, kWallHY);
        break;
    case MO_WANDER: case MO_PULSE:
        t.timer -= dt;
        if (t.timer <= 0) {
            t.timer = rnd(0.3f, 0.9f);
            const float a = rnd(-kPi, kPi), s = speedRoll();
            t.vel = {std::cos(a) * s, std::sin(a) * s, 0};
        }
        t.pos.x += t.vel.x * dt; t.pos.y += t.vel.y * dt;
        if (sc_->motion == MO_PULSE) {   // swells and shrinks: 0.6x .. 1.4x (near and far)
            t.phase += dt * (1.5f + std::fabs(t.vel.x) * 0.1f);
            t.r = t.baseR * (1.0f + 0.4f * std::sin(t.phase));
            t.pos.z = kWallZ + t.r;
        }
        bounceWall(kWallHX, kWallHY);
        break;
    case MO_RING: case MO_STRAFE: {
        t.timer -= dt;
        if (t.timer <= 0) {   // a new speed to ease to, sometimes the other way
            t.timer = rnd(0.5f, 1.5f);
            t.goal = speedRoll() * (rnd() < 0.4f ? -sgn(t.goal) : sgn(t.goal));
        }
        const float ease = std::min(1.0f, dt * 4.0f);
        t.speed += (t.goal - t.speed) * ease;
        if (sc_->motion == MO_RING) {
            t.angle += t.speed / kRingR * dt;
            t.pos = {std::sin(t.angle) * kRingR, 0, -std::cos(t.angle) * kRingR};
        } else {
            t.pos.x += t.speed * dt;
            if (std::fabs(t.pos.x) > kWallHX - r) { t.pos.x = sgn(t.pos.x) * (kWallHX - r); t.speed = -t.speed; t.goal = -t.goal; }
        }
        break;
    }
    case MO_SHELL: {
        t.timer -= dt;
        if (t.timer <= 0) {
            t.timer = rnd(0.4f, 1.0f);
            Vec3 d = {rnd(-1, 1), rnd(-0.6f, 0.6f), rnd(-1, 1)};
            const float l = length(d);
            t.vel = l > 0.01f ? d * (speedRoll() / l) : Vec3{speedRoll(), 0, 0};
        }
        t.pos += t.vel * dt;
        const float d = length(t.pos);
        if (d < kShellMin || d > kShellMax) {   // turn its radial part back inside 12..24 m
            const Vec3 n = t.pos * (1.0f / std::max(d, 0.01f));
            const float vr = dot(t.vel, n);
            if ((d < kShellMin && vr < 0) || (d > kShellMax && vr > 0)) t.vel -= n * (2 * vr);
            t.pos = n * std::max(kShellMin, std::min(kShellMax, d));
        }
        if (t.pos.y < kShellLow) { t.pos.y = kShellLow; t.vel.y = std::fabs(t.vel.y); }
        if (t.pos.y > kShellHigh) { t.pos.y = kShellHigh; t.vel.y = -std::fabs(t.vel.y); }
        break;
    }
    case MO_GRID: {   // along the grid lines to the next node; there, a random neighbour
        const Vec3 goal = onWall(gridNode(t.slot).x, gridNode(t.slot).y, r);
        Vec3 d = goal - t.pos;
        const float l = length(d), step = sc_->speedMax[diff_] * dt;
        if (l <= step) {
            t.pos = goal;
            int nb[4], n = 0;
            const int gx = t.slot % 3, gy = t.slot / 3;
            if (gx > 0) nb[n++] = t.slot - 1;
            if (gx < 2) nb[n++] = t.slot + 1;
            if (gy > 0) nb[n++] = t.slot - 3;
            if (gy < 2) nb[n++] = t.slot + 3;
            t.from = t.slot;
            t.slot = nb[std::min(n - 1, (int)(rnd() * n))];
        } else t.pos += d * (step / l);
        break;
    }
    case MO_DASH: {   // 1 m/s, accelerating fast to a top speed rolled up to the max; reached: a new direction
        if (t.goal <= 0 || t.speed >= t.goal) {
            t.goal = rnd(std::max(4.0f, sc_->speedMax[diff_] * 0.35f), sc_->speedMax[diff_]);
            t.speed = sc_->speedMin[diff_];
            const float a = rnd(-kPi, kPi);
            t.vel = {std::cos(a), std::sin(a), 0};   // (a heading along the wall: x round it, y up)
        }
        t.speed = std::min(t.goal, t.speed + kDashAccel * dt);
        if (flat()) {
            t.pos.x += t.vel.x * t.speed * dt; t.pos.y += t.vel.y * t.speed * dt;
            if (t.pos.x < -kWallHX + r || t.pos.x > kWallHX - r) t.vel.x = -t.vel.x;
            if (t.pos.y < -kWallHY + r || t.pos.y > kWallHY - r) t.vel.y = -t.vel.y;
            t.pos.x = std::max(-kWallHX + r, std::min(kWallHX - r, t.pos.x));
            t.pos.y = std::max(-kWallHY + r, std::min(kWallHY - r, t.pos.y));
        } else {
            t.angle += t.vel.x * t.speed / kArenaR * dt;
            t.phase += t.vel.y * t.speed * dt;
            if (t.angle < -kDashArc || t.angle > kDashArc) { t.vel.x = -t.vel.x; t.angle = std::max(-kDashArc, std::min(kDashArc, t.angle)); }
            if (t.phase < -kDashHY || t.phase > kDashHY) { t.vel.y = -t.vel.y; t.phase = std::max(-kDashHY, std::min(kDashHY, t.phase)); }
            const float R = kArenaR - r;
            t.pos = {std::sin(t.angle) * R, t.phase, -std::cos(t.angle) * R};
        }
        break;
    }
    default: break;   // still ones
    }
}

int Run::raycast(const Vec3& dir, float range, float* dist) const {
    int best = -1;
    float bt = range;
    for (size_t i = 0; i < targets_.size(); i++) {
        const Target& t = targets_[i];
        if (t.hide > 0) continue;
        const float b = dot(t.pos, dir), c = dot(t.pos, t.pos) - t.r * t.r;
        const float disc = b * b - c;
        if (disc < 0) continue;
        const float hit = b - std::sqrt(disc);
        if (hit > 0 && hit < bt) { bt = hit; best = (int)i; }
    }
    if (dist) *dist = bt;
    return best;
}

StepEvents Run::step(float dt, const Vec3& aim, bool fire, bool click) {
    StepEvents ev;
    if (!sc_ || finished_ || dt <= 0) return ev;
    dt = std::min(dt, 0.1f);
    // the shot first, against the targets where the player saw them (the last frame drew them there), then the motion
    if (countdown_ <= 0) {   // (shots before the clock starts don't count)
        refire_ = std::max(0.0f, refire_ - dt);
        if (sc_->laser) {
            if (fire) {
                stats_.fireTime += dt;
                float d = kLaserRange;
                const int i = raycast(aim, kLaserRange, &d);
                ev.laserEnd = aim * d;
                if (i >= 0) {
                    ev.laserOn = true;
                    const float dmg = kLaserDps * dt;
                    ev.damage = dmg;
                    stats_.onTime += dt;
                    stats_.damage += dmg;
                    Target& t = targets_[i];
                    if (t.hp > 0) { t.hp -= dmg; if (t.hp <= 0) { kill(i); ev.kills++; } }
                }
            }
        } else {   // the rifle, autofire off: one shot a click (owner); a click just before it's ready waits for it
            if (click) clickQueued_ = kClickBuffer;
            if (clickQueued_ >= 0 && refire_ <= 0) {
                clickQueued_ = -1;
                refire_ = kRifleRefire;
                stats_.shots++;
                ev.shot = true;
                float d = kLaserRange * 4;
                const int i = raycast(aim, d, &d);
                ev.shotEnd = aim * d;
                if (i >= 0) { ev.shotHit = true; stats_.hits++; kill(i); ev.kills++; }
            } else if (clickQueued_ >= 0) clickQueued_ -= dt;
        }
    }
    // the targets: substeps of at most 2 ms (fast ones bounce on time)
    const int n = std::max(1, (int)std::ceil(dt / 0.002f));
    const float h = dt / n;
    for (int k = 0; k < n; k++)
        for (auto& t : targets_) {
            if (t.hide > 0) { t.hide -= h; continue; }
            move(t, h);
        }
    if (countdown_ > 0) {
        countdown_ -= dt;
        clickQueued_ = -1;
        return ev;
    }
    timeLeft_ -= dt;
    if (timeLeft_ <= 0) { timeLeft_ = 0; finished_ = true; }
    return ev;
}

float Run::accuracy() const {
    if (sc_ && sc_->laser) return stats_.fireTime > 0 ? (float)(stats_.onTime / stats_.fireTime) : 0.0f;
    return stats_.shots > 0 ? (float)stats_.hits / stats_.shots : 0.0f;
}

int Run::score() const {
    if (!sc_) return 0;
    if (sc_->cat == CAT_TRACK) return (int)std::lround(stats_.damage * 10.0 * accuracy());
    return (int)std::lround(stats_.kills * 100.0 * accuracy());
}

Room Run::room() const {
    Room rm;
    if (!sc_) return rm;
    switch (sc_->motion) {
    case MO_AIR: case MO_SIDE_AIR:
        if (!flat()) { rm.mn = {-kRoomHalf, -kRoomHalf, -2 * kRoomHalf}; rm.mx = {kRoomHalf, kRoomHalf, 0.6f}; rm.pillar = false; return rm; }
        break;
    case MO_BOUNCY: case MO_RING: case MO_SHELL:
        rm.mn = {-32, kArenaFloor, -32}; rm.mx = {32, kArenaFloor + 30, 32}; return rm;
    case MO_DASH:
        if (!flat()) { rm.mn = {-kArenaR - 1, -kDashHY - 3, -kArenaR}; rm.mx = {kArenaR + 1, kDashHY + 3, 6}; return rm; }
        break;
    default: break;
    }
    rm.mn = {-kCrossHX - 1, -kCrossHY - 1, kWallZ}; rm.mx = {kCrossHX + 1, kCrossHY + 1, 8};
    return rm;
}

} // namespace trainer
