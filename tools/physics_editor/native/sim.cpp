// The physics editor's scenario runner: the game's own pmove() (src/game/pmove.cpp), 1 ms per command like the
// 1000 Hz server, under a physics set applied with setActiveRules. Commands: physsim, physpresets, physdrive.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include "maptool.h"
#include "physics.h"
#include "game/gamedefs.h"

namespace phys {

float hspeed(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float roundTo(float v, double q) { return (float)(std::round((double)v / q) * q); }   // (in double: the nearest float to 1.2208, not 12208 x 1e-4f)
Json vec(const Vec3& v, double q) { Json a = Json::array(); a.push(roundTo(v.x, q)); a.push(roundTo(v.y, q)); a.push(roundTo(v.z, q)); return a; }

namespace {
constexpr float kRad = PI / 180.0f;
inline float headingOf(float x, float z) { return std::atan2(-x, -z); }   // the yaw whose forward points along (x, z)
inline bool onGroundF(const MoveState& s) { return (s.flags & MF_ONGROUND) != 0; }
bool ladderAt(const MoveState& s, const Map& map) {
    return !(s.flags & MF_HOOKED) && s.dashTimer <= 0 && map.onLadder(playerBox(s));
}
// does the floor hold a hull standing at p (the game's own ground probe: 5 cm down)
bool supported(const Map& map, const Vec3& p) { return map.sweepAxis(playerBox(p, false), 1, -0.05f) > -0.049f; }
}

// ---- the strafe bot ------------------------------------------------------------------------------------------------
// The angle between the velocity and the wish direction that gains the most speed this ms. pmove's air acceleration
// (airAccelerate) is Quake's: push = min(accel x wishSpeed x dt, wishSpeed - v.wish), so the best push fills the
// whole step, v.wish = wishSpeed - accel x wishSpeed x dt, cos(angle) = that / |v|. The numbers are the active custom
// mode's (moveParamBase x multiplier); the A/D factors (x1.1 cap, x1.15 accel, the boost's 0.25 ramp) are pmove.cpp's
// kStrafeCapAD / kStrafeAccelAD (test_phys.py checks them against the source). Mode 2 checks it the slow way: it
// tries angles through pmove itself on a copy of the state and keeps the fastest.
float strafeYaw(const MoveState& s, int forward, int side, float curYawDeg, int mode, const Map& map, uint16_t buttons, float pitchDeg) {
    const float h = hspeed(s.vel);
    if (h < 1.0f || (s.flags & MF_HOOKED)) return curYawDeg;
    if (onGroundF(s) && !(buttons & BTN_JUMP)) return curYawDeg;   // (on the floor: running, not strafing)
    if (forward == 0 && side == 0) side = 127;
    const float sgn = side > 0 ? -1.0f : 1.0f;                      // D: the wish ahead of the velocity, turning right
    const float off = std::atan2(-(float)side, (float)forward);     // the wish's heading at yaw 0
    const float velHeading = headingOf(s.vel.x, s.vel.z);
    auto yawFor = [&](float theta) { return (velHeading + sgn * theta - off) / kRad; };
    if (mode == 2) {
        auto gain = [&](float theta) {
            MoveState t = s;
            UserCmd c; c.forward = (int8_t)forward; c.side = (int8_t)side; c.buttons = buttons; c.msec = 1;
            c.yaw = yawFor(theta) * kRad; c.pitch = pitchDeg * kRad;
            pmove(t, c, map);
            return hspeed(t.vel);
        };
        float best = 0, bestG = -1;
        for (int i = 0; i <= 36; i++) { float th = i * (PI / 36); float g = gain(th); if (g > bestG) { bestG = g; best = th; } }
        for (float span = PI / 36; span > 0.0005f; span *= 0.25f)
            for (int i = -4; i <= 4; i++) { float th = best + i * span * 0.25f; float g = gain(th); if (g > bestG) { bestG = g; best = th; } }
        return yawFor(best);
    }
    MoveTuning scratch;   // (the values this ms plays by: movement equations worked out for s, as pmove does)
    const MoveTuning& mt = stepTuning(s, &map, scratch);
    const float airAccel = moveParamBase(MV_AIR_ACCEL) * mt.m[MV_AIR_ACCEL] * 10.0f;
    const float airCap = moveParamBase(MV_AIR_CAP) * mt.m[MV_AIR_CAP];
    const float airControl = moveParamBase(MV_AIR_CONTROL) * mt.m[MV_AIR_CONTROL];
    const float cross = airAccel * airCap / std::max(1e-4f, airControl);
    float accel = airAccel, cap = airCap;
    if (side != 0) {
        cap *= 1.1f;   // kStrafeCapAD
        const float t = std::clamp((h - cross) / (0.25f * cross), 0.0f, 1.0f);
        accel *= 1.15f * (1.0f + moveParamBase(MV_STRAFE_BOOST) * mt.m[MV_STRAFE_BOOST] * t);   // kStrafeAccelAD
    }
    const float wishLen = std::min(1.0f, std::sqrt((float)(forward * forward + side * side)) / 127.0f);
    const float ws = std::min(wishLen * moveBaseSpeed() * mt.speed, cap);
    const float push = accel * ws * kMoveStep;
    const float c = std::clamp((ws - push) / h, 0.0f, 1.0f);
    return yawFor(std::acos(c));
}

// ---- events --------------------------------------------------------------------------------------------------------
namespace {
struct Watch {
    bool onLadder = false;
    bool air = false;                 // off the floor since a takeoff
    std::string takeoffKind;
    int takeoffMs = 0;
    Vec3 takeoffPos;
    float apexY = 0;
    int lastWallMs = -1000;
    int lastLandMs = -1000;
    bool landedThisStep = false;
    void reset(const MoveState& s, const Map& map) {
        onLadder = ladderAt(s, map);
        air = !onGroundF(s) && !onLadder;
        takeoffKind = air ? "start" : "";
        takeoffMs = 0; takeoffPos = s.pos; apexY = s.pos.y;
        lastWallMs = -1000; lastLandMs = -1000;
    }
    static Json ev(const char* kind, int tMs, const MoveState& s) {
        Json e = Json::object();
        e.set("t", tMs / 1000.0);
        e.set("kind", kind);
        e.set("pos", vec(s.pos, 1e-4));
        e.set("hspeed", roundTo(hspeed(s.vel), 1e-4));
        e.set("speed", roundTo(length(s.vel), 1e-4));
        return e;
    }
    void takeoff(const char* kind, int tMs, const MoveState& from) {
        air = true; takeoffKind = kind; takeoffMs = tMs; takeoffPos = from.pos; apexY = from.pos.y;
    }
    void step(const MoveState& b, const MoveState& a, const UserCmd& cmd, const Map& map, int tMs, Json& out) {
        landedThisStep = false;
        const int t = tMs + 1;   // (the state after this ms)
        const bool bG = onGroundF(b), aG = onGroundF(a);
        const bool jumpHeld = (cmd.buttons & BTN_JUMP) != 0, frozen = (b.flags & MF_FROZEN) != 0;
        const bool freshDash = (cmd.buttons & BTN_DASH) && !(b.flags & MF_DASHHELD);
        const bool dashStart = freshDash && moveTuning().dash && dashReady(b) && b.dashTimer <= 0 && !frozen;
        bool tech = false;
        if (dashStart) {
            Json e = ev("dash", t, a);
            e.set("charges", (int)a.dashCharges);
            e.set("dir", vec(Vec3(a.dashDirX, a.dashUp, a.dashDirZ), 1e-4));
            MoveState q = b;
            q.dashUp = a.dashUp;
            e.set("aimedDown", dashAimedDown(q, &map));
            out.push(std::move(e));
        }
        const bool wasDashing = b.dashTimer > 0 || dashStart;
        if (wasDashing && a.dashTimer <= 0) {
            // a dash jump / super dash ends it with time left, and leaves rising; else it ran out
            const bool early = dashStart || b.dashTimer > 1.5f * kMoveStep;
            if (early && a.vel.y > 0.01f) {
                tech = true;
                MoveState q = b;   // (pmove's test: this dash's aim, the dash_vertical of the step it ended in)
                q.dashUp = a.dashUp;
                const char* k = dashAimedDown(q, &map) ? "superDash" : "dashJump";
                Json e = ev(k, t, a);
                e.set("vy", roundTo(a.vel.y, 1e-4));
                e.set("charges", (int)a.dashCharges);
                out.push(std::move(e));
                takeoff(k, t, a);
            } else {
                Json e = ev("dashEnd", t, a);
                out.push(std::move(e));
            }
        }
        if (a.dashBounces > b.dashBounces && !dashStart) {
            Json e = ev("bounce", t, a);
            e.set("bounces", (int)a.dashBounces);
            e.set("charges", (int)a.dashCharges);
            out.push(std::move(e));
        }
        if (!(b.flags & MF_HOOKED) && (a.flags & MF_HOOKED)) {
            Json e = ev("hook", t, a);
            e.set("point", vec(a.hookPoint, 1e-4));
            e.set("dist", roundTo(length(a.hookPoint - (a.pos + Vec3(0, kEyeHeight, 0))), 1e-3));
            out.push(std::move(e));
            if (bG && !aG) takeoff("hook", t, a);
        } else if ((b.flags & MF_HOOKED) && !(a.flags & MF_HOOKED)) {
            out.push(ev("unhook", t, a));
        }
        const bool lad = ladderAt(a, map);
        if (lad != onLadder) {
            onLadder = lad;
            out.push(ev(lad ? "ladder" : "ladderOff", t, a));
            if (lad) air = false;
            else if (!aG) takeoff("ladder", t, a);
        }
        // a ladder kick-off (a fresh jump on it), a jump off the floor, a bhop off a hard landing (it bounces in the
        // same ms it lands)
        if (!tech && b.dashTimer <= 0 && a.dashTimer <= 0 && !(a.flags & MF_HOOKED)) {
            if (bG && !aG && a.vel.y > 0.01f && jumpHeld) {
                Json e = ev("jump", t, a);
                e.set("vy", roundTo(a.vel.y, 1e-4));
                e.set("bhop", t - lastLandMs <= 2);
                out.push(std::move(e));
                takeoff("jump", t, b);
            } else if (!bG && !aG && jumpHeld && b.vel.y < -0.01f && a.vel.y > 0.01f && !onLadder && !lad) {
                land(b, a, t, out);
                Json e = ev("jump", t, a);
                e.set("vy", roundTo(a.vel.y, 1e-4));
                e.set("bhop", true);
                e.set("bounce", true);
                out.push(std::move(e));
                takeoff("jump", t, a);
            }
        }
        if (bG && !aG && !air) takeoff(a.vel.y > 0.01f ? "launch" : "fall", t, b);
        if (!bG && aG) land(b, a, t, out);
        if (air) apexY = std::max(apexY, a.pos.y);
        // steps up (the step-up carried you over a ledge this ms)
        const float rise = a.pos.y - b.pos.y - std::max(0.0f, b.vel.y) * kMoveStep;
        if (rise > 0.05f && !(a.flags & MF_HOOKED) && !lad) {
            Json e = ev("step", t, a);
            e.set("rise", roundTo(rise, 1e-4));
            out.push(std::move(e));
        }
        // a wall: a horizontal speed stopped against something solid (not climbing a ladder; once per contact)
        if (!lad && !onLadder) {
            bool hit = false;
            for (int ax : {0, 2}) {
                const float bv = ax == 0 ? b.vel.x : b.vel.z, av = ax == 0 ? a.vel.x : a.vel.z;
                if (std::fabs(bv) > 0.5f && av == 0.0f) {
                    const float d = bv > 0 ? 0.01f : -0.01f;
                    if (std::fabs(map.sweepAxis(playerBox(a), ax, d)) < 0.009f) hit = true;
                }
            }
            if (hit) {
                if (t - lastWallMs > 100) { Json e = ev("wall", t, a); e.set("before", roundTo(hspeed(b.vel), 1e-4)); out.push(std::move(e)); }
                lastWallMs = t;
            }
        }
        if (bG && aG && hspeed(b.vel) > 0.05f && hspeed(a.vel) == 0.0f) out.push(ev("stop", t, a));
    }
    void land(const MoveState& b, const MoveState& a, int t, Json& out) {
        Json e = ev("land", t, a);
        e.set("impact", roundTo(-b.vel.y, 1e-4));
        if (air && !takeoffKind.empty()) {
            e.set("from", takeoffKind);
            e.set("airTime", (t - takeoffMs) / 1000.0);
            e.set("height", roundTo(apexY - takeoffPos.y, 1e-4));
            e.set("drop", roundTo(apexY - a.pos.y, 1e-4));
            e.set("distance", roundTo(hspeed(a.pos - takeoffPos), 1e-4));
        }
        out.push(std::move(e));
        air = false;
        takeoffKind.clear();
        lastLandMs = t;
        landedThisStep = true;
    }
};
} // namespace

// ---- scenarios ------------------------------------------------------------------------------------------------------
namespace {
bool readInt(const Json& j, const char* k, int& v, int lo, int hi, std::string& err) {
    if (j[k].isNull()) return true;
    if (!j[k].isNum()) { err = std::string("step.") + k + " must be a number"; return false; }
    v = std::clamp(j[k].i(), lo, hi);
    return true;
}
}

bool scenarioFromJson(const Json& j, Scenario& sc, std::string& err) {
    if (!j.isObj()) { err = "scenario must be an object (or a preset id)"; return false; }
    sc = Scenario();
    sc.name = j["name"].str("scenario");
    const Json& st = j["start"];
    sc.pos = toVec(st["pos"], Vec3());
    sc.vel = toVec(st["vel"], Vec3());
    sc.yaw = st["yaw"].f(-90);
    sc.pitch = st["pitch"].f(0);
    if (st["onGround"].isBool()) sc.onGround = st["onGround"].boolean() ? 1 : 0;
    if (st["dashCharges"].isNum()) sc.dashCharges = std::clamp(st["dashCharges"].i(), 0, kMaxDashCharges);
    for (float f : {sc.pos.x, sc.pos.y, sc.pos.z, sc.vel.x, sc.vel.y, sc.vel.z, sc.yaw, sc.pitch})
        if (!std::isfinite(f)) { err = "scenario.start has a value that isn't a number"; return false; }
    sc.durationMs = std::clamp(j["durationMs"].i(2000), 1, 120000);
    if (!j["steps"].isNull() && !j["steps"].isArr()) { err = "scenario.steps must be an array"; return false; }
    if (j["steps"].size() > 20000) { err = "at most 20000 steps"; return false; }
    for (auto& sj : j["steps"].items()) {
        if (!sj.isObj()) { err = "each step must be an object"; return false; }
        Step s;
        if (!readInt(sj, "atMs", s.atMs, 0, 120000, err) || !readInt(sj, "forward", s.forward, -127, 127, err) ||
            !readInt(sj, "side", s.side, -127, 127, err)) return false;
        if (sj["yaw"].isNum()) { s.hasYaw = true; s.yaw = sj["yaw"].f(); }
        if (sj["pitch"].isNum()) { s.hasPitch = true; s.pitch = sj["pitch"].f(); }
        s.yawRate = sj["yawRate"].f(0);
        s.pitchRate = sj["pitchRate"].f(0);
        s.jump = sj["jump"].boolean(); s.crouch = sj["crouch"].boolean(); s.dash = sj["dash"].boolean(); s.hook = sj["hook"].boolean();
        const Json& sb = sj["strafeBot"];
        s.strafeBot = sb.isStr() ? (sb.str() == "search" ? 2 : sb.str() == "off" ? 0 : 1) : sb.boolean() ? 1 : 0;
        const Json& sw = sj["strafeSwitch"];
        s.strafeSwitch = sw.isStr() ? (sw.str() == "land" ? 2 : sw.str() == "off" ? 0 : 1) : sw.boolean() ? 1 : 0;
        s.strafeSwing = std::clamp(sj["strafeSwing"].f(25), 1.0f, 180.0f);
        s.jumpAtEdge = sj["jumpAtEdge"].boolean();
        for (float f : {s.yaw, s.pitch, s.yawRate, s.pitchRate})
            if (!std::isfinite(f)) { err = "a step has a value that isn't a number"; return false; }
        sc.steps.push_back(s);
    }
    std::stable_sort(sc.steps.begin(), sc.steps.end(), [](const Step& a, const Step& b) { return a.atMs < b.atMs; });
    return true;
}

Json scenarioToJson(const Scenario& sc) {
    Json j = Json::object();
    j.set("name", sc.name);
    Json st = Json::object();
    st.set("pos", jvec(sc.pos));
    if (sc.vel.x != 0 || sc.vel.y != 0 || sc.vel.z != 0) st.set("vel", jvec(sc.vel));
    st.set("yaw", sc.yaw);
    st.set("pitch", sc.pitch);
    if (sc.onGround >= 0) st.set("onGround", sc.onGround == 1);
    if (sc.dashCharges >= 0) st.set("dashCharges", sc.dashCharges);
    j.set("start", std::move(st));
    j.set("durationMs", sc.durationMs);
    Json steps = Json::array();
    for (auto& s : sc.steps) {
        Json o = Json::object();
        o.set("atMs", s.atMs);
        o.set("forward", s.forward);
        o.set("side", s.side);
        if (s.hasYaw) o.set("yaw", s.yaw);
        if (s.hasPitch) o.set("pitch", s.pitch);
        if (s.yawRate != 0) o.set("yawRate", s.yawRate);
        if (s.pitchRate != 0) o.set("pitchRate", s.pitchRate);
        if (s.jump) o.set("jump", true);
        if (s.crouch) o.set("crouch", true);
        if (s.dash) o.set("dash", true);
        if (s.hook) o.set("hook", true);
        if (s.strafeBot) o.set("strafeBot", s.strafeBot == 2 ? Json("search") : Json(true));
        if (s.strafeSwitch) o.set("strafeSwitch", s.strafeSwitch == 2 ? Json("land") : Json(true));
        if (s.strafeSwitch == 1 && s.strafeSwing != 25) o.set("strafeSwing", s.strafeSwing);
        if (s.jumpAtEdge) o.set("jumpAtEdge", true);
        steps.push(std::move(o));
    }
    j.set("steps", std::move(steps));
    return j;
}

namespace {
MoveState startState(const Vec3& pos, const Vec3& vel, int onGround, int dashCharges, const Map& map) {
    MoveState s;
    s.pos = pos;
    s.vel = vel;
    bool g = onGround == 1;
    if (onGround < 0) {
        const float probe = map.sweepAxis(playerBox(pos, false), 1, -0.05f);
        g = probe > -0.049f && vel.y <= 0.01f;
        if (g) s.pos.y += probe;
    }
    if (g) s.flags |= MF_ONGROUND;
    const int most = dashMaxCharges(s, &map);   // (for this start: a dash_charges equation sees it, with the usual 2 held)
    s.dashCharges = (uint8_t)(dashCharges >= 0 ? std::min(dashCharges, most) : most);
    return s;
}

Json sampleJson(int ms, const MoveState& s, float yawDeg) {
    Json o = Json::object();
    o.set("t", ms / 1000.0);
    o.set("pos", vec(s.pos, 1e-4));
    o.set("vel", vec(s.vel, 1e-3));
    o.set("speed", roundTo(length(s.vel), 1e-3));
    o.set("hspeed", roundTo(hspeed(s.vel), 1e-3));
    o.set("flags", (int)s.flags);
    o.set("dashCharges", (int)s.dashCharges);
    o.set("dashTimer", roundTo(s.dashTimer, 1e-4));
    o.set("onGround", onGroundF(s));
    o.set("yaw", roundTo(yawDeg, 1e-2));
    return o;
}
}

RunResult runScenario(const Scenario& sc, const Map& map, bool isRoom, int sampleMs) {
    RunResult rr;
    rr.events = Json::array();
    rr.trace = Json::array();
    const int dur = sc.durationMs;
    MoveState s = startState(sc.pos, sc.vel, sc.onGround, sc.dashCharges, map);
    const Vec3 start = s.pos;
    Watch w;
    w.reset(s, map);
    std::vector<Vec3> pos(dur + 1), vel(dur + 1);
    std::vector<uint8_t> flags(dur + 1);
    std::vector<float> yaws(dur + 1);
    pos[0] = s.pos; vel[0] = s.vel; flags[0] = s.flags;
    float yaw = sc.yaw, pitch = sc.pitch;
    yaws[0] = yaw;
    size_t si = 0;
    const Step idle;
    const Step* cur = &idle;
    float sideFlip = 1, course = yaw * kRad;
    sampleMs = std::max({1, sampleMs, (dur + 19999) / 20000});   // (at most ~20000 samples a run, plus the events)
    rr.trace.push(sampleJson(0, s, yaw));
    for (int t = 0; t < dur; t++) {
        while (si < sc.steps.size() && sc.steps[si].atMs <= t) {
            cur = &sc.steps[si++];
            if (cur->hasYaw) yaw = cur->yaw;
            if (cur->hasPitch) pitch = cur->pitch;
            course = yaw * kRad;   // (the strafe bot weaves around the heading the step starts facing)
        }
        yaw += cur->yawRate * kMoveStep;
        pitch = std::clamp(pitch + cur->pitchRate * kMoveStep, -89.0f, 89.0f);
        UserCmd cmd;
        cmd.seq = (uint32_t)t;
        cmd.msec = 1;
        cmd.forward = (int8_t)cur->forward;
        cmd.side = (int8_t)std::clamp((int)std::lround(cur->side * sideFlip), -127, 127);
        if (cur->jump) cmd.buttons |= BTN_JUMP;
        if (cur->crouch) cmd.buttons |= BTN_CROUCH;
        if (cur->dash) cmd.buttons |= BTN_DASH;
        if (cur->hook) cmd.buttons |= BTN_GRAPPLE;
        if (cur->jumpAtEdge && onGroundF(s) && hspeed(s.vel) > 0.5f && !supported(map, s.pos + Vec3(s.vel.x, 0, s.vel.z) * (2 * kMoveStep)))
            cmd.buttons |= BTN_JUMP;   // (the last ms the floor holds you: next ms it wouldn't)
        if (cur->strafeBot) yaw = strafeYaw(s, cmd.forward, cmd.side, yaw, cur->strafeBot, map, cmd.buttons, pitch);
        cmd.yaw = yaw * kRad;
        cmd.pitch = pitch * kRad;
        const MoveState before = s;
        const size_t ne = rr.events.size();
        pmove(s, cmd, map);
        w.step(before, s, cmd, map, t, rr.events);
        if (cur->strafeSwitch == 2 && w.landedThisStep) sideFlip = -sideFlip;
        if (cur->strafeSwitch == 1 && hspeed(s.vel) > 1.0f) {
            // weave: swap A / D once the velocity has swung strafeSwing degrees past the course, toward the turn
            float dev = std::remainder(headingOf(s.vel.x, s.vel.z) - course, 2 * PI);
            const float side = cur->side * sideFlip, lim = cur->strafeSwing * kRad;
            if ((side > 0 && dev < -lim) || (side < 0 && dev > lim)) sideFlip = -sideFlip;
        }
        pos[t + 1] = s.pos; vel[t + 1] = s.vel; flags[t + 1] = s.flags; yaws[t + 1] = yaw;
        if ((t + 1) % sampleMs == 0 || rr.events.size() != ne || t + 1 == dur) {
            rr.trace.push(sampleJson(t + 1, s, yaw));
        }
    }

    // ---- metrics ----
    Json m = Json::object();
    float topSpeed = 0, maxSpeed = 0, maxY = start.y, pathLen = 0;
    for (int k = 0; k <= dur; k++) {
        topSpeed = std::max(topSpeed, hspeed(vel[k]));
        maxSpeed = std::max(maxSpeed, length(vel[k]));
        maxY = std::max(maxY, pos[k].y);
        if (k) pathLen += length(pos[k] - pos[k - 1]);
    }
    m.set("duration", dur / 1000.0);
    m.set("topSpeed", roundTo(topSpeed, 1e-4));
    m.set("maxSpeed", roundTo(maxSpeed, 1e-4));
    if (topSpeed > 0.5f) {
        for (int k = 0; k <= dur; k++) if (hspeed(vel[k]) >= 0.99f * topSpeed) { m.set("timeToTopSpeed", k / 1000.0); break; }
        for (int k = 0; k <= dur; k++) if (hspeed(vel[k]) >= 0.9f * topSpeed) { m.set("timeTo90", k / 1000.0); break; }
    }
    m.set("speedAfter", roundTo(hspeed(vel[dur]), 1e-4));
    m.set("maxHeight", roundTo(maxY - start.y, 1e-4));
    m.set("distance", roundTo(hspeed(pos[dur] - start), 1e-4));
    m.set("pathLength", roundTo(pathLen, 1e-4));
    m.set("endPos", vec(pos[dur], 1e-4));
    // stopping: from the first step that lets go of every key after one that held some
    for (size_t i = 1; i < sc.steps.size(); i++) {
        const Step& a = sc.steps[i - 1];
        const Step& b = sc.steps[i];
        if ((a.forward || a.side) && !b.forward && !b.side && !b.jump && !b.dash && !b.hook && b.atMs < dur) {
            const int r = b.atMs;
            m.set("speedAtRelease", roundTo(hspeed(vel[r]), 1e-4));
            for (int k = r; k <= dur; k++)
                if (hspeed(vel[k]) < 0.01f && (flags[k] & MF_ONGROUND)) {
                    m.set("stopTime", (k - r) / 1000.0);
                    m.set("stopDistance", roundTo(hspeed(pos[k] - pos[r]), 1e-4));
                    break;
                }
            break;
        }
    }
    // from the events
    Json hops = Json::array();
    int jumps = 0, landings = 0, dashes = 0;
    bool firstLand = false, firstDash = false, firstHook = false, firstLadder = false;
    double dashStartT = 0, hookT = 0, ladderT = 0;
    Vec3 dashStartP;
    float hookTop = 0, ladderY = 0;
    auto sec = [](double v) { return std::round(v * 1000) / 1000; };   // (whole ms)
    float maxImpact = 0, maxDrop = 0;
    const Json& evs = rr.events;
    for (size_t i = 0; i < evs.size(); i++) {
        const Json& e = evs[i];
        const std::string& k = e["kind"].str();
        const double t = e["t"].num();
        if (k == "jump") jumps++;
        if (k == "land") {
            landings++;
            hops.push(e["hspeed"]);
            maxImpact = std::max(maxImpact, e["impact"].f());
            maxDrop = std::max(maxDrop, e["drop"].f());
            if (!firstLand && e.has("airTime")) {
                firstLand = true;
                m.set("airTime", e["airTime"]);
                m.set("jumpHeight", e["height"]);
                m.set("jumpDistance", e["distance"]);
                m.set("takeoff", e["from"]);
            }
        }
        if (k == "dash") { dashes++; if (!firstDash) { dashStartT = t; dashStartP = toVec(e["pos"]); } }
        if ((k == "dashEnd" || k == "dashJump" || k == "superDash") && !firstDash && dashes > 0) {
            firstDash = true;
            m.set("dashDistance", roundTo(hspeed(toVec(e["pos"]) - dashStartP), 1e-4));
            m.set("dashDuration", sec(t - dashStartT));
            m.set("dashExitSpeed", e["hspeed"]);
        }
        if (k == "dashJump" && !m.has("dashJumpSpeed")) m.set("dashJumpSpeed", e["hspeed"]);
        if (k == "superDash" && !m.has("superDashSpeed")) m.set("superDashSpeed", e["hspeed"]);
        if (k == "bounce" && !m.has("bounceSpeed")) m.set("bounceSpeed", e["hspeed"]);
        if (k == "hook" && !firstHook) { firstHook = true; hookT = t; m.set("hookDistance", e["dist"]); }
        if (k == "unhook" && firstHook && !m.has("hookTime")) { m.set("hookTime", sec(t - hookT)); m.set("speedAtUnhook", e["speed"]); }
        if (k == "ladder" && !firstLadder) { firstLadder = true; ladderT = t; ladderY = toVec(e["pos"]).y; }
        if (k == "ladderOff" && firstLadder && !m.has("ladderTime")) {
            const float dy = toVec(e["pos"]).y - ladderY;
            m.set("ladderTime", sec(t - ladderT));
            m.set("climbHeight", roundTo(dy, 1e-4));
            if (t > ladderT) m.set("climbRate", roundTo((float)(dy / (t - ladderT)), 1e-4));
        }
    }
    if (!firstLand && w.air && !w.takeoffKind.empty() && w.takeoffKind != "start") {   // (still in the air at the end)
        m.set("jumpHeight", roundTo(w.apexY - w.takeoffPos.y, 1e-4));
        m.set("takeoff", w.takeoffKind);
        m.set("landed", false);
    }
    if (firstHook) {
        for (int k = (int)std::lround(hookT * 1000); k <= dur && (flags[k] & MF_HOOKED); k++) hookTop = std::max(hookTop, length(vel[k]));
        m.set("hookTopSpeed", roundTo(hookTop, 1e-4));
    }
    if (!firstDash && dashes > 0) m.set("dashDistance", roundTo(hspeed(pos[dur] - dashStartP), 1e-4));
    m.set("jumps", jumps);
    m.set("landings", landings);
    m.set("dashes", dashes);
    m.set("hopSpeeds", std::move(hops));
    if (landings) { m.set("landImpact", roundTo(maxImpact, 1e-4)); m.set("fallHeight", roundTo(maxDrop, 1e-4)); }
    if (isRoom) {
        const RoomPart* part = roomPartAt(start);
        if (part) m.set("part", part->id);
        if (part && std::string(part->id) == "gaps") {
            int cleared = 0; float widest = 0;
            Json list = Json::array();
            for (auto& g : roomGaps()) {
                bool ok = false;
                for (int k = 0; k <= dur && !ok; k++) ok = (flags[k] & MF_ONGROUND) && pos[k].y > -0.5f && pos[k].x - kPlayerHalfWidth >= g.x1 - 1e-3f;
                Json o = Json::object(); o.set("width", g.x1 - g.x0); o.set("cleared", ok); list.push(std::move(o));
                if (!ok) break;
                cleared++; widest = g.x1 - g.x0;
            }
            m.set("gapsCleared", cleared);
            m.set("widestGap", widest);
            m.set("gaps", std::move(list));
        }
    }
    rr.metrics = std::move(m);
    return rr;
}

// ---- presets ------------------------------------------------------------------------------------------------------
namespace {
struct S {   // a step, built with chained setters
    Step s;
    explicit S(int at) { s.atMs = at; }
    S& fw(int v = 127) { s.forward = v; return *this; }
    S& sd(int v) { s.side = v; return *this; }
    S& jump() { s.jump = true; return *this; }
    S& dash() { s.dash = true; return *this; }
    S& hook() { s.hook = true; return *this; }
    S& pitch(float p) { s.hasPitch = true; s.pitch = p; return *this; }
    S& strafe(int sw) { s.strafeBot = 1; s.strafeSwitch = sw; return *this; }
    S& edge() { s.jumpAtEdge = true; return *this; }
};
struct Preset { const char* id; const char* label; const char* desc; const char* part; Scenario sc; std::vector<const char*> keys; };

std::vector<Preset> makePresets() {
    auto P = [](const char* id, const char* label, const char* desc, const char* part, int dur, std::vector<S> steps,
                std::vector<const char*> keys, float pitch = 0) {
        Preset p{id, label, desc, part, Scenario(), keys};
        for (auto& r : roomParts()) if (!strcmp(r.id, part)) { p.sc.pos = r.start; p.sc.yaw = r.yawDeg; }
        p.sc.name = label;
        p.sc.pitch = pitch;
        p.sc.durationMs = dur;
        for (auto& s : steps) p.sc.steps.push_back(s.s);
        return p;
    };
    return {
        P("standing_jump", "Standing jump", "jump once from standing still", "flat", 2500, {S(0).jump(), S(30)},
          {"jumpHeight", "airTime"}),
        P("running_jump", "Running jump", "run for 0.8 s, jump once, keep running forward", "flat", 2000,
          {S(0).fw(), S(800).fw().jump(), S(830).fw()}, {"jumpDistance", "jumpHeight", "airTime", "topSpeed"}),
        P("run_up", "Run-up to top speed", "hold forward from standing still", "flat", 1000, {S(0).fw()},
          {"timeToTopSpeed", "timeTo90", "topSpeed"}),
        P("stop", "Stop from top speed", "run for 1 s, then let go of every key", "flat", 2500, {S(0).fw(), S(1000)},
          {"stopDistance", "stopTime", "speedAtRelease"}),
        P("bhop_plain", "Bhop chain (forward only)", "run, then hold jump and forward: bunny hops with W-only air control", "flat", 6000,
          {S(0).fw(), S(500).fw().jump()}, {"hopSpeeds", "speedAfter", "jumps"}),
        P("bhop_strafe", "Bhop chain x10 (strafe bot)", "run 0.5 s, then hold jump and strafe: the strafe bot steers each ms for the best gain, weaving A / D 25 degrees either side of the course",
          "flat", 7000, {S(0).fw(), S(500).sd(127).jump().strafe(1)}, {"hopSpeeds", "topSpeed", "speedAfter", "jumps"}),
        P("dash", "Dash", "dash once from standing still, looking level", "flat", 1000, {S(100).dash(), S(120)},
          {"dashDistance", "dashExitSpeed", "dashDuration"}),
        P("dash_jump", "Dash jump", "run, dash, and jump during the dash on the floor", "flat", 2000,
          {S(0).fw(), S(600).fw().dash(), S(620).fw(), S(640).fw().jump(), S(670).fw()},
          {"dashJumpSpeed", "jumpDistance", "jumpHeight", "speedAfter"}),
        P("super_dash", "Super dash", "run looking 45 degrees down, dash into the floor and jump during it", "flat", 2000,
          {S(0).fw(), S(600).fw().dash(), S(610).fw().dash().jump(), S(640).fw()},
          {"superDashSpeed", "jumpHeight", "jumpDistance", "speedAfter"}, -45),
        P("dash_wall", "Dash into a wall", "dash straight at the wall 1.6 m ahead: it bounces back", "wall", 1200,
          {S(50).dash(), S(70)}, {"bounceSpeed", "dashDistance", "dashExitSpeed"}),
        P("grapple_swing", "Grapple swing", "run, jump, hook the ceiling 40 degrees up ahead, hold for 0.45 s, let go and fly", "grapple", 3500,
          {S(0).fw(), S(300).fw().jump(), S(330).fw(), S(350).fw().hook(), S(800)},
          {"hookTime", "hookTopSpeed", "speedAtUnhook", "maxHeight", "distance"}, 40),
        P("ladder_climb", "Ladder climb", "walk into the ladder and hold forward to the top of the 8 m tower", "ladder", 2500,
          {S(0).fw()}, {"ladderTime", "climbHeight", "climbRate", "maxHeight"}),
        P("stairs", "Step-up stairs", "walk up 0.5 m risers to 3 m, then at the 0.75 m risers (over the 0.55 m step-up)", "stairs", 3000,
          {S(0).fw()}, {"maxHeight", "distance"}),
        P("max_gap", "Max gap", "run along the gap course jumping at each edge: gaps 2..16 m", "gaps", 15000,
          {S(0).fw().edge()}, {"gapsCleared", "widestGap", "jumpDistance"}),
        P("fall", "Fall", "walk off the 20 m ledge", "drop", 3500, {S(0).fw(), S(800)},
          {"fallHeight", "landImpact", "airTime"}),
    };
}
const std::vector<Preset>& presets() { static const std::vector<Preset> p = makePresets(); return p; }
const Preset* presetById(const std::string& id) { for (auto& p : presets()) if (id == p.id) return &p; return nullptr; }

Json presetJson(const Preset& p) {
    Json o = Json::object();
    o.set("id", p.id); o.set("label", p.label); o.set("desc", p.desc);
    o.set("map", std::string("room:") + p.part);
    o.set("scenario", scenarioToJson(p.sc));
    Json k = Json::array(); for (auto* s : p.keys) k.push(s); o.set("metrics", std::move(k));
    return o;
}

// ---- physsim -------------------------------------------------------------------------------------------------------
int runSim(const Json& req, Json& out, CmdCtx&) {
    const double t0 = maptoolNow();
    std::vector<CustomRules> sets(1);
    std::vector<std::vector<std::string>> clamped(1);
    std::string err;
    if (!requestPhysics(req, sets[0], clamped[0], err)) return cmdError(out, err);
    for (auto& c : req["compare"].items()) {
        sets.emplace_back(); clamped.emplace_back();
        if (!physicsFromJson(c, sets.back(), clamped.back(), err)) return cmdError(out, "compare: " + err);
        if (c.isStr() && c.str() == "standard") sets.back().name = "standard";
    }
    if (sets.size() > 16) return cmdError(out, "at most 16 physics sets in one physsim");
    // the scenario: an object, or a preset's id ("scenario": "dash" or "preset": "dash"), whose map is the default
    Scenario sc;
    Json mapReq = req["map"];
    const Json& sj = req["scenario"];
    std::string presetId = sj.isStr() ? sj.str() : req["preset"].str();
    if (!presetId.empty()) {
        const Preset* p = presetById(presetId);
        if (!p) return cmdError(out, "no preset '" + presetId + "' (physpresets lists them)");
        sc = p->sc;
        if (mapReq.isNull()) mapReq = Json(std::string("room:") + p->part);
    } else if (!scenarioFromJson(sj, sc, err)) return cmdError(out, err);
    const Map* map = nullptr;
    Map holder;
    bool isRoom = false;
    std::string label;
    if (!requestMap(mapReq, map, holder, isRoom, label, err)) return cmdError(out, err);
    const int sampleMs = std::clamp(req["sampleMs"].i(10), 1, 10000);
    std::vector<RunResult> res(sets.size());
    auto one = [&](size_t i) { setActiveRules(sets[i]); res[i] = runScenario(sc, *map, isRoom, sampleMs); };
    const double tSim = maptoolNow();
    if (sets.size() == 1 || req["threads"].boolean(true) == false) for (size_t i = 0; i < sets.size(); i++) one(i);
    else {
        std::vector<std::thread> th;
        for (size_t i = 1; i < sets.size(); i++) th.emplace_back(one, i);
        one(0);
        for (auto& t : th) t.join();
    }
    const double simMs = (maptoolNow() - tSim) * 1000;
    out = Json::object();
    out.set("scenario", scenarioToJson(sc));
    out.set("map", label);
    Json runs = Json::array();
    for (size_t i = 0; i < sets.size(); i++) {
        Json r = Json::object();
        r.set("label", i > 0 && sets[i].name == "physics" ? "compare " + std::to_string(i) : sets[i].name);
        r.set("physics", physicsToJson(sets[i]));
        if (!clamped[i].empty()) { Json c = Json::array(); for (auto& k : clamped[i]) c.push(k); r.set("clamped", std::move(c)); }
        r.set("metrics", std::move(res[i].metrics));
        r.set("events", std::move(res[i].events));
        r.set("trace", std::move(res[i].trace));
        runs.push(std::move(r));
    }
    out.set("runs", std::move(runs));
    out.set("simMs", simMs);
    out.set("ms", (maptoolNow() - t0) * 1000);
    return 0;
}

int runPresets(const Json& req, Json& out, CmdCtx&) {
    const double t0 = maptoolNow();
    out = Json::object();
    Json list = Json::array();
    const bool run = req["run"].boolean(false);
    CustomRules rules;
    std::vector<std::string> clamped;
    std::string err;
    if (run && !requestPhysics(req, rules, clamped, err)) return cmdError(out, err);
    if (run) setActiveRules(rules);
    for (auto& p : presets()) {
        Json o = presetJson(p);
        if (run) {
            RunResult r = runScenario(p.sc, roomMap(), true, 1000000);
            Json pick = Json::object();
            for (auto* k : p.keys) pick.set(k, r.metrics[k]);
            o.set("result", std::move(pick));
        }
        list.push(std::move(o));
    }
    out.set("presets", std::move(list));
    Json parts = Json::array();
    for (auto& r : roomParts()) parts.push(std::string("room:") + r.id);
    out.set("maps", std::move(parts));
    out.set("ms", (maptoolNow() - t0) * 1000);
    return 0;
}

// ---- physdrive --------------------------------------------------------------------------------------------------------
struct Drive {
    CustomRules rules;
    const Map* map = nullptr;
    Map holder;
    bool isRoom = true;
    std::string mapLabel;
    Vec3 startPos, startVel;
    int startGround = -1;
    float yaw = -90, pitch = 0;   // (a command without them keeps the last ones; a start sets them)
    MoveState s;
    Watch w;
    long long ms = 0;
    bool samples = false;
    void reset() { s = startState(startPos, startVel, startGround, -1, *map); w.reset(s, *map); ms = 0; }
    bool setStart(const Json& st, std::string& err) {
        if (st.isNull()) return true;
        if (!st.isObj()) { err = "start must be {pos, vel?, onGround?}"; return false; }
        startPos = toVec(st["pos"], startPos);
        startVel = toVec(st["vel"], Vec3());
        startGround = st["onGround"].isBool() ? (st["onGround"].boolean() ? 1 : 0) : -1;
        if (st["yaw"].isNum() && std::isfinite(st["yaw"].f())) yaw = st["yaw"].f();
        if (st["pitch"].isNum() && std::isfinite(st["pitch"].f())) pitch = std::clamp(st["pitch"].f(), -89.0f, 89.0f);
        return true;
    }
    Json state(Json events, double us) const {
        Json o = Json::object();
        o.set("type", "state");
        o.set("t", ms / 1000.0);
        o.set("pos", vec(s.pos, 1e-4));
        o.set("vel", vec(s.vel, 1e-3));
        o.set("speed", roundTo(length(s.vel), 1e-3));
        o.set("hspeed", roundTo(hspeed(s.vel), 1e-3));
        o.set("flags", (int)s.flags);
        o.set("onGround", onGroundF(s));
        o.set("dashCharges", (int)s.dashCharges);
        o.set("dashTimer", roundTo(s.dashTimer, 1e-4));
        o.set("dashCooldownMs", (int)s.dashCooldownMs);
        o.set("hookCooldown", roundTo(s.hookCooldown, 1e-3));
        if (s.flags & MF_HOOKED) o.set("hookPoint", vec(s.hookPoint, 1e-4));
        o.set("onLadder", w.onLadder);
        o.set("events", std::move(events));
        o.set("us", roundTo((float)us, 0.1));
        return o;
    }
};

uint16_t buttonsOf(const Json& c) {
    const Json& b = c["buttons"];
    uint16_t v = 0;
    if (b.isNum()) v = (uint16_t)b.i();
    else if (b.isArr()) for (auto& n : b.items()) {
        const std::string& s = n.str();
        v |= s == "jump" ? BTN_JUMP : s == "dash" ? BTN_DASH : s == "hook" || s == "grapple" ? BTN_GRAPPLE : s == "crouch" ? BTN_CROUCH : 0;
    } else if (b.isObj()) {
        if (b["jump"].boolean()) v |= BTN_JUMP;
        if (b["dash"].boolean()) v |= BTN_DASH;
        if (b["hook"].boolean() || b["grapple"].boolean()) v |= BTN_GRAPPLE;
        if (b["crouch"].boolean()) v |= BTN_CROUCH;
    }
    if (c["jump"].boolean()) v |= BTN_JUMP;
    if (c["dash"].boolean()) v |= BTN_DASH;
    if (c["hook"].boolean()) v |= BTN_GRAPPLE;
    return v & (BTN_JUMP | BTN_CROUCH | BTN_DASH | BTN_GRAPPLE);
}

int runDrive(const Json& req, Json& out, CmdCtx& ctx) {
    Drive d;
    std::vector<std::string> clamped;
    std::string err;
    if (!requestPhysics(req, d.rules, clamped, err)) return cmdError(out, err);
    if (!requestMap(req["map"], d.map, d.holder, d.isRoom, d.mapLabel, err)) return cmdError(out, err);
    // the start: given, else the room part's (room:<part>), else the map's first spawn, else the origin
    float yaw = -90;
    if (d.isRoom) {
        std::string part = req["map"].isStr() && req["map"].str().size() > 5 ? req["map"].str().substr(5) : "flat";
        for (auto& r : roomParts()) if (part == r.id) { d.startPos = r.start; yaw = r.yawDeg; }
    } else if (!d.map->spawns.empty()) {
        d.startPos = d.map->spawns[0].pos;
        yaw = d.map->spawns[0].yaw / kRad;
    }
    if (!d.setStart(req["start"], err)) return cmdError(out, err);
    if (req["start"]["yaw"].isNum()) yaw = req["start"]["yaw"].f();
    if (req["start"]["pitch"].isNum()) d.pitch = std::clamp(req["start"]["pitch"].f(), -89.0f, 89.0f);
    d.yaw = yaw;
    d.samples = req["samples"].boolean(false);
    setActiveRules(d.rules);
    d.reset();
    out = Json::object();
    {
        Json st = d.state(Json::array(), 0);
        st.set("type", "start");
        st.set("map", d.mapLabel);
        st.set("yaw", yaw);
        st.set("physics", physicsToJson(d.rules));
        if (!clamped.empty()) { Json c = Json::array(); for (auto& k : clamped) c.push(k); st.set("clamped", std::move(c)); }
        ctx.emit(st);
    }
    if (!ctx.control) return 0;
    std::string line;
    while (std::getline(*ctx.control, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        const double t0 = maptoolNow();
        Json c;
        if (line == "stop" || line == "reset") { c = Json::object(); c.set(line, true); }
        else if (!Json::parse(line, c, &err) || !c.isObj()) {
            Json e = Json::object(); e.set("type", "warning"); e.set("warning", "not a control line: " + line.substr(0, 80)); ctx.emit(e);
            continue;
        }
        if (c["stop"].boolean()) { Json e = Json::object(); e.set("type", "done"); e.set("t", d.ms / 1000.0); ctx.emit(e); return 0; }
        Json events = Json::array();
        Json note = Json::object();
        if (!c["physics"].isNull() || c["qmode"].isStr()) {
            CustomRules r; std::vector<std::string> cl;
            if (!requestPhysics(c, r, cl, err)) { Json e = Json::object(); e.set("type", "warning"); e.set("warning", err); ctx.emit(e); continue; }
            d.rules = r;
            setActiveRules(d.rules);
            note.set("physics", physicsToJson(d.rules));
            if (!cl.empty()) { Json a = Json::array(); for (auto& k : cl) a.push(k); note.set("clamped", std::move(a)); }
        }
        if (!c["map"].isNull()) {
            const Map* m = nullptr; bool room = false; std::string lab;
            Map* fresh = new Map();   // (kept for the process: the old holder may be what d.map points at)
            if (!requestMap(c["map"], m, *fresh, room, lab, err)) { delete fresh; Json e = Json::object(); e.set("type", "warning"); e.set("warning", err); ctx.emit(e); continue; }
            d.map = m; d.isRoom = room; d.mapLabel = lab;
            note.set("map", lab);
            c.set("reset", true);
        }
        if (!c["reset"].isNull() && c["reset"].isObj()) { if (!d.setStart(c["reset"], err)) { Json e = Json::object(); e.set("type", "warning"); e.set("warning", err); ctx.emit(e); continue; } }
        if (c.has("start")) { if (!d.setStart(c["start"], err)) { Json e = Json::object(); e.set("type", "warning"); e.set("warning", err); ctx.emit(e); continue; } c.set("reset", true); }
        if (!c["reset"].isNull() && c["reset"].boolean(true)) { d.reset(); note.set("reset", true); }
        Json path = Json::array();
        for (auto& cj : c["cmd"].items()) {
            UserCmd cmd;
            cmd.forward = (int8_t)std::clamp(cj["forward"].i(0), -127, 127);
            cmd.side = (int8_t)std::clamp(cj["side"].i(0), -127, 127);
            const float y = cj["yaw"].f(d.yaw), pt = cj["pitch"].f(d.pitch);
            if (!std::isfinite(y) || !std::isfinite(pt)) continue;
            d.yaw = y; d.pitch = std::clamp(pt, -89.0f, 89.0f);
            cmd.yaw = d.yaw * kRad;
            cmd.pitch = d.pitch * kRad;
            cmd.buttons = buttonsOf(cj);
            const int n = std::clamp(cj["msec"].i(1), 1, 100);   // (split into 1 ms commands: events land on the exact ms)
            cmd.msec = 1;
            for (int k = 0; k < n; k++) {
                const MoveState before = d.s;
                cmd.seq = (uint32_t)d.ms;
                pmove(d.s, cmd, *d.map);
                d.w.step(before, d.s, cmd, *d.map, (int)(d.ms % 2000000000), events);
                d.ms++;
                if (d.samples) path.push(vec(d.s.pos, 1e-4));
            }
        }
        Json st = d.state(std::move(events), (maptoolNow() - t0) * 1e6);
        for (auto& kv : note.members()) st.set(kv.first, kv.second);
        if (d.samples) st.set("path", std::move(path));
        ctx.emit(st);
    }
    return 0;
}

} // namespace

MAPTOOL_COMMAND("physsim", runSim, "physics editor: {physics|qmode, scenario|preset, map?, compare?, sampleMs?} -> {runs:[{trace, events, metrics}]}");
MAPTOOL_COMMAND("physpresets", runPresets, "physics editor: the built-in scenarios {presets:[{id,label,desc,scenario,map,metrics}]} (run: true adds results)");
MAPTOOL_COMMAND("physdrive", runDrive, "physics editor --stream: {physics|qmode, map?, start?} then {cmd:[...]}, {physics}, reset, stop lines");

} // namespace phys
