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
    float yaw = 0, pitch = 0;
};

// Everything prediction needs to replay movement. Sent unquantized for the local player.
struct MoveState {
    Vec3 pos;      // feet position (bottom center of hull)
    Vec3 vel;
    uint8_t flags = 0; // MF_*
    float jumpTimer = 0;
};
enum : uint8_t { MF_ONGROUND = 1, MF_CROUCHED = 2, MF_JUMPHELD = 4, MF_FROZEN = 8 };

AABB playerBox(const Vec3& feet, bool crouched);
// Advances movement by one tick.
void pmove(MoveState& s, const UserCmd& cmd, const Map& map, float dt);
