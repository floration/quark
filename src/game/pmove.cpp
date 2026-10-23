#include "pmove.h"
#include "gamedefs.h"

// Placeholder tuning; movement will be redefined later.
namespace {
// A custom mode's x on each constant (1 normally) is mt.m[MV_...], mt = moveTuning() fetched once per command: it is a
// call into customrules.cpp plus a thread_local lookup, and a step used to fetch it about ten times.
constexpr float kGravity = 22.0f;   // (1.14: +10%, less floaty)
constexpr float kJumpSpeed = 7.34f;  // (with the heavier gravity: still a 1.22 m standing jump, maps rely on it)
constexpr float kGroundSpeed = 9.5f;   // (owner 1.17: 8 -> 9.5; VQ3 / CPMA 10.3, Warfork 9.0, Cocaine Diesel 11.25 for a 1.8 m player)
constexpr float kCrouchSpeed = 4.0f;
constexpr float kGroundAccel = 16.0f;  // (owner 1.17: 16.8 -> 16 with the faster run: still at full speed in ~80 ms)
constexpr float kAirAccel = 2.2f;
constexpr float kAirSpeedCap = 2.3f; // quake-style air strafe cap: a wider window of angles that gain speed (was 2.0); W-only
                                     // strafing uses this, a strafe key held x kStrafeCapAD
constexpr float kStrafeCapAD = 1.1f; // a strafe key held (A / D): the cap x this, 2.3 -> 2.53 (owner 1.18: "increase ad strafing
                                     // limit by 10%"; `cross` below still comes from the 2.3)
constexpr float kStrafeAccelAD = 1.15f; // a strafe key held: air acceleration x this at every speed, on top of the kStrafeBoost
                                        // ramp (owner 1.18: "increase rate of speed increase from ad strafing by 15%")
constexpr float kOverstrafeBrake = 0.35f; // turning past the window: only this much of the push against your momentum applies
constexpr float kAirControl = 4.0f;  // how fast held direction bends momentum in the air (rad/s-ish), speed kept; above
                                     // kAirControlFrom it falls off as 1 / speed, so A/D strafing turns better (owner 1.17)
constexpr float kAirControlFrom = 9.5f;   // m/s: where W-only steering starts losing to A/D (owner 1.17: "9.5m/s instead of
                                          // 12.7 ... by lowering w stafing power not increasing ad strafing"; it was 12.65)
constexpr float kAirControlW = 0.9f;      // W-only air control's turn rate x this at every speed: 3.6 rad/s under 9.5 m/s,
                                          // 0.9 x 38 / speed above (owner 1.18: "reduce w strafing by 10%"; `cross` below
                                          // still comes from the unscaled 4)
constexpr float kAirDriftAccel = 8.0f;  // m/s2: below run speed, held direction also pushes you this hard in the air (owner
                                        // 1.17: a standing jump drifts like a run-up one would, never past run speed)
constexpr float kStrafeBoost = 0.10f;   // a strafe key held: air acceleration x (1 + this), ramping in from 12.65 m/s to
                                        // 1.25x that (15.8), so at speed A/D turns beat W-only (owner 1.17)
constexpr float kFriction = 7.0f;
constexpr float kStopSpeed = 2.0f;
constexpr float kStepHeight = 0.55f;
constexpr float kDashSpeed = 24.0f;     // burst speed
constexpr float kDashTime = 0.12f;      // burst length; then speed settles back
constexpr float kQuickDashCooldown = 1.0f / 3.0f;   // quick dash (MOVE_QUICK_DASH): the dash cooldown x this, 1.2 -> 0.4 s
                                                    // (x a custom mode's own multiplier), the usual 2 charges (owner 1.18:
                                                    // "quick dash should reduce dash cooldown"; 1.17 gave a third charge)
constexpr float kDashExitMin = 11.0f;   // speed you leave a dash with at least
constexpr float kDashKeep = 1.0f;       // ...and your old speed plus this, so dashes build momentum (owner 1.17: was 3)
constexpr float kDashExitMax = 32.0f;   // (1.12: above the grapple's 24, so a dash out of a fast strafe still adds speed)
constexpr float kDashVertical = 0.6375f;  // looking straight up / down makes the dash this vertical (sine)
// Dash jump and super dash (owner, after Celeste's super and hyper): jump during a dash on the floor and the dash
// ends there and then, leaving with its full burst speed plus a jump; dash down into the floor holding jump for more
// speed and a lower hop. Both take the rest of the dash; normal air movement (gravity, strafing) applies from there.
constexpr float kDashJumpWindow = 0.1f;  // input buffer (Celeste ~0.08 s): a jump pressed this long before a dash meets
                                         // the floor still counts, and a dash this soon after jumping off the floor
                                         // (pressing jump a hair before dash) is still a ground dash jump
constexpr float kSuperDashDown = 0.25f;  // a dash at least this far down (share of kDashVertical: looking >= 14.5 deg
                                         // below level) that meets the floor is a super dash; shallower is a dash jump
constexpr float kSuperDashBoost = 1.25f; // super dash: the burst speed x this (Celeste's hyper vs super: 325 / 260)...
constexpr float kSuperDashJump = 0.5f;   // ...with a jump at this share of the jump speed (Celeste: half; a 0.31 m hop
                                         // that skims the floor, bhop on from its landing)
constexpr float kSuperDashMax = 36.0f;   // its cap: above the dash's 32, so a super dash at top dash speed still boosts
// Dash wall bounce (owner): a dash into a wall at under this angle from the wall's normal (fairly head-on)
// bounces off it, reflected, at the dash's full speed; a more glancing hit slides along it as before.
constexpr float kDashBounceAngle = 45.0f; // degrees
constexpr int kDashMaxBounces = 2;        // per dash (a corner can take two; a third wall slides)
constexpr float kBounceGain = 0.5f;     // bhop: landing faster than a jump adds this share of the extra to the jump
constexpr float kBounceMax = 8.0f;
constexpr float kHookAccel = 38.0f;     // pull acceleration toward the anchor (adds to momentum)
constexpr float kHookMaxSpeed = 24.0f;
constexpr float kHookGravityScale = 0.5f;
constexpr float kHookRelease = 1.2f;    // auto-release this close to the anchor
constexpr float kLadderSpeed = 6.75f;   // climbing speed (owner 1.17: 4.5 -> 6.75; Warfork 7.0, Cocaine Diesel 8.4)
constexpr float kLadderSide = 4.5f;     // sideways speed on a ladder (owner 1.17: 3 -> 4.5)
}

AABB playerBox(const Vec3& f, bool crouched, float scale) {
    float h = (crouched ? kPlayerCrouchHeight : kPlayerHeight) * scale, w = kPlayerHalfWidth * scale;
    return {{f.x - w, f.y, f.z - w}, {f.x + w, f.y + h, f.z + w}};
}

AABB headBox(const AABB& b) {
    Vec3 c = b.center();
    float hx = (b.mx.x - b.mn.x) * 0.5f * kHeadWidthFraction, hz = (b.mx.z - b.mn.z) * 0.5f * kHeadWidthFraction;
    return {{c.x - hx, b.mx.y - (b.mx.y - b.mn.y) * kHeadFraction, c.z - hz}, {c.x + hx, b.mx.y, c.z + hz}};
}

bool rayHitsHead(const Vec3& o, const Vec3& dir, const AABB& hitbox) {
    float far = length(hitbox.mx - hitbox.mn) + length(hitbox.center() - o) + 1.0f;   // (anywhere along the line through the box)
    return rayAABB(o, safeInv(dir), headBox(hitbox), far) >= 0;
}

// Air strafing: Quake acceleration, except that the part of the push that would brake you (wish direction turned
// past 90 degrees from your momentum: overstrafing) is softened, so a sloppy turn costs a little, not a lot.
// A strafe key held (side != 0) pushes harder: x kStrafeAccelAD at every speed (owner 1.18), and at speed the
// kStrafeBoost on top, ramping in from `cross` (where air control starts falling off) to 1.25x it, so above that
// A/D strafing turns faster than W-only air control (owner 1.17). (The caller already raised the A/D wish cap.)
static void airAccelerate(const MoveTuning& mt, Vec3& vel, const Vec3& wishDir, float wishSpeed, float accel, float dt, int side, float cross) {
    float cur = dot(vel, wishDir);
    float add = wishSpeed - cur;
    if (add <= 0) return;
    float h = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    if (side != 0) {
        float t = std::clamp((h - cross) / (0.25f * cross), 0.0f, 1.0f);
        accel *= kStrafeAccelAD * (1.0f + (kStrafeBoost * mt.m[MV_STRAFE_BOOST]) * t);
    }
    Vec3 push = wishDir * std::min(accel * wishSpeed * dt, add);
    if (h > 1e-3f) {
        Vec3 along = {vel.x / h, 0, vel.z / h};
        float pa = dot(push, along);
        if (pa < 0) push = push - along * (pa * (1.0f - (kOverstrafeBrake * mt.m[MV_OVERSTRAFE])));
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
    const float scale = moveScale(s.mods);
    for (int i : order) {
        AABB box = playerBox(s.pos, crouched, scale);
        float d = map.sweepAxis(box, i, delta[i]);
        if (std::fabs(d - delta[i]) > 1e-5f) blocked |= 1 << i;
        s.pos[i] += d;
    }
    return blocked;
}

// The parts of a step that only depend on the command (the same for each of its up to 8 steps).
struct CmdCtx { const MoveTuning& mt; Vec3 fwd, right; };

static int maxChargesFor(const MoveTuning& mt, uint8_t) {
    return std::max(1, (int)std::lround(kDashCharges * mt.m[MV_DASH_CHARGES]));   // (quick dash: no extra charge since 1.18)
}
static int cooldownMsFor(const MoveTuning& mt, uint8_t mods) {
    const float quick = (mods & MOVE_QUICK_DASH) ? kQuickDashCooldown : 1.0f;
    return std::min(60000, std::max(0, (int)std::lround(kDashCooldown * mt.m[MV_DASH_COOLDOWN] * quick * 1000.0f)));
}

static void pmoveStep(MoveState& s, const UserCmd& cmd, const Map& map, float dt, HookProbe* hp, const CmdCtx& cx) {
    const MoveTuning& mt = cx.mt; // a custom mode's movement (all 1 / on normally)
    if (s.flags & MF_FROZEN) {
        s.vel = {0, std::min(s.vel.y, 0.0f), 0};
        s.flags &= ~MF_PULLED;
    }
    const float scale = moveScale(s.mods);   // size modifiers scale the hull and the eyes
    bool wantCrouch = false; // crouching was removed from the game (BTN_CROUCH is ignored)
    bool crouched = (s.flags & MF_CROUCHED) != 0;
    if (wantCrouch && !crouched) {
        crouched = true;
    } else if (!wantCrouch && crouched) {
        if (!map.boxSolid(playerBox(s.pos, false, scale))) crouched = false;
    }

    const Vec3 fwd = cx.fwd, right = cx.right;
    Vec3 wish = fwd * (cmd.forward / 127.0f) + right * (cmd.side / 127.0f);
    if ((s.flags & MF_FROZEN) || (s.mods & MOVE_GRAPPLE_ONLY)) wish = {};
    float wishLen = length(wish);
    Vec3 wishDir = wishLen > 0 ? wish / wishLen : Vec3();
    const float jumpSpeed = kJumpSpeed * mt.jump;
    float maxSpeed = (crouched ? (kCrouchSpeed * mt.m[MV_CROUCH_SPEED]) : kGroundSpeed) * mt.speed;
    float wishSpeed = std::min(wishLen, 1.0f) * maxSpeed;

    bool onGround = (s.flags & MF_ONGROUND) != 0;
    const bool grappleOnly = (s.mods & MOVE_GRAPPLE_ONLY) != 0;
    bool jumpHeld = (cmd.buttons & BTN_JUMP) != 0 && !grappleOnly;
    bool frozen = (s.flags & MF_FROZEN) != 0;
    const bool grounded = onGround;          // on the floor as this step starts (the dash jump / super dash need it)
    const bool dashActive = s.dashTimer > 0; // a dash from an earlier step still running
    // A fresh jump press is buffered for the dash techs (the buffer waits while a dash runs, so a press just before
    // or during a dash counts when the dash meets the floor); bunny-hopping (jump held) needs no buffer.
    const bool freshJump = jumpHeld && !(s.flags & MF_JUMPHELD) && !frozen;
    const float jumpWindow = kDashJumpWindow * mt.m[MV_DASH_JUMP_WINDOW];
    if (freshJump) s.jumpBuffer = jumpWindow;
    else if (!dashActive) s.jumpBuffer = std::max(0.0f, s.jumpBuffer - dt);
    s.jumpGrace = std::max(0.0f, s.jumpGrace - dt);
    // Holding jump bunny-hops: jump on the first grounded tick, before friction applies. (Not while a dash runs: a
    // jump on the floor then is the dash's, below.)
    if (onGround && jumpHeld && !frozen && !dashActive) {
        s.vel.y = jumpSpeed;
        onGround = false;
        if (freshJump) s.jumpGrace = jumpWindow;   // (a dash right after this press is still a ground dash jump)
    }

    // Dash: one burst in the movement direction (or forward), spending a charge, once the cooldown since the last
    // dash ENDED has run out (owner 1.17: charges on top of the cooldown). Not while a dash runs (a press then does
    // nothing, and isn't kept). Refills: at the end of this step (the ground all of them, a bhop landing one).
    s.hookCooldown = std::max(0.0f, s.hookCooldown - dt);
    if (!dashActive) {   // (whole ms: the cooldown counts down only while no dash runs)
        const int stepMs = std::max(1, (int)std::lround(dt * 1000.0f));
        s.dashCooldownMs = (uint16_t)std::max(0, (int)s.dashCooldownMs - stepMs);
    }
    const int maxCharges = maxChargesFor(mt, s.mods);
    if (s.dashCharges > maxCharges) s.dashCharges = (uint8_t)maxCharges;
    bool dashHeld = (cmd.buttons & BTN_DASH) != 0 && !grappleOnly && !(s.mods & MOVE_RUN_ONLY) && mt.dash;
    const bool dashStart = dashHeld && !(s.flags & MF_DASHHELD) && s.dashCharges > 0 && s.dashCooldownMs == 0 && !dashActive && !frozen;
    bool chargesSet = false;   // a dash tech set the charges this step: no ground / bhop refill on top
    if (dashStart) {
        s.flags &= ~MF_PULLED;   // a dash breaks free of another player's grapple
        Vec3 d = wishLen > 0 ? wishDir : fwd;
        float h = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        s.dashDirX = d.x;
        s.dashDirZ = d.z;
        s.dashExitSpeed = std::min((kDashExitMax * mt.m[MV_DASH_EXIT_MAX]), std::max((kDashExitMin * mt.m[MV_DASH_EXIT_MIN]), h + (kDashKeep * mt.m[MV_DASH_KEEP])));
        s.dashUp = std::sin(cmd.pitch) * (kDashVertical * mt.m[MV_DASH_VERTICAL]);
        s.dashTimer = (kDashTime * mt.m[MV_DASH_TIME]);
        s.dashBounces = 0;
        s.dashCharges--;
    }
    bool dashing = s.dashTimer > 0;
    if (dashing && !frozen) {
        // Dash jump (Celeste's super): jump while a dash runs on the floor (it started there, or it's sliding along it)
        // and the dash ends at once, leaving with its full burst speed along its direction plus a normal jump; also a
        // dash started just after jumping off the floor (jump pressed a hair first: the jump's rise is kept as it is).
        // Super dash (Celeste's hyper): the same off a dash aimed down (kSuperDashDown) into the floor, or meeting it
        // during the dash: faster than the burst, and a lower hop. The jump replaces the bhop: no landing bounce
        // (the ground check doesn't bounce off a dash) and no bhop jump on top.
        const float burst = std::max((kDashSpeed * mt.m[MV_DASH_SPEED]), s.dashExitSpeed);
        const bool down = s.dashUp <= -kSuperDashDown * (kDashVertical * mt.m[MV_DASH_VERTICAL]);
        const bool wantJump = jumpHeld || s.jumpBuffer > 0;
        const bool graceJump = dashStart && !grounded && s.jumpGrace > 0 && !down;
        if ((grounded && wantJump) || graceJump) {
            float sp = burst, vy = graceJump ? s.vel.y : jumpSpeed;
            // charges (owner): a dash jump leaves you all of them (it's off the ground), a super dash exactly one
            s.dashCharges = (uint8_t)maxCharges;
            if (grounded && down) {
                sp = std::max(burst, std::min(kSuperDashMax * mt.m[MV_SUPER_DASH_MAX], burst * (kSuperDashBoost * mt.m[MV_SUPER_DASH_BOOST])));
                vy = jumpSpeed * (kSuperDashJump * mt.m[MV_SUPER_DASH_JUMP]);
                s.dashCharges = 1;
            }
            chargesSet = true;
            s.vel = {s.dashDirX * sp, vy, s.dashDirZ * sp};
            s.dashTimer = 0;
            s.jumpBuffer = 0;
            s.jumpGrace = 0;
            dashing = false;
            onGround = false;
        }
    }
    if (dashing) {
        // Short, strong burst, tilted a little by where you look: no gravity, no steering.
        float hs = std::sqrt(std::max(0.0f, 1.0f - s.dashUp * s.dashUp)); // aiming up / down trades horizontal for vertical
        float burst = std::max((kDashSpeed * mt.m[MV_DASH_SPEED]), s.dashExitSpeed); // (never slower than what you leave it with)
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
        float eyeH = (crouched ? kCrouchEyeHeight : kEyeHeight) * scale;
        Vec3 eye = s.pos + Vec3(0, eyeH, 0);
        const Vec3 dir = angleForward(cmd.yaw, cmd.pitch);
        const float range = kGrappleRange * mt.m[MV_HOOK_RANGE];
        float t = map.raycast(eye, dir, range);
        int caught = -1;   // a player in front of the wall (or in the open) catches it instead
        if (hp && hp->n > 0) {
            float best = t >= 0 ? t : range;
            Vec3 inv = safeInv(dir);
            for (int k = 0; k < hp->n && k < HookProbe::kMax; k++) {
                float tk = rayAABB(eye, inv, hp->box[k], best);
                if (tk >= 0 && tk < best) { best = tk; caught = k; }
            }
        }
        if (caught >= 0) {
            hp->hit = caught;
            hp->head = rayHitsHead(eye, dir, hp->box[caught]);
            s.hookPoint = hp->box[caught].center();   // (the server moves it with them from here on)
            s.hookTarget = (uint8_t)(hp->id[caught] + 1);
            s.flags |= MF_HOOKED | MF_HOOKHELD;
            s.flags &= ~MF_PULLED;   // grabbing with your own grapple breaks free of someone else's
        } else if (t >= 0) {
            s.hookPoint = eye + dir * t;
            s.hookTarget = 0;
            s.flags |= MF_HOOKED | MF_HOOKHELD;
            s.flags &= ~MF_PULLED;
        }
    }
    if (!hookHeld || frozen) s.flags &= ~MF_HOOKED;
    bool hooked = (s.flags & MF_HOOKED) != 0;
    if (hooked) {
        Vec3 to = s.hookPoint - (s.pos + Vec3(0, 1.0f * scale, 0));
        float dist = length(to);
        if (dist < (kHookRelease * mt.m[MV_HOOK_RELEASE])) {
            s.flags &= ~MF_HOOKED;
            hooked = false;
        } else if (!dashing) {
            // The pull adds speed up to the grapple's top speed; it never brakes you (owner 5.4): faster than that
            // already, it only bends your path toward the anchor and keeps your speed.
            float before = length(s.vel);
            s.vel += to / dist * ((kHookAccel * mt.m[MV_HOOK_ACCEL]) * dt);
            float sp = length(s.vel), cap = std::max(kHookMaxSpeed * mt.m[MV_HOOK_MAX], before);
            if (sp > cap) s.vel *= cap / sp;
            onGround = false;
        }
    }
    if (wasHooked && !(s.flags & MF_HOOKED)) s.hookCooldown = (kHookCooldown * mt.m[MV_HOOK_COOLDOWN]);
    if (!(s.flags & MF_HOOKED)) s.hookTarget = 0;
    // Held by another player's grapple: pulled toward them like a hook pulls its owner (the same acceleration and
    // top speed, momentum kept, half gravity). The server moves pullPoint with them and ends it; dashing or grabbing
    // with your own grapple (above) breaks free.
    bool pulled = (s.flags & MF_PULLED) != 0;
    if (pulled && !dashing) {
        Vec3 to = s.pullPoint - (s.pos + Vec3(0, 1.0f * scale, 0));
        float dist = length(to);
        if (dist >= (kHookRelease * mt.m[MV_HOOK_RELEASE])) {
            float before = length(s.vel);
            s.vel += to / dist * ((kHookAccel * mt.m[MV_HOOK_ACCEL]) * dt);
            float sp = length(s.vel), cap = std::max(kHookMaxSpeed * mt.m[MV_HOOK_MAX], before);
            if (sp > cap) s.vel *= cap / sp;
            onGround = false;
        }
    }

    // Ladders: inside a ladder volume, moving climbs (look down to climb down), no input holds you there,
    // a fresh jump kicks you off backwards. No gravity while on it.
    bool onLadder = !hooked && !pulled && !dashing && !frozen && map.onLadder(playerBox(s.pos, crouched, scale));
    if (onLadder) {
        bool jumpPress = jumpHeld && !(s.flags & MF_JUMPHELD);
        if (jumpPress) {
            s.vel = fwd * -4.0f + Vec3(0, jumpSpeed * 0.7f, 0);
        } else {
            float climb = std::min(wishLen, 1.0f) * (kLadderSpeed * mt.m[MV_LADDER_SPEED]) * (cmd.pitch < -0.45f ? -1.0f : 1.0f);
            s.vel = {wishDir.x * std::min(wishSpeed, (kLadderSide * mt.m[MV_LADDER_SIDE])), wishLen > 0 ? climb : 0.0f, wishDir.z * std::min(wishSpeed, (kLadderSide * mt.m[MV_LADDER_SIDE]))};
        }
        onGround = false;
    } else if (onGround) {
        float sp = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        if (sp > 0) {
            float drop = std::max(sp, (kStopSpeed * mt.m[MV_STOP_SPEED])) * (kFriction * mt.m[MV_FRICTION]) * dt;
            float ns = std::max(sp - drop, 0.0f) / sp;
            s.vel.x *= ns;
            s.vel.z *= ns;
        }
        accelerate(s.vel, wishDir, wishSpeed, (kGroundAccel * mt.m[MV_GROUND_ACCEL]), dt);
        s.vel.y = 0;
    } else {
        if (!dashing && !onLadder) {
            const float airAccel = (kAirAccel * mt.m[MV_AIR_ACCEL]) * 10.0f, airCap = kAirSpeedCap * mt.m[MV_AIR_CAP];
            const float airControl = kAirControl * mt.m[MV_AIR_CONTROL];
            // 22 x 2.3 / 4 = 12.65 m/s: where the A/D boost ramp starts (the base cap and the unscaled air control, so
            // the 1.18 A/D cap and the slower W-only turn leave it where it was)
            const float cross = airAccel * airCap / airControl;
            const float strafeCap = cmd.side != 0 ? airCap * kStrafeCapAD : airCap;   // A/D: 2.53, W-only: 2.3
            airAccelerate(mt, s.vel, wishDir, std::min(wishSpeed, strafeCap), airAccel, dt, cmd.side, cross);
            // Drift: below run speed, held direction pushes you along it (kAirDriftAccel) up to run speed and never
            // past it, so a standing jump moves about like the run-up one would (owner 1.17; it was 2.3 m/s at most).
            const float runSpeed = kGroundSpeed * mt.speed;
            float h = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
            if (wishLen > 0 && h < runSpeed) {
                float d = s.vel.x * wishDir.x + s.vel.z * wishDir.z;
                float a = std::min((kAirDriftAccel * mt.m[MV_AIR_DRIFT]) * std::min(wishLen, 1.0f) * dt, runSpeed - d);
                // (the push that would bring the horizontal speed exactly to run speed: |v + a w| = runSpeed)
                a = std::min(a, -d + std::sqrt(std::max(0.0f, d * d + runSpeed * runSpeed - h * h)));
                if (a > 0) { s.vel.x += wishDir.x * a; s.vel.z += wishDir.z * a; }
                h = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
            }
            // Air control (CPM style), forward / back only: bend momentum toward where you look without changing
            // speed. Holding a strafe key turns it off, so strafe-and-turn gains speed like in Quake. Its rate is capped
            // so that above kAirControlFrom (9.5 m/s) W-only bends slower, as 1 / speed, and A/D strafing is the faster
            // way to turn at speed (owner 1.17). Only W-only is weakened: the A/D boost still ramps in from `cross`.
            // 1.18 (owner): the whole rate x kAirControlW (0.9), at every speed.
            if (wishLen > 0 && cmd.side == 0 && h > 0.5f) {
                float rate = kAirControlW * std::min(airControl, airControl * kAirControlFrom * mt.speed / std::max(h, 1e-3f));
                float k = std::min(1.0f, rate * dt);
                float nx = s.vel.x / h + (wishDir.x - s.vel.x / h) * k, nz = s.vel.z / h + (wishDir.z - s.vel.z / h) * k;
                float nl = std::sqrt(nx * nx + nz * nz);
                if (nl > 1e-4f) { s.vel.x = nx / nl * h; s.vel.z = nz / nl * h; }
            }
            s.vel.y -= kGravity * mt.gravity * dt * ((hooked || pulled) ? (kHookGravityScale * mt.m[MV_HOOK_GRAVITY]) : 1.0f) * ((s.mods & MOVE_LOW_GRAVITY) ? 0.4f : 1.0f);
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
        AABB b = playerBox(step.pos, crouched, scale);
        float up = map.sweepAxis(b, 1, (kStepHeight * mt.m[MV_STEP]));
        step.pos.y += up;
        int sb = slideMove(step, map, {delta.x, 0, delta.z}, crouched);
        AABB b2 = playerBox(step.pos, crouched, scale);
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
    // Dash wall bounce: a dash (this step's, including its last) that runs into a wall under kDashBounceAngle from
    // the wall's normal reflects off it (d' = d - 2(d.n)n: walls are axis-aligned, so one horizontal component
    // flips; the vertical part is kept) and goes on at the dash's burst speed: the rest of the dash runs in the new
    // direction, and it leaves at the burst speed however little was left (so every bounce carries). A dash jump
    // or super dash after it keeps that.
    if (dashing && (blocked & 0b101) && s.dashBounces < kDashMaxBounces) {
        const float c = std::cos(kDashBounceAngle * mt.m[MV_DASH_BOUNCE_ANGLE] * (PI / 180.0f));
        const bool bx = (blocked & 1) && std::fabs(s.dashDirX) > c, bz = !bx && (blocked & 4) && std::fabs(s.dashDirZ) > c;
        if (bx || bz) {
            if (bx) { s.dashDirX = -s.dashDirX; blocked &= ~1; }
            else { s.dashDirZ = -s.dashDirZ; blocked &= ~4; }
            s.dashBounces++;
            s.dashCharges = 1;   // (owner: a wall bounce leaves you exactly one charge)
            chargesSet = true;
            const float burst = std::max((kDashSpeed * mt.m[MV_DASH_SPEED]), s.dashExitSpeed);
            const float hs = std::sqrt(std::max(0.0f, 1.0f - s.dashUp * s.dashUp));
            s.dashExitSpeed = burst;
            s.vel.x = s.dashDirX * burst * hs;
            s.vel.z = s.dashDirZ * burst * hs;
        }
    }
    if (blocked & 1) s.vel.x = 0;
    if (blocked & 4) s.vel.z = 0;
    if (blocked & 2) {
        if (s.vel.y > 0) s.vel.y = 0;
    }

    // Ground check: probe slightly below.
    AABB feet = playerBox(s.pos, crouched, scale);
    const bool canLand = s.vel.y <= 0.01f;   // (rising: no landing whatever the probe says, so don't sweep for it)
    float probe = canLand ? map.sweepAxis(feet, 1, -0.05f) : 0.0f;
    bool nowGround = canLand && probe > -0.049f;
    bool bounced = false;
    if (nowGround) {
        s.pos.y += probe; // settle onto the floor
        float impact = -before.vel.y;
        if (jumpHeld && !frozen && !grappleOnly && impact > jumpSpeed && !onLadder && !dashing) {
            // bhop off a hard landing: bounce higher than a normal jump (the landing speed is the only input,
            // so prediction replays it exactly). Not off a dash's own push into the floor: that's a super dash
            // (next step, from the floor) or nothing.
            s.vel.y = jumpSpeed + std::min((impact - jumpSpeed) * (kBounceGain * mt.m[MV_BOUNCE_GAIN]), (kBounceMax * mt.m[MV_BOUNCE_MAX]));
            nowGround = false;
            bounced = true;
        } else if (s.vel.y < 0) s.vel.y = 0;
    } else if (onGround && s.vel.y <= 0) {
        float down = map.sweepAxis(feet, 1, -(kStepHeight * mt.m[MV_STEP]));
        if (down > -(kStepHeight * mt.m[MV_STEP]) + 0.001f) { s.pos.y += down; nowGround = true; s.vel.y = 0; }
    }

    // the dash cooldown starts when a dash ends (its timer ran out, or a dash jump / super dash ended it), this step
    if ((dashActive || dashStart) && s.dashTimer <= 0) s.dashCooldownMs = (uint16_t)cooldownMsFor(mt, s.mods);

    s.flags &= (MF_FROZEN | MF_HOOKED | MF_PULLED);
    if (nowGround && !(s.flags & (MF_HOOKED | MF_PULLED))) s.flags |= MF_ONGROUND;
    // Dash charges back (owner 1.17): on the floor at the end of a step, all of them; while holding jump (a bhop
    // landing: the next step leaves the floor again, or this one bounced off it) only up to ONE ("when holding space you
    // should only be able to charge 1 dash"): hopping never builds the second charge back. Not during a dash's own
    // steps (its push into the floor is no landing: a super dash would refill itself), not on a ladder or off a grapple.
    if (!chargesSet && !dashing && !onLadder) {
        const bool hopping = jumpHeld && !frozen && !grappleOnly;
        if (bounced || (hopping && (s.flags & MF_ONGROUND))) s.dashCharges = (uint8_t)std::max<int>(s.dashCharges, std::min(maxCharges, 1));
        else if (s.flags & MF_ONGROUND) s.dashCharges = (uint8_t)maxCharges;
    }
    if (crouched) s.flags |= MF_CROUCHED;
    if (jumpHeld) s.flags |= MF_JUMPHELD;
    if (dashHeld) s.flags |= MF_DASHHELD;

    if (s.pos.y < -100.0f) s.pos.y = -100.0f; // kill plane handled by game logic
}

void pmove(MoveState& s, const UserCmd& cmd, const Map& map, HookProbe* probe) {
    static_assert(HookProbe::kMax >= kMaxPlayers, "a probe holds every player");
    int n = cmd.msec < 1 ? 1 : (cmd.msec > 8 ? 8 : cmd.msec);
    const float sy = std::sin(cmd.yaw), cy = std::cos(cmd.yaw);
    const CmdCtx cx{moveTuning(), {-sy, 0, -cy}, {cy, 0, -sy}};
    for (int i = 0; i < n; i++) pmoveStep(s, cmd, map, kMoveStep, probe, cx);
}

float moveParamBase(int i) {
    static const float v[MV_COUNT] = {kCrouchSpeed, kGroundAccel, kFriction, kStopSpeed, kAirAccel, kAirSpeedCap, kOverstrafeBrake, kAirControl,
        kStepHeight, kDashSpeed, kDashTime, kDashExitMin, kDashKeep, kDashExitMax, kDashVertical, (float)kDashCharges, kDashCooldown,
        kBounceGain, kBounceMax, kGrappleRange, kHookAccel, kHookMaxSpeed, kHookGravityScale, kHookRelease, kHookCooldown,
        kLadderSpeed, kLadderSide, kDashJumpWindow, kSuperDashBoost, kSuperDashJump, kSuperDashMax, kDashBounceAngle,
        kAirDriftAccel, kStrafeBoost};
    return i >= 0 && i < MV_COUNT ? v[i] : 1.0f;
}
const char* moveParamUnit(int i) {
    static const char* u[MV_COUNT] = {"m/s", "", "", "m/s", "", "m/s", "", "", "m", "m/s", "s", "m/s", "m/s", "m/s", "", "", "s",
        "", "m/s", "m", "m/s2", "m/s", "x", "m", "s", "m/s", "m/s", "s", "x", "x", "m/s", "deg", "m/s2", ""};
    return i >= 0 && i < MV_COUNT ? u[i] : "";
}
int dashMaxCharges(uint8_t mods) { return maxChargesFor(moveTuning(), mods); }
int dashCooldownMs(uint8_t mods) { return cooldownMsFor(moveTuning(), mods); }
float moveBaseGravity() { return kGravity; }
float moveBaseSpeed() { return kGroundSpeed; }
float moveBaseJump() { return kJumpSpeed; }
