#include "pmove.h"
#include "gamedefs.h"

// Placeholder tuning; movement will be redefined later.
namespace {
inline float tune(int i) { return moveTuning().m[i]; }   // a custom mode's x on that constant (1 normally)
constexpr float kGravity = 22.0f;   // (1.14: +10%, less floaty)
constexpr float kJumpSpeed = 7.34f;  // (with the heavier gravity: still a 1.22 m standing jump, maps rely on it)
constexpr float kGroundSpeed = 8.0f;
constexpr float kCrouchSpeed = 4.0f;
constexpr float kGroundAccel = 12.0f;
constexpr float kAirAccel = 2.2f;
constexpr float kAirSpeedCap = 2.3f; // quake-style air strafe cap: a wider window of angles that gain speed (was 2.0)
constexpr float kOverstrafeBrake = 0.35f; // turning past the window: only this much of the push against your momentum applies
constexpr float kAirControl = 4.0f;  // how fast held direction bends momentum in the air (rad/s-ish), speed kept
constexpr float kFriction = 7.0f;
constexpr float kStopSpeed = 2.0f;
constexpr float kStepHeight = 0.55f;
constexpr float kDashSpeed = 24.0f;     // burst speed
constexpr float kDashTime = 0.12f;      // burst length; then speed settles back
constexpr float kDashExitMin = 11.0f;   // speed you leave a dash with at least
constexpr float kDashKeep = 3.0f;       // ...and your old speed plus this, so dashes build momentum
constexpr float kDashExitMax = 32.0f;   // (1.12: above the grapple's 24, so a dash out of a fast strafe still adds speed)
constexpr float kDashVertical = 0.6375f;  // looking straight up / down makes the dash this vertical (sine)
constexpr float kBounceGain = 0.5f;     // bhop: landing faster than a jump adds this share of the extra to the jump
constexpr float kBounceMax = 8.0f;
constexpr float kHookAccel = 38.0f;     // pull acceleration toward the anchor (adds to momentum)
constexpr float kHookMaxSpeed = 24.0f;
constexpr float kHookGravityScale = 0.5f;
constexpr float kHookRelease = 1.2f;    // auto-release this close to the anchor
constexpr float kLadderSpeed = 4.5f;    // climbing speed
constexpr float kLadderSide = 3.0f;     // sideways speed on a ladder
}

AABB playerBox(const Vec3& f, bool crouched) {
    float h = crouched ? kPlayerCrouchHeight : kPlayerHeight;
    return {{f.x - kPlayerHalfWidth, f.y, f.z - kPlayerHalfWidth}, {f.x + kPlayerHalfWidth, f.y + h, f.z + kPlayerHalfWidth}};
}

// Air strafing: Quake acceleration, except that the part of the push that would brake you (wish direction turned
// past 90 degrees from your momentum: overstrafing) is softened, so a sloppy turn costs a little, not a lot.
static void airAccelerate(Vec3& vel, const Vec3& wishDir, float wishSpeed, float accel, float dt) {
    float cur = dot(vel, wishDir);
    float add = wishSpeed - cur;
    if (add <= 0) return;
    Vec3 push = wishDir * std::min(accel * wishSpeed * dt, add);
    float h = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    if (h > 1e-3f) {
        Vec3 along = {vel.x / h, 0, vel.z / h};
        float pa = dot(push, along);
        if (pa < 0) push = push - along * (pa * (1.0f - (kOverstrafeBrake * tune(MV_OVERSTRAFE))));
    }
    vel += push;
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

static void pmoveStep(MoveState& s, const UserCmd& cmd, const Map& map, float dt) {
    if (s.flags & MF_FROZEN) {
        s.vel = {0, std::min(s.vel.y, 0.0f), 0};
    }
    bool wantCrouch = false; // crouching was removed from the game (BTN_CROUCH is ignored)
    bool crouched = (s.flags & MF_CROUCHED) != 0;
    if (wantCrouch && !crouched) {
        crouched = true;
    } else if (!wantCrouch && crouched) {
        if (!map.boxSolid(playerBox(s.pos, false))) crouched = false;
    }

    Vec3 fwd = {-std::sin(cmd.yaw), 0, -std::cos(cmd.yaw)};
    Vec3 right = {std::cos(cmd.yaw), 0, -std::sin(cmd.yaw)};
    Vec3 wish = fwd * (cmd.forward / 127.0f) + right * (cmd.side / 127.0f);
    if ((s.flags & MF_FROZEN) || (s.mods & MOVE_GRAPPLE_ONLY)) wish = {};
    float wishLen = length(wish);
    Vec3 wishDir = wishLen > 0 ? wish / wishLen : Vec3();
    const MoveTuning& mt = moveTuning(); // a custom mode's movement (all 1 / on normally)
    const float jumpSpeed = kJumpSpeed * mt.jump;
    float maxSpeed = (crouched ? (kCrouchSpeed * tune(MV_CROUCH_SPEED)) : kGroundSpeed) * mt.speed;
    float wishSpeed = std::min(wishLen, 1.0f) * maxSpeed;

    bool onGround = (s.flags & MF_ONGROUND) != 0;
    const bool grappleOnly = (s.mods & MOVE_GRAPPLE_ONLY) != 0;
    bool jumpHeld = (cmd.buttons & BTN_JUMP) != 0 && !grappleOnly;
    bool frozen = (s.flags & MF_FROZEN) != 0;
    // Holding jump bunny-hops: jump on the first grounded tick, before friction applies.
    if (onGround && jumpHeld && !frozen) {
        s.vel.y = jumpSpeed;
        onGround = false;
    }

    // Dash: one burst in the movement direction (or forward), on a cooldown.
    s.dashCooldown = std::max(0.0f, s.dashCooldown - dt);
    s.hookCooldown = std::max(0.0f, s.hookCooldown - dt);
    bool dashHeld = (cmd.buttons & BTN_DASH) != 0 && !grappleOnly && !(s.mods & MOVE_RUN_ONLY) && mt.dash;
    if (dashHeld && !(s.flags & MF_DASHHELD) && s.dashCooldown <= 0 && !frozen) {
        Vec3 d = wishLen > 0 ? wishDir : fwd;
        float h = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        s.dashDirX = d.x;
        s.dashDirZ = d.z;
        s.dashExitSpeed = std::min((kDashExitMax * tune(MV_DASH_EXIT_MAX)), std::max((kDashExitMin * tune(MV_DASH_EXIT_MIN)), h + (kDashKeep * tune(MV_DASH_KEEP))));
        s.dashUp = std::sin(cmd.pitch) * (kDashVertical * tune(MV_DASH_VERTICAL));
        s.dashTimer = (kDashTime * tune(MV_DASH_TIME));
        s.dashCooldown = (s.mods & MOVE_QUICK_DASH) ? (kDashCooldown * tune(MV_DASH_COOLDOWN)) / 3 : (kDashCooldown * tune(MV_DASH_COOLDOWN));
    }
    bool dashing = s.dashTimer > 0;
    if (dashing) {
        // Short, strong burst, tilted a little by where you look: no gravity, no steering.
        float hs = std::sqrt(std::max(0.0f, 1.0f - s.dashUp * s.dashUp)); // aiming up / down trades horizontal for vertical
        float burst = std::max((kDashSpeed * tune(MV_DASH_SPEED)), s.dashExitSpeed); // (never slower than what you leave it with)
        s.vel = {s.dashDirX * burst * hs, s.dashUp * burst, s.dashDirZ * burst * hs};
        onGround = false;
        s.dashTimer -= dt;
        if (s.dashTimer <= 0) {
            s.dashTimer = 0;
            s.vel.x = s.dashDirX * s.dashExitSpeed * hs;
            s.vel.z = s.dashDirZ * s.dashExitSpeed * hs;
            s.vel.y = s.dashUp * s.dashExitSpeed;
        }
    }

    // Grapple: hold to fire, hold to be pulled; momentum is kept. While held it keeps trying every tick
    // until it catches something (sweep your aim across a thin bar and it grabs it), also waiting out the
    // cooldown; one grab per press (MF_HOOKHELD: this press already grabbed), release to fire again.
    bool hookHeld = (cmd.buttons & BTN_GRAPPLE) != 0 && !(s.mods & MOVE_RUN_ONLY) && mt.grapple;
    bool wasHooked = (s.flags & MF_HOOKED) != 0;
    if (!hookHeld) s.flags &= ~MF_HOOKHELD;
    if (hookHeld && !(s.flags & MF_HOOKHELD) && !(s.flags & MF_HOOKED) && !frozen && s.hookCooldown <= 0) {
        float eyeH = crouched ? kCrouchEyeHeight : kEyeHeight;
        Vec3 eye = s.pos + Vec3(0, eyeH, 0);
        float t = map.raycast(eye, angleForward(cmd.yaw, cmd.pitch), (kGrappleRange * tune(MV_HOOK_RANGE)));
        if (t >= 0) {
            s.hookPoint = eye + angleForward(cmd.yaw, cmd.pitch) * t;
            s.flags |= MF_HOOKED | MF_HOOKHELD;
        }
    }
    if (!hookHeld || frozen) s.flags &= ~MF_HOOKED;
    bool hooked = (s.flags & MF_HOOKED) != 0;
    if (hooked) {
        Vec3 to = s.hookPoint - (s.pos + Vec3(0, 1.0f, 0));
        float dist = length(to);
        if (dist < (kHookRelease * tune(MV_HOOK_RELEASE))) {
            s.flags &= ~MF_HOOKED;
            hooked = false;
        } else if (!dashing) {
            s.vel += to / dist * ((kHookAccel * tune(MV_HOOK_ACCEL)) * dt);
            float sp = length(s.vel);
            if (sp > (kHookMaxSpeed * tune(MV_HOOK_MAX))) s.vel *= (kHookMaxSpeed * tune(MV_HOOK_MAX)) / sp;
            onGround = false;
        }
    }
    if (wasHooked && !(s.flags & MF_HOOKED)) s.hookCooldown = (kHookCooldown * tune(MV_HOOK_COOLDOWN));

    // Ladders: inside a ladder volume, moving climbs (look down to climb down), no input holds you there,
    // a fresh jump kicks you off backwards. No gravity while on it.
    bool onLadder = !hooked && !dashing && !frozen && map.onLadder(playerBox(s.pos, crouched));
    if (onLadder) {
        bool jumpPress = jumpHeld && !(s.flags & MF_JUMPHELD);
        if (jumpPress) {
            s.vel = fwd * -4.0f + Vec3(0, jumpSpeed * 0.7f, 0);
        } else {
            float climb = std::min(wishLen, 1.0f) * (kLadderSpeed * tune(MV_LADDER_SPEED)) * (cmd.pitch < -0.45f ? -1.0f : 1.0f);
            s.vel = {wishDir.x * std::min(wishSpeed, (kLadderSide * tune(MV_LADDER_SIDE))), wishLen > 0 ? climb : 0.0f, wishDir.z * std::min(wishSpeed, (kLadderSide * tune(MV_LADDER_SIDE)))};
        }
        onGround = false;
    } else if (onGround) {
        float sp = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        if (sp > 0) {
            float drop = std::max(sp, (kStopSpeed * tune(MV_STOP_SPEED))) * (kFriction * tune(MV_FRICTION)) * dt;
            float ns = std::max(sp - drop, 0.0f) / sp;
            s.vel.x *= ns;
            s.vel.z *= ns;
        }
        accelerate(s.vel, wishDir, wishSpeed, (kGroundAccel * tune(MV_GROUND_ACCEL)), dt);
        s.vel.y = 0;
    } else {
        if (!dashing && !onLadder) {
            airAccelerate(s.vel, wishDir, std::min(wishSpeed, (kAirSpeedCap * tune(MV_AIR_CAP))), (kAirAccel * tune(MV_AIR_ACCEL)) * 10.0f, dt);
            // Air control (CPM style), forward / back only: bend momentum toward where you look without changing
            // speed. Holding a strafe key turns it off, so strafe-and-turn gains speed like in Quake.
            float h = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
            if (wishLen > 0 && cmd.side == 0 && h > 0.5f) {
                float k = std::min(1.0f, (kAirControl * tune(MV_AIR_CONTROL)) * dt);
                float nx = s.vel.x / h + (wishDir.x - s.vel.x / h) * k, nz = s.vel.z / h + (wishDir.z - s.vel.z / h) * k;
                float nl = std::sqrt(nx * nx + nz * nz);
                if (nl > 1e-4f) { s.vel.x = nx / nl * h; s.vel.z = nz / nl * h; }
            }
            s.vel.y -= kGravity * mt.gravity * dt * (hooked ? (kHookGravityScale * tune(MV_HOOK_GRAVITY)) : 1.0f) * ((s.mods & MOVE_LOW_GRAVITY) ? 0.4f : 1.0f);
        }
    }

    Vec3 delta = s.vel * dt;
    MoveState before = s;
    int blocked = slideMove(s, map, delta, crouched);

    // Step up small ledges when walking into them, and in the air too (jumping, falling, hooked, dashing):
    // running into a stair's riser carries you over its lip like walking does, keeping your speed, instead
    // of stopping you dead. Only ledges up to a step's height qualify, so walls still block.
    bool airStep = !onGround;
    if ((onGround || airStep) && (blocked & 0b101)) {
        MoveState step = before;
        AABB b = playerBox(step.pos, crouched);
        float up = map.sweepAxis(b, 1, (kStepHeight * tune(MV_STEP)));
        step.pos.y += up;
        int sb = slideMove(step, map, {delta.x, 0, delta.z}, crouched);
        AABB b2 = playerBox(step.pos, crouched);
        // walking: back down onto the step; in the air: back down, then on with this move's own rise or fall
        float rest = airStep ? delta.y - up : -up;
        float moved = map.sweepAxis(b2, 1, rest);
        step.pos.y += moved;
        float d0 = (s.pos.x - before.pos.x) * (s.pos.x - before.pos.x) + (s.pos.z - before.pos.z) * (s.pos.z - before.pos.z);
        float d1 = (step.pos.x - before.pos.x) * (step.pos.x - before.pos.x) + (step.pos.z - before.pos.z) * (step.pos.z - before.pos.z);
        // take the step if it gets further, or if the raised path was clear (a hair short of the riser, both
        // moves stop at the same spot: refusing would zero your speed and leave you stuck against a 0.5 m step)
        if (d1 > d0 + 1e-6f || (!(sb & 0b101) && d1 >= d0)) { s.pos = step.pos; blocked &= ~0b101; }
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
        float impact = -before.vel.y;
        if (jumpHeld && !frozen && !grappleOnly && impact > jumpSpeed && !onLadder) {
            // bhop off a hard landing: bounce higher than a normal jump (the landing speed is the only input,
            // so prediction replays it exactly)
            s.vel.y = jumpSpeed + std::min((impact - jumpSpeed) * (kBounceGain * tune(MV_BOUNCE_GAIN)), (kBounceMax * tune(MV_BOUNCE_MAX)));
            nowGround = false;
        } else if (s.vel.y < 0) s.vel.y = 0;
    } else if (onGround && s.vel.y <= 0) {
        float down = map.sweepAxis(feet, 1, -(kStepHeight * tune(MV_STEP)));
        if (down > -(kStepHeight * tune(MV_STEP)) + 0.001f) { s.pos.y += down; nowGround = true; s.vel.y = 0; }
    }

    s.flags &= (MF_FROZEN | MF_HOOKED);
    if (nowGround && !(s.flags & MF_HOOKED)) s.flags |= MF_ONGROUND;
    if (crouched) s.flags |= MF_CROUCHED;
    if (jumpHeld) s.flags |= MF_JUMPHELD;
    if (dashHeld) s.flags |= MF_DASHHELD;

    if (s.pos.y < -100.0f) s.pos.y = -100.0f; // kill plane handled by game logic
}

void pmove(MoveState& s, const UserCmd& cmd, const Map& map) {
    int n = cmd.msec < 1 ? 1 : (cmd.msec > 8 ? 8 : cmd.msec);
    for (int i = 0; i < n; i++) pmoveStep(s, cmd, map, kMoveStep);
}
