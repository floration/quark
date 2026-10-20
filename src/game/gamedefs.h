// Shared gameplay constants and enums (client + server).
#pragma once
#include <cstdint>

// Server tick rate is chosen at runtime: it starts at 1000 Hz and steps down
// (500, 250, 125) during warmup if the host can't keep up. A snapshot is sent every tick.
// Movement always simulates in fixed 1 ms steps, so it is identical at every rate.
constexpr int kTickRates[] = {1000, 500, 250, 125};
constexpr int kTickRateLevels = 4;
constexpr float kMoveStep = 0.001f;        // movement substep (s)
constexpr int kMaxPlayers = 32;        // engine limit; servers default to 8 (ServerConfig::maxPlayers)
constexpr int kMaxProjectiles = 128;
constexpr uint16_t kDefaultPort = 27500;
constexpr uint32_t kProtocolMagic = 0x51524B31; // "QRK1"
constexpr uint16_t kProtocolVersion = 21;

// Player hull
constexpr float kPlayerHalfWidth = 0.4f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kPlayerCrouchHeight = 1.2f;
constexpr float kEyeHeight = 1.6f;
constexpr float kCrouchEyeHeight = 1.05f;
constexpr int kMaxHealth = 100;

enum GameModeId : uint8_t { MODE_VS = 0, MODE_BOMB, MODE_RACE, MODE_KOTH, MODE_PAYLOAD, MODE_FFA, MODE_NPAYLOAD, MODE_DUEL, MODE_ARENA, MODE_TOURNEY, MODE_HIDE, MODE_TAG, MODE_COUNT };
inline bool isPayloadMode(int m) { return m == MODE_PAYLOAD || m == MODE_NPAYLOAD; }
constexpr int kPlayerModels = 5;          // STANDARD, HEAVY, SCOUT, ROBOT, KNIGHT: cosmetic, every one has the same hitbox
constexpr int kSoundPacks = 5;            // STANDARD, ARCADE, HEAVY, RETRO, SOFT
inline const char* playerModelName(int m) { static const char* n[] = {"STANDARD", "HEAVY", "SCOUT", "ROBOT", "KNIGHT"}; return n[(m < 0 || m >= kPlayerModels) ? 0 : m]; }
inline bool isItMode(int m) { return m == MODE_HIDE || m == MODE_TAG; } // one player is "it" (seeker / tagger) with the knife
constexpr float kHideTime = 30.0f;       // hide and seek: hiders' head start while the seeker faces the wall
constexpr float kHideRoundTime = 300.0f; // hiders win if anyone lasts this long
constexpr float kTagRoundTime = 60.0f;   // tag: whoever is "it" when this runs out is eliminated
constexpr float kTagFreeze = 2.0f;       // a newly tagged player stands still this long (no instant tag-back)
inline const char* modeName(int m) {
    static const char* n[] = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag"};
    return (m >= 0 && m < MODE_COUNT) ? n[m] : "?";
}
inline const char* modeTitle(int m) {
    static const char* n[] = {"VS", "BOMB", "RACE", "KING OF THE HILL", "PAYLOAD", "FREE FOR ALL", "NEUTRAL PAYLOAD", "DUEL", "1V1", "TOURNEY", "HIDE AND SEEK", "TAG"};
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
        {"vanilla", "VANILLA", "EVERY WEAPON, ALL MOVEMENT - NO GIMMICKS", 0xFF, 0},
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

// Weapon set. Rifle and knife are placeholders. No single shot does more than 50 (of 100 health).
enum WeaponId : uint8_t {
    WPN_RIFLE = 0, WPN_ROCKET, WPN_KNIFE, WPN_SNIPER, WPN_LASER, WPN_SHOTGUN, WPN_STICKY, WPN_CROSSBOW, WPN_COUNT
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
constexpr float kWallbangDamage = 0.5f; // hitscan through a wallbang wall
constexpr float kLaserPush = 20.0f;        // laser knockback (m/s per second of beam on target)
constexpr float kLaserPullScale = 1.3f;    // alt-fire pulls this much harder than the normal beam pushes
constexpr float kLaserPullDamage = 0.75f;  // ...for 75% damage

// Shot spread from the command's sequence number, so the client draws exactly the shot the server fired.
inline uint32_t spreadHash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline void spreadOffset(uint32_t seq, int pellet, float spread, float& a, float& r) {
    uint32_t h1 = spreadHash(seq * 31u + (uint32_t)pellet * 7919u + 1u), h2 = spreadHash(h1 ^ 0x9E3779B9u);
    a = (h1 & 0xFFFFFF) / 16777216.0f * 6.2831853f;
    r = __builtin_sqrtf((h2 & 0xFFFFFF) / 16777216.0f) * spread;
}
inline const WeaponDef& weaponDef(int w) {
    static const WeaponDef defs[WPN_COUNT] = {
        // Tuned by the owner (0.15.1). Perfect-aim time to kill from 100 hp in the trailing comment.
        //  name        refire   dmg    range   proj  speed  splash spread scoped zoom   beam  pel grav  sticky life
        {"RIFLE",       2.0f/9.9f, 7,     300.0f, false,  0,    0,     0.0f,  0.0f, 60.0f,  false, 1,  0,    false, 0},  // 15 shots, 2.8 s (scoped: 3/4 fire rate)
        {"ROCKET",      1.6f,    40,      0,    true,  48.0f, 3.6f,  0.0f,  -1,     0,    false, 1,  12.0f, false, 5}, // 3, 3.2 s
        {"KNIFE",       0.7f,    40,      2.8f, false,  0,    0,     0.0f,  -1,     0,    false, 1,  0,    false, 0},  // 3, 1.4 s
        {"SNIPER",      2.0f,    40,    500.0f, false,  0,    0,     0.09f, 0.0f, 45.0f,  false, 1,  0,    false, 0},  // 3, 4.0 s (headshots 50: 2, 2.0 s)
        {"LASER",       0.001f,  0.04f,  32.0f, false,  0,    0,     0.0f,  -1,     0,    true,  1,  0,    false, 0},  // every 1 ms, 2500 hits, 2.5 s
        {"SHOTGUN",     1.0f,    2,      60.0f, false,  0,    0,     0.085f,-1,     0,    false, 20, 0,    false, 0},  // 20 x 2 = 40 per blast, 3, 2.0 s
        {"STICKY",      1.0f,    35,      0,    true,  22.0f, 3.4f,  0.0f,  -1,     0,    false, 1,  15.0f, true, 30}, // 3, 2.0 s
        {"CROSSBOW",    2.0f,    50,      0,    true,  76.5f, 0,     0.0f,  0.0f, 70.0f,  false, 1,  6.0f, false, 3},  // 2, 2.0 s
    };
    return defs[w < 0 || w >= WPN_COUNT ? 0 : w];
}
// Alt-fires: knife throw (a projectile that hits for knife damage), shotgun bouncing pellets (ricochet off
// walls, half damage), rifle scope (zooms like the sniper, 25% slower fire rate).
constexpr float kSniperHeadDamage = 50.0f; // sniper hits on the head (top 20% of the hitbox)
constexpr float kHeadFraction = 0.2f;
constexpr float kRocketLobSpeed = 0.7f, kRocketLobGravity = 3.0f; // rocket alt-fire: a lob at 70% speed with 3x gravity
constexpr float kKnifeThrowSpeed = 38.0f, kKnifeThrowGravity = 14.0f, kKnifeThrowLife = 2.0f;
constexpr float kKnifeAway = 5.0f; // after a throw the knife is gone this long: you switch to another weapon (no delay on it)
constexpr int kShotgunBounces = 2;
constexpr float kShotgunBounceDamage = 0.5f;
constexpr float kRifleScopedRefire = 4.0f / 3.0f; // refire multiplier while scoped (25% slower fire rate)
constexpr float kVampireFadeTime = 60.0f, kVampireMinHeal = 0.5f; // laser vampire: healing fades 100% -> 50% over the first minute of a round
// The refire for this shot: alt-fire modes change it.
inline float shotRefire(int w, bool alt) {
    float r = weaponDef(w).refire;
    if (alt && w == WPN_KNIFE) return 0;                    // knife throw: no delay (you switch weapons)
    if (alt && w == WPN_RIFLE) return r * kRifleScopedRefire;       // scoped rifle
    return r;
}
// Slot order (number keys, scroll wheel, HUD list): the weapon ids themselves stay as they are.
constexpr int kWeaponSlots[WPN_COUNT] = {WPN_ROCKET, WPN_RIFLE, WPN_LASER, WPN_SHOTGUN, WPN_SNIPER, WPN_STICKY, WPN_CROSSBOW, WPN_KNIFE};
inline int weaponSlot(int w) { for (int i = 0; i < WPN_COUNT; i++) if (kWeaponSlots[i] == w) return i; return 0; }
inline bool altFires(int w) { return w == WPN_KNIFE || w == WPN_SHOTGUN || w == WPN_ROCKET; } // knife (throw), shotgun (bounce), rocket (lob): alt-fire is a shot of its own
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
    EV_BRACKET,      // tourney bracket: text = rounds ("\n"), entries ("|"): player id, -1 bye, -2 not decided; last line "@round,match"
    EV_RANKINFO,     // a = player id, text = their ranked id (32 hex), empty = none
    EV_RANKED,       // ranked match over: text = "<matchId> <mode> slot:team:place,..." (each player reports it)
    EV_EDIT,         // map edit session: text = "<crcBefore hex> <crcAfter hex> <op>" (see applyMapEdit)
    EV_RACE_SPLIT,   // race: a = player, b = checkpoint reached (1-based; the last one is the finish), text = ms since the start
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
