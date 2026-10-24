// The numbers behind each built-in mode's rules (the mode editor's `param` lines: design.md 2.6, 3).
//
// The native rules (gamemodes.cpp) read every number from the server's ModeParams table (Server::mp_): the
// built-in mode's table below, or, in phase 3 of the mode editor, a custom mode's own numbers. A custom mode that only
// changes numbers then still runs the native code: one L1 load in place of an immediate.
//
// QUARK_MODE_PARAMS(X): X(name, C type, logic type, unit, min, max, default, doc). The name is the `param` name the
// built-in .qlogic texts (tools/modes/*.qlogic) declare; the default is the common one, and kBuiltinModeParams[mode]
// holds each mode's own values (builtinModeParamsFor). Changing a default here changes the game.
#pragma once
#include "gamedefs.h"

#define QUARK_MODE_PARAMS(X) \
    /* every mode (common.qlogic) */ \
    X(freezeTime,      float, "f32", "s",    0,    600,  3.0f,  "countdown before each round") \
    X(roundEndTime,    float, "f32", "s",    0,    60,   4.0f,  "the round result on screen before the next round") \
    X(matchEndTime,    float, "f32", "s",    0,    120,  10.0f, "the match result on screen before the next match") \
    X(warmupCountdown, float, "f32", "s",    0,    60,   5.0f,  "from enough players to the start (autostart servers)") \
    X(warmupRespawn,   float, "f32", "s",    0,    60,   1.5f,  "dead in the warmup before respawning") \
    X(overtime,        float, "f32", "s",    0,    60,   5.0f,  "an objective touch holds the round open this long after the clock runs out") \
    /* per mode */ \
    X(rounds,          int,   "int", "",     1,    50,   1,     "rounds to win the match (the host's rounds key beats it)") \
    X(roundTime,       float, "f32", "s",    0,    3600, 0.0f,  "the round clock (0: none)") \
    X(respawnTime,     float, "f32", "s",    -1,   60,   -1.0f, "dead before respawning during play (< 0: one life a round)") \
    X(matchTime,       float, "f32", "s",    1,    3600, 600.0f, "free for all: the match clock when the host sets no time limit") \
    X(rankedTime,      float, "f32", "s",    1,    3600, kRankedFfaTime, "ranked free for all (instagib): the match clock") \
    X(target,          int,   "int", "",     1,    10000, 100,  "king of the hill: points to win when the host sets no score limit") \
    /* drain (vs, duel, 1v1, tourney) */ \
    X(stalemate,       float, "f32", "s",    1,    600,  30.0f, "no damage dealt this long: health starts draining") \
    X(drainRate,       float, "f32", "hp/s", 0,    100,  5.0f,  "the stalemate drain") \
    /* bomb */ \
    X(plantTime,       float, "f32", "s",    0.1f, 60,   3.0f,  "holding USE on a site, on the ground, to plant") \
    X(defuseTime,      float, "f32", "s",    0.1f, 60,   6.0f,  "holding USE by the bomb to defuse it") \
    X(bombFuse,        float, "f32", "s",    1,    600,  40.0f, "from the plant to the blast") \
    X(bombRadius,      float, "f32", "m",    0,    200,  18.0f, "the blast kills everyone this close") \
    X(defuseRange,     float, "f32", "m",    0.1f, 20,   1.8f,  "from the bomb to defuse it") \
    X(pickupRange,     float, "f32", "m",    0.1f, 20,   1.2f,  "from a dropped bomb for an attacker to take it") \
    X(bombReturn,      float, "f32", "s",    0,    600,  15.0f, "a dropped bomb lies this long, then goes back to a random attacker") \
    /* payload carts (payload; neutral payload uses range and speed) */ \
    X(cartRange,       float, "f32", "m",    0.5f, 50,   4.0f,  "from the cart to push (or block) it") \
    X(cartSpeed,       float, "f32", "m/s",  0.1f, 20,   1.6f,  "one pusher; each extra one +25%, at most twice as fast") \
    X(cartRollback,    float, "f32", "m/s",  0,    20,   0.6f,  "the cart rolls back this fast when left (never past a checkpoint)") \
    X(cartIdle,        float, "f32", "s",    0,    600,  10.0f, "left alone this long before it rolls back") \
    X(cartCheckBonus,  float, "f32", "s",    0,    600,  60.0f, "added to the clock at each cart checkpoint") \
    /* multi point */ \
    X(captureTime,     float, "f32", "s",    0.1f, 600,  12.0f, "one attacker alone takes a point in this long; more go faster (up to twice)") \
    X(captureDecay,    float, "f32", "/s",   0,    10,   0.05f, "a point the attackers leave loses this much capture a second") \
    X(pointBonus,      float, "f32", "s",    0,    600,  90.0f, "added to the clock for every point taken") \
    /* hide and seek, tag */ \
    X(hideTime,        float, "f32", "s",    0,    600,  kHideTime, "hide and seek: the hiders' head start (the freeze), the seeker facing the wall") \
    X(tagFreeze,       float, "f32", "s",    0,    60,   kTagFreeze, "tag: a newly tagged player stands still this long") \
    /* hide and seek tiebreak (rock paper scissors) */ \
    X(rpsPickTime,     float, "f32", "s",    1,    60,   kRpsPickTime, "to choose a shape") \
    X(rpsRevealTime,   float, "f32", "s",    0,    60,   kRpsRevealTime, "the throw shown to everyone before the next")

struct ModeParams {
#define QUARK_MP_FIELD(name, ctype, ltype, unit, lo, hi, def, doc) ctype name = def;
    QUARK_MODE_PARAMS(QUARK_MP_FIELD)
#undef QUARK_MP_FIELD
};

// Each built-in mode's numbers: the common defaults above, with the mode's own rounds, clock and respawn.
constexpr ModeParams builtinModeParamsFor(int mode) {
    ModeParams p;
    p.respawnTime = modeRespawnDelay(mode);
    switch (mode) {
        case MODE_VS: p.rounds = 5; break;
        case MODE_DUEL: p.rounds = 11; break;          // first to 11 lives
        case MODE_ARENA: p.rounds = 5; break;
        case MODE_TOURNEY: p.rounds = 3; break;        // each bracket match
        case MODE_BOMB: p.rounds = 7; p.roundTime = 105.0f; break;
        case MODE_RACE: p.rounds = 3; p.roundTime = 300.0f; break;   // two loops of Viaduct
        case MODE_PAYLOAD: p.roundTime = 240.0f; break;              // each team attacks once (rounds 1)
        case MODE_POINTS: p.roundTime = 150.0f; break;               // the attack's clock at the start
        case MODE_NPAYLOAD: p.roundTime = 300.0f; break;
        case MODE_HIDE: p.roundTime = kHideRoundTime; break;
        case MODE_TAG: p.roundTime = kTagRoundTime; break;
        default: break;                                              // koth (target), ffa (matchTime): no round clock
    }
    return p;
}

inline constexpr ModeParams kBuiltinModeParams[MODE_COUNT] = {
    builtinModeParamsFor(0), builtinModeParamsFor(1), builtinModeParamsFor(2), builtinModeParamsFor(3), builtinModeParamsFor(4),
    builtinModeParamsFor(5), builtinModeParamsFor(6), builtinModeParamsFor(7), builtinModeParamsFor(8), builtinModeParamsFor(9),
    builtinModeParamsFor(10), builtinModeParamsFor(11), builtinModeParamsFor(12),
};
static_assert(MODE_COUNT == 13, "a new mode needs its row in kBuiltinModeParams");
inline const ModeParams& builtinModeParams(int mode) { return kBuiltinModeParams[mode >= 0 && mode < MODE_COUNT ? mode : MODE_FFA]; }
