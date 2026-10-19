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
constexpr uint16_t kProtocolVersion = 6;

// Player hull
constexpr float kPlayerHalfWidth = 0.4f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kPlayerCrouchHeight = 1.2f;
constexpr float kEyeHeight = 1.6f;
constexpr float kCrouchEyeHeight = 1.05f;
constexpr int kMaxHealth = 100;

enum GameModeId : uint8_t { MODE_VS = 0, MODE_BOMB, MODE_RACE, MODE_KOTH, MODE_PAYLOAD, MODE_FFA, MODE_COUNT };
inline const char* modeName(int m) {
    static const char* n[] = {"vs", "bomb", "race", "koth", "payload", "ffa"};
    return (m >= 0 && m < MODE_COUNT) ? n[m] : "?";
}
inline const char* modeTitle(int m) {
    static const char* n[] = {"VS", "BOMB", "RACE", "KING OF THE HILL", "PAYLOAD", "FREE FOR ALL"};
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

// Weapon set. Rifle and knife are placeholders.
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
};
inline const WeaponDef& weaponDef(int w) {
    static const WeaponDef defs[WPN_COUNT] = {
        //  name        refire dmg  range   proj  speed  splash spread scoped zoom   beam  pel grav  sticky life
        {"RIFLE",       0.12f, 18, 300.0f, false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0},
        {"ROCKET",      0.85f, 95,   0,    true,  32.0f, 3.6f,  0.0f,  -1,     0,    false, 1,  0,    false, 5},
        {"KNIFE",       0.45f, 55,   2.2f, false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0},
        {"SNIPER",      1.25f, 95, 500.0f, false,  0,    0,     0.06f, 0.0f, 24.0f,  false, 1,  0,    false, 0},
        {"LASER",       0.05f,  5,  32.0f, false,  0,    0,     0.0f,  -1,     0,    true,  1,  0,    false, 0},
        {"SHOTGUN",     0.85f,  9,  60.0f, false,  0,    0,     0.085f,-1,     0,    false, 10, 0,    false, 0},
        {"STICKY",      0.6f,  85,   0,    true,  22.0f, 3.4f,  0.0f,  -1,     0,    false, 1,  15.0f, true, 30},
        {"CROSSBOW",    1.1f,  85,   0,    true,  90.0f, 0,     0.0f,  -1,     0,    false, 1,  6.0f, false, 3},
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
