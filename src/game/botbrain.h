// Bot brains: the bot's choices and numbers as data (tools/bots/GRAMMAR.md is the file format; tools/bots/default.qbrain
// the built-in brain, which is exactly the native bot in bots.cpp).
//
// BrainParams is every number the bot code reads, one set per skill level. The built-in brain's values are the C++
// table below (QUARK_BRAIN_PARAMS: today's values, typed as the code used them, so the native bot is bit-identical
// to the old literals); a loaded brain only replaces values. The lists, the table and the data curve that the native
// code reads are part of it too.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "../common/qmath.h"

// The bot's dice: xorshift, 0..1 (one draw), and Box-Muller (two draws). The native bot and the interpreter both
// draw through these, so a brain makes the same numbers in the same order either way.
inline float brainRnd(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0xFFFFFF) / 16777216.0f;
}
inline float brainGauss(uint32_t& s) {
    float u = std::max(brainRnd(s), 1e-7f), v = brainRnd(s);
    return std::sqrt(-2.0f * std::log(u)) * std::cos(2 * PI * v);
}

// One weapon preference list (best first) and its membership mask.
struct BrainList {
    uint8_t n = 0;
    uint8_t w[16] = {};
    uint16_t mask = 0;
};
// A data curve: steps (base, then y[k] for the last x[k] < arg) or points (piecewise linear, flat beyond the ends).
struct BrainCurve {
    enum Kind : uint8_t { STEPS = 0, POINTS = 1 };
    uint8_t kind = STEPS, n = 0;
    float base = 0;
    float x[16] = {}, y[16] = {};
};
float evalBrainCurve(const BrainCurve& c, float x);

#define QB_CT_F32 float
#define QB_CT_F64 double
#define QB_CT_INT int
#define QB_CT_BOOL bool

// X(name, type, unit, min, max, doc, BEGINNER, INTERMEDIATE, ADVANCED, EXPERT): every parameter the game reads, in
// default.qbrain's order (the editor's ranges are a guide, not enforced).
#define QUARK_BRAIN_PARAMS(X) \
    X(fov, F32, "rad", 0, 8, "full horizontal view cone", 1.55f, 1.7f, 1.85f, 2.0f) \
    X(viewDist, F32, "m", 0, 600, "sees nobody farther than this", 150.0f, 150.0f, 150.0f, 150.0f) \
    X(vertCone, F32, "rad", 0, 3.8, "vertical half cone", 0.95f, 0.95f, 0.95f, 0.95f) \
    X(feelDist, F32, "m", 0, 4.8, "someone this close is noticed in any direction (bumped into)", 1.2f, 1.2f, 1.2f, 1.2f) \
    X(chestHeight, F32, "m, x body scale", 0, 4.4, "line of sight checked to the chest (above the feet)...", 1.1f, 1.1f, 1.1f, 1.1f) \
    X(headHeight, F32, "m, x body scale", 0, 6.4, "...and the head", 1.6f, 1.6f, 1.6f, 1.6f) \
    X(lookEvery, F64, "s of game time", 0, 0.2, "perception runs this often", 0.05, 0.05, 0.05, 0.05) \
    X(hearSteps, F32, "m", 0, 96, "footsteps heard within", 12.0f, 16.0f, 20.0f, 24.0f) \
    X(hearGun, F32, "m", 0, 300, "gunfire heard within", 45.0f, 55.0f, 65.0f, 75.0f) \
    X(hearGunWindow, F64, "s", 0, 0.8, "a shot can be heard this long after it", 0.2, 0.2, 0.2, 0.2) \
    X(hearStepSpeed, F32, "m/s", 0, 12, "footsteps are audible above this ground speed", 3.0f, 3.0f, 3.0f, 3.0f) \
    X(hearDashMult, F32, "", 0, 6, "a dash is heard this many times farther than footsteps", 1.5f, 1.5f, 1.5f, 1.5f) \
    X(soundErr, F32, "sd", 0, 1.2, "heard position error, fraction of the distance", 0.3f, 0.22f, 0.15f, 0.1f) \
    X(soundNewWindow, F64, "s", 0, 2.4, "a sound after this long is a new one: a fresh guess", 0.6, 0.6, 0.6, 0.6) \
    X(soundRefine, F32, "", 0, 0.6, "a repeated sound moves the guess this far toward the truth", 0.15f, 0.15f, 0.15f, 0.15f) \
    X(memory, F32, "s", 0, 20, "a last-seen position is hunted / aimed at for", 1.5f, 2.5f, 3.5f, 5.0f) \
    X(reactMs, F32, "ms", 0, 1800, "reaction time to a newly seen target", 450.0f, 300.0f, 200.0f, 150.0f) \
    X(reacquire, F32, "", 0, 3, "reaction multiplier for a target lost a moment ago", 0.75f, 0.65f, 0.55f, 0.45f) \
    X(reacquireWindow, F64, "s", 0, 4, "'a moment ago'", 1.0, 1.0, 1.0, 1.0) \
    X(targetAimHeight, F32, "m", 0, 4.4, "target choice measures to this point above the feet", 1.1f, 1.1f, 1.1f, 1.1f) \
    X(targetAngleWeight, F32, "", 0, 48, "target score: metres per rad off the crosshair", 12.0f, 12.0f, 12.0f, 12.0f) \
    X(targetKeepBonus, F32, "m", 0, 24, "target score: the current target counts this much nearer", 6.0f, 6.0f, 6.0f, 6.0f) \
    X(visDelayMs, F32, "ms", 0, 640, "sees the target as it was this long ago", 160.0f, 120.0f, 90.0f, 60.0f) \
    X(predict, F32, "", 0, 3.2, "fraction of that delay extrapolated; hunt extrapolation; feed-forward", 0.0f, 0.3f, 0.6f, 0.8f) \
    X(velTau, F32, "s", 0, 1.6, "perceived velocity catch-up time constant", 0.4f, 0.28f, 0.16f, 0.09f) \
    X(initVelEst, F32, "", 0, 2, "a new target's velocity starts at this fraction of what was seen", 0.5f, 0.5f, 0.5f, 0.5f) \
    X(speedJudge, F32, "", 0, 1.2, "sd of the perceived speed factor", 0.3f, 0.2f, 0.12f, 0.07f) \
    X(huntPredictCap, F64, "s", 0, 2, "a hidden target is extrapolated at most this long", 0.5, 0.5, 0.5, 0.5) \
    X(trackBase, F32, "rad", 0, 0.4, "tracking error sd on a still target", 0.1f, 0.06f, 0.045f, 0.03f) \
    X(trackPerRad, F32, "", 0, 0.6, "extra tracking error sd per rad/s of crossing speed", 0.15f, 0.1f, 0.075f, 0.055f) \
    X(trackPitch, F32, "", 0, 2.4, "pitch share of the tracking error", 0.6f, 0.6f, 0.6f, 0.6f) \
    X(noiseTau, F32, "s", 0, 1, "how slowly the tracking error wanders", 0.25f, 0.2f, 0.15f, 0.12f) \
    X(flickErr, F32, "", 0, 0.88, "flick landing sd, fraction of the flick", 0.22f, 0.15f, 0.1f, 0.06f) \
    X(overshoot, F32, "fraction", 0, 0.48, "mean flick overshoot", 0.12f, 0.09f, 0.06f, 0.03f) \
    X(flickMin, F32, "rad", 0, 0.2, "flicks smaller than this land exactly", 0.05f, 0.05f, 0.05f, 0.05f) \
    X(flickAcross, F32, "", 0, 2, "sideways flick error, share of the along error", 0.5f, 0.5f, 0.5f, 0.5f) \
    X(flickPitch, F32, "", 0, 2.4, "pitch share of the flick error", 0.6f, 0.6f, 0.6f, 0.6f) \
    X(settleTau, F32, "s", 0, 1.2, "flick error correction time constant", 0.3f, 0.22f, 0.15f, 0.09f) \
    X(trackGain, F32, "1/s", 0, 88, "aim correction gain while tracking", 7.0f, 10.0f, 15.0f, 22.0f) \
    X(turnRate, F32, "rad/s", 0, 52, "top hand speed", 4.0f, 6.0f, 9.0f, 13.0f) \
    X(turnAccel, F32, "rad/s^2", 0, 480, "hand acceleration", 25.0f, 40.0f, 70.0f, 120.0f) \
    X(handBrake, F32, "", 0, 3.4, "hand braking curve factor", 0.85f, 0.85f, 0.85f, 0.85f) \
    X(lookGain, F32, "1/s", 0, 40, "aim gain just looking around", 10.0f, 10.0f, 10.0f, 10.0f) \
    X(soundGain, F32, "1/s", 0, 56, "aim gain turning to a sound", 14.0f, 14.0f, 14.0f, 14.0f) \
    X(hookAimGain, F32, "1/s", 0, 100, "aim gain onto a grapple anchor", 25.0f, 25.0f, 25.0f, 25.0f) \
    X(hookAimTol, F32, "rad", 0, 0.32, "the route hook fires when the aim is this close", 0.08f, 0.08f, 0.08f, 0.08f) \
    X(soundLookWindow, F64, "s", 0, 4.8, "looks toward a sound this fresh", 1.2, 1.2, 1.2, 1.2) \
    X(fireCone, F32, "", 0, 0.6, "clicks when the aim error is below this (rad, + body width / distance)", 0.15f, 0.1f, 0.07f, 0.04f) \
    X(leadMean, F32, "", 0, 3.8, "mean fraction of the needed projectile lead", 0.3f, 0.6f, 0.85f, 0.95f) \
    X(leadSd, F32, "drop: x 0.7", 0, 1.4, "sd of the lead", 0.35f, 0.3f, 0.2f, 0.12f) \
    X(canLead, BOOL, "", 0, 1, "leads well: projectile weapon lists, knife throws, rockets at the feet, rifle scope", false, false, true, true) \
    X(rocketFeetHeight, F32, "m", 0, 0.4, "with canLead: rockets aim this high above the target's feet", 0.1f, 0.1f, 0.1f, 0.1f) \
    X(aimHeight, F32, "m, x body scale", 0, 5.2, "aim point above the feet", 1.0f, 1.1f, 1.2f, 1.3f) \
    X(weapons, BOOL, "else the rifle", 0, 1, "picks weapons by range", false, true, true, true) \
    X(weaponClose, F32, "m", 0, 28, "close range ends here", 7.0f, 7.0f, 7.0f, 7.0f) \
    X(weaponMid, F32, "m", 0, 72, "mid range ends here", 18.0f, 18.0f, 18.0f, 18.0f) \
    X(weaponFar, F32, "m", 0, 140, "far range ends here; beyond: very far", 35.0f, 35.0f, 35.0f, 35.0f) \
    X(knifeThrowMax, F32, "m", 0, 72, "throws the knife at targets out of reach but nearer than this", 18.0f, 18.0f, 18.0f, 18.0f) \
    X(rifleScopeDist, F32, "m", 0, 120, "with canLead: scopes the rifle beyond this", 30.0f, 30.0f, 30.0f, 30.0f) \
    X(strafe, F32, "", 0, 4, "share of fight time spent strafing", 0.35f, 0.7f, 1.0f, 1.0f) \
    X(jukeRate, F32, "", 0, 10, "strafe direction changes per second", 1.0f, 1.4f, 2.0f, 2.6f) \
    X(rangeBand, F32, "m", 0, 12, "keeps within this of the weapon's ideal range", 3.0f, 3.0f, 3.0f, 3.0f) \
    X(edgeFlipDot, F32, "", -1, 0, "by a drop: a strafe pointing this much toward it is flipped", -0.2f, -0.2f, -0.2f, -0.2f) \
    X(edgePushNear, F32, "", 0, 6, "by a drop (3 m): steps away from it this hard", 1.5f, 1.5f, 1.5f, 1.5f) \
    X(edgePushFar, F32, "", 0, 2.8, "near a drop (5 m): steps away this hard", 0.7f, 0.7f, 0.7f, 0.7f) \
    X(jumpRate, F32, "", 0, 2.4, "fight jumps per second", 0.0f, 0.1f, 0.35f, 0.6f) \
    X(dashRate, F32, "", 0, 1.4, "fight dashes per second", 0.0f, 0.05f, 0.2f, 0.35f) \
    X(dashCloseDist, F32, "m", 0, 80, "a fight dash closes in beyond this, else dodges sideways", 20.0f, 20.0f, 20.0f, 20.0f) \
    X(pulledDashRate, F32, "", 0, 12, "dashes out of an enemy's grapple, per second", 3.0f, 3.0f, 3.0f, 3.0f) \
    X(pulledDashMinRange, F32, "m", 0, 24, "...when the weapon wants more range than this", 6.0f, 6.0f, 6.0f, 6.0f) \
    X(grapple, BOOL, "routes, fights", 0, 1, "uses the grapple", false, false, true, true) \
    X(fightHookMin, F32, "", 0, 28, "hooks a target farther than this (m)...", 7.0f, 7.0f, 7.0f, 7.0f) \
    X(fightHookChance, F32, "", 0, 2, "...with this chance per try", 0.3f, 0.3f, 0.3f, 0.5f) \
    X(fightHookRetry, F64, "s", 0, 2, "tries again after", 0.5, 0.5, 0.5, 0.5) \
    X(fightHookHold, F64, "s", 0, 1.2, "holds the aim on the target for the hook this long", 0.3, 0.3, 0.3, 0.3) \
    X(bhop, BOOL, "", 0, 1, "bunny-hops on routes", false, false, true, true) \
    X(bhopMinDist, F32, "m", 0, 12, "...toward waypoints farther than this", 3.0f, 3.0f, 3.0f, 3.0f) \
    X(airStrafe, F32, "", 0, 3.6, "air-strafe quality 0..1", 0.0f, 0.0f, 0.6f, 0.9f) \
    X(runDash, F32, "", 0, 1.4, "dashes per second on long straight routes", 0.0f, 0.0f, 0.15f, 0.35f) \
    X(runDashMinDist, F32, "m", 0, 36, "...toward a waypoint farther than this", 9.0f, 9.0f, 9.0f, 9.0f) \
    X(runDashStraight, F32, "cos", 0, 3.8, "...when facing it this squarely", 0.95f, 0.95f, 0.95f, 0.95f) \
    X(dashJump, BOOL, "", 0, 1, "makes a run dash a dash jump", false, false, false, true) \
    X(dashJumpMinDist, F32, "m", 0, 80, "...toward a waypoint farther than this", 20.0f, 20.0f, 20.0f, 20.0f) \
    X(hookRetry, F64, "s", 0, 2, "route hook: looks for an anchor this often", 0.5, 0.5, 0.5, 0.25) \
    X(hookRetryGrappleOnly, F64, "s", 0, 0.8, "...in grapple-only movement", 0.2, 0.2, 0.2, 0.2) \
    X(hookRise, F32, "", 0, 8, "hooks up to ledges higher than this (m)...", 2.0f, 2.0f, 2.0f, 1.2f) \
    X(hookRiseReach, F32, "m", 0, 120, "...nearer than this", 30.0f, 30.0f, 30.0f, 30.0f) \
    X(hookFlat, F32, "", 0, 56, "hooks across open ground longer than this (m)...", 14.0f, 14.0f, 14.0f, 8.0f) \
    X(hookFlatDrop, F32, "m", -12, 0, "...not dropping more than this", -3.0f, -3.0f, -3.0f, -3.0f) \
    X(repathEvery, F64, "", 0, 6, "plans the route again after (s)...", 1.5, 1.5, 1.5, 1.5) \
    X(repathRand, F64, "s", 0, 4, "...plus up to this", 1.0, 1.0, 1.0, 1.0) \
    X(steerEvery, F64, "s", 0, 0.08, "picks the next waypoint this often", 0.02, 0.02, 0.02, 0.02) \
    X(defuseDist, F32, "m", 0, 6, "bomb: defuses within this", 1.5f, 1.5f, 1.5f, 1.5f) \
    X(objectiveSlow, F32, "", 0, 1.2, "on the hill / point: moves at this fraction", 0.3f, 0.3f, 0.3f, 0.3f) \
    X(stuckDist, F32, "", 0, 2, "moved less than this...", 0.5f, 0.5f, 0.5f, 0.5f) \
    X(stuckEvery, F64, "s", 0, 6, "...in this long: stuck", 1.5, 1.5, 1.5, 1.5) \
    X(unstickTime, F64, "s", 0, 3.2, "sidesteps this long when stuck", 0.8, 0.8, 0.8, 0.8) \
    X(glanceIdle, F32, "rad", 0, 10, "standing: looks up to this far around", 2.6f, 2.6f, 2.6f, 2.6f) \
    X(glanceMoving, F32, "rad", 0, 6.4, "moving: glances up to this far off the route", 1.6f, 1.6f, 1.6f, 1.6f) \
    X(arriveDist, F32, "m", 0, 8, "a goal this close is reached", 2.0f, 2.0f, 2.0f, 2.0f) \
    X(huntHeardWindow, F64, "s", 0, 12, "no objective: goes after sounds this fresh", 3.0, 3.0, 3.0, 3.0) \
    X(roamGoal, F64, "", 0, 32, "roams to a spawn point for (s)...", 8.0, 8.0, 8.0, 8.0) \
    X(roamGoalRand, F64, "s", 0, 24, "...plus up to this", 6.0, 6.0, 6.0, 6.0) \
    X(seekLocatorWindow, F64, "", 0, 0.4, "seeker: follows a locator find this fresh (s)...", 0.1, 0.1, 0.1, 0.1) \
    X(seekLocatorGoal, F64, "s", 0, 24, "...for this long", 6.0, 6.0, 6.0, 6.0) \
    X(seekHeardWindow, F64, "", 0, 4, "seeker: follows sounds this fresh (s)...", 1.0, 1.0, 1.0, 1.0) \
    X(seekUnseen, F64, "", 0, 4, "...of someone not seen for this long (s)...", 1.0, 1.0, 1.0, 1.0) \
    X(seekHeardGoal, F64, "s", 0, 16, "...for this long", 4.0, 4.0, 4.0, 4.0) \
    X(seekWanderGoal, F64, "s", 0, 48, "seeker: wanders to a spot for", 12.0, 12.0, 12.0, 12.0) \
    X(evadeRangeTag, F32, "m", 0, 120, "tag: runs from 'it' seen within", 30.0f, 30.0f, 30.0f, 30.0f) \
    X(evadeRangeHide, F32, "m", 0, 72, "hide and seek: runs from the seeker seen within", 18.0f, 18.0f, 18.0f, 18.0f) \
    X(evadeHeardMult, F32, "", 0, 2, "...or heard within this fraction of that", 0.5f, 0.5f, 0.5f, 0.5f) \
    X(evadeFresh, F64, "s", 0, 3.2, "...this recently", 0.8, 0.8, 0.8, 0.8) \
    X(fleeCandidates, INT, "", 0, 48, "spots looked at for an escape", 12, 12, 12, 12) \
    X(fleeNearWeight, F32, "", 0, 2, "escape score: distance from 'it' minus this x distance from here", 0.5f, 0.5f, 0.5f, 0.5f) \
    X(fleeGoal, F64, "s", 0, 24, "runs to the escape spot for", 6.0, 6.0, 6.0, 6.0) \
    X(fleeCommit, F64, "", 0, 6, "committed to it for this long (s)...", 1.5, 1.5, 1.5, 1.5) \
    X(fleeHesitate, F32, "s", 0, 3.2, "...plus this x (1 - fleePredict)", 0.8f, 0.8f, 0.8f, 0.8f) \
    X(fleePredict, F32, "", 0, 3.2, "how well it reads where 'it' is going", 0.0f, 0.3f, 0.6f, 0.8f) \
    X(hideHold, F64, "", 0, 360, "hider keeps one spot for (s)...", 90.0, 90.0, 90.0, 90.0) \
    X(hideArrive, F32, "m", 0, 4.8, "...and stands still within this of it", 1.2f, 1.2f, 1.2f, 1.2f) \
    X(evadeWanderGoal, F64, "s", 0, 40, "tag runner: wanders to a spot for", 10.0, 10.0, 10.0, 10.0) \

// Lists, tables and data curves the native code reads (names as in the file).
#define QUARK_BRAIN_LISTS(X) X(close) X(mid) X(midNoLead) X(far) X(farNoLead) X(vfar) X(hookWeapons)
#define QUARK_BRAIN_TABLES(X) X(idealRange)
#define QUARK_BRAIN_CURVES(X) X(reactDist)

struct BrainParams {
#define QB_FIELD(name, type, unit, lo, hi, doc, a, b, c, d) QB_CT_##type name;
    QUARK_BRAIN_PARAMS(QB_FIELD)
#undef QB_FIELD
#define QB_FIELD(name) BrainList l_##name;
    QUARK_BRAIN_LISTS(QB_FIELD)
#undef QB_FIELD
#define QB_FIELD(name) float t_##name[16];
    QUARK_BRAIN_TABLES(QB_FIELD)
#undef QB_FIELD
#define QB_FIELD(name) BrainCurve c_##name;
    QUARK_BRAIN_CURVES(QB_FIELD)
#undef QB_FIELD
};

// The built-in brain's parameters for a level (0-3; out of range: INTERMEDIATE, like botSkillName).
const BrainParams& defaultBrainParams(int level);

// ---- slots: the decision points the bot code calls (GRAMMAR.md section 5)
enum BrainSlot : uint8_t {
    SLOT_REACT, SLOT_HEARING, SLOT_TARGET_SCORE, SLOT_URGENT, SLOT_PICK_WEAPON, SLOT_THROW_KNIFE, SLOT_IDEAL_RANGE,
    SLOT_REJUDGE, SLOT_FIRE, SLOT_FIGHT_HOOK, SLOT_STRAFE, SLOT_PULLED_DASH, SLOT_FIGHT_JUMP, SLOT_FIGHT_DASH,
    SLOT_GLANCE, SLOT_HOOK_RETRY, SLOT_ROUTE_HOOK, SLOT_BHOP, SLOT_RUN_DASH, SLOT_DASH_JUMP, SLOT_UNSTICK,
    SLOT_OBJECTIVE,                              // + mode (MODE_*); SLOT_OBJECTIVE + kBrainModes: `objective default`
    SLOT_COUNT = SLOT_OBJECTIVE + 13 + 1
};
constexpr int kBrainModes = 13;                  // MODE_COUNT (static_assert in botbrain.cpp)

// Sensors: what the bot code tells a slot (one id space; each slot gets its own few plus the common ones).
enum BrainSensor : uint8_t {
    S_NOW, S_DT, S_MODE, S_LEVEL, S_MYID, S_HEALTH, S_ITMODE,                 // common (every slot)
    S_OFF, S_DIST, S_SINCESEEN, S_GUNAGE, S_ONGROUND, S_GROUNDSPEED, S_DASHING, S_ANGLEOFF, S_CURRENT,
    S_CHECKPOINTSLEFT, S_HASHILL, S_HASCART, S_POINTACTIVE, S_BOMBPLANTED, S_BOMBDROPPED, S_BOMBCARRIED,
    S_ATTACKER, S_CARRIERISME, S_CARRIERALIVE, S_NAVREADY, S_IT, S_SITECOUNT,
    S_WEAPON, S_WEAPONRANGE, S_ERR, S_THROWKNIFE, S_OWNBLAST, S_HOOKRANGE, S_IDEAL, S_IDLE, S_GRAPPLEONLY,
    S_RISE, S_FLAT, S_FIGHTING, S_WPDIST, S_STRAIGHT, S_HOPPING, S_MOVING, S_MOVED, S_HASTARGET,
    S_COUNT
};
// Points an objective slot can go to (`goto`); the bot code turns them into positions.
enum BrainPoint : uint8_t { PT_ME, PT_CHECKPOINT, PT_HILL, PT_CART, PT_POINT, PT_BOMB, PT_CARRIER, PT_SITE, PT_COUNT };
// What an objective slot decided (its output 0; output 1 the point, output 2 site()'s argument).
enum BrainAction : uint8_t { ACT_NONE = 0, ACT_GOTO, ACT_FALLBACK, ACT_SEEK, ACT_EVADE };

// One call of an interpreted slot. The caller fills the sensors the slot gets (in[S_*]) and reads out[] (the slot's
// outputs in catalog order: bool 0/1, weapons as ids, times absolute). No allocation, no strings.
struct BrainCall {
    double in[S_COUNT];
    double out[8];
    uint32_t* rng = nullptr;                     // the bot's dice (BotBrain::rng)
    uint16_t weaponMask = 0;                     // Server::weaponMask() (0: every weapon)
    bool (*lazy)(void* user, int sensor) = nullptr; void* user = nullptr; // sensors worked out only when read (S_OWNBLAST)
    float* chanceP = nullptr;                    // the bot's cache of chance(param) thresholds (BrainChanceCache)
};
struct BrainChanceCache { const void* brain = nullptr; int level = -1; float dt = -1; float p[16] = {}; };

struct BrainInsn { uint8_t op, d, a, b; int32_t k; };   // 8 bytes: an op over a fixed register file

// A compiled brain. Shared (const) by every bot that plays it.
struct Brain {
    std::string name;                            // `brain "..."`
    std::string hash;                            // 8 hex: sha256 of the canonical text without comments
    std::string text;                            // canonical text
    bool builtIn = false;                        // the embedded default
    BrainParams lvl[4];                          // the native code's numbers, per level
    bool native[SLOT_COUNT] = {};                // slot runs the built-in C++ code (its AST is the default's)
    uint8_t objSlot[kBrainModes];                // the slot each mode's objective runs (an objective slot index)
    // the interpreter's program (every slot is compiled, native or not)
    std::vector<BrainInsn> code;
    int32_t start[SLOT_COUNT];                   // first instruction of each slot (-1: none)
    uint8_t regs[SLOT_COUNT] = {};               // registers it uses
    uint8_t rolls[SLOT_COUNT] = {};              // random draws on its longest path (validation display)
    std::vector<double> konst;                   // constants
    std::vector<double> P[4];                    // every param (catalog and the file's own), per level
    std::vector<BrainList> lists;
    std::vector<std::vector<float>> tables;      // per weapon (16)
    std::vector<BrainCurve> curves;              // data curves
    std::vector<int> chanceParams;               // params used as chance() rates (cached per bot: BrainChanceCache)
    bool allNative = false;                      // every slot native: the bot code skips the interpreter entirely
    bool usesSlot(int slot) const { return start[slot] >= 0; }
};

// Run slot `slot` of brain `b` for skill level `level` (0-3). Outputs in call.out.
void runBrainSlot(const Brain& b, int slot, int level, float dt, BrainCall& call);

// ---- loading, checking, the editor's tools
struct BrainDiag {
    struct Msg { int line = 0, col = 0; std::string msg; };
    std::vector<Msg> errors, warnings;
};
constexpr int kBrainFormatVersion = 1;
// Compile brain text (any version: older ones are converted first). Null on errors (in *diag, with line:col).
// forceInterpreter: every slot runs in the interpreter, even ones equal to the default (lockstep tests, cost).
std::shared_ptr<const Brain> compileBrain(const std::string& text, BrainDiag* diag = nullptr, bool forceInterpreter = false);
// Brain text of an older format version converted to the current one (unchanged if current; "" + *err if it can't be).
std::string upgradeBrainText(const std::string& text, std::string* err = nullptr, int* fromVersion = nullptr);
const Brain& defaultBrain();                     // the built-in brain (native code; params = the C++ table)
const char* defaultBrainText();                  // tools/bots/default.qbrain as embedded at build time
// userDataPath("bots")/<name>.qbrain, compiled (cached while the file is unchanged). "" or "default": null with no
// *err (the built-in brain: give setBotBrain null).
// An older format version is converted on disk (the original kept in bots/old/v<N>/). Null + *err on failure.
std::shared_ptr<const Brain> loadBrain(const std::string& name, std::string* err = nullptr);
std::string brainLabel(const Brain& b);          // "name#hash"
std::string brainDiagJson(const BrainDiag& d, const Brain* b);   // checkbrain's JSON
std::string brainAstJson(const std::string& text, BrainDiag* diag);   // the JSON AST of text ("" on parse errors)
std::string printBrainText(const std::string& text, BrainDiag* diag); // canonical text ("" on parse errors)
std::string exportBrainJson();                   // default text + AST + the catalog (quark_tests exportbrain)

class Server;
// Give bot `id` a brain (null: the built-in one). False if `id` isn't a bot, or the server is ranked / time trial
// (bots there always play the built-in brain). Defined in bots.cpp.
bool setBotBrain(Server& s, int id, std::shared_ptr<const Brain> brain);
