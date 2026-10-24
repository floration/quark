// Game mode rules: the engine's phase machine (warmup, freeze, live, round end, match end) and, for each of the 13
// modes, its rules as slots (the mode editor's design, design.md 2.3): event slots the engine calls at fixed points
// (matchStart, roundSetup, tick, kill, roundOver, ...) and query slots it asks (roundLength, respawnDelay, canDamage,
// frozen, ...). Each slot has a native version per mode (gamemodes.cpp) that reads its numbers from the server's
// ModeParams (modeparams.h). Objects (the bomb, the carts, the points, the race, the stalemate drain, "it", the rock
// paper scissors tiebreak, the tourney bracket, duel modifiers) are native mechanics the slots drive; they raise
// events (plant, defuse, explode, cartCheckpoint, delivered, capture, checkpoint, finish) that are slots too.
//
// A custom mode's logic (phase 3) replaces some slots: the server then holds a ModeHooks, and each slot first tests
// it (one null test, predicted not taken; built-in modes never have one). The logic reads and drives the same
// engine through ModeHost (modelogic.h), which the hooks implement over the Server (gamemodes.cpp, end).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "modelogic.h"   // ModeSlot (the slots), ModeLogic
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

// ---- the slots (design.md 2.3: ModeSlot, modelogic.h) and the hook a custom mode's logic plugs in (phase 3)
// Each slot's arguments arrive in ModeHooks::event / query's a, b, c in modelogic.h's order (ModeArgs): kill [victim,
// killer, weapon]; stab [attacker, victim]; leave, spectate, plant, defuse, finish, respawnDelay, frozen, spawnType
// [p]; cartCheckpoint, capture [the count reached now, 1 = the first]; checkpoint [p, i]; canDamage, canHit
// [attacker, victim, weapon].
static_assert(MSLOT_COUNT <= 64, "ModeHooks::interp is a 64-bit mask");

// What a custom mode's logic gives the server. Server::modeHooks_ is null for every built-in mode (and on ranked and
// time trial servers), and then each slot is one null test away from the native code.
struct ModeHooks {
    uint64_t interp = 0;                   // bit per ModeSlot (a timer's: MSLOT_TIMER + i): run through the logic
    const ModeParams* params = nullptr;    // the mode's numbers for the native slots (null: the base mode's built-in ones)
    virtual ~ModeHooks() = default;
    // An event slot (MSLOT_TIMER: every timer that ran out). When it ends the round or the match (the phase changes),
    // the caller returns, as the native code does after an endRound. False: it couldn't run (slots nested deeper than
    // kModeFrames): the caller runs the native version instead.
    virtual bool event(Server& s, ModeSlot slot, int a, int b, int c) = 0;
    // A query slot's value in out (f32 / int / bool / SPAWN_* as a double); false: the caller asks the native version.
    // Read-only for the server: a query's actions and field writes are ignored.
    virtual bool query(const Server& s, ModeSlot slot, int a, int b, int c, double& out) = 0;
    // reset / matchStart / roundSetup ran natively: the logic's state of that lifetime goes back to its declared
    // values all the same (runModeSlot does it itself when the slot is interpreted).
    virtual void nativeLifetime(ModeSlot slot) { (void)slot; }
    bool has(int k) const { return (interp >> k & 1) != 0; }
};

// The ModeHooks that plays a compiled ModeLogic on a server: its interpreted slots through the interpreter (with the
// Server as its ModeHost), its params as the native slots' ModeParams. One per server: it holds the logic's state.
std::shared_ptr<ModeHooks> makeModeLogicHooks(std::shared_ptr<const ModeLogic> logic);
// "reset matchStart tick ..." : the slots a ModeHooks runs through the logic, by name (the start log line)
std::string modeHookedSlots(const ModeHooks& h);
