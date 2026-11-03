// TUTORIAL: the lessons, their ghost scripts and their checks (tutorial.h)
#include "tutorial.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace tut {
namespace {

constexpr int8_t F = 127, B = -127, R = 127, Lf = -127;
constexpr uint16_t J = BTN_JUMP, D = BTN_DASH, G = BTN_GRAPPLE, FI = BTN_FIRE, AL = BTN_ALTFIRE;

// ---- the ghost's scripts (quark_tests "tutorial" runs each on the map and checks it does what its lesson asks) ----
// Walking: run to the hurdles, jump each (a tap), on to the end
const Seg kMove[] = {
    {950, F, 0, 0, 0, 0}, {120, F, 0, J, 0, 0}, {1130, F, 0, 0, 0, 0}, {120, F, 0, J, 0, 0}, {2300, F, 0, 0, 0, 0}, {900, 0, 0, 0, 0, 0},
};
// A/D strafing: a run, then hops with jump held; each hop one strafe key held and the view turned the same way, W let
// go (the first hop half the turn: the path snakes along the lane)
const Seg kStrafe[] = {
    {350, F, 0, J, 0, 0},
    {640, 0, R, J, -45, 0}, {640, 0, Lf, J, 90, 0}, {640, 0, R, J, -90, 0}, {640, 0, Lf, J, 90, 0}, {640, 0, R, J, -90, 0},
    {640, 0, Lf, J, 90, 0}, {640, 0, R, J, -90, 0}, {640, 0, Lf, J, 90, 0}, {640, 0, R, J, -90, 0}, {640, 0, Lf, J, 90, 0},
    {320, 0, R, J, -90, 0}, {1200, 0, 0, 0, 0, 0},
};
// The dash: run, jump at the gap's edge, dash at the top of the jump
const Seg kDash[] = {
    {1300, F, 0, 0, 0, 0}, {100, F, 0, J, 0, 0}, {220, F, 0, 0, 0, 0}, {120, F, 0, D, 0, 0}, {1600, F, 0, 0, 0, 0}, {900, 0, 0, 0, 0, 0},
};
// Sideways dashes: looking ahead, dash holding D (right), then holding A (left)
const Seg kSideDash[] = {
    {500, 0, 0, 0, 0, 0}, {120, 0, R, D, 0, 0}, {1100, 0, 0, 0, 0, 0}, {120, 0, Lf, D, 0, 0}, {1100, 0, 0, 0, 0, 0},
};
// Super: dash along the floor, jump 30 ms into it; twice
const Seg kSuper[] = {
    {400, 0, 0, 0, 0, 0}, {30, F, 0, D, 0, 0}, {80, F, 0, D | J, 0, 0}, {1900, 0, 0, 0, 0, 0},
    {30, F, 0, D, 0, 0}, {80, F, 0, D | J, 0, 0}, {1900, 0, 0, 0, 0, 0},
};
// Hyper: run, look 45 degrees down, jump and dash together (the dash into the floor while jump is held); twice
const Seg kHyper[] = {
    {600, F, 0, 0, 0, 0}, {80, F, 0, J | D, 0, -45}, {1400, F, 0, 0, 0, 0}, {80, F, 0, J | D, 0, -45}, {1700, 0, 0, 0, 0, 0},
};
// Super chaining: W and jump held (bunny hops), looking 30 degrees down, dash held: each dash fires the moment it may,
// meets the floor and is a hyper, +5 m/s each
const Seg kChain[] = {
    {400, F, 0, J, 0, -20}, {5400, F, 0, J | D, 0, -20}, {1400, 0, 0, 0, 0, 0},
};
// The dash slide: a super for speed, then just before landing look 20 degrees down and dash (no jump): it slides on
// at ice's grip until you're down to run speed; a jump out keeps what's left
const Seg kSlide[] = {
    {400, 0, 0, 0, 0, 0}, {30, F, 0, D, 0, 0}, {80, F, 0, D | J, 0, 0}, {560, F, 0, 0, 0, 0}, {120, F, 0, D, 0, -20},
    {1500, F, 0, 0, 0, -20}, {100, F, 0, J, 0, 0}, {1600, 0, 0, 0, 0, 0},
};
// The wall bounce: walk up to the end wall, dash into it head on
const Seg kBounce[] = {
    {820, F, 0, 0, 0, 0}, {120, F, 0, D, 0, 0}, {1100, 0, 0, 0, 0, 0}, {520, F, 0, 0, 0, 0}, {120, F, 0, D, 0, 0}, {1300, 0, 0, 0, 0, 0},
};
// The grapple: look up past the ledge's edge (the hook takes the ceiling over it), hold grapple: it pulls you to the
// wall and up it; let go once over the edge and hold W onto the ledge
const Seg kGrapple[] = {
    {400, 0, 0, 0, 0, 30}, {1550, F, 0, G, 0, 30}, {1200, F, 0, 0, 0, 0}, {800, 0, 0, 0, 0, 0},
};
// Grapple walking: aim at the floor far ahead, hold grapple: dragged along it at up to 24 m/s
const Seg kHookWalk[] = {
    {300, 0, 0, 0, 0, -5}, {1100, F, 0, G, 0, -5}, {1600, 0, 0, 0, 0, 0},
};
// Rocket jump: look straight down, jump and fire together
const Seg kRocketJump[] = {
    {600, 0, 0, 0, 0, -89}, {60, F, 0, J | FI, 0, -89}, {1500, F, 0, 0, 0, -20}, {1400, 0, 0, 0, 0, 0},
};
// THE LAUNCH (key tech): grapple the floor far ahead to 24 m/s, then look straight down: jump, dash and fire at once (the
// hyper takes the 24 m/s carried in, + 5, 90% of it up; the rocket at your feet adds its blast)
const Seg kLaunch[] = {
    {300, 0, 0, 0, 0, -5}, {900, F, 0, G, 0, -5}, {40, F, 0, J | D | FI, 0, -89}, {3800, F, 0, 0, 0, 0},
};
// Sticky jump: a sticky at your feet, wait for it to arm, then jump and detonate (alt-fire) together
const Seg kStickyJump[] = {
    {500, 0, 0, 0, 0, -89}, {60, 0, 0, FI, 0, -89}, {500, 0, 0, 0, 0, -89}, {80, F, 0, J | AL, 0, -89}, {1500, F, 0, 0, 0, -20}, {1400, 0, 0, 0, 0, 0},
};
// Grapple a player (key tech): the PARTNER runs across the bay; aimed at it, a press of grapple latches on and pulls
// the two together (it lets go as they meet)
const Seg kHookPlayer[] = {
    {900, 0, 0, 0, 0, kAimAt}, {1000, 0, 0, G, 0, kAimAt}, {2100, 0, 0, 0, 0, 0},
};
const Seg kHookPlayerPartner[] = {   // (facing back down the bay: R and L across it)
    {300, 0, 0, 0, 0, 0}, {700, 0, R, 0, 0, 0}, {1400, 0, Lf, 0, 0, 0}, {1400, 0, R, 0, 0, 0}, {200, 0, 0, 0, 0, 0},
};
// Dash out of a grapple (key tech): the PARTNER hooks the ghost (you, your turn) from the far end; pulled toward it a
// moment, the ghost dashes (left, away from your lane): the pull lets go at once
const Seg kBreakFree[] = {
    {950, 0, 0, 0, 0, 0}, {120, 0, Lf, D, 0, 0}, {2430, 0, 0, 0, 0, 0},
};
const Seg kBreakFreePartner[] = {
    {600, 0, 0, 0, 0, kAimAt}, {900, 0, 0, G, 0, kAimAt}, {2000, 0, 0, 0, 0, 0},
};
// The rocket grapple (rocket riding): fire ahead and hold grapple along the rocket: it catches it and rides it
const Seg kRocketRide[] = {
    {500, 0, 0, 0, 0, 2}, {60, 0, 0, FI | G, 0, 2}, {2200, 0, 0, G, 0, 2}, {1400, 0, 0, 0, 0, 0},
};

#define SCRIPT(a) a, (int)(sizeof(a) / sizeof(a[0]))
// (the bays: tools/maps/tutorial.py builds them from the same lengths and heights, in this order)
const LessonDef kLessons[L_COUNT] = {
    //  id            len    height weapon             script                   goal         target aux    key
    {"move",          50,    12,  kNoWeaponChange,  SCRIPT(kMove),           G_REACH,      44,    0,     false},
    {"strafe",        150,   12,  kNoWeaponChange,  SCRIPT(kStrafe),         G_SPEED,      12,    0,     false},
    {"dash",          50,    12,  kNoWeaponChange,  SCRIPT(kDash),           G_GAP,        26,    16,    false},
    {"sidedash",      30,    12,  kNoWeaponChange,  SCRIPT(kSideDash),       G_SIDEDASH,   2,     0,     false},
    {"super",         60,    12,  kNoWeaponChange,  SCRIPT(kSuper),          G_EVENTS,     2,     ME_SUPER, false},
    {"hyper",         60,    12,  kNoWeaponChange,  SCRIPT(kHyper),          G_EVENTS,     2,     ME_HYPER, false},
    {"chain",         180,   16,  kNoWeaponChange,  SCRIPT(kChain),          G_SPEED,      27,    1,     false},
    {"slide",         60,    12,  kNoWeaponChange,  SCRIPT(kSlide),          G_SLIDE,      0.8f,  0,     false},
    {"bounce",        12,    12,  kNoWeaponChange,  SCRIPT(kBounce),         G_BOUNCES,    2,     0,     false},
    {"grapple",       40,    18,  kNoWeaponChange,  SCRIPT(kGrapple),        G_LEDGE,      9.5f,  0,     false},
    {"hookwalk",      80,    12,  kNoWeaponChange,  SCRIPT(kHookWalk),       G_HOOKSPEED,  16,    0,     false},
    {"hookplayer",    50,    14,  kNoWeaponChange,  SCRIPT(kHookPlayer),     G_HOOKPLAYER, 1,     0,     true,  SCRIPT(kHookPlayerPartner), 30},
    {"breakfree",     50,    12,  kNoWeaponChange,  SCRIPT(kBreakFree),      G_BREAKFREE,  1,     0,     true,  SCRIPT(kBreakFreePartner), 34},
    {"rocketjump",    40,    24,  WPN_ROCKET,       SCRIPT(kRocketJump),     G_KNOCKUP,    7,     0,     false},
    {"launch",        160,   72,  WPN_ROCKET,       SCRIPT(kLaunch),         G_LAUNCH,     16,    20,    true},
    {"stickyjump",    40,    24,  WPN_STICKY,       SCRIPT(kStickyJump),     G_KNOCKUP,    7,     0,     false},
    {"rocketgrapple", 110,   16,  WPN_ROCKET,       SCRIPT(kRocketRide),     G_RIDE,       0.4f,  0,     true},
};
#undef SCRIPT

float hspeed(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

}  // namespace

const LessonDef& lesson(int l) { return kLessons[l < 0 || l >= L_COUNT ? 0 : l]; }

int lessonFromName(const char* s) {
    if (!s || !*s) return -1;
    for (int l = 0; l < L_COUNT; l++) if (std::strcmp(s, kLessons[l].id) == 0) return l;
    char* end = nullptr;
    long n = std::strtol(s, &end, 10);
    return end && *end == 0 && n >= 0 && n < L_COUNT ? (int)n : -1;
}

float bayX0(int) { return kBayX0; }
float bayZ(int l) { return (std::clamp(l, 0, L_COUNT - 1) - (L_COUNT - 1) * 0.5f) * (2 * kBayHalfZ + kWall); }
Vec3 ghostStart(int l) { return {bayX0(l) + kStartIn, 0, bayZ(l) + kGhostZ}; }
Vec3 playerStart(int l) { return {bayX0(l) + kStartIn, 0, bayZ(l) + kPlayerZ}; }

int loopMs(int l) {
    const LessonDef& d = lesson(l);
    int t = 0;
    for (int i = 0; i < d.segs; i++) t += d.script[i].ms;
    return t;
}

namespace {
// a script's command at tMs into its loop (the last step held past its end); `aim`: whether that step aims at the other
UserCmd scriptCmd(const Seg* sc, int segs, int tMs, float yaw0, uint8_t weapon, bool* aim = nullptr) {
    UserCmd c;
    c.yaw = yaw0;
    c.weapon = weapon;
    if (aim) *aim = false;
    if (tMs < 0 || !sc) return c;
    float yawDeg = 0;
    int t0 = 0;
    for (int i = 0; i < segs; i++) {
        const Seg& s = sc[i];
        if (tMs < t0 + s.ms || i == segs - 1) {
            const int in = std::min(tMs - t0, (int)s.ms);
            yawDeg += s.yawRate * in * 0.001f;
            c.forward = s.fwd;
            c.side = s.side;
            c.buttons = s.btn;
            c.pitch = s.pitch == kAimAt ? 0.0f : s.pitch * (PI / 180.0f);
            c.yaw = yaw0 + yawDeg * (PI / 180.0f);
            if (aim) *aim = s.pitch == kAimAt;
            return c;
        }
        yawDeg += s.yawRate * s.ms * 0.001f;
        t0 += s.ms;
    }
    return c;
}
}  // namespace

UserCmd ghostCmd(int l, int tMs) {
    const LessonDef& d = lesson(l);
    return scriptCmd(d.script, d.segs, tMs, kStartYaw, d.weapon);
}

UserCmd partnerCmd(int l, int tMs) {
    const LessonDef& d = lesson(l);
    return scriptCmd(d.partner, d.partnerSegs, tMs, kPartnerYaw, kNoWeaponChange);
}

Vec3 partnerStart(int l) { return {bayX0(l) + lesson(l).partnerIn, 0, bayZ(l)}; }

bool ghostAimsAt(int l, int tMs) {
    const LessonDef& d = lesson(l);
    bool a;
    scriptCmd(d.script, d.segs, tMs, kStartYaw, d.weapon, &a);
    return a;
}

bool partnerAimsAt(int l, int tMs) {
    const LessonDef& d = lesson(l);
    bool a;
    scriptCmd(d.partner, d.partnerSegs, tMs, kPartnerYaw, kNoWeaponChange, &a);
    return a;
}

void aimAt(const Vec3& eye, const Vec3& at, float& yaw, float& pitch) {
    const Vec3 d = at - eye;
    yaw = std::atan2(-d.x, -d.z);
    pitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
}

void Check::start(int l) {
    *this = Check();
    lesson = l;
}

void Check::frame(const MoveState& m, uint8_t ev, int fwd, int side, int weapon, float dt) {
    if (lesson < 0 || done) return;
    const LessonDef& d = tut::lesson(lesson);
    const float x = m.pos.x - bayX0(lesson), y = m.pos.y, h = hspeed(m.vel);
    const bool ground = (m.flags & MF_ONGROUND) != 0, hookedNow = (m.flags & MF_HOOKED) != 0;
    // thrown up by a blast: the upward speed jumped this frame by more than gravity takes, with no jump, dash or
    // grapple of your own to explain it (a landing only stops a fall: it never leaves you rising)
    // (a jump in the same frame: what's past the jump's own speed; a sticky goes off in the command that jumps)
    const float rise = m.vel.y - prevVel.y - ((ev & ME_JUMP) ? moveBaseJump() : 0.0f);
    const bool knock = havePrev && rise >= 4.0f && m.vel.y >= 4.0f && !hookedNow && m.dashTimer <= 0 && !(ev & (ME_DASH | ME_SUPER | ME_HYPER));
    if (ev & ME_DASH) dashed = true;
    if ((ev & ME_HOOK) || hookedNow) hooked = true;
    // standing still on the floor starts the attempt over (G_SPEED: no dash or grapple since)
    still = ground && h < 1.0f ? still + dt : 0.0f;
    if (still > 0.3f) dashed = hooked = knocked = armed = false;
    value = 0;
    switch (d.goal) {
        case G_REACH:
            value = std::max(0.0f, x - kStartIn);
            progress = value / (d.target - kStartIn);
            done = ground && x >= d.target;
            break;
        case G_SPEED:
            if (d.aux == 0 && (dashed || hooked)) { value = 0; break; }   // (strafing alone: a dash or a grapple doesn't count)
            value = h;
            best = std::max(best, h);
            progress = best / d.target;
            done = h >= d.target;
            break;
        case G_GAP:
            if (ground && x < d.aux) dashed = false;          // (back before the gap: a new try)
            value = std::max(0.0f, x - kStartIn);
            progress = std::min(1.0f, value / (d.target - kStartIn));
            done = ground && x >= d.target && dashed;
            break;
        case G_SIDEDASH:
            if (ev & ME_DASH) {
                if (fwd == 0 && side < 0) left = true;
                if (fwd == 0 && side > 0) right = true;
            }
            count = (int)left + (int)right;
            value = (float)count;
            progress = count / d.target;
            done = left && right;
            break;
        case G_EVENTS:
            if (ev & (uint8_t)d.aux) count++;
            value = (float)count;
            progress = count / d.target;
            done = count >= d.target;
            break;
        case G_SLIDE:
            timer = m.dashSlide ? timer + dt : 0.0f;
            best = std::max(best, timer);
            value = timer;
            progress = best / d.target;
            done = timer >= d.target;
            break;
        case G_BOUNCES:
            if (ev & ME_DASH) prevBounces = 0;   // (a new dash starts its own count, maybe already bounced this frame)
            if (m.dashBounces > prevBounces) count += m.dashBounces - prevBounces;
            prevBounces = m.dashBounces;
            value = (float)count;
            progress = count / d.target;
            done = count >= d.target;
            break;
        case G_LEDGE:
            if (ground && y < d.target - 1.0f) hooked = hookedNow;
            value = std::max(0.0f, y);
            best = std::max(best, value);
            progress = best / d.target;
            done = ground && y >= d.target && hooked;
            break;
        case G_HOOKSPEED: {
            const bool low = hookedNow && m.hookTarget == 0 && m.hookPoint.y < y + 1.0f && y < 1.0f;
            value = low ? h : 0.0f;
            best = std::max(best, value);
            progress = best / d.target;
            done = low && h >= d.target;
            break;
        }
        case G_KNOCKUP:
            if (knock && weapon == d.weapon) knocked = true;
            if (ground && !knock) knocked = knocked && y >= 0.5f;   // (back on the floor: a new try)
            value = knocked ? std::max(0.0f, y) : 0.0f;
            best = std::max(best, value);
            progress = best / d.target;
            done = knocked && y >= d.target;
            break;
        case G_RIDE:
            timer = hookedNow && m.hookTarget == kHookRocket ? timer + dt : 0.0f;
            best = std::max(best, timer);
            value = timer;
            progress = best / d.target;
            done = timer >= d.target;
            break;
        case G_LAUNCH:
            if ((ev & ME_HYPER) && m.dashFromSpeed >= d.aux) armed = true;   // (dashFromSpeed: the speed carried into that dash)
            if (armed && knock && weapon == WPN_ROCKET) knocked = true;
            if (ground && !(ev & ME_HYPER) && !knock && m.vel.y <= 0) armed = knocked = false;
            value = armed ? std::max(0.0f, y) : 0.0f;
            best = std::max(best, knocked ? value : 0.0f);
            progress = best / d.target;
            done = armed && knocked && y >= d.target;
            break;
        case G_HOOKPLAYER: {   // (the moment it latches on: a player's id + 1, not the map, a rocket or the ball)
            const bool on = hookedNow && m.hookTarget >= 1 && m.hookTarget <= kMaxPlayers;
            if (on && !prevOn) count++;
            prevOn = on;
            value = (float)count;
            progress = count / d.target;
            done = count >= d.target;
            break;
        }
        case G_BREAKFREE: {   // (held last frame, a dash this one, held no more: pmove's dash clears MF_PULLED)
            const bool pulled = (m.flags & MF_PULLED) != 0;
            if (prevOn && !pulled && (ev & ME_DASH)) count++;
            prevOn = pulled;
            value = (float)count;
            progress = count / d.target;
            done = count >= d.target;
            break;
        }
    }
    if (progress > 1) progress = 1;
    if (progress < 0) progress = 0;
    if (done) progress = 1;
    prevVel = m.vel;
    havePrev = true;
}

}  // namespace tut
