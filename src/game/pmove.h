// Shared player movement. Runs identically on server (authoritative) and client (prediction).
#pragma once
#include "../common/qmath.h"
#include "map.h"

struct UserCmd {
    uint32_t seq = 0;       // monotonically increasing per client
    uint32_t viewTick = 0;  // server tick the client was rendering (lag compensation)
    int8_t forward = 0;     // -127..127
    int8_t side = 0;
    uint16_t buttons = 0;
    uint8_t msec = 1;       // duration this command covers (1, 2, 4 or 8 ms = one server tick)
    uint8_t weapon = 255;   // weapon to switch to (kNoWeaponChange = keep)
    float yaw = 0, pitch = 0;
};

// Everything prediction needs to replay movement. Sent unquantized for the local player.
struct MoveState {
    Vec3 pos;      // feet position (bottom center of hull)
    Vec3 vel;
    uint8_t flags = 0; // MF_*
    float dashCooldown = 0;
    float dashTimer = 0;     // > 0 while the dash burst is active
    float dashDirX = 0, dashDirZ = 0;
    float dashUp = 0;        // vertical part of the dash (from look pitch, small)
    float dashExitSpeed = 0; // horizontal speed to settle to when the burst ends
    float hookCooldown = 0;
    Vec3 hookPoint;    // grapple anchor while MF_HOOKED
    uint8_t mods = 0;  // MOVE_* rule modifiers (duel)
};
enum : uint8_t { MOVE_GRAPPLE_ONLY = 1 }; // no walking, jumping or dashing: the grapple is the only way to move
enum : uint8_t {
    MF_ONGROUND = 1, MF_CROUCHED = 2, MF_JUMPHELD = 4, MF_FROZEN = 8,
    MF_HOOKHELD = 16, MF_HOOKED = 32, MF_DASHHELD = 64,
};
constexpr float kGrappleRange = 45.0f;
constexpr float kDashCooldown = 1.2f;
constexpr float kHookCooldown = 1.0f;   // after the hook is released

AABB playerBox(const Vec3& feet, bool crouched);
// Advances movement by cmd.msec milliseconds, in fixed 1 ms steps (tick-rate independent).
void pmove(MoveState& s, const UserCmd& cmd, const Map& map);
