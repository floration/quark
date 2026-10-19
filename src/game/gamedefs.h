// Shared gameplay constants and enums (client + server).
#pragma once
#include <cstdint>

// Server tick rate is chosen at runtime: it starts at 1000 Hz and steps down
// (500, 250, 125) during warmup if the host can't keep up. A snapshot is sent every tick.
// Movement always simulates in fixed 1 ms steps, so it is identical at every rate.
constexpr int kTickRates[] = {1000, 500, 250, 125};
constexpr int kTickRateLevels = 4;
constexpr float kMoveStep = 0.001f;        // movement substep (s)
constexpr int kMaxPlayers = 8;
constexpr int kMaxProjectiles = 128;
constexpr uint16_t kDefaultPort = 27500;
constexpr uint32_t kProtocolMagic = 0x51524B31; // "QRK1"
constexpr uint16_t kProtocolVersion = 10;

// Player hull
constexpr float kPlayerHalfWidth = 0.4f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kPlayerCrouchHeight = 1.2f;
constexpr float kEyeHeight = 1.6f;
constexpr float kCrouchEyeHeight = 1.05f;
constexpr int kMaxHealth = 100;

enum GameModeId : uint8_t { MODE_VS = 0, MODE_BOMB, MODE_RACE, MODE_KOTH, MODE_PAYLOAD, MODE_FFA, MODE_NPAYLOAD, MODE_DUEL, MODE_COUNT };
inline bool isPayloadMode(int m) { return m == MODE_PAYLOAD || m == MODE_NPAYLOAD; }
inline const char* modeName(int m) {
    static const char* n[] = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel"};
    return (m >= 0 && m < MODE_COUNT) ? n[m] : "?";
}
inline const char* modeTitle(int m) {
    static const char* n[] = {"VS", "BOMB", "RACE", "KING OF THE HILL", "PAYLOAD", "FREE FOR ALL", "NEUTRAL PAYLOAD", "DUEL"};
    return (m >= 0 && m < MODE_COUNT) ? n[m] : "?";
}
inline int modeFromName(const char* s) {
    if (s[0] == '1' && s[1] == 'v' && s[2] == '1' && !s[3]) return MODE_DUEL; // old name
    for (int i = 0; i < MODE_COUNT; i++) {
        const char* a = modeName(i); const char* b = s;
        while (*a && *b && (*a == (*b | 32))) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

// DUEL (1v1 or 2v2): every life (round) is played under one modifier, picked at random each round.
enum DuelMod : uint8_t { DUEL_VAMPIRE_LASER = 0, DUEL_ROCKET, DUEL_KNIFE, DUEL_ONE_HP, DUEL_BIG, DUEL_GRAPPLE, DUEL_MOD_COUNT };
inline const char* duelModTitle(int m) {
    static const char* n[] = {"VAMPIRE LASER", "ROCKETS ONLY", "KNIVES ONLY", "1 HP", "DOUBLE SIZE", "GRAPPLE MOVEMENT ONLY"};
    return (m >= 0 && m < DUEL_MOD_COUNT) ? n[m] : "";
}
// Weapon a modifier forces, or -1 for free choice.
inline int duelModWeapon(int m) { return m == DUEL_VAMPIRE_LASER ? 4 : m == DUEL_ROCKET ? 1 : m == DUEL_KNIFE ? 2 : -1; }
constexpr float kBigScale = 2.0f; // DUEL_BIG hitbox / model scale

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

// Weapon set. Rifle and knife are placeholders. No single shot does more than 50 (of 100 health).
enum WeaponId : uint8_t {
    WPN_RIFLE = 0, WPN_ROCKET, WPN_KNIFE, WPN_SNIPER, WPN_LASER, WPN_SHOTGUN, WPN_STICKY, WPN_CROSSBOW, WPN_COUNT
};
constexpr uint8_t kNoWeaponChange = 255;
struct WeaponDef {
    const char* name;
    float refire;       // seconds between shots
    int damage;         // per pellet / per hit / at the center of a blast
    float range;        // hitscan range (m)
    bool projectile;
    float projSpeed;
    float splashRadius; // 0 = direct hit only
    float spread;       // radians of random spread (hip fire)
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

// Shot spread from the command's sequence number, so the client draws exactly the shot the server fired.
inline uint32_t spreadHash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline void spreadOffset(uint32_t seq, int pellet, float spread, float& a, float& r) {
    uint32_t h1 = spreadHash(seq * 31u + (uint32_t)pellet * 7919u + 1u), h2 = spreadHash(h1 ^ 0x9E3779B9u);
    a = (h1 & 0xFFFFFF) / 16777216.0f * 6.2831853f;
    r = __builtin_sqrtf((h2 & 0xFFFFFF) / 16777216.0f) * spread;
}
inline const WeaponDef& weaponDef(int w) {
    static const WeaponDef defs[WPN_COUNT] = {
        // Tuned by the owner (0.14.0). Perfect-aim time to kill from 100 hp in the trailing comment.
        //  name        refire   dmg  range   proj  speed  splash spread scoped zoom   beam  pel grav  sticky life
        {"RIFLE",       2.0f/9,   7, 300.0f, false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0, true}, // semi-auto, 15 shots, 3.1 s
        {"ROCKET",      1.0f,    45,   0,    true,  48.0f, 3.6f,  0.0f,  -1,     0,    false, 1,  12.0f, false, 5}, // 3, 2.0 s
        {"KNIFE",       1.0f,    40,   2.2f, false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0},  // 3, 2.0 s
        {"SNIPER",      4.0f,    50, 500.0f, false,  0,    0,     0.09f, 0.0f, 24.0f,  false, 1,  0,    false, 0},  // 2, 4.0 s
        {"LASER",       0.02f,    1,  32.0f, false,  0,    0,     0.0f,  -1,     0,    true,  1,  0,    false, 0},  // 100 ticks, 2.0 s
        {"SHOTGUN",     2.0f,     5,  60.0f, false,  0,    0,     0.085f,-1,     0,    false, 10, 0,    false, 0},  // 2 full blasts, 2.0 s
        {"STICKY",      1.0f,    35,   0,    true,  22.0f, 3.4f,  0.0f,  -1,     0,    false, 1,  15.0f, true, 30}, // 3, 2.0 s
        {"CROSSBOW",    1.5f,    50,   0,    true,  90.0f, 0,     0.0f,  0.0f, 70.0f,    false, 1,  6.0f, false, 3},  // 2, 1.5 s
    };
    return defs[w < 0 || w >= WPN_COUNT ? 0 : w];
}
constexpr int kMaxStickies = 6;       // per player; firing a 7th removes the oldest
constexpr float kStickyArmTime = 0.35f;

// Reliable game events, delivered in order to every client.
enum EventType : uint8_t {
    EV_KILL = 0,     // a = killer, b = victim, c = weapon
    EV_CHAT,         // a = sender, text
    EV_MESSAGE,      // server announcement, text
    EV_ROUND_WIN,    // a = winning team (or player for ffa/race), text
    EV_HIT,          // a = attacker, b = victim, c = damage
    EV_PLAYERINFO,   // a = player id, text = name
    EV_SPAWN,        // a = player id, c = yaw in milliradians (sent to the spawned player)
    EV_VOTE,         // text = what's being voted on (vote started)
    EV_SOUND,        // reserved
};
