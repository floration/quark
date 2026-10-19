// Shared gameplay constants and enums (client + server).
#pragma once
#include <cstdint>

constexpr int kTickRate = 60;              // simulation + input rate (Hz)
constexpr float kTickDt = 1.0f / kTickRate;
constexpr int kSnapshotDivider = 2;        // snapshots at 30 Hz
constexpr int kMaxPlayers = 32;
constexpr int kMaxProjectiles = 128;
constexpr uint16_t kDefaultPort = 27500;
constexpr uint32_t kProtocolMagic = 0x51524B31; // "QRK1"
constexpr uint16_t kProtocolVersion = 1;

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
};

// Placeholder weapon set; real weapons will be defined later.
enum WeaponId : uint8_t { WPN_RIFLE = 0, WPN_ROCKET, WPN_KNIFE, WPN_COUNT };
struct WeaponDef {
    const char* name;
    float refire;     // seconds between shots
    int damage;
    float range;
    bool projectile;
    float projSpeed;
    float splashRadius;
};
inline const WeaponDef& weaponDef(int w) {
    static const WeaponDef defs[WPN_COUNT] = {
        {"RIFLE", 0.12f, 18, 300.0f, false, 0, 0},
        {"ROCKET", 0.9f, 90, 0, true, 30.0f, 3.5f},
        {"KNIFE", 0.45f, 55, 2.2f, false, 0, 0},
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
