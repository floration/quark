// TUTORIAL (1.19.9, "Tutorial update"; q: "show a demo of a bot doing whatever movement tech, then puts you on a map
// that shows the bot doing it and gives you instructions on how to"): one lesson per movement tech on the map
// `tutorial` (tools/maps/tutorial.py: a sealed bay per lesson, laid out from the same numbers as kBays below).
//
// This is the part without drawing or networking, shared by three users:
// - the server (tutorial_server.cpp): the GHOST, a bot whose commands are each lesson's input script (ghostCmd) run
//   through the normal pmove and weapons, looping, put back at its start every loop;
// - the game (src/client/tutorial.cpp): the keys the ghost is pressing (the same script at the same time), and the
//   lesson's check (Check) fed with the own predicted movement each frame;
// - quark_tests (group "tutorial"): every script run on the real map does the tech its lesson asks for.
//
// Times are ms from the start of the ghost's loop. Yaw: the ghost and the player start facing +x (kStartYaw); a
// script's yaw is an offset from that (degrees, + turns left), its pitch absolute (degrees, + up).
#pragma once
#include "pmove.h"
#include "gamedefs.h"
#include <cstdint>

namespace tut {

enum Lesson : int {
    L_MOVE = 0,     // walking: run and jump the hurdles
    L_STRAFE,       // A/D strafing: bunny hops with a strafe key and the mouse turned with it
    L_DASH,         // the dash: an air dash across a lava gap
    L_SIDEDASH,     // sideways dashes: a dash goes the way you move, not the way you look
    L_SUPER,        // super (the dash jump): jump during a floor dash, 24 m/s
    L_HYPER,        // hyper (the super dash): dash down into the floor holding jump, your speed + 5
    L_CHAIN,        // super chaining: hypers chained off bhop landings
    L_SLIDE,        // the dash slide: dash down into the floor without jumping, ice's grip
    L_BOUNCE,       // the dash wall bounce: dash head on into a wall
    L_GRAPPLE,      // the grapple: up onto a 10 m ledge
    L_HOOKWALK,     // grapple walking: hook the floor far ahead, dragged along it with no friction
    L_HOOKPLAYER,   // KEY TECH (q): grappling another player: the hook latches onto the PARTNER running across the bay
    L_BREAKFREE,    // KEY TECH (q): the PARTNER's grapple catches you (the ghost in the demo): a dash breaks free
    L_ROCKETJUMP,   // rocket jumping
    L_LAUNCH,       // KEY TECH (q): grapple to 24 m/s, then a rocket straight down and a hyper straight down
    L_STICKYJUMP,   // sticky jumping
    L_ROCKETGRAPPLE,   // KEY TECH (q): the rocket grapple (rocket riding): the grapple catches your own rocket
    L_COUNT
};

constexpr const char* kMapName = "tutorial";
constexpr float kStartYaw = -PI * 0.5f;   // facing +x
constexpr float kGhostZ = -6.0f, kPlayerZ = 6.0f;   // the two lanes of every bay, from its middle (the ghost on your left)
constexpr float kStartIn = 3.0f;          // both start this far into their bay
constexpr float kBayHalfZ = 12.0f;        // every bay is 24 m across
constexpr float kWall = 1.0f;             // the walls between bays
// The bays side by side along z, all starting at x kBayX0 and running along +x: the map stays inside the snapshots'
// +-512 m (Buffer::vecq: 1/64 m in an int16; a row of them along x ran past it, the ghost drawn stuck at x 512)
constexpr float kBayX0 = -90.0f;

// One step of a script: held for `ms`. fwd / side -127..127 (side + = right), btn BTN_*, the yaw turning at yawRate
// degrees a second (+ = left) and the pitch set to `pitch` degrees; pitch kAimAt: aimed at the other one (the ghost at
// the PARTNER, the partner at whoever it hooks: the server works the yaw and pitch out every tick, aimAt)
struct Seg { uint16_t ms; int8_t fwd, side; uint16_t btn; float yawRate, pitch; };
constexpr float kAimAt = 1000.0f;

// What a lesson asks for (Check)
enum Goal : uint8_t {
    G_REACH,        // on the floor at least `target` m into the bay (from its start wall)
    G_SPEED,        // horizontal speed >= target, no dash or grapple used since you last stood still on the floor
    G_GAP,          // on the floor past `target` m into the bay, having dashed since you were last on the floor before `aux` m
    G_SIDEDASH,     // a dash holding only LEFT and one holding only RIGHT (not forward or back)
    G_EVENTS,       // `target` of the ME_* events in `aux`
    G_SLIDE,        // in a dash slide for `target` seconds in a row
    G_BOUNCES,      // `target` dash wall bounces
    G_LEDGE,        // on the floor `target` m up or higher, having grappled since you were last on the ground below it
    G_HOOKSPEED,    // dragged along the floor by the grapple (anchor below your waist) at `target` m/s
    G_KNOCKUP,      // thrown up by your own blast (holding this lesson's weapon), then `target` m up or higher
    G_RIDE,         // riding a rocket on the grapple for `target` seconds
    G_LAUNCH,       // a hyper out of `aux` m/s or more, then thrown up by your own rocket, then `target` m up or higher
    G_HOOKPLAYER,   // your grapple latched onto a player `target` times (MoveState::hookTarget 1 + their id)
    G_BREAKFREE,    // held by another player's grapple (MF_PULLED), then a dash that breaks free: `target` times
};

struct LessonDef {
    const char* id;      // --tutorial NAME, the lesson list
    float len, height;   // the bay's inside length (x) and ceiling
    uint8_t weapon;      // held for the lesson (kNoWeaponChange: none in particular)
    const Seg* script;
    int segs;
    Goal goal;
    float target, aux;
    bool key;            // KEY TECH (q: "big techs that are important")
    // the PARTNER (the grapple lessons with another player: a second scripted bot), null: none; it starts `partnerIn` m
    // into the bay in its middle, facing back down it (-x), and loops its script with the ghost's
    const Seg* partner = nullptr;
    int partnerSegs = 0;
    float partnerIn = 0;
};

const LessonDef& lesson(int l);
int lessonFromName(const char* s);   // an id ("hyper") or a number (0-based); -1: none
float bayX0(int l);                  // the bay's start wall (inside face), x (kBayX0: every bay's)
float bayZ(int l);                   // the bay's middle, z
Vec3 ghostStart(int l);
Vec3 playerStart(int l);
int loopMs(int l);                   // the ghost's loop
// The ghost's command at `tMs` into its loop (yaw / pitch absolute, radians; weapon: the lesson's). tMs < 0: standing
// at the start, nothing pressed.
UserCmd ghostCmd(int l, int tMs);
// The PARTNER's (lessons with one: LessonDef::partner); yaw from facing -x
UserCmd partnerCmd(int l, int tMs);
Vec3 partnerStart(int l);
constexpr float kPartnerYaw = PI * 0.5f;   // facing -x, back down the bay at you
// Whether the ghost's (partner's) script aims at the other one at tMs (Seg pitch kAimAt), and the yaw / pitch that
// aims from `eye` at `at`
bool ghostAimsAt(int l, int tMs);
bool partnerAimsAt(int l, int tMs);
void aimAt(const Vec3& eye, const Vec3& at, float& yaw, float& pitch);

// A lesson's check, fed the own movement once a frame (predicted: what you see). No allocation, a few comparisons.
struct Check {
    int lesson = -1;
    bool done = false;
    float progress = 0;   // 0..1, for the bar
    float value = 0;      // what's measured now (speed, height, count...), for the readout
    // the attempt
    bool havePrev = false, dashed = false, hooked = false, knocked = false, armed = false, left = false, right = false;
    Vec3 prevVel;
    uint8_t prevBounces = 0;
    bool prevOn = false;   // (G_HOOKPLAYER: latched onto a player last frame; G_BREAKFREE: held by one)
    int count = 0;
    float timer = 0, still = 0, best = 0;
    void start(int l);
    // m: the own predicted movement; ev: the ME_* events since the last frame; fwd / side: the keys held (the last
    // command's); weapon: the weapon held; dt: the frame's seconds
    void frame(const MoveState& m, uint8_t ev, int fwd, int side, int weapon, float dt);
};

}  // namespace tut
