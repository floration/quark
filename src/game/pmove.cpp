#include "pmove.h"
#include "gamedefs.h"
#include "moveexpr.h"

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
constexpr float kQuickDashCooldown = 1.0f / 3.0f;   // quick dash (MOVE_QUICK_DASH): the dash cooldown and recharge x this,
                                                    // 0.6 -> 0.2 s and 1.2 -> 0.4 s (x a custom mode's own multiplier), the
                                                    // usual 2 charges (owner 1.18: "quick dash should reduce dash cooldown";
                                                    // 1.17 gave a third charge)
constexpr float kDashExitMin = 11.0f;   // speed you leave a dash with at least
constexpr float kDashKeep = 1.0f;       // ...and your old speed plus this, so dashes build momentum (owner 1.17: was 3)
constexpr float kDashExitMax = 32.0f;   // (1.12: above the grapple's 24, so a dash out of a fast strafe still adds speed)
constexpr float kDashAirGap = 0.35f;    // a dash in the air leaves with this share of the gap between your speed and
                                        // kDashExitMax added (owner, 2026-10-28: "if you dash at 10 m/s it should let you
                                        // keep a lot more than if you dash at 25"): 0 -> 11.2, 10 -> 17.7, 25 -> 27.5,
                                        // 31 -> 31.35 (then kDashKeep's +1 is more); on the floor only kDashKeep, as before.
                                        // A custom mode's dash keep multiplier scales it too
constexpr float kRampAirAccel = 2.0f;   // surf, touching a ramp's face: x air acceleration (owner 2026-10-28: "make it like 2x base", 8 was "insane")
constexpr float kIceFriction = 0.08f;    // standing on ice: ground friction x this (owner, 2026-10-28: "ice is slippery")...
constexpr float kIceDash = 0.25f;        // a dash off ice: this share of what it does elsewhere (owner, 2026-10-28)
constexpr float kIceAccel = 0.2f;        // ...and ground acceleration x this (you push off it as badly as you stop on it)
constexpr float kRubberBounce = 0.85f;   // hitting rubber (floor, wall or ceiling) faster than kRubberMin: you leave it at this
constexpr float kRubberMin = 3.0f;       // share of the speed you hit it with, back the way you came (owner: "extra bouncy")
constexpr float kRubberGrip = 1.6f;      // standing on rubber: ground friction and acceleration x this (owner: "more acceleration
                                         // for ground movement and quicker breaking")
constexpr float kGlassHookToward = 1.0f;   // grappling glass while moving toward it faster than this (m/s) latches on
constexpr float kAsphaltSpeed = 1.15f;   // on asphalt (owner, 2026-10-28: "increase top ground speed, increase accel and breaking"):
constexpr float kAsphaltGrip = 1.4f;     // run speed x this, ground friction and acceleration x this
constexpr float kMudSpeed = 0.6f;        // in mud (owner: "sticky"): run speed x this, ground friction x kMudGrip (you stop
constexpr float kMudGrip = 2.5f;         // dead), jumps x kMudJump
constexpr float kMudJump = 0.75f;
constexpr float kSlopeGravityBack = 0.5f;   // riding a ramp's face: this share of a step's gravity is given back (owner 2026-10-28)
constexpr float kDashVerticalUp = 0.5f;    // looking straight up makes the dash this vertical (the sine: 50% of its speed
                                           // goes up; owner 1.18.31: "40% up and 90% down", it was 0.6375 both ways;
                                           // owner 2026-10-28: 40% -> 50% up)...
constexpr float kDashVerticalDown = 0.9f;  // ...and looking straight down this (90% down); a custom mode's dash vertical
                                           // multiplier scales both
// Dash jump and super dash (owner, after Celeste's super and hyper): jump during a dash on the floor and the dash
// ends there and then, leaving with its full burst speed plus a jump; dash down into the floor holding jump for more
// speed and a hop that is higher the further down you look. Both take the rest of the dash; normal air movement
// (gravity, strafing) applies from there.
constexpr float kDashJumpWindow = 0.1f;  // input buffer (Celeste ~0.08 s): a jump pressed this long before a dash meets
                                         // the floor still counts, and a dash this soon after jumping off the floor
                                         // (pressing jump a hair before dash) is still a ground dash jump
constexpr float kSuperDashDown = 0.25f;  // a dash at least this far down (share of kDashVerticalDown: looking >= 14.5 deg
                                         // below level) that meets the floor is a super dash; shallower is a dash jump
constexpr float kSuperDashBoost = 5.0f;  // super dash (the hyper): the horizontal speed you carried into the dash + this, m/s
                                         // (owner 1.18.7: "+5 of current"; it was the burst speed x 1.25, 30 m/s from a
                                         // standstill), never less than the same dash would leave with on its own...
constexpr float kSuperDashJump = 0.9f;   // ...sent off the floor like the dash bounced: the dash's own down share (the
                                         // sine of its angle below level) of that speed goes up, the rest stays level,
                                         // at most this share up (owner 1.18.31: "when you hyper you can use up to 90% of
                                         // the speed on vertical"; it was a hop of 0.866 -> 1.732 jumps, 1.18.28). A
                                         // straight-down dash (0.9) leaves 90% up: from a 9.5 m/s run, 14.5 m/s, 13.05
                                         // up (3.87 m) and 6.3 level; just past the threshold (0.225) 3.26 up (0.24 m).
                                         // A custom mode's super dash jump multiplier scales the share (still <= 0.99)
constexpr float kSuperDashMax = 36.0f;   // its cap: above the dash's 32, so a super dash at top dash speed still boosts (the
                                         // dash's limit is intended, owner decision 5.4: the cap keeps chained hypers from
                                         // adding 5 each without end)
// Dash wall bounce (owner): a dash into a wall at under this angle from the wall's normal (fairly head-on)
// bounces off it, reflected, at kDashBounceSpeed; a more glancing hit slides along it as before.
constexpr float kDashBounceSpeed = 16.0f; // the bounce's speed (x a custom mode's dash speed): the rest of the dash runs at it
                                          // and leaves with it (owner 1.18.7: "16"; it was the dash's burst, 24 or more)
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

// A slope's face that `box` touches, if it does (n its outward normal): the box's corner deepest toward the face's
// plane lies on it, within a sweep's gap (a corner: the face of the two that the box is outside of).
static bool slopeContact(const Brush* b, const AABB& box, Vec3& n) {
    if (!b || b->shape == BRUSH_BOX) return false;
    Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
    const int np = slopePlanes(*b, pn, pd);
    float best = -1e30f;
    for (int k = 0; k < np; k++) {
        const Vec3 c = {pn[k].x > 0 ? box.mn.x : box.mx.x, pn[k].y > 0 ? box.mn.y : box.mx.y, pn[k].z > 0 ? box.mn.z : box.mx.z};
        const float dist = dot(pn[k], c) - pd[k];
        if (dist > best) { best = dist; n = pn[k]; }
    }
    return std::fabs(best) < 0.01f;
}
// A face up to 45 degrees from flat is a floor (you walk and stand on it); a steeper one, one on its side or one
// facing down is a wall: you can't walk or step up it, and you slide down it.
static bool floorFace(const Vec3& n) { return n.y >= 0.70710678f - 1e-4f; }
static bool steepUnder(const Brush* b, const AABB& feet) { Vec3 n; return slopeContact(b, feet, n) && !floorFace(n); }
static AABB shifted(AABB b, int axis, float d) { b.mn[axis] += d; b.mx[axis] += d; return b; }
// Touching a ramp: a slope's wall face that faces up, within 3 cm of the box (probed down and to the four sides with
// the sweep, before this step moves), so the air move can know it (surf's ramp air acceleration)
static bool onRampFace(const Map& map, const AABB& box) {
    static const int ax[5] = {1, 0, 0, 2, 2};
    static const float dd[5] = {-0.03f, -0.03f, 0.03f, -0.03f, 0.03f};
    for (int k = 0; k < 5; k++) {
        const Brush* hit = nullptr;
        const float d = map.sweepAxis(box, ax[k], dd[k], &hit);
        Vec3 n;
        if (std::fabs(d - dd[k]) > 1e-5f && slopeContact(hit, shifted(box, ax[k], d), n) && !floorFace(n) && n.y > 0) return true;
    }
    return false;
}

// Moves the hull through the world one axis at a time. Returns which axes were blocked. A slope's wall face (steep,
// on its side, or facing down) doesn't block: what it stops of the move goes on along it, on the axes still to come
// (*wall: its outward normal, zero for none). Falling onto a steep face slides you off it: what it stops of the fall
// goes sideways, downhill.
static int slideMove(MoveState& s, const Map& map, Vec3 delta, bool crouched, Vec3* wall = nullptr) {
    int blocked = 0;
    const int order[3] = {1, 0, 2};
    const float scale = moveScale(s.mods);
    if (wall) *wall = Vec3();
    for (int o = 0; o < 3; o++) {
        const int i = order[o];
        AABB box = playerBox(s.pos, crouched, scale);
        const Brush* hit = nullptr;
        // dashing goes through glass (it breaks: the server, from s.glassBrush)
        int pane = -1;
        float d = map.sweepAxis(box, i, delta[i], &hit, s.dashTimer > 0 && !map.glass.empty() ? &pane : nullptr);
        if (pane >= 0 && s.glassBrush < 0) {
            const float hs = std::sqrt(std::max(0.0f, 1.0f - s.dashUp * s.dashUp));
            s.glassBrush = pane; s.glassHow = GLASS_DASH;
            s.glassAt = box.center(); s.glassDir = {s.dashDirX * hs, s.dashUp, s.dashDirZ * hs};
        }
        Vec3 n;
        if (std::fabs(d - delta[i]) > 1e-5f) {
            if (slopeContact(hit, shifted(box, i, d), n) && !floorFace(n)) {
                if (wall) *wall = n;
                const float rest = delta[i] - d;   // (what it stopped)
                if (i == 1 && rest < 0 && n.y > 0) {   // a fall: on downhill, as far sideways as the face drops by it
                    const float l = std::sqrt(n.x * n.x + n.z * n.z);
                    delta.x -= n.x / l * rest * n.y / l; delta.z -= n.z / l * rest * n.y / l;
                } else {   // along the face: what of it doesn't go into it, on the axes still to come
                    Vec3 r; r[i] = rest;
                    r -= n * dot(n, r);
                    for (int k = o + 1; k < 3; k++) delta[order[k]] += r[order[k]];
                }
            } else blocked |= 1 << i;
        }
        s.pos[i] += d;
    }
    return blocked;
}

// The parts of a step that only depend on the command (the same for each of its up to 8 steps).
struct CmdCtx { const MoveTuning& mt; Vec3 fwd, right; };

static int maxChargesFor(const MoveTuning& mt, uint8_t) {   // (quick dash: no extra charge since 1.18)
    return std::min(kMaxDashCharges, std::max(1, (int)std::lround(kDashCharges * mt.m[MV_DASH_CHARGES])));
}
static int cooldownMsFor(const MoveTuning& mt, uint8_t mods) {
    const float quick = (mods & MOVE_QUICK_DASH) ? kQuickDashCooldown : 1.0f;
    return std::min(60000, std::max(0, (int)std::lround(kDashCooldown * mt.m[MV_DASH_COOLDOWN] * quick * 1000.0f)));
}
static int rechargeMsFor(const MoveTuning& mt, uint8_t mods) {   // (at least 1 ms: the dash's own timer is always there to hold)
    const float quick = (mods & MOVE_QUICK_DASH) ? kQuickDashCooldown : 1.0f;
    return std::min(60000, std::max(1, (int)std::lround(kDashRecharge * mt.m[MV_DASH_COOLDOWN] * quick * 1000.0f)));
}
// The speed a running dash moves at: its burst (never slower than what you leave it with), or after a wall bounce the
// bounce's speed (kDashBounceSpeed, kept in dashExitSpeed).
static float dashBurst(const MoveState& s, const MoveTuning& mt) {
    return s.dashBounces > 0 ? s.dashExitSpeed : std::max(kDashSpeed * mt.m[MV_DASH_SPEED], s.dashExitSpeed);
}
// Charges back (a refill): up to `target`, from the spent charges that have recharged (none still recharging).
static void refillCharges(MoveState& s, int maxCharges, int target) {
    const int ready = maxCharges - (int)s.dashCharges - (int)s.dashRecharging;
    const int add = std::min(std::min(target, maxCharges) - (int)s.dashCharges, ready);
    if (add > 0) s.dashCharges = (uint8_t)(s.dashCharges + add);
}
// A dash tech that leaves exactly `n` (a super dash, a wall bounce): fewer if you hold more (those count as recharged:
// a refill gives them back), else a refill up to it.
static void setCharges(MoveState& s, int maxCharges, int n) {
    if (s.dashCharges > n) s.dashCharges = (uint8_t)n;
    else refillCharges(s, maxCharges, n);
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
    bool onGround = (s.flags & MF_ONGROUND) != 0;
    // what you stand on, asked only on a map with a floor material that moves you differently (ice, rubber, asphalt, mud)
    int floorMat = -1;
    if (onGround && map.hasGripMats) {
        const Brush* fb = nullptr;
        map.sweepAxis(playerBox(s.pos, crouched, scale), 1, -0.05f, &fb);
        if (fb) floorMat = fb->material;
    }
    // mud (owner, 2026-10-28: "sticky"): slow to run through and to jump out of; asphalt: a faster run
    const float jumpSpeed = kJumpSpeed * mt.jump * (floorMat == kMatMud ? kMudJump : 1.0f);
    float maxSpeed = (crouched ? (kCrouchSpeed * mt.m[MV_CROUCH_SPEED]) : kGroundSpeed) * mt.speed
                   * (floorMat == kMatAsphalt ? kAsphaltSpeed : floorMat == kMatMud ? kMudSpeed : 1.0f);
    float wishSpeed = std::min(wishLen, 1.0f) * maxSpeed;

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
    // A jump tapped up to the buffer's 0.1 s before landing (let go again by then) jumps there too (owner, T1): a tap a
    // hair early used to do nothing, and each 10 ms late cost ~1.4 m/s against holding jump.
    const bool tapped = !jumpHeld && s.jumpBuffer > 0 && !frozen && !grappleOnly;
    if (onGround && (jumpHeld || tapped) && !frozen && !dashActive) {
        s.vel.y = jumpSpeed;
        onGround = false;
        s.events |= ME_JUMP;
        s.jumpGrace = jumpWindow;   // (a dash right after this jump is still a ground dash jump; T4: held-jump hops too,
                                    // they had only the ~6 ms of floor contact against a fresh press's 0.1 s)
        if (tapped) s.jumpBuffer = 0;              // (that press is used)
    }

    // Dash: one burst in the movement direction (or forward), spending a charge, once the cooldown since the last
    // dash ENDED has run out (owner 1.17: charges on top of the cooldown; 1.18.7: 0.6 s), while dash is held (T2). The charge it spends recharges for kDashRecharge from the dash's end, and only
    // then can a refill give it back: at the end of a step (the ground all of them, a bhop landing one) or a dash tech.
    s.hookCooldown = std::max(0.0f, s.hookCooldown - dt);
    const int stepMs = std::max(1, (int)std::lround(dt * 1000.0f));
    if (!dashActive)   // (whole ms: the cooldown counts down only while no dash runs)
        s.dashCooldownMs = (uint16_t)std::max(0, (int)s.dashCooldownMs - stepMs);
    {   // the spent charges' recharges count down too, whole ms, all but the newest while a dash runs (the one it spent:
        // its recharge starts when it ends); one that is done leaves the list (a refill can give it back now)
        const int n = std::min<int>(s.dashRecharging, kMaxDashCharges);
        int k = 0;
        for (int i = 0; i < n; i++) {
            int v = s.dashRechargeMs[i];
            if (!(dashActive && i == n - 1)) v = std::max(0, v - stepMs);
            if (v > 0) s.dashRechargeMs[k++] = (uint16_t)v;
        }
        for (int i = k; i < kMaxDashCharges; i++) s.dashRechargeMs[i] = 0;
        s.dashRecharging = (uint8_t)k;
    }
    // fewer charges now (a custom mode's dash_charges equation fell): the charges held above it go, but the ones
    // recharging keep counting down (so a max that rises again gives them back on their own timer, not at once; a refill
    // gives back only what fits: refillCharges). With a fixed max this never happens (charges + recharging <= max).
    const int maxCharges = maxChargesFor(mt, s.mods);
    if (s.dashCharges > maxCharges) s.dashCharges = (uint8_t)maxCharges;
    bool dashHeld = (cmd.buttons & BTN_DASH) != 0 && !grappleOnly && !(s.mods & MOVE_RUN_ONLY) && mt.dash;
    // (owner, T2: held dash fires the moment a dash is allowed, like the held grapple: holding it chains the charges 0.6 s
    // apart; it was a fresh press only, and a press while not ready was lost. A press up to 0.1 s early: the client holds
    // the button that long, so this stays the only rule)
    const bool dashStart = dashHeld && s.dashCharges > 0 && s.dashCooldownMs == 0 && !dashActive && !frozen;
    bool chargesSet = false;   // a dash tech set the charges this step: no ground / bhop refill on top
    if (dashStart) {
        s.flags &= ~MF_PULLED;   // a dash breaks free of another player's grapple
        Vec3 d = wishLen > 0 ? wishDir : fwd;
        float h = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        s.dashDirX = d.x;
        s.dashDirZ = d.z;
        // T3: a dash that starts on the floor counts as on it for the jump window (one aimed a hair up leaves it at once:
        // at +0.5 deg a jump made a plain 11 m/s dash, not the 24 m/s dash jump). jumpGrace is on the wire: no new state.
        if (grounded) s.jumpGrace = jumpWindow;
        s.dashFromSpeed = h;   // (a super dash adds to this)
        const float exitMax = kDashExitMax * mt.m[MV_DASH_EXIT_MAX];
        float keep = kDashKeep * mt.m[MV_DASH_KEEP];   // (in the air: more the further below the top you dash)
        if (!grounded) keep = std::max(keep, kDashAirGap * mt.m[MV_DASH_KEEP] * (exitMax - h));
        s.dashExitSpeed = std::min(exitMax, std::max((kDashExitMin * mt.m[MV_DASH_EXIT_MIN]), h + keep));
        s.dashUp = std::sin(cmd.pitch) * ((cmd.pitch > 0 ? kDashVerticalUp : kDashVerticalDown) * mt.m[MV_DASH_VERTICAL]);
        s.dashTimer = (kDashTime * mt.m[MV_DASH_TIME]);
        s.dashBounces = 0;
        s.dashCharges--;
        s.events |= ME_DASH;
        if (s.dashRecharging < kMaxDashCharges) s.dashRechargeMs[s.dashRecharging++] = (uint16_t)rechargeMsFor(mt, s.mods);   // (held there until the dash ends)
        if (grounded && floorMat == kMatIce) {
            // on ice (owner, 2026-10-28: "it should only work 25% because its slippery"): no burst, only a quarter of the
            // change it would make to your speed, toward what it leaves you with
            const float hs = std::sqrt(std::max(0.0f, 1.0f - s.dashUp * s.dashUp));
            const Vec3 target = {s.dashDirX * s.dashExitSpeed * hs, s.vel.y, s.dashDirZ * s.dashExitSpeed * hs};
            s.vel += (target - s.vel) * kIceDash;
            s.dashTimer = 0;
        }
    }
    bool dashing = s.dashTimer > 0;
    float gravDv = 0;   // (this step's fall from gravity, m/s: a ramp's wall face takes half of it back)
    if (dashing && !frozen) {
        // Dash jump (Celeste's super): jump while a dash runs on the floor (it started there, or it's sliding along it)
        // and the dash ends at once, leaving with its full burst speed along its direction plus a normal jump; also a
        // dash started just after jumping off the floor (jump pressed a hair first: the jump's rise is kept as it is).
        // Super dash (Celeste's hyper): the same off a dash aimed down (kSuperDashDown) into the floor, or meeting it
        // during the dash: the speed you carried into it + 5 (1.18.7; it was the burst x 1.25), sent up off the floor by
        // the dash's own down share, up to 90% (1.18.31: a skim aimed just past the threshold). The jump replaces the bhop: no
        // landing bounce (the ground check doesn't bounce off a dash) and no bhop jump on top.
        const float burst = dashBurst(s, mt);
        const bool down = s.dashUp <= -kSuperDashDown * (kDashVerticalDown * mt.m[MV_DASH_VERTICAL]);
        const bool wantJump = jumpHeld || s.jumpBuffer > 0;
        const bool graceJump = dashStart && !grounded && s.jumpGrace > 0 && !down;
        // (T3: a dash running since it left the floor, the jump within the window: a dash jump from the floor. During a
        // dash, jumpGrace is left only by that: a jump just before a dash spends it as the dash starts, graceJump)
        const bool liftJump = !dashStart && dashActive && !grounded && s.jumpGrace > 0 && wantJump && !down;
        if ((grounded && wantJump) || graceJump || liftJump) {
            float sp = burst, vy = graceJump ? s.vel.y : jumpSpeed, level = 1.0f;
            if (grounded && down) {
                // the hyper: the speed carried into the dash + kSuperDashBoost (owner 1.18.7: "+5 of current"), at least
                // what the dash leaves with on its own (11 from a standstill), at most kSuperDashMax
                // (the floor dash's exit, not an air dash's kDashAirGap one: a hyper out of the air is the same as off the floor)
                const float cap = kSuperDashMax * mt.m[MV_SUPER_DASH_MAX];
                const float own = std::min(kDashExitMax * mt.m[MV_DASH_EXIT_MAX],
                                           std::max(kDashExitMin * mt.m[MV_DASH_EXIT_MIN], s.dashFromSpeed + kDashKeep * mt.m[MV_DASH_KEEP]));
                sp = std::min(std::max(own, s.dashFromSpeed + kSuperDashBoost * mt.m[MV_SUPER_DASH_BOOST]), std::max(cap, own));
                // the bounce: the dash's down share of that speed goes up (the further down it was aimed, the higher;
                // owner 1.18: "the lower you look the higher you should bounce"), at most kSuperDashJump; the rest stays
                // level, so the speed is the same whichever way it points
                const float up = std::min(std::max(0.0f, -s.dashUp) * mt.m[MV_SUPER_DASH_JUMP],
                                          std::min(0.99f, kSuperDashJump * mt.m[MV_SUPER_DASH_JUMP]));
                vy = sp * up;
                level = std::sqrt(1.0f - up * up);
                setCharges(s, maxCharges, 1);   // (owner: a super dash leaves exactly one; 1.18.7: if one has recharged)
            } else {
                // a dash jump leaves you all of them (owner: it's off the ground), the one its own dash just spent too
                // (owner 1.18.7: "exempt the dash jump": that charge's recharge, the newest, is dropped); others still
                // come back only once recharged
                if (s.dashRecharging > 0) s.dashRechargeMs[--s.dashRecharging] = 0;
                refillCharges(s, maxCharges, maxCharges);
            }
            chargesSet = true;
            s.events |= (grounded && down) ? ME_HYPER : ME_SUPER;
            s.vel = {s.dashDirX * sp * level, vy, s.dashDirZ * sp * level};
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
        float burst = dashBurst(s, mt);
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
    bool grabbed = false;   // (this step's grab: one let go of in its own step still starts the cooldown)
    if (hookHeld && !(s.flags & MF_HOOKHELD) && !(s.flags & MF_HOOKED) && !frozen && s.hookCooldown <= 0) {
        float eyeH = (crouched ? kCrouchEyeHeight : kEyeHeight) * scale;
        Vec3 eye = s.pos + Vec3(0, eyeH, 0);
        const Vec3 dir = angleForward(cmd.yaw, cmd.pitch);
        const float range = kGrappleRange * mt.m[MV_HOOK_RANGE];
        int wallMat = -1;
        float t = map.raycast(eye, dir, range, nullptr, &wallMat);
        int caught = -1, rocket = -1;   // a player or a rocket in front of the wall (or in the open) catches it instead
        if (hp && (hp->n > 0 || hp->nr > 0)) {
            float best = t >= 0 ? t : range;
            Vec3 inv = safeInv(dir);
            for (int k = 0; k < hp->n && k < HookProbe::kMax; k++) {
                bool head;
                float tk = rayHitbox(eye, inv, hp->box[k], best, &head);   // (the body and the head, as shots)
                if (tk >= 0 && tk < best) { best = tk; caught = k; hp->head = head; }
            }
            for (int k = 0; k < hp->nr && k < HookProbe::kRockets; k++) {
                float tk = rayAABB(eye, inv, rocketCatchBox(hp->at[k]), best);
                if (tk >= 0 && tk < best) { best = tk; rocket = k; caught = -1; }
            }
        }
        // (a rocket still inside the release distance, one you just fired: the hook waits for it rather than catching it
        // and letting go at once, or going past it to the wall; held, it catches it a few ms later)
        const float release = kHookRelease * mt.m[MV_HOOK_RELEASE];
        if (rocket >= 0 && length(hp->pos[rocket] - (s.pos + Vec3(0, 1.0f * scale, 0))) < release) {
        } else if (rocket >= 0) {   // riding it: the anchor is the rocket as the server has it, flown with it from here on
            hp->hitRocket = rocket;
            s.hookPoint = hp->pos[rocket];
            s.hookVel = hp->vel[rocket];
            s.hookGrav = hp->grav[rocket];
            s.hookProj = hp->rid[rocket];
            s.hookTarget = kHookRocket;
            s.flags |= MF_HOOKED | MF_HOOKHELD;
            s.flags &= ~MF_PULLED;
            s.events |= ME_HOOK;
            grabbed = true;
        } else if (caught >= 0) {
            hp->hit = caught;
            s.hookPoint = hp->box[caught].center();   // (the server moves it with them from here on)
            s.hookTarget = (uint8_t)(hp->id[caught] + 1);
            s.flags |= MF_HOOKED | MF_HOOKHELD;
            s.flags &= ~MF_PULLED;   // grabbing with your own grapple breaks free of someone else's
            s.events |= ME_HOOK;
            grabbed = true;
        } else if (t >= 0 && wallMat == kMatGlassBreak && dot(s.vel, dir) < kGlassHookToward) {
            // glass, standing or moving off (owner, 2026-10-28): as if you'd shot it from the other side, shards come back
            // at you; the hook catches nothing (one try a press, the cooldown as a let-go)
            const int pane = map.glassAt(eye + dir * (t + 0.01f));
            if (pane >= 0 && s.glassBrush < 0) { s.glassBrush = pane; s.glassHow = GLASS_HOOK_BACK; s.glassAt = eye + dir * t; s.glassDir = dir * -1.0f; }
            s.flags |= MF_HOOKHELD;
            grabbed = true;
        } else if (t >= 0 && wallMat != kMatNoGrapple) {   // (nograpple: the hook hits it and lets go)
            s.hookPoint = eye + dir * t;
            s.hookTarget = 0;
            s.flags |= MF_HOOKED | MF_HOOKHELD;
            s.flags &= ~MF_PULLED;
            s.events |= ME_HOOK;
            grabbed = true;
        }
    }
    if (hp)   // (the rockets a held grapple could still catch fly on with this step, as the server's do)
        for (int k = 0; k < hp->nr && k < HookProbe::kRockets; k++) {
            hp->vel[k].y -= hp->grav[k] * kMoveStep; hp->pos[k] += hp->vel[k] * kMoveStep;
            hp->at[k] += hp->vel[k] * kMoveStep;   // (where it's seen moves on at the same speed)
        }
    if (!hookHeld || frozen) s.flags &= ~MF_HOOKED;
    if ((s.flags & MF_HOOKED) && s.hookTarget == kHookRocket) {
        // riding a rocket: the anchor flies its step the way Server::stepProjectile does (the same arithmetic, so the
        // two agree), and lets go where the rocket meets a wall (it explodes there). A player it hits is the server's
        // word: it lets go a snapshot later.
        s.hookVel.y -= s.hookGrav * kMoveStep;
        const Vec3 step = s.hookVel * kMoveStep;
        const float len = length(step);
        if (len >= 1e-6f && map.raycast(s.hookPoint, step / len, len) >= 0) s.flags &= ~MF_HOOKED;
        else if (len >= 1e-6f) s.hookPoint += step;
    }
    bool hooked = (s.flags & MF_HOOKED) != 0;
    if (hooked) {
        Vec3 to = s.hookPoint - (s.pos + Vec3(0, 1.0f * scale, 0));
        float dist = length(to);
        if (dist < (kHookRelease * mt.m[MV_HOOK_RELEASE])) {
            // pulled to a pane: it shatters on away from you as the grapple lets go
            if (s.hookTarget == 0 && !map.glass.empty() && s.glassBrush < 0) {
                const int pane = map.glassAt(s.hookPoint, 0.05f);
                if (pane >= 0) { s.glassBrush = pane; s.glassHow = GLASS_HOOK_THROUGH; s.glassAt = s.hookPoint; s.glassDir = dist > 1e-4f ? to / dist : Vec3(0, 0, 1); }
            }
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
    // (a grab already inside the release distance, e.g. against a wall, lets go in the step it grabbed: without the
    // cooldown it grabbed again every 1 ms step, and its sound played every frame; owner 1.18.32)
    if ((wasHooked || grabbed) && !(s.flags & MF_HOOKED)) s.hookCooldown = (kHookCooldown * mt.m[MV_HOOK_COOLDOWN]);
    if (!(s.flags & MF_HOOKED)) { s.hookTarget = 0; s.hookProj = 0; }
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
            s.events |= ME_JUMP;
        } else {
            // (owner, T6: W climbs (down while looking down past 26 deg), S the other way, A/D don't climb, only move
            // sideways; it was any key climbing, S and A/D up too. Held keys still move you along the wall and to / from it
            // as before: W carries you over the top, S backs you off one that ends at a wall)
            const float f = std::clamp(cmd.forward / 127.0f, -1.0f, 1.0f);
            float climb = (wishLen > 0 ? f : 0.0f) * (kLadderSpeed * mt.m[MV_LADDER_SPEED]) * (cmd.pitch < -0.45f ? -1.0f : 1.0f);
            s.vel = {wishDir.x * std::min(wishSpeed, (kLadderSide * mt.m[MV_LADDER_SIDE])), climb, wishDir.z * std::min(wishSpeed, (kLadderSide * mt.m[MV_LADDER_SIDE]))};
        }
        onGround = false;
    } else if (onGround) {
        // the floor's grip: ice slippery (little friction, little push), rubber and asphalt grippy (more of both), mud
        // sticky (it stops you hard)
        float grip = 1.0f, push = 1.0f;
        if (floorMat == kMatIce) { grip = kIceFriction; push = kIceAccel; }
        else if (floorMat == kMatRubber) { grip = kRubberGrip; push = kRubberGrip; }
        else if (floorMat == kMatAsphalt) { grip = kAsphaltGrip; push = kAsphaltGrip; }
        else if (floorMat == kMatMud) grip = kMudGrip;
        float sp = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        if (sp > 0) {
            float drop = std::max(sp, (kStopSpeed * mt.m[MV_STOP_SPEED])) * (kFriction * mt.m[MV_FRICTION]) * grip * dt;
            float ns = std::max(sp - drop, 0.0f) / sp;
            s.vel.x *= ns;
            s.vel.z *= ns;
        }
        accelerate(s.vel, wishDir, wishSpeed, (kGroundAccel * mt.m[MV_GROUND_ACCEL]) * push, dt);
        s.vel.y = 0;
    } else {
        if (!dashing && !onLadder) {
            // surf: strafing into a ramp you touch holds you on it (kRampAirAccel: the wish-speed cap stays, so it's grip,
            // not free speed)
            const bool ramp = (s.mods & MOVE_RUN_ONLY) && onRampFace(map, playerBox(s.pos, crouched, scale));
            const float airAccel = (kAirAccel * mt.m[MV_AIR_ACCEL]) * 10.0f * (ramp ? kRampAirAccel : 1.0f), airCap = kAirSpeedCap * mt.m[MV_AIR_CAP];
            const float airControl = kAirControl * mt.m[MV_AIR_CONTROL];
            // 22 x 2.3 / 4 = 12.65 m/s: where the A/D boost ramp starts (the base cap and the unscaled air control, so
            // the 1.18 A/D cap and the slower W-only turn leave it where it was)
            const float cross = (kAirAccel * mt.m[MV_AIR_ACCEL]) * 10.0f * airCap / airControl;   // (the ramp boost leaves it)
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
            gravDv = kGravity * mt.gravity * dt * ((hooked || pulled) ? (kHookGravityScale * mt.m[MV_HOOK_GRAVITY]) : 1.0f) * ((s.mods & MOVE_LOW_GRAVITY) ? 0.4f : 1.0f);
            s.vel.y -= gravDv;
        }
    }

    Vec3 delta = s.vel * dt;
    MoveState before = s;
    Vec3 wall;
    int blocked = slideMove(s, map, delta, crouched, &wall);
    const bool steep = wall.x != 0 || wall.y != 0 || wall.z != 0;
    // against a slope's wall face: the velocity along it (what goes into it is gone), so you slide down it as you
    // fall and along it as you run; it never lifts you off the floor (no climbing a steep slope by running at it; in
    // the air it's a plain clip, so a ramp turns a fall along it: surfing). A dash does go
    // up it (owner, 2026-10-28: "if you dash into a slope ... it should push you up it, even if the angle is >45";
    // down a downward one too): its direction turns along the face, keeping its speed.
    if (steep) {
        const float vn = dot(s.vel, wall);
        if (vn < 0) {
            const float vy = s.vel.y, speed = length(s.vel);
            s.vel -= wall * vn;
            if (!dashing && grounded) s.vel.y = std::min(s.vel.y, std::max(vy, 0.0f));   // (off the floor it's a pure clip: surfing)
            // riding a ramp's face (it faces up): gravity at half strength, so you slip down it and off it half as fast
            // (owner, 2026-10-28: "the slopes are too slippery to make surf work ... half of what it currently is")
            if (!dashing && !grounded && wall.y > 0) s.vel.y += kSlopeGravityBack * gravDv;
            else {
                const float hs = std::sqrt(std::max(0.0f, 1.0f - s.dashUp * s.dashUp));
                Vec3 d = {s.dashDirX * hs, s.dashUp, s.dashDirZ * hs};
                const float dn = dot(d, wall);
                if (dn < 0) {
                    d -= wall * dn;
                    const float l = length(d), h = std::sqrt(d.x * d.x + d.z * d.z);
                    if (l > 1e-3f && h > 1e-4f) { s.dashUp = d.y / l; s.dashDirX = d.x / h; s.dashDirZ = d.z / h; }
                    else if (l > 1e-3f) s.dashUp = d.y > 0 ? 1.0f : -1.0f;
                    // (owner, 2026-10-28: "keep your velocity from the dash as if there was no block there"): its whole
                    // speed, turned along the face, nothing lost to the hit
                    if (l > 1e-3f) s.vel = d * (speed / l);
                }
            }
        }
    }

    // Step up small ledges when walking into them, and in the air too (jumping, falling, hooked, dashing):
    // running into a stair's riser carries you over its lip like walking does, keeping your speed, instead
    // of stopping you dead. Only ledges up to a step's height qualify, so walls still block. Walking up a slope is
    // this too (each step's rise is a sliver); a steep one is a wall, never stepped up.
    bool airStep = !onGround;
    if ((onGround || airStep) && (blocked & 0b101) && !steep) {
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
    // flips; the vertical part is kept) and goes on at kDashBounceSpeed (16; it was the burst): the rest of the dash
    // runs in the new direction, and it leaves at that speed however little was left. A dash jump after it keeps
    // that speed; a super dash adds to it.
    if (dashing && (blocked & 0b101) && s.dashBounces < kDashMaxBounces) {
        const float c = std::cos(kDashBounceAngle * mt.m[MV_DASH_BOUNCE_ANGLE] * (PI / 180.0f));
        const bool bx = (blocked & 1) && std::fabs(s.dashDirX) > c, bz = !bx && (blocked & 4) && std::fabs(s.dashDirZ) > c;
        if (bx || bz) {
            if (bx) { s.dashDirX = -s.dashDirX; blocked &= ~1; }
            else { s.dashDirZ = -s.dashDirZ; blocked &= ~4; }
            s.dashBounces++;
            setCharges(s, maxCharges, 1);   // (owner: a wall bounce leaves you exactly one charge; 1.18.7: if one has recharged)
            chargesSet = true;
            const float bounce = kDashBounceSpeed * mt.m[MV_DASH_SPEED];   // (owner 1.18.7: 16, was the burst)
            const float hs = std::sqrt(std::max(0.0f, 1.0f - s.dashUp * s.dashUp));
            s.dashExitSpeed = bounce;
            s.dashFromSpeed = bounce;   // (a super dash off the bounce adds to this)
            s.vel.x = s.dashDirX * bounce * hs;
            s.vel.z = s.dashDirZ * bounce * hs;
        }
    }
    // Rubber walls and ceilings: a hit faster than kRubberMin bounces back (the floor: at landing, below)
    if (map.hasRubber && (blocked & 0b111)) {
        const AABB b = playerBox(s.pos, crouched, scale);
        for (int a = 0; a < 3; a++) {
            if (!(blocked & (1 << a)) || (a == 1 && delta.y <= 0) || std::fabs(s.vel[a]) < kRubberMin) continue;
            const Brush* hb = nullptr;
            map.sweepAxis(b, a, delta[a] > 0 ? 0.02f : -0.02f, &hb);
            if (hb && hb->material == kMatRubber) { s.vel[a] = -s.vel[a] * kRubberBounce; blocked &= ~(1 << a); }
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
    const Brush* under = nullptr;
    float probe = canLand ? map.sweepAxis(feet, 1, -0.05f, &under) : 0.0f;
    bool nowGround = canLand && probe > -0.049f && !steepUnder(under, shifted(feet, 1, probe));   // (a wall face is no floor: you slide)
    bool bounced = false;
    if (nowGround) {
        s.pos.y += probe; // settle onto the floor
        float impact = -before.vel.y;
        if (!grounded && !dashing && impact > kLandSoundSpeed) { s.events |= ME_LAND; s.landSpeed = std::max(s.landSpeed, impact); }
        const bool hop = (jumpHeld || (tapped && s.jumpBuffer > 0)) && !frozen && !grappleOnly && impact > jumpSpeed && !onLadder && !dashing;
        if (under && under->material == kMatRubber && impact > kRubberMin && !onLadder) {
            // rubber: back up at kRubberBounce of the landing speed (a hop off it: whichever is higher)
            s.vel.y = impact * kRubberBounce;
            if (hop) s.vel.y = std::max(s.vel.y, jumpSpeed + std::min((impact - jumpSpeed) * (kBounceGain * mt.m[MV_BOUNCE_GAIN]), (kBounceMax * mt.m[MV_BOUNCE_MAX])));
            nowGround = false;
            bounced = true;
            if (hop) { s.events |= ME_JUMP; if (tapped) s.jumpBuffer = 0; s.jumpGrace = jumpWindow; }
        } else if ((jumpHeld || (tapped && s.jumpBuffer > 0)) && !frozen && !grappleOnly && impact > jumpSpeed && !onLadder && !dashing) {
            // bhop off a hard landing: bounce higher than a normal jump (the landing speed is the only input,
            // so prediction replays it exactly). Not off a dash's own push into the floor: that's a super dash
            // (next step, from the floor) or nothing.
            s.vel.y = jumpSpeed + std::min((impact - jumpSpeed) * (kBounceGain * mt.m[MV_BOUNCE_GAIN]), (kBounceMax * mt.m[MV_BOUNCE_MAX]));
            nowGround = false;
            bounced = true;
            s.events |= ME_JUMP;
            if (tapped) s.jumpBuffer = 0;
            s.jumpGrace = jumpWindow;   // (T4: a hop off a landing, like a jump off the floor)
        } else if (s.vel.y < 0) s.vel.y = 0;
    } else if (onGround && s.vel.y <= 0) {
        float down = map.sweepAxis(feet, 1, -(kStepHeight * mt.m[MV_STEP]), &under);
        if (down > -(kStepHeight * mt.m[MV_STEP]) + 0.001f && !steepUnder(under, shifted(feet, 1, down))) { s.pos.y += down; nowGround = true; s.vel.y = 0; }
    }

    // the dash cooldown starts when a dash ends (its timer ran out, or a dash jump / super dash ended it), this step
    if ((dashActive || dashStart) && s.dashTimer <= 0) s.dashCooldownMs = (uint16_t)cooldownMsFor(mt, s.mods);

    s.flags &= (MF_FROZEN | MF_HOOKED | MF_PULLED);
    if (nowGround && !(s.flags & (MF_HOOKED | MF_PULLED))) s.flags |= MF_ONGROUND;
    // Dash charges back (owner 1.17): on the floor at the end of a step, all of them; while holding jump (a bhop
    // landing: the next step leaves the floor again, or this one bounced off it) only up to ONE ("when holding space you
    // should only be able to charge 1 dash"): hopping never builds the second charge back. Not during a dash's own
    // steps (its push into the floor is no landing: a super dash would refill itself), not on a ladder or off a grapple.
    // Only charges that have recharged come back (1.18.7: kDashRecharge since the dash that spent them ended).
    if (!chargesSet && !dashing && !onLadder) {
        const bool hopping = jumpHeld && !frozen && !grappleOnly;
        if (bounced || (hopping && (s.flags & MF_ONGROUND))) refillCharges(s, maxCharges, 1);
        else if (s.flags & MF_ONGROUND) refillCharges(s, maxCharges, maxCharges);
    }
    if (crouched) s.flags |= MF_CROUCHED;
    if (jumpHeld) s.flags |= MF_JUMPHELD;
    if (dashHeld) s.flags |= MF_DASHHELD;

    if (s.pos.y < -100.0f) s.pos.y = -100.0f; // kill plane handled by game logic
}

// The player's state as a dynamic movement equation reads it (p.*, moveexpr.h), at the start of a 1 ms step. One
// definition for pmove and every caller that asks for a per-step value (stepTuning), so they never disagree.
// `map` null: p.height reads 0.
static void exprInputs(const MoveState& s, const Map* map, uint32_t need, double* in) {
    const bool ground = (s.flags & MF_ONGROUND) != 0;
    const float h2 = s.vel.x * s.vel.x + s.vel.z * s.vel.z;
    in[mexpr::P_SPEED] = std::sqrt(h2 + s.vel.y * s.vel.y);
    in[mexpr::P_HSPEED] = std::sqrt(h2);
    in[mexpr::P_VSPEED] = s.vel.y;
    in[mexpr::P_AIR] = ground ? 0.0 : s.phaseMs / 1000.0;
    in[mexpr::P_GROUND] = ground ? s.phaseMs / 1000.0 : 0.0;
    in[mexpr::P_ONGROUND] = ground ? 1.0 : 0.0;
    in[mexpr::P_CHARGES] = s.dashCharges;
    in[mexpr::P_DASHING] = s.dashTimer > 0 ? 1.0 : 0.0;
    in[mexpr::P_HOOKED] = (s.flags & MF_HOOKED) ? 1.0 : 0.0;
    in[mexpr::P_HEIGHT] = 0;
    if ((need & 1u << mexpr::P_HEIGHT) && !ground && map)   // (a sweep down: only when an equation reads it)
        in[mexpr::P_HEIGHT] = -map->sweepAxis(playerBox(s), 1, -50.0f);
}

const MoveTuning& stepTuning(const MoveState& s, const Map* map, MoveTuning& scratch) {
    const MoveTuning& mt = moveTuning();
    if (__builtin_expect(mt.expr == nullptr, 1)) return mt;
    scratch = mt;
    double in[mexpr::P_COUNT];
    exprInputs(s, map, mt.expr->inputs, in);
    mt.expr->run(in, scratch);
    return scratch;
}
float moveValue(const MoveState& s, int param, const Map* map) {
    const MoveTuning& mt = moveTuning();
    if (__builtin_expect(mt.expr == nullptr, 1)) return mt.m[param];
    MoveTuning t;
    return stepTuning(s, map, t).m[param];
}

// Acid, after each 1 ms step (only on maps that have it): where the step left the hull. Deterministic, so the
// client's prediction counts the same damage the server will take.
static inline void acidStep(MoveState& s, const Map& map) {
    if (map.inAcid(playerBox(s))) {
        if (s.acidOutMs >= kAcidRearmMs) s.acidTaken += kAcidContact;   // a new contact
        s.acidTaken += kAcidPerSec * kMoveStep;
        s.acidOutMs = 0;
    } else if (s.acidOutMs < kAcidRearmMs) s.acidOutMs++;
}

// A custom mode's dynamic movement equations (moveexpr.h): before each 1 ms step, the player's state (p.*) goes in
// and the dynamic settings come out, into this command's own copy of the tuning (the static ones are already in it).
// Out of line, so the standard path below stays as it was: one predictable branch per command.
[[gnu::noinline]] static void pmoveExpr(MoveState& s, const UserCmd& cmd, const Map& map, HookProbe* probe, const MoveTuning& base,
                                        const Vec3& fwd, const Vec3& right, int n) {
    MoveTuning t = base;
    const mexpr::Program& prog = *base.expr;
    const uint32_t need = prog.inputs;
    const CmdCtx cx{t, fwd, right};
    for (int i = 0; i < n; i++) {
        const bool ground = (s.flags & MF_ONGROUND) != 0;
        double in[mexpr::P_COUNT];
        exprInputs(s, &map, need, in);
        prog.run(in, t);
        pmoveStep(s, cmd, map, kMoveStep, probe, cx);
        if (!map.acid.empty()) acidStep(s, map);
        if (need & mexpr::kPhaseInputs)
            s.phaseMs = ((s.flags & MF_ONGROUND) != 0) != ground ? 0 : (uint16_t)(s.phaseMs < 65535 ? s.phaseMs + 1 : 65535);
    }
}

void pmove(MoveState& s, const UserCmd& cmd, const Map& map, HookProbe* probe) {
    static_assert(HookProbe::kMax >= kMaxPlayers, "a probe holds every player");
    int n = cmd.msec < 1 ? 1 : (cmd.msec > 8 ? 8 : cmd.msec);
    const float sy = std::sin(cmd.yaw), cy = std::cos(cmd.yaw);
    s.events = 0;
    s.landSpeed = 0;
    s.glassBrush = -1;
    const MoveTuning& mt = moveTuning();
    if (__builtin_expect(mt.expr != nullptr, 0)) { pmoveExpr(s, cmd, map, probe, mt, {-sy, 0, -cy}, {cy, 0, -sy}, n); return; }
    const CmdCtx cx{mt, {-sy, 0, -cy}, {cy, 0, -sy}};
    if (__builtin_expect(map.acid.empty(), 1)) { for (int i = 0; i < n; i++) pmoveStep(s, cmd, map, kMoveStep, probe, cx); return; }
    for (int i = 0; i < n; i++) { pmoveStep(s, cmd, map, kMoveStep, probe, cx); acidStep(s, map); }
}

float moveParamBase(int i) {
    static const float v[MV_COUNT] = {kCrouchSpeed, kGroundAccel, kFriction, kStopSpeed, kAirAccel, kAirSpeedCap, kOverstrafeBrake, kAirControl,
        kStepHeight, kDashSpeed, kDashTime, kDashExitMin, kDashKeep, kDashExitMax, kDashVerticalDown, (float)kDashCharges, kDashCooldown,
        kBounceGain, kBounceMax, kGrappleRange, kHookAccel, kHookMaxSpeed, kHookGravityScale, kHookRelease, kHookCooldown,
        kLadderSpeed, kLadderSide, kDashJumpWindow, kSuperDashBoost, kSuperDashJump, kSuperDashMax, kDashBounceAngle,
        kAirDriftAccel, kStrafeBoost};
    return i >= 0 && i < MV_COUNT ? v[i] : 1.0f;
}
const char* moveParamUnit(int i) {
    static const char* u[MV_COUNT] = {"m/s", "", "", "m/s", "", "m/s", "", "", "m", "m/s", "s", "m/s", "m/s", "m/s", "", "", "s",
        "", "m/s", "m", "m/s2", "m/s", "x", "m", "s", "m/s", "m/s", "s", "m/s", "x", "m/s", "deg", "m/s2", ""};   // (super dash boost: + m/s since 1.18.7)
    return i >= 0 && i < MV_COUNT ? u[i] : "";
}
int dashMaxCharges(uint8_t mods) { return maxChargesFor(moveTuning(), mods); }
int dashMaxCharges(const MoveState& s, const Map* map) { MoveTuning t; return maxChargesFor(stepTuning(s, map, t), s.mods); }
int dashCooldownMs(uint8_t mods) { return cooldownMsFor(moveTuning(), mods); }
int dashRechargeMs(uint8_t mods) { return rechargeMsFor(moveTuning(), mods); }
bool dashReady(const MoveState& s) {   // (the next step counts the cooldown down by its 1 ms before it looks)
    return s.dashCharges > 0 && s.dashTimer <= 0 && (int)s.dashCooldownMs <= std::max(1, (int)std::lround(kMoveStep * 1000.0f));
}
bool dashAimedDown(const MoveState& s, const Map* map) { return s.dashUp <= -kSuperDashDown * (kDashVerticalDown * moveValue(s, MV_DASH_VERTICAL, map)); }
void dashRefillAll(MoveState& s, const Map* map) {
    s.dashCharges = (uint8_t)dashMaxCharges(s, map);
    s.dashCooldownMs = 0;
    s.dashRecharging = 0;
    for (uint16_t& v : s.dashRechargeMs) v = 0;
}
float moveBaseGravity() { return kGravity; }
float moveBaseSpeed() { return kGroundSpeed; }
float moveBaseJump() { return kJumpSpeed; }
