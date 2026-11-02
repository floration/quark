// Shared player movement. Runs identically on server (authoritative) and client (prediction).
#pragma once
#include "../common/qmath.h"
#include "map.h"
#include "gamedefs.h"

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

constexpr int kDashCharges = 2;   // dashes you hold (owner 1.17)...
constexpr float kDashCooldown = 0.6f;   // ...a dash waits this long after the previous dash ENDS (owner 1.18.7: "you can use them 0.6
                                        // seconds apart"; it was 1.2 s between any two dashes)...
constexpr float kDashRecharge = 1.2f;   // ...and a spent charge can only come back this long after the dash that spent it ended (owner
                                        // 1.18.7: "dashes take 1.2 seconds to recharge ... when you jump in the air with 2 dashes, you
                                        // can use both of them within 1 second, without recharging any charges"); it still comes back
                                        // only the way charges always have (the floor all, a bhop landing one, the dash techs)
constexpr int kMaxDashCharges = 10;     // the most charges a custom mode can give (2 x its 5x limit): the recharge timers' room

// Everything prediction needs to replay movement. Sent unquantized for the local player.
struct MoveState {
    Vec3 pos;      // feet position (bottom center of hull)
    Vec3 vel;
    uint8_t flags = 0; // MF_*
    uint8_t dashCharges = kDashCharges;   // dashes left: the ground refills them all, a bhop landing one (once recharged)
    uint16_t dashCooldownMs = 0;   // ms until the next dash may start (counts from the end of the last one; whole ms: exact on the wire)
    uint8_t dashRecharging = 0;    // spent charges still recharging: their ms left in dashRechargeMs[0..n), oldest first (the newest
                                   // waits at the full recharge while the dash that spent it runs); spent charges past it are the rest
                                   // (max - dashCharges - dashRecharging): a refill can give those back
    uint16_t dashRechargeMs[kMaxDashCharges] = {};
    float dashTimer = 0;     // > 0 while the dash burst is active
    float dashDirX = 0, dashDirZ = 0;
    float dashUp = 0;        // vertical part of the dash (from look pitch, small)
    float dashExitSpeed = 0; // horizontal speed to settle to when the burst ends (after a wall bounce: the bounce's speed)
    float dashFromSpeed = 0; // horizontal speed when this dash started (after a wall bounce: the bounce's): a super dash adds to it
    float jumpBuffer = 0;    // a fresh jump press still counts this long for a dash jump / super dash (frozen while dashing)
    float jumpGrace = 0;     // jumped off the ground by a press this recently: a dash now is still a ground dash jump
    uint8_t dashBounces = 0; // wall bounces this dash has made
    bool dashSlide = false;  // a dash aimed down met the floor with no jump: ice's grip until run speed or a jump (protocol 64)
    // Zero-g flight (3D VAMP, AR_FLY; owner, 2026-11-01: "Minecraft creative" flying): no gravity, no jump, dash or grapple;
    // W/S/A/D on the level, MOVE UP / MOVE DOWN (BTN_UP / BTN_DOWN) straight up and down, at a flat kFlySpeed. The server
    // sets it at spawn from the rules, like mods (protocol 67)
    bool fly = false;
    float hookCooldown = 0;
    Vec3 hookPoint;    // grapple anchor while MF_HOOKED (latched onto a player: their body, moved by the server every tick)
    uint8_t mods = 0;  // MOVE_* rule modifiers (duel)
    uint8_t hookTarget = 0; // while MF_HOOKED: 0 = the map, 1 + id = latched onto that player, kHookRocket = riding a rocket
    // riding a rocket (hookTarget kHookRocket): its id (0: your own one the server hasn't named yet, a predicted copy),
    // and its velocity and gravity: pmove flies the anchor with it in the projectile's own 1 ms steps (stepProjectile's
    // arithmetic), so prediction follows it without the server; the server sets all three from the rocket every tick
    uint16_t hookProj = 0;
    Vec3 hookVel;
    float hookGrav = 0;
    float hookAccel = 0;   // ...and its thrust (m/s2 along its velocity: the primary rocket's, projectileAccel; protocol 55)
    Vec3 pullPoint;    // while MF_PULLED: the body of the player whose grapple holds you (set by the server every tick)
    // What the last pmove call did (ME_*; each call clears it), for sounds: a frame or a snapshot can hold several 1 ms
    // steps, and comparing states across it missed a jump and a landing in the same step (a bhop) or a dash and its dash
    // jump. Not sent and not compared: the client's prediction and the server each make their own.
    uint8_t events = 0;
    float landSpeed = 0;   // ME_LAND: the hardest landing's speed in that call, m/s
    // Glass this call broke (owner, 2026-10-28), for the server to break and prediction to show: a dash through a pane
    // (GLASS_DASH, dir the dash's), a grapple fired at one while standing or moving off (GLASS_HOOK_BACK: shards back at
    // you) or let go next to one it pulled you to (GLASS_HOOK_THROUGH: it shatters on away from you). Not sent.
    int32_t glassBrush = -1;
    uint8_t glassHow = 0;
    Vec3 glassAt, glassDir;
    uint16_t phaseMs = 0;   // ms since you last landed or left the floor (MF_ONGROUND says which), at most 65535: counted
                            // only while a custom mode's movement equation reads p.air / p.ground (moveexpr.h), else 0
    uint16_t acidOutMs = kAcidRearmMs;   // ms fully out of acid (0: touching it in the last step), up to kAcidRearmMs: there,
                                         // the next touch is a new contact (its kAcidContact hit). Sent while below it
    float acidTaken = 0;   // share of max health acid took (kAcidContact, kAcidPerSec x 1 ms per step in it): the server
                           // takes it as damage after each command and zeroes it, so it's 0 on the wire; prediction keeps
                           // adding (NetClient::pendingAcid: the part the server hasn't acked yet)
};
enum : uint8_t {
    ME_JUMP = 1,    // left the floor by a jump (a bhop off a landing too, and a jump off a ladder)
    ME_LAND = 2,    // landed at over kLandSoundSpeed (a bhop that bounces straight off still lands)
    ME_DASH = 4,    // a dash started
    ME_SUPER = 8,   // a dash jump (Celeste's super)
    ME_HYPER = 16,  // a super dash (the hyper)
    ME_HOOK = 32,   // your grapple caught something
};
constexpr float kLandSoundSpeed = 6.0f;   // m/s: softer landings make no sound
enum : uint8_t {
    MOVE_GRAPPLE_ONLY = 1, // no walking, jumping or dashing: the grapple is the only way to move
    MOVE_QUICK_DASH = 2,   // the dash cooldown and recharge a third as long (0.6 -> 0.2 s, 1.2 -> 0.4 s; 1.17: a third charge instead)
    MOVE_LOW_GRAVITY = 4,  // 40% gravity
    MOVE_RUN_ONLY = 8,     // no dash, no grapple: run and jump (and climb ladders) only
    MOVE_BIG = 16,         // duel "double size" / "giant snipers": the hull (and eyes) x kBigScale
    MOVE_TINY = 32,        // duel "tiny players": x kTinyScale
};                         // (64 and 128 are taken on the wire: the own state's acid and phase flags, protocol.cpp)
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
constexpr uint8_t kHookRocket = kMaxPlayers + 1;   // MoveState::hookTarget: the grapple rides a rocket (PlayerSnap hooked: 1 + it)
constexpr uint8_t kHookBall = kMaxPlayers + 2;     // ...latched onto the ball (ball mode; protocol 65: the server moves hookPoint with it)
// Rocket riding (owner 2026-10-27): the grapple catches a live rocket in flight, anyone's, as this box round it: 0.8 m
// across, a player's width. The rocket is 0.6 m long and 0.3 m over its fins, and at 48 m/s it moves 0.8 m in a 60 Hz
// frame, so a box the size of one frame's flight catches a hook aimed at it in any frame, and covers the few ms the
// client's idea of where the server has it can be out (snapshot jitter, the 1/64 m grid)
constexpr float kRocketCatchHalf = 0.4f;
// How far past its view time a client draws projectiles (it extrapolates them by its interpolation delay), in ms: its
// fixed delay as it reported it ("interp"), else the adaptive delay's base, two snapshots. The server tests anyone
// else's rocket there, and the client's prediction does the same sum.
inline uint32_t rocketSeenMs(uint32_t fixedInterpMs, int snapRate) { return fixedInterpMs ? fixedInterpMs : 2000u / (uint32_t)(snapRate > 0 ? snapRate : 1); }
inline AABB rocketCatchBox(const Vec3& p) { const Vec3 h(kRocketCatchHalf, kRocketCatchHalf, kRocketCatchHalf); return {p - h, p + h}; }
constexpr float kHookCooldown = 1.0f;   // after the hook is released

// The hull: kPlayerHalfWidth each side, kPlayerHeight tall (crouched: kPlayerCrouchHeight), times scale (size modifiers).
AABB playerBox(const Vec3& feet, bool crouched, float scale = 1.0f);
inline AABB playerBox(const MoveState& m) { return playerBox(m.pos, (m.flags & MF_CROUCHED) != 0, moveScale(m.mods)); }
// What hits test, inside a player's hitbox (the hull above): two boxes. The head is a head-sized box on top, centred
// (owner 5.5: shoulders aren't headshots); the body is the full width from the feet up to the head's bottom (owner
// 2026-10-27: nothing above that line but the head, so a shot past the head over the shoulders misses). Crouching and
// size modifiers scale both with the hull. Movement and zones still use the whole hull.
// (inline: hitscan tests every player for every pellet)
inline AABB headBox(const AABB& b) {
    Vec3 c = b.center();
    float hx = (b.mx.x - b.mn.x) * 0.5f * kHeadWidthFraction, hz = (b.mx.z - b.mn.z) * 0.5f * kHeadWidthFraction;
    return {{c.x - hx, b.mx.y - (b.mx.y - b.mn.y) * kHeadFraction, c.z - hz}, {c.x + hx, b.mx.y, c.z + hz}};
}
inline AABB bodyBox(const AABB& b) {   // (its top is the same expression as the head's bottom: no gap, no overlap)
    return {b.mn, {b.mx.x, b.mx.y - (b.mx.y - b.mn.y) * kHeadFraction, b.mx.z}};
}
// A ray o + dir * t (inv = safeInv(dir)) against a player as hits see them: the entry into the nearer of body and head
// in [0, maxT], else -1 (two slab tests, nothing else). *head: the head was hit first (a tie is the body's). grow: both
// boxes this much wider each side (the knife swing).
inline float rayHitbox(const Vec3& o, const Vec3& inv, const AABB& hitbox, float maxT, bool* head = nullptr, float grow = 0.0f) {
    AABB body = bodyBox(hitbox), hd = headBox(hitbox);
    if (grow != 0.0f) {
        body.mn.x -= grow; body.mn.z -= grow; body.mx.x += grow; body.mx.z += grow;
        hd.mn.x -= grow; hd.mn.z -= grow; hd.mx.x += grow; hd.mx.z += grow;
    }
    const float tb = rayAABB(o, inv, body, maxT), th = rayAABB(o, inv, hd, maxT);
    const bool h = th >= 0 && (tb < 0 || th < tb);
    if (head) *head = h;
    return h ? th : tb;
}
// The point of the two boxes nearest p (blast distances)
inline Vec3 hitboxClosest(const AABB& hitbox, const Vec3& p) {
    const AABB body = bodyBox(hitbox), hd = headBox(hitbox);
    const Vec3 a = vmax(body.mn, vmin(p, body.mx)), b = vmax(hd.mn, vmin(p, hd.mx));
    return dot(a - p, a - p) <= dot(b - p, b - p) ? a : b;
}
// Where a projectile that met a wall, the floor or a player `hit` metres along its step from `from` (unit `dir`) goes
// off: 5 cm short of the surface, so a blast's cover rays (Server::explode, Game::reportBlast) don't start on the
// surface they then hit at t = 0 (every splash victim "behind cover"). The server's stepProjectile and the shooter's
// game (reportImpact) both use it, so a reported blast is where the server's would be.
constexpr float kBlastBackoff = 0.05f;
inline Vec3 blastPoint(const Vec3& from, const Vec3& dir, float hit) { return from + dir * (hit > kBlastBackoff ? hit - kBlastBackoff : 0.0f); }
// A blast at `at` reaches the player in `hitbox` (Server::explode and the shooter's Game::reportBlast, the same test):
// the ray to the box's centre is clear, or, the centre behind cover, a half-peeking player still takes it if the blast
// reaches the box's nearest point or the head (F16). At most three rays.
inline bool blastReaches(const Map& map, const Vec3& at, const AABB& hitbox) {
    const Vec3 c = hitbox.center(), to = c - at;
    const float dist = length(to);
    if (dist <= 0.01f) return true;
    const float w = map.raycast(at, to / dist, dist);
    if (w < 0 || w >= dist - 0.1f) return true;
    auto clear = [&](const Vec3& pt) {
        const Vec3 v = pt - at;
        const float len = length(v);
        return len < 0.11f || !map.raycastAny(at, v / len, len - 0.1f);
    };
    return clear(hitboxClosest(hitbox, at)) || clear(Vec3(c.x, hitbox.mx.y - 0.15f * (hitbox.mx.y - hitbox.mn.y), c.z));
}
// the standard values a custom mode scales (MOVEMENT / GENERAL pages show "standard -> this mode"), and their units
float moveParamBase(int param);           // MV_*
const char* moveParamUnit(int param);
float moveBaseGravity(), moveBaseSpeed(), moveBaseJump();
// The tuning pmove's next 1 ms step plays by for this state: the custom mode's values with its dynamic movement
// equations (moveexpr.h) worked out for `s` (the same inputs pmove computes). Without dynamic equations it is
// moveTuning() itself (no copy, no cost); with them it is `scratch`. `map`: for p.height (null: 0). Anything outside
// pmove that reads a movement value for a player (HUD, server grapple, bots, sounds) uses this or moveValue, never
// moveTuning().m directly, so it never mixes a value at rest with the one the player's movement uses.
struct MoveTuning;   // (gamedefs.h)
const MoveTuning& stepTuning(const MoveState& s, const Map* map, MoveTuning& scratch);
float moveValue(const MoveState& s, int param, const Map* map = nullptr);   // stepTuning(...).m[param] (MV_*)
// how many dash charges a full refill gives (kDashCharges x the custom mode's multiplier, at least 1; quick dash
// gives none extra since 1.18, `mods` is kept for callers). The state's own: for that state (equations); the mods
// one: the value at rest (moveTuning(): a fresh spawn, the tests)
int dashMaxCharges(const MoveState& s, const Map* map = nullptr);
int dashMaxCharges(uint8_t mods);
// the dash cooldown in ms (kDashCooldown x the custom mode's multiplier; MOVE_QUICK_DASH in `mods`: a third of that)
int dashCooldownMs(uint8_t mods = 0);
// a spent charge's recharge in ms (kDashRecharge, scaled the same way: the custom mode's DASH COOLDOWN scales both)
int dashRechargeMs(uint8_t mods = 0);
// would a fresh dash press in the next command start a dash (not frozen / run only): a charge left, no dash running,
// and the cooldown since the last one ended over by that command's first step. Bots and the HUD use it, so they match pmove.
bool dashReady(const MoveState& s);
// is this dash aimed down far enough that a jump makes it a super dash (else a dash jump)
bool dashAimedDown(const MoveState& s, const Map* map = nullptr);
// every charge back at once and nothing recharging (a teammate's syringe)
void dashRefillAll(MoveState& s, const Map* map = nullptr);
// The players a grapple fired in this command can latch onto (the server fills it, lag compensated: where the shooter
// saw them; enemies only). The hook takes the nearest of these or the map along its ray; `hit` says which box caught.
struct HookProbe {
    static constexpr int kMax = 32;   // (kMaxPlayers)
    int n = 0;
    AABB box[kMax];
    uint8_t id[kMax] = {};
    int hit = -1;        // out: index of the box the hook caught (-1: none, or the map)
    bool head = false;   // out: the hook caught that player's head (rayHitbox)
    // Rocket riding: the rockets in flight it can catch (only while the mode allows it; never for bots), at most kRockets
    // (the nearest few in reach: a few box tests a try). at: where the box is tested (where the shooter sees it: an
    // enemy's rewound to their view, your own where the server has it); pos / vel / grav: the rocket as the server has
    // it at this command, the anchor's start. pmove moves both along by the rocket's steps as its steps go by.
    static constexpr int kRockets = 6;
    int nr = 0;
    Vec3 at[kRockets], pos[kRockets], vel[kRockets];
    float grav[kRockets] = {}, accel[kRockets] = {};   // (accel: its thrust, projectileAccel)
    uint16_t rid[kRockets] = {};   // the rocket's id (0: your own predicted copy, not named by the server yet)
    int hitRocket = -1;   // out: index of the rocket the hook caught (-1: none)
    bool ball = false;    // ball mode: the ball can catch it (ballBox: where it's seen)
    AABB ballBox;
    bool hitBall = false; // out: the hook caught the ball
};
// Advances movement by cmd.msec milliseconds, in fixed 1 ms steps (tick-rate independent). `probe`: players and rockets
// the grapple can catch (the server's lag compensated picks; the client's, as it sees them: NetClient::hookProbe).
void pmove(MoveState& s, const UserCmd& cmd, const Map& map, HookProbe* probe = nullptr);
// The ball (MODE_BALL; owner, 2026-11-01: "the same physics as a player on ice at 50% gravity"): its box (kBallHalf each
// side of its centre `c`) moves like a player's hull with no input on an ice floor: half the gravity, the ground's
// friction x kIceFriction on any floor, 1 ms steps. A wall or a ceiling sends it back at half the speed it hit with (a
// player's hull would stop dead: a ball against a wall would stick there); a landing stops its fall, like a player's.
// Returns whether it ends the call on the floor.
bool ballMove(Vec3& c, Vec3& v, const Map& map, float dt);
