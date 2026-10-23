// Shared player movement. Runs identically on server (authoritative) and client (prediction).
#pragma once
#include "../common/qmath.h"
#include "map.h"

struct UserCmd {
    uint32_t seq = 0;       // monotonically increasing per client
    uint32_t viewTick = 0;  // server ms the client was rendering (lag compensation; low 32 bits, the server unwraps it)
    int8_t forward = 0;     // -127..127
    int8_t side = 0;
    uint16_t buttons = 0;
    uint8_t msec = 1;       // duration this command covers (1, 2, 4 or 8 ms = one server tick)
    uint8_t weapon = 255;   // weapon to switch to (kNoWeaponChange = keep)
    float yaw = 0, pitch = 0;
};

constexpr int kDashCharges = 2;   // dashes you hold (owner 1.17: charges replace the 1.2 s cooldown; see pmove.cpp)

// Everything prediction needs to replay movement. Sent unquantized for the local player.
struct MoveState {
    Vec3 pos;      // feet position (bottom center of hull)
    Vec3 vel;
    uint8_t flags = 0; // MF_*
    uint8_t dashCharges = kDashCharges;   // dashes left: the ground refills them all, a bhop landing one
    float dashTimer = 0;     // > 0 while the dash burst is active
    float dashDirX = 0, dashDirZ = 0;
    float dashUp = 0;        // vertical part of the dash (from look pitch, small)
    float dashExitSpeed = 0; // horizontal speed to settle to when the burst ends
    float jumpBuffer = 0;    // a fresh jump press still counts this long for a dash jump / super dash (frozen while dashing)
    float jumpGrace = 0;     // jumped off the ground by a press this recently: a dash now is still a ground dash jump
    uint8_t dashBounces = 0; // wall bounces this dash has made
    float hookCooldown = 0;
    Vec3 hookPoint;    // grapple anchor while MF_HOOKED (latched onto a player: their body, moved by the server every tick)
    uint8_t mods = 0;  // MOVE_* rule modifiers (duel)
    uint8_t hookTarget = 0; // while MF_HOOKED: 0 = the map, 1 + id = latched onto that player
    Vec3 pullPoint;    // while MF_PULLED: the body of the player whose grapple holds you (set by the server every tick)
};
enum : uint8_t {
    MOVE_GRAPPLE_ONLY = 1, // no walking, jumping or dashing: the grapple is the only way to move
    MOVE_QUICK_DASH = 2,   // one dash charge more (3)
    MOVE_LOW_GRAVITY = 4,  // 40% gravity
    MOVE_RUN_ONLY = 8,     // no dash, no grapple: run and jump (and climb ladders) only
    MOVE_BIG = 16,         // duel "double size" / "giant snipers": the hull (and eyes) x kBigScale
    MOVE_TINY = 32,        // duel "tiny players": x kTinyScale
};
constexpr float kBigScale = 2.0f, kTinyScale = 0.6f;
inline float moveScale(uint8_t mods) { return (mods & MOVE_BIG) ? kBigScale : (mods & MOVE_TINY) ? kTinyScale : 1.0f; }
// the MOVE_* size bit for a body scale (DuelModDef::size: 1, 2 or 0.6)
inline uint8_t moveScaleMods(float size) { return size > 1.01f ? MOVE_BIG : size < 0.99f ? MOVE_TINY : 0; }
enum : uint8_t {
    MF_ONGROUND = 1, MF_CROUCHED = 2, MF_JUMPHELD = 4, MF_FROZEN = 8,
    MF_HOOKHELD = 16, MF_HOOKED = 32, MF_DASHHELD = 64,
    MF_PULLED = 128,   // another player's grapple holds you: pulled toward pullPoint (a fresh dash or grab breaks free)
};
constexpr float kGrappleRange = 45.0f;
constexpr float kHookCooldown = 1.0f;   // after the hook is released

// The hull: kPlayerHalfWidth each side, kPlayerHeight tall (crouched: kPlayerCrouchHeight), times scale (size modifiers).
AABB playerBox(const Vec3& feet, bool crouched, float scale = 1.0f);
inline AABB playerBox(const MoveState& m) { return playerBox(m.pos, (m.flags & MF_CROUCHED) != 0, moveScale(m.mods)); }
// The head inside a hitbox: a head-sized box on top, centred (owner 5.5: shoulders aren't headshots).
AABB headBox(const AABB& hitbox);
// A shot along o + dir * t that hit `hitbox` at entry: is it a headshot? (It passes through the head box.)
bool rayHitsHead(const Vec3& o, const Vec3& dir, const AABB& hitbox);
// the standard values a custom mode scales (MOVEMENT / GENERAL pages show "standard -> this mode"), and their units
float moveParamBase(int param);           // MV_*
const char* moveParamUnit(int param);
float moveBaseGravity(), moveBaseSpeed(), moveBaseJump();
// how many dash charges a full refill gives (kDashCharges x the custom mode's multiplier, at least 1; quick dash +1)
int dashMaxCharges(uint8_t mods);
// The players a grapple fired in this command can latch onto (the server fills it, lag compensated: where the shooter
// saw them; enemies only). The hook takes the nearest of these or the map along its ray; `hit` says which box caught.
struct HookProbe {
    static constexpr int kMax = 32;   // (kMaxPlayers)
    int n = 0;
    AABB box[kMax];
    uint8_t id[kMax] = {};
    int hit = -1;        // out: index of the box the hook caught (-1: none, or the map)
    bool head = false;   // out: the ray went through that player's head box
};
// Advances movement by cmd.msec milliseconds, in fixed 1 ms steps (tick-rate independent). `probe`: players the
// grapple can catch (server only; prediction hooks the map and the server's state corrects it).
void pmove(MoveState& s, const UserCmd& cmd, const Map& map, HookProbe* probe = nullptr);
