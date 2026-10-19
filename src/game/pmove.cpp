#include "pmove.h"
#include "gamedefs.h"

// Placeholder tuning; movement will be redefined later.
namespace {
constexpr float kGravity = 20.0f;
constexpr float kJumpSpeed = 7.0f;
constexpr float kGroundSpeed = 8.0f;
constexpr float kCrouchSpeed = 4.0f;
constexpr float kGroundAccel = 12.0f;
constexpr float kAirAccel = 1.2f;
constexpr float kAirSpeedCap = 1.0f; // quake-style air strafe cap
constexpr float kFriction = 7.0f;
constexpr float kStopSpeed = 2.0f;
constexpr float kStepHeight = 0.55f;
}

AABB playerBox(const Vec3& f, bool crouched) {
    float h = crouched ? kPlayerCrouchHeight : kPlayerHeight;
    return {{f.x - kPlayerHalfWidth, f.y, f.z - kPlayerHalfWidth}, {f.x + kPlayerHalfWidth, f.y + h, f.z + kPlayerHalfWidth}};
}

static void accelerate(Vec3& vel, const Vec3& wishDir, float wishSpeed, float accel, float dt) {
    float cur = dot(vel, wishDir);
    float add = wishSpeed - cur;
    if (add <= 0) return;
    float a = std::min(accel * wishSpeed * dt, add);
    vel += wishDir * a;
}

// Moves the hull through the world one axis at a time. Returns which axes were blocked.
static int slideMove(MoveState& s, const Map& map, const Vec3& delta, bool crouched) {
    int blocked = 0;
    const int order[3] = {1, 0, 2};
    for (int i : order) {
        AABB box = playerBox(s.pos, crouched);
        float d = map.sweepAxis(box, i, delta[i]);
        if (std::fabs(d - delta[i]) > 1e-5f) blocked |= 1 << i;
        s.pos[i] += d;
    }
    return blocked;
}

void pmove(MoveState& s, const UserCmd& cmd, const Map& map, float dt) {
    if (s.flags & MF_FROZEN) {
        s.vel = {0, std::min(s.vel.y, 0.0f), 0};
    }
    bool wantCrouch = (cmd.buttons & BTN_CROUCH) != 0;
    bool crouched = (s.flags & MF_CROUCHED) != 0;
    if (wantCrouch && !crouched) {
        crouched = true;
    } else if (!wantCrouch && crouched) {
        if (!map.boxSolid(playerBox(s.pos, false))) crouched = false;
    }

    Vec3 fwd = {-std::sin(cmd.yaw), 0, -std::cos(cmd.yaw)};
    Vec3 right = {std::cos(cmd.yaw), 0, -std::sin(cmd.yaw)};
    Vec3 wish = fwd * (cmd.forward / 127.0f) + right * (cmd.side / 127.0f);
    if (s.flags & MF_FROZEN) wish = {};
    float wishLen = length(wish);
    Vec3 wishDir = wishLen > 0 ? wish / wishLen : Vec3();
    float maxSpeed = crouched ? kCrouchSpeed : kGroundSpeed;
    float wishSpeed = std::min(wishLen, 1.0f) * maxSpeed;

    bool onGround = (s.flags & MF_ONGROUND) != 0;
    bool jumpHeld = (cmd.buttons & BTN_JUMP) != 0;
    if (onGround && jumpHeld && !(s.flags & MF_JUMPHELD) && !(s.flags & MF_FROZEN)) {
        s.vel.y = kJumpSpeed;
        onGround = false;
    }

    if (onGround) {
        float sp = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        if (sp > 0) {
            float drop = std::max(sp, kStopSpeed) * kFriction * dt;
            float ns = std::max(sp - drop, 0.0f) / sp;
            s.vel.x *= ns;
            s.vel.z *= ns;
        }
        accelerate(s.vel, wishDir, wishSpeed, kGroundAccel, dt);
        s.vel.y = 0;
    } else {
        accelerate(s.vel, wishDir, std::min(wishSpeed, kAirSpeedCap), kAirAccel * 10.0f, dt);
        s.vel.y -= kGravity * dt;
    }

    Vec3 delta = s.vel * dt;
    MoveState before = s;
    int blocked = slideMove(s, map, delta, crouched);

    // Step up small ledges when walking into them.
    if (onGround && (blocked & 0b101)) {
        MoveState step = before;
        AABB b = playerBox(step.pos, crouched);
        float up = map.sweepAxis(b, 1, kStepHeight);
        step.pos.y += up;
        slideMove(step, map, {delta.x, 0, delta.z}, crouched);
        AABB b2 = playerBox(step.pos, crouched);
        step.pos.y += map.sweepAxis(b2, 1, -up);
        float d0 = (s.pos.x - before.pos.x) * (s.pos.x - before.pos.x) + (s.pos.z - before.pos.z) * (s.pos.z - before.pos.z);
        float d1 = (step.pos.x - before.pos.x) * (step.pos.x - before.pos.x) + (step.pos.z - before.pos.z) * (step.pos.z - before.pos.z);
        if (d1 > d0 + 1e-6f) { s.pos = step.pos; blocked &= ~0b101; }
    }
    if (blocked & 1) s.vel.x = 0;
    if (blocked & 4) s.vel.z = 0;
    if (blocked & 2) {
        if (s.vel.y > 0) s.vel.y = 0;
    }

    // Ground check: probe slightly below.
    AABB feet = playerBox(s.pos, crouched);
    float probe = map.sweepAxis(feet, 1, -0.05f);
    bool nowGround = probe > -0.049f && s.vel.y <= 0.01f;
    if (nowGround) {
        s.pos.y += probe; // settle onto the floor
        if (s.vel.y < 0) s.vel.y = 0;
    } else if (onGround && s.vel.y <= 0) {
        float down = map.sweepAxis(feet, 1, -kStepHeight);
        if (down > -kStepHeight + 0.001f) { s.pos.y += down; nowGround = true; s.vel.y = 0; }
    }

    s.flags &= MF_FROZEN;
    if (nowGround) s.flags |= MF_ONGROUND;
    if (crouched) s.flags |= MF_CROUCHED;
    if (jumpHeld) s.flags |= MF_JUMPHELD;

    if (s.pos.y < -100.0f) s.pos.y = -100.0f; // kill plane handled by game logic
}
