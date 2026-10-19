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
constexpr uint16_t kProtocolVersion = 4;

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
    BTN_WEAPON1 = 1 << 6,
    BTN_WEAPON2 = 1 << 7,
    BTN_WEAPON3 = 1 << 8,
    BTN_WEAPON4 = 1 << 9,
    BTN_WEAPON5 = 1 << 10,
    BTN_DASH = 1 << 11,    // left shift
    BTN_GRAPPLE = 1 << 12, // q
};

// Weapon set. Rifle and knife are placeholders; sniper, laser and rocket are the real designs.
enum WeaponId : uint8_t { WPN_RIFLE = 0, WPN_ROCKET, WPN_KNIFE, WPN_SNIPER, WPN_LASER, WPN_COUNT };
struct WeaponDef {
    const char* name;
    float refire;       // seconds between shots
    int damage;
    float range;
    bool projectile;
    float projSpeed;
    float splashRadius;
    float spread;       // radians of random spread (hip fire)
    float scopedSpread; // radians when scoped (alt-fire held); < 0 = can't scope
    float zoomFov;      // horizontal fov while scoped
    bool beam;          // continuous beam (laser)
};
inline const WeaponDef& weaponDef(int w) {
    static const WeaponDef defs[WPN_COUNT] = {
        //  name      refire  dmg  range   proj  speed  splash spread  scoped  zoom  beam
        {"RIFLE",     0.12f,  18, 300.0f, false,  0,    0,     0.0f,   -1,     0,    false},
        {"ROCKET",    0.85f,  95,   0,    true,  32.0f, 3.6f,  0.0f,   -1,     0,    false},
        {"KNIFE",     0.45f,  55,   2.2f, false,  0,    0,     0.0f,   -1,     0,    false},
        {"SNIPER",    1.25f,  95, 500.0f, false,  0,    0,     0.06f,  0.0f,  24.0f, false},
        {"LASER",     0.05f,   5,  32.0f, false,  0,    0,     0.0f,   -1,     0,    true},
    };
    return defs[w < 0 || w >= WPN_COUNT ? 0 : w];
}

// Reliable game events, delivered in order to every client.
enum EventType : uint8_t {
    EV_KILL = 0,     // a = killer, b = victim, c = weapon
    EV_CHAT,         // a = sender, text
    EV_MESSAGE,      // server announcement, text
    EV_ROUND_WIN,    // a = winning team (or player for ffa/race), text
    EV_HIT,          // a = attacker, b = victim, c = damage
    EV_PLAYERINFO,   // a = player id, text = name
    EV_SPAWN,        // a = player id, c = yaw in milliradians (sent to the spawned player)
    EV_SOUND,        // reserved
};
