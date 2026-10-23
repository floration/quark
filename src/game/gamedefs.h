// Shared gameplay constants and enums (client + server).
#pragma once
#include <cstdint>
#include <cmath>
#include "../common/qmath.h"

// Server tick rate is chosen at runtime: it starts at 1000 Hz and steps down
// (500, 250, 125) in warmup or between rounds if the host can't keep up (mid-round only if it can't keep up at all).
// A snapshot is sent every tick.
// Movement and projectiles always simulate in fixed 1 ms steps, so they are identical at every rate.
constexpr int kTickRates[] = {1000, 500, 250, 125};
constexpr int kTickRateLevels = 4;
constexpr float kMoveStep = 0.001f;        // movement / projectile substep (s)
constexpr int kMaxPlayers = 32;        // engine limit; servers default to 8 (ServerConfig::maxPlayers)
constexpr int kMaxProjectiles = 128;
constexpr uint16_t kDefaultPort = 27500;
constexpr int kMaxInterpMs = 200;      // the longest fixed interpolation delay a client can set (interp_ms; lag compensation covers it)
constexpr uint32_t kProtocolMagic = 0x51524B31; // "QRK1"
constexpr uint16_t kProtocolVersion = 37;   // 37: maps named by sha256 (accept, map requests and data carry 32 byte ids; edit events the first 16 of the id they leave); 36: the locator is a weapon (WPN_LOCATOR; hide and seek / tag only), EV_RPS; 35: weapons (syringeAway, projectile lob/stuck flags) + NET-10 (projectiles out of sight sent as an id, hidden players carry no health / weapon); projectile bits: 1 new, 2 velocity, 4 lob, 8 stuck, 16 out of sight; 33: fixed weapon spread pattern (NET-9), clients report a fixed interp delay ("interp"); 32: ranked signatures name the dialled server address (REVIEW 2.2)
constexpr uint16_t kSnapSyringeSince = 35;  // the snapshot header carries syringeAway from this protocol on (older demos are read without it)
constexpr uint16_t kOldestDemoProtocol = 33;  // demos recorded at this protocol or later still play (raise it when a wire change can't read older ones)
// PlayerSnap::keys: what a player is holding (spectators see it on the key overlay)
enum : uint16_t { KEY_FWD = 1, KEY_BACK = 2, KEY_LEFT = 4, KEY_RIGHT = 8, KEY_JUMP = 16, KEY_CROUCH = 32, KEY_FIRE = 64, KEY_ALT = 128, KEY_DASH = 256, KEY_HOOK = 512 };

// Player hull
constexpr float kPlayerHalfWidth = 0.4f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kPlayerCrouchHeight = 1.2f;
constexpr float kEyeHeight = 1.6f;
constexpr float kCrouchEyeHeight = 1.05f;
constexpr int kMaxHealth = 100;

enum GameModeId : uint8_t { MODE_VS = 0, MODE_BOMB, MODE_RACE, MODE_KOTH, MODE_PAYLOAD, MODE_FFA, MODE_NPAYLOAD, MODE_DUEL, MODE_ARENA, MODE_TOURNEY, MODE_HIDE, MODE_TAG, MODE_POINTS, MODE_COUNT };
inline bool isPayloadMode(int m) { return m == MODE_PAYLOAD || m == MODE_NPAYLOAD; }
constexpr int kPlayerModels = 5;          // STANDARD, HEAVY, SCOUT, ROBOT, KNIGHT: cosmetic, every one has the same hitbox
constexpr int kSoundPacks = 5;            // DEFAULT, HEAVY METAL, MAGICAL, RETRO, WOODEN
inline const char* playerModelName(int m) { static const char* n[] = {"STANDARD", "HEAVY", "SCOUT", "ROBOT", "KNIGHT"}; return n[(m < 0 || m >= kPlayerModels) ? 0 : m]; }
// Seconds a player waits to respawn during play, or < 0 where nobody does: rounds with one life (VS, bomb, duel,
// 1v1, tourney, hide and seek). The one list behind modeRespawns() and rulesRespawnDelay() (warmup / match end aside).
inline float modeRespawnDelay(int m) {
    switch (m) {
        case MODE_RACE: return 1.0f;
        case MODE_KOTH: return 4.0f;
        case MODE_POINTS: return 5.0f;
        case MODE_PAYLOAD:
        case MODE_NPAYLOAD: return 5.0f;
        case MODE_FFA: return 2.0f;
        case MODE_TAG: return 2.0f;   // fell off something: back in (eliminations don't respawn)
        default: return -1.0f;
    }
}
inline bool modeRespawns(int m) { return modeRespawnDelay(m) >= 0; }
inline bool isItMode(int m) { return m == MODE_HIDE || m == MODE_TAG; } // one player is "it" (seeker / tagger) with the knife
constexpr float kHideTime = 30.0f;       // hide and seek: hiders' head start while the seeker faces the wall
constexpr float kHideRoundTime = 300.0f; // hiders win if anyone lasts this long
constexpr float kTagRoundTime = 60.0f;   // tag: whoever is "it" when this runs out is eliminated
constexpr float kTagFreeze = 2.0f;       // a newly tagged player stands still this long (no instant tag-back)
// Hide and seek tiebreak (owner B8): once every round is played, players tied for first throw rock paper scissors.
// Two: best of 3 (a draw is thrown again). Three or more throw at once: when exactly two shapes show, everyone with
// the losing one is out; one shape or all three, nobody is (throw again); down to two, best of 3.
enum RpsShape : uint8_t { RPS_NONE = 0, RPS_ROCK, RPS_PAPER, RPS_SCISSORS, RPS_PICKED };   // RPS_PICKED: chosen, not shown yet
enum RpsStage : uint8_t { RPS_PICK = 0, RPS_REVEAL, RPS_DONE };
constexpr float kRpsPickTime = 6.0f;     // to choose (no pick by then: a random one)
constexpr float kRpsRevealTime = 3.0f;   // the throw shown to everyone before the next
inline const char* rpsName(int s) { static const char* n[] = {"-", "ROCK", "PAPER", "SCISSORS", "?"}; return n[s >= 0 && s <= RPS_PICKED ? s : 0]; }
inline bool rpsBeats(int a, int b) { return (a == RPS_ROCK && b == RPS_SCISSORS) || (a == RPS_PAPER && b == RPS_ROCK) || (a == RPS_SCISSORS && b == RPS_PAPER); }
// The shape that's out when exactly two were thrown; RPS_NONE when nobody is (one shape, or all three).
inline int rpsLosingShape(const uint8_t* picks, int n) {
    bool has[4] = {};
    for (int i = 0; i < n; i++) if (picks[i] >= RPS_ROCK && picks[i] <= RPS_SCISSORS) has[picks[i]] = true;
    if (has[1] + has[2] + has[3] != 2) return RPS_NONE;
    for (int a = RPS_ROCK; a <= RPS_SCISSORS; a++)
        for (int b = RPS_ROCK; b <= RPS_SCISSORS; b++) if (has[a] && has[b] && rpsBeats(a, b)) return b;
    return RPS_NONE;
}
inline const char* modeName(int m) {
    static const char* n[] = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"};
    return (m >= 0 && m < MODE_COUNT) ? n[m] : "?";
}
inline const char* modeTitle(int m) {
    static const char* n[] = {"VS", "BOMB", "RACE", "KING OF THE HILL", "PAYLOAD", "FREE FOR ALL", "NEUTRAL PAYLOAD", "DUEL", "1V1", "TOURNEY", "HIDE AND SEEK", "TAG", "MULTI POINT"};
    return (m >= 0 && m < MODE_COUNT) ? n[m] : "?";
}
inline int modeFromName(const char* s) {
    for (int i = 0; i < MODE_COUNT; i++) {
        const char* a = modeName(i); const char* b = s;
        while (*a && *b && (*a == (*b | 32))) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}


// 1V1 mode: one of these rulesets (chosen by the host or a vote). Rounds, one life each, first to 5.
enum ArenaRule : uint8_t {
    AR_VAMPIRE = 1,        // damage you deal heals you
    AR_INSTAGIB = 2,       // any hit kills
    AR_NO_SELF_DAMAGE = 4, // your own splash only pushes you
    AR_QUICK_DASH = 8,     // dash recharges 3x faster
    AR_LOW_GRAVITY = 16,   // 40% gravity
};
constexpr float kLocatorCooldown = 5.0f, kLocatorShow = 1.0f; // the locator (WPN_LOCATOR, held by the seeker / tagger): fire it to see one random hider / runner this long, every kLocatorCooldown
constexpr int KILL_HEADSHOT = 0x100, KILL_AIRBORNE = 0x200, KILL_WEAPON = 0xFF; // EV_KILL c: weapon in the low byte
#define WB(w) (1u << (w))

// DUEL (1v1 or 2v2): every life (round) is played under one of these, picked at random (never the same twice
// in a row). First to 11. weapons: allowed pool (0 = all); rules: AR_*; size scales the hitbox and model.
struct DuelModDef { const char* title; uint16_t weapons; uint8_t rules; float health; float size; uint8_t moveMods; float damageMul; };
constexpr uint8_t AR_REGEN = 32;   // (duel) 10 hp/s back after 3 s without taking damage
inline const DuelModDef& duelModDef(int m);
struct ArenaVariant { const char* name; const char* title; const char* desc; uint16_t weapons; uint8_t rules; };
constexpr int kArenaVariants = 9;
inline const ArenaVariant& arenaVariant(int v) {
    // weapon ids: 0 rifle 1 rocket 2 knife 3 sniper 4 laser 5 shotgun 6 sticky 7 crossbow
    static const ArenaVariant vs[kArenaVariants] = {
        {"vanilla", "VANILLA", "EVERY WEAPON, ALL MOVEMENT - NO GIMMICKS", 0x1FF, 0},
        {"vampire", "LASER VAMPIRE", "LASERS ONLY - DAMAGE YOU DEAL HEALS YOU", WB(4), AR_VAMPIRE},
        {"classic", "CLASSIC", "ROCKET, LASER AND KNIFE", WB(1) | WB(4) | WB(2), 0},
        {"quirky", "QUIRKY", "CROSSBOW, STICKY AND RIFLE", WB(7) | WB(6) | WB(0), 0},
        {"hitscan", "HITSCAN", "RIFLE, SNIPER, LASER, SHOTGUN AND KNIFE", WB(0) | WB(3) | WB(4) | WB(5) | WB(2), 0},
        {"instagib", "INSTAGIB", "SNIPERS - ANY HIT KILLS", WB(3), AR_INSTAGIB},
        {"boomers", "BOOMERS", "ROCKETS AND STICKIES - NO SELF DAMAGE", WB(1) | WB(6), AR_NO_SELF_DAMAGE},
        {"blades", "BLADES", "KNIVES ONLY - DASH RECHARGES 3X FASTER", WB(2), AR_QUICK_DASH},
        {"moon", "MOON SHOTGUN", "SHOTGUNS IN LOW GRAVITY", WB(5), AR_LOW_GRAVITY},
    };
    return vs[v < 0 || v >= kArenaVariants ? 0 : v];
}
#undef WB
// Ranked ladders: 1v1 on one of these rulesets, each with its own rating.
inline bool arenaVariantRanked(int v) { return v == 0 || v == 1 || v == 5; } // vanilla (all weapons), laser vampire, instagib
inline int rankedMode(int v) { return v == 5 ? MODE_FFA : MODE_ARENA; }       // ranked instagib is a free for all, the rest 1v1
inline const char* rankedKind(int v) { return v == 5 ? "RANKED FFA" : "RANKED 1V1"; }
constexpr float kRankedFfaTime = 300.0f;                                       // ranked instagib: 5 minutes
inline int arenaVariantFromName(const char* s) {
    for (int i = 0; i < kArenaVariants; i++) {
        const char* a = arenaVariant(i).name; const char* b = s;
        while (*a && *b && (*a == (*b | 32))) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

enum Team : uint8_t { TEAM_A = 0, TEAM_B = 1, TEAM_SPECTATOR = 2 };

enum Phase : uint8_t {
    PHASE_WARMUP = 0,  // waiting for players, free play
    PHASE_FREEZE,      // round countdown, movement locked
    PHASE_LIVE,
    PHASE_ROUND_END,
    PHASE_MATCH_END,
};

// Input buttons
enum : uint16_t {
    BTN_JUMP = 1 << 0,
    BTN_CROUCH = 1 << 1,
    BTN_FIRE = 1 << 2,
    BTN_ALTFIRE = 1 << 3,
    BTN_USE = 1 << 4,      // plant / defuse
    BTN_RELOAD = 1 << 5,
    BTN_DASH = 1 << 11,    // left shift
    BTN_GRAPPLE = 1 << 12, // q
};

// Weapon set. Rifle and knife are placeholders. No body shot does more than 50 (of 100 health); headshots can (the crossbow bolt: 75).
enum WeaponId : uint8_t {
    WPN_RIFLE = 0, WPN_ROCKET, WPN_KNIFE, WPN_SNIPER, WPN_LASER, WPN_SHOTGUN, WPN_STICKY, WPN_CROSSBOW, WPN_BUBBLE, WPN_SYRINGE, WPN_LOCATOR, WPN_COUNT
};
constexpr uint8_t kNoWeaponChange = 255;
struct WeaponDef {
    const char* name;
    float refire;       // seconds between shots
    float damage;       // per pellet / per hit / at the center of a blast
    float range;        // hitscan range (m)
    bool projectile;
    float projSpeed;
    float splashRadius; // 0 = direct hit only
    float spread;       // radians of spread (hip fire; a fixed pattern: pelletOffset / burstOffset)
    float scopedSpread; // radians when scoped (alt-fire held); < 0 = can't scope
    float zoomFov;      // horizontal fov while scoped
    bool beam;          // continuous beam (laser)
    int pellets;        // hitscan rays per shot (shotgun)
    float gravity;      // projectile drop (m/s^2)
    bool sticky;        // projectile sticks to surfaces and is detonated with alt-fire
    float life;         // projectile lifetime (s)
    bool semiAuto = false; // one shot per click (a click up to 0.15 s early is kept until it can fire)
};
constexpr float kFireBuffer = 0.15f;
constexpr float kWallbangDamage = 0.5f; // hitscan through a wallbang wall
constexpr float kLaserPush = 20.0f;        // laser knockback (m/s per second of beam on target)
constexpr float kLaserPullScale = 1.82f;   // alt-fire pulls this much harder than the normal beam pushes
constexpr float kLaserPullDamage = 0.75f;  // ...for 75% damage

// Weapon spread is a fixed pattern, never seeded by anything the client picks (REVIEW NET-9: it came from the
// command's sequence number, and a client choosing its best numbers landed about twice the shotgun pellets).
// An offset is an angle `a` around the aim and a radius `r` (radians) off it.
// Pellets of one shot (shotgun, the bubble stream): one dead center, then rings around it. Every pellet covers an
// equal share of the spread disc (each ring sits in the middle of its equal-area annulus, pellets per ring in
// proportion to its size), so the density is that of the old uniform random spread (mean radius 2/3 of it);
// 20 pellets = 1 + 6 at 0.45 + 13 at 0.82 of the spread.
constexpr int kBurstPattern = 16;   // single-shot weapons with spread: shots per pattern cycle
inline void pelletOffset(int k, int count, float spread, float& a, float& r) {
    a = 0; r = 0;
    if (count <= 1 || spread <= 0 || k <= 0 || k >= count) return;
    int rest = count - 1;
    int rings = (int)std::lround((std::sqrt(1.0 + 4.0 * rest / 3.0) - 1.0) / 2.0);   // ~6, 12, 18... per ring
    if (rings < 1) rings = 1;
    int tri = rings * (rings + 1) / 2, before = 1;
    for (int j = 1; j <= rings; j++) {
        int n = j == rings ? count - before : (int)std::lround((double)rest * j / tri);
        if (n <= 0) continue;
        if (k < before + n) {
            r = spread * (float)std::sqrt((before + n * 0.5) / count);
            a = 1.5707963f + 6.2831853f * ((k - before) + ((j & 1) ? 0.0f : 0.5f)) / n;   // from the top; rings staggered
            return;
        }
        before += n;
    }
}
// A single-shot weapon with spread: the shot's number in a held burst (0 = the first after letting go of fire)
// walks a fixed sunflower sequence starting dead center, so consecutive shots land far apart and evenly.
inline void burstOffset(uint32_t shot, float spread, float& a, float& r) {
    uint32_t i = shot % kBurstPattern;
    a = 2.3999632f * (float)i;   // the golden angle
    r = spread <= 0 ? 0.0f : spread * std::sqrt((float)i / kBurstPattern);
}
// The direction of pellet `k` of `pellets` (burst shot `burst` when it's a single shot). The server fires this
// and the client draws its tracers with it, so they always match.
inline Vec3 spreadAim(const Vec3& aim, float spread, int k, int pellets, uint32_t burst) {
    if (spread <= 0) return aim;
    float a, r;
    if (pellets > 1) pelletOffset(k, pellets, spread, a, r);
    else burstOffset(burst, spread, a, r);
    if (r <= 0) return aim;
    Vec3 right = normalize(cross(aim, {0, 1, 0})), up = cross(right, aim);
    return normalize(aim + right * (std::cos(a) * r) + up * (std::sin(a) * r));
}
// The standard weapon table. weaponDef() is what everything uses: this, scaled by the active custom mode
// (customrules.h; identical when none is active).
inline const WeaponDef& baseWeaponDef(int w) {
    static const WeaponDef defs[WPN_COUNT] = {
        // Tuned by the owner (1.6.0: every cooldown 25% shorter except the laser's damage tick; every damage 15% lower
        // except laser and knife; crossbow 50 / 60 head on a 2 s cooldown; the bubble gun added. 1.17: rifle 1 less a
        // shot (4.95 body, 7.5 head; no spread, no falloff), laser range 32 -> 24 m, sniper a flat 50).
        // Perfect-aim time to kill from 100 hp in the trailing comment. DPS against the laser's 40: rifle 0.82 body,
        // 1.24 head; sniper 0.83 (the laser out-damages the rifle inside its 24 m unless you land headshots).
        //  name        refire   dmg    range   proj  speed  splash spread scoped zoom   beam  pel grav  sticky life
        {"RIFLE",       0.75f*2.0f/9.9f, 4.95f, 300.0f, false, 0, 0, 0.0f, 0.0f, 60.0f, false, 1, 0, false, 0},  // 21 shots, 3.0 s body; 14, 2.0 s head (scoped: 3/4 fire rate)
        {"ROCKET",      1.2f,    34,      0,    true,  48.0f, 3.6f,  0.0f,  -1,     0,    false, 1,  10.8f, false, 5}, // 3, 2.4 s (gravity 12 -> 10.8: owner, 1.17)
        {"KNIFE",       0.75f,   30,      3.5f, false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0},  // 4, 2.25 s (1.11: 30 every 0.75 s)
        {"SNIPER",      1.5f,    50,    500.0f, false,  0,    0,     0.0f,  0.0f, 45.0f,  false, 1,  0,    false, 0},  // 2 shots, 1.5 s (owner: a flat 50, head or body)
        {"LASER",       0.001f,  0.04f,  24.0f, false,  0,    0,     0.0f,  -1,     0,    true,  1,  0,    false, 0},  // every 1 ms, 2500 hits, 2.5 s, 24 m
        {"SHOTGUN",     0.75f,   1.7f,   60.0f, false,  0,    0,     0.085f,-1,     0,    false, 20, 0,    false, 0},  // 20 x 1.7 = 34 per blast, 3, 1.5 s
        {"STICKY",      0.75f,   29.75f,  0,    true,  22.0f, 3.4f,  0.0f,  -1,     0,    false, 1,  15.0f, true, 30}, // 4, 2.25 s
        {"CROSSBOW",    2.0f,    50,      0,    true,  76.5f, 0,     0.0f,  0.0f, 70.0f,  false, 1,  6.0f, false, 3},  // 2, 2.0 s (headshots 75: kCrossbowHeadDamage)
        {"BUBBLE",      1.0f/5,  10,      0,    true,  15.6f, 1.8f,  0.04f, -1,     0,    false, 1, -1.5f, false, 4}, // 10 pops, 1.8 s (floats up a little; 5 a second)
        {"SYRINGE",     1.0f,    10,      0,    false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0},  // heals you 10 hp a use, 1 s cooldown (damage = heal; owner 1.17)
        {"LOCATOR",     kLocatorCooldown, 0, 0, false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0},  // no damage: shows a hider / runner (hide and seek / tag only)
    };
    return defs[w < 0 || w >= WPN_COUNT ? 0 : w];
}
// Alt-fires: knife throw (a projectile that hits for knife damage), shotgun bouncing pellets (ricochet off
// walls, half damage), rifle scope (zooms like the sniper, 25% slower fire rate).
constexpr float kRocketDirectDamage = 50.0f; // a rocket that hits a player itself (not the floor next to them)
constexpr float kSniperHeadDamage = 50.0f; // hits on the head (headBox(): the top 20%, head-wide): sniper, crossbow bolt,
constexpr float kCrossbowHeadDamage = 75.0f; // rifle bullet and each shotgun pellet
constexpr float kRifleHeadDamage = 7.5f;   // (1.17: 8.5 -> 7.5 with the body 5.95 -> 4.95)
constexpr float kPelletHeadDamage = 2.55f;
const WeaponDef& weaponDef(int w);   // customrules.cpp
float tunedDamage(int w);            // the active custom mode's damage multiplier for this weapon (1 = standard)
// Movement settings of the active custom mode (pmove reads these; all 1 / on by default)
// Every movement constant a custom mode can scale (x the normal value; pmove.cpp / pmove.h hold the normal values)
enum MoveParam {
    MV_CROUCH_SPEED, MV_GROUND_ACCEL, MV_FRICTION, MV_STOP_SPEED, MV_AIR_ACCEL, MV_AIR_CAP, MV_OVERSTRAFE, MV_AIR_CONTROL,
    MV_STEP, MV_DASH_SPEED, MV_DASH_TIME, MV_DASH_EXIT_MIN, MV_DASH_KEEP, MV_DASH_EXIT_MAX, MV_DASH_VERTICAL, MV_DASH_COOLDOWN,
    MV_BOUNCE_GAIN, MV_BOUNCE_MAX, MV_HOOK_RANGE, MV_HOOK_ACCEL, MV_HOOK_MAX, MV_HOOK_GRAVITY, MV_HOOK_RELEASE, MV_HOOK_COOLDOWN,
    MV_LADDER_SPEED, MV_LADDER_SIDE, MV_COUNT
};
// (key in the .qmode file, label in the editor)
inline const char* moveParamKey(int i) {
    static const char* k[MV_COUNT] = {"crouch_speed", "ground_accel", "friction", "stop_speed", "air_accel", "air_cap", "overstrafe", "air_control",
        "step", "dash_speed", "dash_time", "dash_exit_min", "dash_keep", "dash_exit_max", "dash_vertical", "dash_cooldown",
        "bounce_gain", "bounce_max", "hook_range", "hook_accel", "hook_max", "hook_gravity", "hook_release", "hook_cooldown",
        "ladder_speed", "ladder_side"};
    return k[i];
}
inline const char* moveParamLabel(int i) {
    static const char* k[MV_COUNT] = {"CROUCH SPEED", "GROUND ACCELERATION", "GROUND FRICTION", "STOPPING SPEED", "AIR ACCELERATION", "AIR STRAFE CAP",
        "OVERSTRAFE BRAKE", "AIR CONTROL", "STEP HEIGHT", "DASH SPEED", "DASH LENGTH", "DASH EXIT SPEED (MIN)", "DASH SPEED KEPT",
        "DASH EXIT SPEED (MAX)", "DASH VERTICAL", "DASH COOLDOWN", "BHOP BOUNCE", "BHOP BOUNCE (MAX)", "GRAPPLE RANGE", "GRAPPLE PULL",
        "GRAPPLE TOP SPEED", "GRAPPLE GRAVITY", "GRAPPLE RELEASE DISTANCE", "GRAPPLE COOLDOWN", "LADDER CLIMB SPEED", "LADDER SIDE SPEED"};
    return k[i];
}
struct MoveTuning {
    float gravity = 1, speed = 1, jump = 1; bool dash = true, grapple = true;
    float m[MV_COUNT];
    MoveTuning() { for (float& v : m) v = 1; }
};
const MoveTuning& moveTuning();
inline float headDamage(int w) {
    return tunedDamage(w) * (w == WPN_SNIPER ? kSniperHeadDamage : w == WPN_CROSSBOW ? kCrossbowHeadDamage : w == WPN_RIFLE ? kRifleHeadDamage
         : w == WPN_SHOTGUN ? kPelletHeadDamage : 0.0f);
}
constexpr float kHeadFraction = 0.2f;       // the head: the top 20% of the hitbox...
constexpr float kHeadWidthFraction = 0.45f; // ...and 45% of its width, centred (0.36 m of 0.8): the shoulders beside it are body
constexpr float kRocketLobSpeed = 0.7f, kRocketLobGravity = 3.0f; // rocket alt-fire: a lob at 70% speed with 3x gravity
constexpr float kKnifeThrowSpeed = 38.0f, kKnifeThrowGravity = 14.0f, kKnifeThrowLife = 2.0f;
constexpr float kKnifeGrow = 0.04f;    // a swing hits boxes 10% wider (0.04 m each side of 0.8)
constexpr float kKnifeSwingTime = 0.12f; // ...and keeps cutting this long after the click if it hasn't hit yet
// Syringe: fire heals you (10 hp a use, its 1 s cooldown); alt-fire throws it: a teammate it hits gets 30 hp at once
// and every cooldown reset (weapon, dash, grapple, a thrown knife / syringe being away).
constexpr float kSyringeThrowSpeed = 30.0f, kSyringeThrowGravity = 9.0f, kSyringeThrowLife = 2.0f;
constexpr float kSyringeHeal = 30.0f;
// Throws (owner 1.17): no special or shared throw cooldown. A throw is a normal shot of that weapon: its own cooldown
// (knife: the 0.75 s swing; syringe: 1 s), during which no weapon can be used, and then the thrown weapon is
// disabled (away) this long from the throw: you switch to another weapon; it can't be drawn or used until it's back.
constexpr float kThrowAway = 4.0f;
constexpr float kKnifeAway = kThrowAway, kSyringeAway = kThrowAway;
inline bool isThrow(int w, bool alt) { return alt && (w == WPN_KNIFE || w == WPN_SYRINGE); }
// Projectiles fly in fixed kMoveStep (1 ms) steps, like movement, so an arc is the same at every server tick rate
// (C8); the client draws them with the same steps. One step: vel.y -= gravity * step, then pos += vel * step.
inline float projectileGravity(int type, float gravMul) {   // gravMul: a rocket lob's kRocketLobGravity, else 1
    return type == WPN_KNIFE ? kKnifeThrowGravity : type == WPN_SYRINGE ? kSyringeThrowGravity : weaponDef(type).gravity * gravMul;
}
// Where those steps take a projectile in t seconds (walls aren't checked): the whole steps summed in closed form (one
// pass, whatever t is), then the part of a step left over in a straight line. The client draws projectiles with it.
inline void projectileExtrapolate(Vec3& pos, Vec3& vel, float gravity, float t) {
    const int n = t > 0 ? (int)(t / kMoveStep) : 0;
    const float h = kMoveStep, rest = t - n * kMoveStep;
    pos += vel * (n * h);
    pos.y -= gravity * h * h * (0.5f * (float)n * (float)(n + 1));
    vel.y -= gravity * h * n;
    if (rest > 0) pos += vel * rest;
}
constexpr int kShotgunBounces = 2;
constexpr float kShotgunBounceDamage = 0.5f;
constexpr float kRifleScopedRefire = 4.0f / 3.0f; // refire multiplier while scoped (25% slower fire rate)
// Bubble gun alt-fire: a stream of bubbles all at once, each at its own speed, then a long reload.
// The stream: 5 bubbles with the nearest enemy within 5 m, up to 15 from 30 m away (so it can't one-shot up close),
// spread evenly from slow to fast.
constexpr int kBubbleStreamMin = 5, kBubbleStreamMax = 15;
constexpr float kBubbleStreamNear = 5.0f, kBubbleStreamFar = 30.0f;
constexpr float kBubbleStreamRefire = 1.8f;
constexpr float kBubbleStreamSlow = 8.4f, kBubbleStreamFast = 24.0f;
inline int bubbleStreamCount(float nearestEnemy) {
    float f = (nearestEnemy - kBubbleStreamNear) / (kBubbleStreamFar - kBubbleStreamNear);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return kBubbleStreamMin + (int)(f * (kBubbleStreamMax - kBubbleStreamMin) + 0.5f);
}
constexpr float kProjInherit = 0.9f;    // rockets / stickies keep 90% of the shooter's velocity (all of it: a rocket fired down while moving lands under you and can't boost you on)
constexpr float kCrossbowKnock = 16.0f; // a bolt hit shoves hard (m/s along the bolt)
constexpr float kBubbleKnock = 0.35f; // a pop shoves you a third as hard as a rocket blast of the same strength
constexpr float kVampireFadeTime = 60.0f, kVampireMinHeal = 0.5f; // laser vampire: healing fades 100% -> 50% over the first minute of a round
// The refire for this shot: alt-fire modes change it.
inline float shotRefire(int w, bool alt) {
    float r = weaponDef(w).refire;
    if (alt && w == WPN_RIFLE) return r * kRifleScopedRefire;       // scoped rifle
    if (alt && w == WPN_BUBBLE) return kBubbleStreamRefire;         // the bubble stream
    // (knife / syringe throws: the weapon's own cooldown, for every weapon: no throw + instant shot combo, GAME-8)
    return r;
}
// Slot order (number keys, scroll wheel, HUD list): the weapon ids themselves stay as they are.
constexpr int kWeaponSlots[WPN_COUNT] = {WPN_ROCKET, WPN_RIFLE, WPN_LASER, WPN_SHOTGUN, WPN_SNIPER, WPN_STICKY, WPN_CROSSBOW, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE, WPN_LOCATOR};
// The locator exists only in hide and seek and tag (owner B8): the seeker / tagger holds it with the knife.
inline bool weaponInMode(int w, int mode) { return w != WPN_LOCATOR || isItMode(mode); }
inline int weaponSlotKey(int slot) { return slot >= 9 ? 0 : slot + 1; }   // number key of a slot (the syringe and the locator share 0: never in one mode)
inline int weaponSlot(int w) { for (int i = 0; i < WPN_COUNT; i++) if (kWeaponSlots[i] == w) return i; return 0; }
inline bool altFires(int w) { return w == WPN_KNIFE || w == WPN_SHOTGUN || w == WPN_ROCKET || w == WPN_BUBBLE || w == WPN_SYRINGE; } // knife (throw), shotgun (bounce), rocket (lob): alt-fire is a shot of its own
constexpr int kMaxStickies = 6;       // per player; firing a 7th removes the oldest
constexpr float kStickyArmTime = 0.35f;

// Reliable game events, delivered in order to every client.
enum EventType : uint8_t {
    EV_KILL = 0,     // a = killer, b = victim, c = weapon | KILL_* flags
    EV_CHAT,         // a = sender, text
    EV_MESSAGE,      // server announcement, text
    EV_ROUND_WIN,    // a = winning team (or player for ffa/race), text
    EV_HIT,          // a = attacker, b = victim, c = damage
    EV_PLAYERINFO,   // a = player id, text = name
    EV_SPAWN,        // a = player id, c = yaw in milliradians (sent to the spawned player)
    EV_VOTE,         // text = what's being voted on (vote started)
    EV_SOUND,        // reserved
    EV_BRACKET,      // tourney bracket: text = rounds ("\n"), entries ("|"): player id, -1 bye, -2 not decided; last line "@round,match"
    EV_RANKINFO,     // a = player id, text = their ranked id (32 hex), empty = none
    EV_RANKED,       // ranked match over: text = "<matchId> <mode> slot:team:place,..." (each player reports it)
    EV_EDIT,         // map edit session: text = "<the map's id after it, first 16 bytes in hex> <op>" (see applyMapEdit, mapEditId)
    EV_RULES,        // custom mode: a = chunk index, b = chunk count, text = that piece of CustomRules::toText()
    EV_HEALED,       // syringe throw: a was healed by b
    EV_RACE_SPLIT,   // race: a = player, b = checkpoint reached (1-based; the last one is the finish), text = ms since the start
    EV_TYPING,       // a is typing in chat (others hear key clicks and see "..." over them)
    EV_QUIP,         // a says a line from the voice pack (b: which one, the same pick for everyone)
    EV_LOCATE,       // the locator fired (WPN_LOCATOR), to the tagger / seeker only: a = who, text = "x y z" where they are
    EV_RANKSIGN,     // ranked match live / time trial started: text = the match (race ticket) id; the game answers "ticketsig <signature>"
    EV_RPS,          // hide and seek tiebreak: a = RpsStage, b = throw number, text = "<best of 3> <winner or -1>" then " id:shape:wins:out"
                     // per player (shape RPS_PICKED until the reveal), then a line with what happened
};

#define WB(w) (1u << (w))
inline const DuelModDef& duelModDef(int m) {
    constexpr uint8_t G = 1, Q = 2, L = 4; // MOVE_GRAPPLE_ONLY, MOVE_QUICK_DASH, MOVE_LOW_GRAVITY (pmove.h)
    static const DuelModDef defs[] = {
        // title                   weapons                                   rules                      hp    size  move dmg
        {"VAMPIRE LASER",          WB(WPN_LASER),                            AR_VAMPIRE,                100, 1.0f, 0, 1.0f},
        {"ROCKETS ONLY",           WB(WPN_ROCKET),                           0,                         100, 1.0f, 0, 1.0f},
        {"KNIVES ONLY",            WB(WPN_KNIFE),                            0,                         100, 1.0f, 0, 1.0f},
        {"1 HP",                   0,                                        0,                           1, 1.0f, 0, 1.0f},
        {"DOUBLE SIZE",            0,                                        0,                         100, 2.0f, 0, 1.0f},
        {"GRAPPLE MOVEMENT ONLY",  0,                                        0,                         100, 1.0f, G, 1.0f},
        {"SNIPERS ONLY",           WB(WPN_SNIPER),                           0,                         100, 1.0f, 0, 1.0f},
        {"SHOTGUNS ONLY",          WB(WPN_SHOTGUN),                          0,                         100, 1.0f, 0, 1.0f},
        {"STICKIES ONLY",          WB(WPN_STICKY),                           0,                         100, 1.0f, 0, 1.0f},
        {"CROSSBOWS ONLY",         WB(WPN_CROSSBOW),                         0,                         100, 1.0f, 0, 1.0f},
        {"RIFLES ONLY",            WB(WPN_RIFLE),                            0,                         100, 1.0f, 0, 1.0f},
        {"LASERS ONLY",            WB(WPN_LASER),                            0,                         100, 1.0f, 0, 1.0f},
        {"INSTAGIB SNIPERS",       WB(WPN_SNIPER),                           AR_INSTAGIB,               100, 1.0f, 0, 1.0f},
        {"INSTAGIB CROSSBOWS",     WB(WPN_CROSSBOW),                         AR_INSTAGIB,               100, 1.0f, 0, 1.0f},
        {"MOON GRAVITY",           0,                                        AR_LOW_GRAVITY,            100, 1.0f, L, 1.0f},
        {"MOON ROCKETS",           WB(WPN_ROCKET),                           AR_LOW_GRAVITY,            100, 1.0f, L, 1.0f},
        {"ROCKET JUMPERS",         WB(WPN_ROCKET),                           AR_NO_SELF_DAMAGE,         100, 1.0f, 0, 1.0f},
        {"QUICK DASH",             0,                                        AR_QUICK_DASH,             100, 1.0f, Q, 1.0f},
        {"DASHING BLADES",         WB(WPN_KNIFE),                            AR_QUICK_DASH,             100, 1.0f, Q, 1.0f},
        {"TINY PLAYERS",           0,                                        0,                         100, 0.6f, 0, 1.0f},
        {"GIANT SNIPERS",          WB(WPN_SNIPER),                           0,                         100, 2.0f, 0, 1.0f},
        {"GLASS CANNONS",          0,                                        0,                          50, 1.0f, 0, 2.0f},
        {"TANKS",                  0,                                        0,                         250, 1.0f, 0, 1.0f},
        {"DOUBLE DAMAGE",          0,                                        0,                         100, 1.0f, 0, 2.0f},
        {"CLOSE QUARTERS",         WB(WPN_SHOTGUN) | WB(WPN_KNIFE),          0,                         100, 1.0f, 0, 1.0f},
        {"HITSCAN ONLY",           WB(WPN_RIFLE) | WB(WPN_SNIPER) | WB(WPN_LASER) | WB(WPN_SHOTGUN), 0, 100, 1.0f, 0, 1.0f},
        {"EXPLOSIVES",             WB(WPN_ROCKET) | WB(WPN_STICKY),          AR_NO_SELF_DAMAGE,         100, 1.0f, 0, 1.0f},
        {"REGENERATION",           0,                                        AR_REGEN,                  100, 1.0f, 0, 1.0f},
        {"1 HP KNIVES",            WB(WPN_KNIFE),                            0,                           1, 1.0f, 0, 1.0f},
        {"MOON SHOTGUNS",          WB(WPN_SHOTGUN),                          AR_LOW_GRAVITY,            100, 1.0f, L, 1.0f},
    };
    static const DuelModDef none = {"", 0, 0, 100, 1.0f, 0, 1.0f};
    return m >= 0 && m < (int)(sizeof defs / sizeof defs[0]) ? defs[m] : none;
}
#undef WB
constexpr int DUEL_MOD_COUNT = 30;
inline const char* duelModTitle(int m) { return duelModDef(m).title; }
