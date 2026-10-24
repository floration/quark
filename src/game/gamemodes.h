// Game mode rules: the engine's phase machine (warmup, freeze, live, round end, match end) and, for each of the 13
// modes, its rules as slots (the mode editor's design, design.md 2.3): event slots the engine calls at fixed points
// (matchStart, roundSetup, tick, kill, roundOver, ...) and query slots it asks (roundLength, respawnDelay, canDamage,
// frozen, ...). Each slot has a native version per mode (gamemodes.cpp) that reads its numbers from the server's
// ModeParams (modeparams.h). Objects (the bomb, the carts, the points, the race, the stalemate drain, "it", the rock
// paper scissors tiebreak, the tourney bracket, duel modifiers) are native mechanics the slots drive; they raise
// events (plant, defuse, explode, cartCheckpoint, delivered, capture, checkpoint, finish) that are slots too.
//
// A custom mode's logic (phase 3) replaces some slots: the server then holds a ModeHooks, and each slot first tests
// it (one null test, predicted not taken; built-in modes never have one).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
class Server;
struct Map;
struct SpawnPoint;
struct ModeParams;

bool modeIsTeamBased(int mode);
void rulesReset(Server& s);                       // back to warmup
void rulesTick(Server& s);                        // once per server tick
void rulesForceStart(Server& s);                  // start the match now, even short-handed
// A player died (Server::killPlayer, after the engine's stats and EV_KILL): the objects' death hooks (the bomb drops),
// then the kill slot (scores; free for all overtime). weapon: the WPN_* / damage source (-1 unknown).
void rulesOnDeath(Server& s, int victim, int killer, int weapon = -1);
void rulesOnLeave(Server& s, int id);
void rulesEndMatch(Server& s, const std::string& text, int winner); // end the match now (a ranked leaver decided it)
void rulesOnSpectate(Server& s, int id);          // a player is about to switch to spectator
bool rulesTeamLocked(const Server& s);            // no joining a side now (a tag match is being played)
float rulesRespawnDelay(const Server& s, int id = -1);  // < 0 means no respawn (id: the player, -1 any)
bool rulesCanDamage(const Server& s, int attacker, int victim, int weapon = -1);
// A hit from attacker on victim lands at all (before knockback): race racers never touch each other, and in hide and
// seek / tag only "it" can touch anyone, with the knife, while live. Then, in those modes, the hit is a stab.
bool rulesCanHit(const Server& s, int attacker, int victim, int weapon);
void rulesStab(Server& s, int attacker, int victim);   // hide and seek / tag: "it" knifed someone (a find / tag passed on)
bool rulesFrozen(const Server& s, int id);         // movement locked (freeze time / planting)
int rulesSpawnType(const Server& s, int id);       // SPAWN_*
// The spawn type (SPAWN_*) a player of `team` (TEAM_*) draws from in `mode` on map m; attackers: the attacking team
// (payload, multi point); tourneyMatch: a tourney match is being played. (rulesSpawnType for a server's player.)
int modeSpawnType(const Map& m, int mode, int team, int attackers, bool tourneyMatch);
// The spawns Server::spawnPlayer picks from for spawn type `want`: a staged type uses the latest stage reached
// (pointsTaken: points / cart checkpoints taken) that has spawns for that side; an empty pool falls back to the
// `any` spawns, then to every spawn. The map tests check every offered mode's pools with this (REVIEW MAP-2).
std::vector<const SpawnPoint*> spawnPool(const Map& m, int want, int pointsTaken);
void rulesRpsPick(Server& s, int id, int shape);   // hide and seek tiebreak: a player chose (RpsShape; "rps" command)

// ---- the slots (design.md 2.3) and the hook a custom mode's logic plugs in (phase 3)
enum ModeSlot : uint8_t {
    // event slots (statements); their arguments in ModeHooks::event's a, b, c
    MSLOT_RESET,          // back to warmup (rulesReset)
    MSLOT_WARMUP,         // each warmup tick, before the autostart check
    MSLOT_MATCH_START,    // a match starts, before round 1 (after the scores and stats are zeroed)
    MSLOT_ROUND_SETUP,    // a round starts: after the round's resets, before the spawns
    MSLOT_ROUND_START,    // after the spawns, as the freeze begins
    MSLOT_LIVE,           // the freeze ended: the round is live
    MSLOT_TIMER,          // each live tick before `tick`: the logic's timers (no native version)
    MSLOT_TICK,           // each live tick (the engine has set ModeState::timeLeft to the round clock, if any)
    MSLOT_TIME_UP,        // each live tick after `tick` while the round clock has run out (no native version)
    MSLOT_KILL,           // a = victim, b = killer (-1 the world), c = weapon / damage source
    MSLOT_STAB,           // a = attacker ("it"), b = victim
    MSLOT_LEAVE,          // a = the player leaving
    MSLOT_SPECTATE,       // a = the player going to spectate
    MSLOT_ROUND_OVER,     // the round-end screen is over: end the match, or the next round starts
    MSLOT_PLANT,          // bomb: a = the planter
    MSLOT_DEFUSE,         // bomb: a = the defuser
    MSLOT_EXPLODE,        // bomb: the blast (everyone in its radius is dead already)
    MSLOT_CART_CHECKPOINT,// payload: a = checkpoints reached (1 = the first)
    MSLOT_DELIVERED,      // payload: the cart reached the end
    MSLOT_CAPTURE,        // multi point: a = the index of the point just taken
    MSLOT_CHECKPOINT,     // race: a = the player, b = checkpoints passed (1 = the first)
    MSLOT_FINISH,         // race: a = the player through the last checkpoint
    // query slots (return a value)
    MSLOT_FREEZE_LENGTH,  // s (f32)
    MSLOT_ROUND_LENGTH,   // s (f32), 0 = no clock
    MSLOT_ROUNDS_TO_WIN,  // int
    MSLOT_RESPAWN_DELAY,  // a = the player (-1 any): s (f32), < 0 no respawn
    MSLOT_CAN_DAMAGE,     // a = attacker (-1 the world), b = victim, c = weapon: bool
    MSLOT_CAN_HIT,        // a = attacker, b = victim, c = weapon: bool
    MSLOT_FROZEN,         // a = the player: bool (asked once a tick for everyone, kept in ModeHooks::frozenMask)
    MSLOT_SPAWN_TYPE,     // a = the player: SPAWN_*
    MSLOT_TEAM_LOCKED,    // bool
    MSLOT_CAN_AUTO_START, // bool
    MSLOT_COUNT
};
static_assert(MSLOT_COUNT <= 64, "ModeHooks::interp is a 64-bit mask");
const char* modeSlotName(int slot);   // the .qlogic name ("matchStart", "cartCheckpoint", ...); "?" out of range

// What a custom mode's logic gives the server (phase 3: the logic runtime implements it). Server::modeHooks_ is null
// for every built-in mode, and then each slot is one null test away from the native code.
struct ModeHooks {
    uint64_t interp = 0;                   // bit per ModeSlot: run through the logic; the rest stay native
    const ModeParams* params = nullptr;    // the mode's numbers for the native slots (null: the base mode's built-in ones)
    uint32_t frozenMask = 0;               // MSLOT_FROZEN's answers this tick (bit per player), filled by rulesTick
    virtual ~ModeHooks() = default;
    // An event slot. When it ends the round or the match (the phase changes), the caller returns, as the native code
    // does after an endRound.
    virtual void event(Server& s, ModeSlot slot, int a, int b, int c) = 0;
    // A query slot's value: f32 / int / bool / SPAWN_* as a double.
    virtual double query(const Server& s, ModeSlot slot, int a, int b, int c) = 0;
    bool has(ModeSlot k) const { return interp >> k & 1; }
};
