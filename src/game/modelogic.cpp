// Game mode logic: the .qlogic / .qmode parser, canonical printer, JSON export, type checker, compiler and interpreter
// (tools/modes/GRAMMAR.md). No dependency on Server: the game binds it through ModeHost.
// The language half is copied and adapted from botbrain.cpp (the brains thread owns that file): the lexer, numbers,
// the expression grammar, the typed arithmetic, the printer's rules and the JSON comment model are the same.
#include "modelogic.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include "../common/sha256.h"
#include "default_modes.h"
#include "gamedefs.h"
#include "protocol.h"

static_assert(sizeof(ModeInsn) == 8, "an instruction is 8 bytes");
static_assert(kModeRegs <= 255 && kModeTextParts <= 255, "register numbers are bytes");
static_assert(MODE_COUNT == 14, "a .qlogic default per mode (tools/modes)");
static_assert(PHASE_WARMUP == 0 && PHASE_FREEZE == 1 && PHASE_LIVE == 2 && PHASE_ROUND_END == 3 && PHASE_MATCH_END == 4, "phase constants");
static_assert(BOMB_NONE == 0 && BOMB_CARRIED == 1 && BOMB_DROPPED == 2 && BOMB_PLANTED == 3 && BOMB_EXPLODED == 4 && BOMB_DEFUSED == 5, "bomb state constants");
static_assert(TEAM_A == 0 && TEAM_B == 1 && TEAM_SPECTATOR == 2, "team constants");
static_assert(WPN_COUNT == 11, "weapon names");

namespace {

// =====================================================================================================
// Catalog
// =====================================================================================================

enum Type : uint8_t { T_BOOL, T_INT, T_F32, T_F64, T_TEAM, T_PLAYER, T_ZONE, T_PHASE, T_BOMBSTATE, T_SPAWN, T_WEAPON, T_TEXT, T_TIMER, T_NONELIT, T_VOID, T_NONE,
                      T_WINNER = 100, T_TEXTS = 101 };   // (argument markers in the action table)
const char* typeName(int t) {
    static const char* n[] = {"bool", "int", "f32", "f64", "team", "player", "zone", "phase", "bombstate", "spawn", "weapon", "text", "timer", "none", "nothing", "?"};
    if (t == T_WINNER) return "winner";
    if (t == T_TEXTS) return "text...";
    return t >= 0 && t <= T_NONE ? n[t] : "?";
}
bool isNumeric(int t) { return t == T_INT || t == T_F32 || t == T_F64; }
bool isNullable(int t) { return t == T_TEAM || t == T_PLAYER || t == T_WEAPON || t == T_BOMBSTATE; }

const char* const kWeaponNames[WPN_COUNT] = {"rifle", "rocket", "knife", "sniper", "laser", "shotgun", "sticky", "crossbow", "bubble", "syringe", "locator"};
const char* const kBaseNames[MODE_COUNT] = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "arena", "tourney", "hide", "tag", "points", "surf"};
bool teamBased(int mode) { return mode != MODE_FFA && !isRaceMode(mode) && mode != MODE_TOURNEY && mode != MODE_HIDE && mode != MODE_TAG; }

struct ArgInfo { const char* name; uint8_t type; const char* doc; };
struct MSlotInfo { const char* name; bool query; std::vector<ArgInfo> args; ArgInfo out; const char* when; const char* doc; };
const ArgInfo kNoOut = {nullptr, T_VOID, nullptr};
const ArgInfo A_P = {"p", T_PLAYER, "the player"};
const MSlotInfo kSlots[MSLOT_TIMER + 1] = {
    {"reset", false, {}, kNoOut, "back to warmup (rulesReset)", "set up the warmup"},
    {"warmup", false, {}, kNoOut, "each warmup tick, before the autostart check", "warmup play (race: practice runs)"},
    {"matchStart", false, {}, kNoOut, "a match starts, before round 1 (startMatch; match state is reset first)", "set up the match"},
    {"roundSetup", false, {}, kNoOut, "a round starts: after roundNo++ and the round's resets, before the spawns (round state is reset first)", "set up the round: sides, who is it"},
    {"roundStart", false, {}, kNoOut, "after the spawns and the objects' round start, as the freeze begins", "start the round: hand out the bomb, announce"},
    {"live", false, {}, kNoOut, "the freeze ends and the round goes live", "the round goes live"},
    {"tick", false, {}, kNoOut, "each live tick (hud.clock holds the round clock)", "the round's rules: objectives, eliminations, the clock"},
    {"timeUp", false, {}, kNoOut, "each live tick after tick, while the round clock has run out", "the clock has run out"},
    {"kill", false, {{"victim", T_PLAYER, "who died"}, {"killer", T_PLAYER, "who killed them (none: the world, a fall)"}, {"weapon", T_WEAPON, "the weapon, or how: fell, bombed, drained"}}, kNoOut,
        "a player died, after the engine counted the death and the kill", "scoring for kills"},
    {"stab", false, {{"attacker", T_PLAYER, "\"it\""}, {"victim", T_PLAYER, "who was knifed"}}, kNoOut, "hide and seek / tag: \"it\" knifed someone while live (after canHit)", "what a stab does"},
    {"leave", false, {A_P}, kNoOut, "a player is leaving (still connected while this runs)", "a player leaves"},
    {"spectate", false, {A_P}, kNoOut, "a player is about to go to spectate", "a player goes to spectate"},
    {"roundOver", false, {}, kNoOut, "the round-end screen is over", "end the match, or fall off the end for the next round"},
    {"plant", false, {A_P}, kNoOut, "bomb: p planted it (use bomb)", "the bomb was planted"},
    {"defuse", false, {A_P}, kNoOut, "bomb: p defused it (use bomb)", "the bomb was defused"},
    {"explode", false, {}, kNoOut, "bomb: the fuse ran out; everyone in bombRadius is dead (use bomb)", "the bomb went off"},
    {"cartCheckpoint", false, {{"i", T_INT, "checkpoints reached now (1 = the first)"}}, kNoOut, "payload: the cart reached a checkpoint (use cart)", "a cart checkpoint"},
    {"delivered", false, {}, kNoOut, "payload: the cart reached the end (use cart)", "the payload was delivered"},
    {"capture", false, {{"i", T_INT, "points taken now (1 = the first)"}}, kNoOut, "multi point: the attack took a point (use points)", "a point was taken"},
    {"checkpoint", false, {A_P, {"i", T_INT, "checkpoints p has passed now"}}, kNoOut, "race: p passed a checkpoint (use race)", "a race checkpoint"},
    {"finish", false, {A_P}, kNoOut, "race: p passed the last checkpoint (use race)", "a racer finished"},
    {"freezeLength", true, {}, {"s", T_F32, "seconds"}, "each round start", "the countdown before a round"},
    {"roundLength", true, {}, {"s", T_F32, "seconds (0: no clock)"}, "the freeze ends", "the round clock"},
    {"roundsToWin", true, {}, {"n", T_INT, "rounds"}, "match start and reset (hud.roundsToWin)", "rounds to win the match"},
    {"respawnDelay", true, {A_P}, {"s", T_F32, "seconds (below 0: no respawn)"}, "a death or a team join, outside warmup and match end", "how long the dead wait"},
    {"canDamage", true, {{"attacker", T_PLAYER, "who hit (none: the world)"}, {"victim", T_PLAYER, "who was hit"}, {"weapon", T_WEAPON, "with what"}}, {"ok", T_BOOL, "the hit hurts"},
        "each hit", "does a hit do damage"},
    {"canHit", true, {{"attacker", T_PLAYER, "who hit (none: the world)"}, {"victim", T_PLAYER, "who was hit"}, {"weapon", T_WEAPON, "with what"}}, {"ok", T_BOOL, "the hit lands (else no damage, no knockback)"},
        "each hit, before knockback", "does a hit touch the victim at all"},
    {"frozen", true, {A_P}, {"frozen", T_BOOL, "can't move"}, "per player, once a tick", "movement locked"},
    {"spawnType", true, {A_P}, {"spawn", T_SPAWN, "the spawns to use"}, "each spawn", "where a player spawns"},
    {"teamLocked", true, {}, {"locked", T_BOOL, "no joining a side"}, "joins and team changes", "joining locked"},
    {"canAutoStart", true, {}, {"ok", T_BOOL, "enough players"}, "each warmup tick on autostart servers", "may the match start by itself"},
    {"timer", false, {}, kNoOut, "the timer ran out (checked once a tick, before tick)", "a timer ran out"},
};
const MSlotInfo& slotInfo(int s) { return kSlots[s < MSLOT_TIMER ? s : MSLOT_TIMER]; }

struct FieldInfo { const char* name; uint8_t type; uint8_t field; bool rw; const char* unit; const char* doc; };
const FieldInfo kFields[] = {
    {"now", T_F64, MF_NOW, false, "s", "game time"},
    {"dt", T_F32, MF_DT, false, "s", "the tick's length"},
    {"phase", T_PHASE, MF_PHASE, false, "", "warmup, freeze, live, roundEnd or matchEnd"},
    {"roundNo", T_INT, MF_ROUND_NO, false, "", "the round (1 = the first of the match)"},
    {"phaseEnd", T_F64, MF_PHASE_END, false, "s", "when the phase's clock runs out (0: no clock)"},
    {"liveSince", T_F64, MF_LIVE_SINCE, false, "s", "when the round went live"},
    {"roundStartTime", T_F64, MF_ROUND_START_TIME, false, "s", "when the round's freeze ends (the round's start)"},
    {"lastProgress", T_F64, MF_LAST_PROGRESS, false, "s", "the last damage dealt or plant (the stalemate clock)"},
    {"teams", T_BOOL, MF_TEAMS, false, "", "the base mode is team based"},
    {"ranked", T_BOOL, MF_RANKED, false, "", "a ranked server (always no where logic runs)"},
    {"tickRate", T_INT, MF_TICK_RATE, false, "Hz", "server ticks a second"},
    {"cfg.timeLimit", T_INT, MF_CFG_TIME_LIMIT, false, "s", "the host's time limit (0: the mode's)"},
    {"cfg.scoreLimit", T_INT, MF_CFG_SCORE_LIMIT, false, "", "the host's score limit (0: the mode's)"},
    {"cfg.roundsToWin", T_INT, MF_CFG_ROUNDS_TO_WIN, false, "", "the host's rounds to win (0: the mode's)"},
    {"hasHill", T_BOOL, MF_HAS_HILL, false, "", "the map has a hill"},
    {"hasCart", T_BOOL, MF_HAS_CART, false, "", "the map has a payload track"},
    {"siteCount", T_INT, MF_SITE_COUNT, false, "", "bomb sites on the map"},
    {"pointCount", T_INT, MF_POINT_COUNT, false, "", "multi point zones on the map"},
    {"checkpointCount", T_INT, MF_CHECKPOINT_COUNT, false, "", "race checkpoints on the map (the last is the finish)"},
    {"attackers", T_TEAM, MF_ATTACKERS, true, "", "the attacking side (bomb, payload, multi point)"},
    {"hud.clock", T_F32, MF_HUD_CLOCK, true, "s", "the clock the HUD shows (the engine sets it to the round clock before tick)"},
    {"hud.overtime", T_BOOL, MF_HUD_OVERTIME, true, "", "the HUD shows OVERTIME"},
    {"hud.contested", T_BOOL, MF_HUD_CONTESTED, true, "", "the HUD shows the objective contested"},
    {"hud.drain", T_BOOL, MF_HUD_DRAIN, true, "", "the HUD shows the stalemate drain"},
    {"hud.target", T_INT, MF_HUD_TARGET, true, "", "the score to reach (KOTH's bar)"},
    {"hud.owner", T_TEAM, MF_HUD_OWNER, true, "", "who holds the objective (none: nobody)"},
    {"hud.roundsToWin", T_INT, MF_HUD_ROUNDS_TO_WIN, false, "", "rounds to win this match (the roundsToWin slot's answer)"},
    {"bomb.state", T_BOMBSTATE, MF_BOMB_STATE, false, "", "none before the round gives it out; carried, dropped, planted, defused, exploded"},
    {"bomb.carrier", T_PLAYER, MF_BOMB_CARRIER, false, "", "who carries it (none: nobody)"},
    {"bomb.acting", T_PLAYER, MF_BOMB_ACTING, false, "", "who is planting or defusing (none: nobody)"},
    {"bomb.timer", T_F32, MF_BOMB_TIMER, false, "s", "the fuse left"},
    {"cart.dist", T_F32, MF_CART_DIST, false, "m", "how far along the track the cart is"},
    {"cart.length", T_F32, MF_CART_LENGTH, false, "m", "the track's length"},
    {"cart.checkpoint", T_INT, MF_CART_CHECKPOINT, false, "", "checkpoints the cart has reached"},
    {"point.index", T_INT, MF_POINT_INDEX, false, "", "the point under attack (= points taken)"},
    {"point.capture", T_F32, MF_POINT_CAPTURE, false, "", "its capture, 0..1"},
    {"cart.checkpointCount", T_INT, MF_CART_CHECKPOINT_COUNT, false, "", "the payload track's checkpoints on the map"},
    {"stagedSpawns", T_BOOL, MF_STAGED_SPAWNS, false, "", "the map's spawns move up as the attack goes (staged spawns)"},
};
constexpr int kNumFields = (int)(sizeof kFields / sizeof kFields[0]);

struct ArrayInfo { const char* name; uint8_t index, type, field; bool rw; const char* unit; const char* doc; };
const ArrayInfo kArrays[] = {
    {"score", T_PLAYER, T_INT, MF_SCORE, true, "", "the player's score"},
    {"objTime", T_PLAYER, T_F32, MF_OBJ_TIME, true, "s", "time on the objective (the scoreboard)"},
    {"it", T_PLAYER, T_BOOL, MF_IT, true, "", "the seeker / tagger"},
    {"out", T_PLAYER, T_BOOL, MF_OUT, true, "", "hide and seek: found this round; tag: eliminated for the match"},
    {"found", T_PLAYER, T_BOOL, MF_FOUND, true, "", "hide and seek: found by the seeker this round"},
    {"sought", T_PLAYER, T_BOOL, MF_SOUGHT, true, "", "hide and seek: has had a turn seeking this match"},
    {"kills", T_PLAYER, T_INT, MF_KILLS, false, "", "kills this match"},
    {"deaths", T_PLAYER, T_INT, MF_DEATHS, false, "", "deaths this match"},
    {"freezeUntil", T_PLAYER, T_F64, MF_FREEZE_UNTIL, false, "s", "frozen by freeze(p, s) until then (tag: a new tagger)"},
    {"race.checkpoint", T_PLAYER, T_INT, MF_RACE_CHECKPOINT, false, "", "race: the next checkpoint to reach"},
    {"race.finished", T_PLAYER, T_BOOL, MF_RACE_FINISHED, false, "", "race: finished this round"},
    {"teamScore", T_TEAM, T_INT, MF_TEAM_SCORE, true, "", "the team's score (rounds won, points; an int16 on the wire)"},
    {"cart.best", T_TEAM, T_F32, MF_CART_BEST, true, "", "payload: the farthest the team pushed the cart (m); multi point: points taken + capture"},
    {"cart.completed", T_TEAM, T_F32, MF_CART_COMPLETED, true, "s", "payload / multi point: how long the team's attack took to finish (0: it didn't)"},
    {"cart.near", T_TEAM, T_INT, MF_CART_NEAR, false, "", "players of the team alive at the cart at the last cart step"},
    {"cart.bestTime", T_TEAM, T_F32, MF_CART_BEST_TIME, false, "s", "payload / multi point: when (since its round went live) the team's attack reached cart.best (a level attack: the sooner wins)"},
};
constexpr int kNumArrays = (int)(sizeof kArrays / sizeof kArrays[0]);

enum FnSpecial : uint8_t { FS_FIELD, FS_NAME, FS_TIMERLEFT, FS_OTHER, FS_LETTER, FS_SITE, FS_POINT, FS_CHECKPOINT };
struct FuncInfo { const char* name; std::vector<uint8_t> args; uint8_t ret; uint8_t special; uint8_t field; const char* doc; };
const FuncInfo kFuncs[] = {
    {"aliveCount", {T_TEAM}, T_INT, FS_FIELD, MF_ALIVE_COUNT, "players of the team alive"},
    {"teamCount", {T_TEAM}, T_INT, FS_FIELD, MF_TEAM_COUNT, "players on the team (connected)"},
    {"alive", {T_PLAYER}, T_BOOL, FS_FIELD, MF_ALIVE, "the player is alive"},
    {"team", {T_PLAYER}, T_TEAM, FS_FIELD, MF_TEAM, "the player's team (a spectator's equals neither A nor B)"},
    {"isBot", {T_PLAYER}, T_BOOL, FS_FIELD, MF_IS_BOT, "the player is a bot"},
    {"onGround", {T_PLAYER}, T_BOOL, FS_FIELD, MF_ON_GROUND, "the player stands on the ground"},
    {"useHeld", {T_PLAYER}, T_BOOL, FS_FIELD, MF_USE_HELD, "the player holds USE"},
    {"health", {T_PLAYER}, T_F32, FS_FIELD, MF_HEALTH, "the player's health (hp)"},
    {"nearCart", {T_PLAYER}, T_BOOL, FS_FIELD, MF_NEAR_CART, "within cartRange of the cart"},
    {"mapSpawn", {T_PLAYER}, T_SPAWN, FS_FIELD, MF_MAP_SPAWN, "the native spawn choice for the player"},
    {"inZone", {T_PLAYER, T_ZONE}, T_BOOL, FS_FIELD, MF_IN_ZONE, "the player's feet are in the zone"},
    {"name", {T_PLAYER}, T_TEXT, FS_NAME, 0, "the player's name (text arguments only)"},
    {"timerLeft", {T_TIMER}, T_F32, FS_TIMERLEFT, 0, "seconds left on a timer (0: stopped)"},
    {"other", {T_TEAM}, T_TEAM, FS_OTHER, 0, "the other team (other(A) = B; anything else: A)"},
    {"letter", {T_INT}, T_TEXT, FS_LETTER, 0, "a capital letter, 0 = A (text arguments only: point names)"},
    {"site", {T_INT}, T_ZONE, FS_SITE, 0, "bomb site i (a zone)"},
    {"point", {T_INT}, T_ZONE, FS_POINT, 0, "multi point zone i (a zone)"},
    {"checkpoint", {T_INT}, T_ZONE, FS_CHECKPOINT, 0, "race checkpoint i (a zone)"},
};

struct BuiltinInfo { const char* name; const char* sig; const char* doc; };
const BuiltinInfo kBuiltins[] = {
    {"min", "min(a, b)", "b < a ? b : a (std::min)"},
    {"max", "max(a, b)", "a < b ? b : a (std::max)"},
    {"clamp", "clamp(x, lo, hi)", "x >= lo ? (x > hi ? hi : x) : lo"},
    {"abs", "abs(x)", "absolute value (f32 / f64)"},
    {"sqrt", "sqrt(x)", "square root (f32 / f64)"},
    {"exp", "exp(x)", "e to the x (f32 / f64)"},
    {"f32", "f32(x) -> f32", "conversion"},
    {"f64", "f64(x) -> f64", "conversion"},
    {"int", "int(x) -> int", "conversion, truncating like a C++ cast"},
};
const char* const kAggregates[] = {"count", "any", "first", "random", "best", "names"};
bool isAggregate(const std::string& n) { for (auto* a : kAggregates) if (n == a) return true; return false; }

enum ActSpecial : uint8_t { AS_HOST, AS_ADD_SCORE, AS_ADD_TEAM_SCORE, AS_START_TIMER, AS_STOP_TIMER };
struct ActionInfo { const char* name; std::vector<ArgInfo> args; uint8_t ret; bool ends; int8_t obj; uint8_t act; uint8_t special; bool available; int draws; const char* doc; };
const ArgInfo X_TEXTS = {"text", T_TEXTS, "text arguments: \"strings\", players (names), teams, ints"};
const ArgInfo X_WINNER = {"winner", T_WINNER, "a team (a player where the base mode scores players: race; endMatch in ffa, race, tourney, hide, tag)"};
const ActionInfo kActions[] = {
    {"addScore", {A_P, {"n", T_INT, "points"}}, T_VOID, false, -1, 0, AS_ADD_SCORE, true, 0, "score[p] = score[p] + n"},
    {"addTeamScore", {{"t", T_TEAM, "the team"}, {"n", T_INT, "points"}}, T_VOID, false, -1, 0, AS_ADD_TEAM_SCORE, true, 0, "teamScore[t] = teamScore[t] + n"},
    {"endRound", {X_WINNER, X_TEXTS}, T_VOID, true, -1, MA_END_ROUND, AS_HOST, true, 0,
        "the round goes to winner (+1 to the team; race: the player): \"X wins the round (text)\"; round end. Ends the slot"},
    {"endRoundNoScore", {X_WINNER, X_TEXTS}, T_VOID, true, -1, MA_END_ROUND_NO_SCORE, AS_HOST, true, 0, "endRound without the point (payload / multi point time-out). Ends the slot"},
    {"endRoundText", {X_TEXTS}, T_VOID, true, -1, MA_END_ROUND_TEXT, AS_HOST, true, 0, "a round result with no team score (hide and seek, tag). Ends the slot"},
    {"endMatch", {X_WINNER, X_TEXTS}, T_VOID, true, -1, MA_END_MATCH, AS_HOST, true, 0, "the match goes to winner, the text is the result. Ends the slot"},
    {"announce", {X_TEXTS}, T_VOID, false, -1, MA_ANNOUNCE, AS_HOST, true, 0, "a message to everyone (at most 4 a second)"},
    {"log", {X_TEXTS}, T_VOID, false, -1, MA_LOG, AS_HOST, true, 0, "a line in the server log"},
    {"setClock", {{"s", T_F32, "seconds"}}, T_VOID, false, -1, MA_SET_CLOCK, AS_HOST, true, 0, "phaseEnd = now + s"},
    {"addTime", {{"s", T_F32, "seconds"}}, T_VOID, false, -1, MA_ADD_TIME, AS_HOST, true, 0, "phaseEnd = max(phaseEnd, now) + s"},
    {"touchObjective", {}, T_VOID, false, -1, MA_TOUCH_OBJECTIVE, AS_HOST, true, 0, "the side that needs time touched the objective now (overtime)"},
    {"holdOvertime", {}, T_BOOL, false, -1, MA_HOLD_OVERTIME, AS_HOST, true, 0,
        "the clock has run out: true while an objective touch in the last `overtime` s holds the round open (announces \"overtime!\" once, sets hud.overtime and hud.clock)"},
    {"startTimer", {{"timer", T_TIMER, "the timer"}, {"s", T_F32, "seconds"}}, T_VOID, false, -1, 0, AS_START_TIMER, true, 0, "start (or restart) a timer: slot timer <name> runs when it runs out"},
    {"stopTimer", {{"timer", T_TIMER, "the timer"}}, T_VOID, false, -1, 0, AS_STOP_TIMER, true, 0, "stop a timer"},
    {"kill", {{"victim", T_PLAYER, "who dies"}, {"killer", T_PLAYER, "credited to (none: the world)"}, {"how", T_WEAPON, "the weapon or cause"}}, T_VOID, false, -1, MA_KILL, AS_HOST, true, 0,
        "kill a player (runs slot kill)"},
    {"damage", {A_P, {"hp", T_F32, "health to take"}}, T_VOID, false, -1, MA_DAMAGE, AS_HOST, true, 0, "health -= hp (at 0: killed by the world, drained)"},
    {"respawn", {A_P}, T_VOID, false, -1, MA_RESPAWN, AS_HOST, true, 0, "spawn the player now"},
    {"freeze", {A_P, {"s", T_F32, "seconds"}}, T_VOID, false, -1, MA_FREEZE, AS_HOST, true, 0, "freezeUntil[p] = now + s (read by slot frozen)"},
    {"swapTeams", {}, T_VOID, false, -1, MA_SWAP_TEAMS, AS_HOST, false, 0, "every player A <-> B (not available yet: needs the owner's rules)"},
    {"bombStep", {}, T_VOID, false, MO_BOMB, MA_BOMB_STEP, AS_HOST, true, 1,
        "the bomb this tick: pickup, return (one draw when it goes back), plant, fuse, defuse, blast; hud.clock = the fuse while planted. Raises plant, defuse, explode"},
    {"giveBomb", {A_P}, T_VOID, false, MO_BOMB, MA_GIVE_BOMB, AS_HOST, true, 0, "p carries the bomb (none: nobody)"},
    {"cartStep", {}, T_VOID, false, MO_CART, MA_CART_STEP, AS_HOST, true, 0,
        "the payload cart this tick: occupancy, push, contest, rollback, checkpoints, objTime, overtime touches, best progress. Raises cartCheckpoint, delivered"},
    {"neutralCartStep", {}, T_VOID, false, MO_NCART, MA_NEUTRAL_CART_STEP, AS_HOST, true, 0, "the neutral cart this tick: occupancy (cart.near), push either way, contest, objTime, hud.owner"},
    {"pointStep", {}, T_BOOL, false, MO_POINTS, MA_POINT_STEP, AS_HOST, true, 0,
        "multi point this tick: capture, decay, contest, objTime, overtime touches, best progress. Raises capture. True when the tick's rules should stop: a point was taken, or there's none to attack"},
    {"raceStep", {}, T_VOID, false, MO_RACE, MA_RACE_STEP, AS_HOST, true, 0, "race checkpoints this tick (splits, time trial runs). Raises checkpoint, finish"},
    {"racePractice", {}, T_VOID, false, MO_RACE, MA_RACE_PRACTICE, AS_HOST, true, 0, "warmup practice runs (untimed)"},
    {"stalemateDrain", {}, T_VOID, false, MO_DRAIN, MA_STALEMATE_DRAIN, AS_HOST, true, 0, "after `stalemate` s with no damage: drainRate hp a second off everyone (hud.drain, hud.clock)"},
    {"pickSeeker", {}, T_VOID, false, MO_IT, MA_PICK_SEEKER, AS_HOST, true, 0, "hide and seek: the next player in slot order who hasn't sought is it"},
    {"pickRandomIt", {}, T_VOID, false, MO_IT, MA_PICK_RANDOM_IT, AS_HOST, true, 1, "a random player in play (not out) is it: one draw"},
    {"tagOut", {A_P}, T_VOID, false, MO_IT, MA_TAG_OUT, AS_HOST, true, 0, "out for the match (remembered across reconnects): out, not it, dead, no respawn"},
    {"passIt", {{"from", T_PLAYER, "it now"}, {"to", T_PLAYER, "it next"}}, T_VOID, false, MO_IT, MA_PASS_IT, AS_HOST, true, 0, "it moves from one player to another (knife out, locator reset)"},
    {"startTiebreak", {}, T_BOOL, false, MO_TIEBREAK, MA_START_TIEBREAK, AS_HOST, true, 0,
        "two or more share the top score: rock paper scissors starts (it ends the match) and this is true"},
    {"tourneyBegin", {}, T_BOOL, false, MO_BRACKET, MA_TOURNEY_BEGIN, AS_HOST, true, 0, "seed the bracket and set up its first match; false: not enough players"},
    {"tourneyWalkover", {}, T_BOOL, false, MO_BRACKET, MA_TOURNEY_WALKOVER, AS_HOST, true, 0, "a finalist left: the other advances, the next match starts (or the tourney ends); true if so"},
    {"tourneyAdvance", {{"winner", T_TEAM, "the side that won the match"}}, T_VOID, false, MO_BRACKET, MA_TOURNEY_ADVANCE, AS_HOST, true, 0,
        "the match's winner advances: the next match is set up, or the tourney ends"},
    {"pickDuelModifier", {}, T_VOID, false, MO_DUELMODS, MA_PICK_DUEL_MODIFIER, AS_HOST, true, 1, "duel: a random modifier for this life, never one played this match (one draw)"},
};
constexpr int kNumActions = (int)(sizeof kActions / sizeof kActions[0]);
const ActionInfo* actionByName(const std::string& n) { for (auto& a : kActions) if (n == a.name) return &a; return nullptr; }

struct ObjectInfo { const char* name; const char* doc; std::vector<const char*> steps, events, params; uint32_t modes; uint32_t needs; };
#define MB(m) (1u << (m))
const ObjectInfo kObjects[MO_COUNT] = {
    {"bomb", "carry, drop (to the floor), pick up, return, plant, defuse, fuse; the carrier's death drops it", {"bombStep", "giveBomb"}, {"plant", "defuse", "explode"},
        {"plantTime", "defuseTime", "bombFuse", "bombRadius", "defuseRange", "pickupRange", "bombReturn"}, MB(MODE_BOMB), MN_SITES},
    {"cart", "the payload cart: pushed by attackers, blocked by defenders, rolls back when left, never past a checkpoint; solid", {"cartStep"}, {"cartCheckpoint", "delivered"},
        {"cartRange", "cartSpeed", "cartRollback", "cartIdle", "cartCheckBonus"}, MB(MODE_PAYLOAD), MN_CART},
    {"ncart", "the neutral cart: whichever team is alone at it pushes it toward its end; solid", {"neutralCartStep"}, {}, {"cartRange", "cartSpeed"}, MB(MODE_NPAYLOAD), MN_CART},
    {"points", "multi point: the attack takes the map's points in order", {"pointStep"}, {"capture"}, {"captureTime", "captureDecay"}, MB(MODE_POINTS), MN_POINTS},
    {"race", "race checkpoints in order, splits, time trial runs, practice", {"raceStep", "racePractice"}, {"checkpoint", "finish"}, {}, MB(MODE_RACE) | MB(MODE_SURF), MN_CHECKPOINTS},
    {"drain", "the stalemate drain", {"stalemateDrain"}, {}, {"stalemate", "drainRate"}, MB(MODE_VS) | MB(MODE_DUEL) | MB(MODE_ARENA) | MB(MODE_TOURNEY), 0},
    {"it", "one player is \"it\" (seeker / tagger) with the knife and the locator; tag knockouts last the match", {"pickSeeker", "pickRandomIt", "tagOut", "passIt"}, {},
        {"hideTime", "tagFreeze"}, MB(MODE_HIDE) | MB(MODE_TAG), 0},
    {"tiebreak", "rock paper scissors for a shared first place (the engine runs it in round end)", {"startTiebreak"}, {}, {"rpsPickTime", "rpsRevealTime"}, MB(MODE_HIDE), 0},
    {"bracket", "the tourney's single-elimination bracket", {"tourneyBegin", "tourneyWalkover", "tourneyAdvance"}, {}, {}, MB(MODE_TOURNEY), 0},
    {"duelmods", "duel: a random modifier each life", {"pickDuelModifier"}, {}, {}, MB(MODE_DUEL), 0},
};
#undef MB
int objectByName(const std::string& n) { for (int i = 0; i < MO_COUNT; i++) if (n == kObjects[i].name) return i; return -1; }

// Every param the built-in modes declare: the type is fixed (a file may only change the value); objects' params take
// these defaults when no file declares them.
struct ParamInfo { const char* name; uint8_t type; const char* unit; double lo, hi; int8_t obj; double def; const char* doc; };
const ParamInfo kParams[] = {
    {"freezeTime", T_F32, "s", 0, 30, -1, 3, "countdown before each round"},
    {"roundEndTime", T_F32, "s", 0, 30, -1, 4, "the round result is shown this long"},
    {"matchEndTime", T_F32, "s", 0, 60, -1, 10, "the match result is shown this long"},
    {"warmupCountdown", T_F32, "s", 0, 60, -1, 5, "from enough players to the start (autostart servers)"},
    {"warmupRespawn", T_F32, "s", 0, 30, -1, 1.5, "dead this long in the warmup"},
    {"overtime", T_F32, "s", 0, 60, -1, 5, "an objective touch holds the round open this long after the clock runs out"},
    {"rounds", T_INT, "", 1, 50, -1, 1, "rounds to win the match, when the host sets none"},
    {"roundTime", T_F32, "s", 0, 3600, -1, 0, "a round's clock (0: no clock)"},
    {"respawnTime", T_F32, "s", -1, 60, -1, -1, "dead this long before respawning (below 0: one life a round)"},
    {"matchTime", T_F32, "s", 0, 3600, -1, 600, "the match clock, when the host sets no time limit"},
    {"rankedTime", T_F32, "s", 0, 3600, -1, 300, "the match clock on ranked servers"},
    {"target", T_INT, "", 1, 10000, -1, 100, "points to win, when the host sets no score limit"},
    {"pointBonus", T_F32, "s", 0, 600, -1, 90, "added to the clock for every point taken"},
    {"plantTime", T_F32, "s", 0, 30, MO_BOMB, 3, "holding USE on a site, on the ground, to plant"},
    {"defuseTime", T_F32, "s", 0, 30, MO_BOMB, 6, "holding USE by the bomb to defuse"},
    {"bombFuse", T_F32, "s", 0, 300, MO_BOMB, 40, "from the plant to the blast"},
    {"bombRadius", T_F32, "m", 0, 200, MO_BOMB, 18, "the blast kills everyone this close"},
    {"defuseRange", T_F32, "m", 0, 20, MO_BOMB, 1.8, "from the bomb to defuse it"},
    {"pickupRange", T_F32, "m", 0, 20, MO_BOMB, 1.2, "from a dropped bomb for an attacker to take it"},
    {"bombReturn", T_F32, "s", 0, 300, MO_BOMB, 15, "a dropped bomb lies this long before it goes back to a random attacker"},
    {"cartRange", T_F32, "m", 0, 40, MO_CART, 4, "from the cart that counts as at it"},
    {"cartSpeed", T_F32, "m/s", 0, 20, MO_CART, 1.6, "with one pusher (+25% for each more, up to twice)"},
    {"cartRollback", T_F32, "m/s", 0, 20, MO_CART, 0.6, "the cart rolls back this fast when left alone..."},
    {"cartIdle", T_F32, "s", 0, 120, MO_CART, 10, "...after this long with no attacker at it"},
    {"cartCheckBonus", T_F32, "s", 0, 600, MO_CART, 60, "added to the clock when the cart reaches a checkpoint"},
    {"captureTime", T_F32, "s", 0.1, 300, MO_POINTS, 12, "one attacker alone takes a point in this long (more go faster, up to twice)"},
    {"captureDecay", T_F32, "/s", 0, 1, MO_POINTS, 0.05, "capture a point loses a second when the attackers leave it"},
    {"stalemate", T_F32, "s", 0, 600, MO_DRAIN, 30, "with no damage dealt before the drain starts"},
    {"drainRate", T_F32, "hp/s", 0, 100, MO_DRAIN, 5, "everyone loses this much health a second in a stalemate"},
    {"hideTime", T_F32, "s", 0, 120, MO_IT, 30, "hide and seek: the hiders' head start while the seeker faces the wall"},
    {"tagFreeze", T_F32, "s", 0, 30, MO_IT, 2, "a newly tagged player stands still this long (no instant tag-back)"},
    {"rpsPickTime", T_F32, "s", 1, 60, MO_TIEBREAK, 6, "to choose rock, paper or scissors"},
    {"rpsRevealTime", T_F32, "s", 0, 60, MO_TIEBREAK, 3, "each throw is shown this long"},
};
constexpr int kNumParams = (int)(sizeof kParams / sizeof kParams[0]);
const ParamInfo* paramInfo(const std::string& n) { for (auto& p : kParams) if (n == p.name) return &p; return nullptr; }

struct ConstInfo { const char* name; uint8_t type; double v; const char* doc; };
const ConstInfo kConsts[] = {
    {"A", T_TEAM, 0, "team A"}, {"B", T_TEAM, 1, "team B"},
    {"warmup", T_PHASE, PHASE_WARMUP, "free play before a match"}, {"freeze", T_PHASE, PHASE_FREEZE, "the countdown before a round"},
    {"live", T_PHASE, PHASE_LIVE, "the round is on"}, {"roundEnd", T_PHASE, PHASE_ROUND_END, "the round result"}, {"matchEnd", T_PHASE, PHASE_MATCH_END, "the match result"},
    {"carried", T_BOMBSTATE, BOMB_CARRIED, "someone carries the bomb"}, {"dropped", T_BOMBSTATE, BOMB_DROPPED, "it lies on the ground"},
    {"planted", T_BOMBSTATE, BOMB_PLANTED, "it is planted"}, {"exploded", T_BOMBSTATE, BOMB_EXPLODED, "it went off"}, {"defused", T_BOMBSTATE, BOMB_DEFUSED, "it was defused"},
    {"spawn.a", T_SPAWN, 0, "team A's spawns"}, {"spawn.b", T_SPAWN, 1, "team B's spawns"}, {"spawn.any", T_SPAWN, 2, "the free for all spawns"},
    {"spawn.race", T_SPAWN, 3, "the race start"}, {"spawn.attack", T_SPAWN, 4, "the attack's staged spawns (the stage reached)"},
    {"spawn.defend", T_SPAWN, 5, "the defence's staged spawns (the stage reached)"},
    {"fell", T_WEAPON, 252, "kill cause: a fall (the void, lava)"}, {"bombed", T_WEAPON, 253, "kill cause: the bomb's blast"},
    {"drained", T_WEAPON, 254, "kill cause: the stalemate drain, damage()"},
    {"dissolved", T_WEAPON, kDamageAcid, "kill cause: acid"},
    {"hill", T_ZONE, MZ_HILL, "the hill"}, {"sites", T_ZONE, MZ_SITES, "any bomb site"},
};
const ConstInfo* constByName(const std::string& n) { for (auto& c : kConsts) if (n == c.name) return &c; return nullptr; }
int weaponByName(const std::string& s) { for (int i = 0; i < WPN_COUNT; i++) if (s == kWeaponNames[i]) return i; return -1; }

const char* const kKeywords[] = {"logic", "base", "param", "curve", "use", "state", "match", "round", "timer", "proc", "slot", "if", "elif", "else", "return",
                                 "pass", "for", "in", "where", "by", "break", "and", "or", "not", "yes", "no", "true", "false", "none", "A", "B"};
bool isKeyword(const std::string& n) { for (auto* k : kKeywords) if (n == k) return true; return false; }
const char* const kSetNames[] = {"everyone", "playing", "team"};

// =====================================================================================================
// Numbers (as botbrain.cpp: the shortest decimal that reads back as the same value, laid out like JavaScript)
// =====================================================================================================

std::string fmtNumber(double v, int type) {
    if (type == T_INT) { char b[32]; snprintf(b, sizeof b, "%lld", (long long)v); return b; }
    if (v == 0) return "0";
    char buf[64];
    int maxp = type == T_F32 ? 9 : 17;
    int p = 1;
    for (; p < maxp; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, v);
        if (type == T_F32 ? std::strtof(buf, nullptr) == (float)v : std::strtod(buf, nullptr) == v) break;
    }
    snprintf(buf, sizeof buf, "%.*e", p - 1, v);
    std::string s = buf, digits;
    bool neg = s[0] == '-';
    size_t i = neg ? 1 : 0;
    for (; i < s.size() && s[i] != 'e'; i++) if (s[i] != '.') digits += s[i];
    int e = atoi(s.c_str() + i + 1);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    int k = (int)digits.size(), n = e + 1;
    std::string out;
    if (k <= n && n <= 21) out = digits + std::string(n - k, '0');
    else if (0 < n && n <= 21) out = digits.substr(0, n) + "." + digits.substr(n);
    else if (-6 < n && n <= 0) out = "0." + std::string(-n, '0') + digits;
    else {
        out = digits.substr(0, 1);
        if (k > 1) out += "." + digits.substr(1);
        out += (n - 1 >= 0 ? "e+" : "e-") + std::to_string(std::abs(n - 1));
    }
    return (neg ? "-" : "") + out;
}
std::string fmtLiteral(double v, int type) {
    std::string s = fmtNumber(v, type);
    if (type == T_INT) return s;
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    if (type == T_F32) s += "f";
    return s;
}

// =====================================================================================================
// AST
// =====================================================================================================

enum NodeOp : uint8_t {
    N_NUM, N_BOOL, N_NAME, N_BIN, N_NEG, N_NOT, N_COND, N_CALL, N_INDEX, N_STR, N_AGG, N_INSET, N_SETX, N_EMPTY,   // expressions (N_SETX: a set)
    N_SET, N_IF, N_RETURN, N_PASS, N_FOR, N_BREAK, N_DO                                                        // statements
};
enum BinOp : uint8_t { B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_LT, B_LE, B_GT, B_GE, B_EQ, B_NE, B_AND, B_OR };
const char* const kBinText[] = {"+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "and", "or"};
int binPrec(int b) { return b <= B_SUB ? 6 : b <= B_MOD ? 7 : b <= B_NE ? 5 : b == B_AND ? 3 : 2; }

struct Node {
    NodeOp op = N_PASS;
    uint8_t bin = 0;            // N_BIN: BinOp; N_SETX: ModeSet
    uint8_t ntype = 0;          // N_NUM: T_INT / T_F32 / T_F64
    bool hasIndex = false, hasWhere = false, hasBy = false;
    double v = 0;
    std::string name;           // N_NAME, N_CALL / N_DO / N_AGG (function), N_INDEX (array), N_SET (target), N_STR (text)
    std::string var;            // N_FOR / N_AGG: the player variable
    std::vector<Node> k;        // N_SET: value, [index]; N_FOR / N_AGG: set, where, by; N_INSET: value, set; N_SETX: [team]
    struct Arm {
        std::vector<std::string> comments;
        std::string trail;
        std::vector<Node> body;
        int line = 0;
    };
    std::vector<Arm> arms;      // N_IF: k[i] is arms[i]'s condition; N_FOR: arms[0] is the body
    bool hasElse = false;
    Arm elseArm;
    std::vector<std::string> comments;
    std::string trail;
    int line = 0, col = 0;
};

struct Decl {
    enum Kind : uint8_t { PARAM, CURVE, USE, STATE, TIMER, PROC, SLOT } kind = PARAM;
    std::string name;                                // PARAM / CURVE / STATE / TIMER / PROC name; USE object; SLOT: timer name for slot timer
    int slot = -1;                                   // SLOT: ModeSlot (MSLOT_TIMER for every timer slot)
    int ptype = T_F32;                               // PARAM / STATE type; CURVE argument type
    double value = 0;                                // PARAM / STATE value
    int life = 0, per = 0;                           // STATE: 0 match / 1 round; 0 scalar / 1 team / 2 player
    std::string arg;                                 // CURVE argument
    int form = 0;                                    // CURVE: 0 expression, 1 steps, 2 points
    double base = 0;
    std::vector<std::pair<double, double>> pts;
    std::vector<std::pair<std::string, int>> args;   // PROC parameters
    std::vector<Node> body;                          // CURVE expr: body[0]; PROC / SLOT: statements
    std::vector<std::string> comments;
    std::string trail;
    int line = 0, col = 0;
    int origin = 0;                                  // (merging) 0 the file, 1 the base mode's default, 2 common
    std::string key() const { return kind == SLOT ? (slot == MSLOT_TIMER ? "slot timer " + name : std::string("slot ") + kSlots[slot].name) : kind == USE ? "use " + name : name; }
};

struct File {
    int version = kModeLogicVersion;
    std::string base;                                // the `base` line ("" when the file has none)
    std::vector<Decl> decls;
    std::vector<std::string> tail;
    // the .qmode around it
    std::string prefix;                              // every line before `logic`, verbatim
    std::vector<std::pair<std::string, std::string>> keys;
    bool hasLogic = false;
};

// =====================================================================================================
// Lexer + parser
// =====================================================================================================

struct Tok {
    enum Kind : uint8_t { NAME, NUM, STR, OP, END } t = END;
    std::string s;
    int col = 0;
};
struct Line {
    int no = 0, indent = 0;
    std::vector<Tok> toks;
    std::string trail;
    std::vector<std::string> comments;
};
struct ParseError { int line, col; std::string msg; };

std::string rtrim(std::string s) { while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back(); return s; }

// (lines before firstLine are skipped: the .qmode's key=value lines)
bool lexLines(const std::string& text, int firstLine, std::vector<Line>& out, std::vector<std::string>& tail, ParseError& err) {
    std::vector<std::string> pending;
    auto addComment = [&](const std::string& c) {
        if (c.empty() && (pending.empty() ? false : pending.back().empty())) return;
        pending.push_back(c);
    };
    std::istringstream in(text);
    std::string raw;
    int no = 0;
    while (std::getline(in, raw)) {
        no++;
        if (no < firstLine) continue;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        size_t hash = std::string::npos;
        bool inStr = false;
        for (size_t i = 0; i < raw.size(); i++) {
            if (raw[i] == '\t') { err = {no, (int)i + 1, "tab: indent with spaces"}; return false; }
            if (raw[i] == '"') inStr = !inStr;
            else if (raw[i] == '#' && !inStr) { hash = i; break; }
        }
        std::string code = hash == std::string::npos ? raw : raw.substr(0, hash);
        std::string trail = hash == std::string::npos ? "" : rtrim(raw.substr(hash));
        size_t ind = 0;
        while (ind < code.size() && code[ind] == ' ') ind++;
        if (rtrim(code).size() <= ind) {
            if (!trail.empty()) addComment(trail);
            else if (!pending.empty() || !out.empty()) addComment("");
            continue;
        }
        Line L;
        L.no = no; L.indent = (int)ind; L.trail = trail;
        L.comments = std::move(pending); pending.clear();
        size_t i = ind;
        while (i < code.size()) {
            char c = code[i];
            if (c == ' ') { i++; continue; }
            Tok t;
            t.col = (int)i + 1;
            if (isalpha((unsigned char)c) || c == '_') {
                size_t j = i;
                for (;;) {
                    while (j < code.size() && (isalnum((unsigned char)code[j]) || code[j] == '_')) j++;
                    if (j + 1 < code.size() && code[j] == '.' && (isalpha((unsigned char)code[j + 1]) || code[j + 1] == '_')) { j++; continue; }
                    break;
                }
                t.t = Tok::NAME; t.s = code.substr(i, j - i); i = j;
            } else if (isdigit((unsigned char)c)) {
                size_t j = i;
                while (j < code.size() && isdigit((unsigned char)code[j])) j++;
                if (j < code.size() && code[j] == '.') {
                    j++;
                    if (j >= code.size() || !isdigit((unsigned char)code[j])) { err = {no, (int)j + 1, "a digit after the '.'"}; return false; }
                    while (j < code.size() && isdigit((unsigned char)code[j])) j++;
                }
                if (j < code.size() && (code[j] == 'e' || code[j] == 'E')) {
                    size_t e = j + 1;
                    if (e < code.size() && (code[e] == '+' || code[e] == '-')) e++;
                    if (e < code.size() && isdigit((unsigned char)code[e])) { j = e; while (j < code.size() && isdigit((unsigned char)code[j])) j++; }
                }
                if (j < code.size() && code[j] == 'f') j++;
                if (j < code.size() && (isalnum((unsigned char)code[j]) || code[j] == '_' || code[j] == '.')) { err = {no, (int)j + 1, "bad number"}; return false; }
                t.t = Tok::NUM; t.s = code.substr(i, j - i); i = j;
            } else if (c == '"') {
                size_t j = code.find('"', i + 1);
                if (j == std::string::npos) { err = {no, (int)i + 1, "unterminated string"}; return false; }
                t.t = Tok::STR; t.s = code.substr(i + 1, j - i - 1); i = j + 1;
                if (t.s.find('\\') != std::string::npos || t.s.size() > 120) { err = {no, t.col, "a string: at most 120 bytes, no \\"}; return false; }
            } else {
                static const char* two[] = {"<=", ">=", "==", "!="};
                std::string op;
                for (auto* o : two) if (code.compare(i, 2, o) == 0) op = o;
                if (op.empty()) {
                    if (!strchr("()[],:=?+-*/%<>", c)) { err = {no, (int)i + 1, std::string("unexpected '") + c + "'"}; return false; }
                    op = std::string(1, c);
                }
                t.t = Tok::OP; t.s = op; i += op.size();
            }
            L.toks.push_back(t);
        }
        out.push_back(std::move(L));
    }
    while (!pending.empty() && pending.back().empty()) pending.pop_back();
    tail = std::move(pending);
    return true;
}

int typeByName(const std::string& t) {
    static const char* n[] = {"bool", "int", "f32", "f64", "team", "player"};
    static const int v[] = {T_BOOL, T_INT, T_F32, T_F64, T_TEAM, T_PLAYER};
    for (int i = 0; i < 6; i++) if (t == n[i]) return v[i];
    return -1;
}

struct Parser {
    std::vector<Line> lines;
    size_t li = 0;
    const Line* L = nullptr;
    size_t ti = 0;
    ParseError err{0, 0, ""};
    bool failed = false;

    bool fail(int col, const std::string& m) { if (!failed) { failed = true; err = {L ? L->no : 0, col, m}; } return false; }
    const Tok& tok() { static Tok end; if (ti < L->toks.size()) return L->toks[ti]; end.col = L->toks.empty() ? 1 : L->toks.back().col + (int)L->toks.back().s.size() + (L->toks.back().t == Tok::STR ? 2 : 0); return end; }
    const Tok& peek(size_t n) { static Tok end; return ti + n < L->toks.size() ? L->toks[ti + n] : end; }
    bool isOp(const char* o) { const Tok& t = tok(); return t.t == Tok::OP && t.s == o; }
    bool isName(const char* n) { const Tok& t = tok(); return t.t == Tok::NAME && t.s == n; }
    bool expectOp(const char* o) { if (!isOp(o)) return fail(tok().col, std::string("expected '") + o + "'"); ti++; return true; }
    bool atEnd() { return ti >= L->toks.size(); }
    bool expectEnd() { if (!atEnd()) return fail(tok().col, "unexpected '" + tok().s + "'"); return true; }
    bool name(std::string& out, const char* what) {
        if (tok().t != Tok::NAME) return fail(tok().col, std::string("expected ") + what);
        out = tok().s; ti++; return true;
    }

    bool dataNum(double& v, int type) {
        bool neg = false;
        int col = tok().col;
        if (isOp("-")) { neg = true; ti++; }
        const Tok& t = tok();
        if (t.t != Tok::NUM) return fail(col, "expected a number");
        if (t.s.back() == 'f') return fail(t.col, "no 'f' here: the type is declared");
        std::string s = (neg ? "-" : "") + t.s;
        errno = 0;
        if (type == T_INT) {
            if (t.s.find_first_of(".eE") != std::string::npos) return fail(t.col, "an int: no '.' or exponent");
            long long n = strtoll(s.c_str(), nullptr, 10);
            if (errno || n < -2147483647LL || n > 2147483647LL) return fail(t.col, "out of range");
            v = (double)n;
        } else if (type == T_F32) {
            float f = strtof(s.c_str(), nullptr);
            if (errno || std::isinf(f)) return fail(t.col, "out of range for f32");
            v = f;
        } else {
            v = strtod(s.c_str(), nullptr);
            if (errno || std::isinf(v)) return fail(t.col, "out of range");
        }
        ti++;
        return true;
    }
    // a declared value of a type: numbers, yes / no, A / B / none
    bool dataValue(double& v, int type) {
        if (type == T_BOOL) {
            std::string b;
            if (!name(b, "yes or no")) return false;
            if (b != "yes" && b != "no" && b != "true" && b != "false") return fail(L->toks[ti - 1].col, "yes or no");
            v = (b == "yes" || b == "true") ? 1 : 0;
            return true;
        }
        if (type == T_TEAM || type == T_PLAYER) {
            std::string b;
            if (!name(b, type == T_TEAM ? "A, B or none" : "none")) return false;
            if (b == "none") { v = -1; return true; }
            if (type == T_TEAM && (b == "A" || b == "B")) { v = b == "A" ? 0 : 1; return true; }
            return fail(L->toks[ti - 1].col, type == T_TEAM ? "A, B or none" : "a player starts as none");
        }
        return dataNum(v, type);
    }

    // ---- expressions
    bool expr(Node& n) { return ternary(n); }
    void setPos(Node& n, int col) { n.line = L->no; n.col = col; }
    int depth = 0;
    struct Nest { int& d; ~Nest() { d--; } };
    bool ternary(Node& n) {
        int col = tok().col;
        Nest nest{++depth};
        if (depth > 48) return fail(col, "nested too deep");
        if (!orExpr(n)) return false;
        if (!isOp("?")) return true;
        ti++;
        Node c = std::move(n), a, b;
        if (!expr(a) || !expectOp(":") || !ternary(b)) return false;
        n = Node(); n.op = N_COND; setPos(n, col);
        n.k.push_back(std::move(c)); n.k.push_back(std::move(a)); n.k.push_back(std::move(b));
        return true;
    }
    bool binLevel(Node& n, bool (Parser::*sub)(Node&), const char* word, uint8_t bop) {
        int col = tok().col;
        if (!(this->*sub)(n)) return false;
        while (isName(word)) {
            ti++;
            Node r;
            if (!(this->*sub)(r)) return false;
            Node x; x.op = N_BIN; x.bin = bop; setPos(x, col);
            x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
            n = std::move(x);
        }
        return true;
    }
    bool orExpr(Node& n) { return binLevel(n, &Parser::andExpr, "or", B_OR); }
    bool andExpr(Node& n) { return binLevel(n, &Parser::notExpr, "and", B_AND); }
    bool notExpr(Node& n) {
        if (isName("not")) {
            int col = tok().col; ti++;
            Nest nest{++depth};
            if (depth > 48) return fail(col, "nested too deep");
            Node a;
            if (!notExpr(a)) return false;
            n = Node(); n.op = N_NOT; setPos(n, col); n.k.push_back(std::move(a));
            return true;
        }
        return cmpExpr(n);
    }
    bool setExpr(Node& s) {
        s = Node(); s.op = N_SETX; setPos(s, tok().col);
        if (isName("everyone")) { s.bin = MSET_EVERYONE; ti++; return true; }
        if (isName("playing")) { s.bin = MSET_PLAYING; ti++; return true; }
        if (isName("team")) {
            s.bin = MSET_TEAM; ti++;
            Node t;
            if (!expectOp("(") || !expr(t) || !expectOp(")")) return false;
            s.k.push_back(std::move(t));
            return true;
        }
        return fail(tok().col, "a set: everyone, playing or team(t)");
    }
    bool cmpExpr(Node& n) {
        int col = tok().col;
        if (!sumExpr(n)) return false;
        static const char* ops[] = {"<", "<=", ">", ">=", "==", "!="};
        for (int k = 0; k < 6; k++)
            if (isOp(ops[k])) {
                ti++;
                Node r;
                if (!sumExpr(r)) return false;
                Node x; x.op = N_BIN; x.bin = (uint8_t)(B_LT + k); setPos(x, col);
                x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
                n = std::move(x);
                for (auto* o : ops) if (isOp(o)) return fail(tok().col, "comparisons don't chain: use and");
                if (isName("in")) return fail(tok().col, "comparisons don't chain");
                return true;
            }
        if (isName("in")) {
            ti++;
            Node s;
            if (!setExpr(s)) return false;
            Node x; x.op = N_INSET; setPos(x, col); x.k.push_back(std::move(n)); x.k.push_back(std::move(s));
            n = std::move(x);
            for (auto* o : ops) if (isOp(o)) return fail(tok().col, "comparisons don't chain");
        }
        return true;
    }
    bool sumExpr(Node& n) {
        int col = tok().col;
        if (!prodExpr(n)) return false;
        while (isOp("+") || isOp("-")) {
            uint8_t b = isOp("+") ? B_ADD : B_SUB; ti++;
            Node r;
            if (!prodExpr(r)) return false;
            Node x; x.op = N_BIN; x.bin = b; setPos(x, col);
            x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
            n = std::move(x);
        }
        return true;
    }
    bool prodExpr(Node& n) {
        int col = tok().col;
        if (!unary(n)) return false;
        while (isOp("*") || isOp("/") || isOp("%")) {
            uint8_t b = isOp("*") ? B_MUL : isOp("/") ? B_DIV : B_MOD; ti++;
            Node r;
            if (!unary(r)) return false;
            Node x; x.op = N_BIN; x.bin = b; setPos(x, col);
            x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
            n = std::move(x);
        }
        return true;
    }
    bool unary(Node& n) {
        if (isOp("-")) {
            int col = tok().col; ti++;
            Nest nest{++depth};
            if (depth > 48) return fail(col, "nested too deep");
            if (tok().t == Tok::NUM) { if (!number(n, true)) return false; n.col = col; return true; }
            Node a;
            if (!unary(a)) return false;
            n = Node(); n.op = N_NEG; setPos(n, col); n.k.push_back(std::move(a));
            return true;
        }
        return primary(n);
    }
    bool number(Node& n, bool neg) {
        const Tok& t = tok();
        n = Node(); n.op = N_NUM; setPos(n, t.col);
        std::string s = (neg ? "-" : "") + t.s;
        errno = 0;
        if (t.s.back() == 'f') {
            n.ntype = T_F32;
            s.pop_back();
            float f = strtof(s.c_str(), nullptr);
            if (errno || std::isinf(f)) return fail(t.col, "out of range for f32");
            n.v = f;
        } else if (t.s.find_first_of(".eE") != std::string::npos) {
            n.ntype = T_F64;
            n.v = strtod(s.c_str(), nullptr);
            if (errno || std::isinf(n.v)) return fail(t.col, "out of range");
        } else {
            n.ntype = T_INT;
            long long v = strtoll(s.c_str(), nullptr, 10);
            if (errno || v < -2147483647LL || v > 2147483647LL) return fail(t.col, "out of range for int");
            n.v = (double)v;
        }
        ti++;
        return true;
    }
    bool args(Node& n) {
        if (!isOp(")"))
            for (;;) {
                Node a;
                if (!expr(a)) return false;
                n.k.push_back(std::move(a));
                if (isOp(",")) { ti++; continue; }
                break;
            }
        return expectOp(")");
    }
    bool primary(Node& n) {
        const Tok& t = tok();
        if (t.t == Tok::NUM) return number(n, false);
        if (t.t == Tok::STR) { n = Node(); n.op = N_STR; n.name = t.s; setPos(n, t.col); ti++; return true; }
        if (t.t == Tok::OP && t.s == "(") {
            ti++;
            if (!expr(n) || !expectOp(")")) return false;
            return true;
        }
        if (t.t != Tok::NAME) return fail(t.col, atEnd() ? "expected a value" : "unexpected '" + t.s + "'");
        int col = t.col;
        std::string nm = t.s;
        ti++;
        if (nm == "yes" || nm == "true" || nm == "no" || nm == "false") {
            n = Node(); n.op = N_BOOL; n.v = (nm == "yes" || nm == "true") ? 1 : 0; setPos(n, col); return true;
        }
        if (isKeyword(nm) && nm != "none" && nm != "A" && nm != "B") return fail(col, "'" + nm + "' can't be used here");
        n = Node(); setPos(n, col);
        if (isOp("(") && isAggregate(nm) && peek(1).t == Tok::NAME && peek(2).t == Tok::NAME && peek(2).s == "in") {
            ti++;
            n.op = N_AGG; n.name = nm; n.var = tok().s; ti += 2;
            Node s;
            if (!setExpr(s)) return false;
            n.k.push_back(std::move(s));
            Node w, b; w.op = b.op = N_EMPTY;
            if (isName("where")) { ti++; n.hasWhere = true; if (!expr(w)) return false; }
            if (isName("by")) { ti++; n.hasBy = true; if (!expr(b)) return false; }
            n.k.push_back(std::move(w)); n.k.push_back(std::move(b));
            return expectOp(")");
        }
        if (isOp("(")) { ti++; n.op = N_CALL; n.name = nm; return args(n); }
        if (isOp("[")) {
            ti++;
            n.op = N_INDEX; n.name = nm;
            Node a;
            if (!expr(a) || !expectOp("]")) return false;
            n.k.push_back(std::move(a));
            return true;
        }
        n.op = N_NAME; n.name = nm;
        return true;
    }

    // ---- statements
    bool simple(Node& n) {
        const Tok& t = tok();
        int col = t.col;
        n = Node(); n.line = L->no; n.col = col;
        if (t.t != Tok::NAME) return fail(col, "expected a statement");
        std::string w = t.s;
        if (w == "return") {
            ti++; n.op = N_RETURN;
            if (atEnd()) return true;
            Node e;
            if (!expr(e)) return false;
            n.k.push_back(std::move(e));
            return expectEnd();
        }
        if (w == "pass" || w == "break") { ti++; n.op = w == "pass" ? N_PASS : N_BREAK; return expectEnd(); }
        if (w == "if" || w == "elif" || w == "else" || w == "for") return fail(col, "'" + w + "' here needs its own line");
        if (isKeyword(w)) return fail(col, "unexpected '" + w + "'");
        ti++;
        if (isOp("(")) {
            ti++;
            n.op = N_DO; n.name = w;
            if (!args(n)) return false;
            return expectEnd();
        }
        n.op = N_SET; n.name = w;
        Node idx;
        if (isOp("[")) {
            ti++;
            if (!expr(idx) || !expectOp("]")) return false;
            n.hasIndex = true;
        }
        if (!isOp("=")) return fail(tok().col, "expected '=' (an assignment) or '(' (an action)");
        ti++;
        Node e;
        if (!expr(e)) return false;
        n.k.push_back(std::move(e));
        if (n.hasIndex) n.k.push_back(std::move(idx));
        return expectEnd();
    }
    bool armBody(Node::Arm& arm, int indent) {
        arm.trail = L->trail;
        if (!atEnd()) {
            Node s;
            if (!simple(s)) return false;
            arm.body.push_back(std::move(s));
            return true;
        }
        li++;
        if (li >= lines.size() || lines[li].indent <= indent) { L = li < lines.size() ? &lines[li] : L; return fail(1, "expected an indented block"); }
        int inner = lines[li].indent;
        if (!block(arm.body, inner)) return false;
        li--;
        return true;
    }
    bool stmt(std::vector<Node>& out, int indent) {
        L = &lines[li]; ti = 0;
        Node n;
        n.comments = L->comments;
        n.line = L->no; n.col = L->toks[0].col;
        if (isName("elif") || isName("else")) return fail(tok().col, "'" + tok().s + "' without an if");
        if (isName("for")) {
            ti++;
            n.op = N_FOR;
            if (!name(n.var, "the player variable") ) return false;
            if (!isName("in")) return fail(tok().col, "expected 'in'");
            ti++;
            Node s;
            if (!setExpr(s)) return false;
            n.k.push_back(std::move(s));
            Node w; w.op = N_EMPTY;
            if (isName("where")) { ti++; n.hasWhere = true; if (!expr(w)) return false; }
            n.k.push_back(std::move(w));
            if (!expectOp(":")) return false;
            Node::Arm arm;
            arm.line = L->no;
            if (!armBody(arm, indent)) return false;
            n.trail = arm.trail;
            n.arms.push_back(std::move(arm));
            out.push_back(std::move(n));
            li++;
            return true;
        }
        if (isName("if")) {
            ti++;
            n.op = N_IF;
            for (bool first = true;; first = false) {
                Node cond;
                if (!expr(cond) || !expectOp(":")) return false;
                Node::Arm arm;
                arm.line = L->no;
                if (!first) arm.comments = L->comments;
                if (!armBody(arm, indent)) return false;
                n.k.push_back(std::move(cond));
                n.arms.push_back(std::move(arm));
                li++;
                if (li < lines.size() && lines[li].indent == indent && !lines[li].toks.empty() && lines[li].toks[0].t == Tok::NAME) {
                    const std::string& w = lines[li].toks[0].s;
                    if (w == "elif") { L = &lines[li]; ti = 1; continue; }
                    if (w == "else") {
                        L = &lines[li]; ti = 1;
                        if (!expectOp(":")) return false;
                        n.hasElse = true;
                        n.elseArm.line = L->no;
                        n.elseArm.comments = L->comments;
                        if (!armBody(n.elseArm, indent)) return false;
                        li++;
                    }
                }
                break;
            }
            out.push_back(std::move(n));
            return true;
        }
        Node s;
        if (!simple(s)) return false;
        s.comments = std::move(n.comments);
        s.trail = L->trail;
        out.push_back(std::move(s));
        li++;
        return true;
    }
    bool block(std::vector<Node>& out, int indent) {
        Nest nest{++depth};
        if (depth > 48) { L = li < lines.size() ? &lines[li] : L; return fail(indent + 1, "nested too deep"); }
        while (li < lines.size()) {
            const Line& l = lines[li];
            if (l.indent < indent) break;
            if (l.indent > indent) { L = &l; return fail(l.indent + 1, "unexpected indent"); }
            if (!stmt(out, indent)) return false;
        }
        return true;
    }
    bool bodyBlock(Decl& d) {
        if (!expectOp(":") || !expectEnd()) return false;
        li++;
        if (li >= lines.size() || lines[li].indent == 0) return fail(1, "expected an indented block");
        return block(d.body, lines[li].indent);
    }

    // ---- declarations
    bool decl(File& f) {
        L = &lines[li]; ti = 0;
        Decl d;
        d.comments = L->comments; d.trail = L->trail; d.line = L->no; d.col = L->toks[0].col;
        if (L->indent != 0) return fail(L->indent + 1, "unexpected indent");
        std::string w;
        if (!name(w, "a declaration (param, curve, use, state, timer, proc, slot)")) return false;
        if (w == "param") {
            d.kind = Decl::PARAM;
            std::string t;
            if (!name(d.name, "a parameter name") || !name(t, "a type (f32, f64, int, bool)")) return false;
            d.ptype = typeByName(t);
            if (d.ptype < 0 || d.ptype > T_F64) return fail(L->toks[ti - 1].col, "a parameter type is f32, f64, int or bool");
            if (!expectOp("=") || !dataValue(d.value, d.ptype)) return false;
        } else if (w == "curve") {
            d.kind = Decl::CURVE;
            std::string t;
            if (!name(d.name, "a curve name") || !expectOp("(") || !name(d.arg, "the argument's name") || !name(t, "its type (f32 or f64)") || !expectOp(")") || !expectOp("="))
                return false;
            d.ptype = t == "f32" ? T_F32 : t == "f64" ? T_F64 : -1;
            if (d.ptype < 0) return fail(L->toks[ti - 3].col, "a curve's argument is f32 or f64");
            size_t save = ti;
            bool data = false;
            if ((isName("steps") || isName("points")) && ti + 1 < L->toks.size()) {
                const Tok& nx = L->toks[ti + 1];
                data = nx.t == Tok::NUM || (nx.t == Tok::OP && nx.s == "-");
            }
            if (data) {
                d.form = isName("steps") ? 1 : 2;
                if (d.ptype != T_F32) return fail(tok().col, "steps / points curves take an f32");
                ti++;
                if (d.form == 1) { if (!dataNum(d.base, T_F32)) return false; if (!atEnd() && !expectOp(",")) return false; }
                while (!atEnd()) {
                    double x, y;
                    if (!dataNum(x, T_F32) || !expectOp(":") || !dataNum(y, T_F32)) return false;
                    d.pts.push_back({x, y});
                    if (atEnd()) break;
                    if (!expectOp(",")) return false;
                }
                if (d.pts.size() > 16 || (d.form == 2 && d.pts.empty())) return fail(1, d.form == 2 ? "1 to 16 points" : "at most 16 steps");
                for (size_t k = 1; k < d.pts.size(); k++)
                    if (!(d.pts[k].first > d.pts[k - 1].first)) return fail(1, "x must go up");
            } else {
                ti = save;
                d.form = 0;
                Node e;
                if (!expr(e)) return false;
                d.body.push_back(std::move(e));
            }
        } else if (w == "use") {
            d.kind = Decl::USE;
            if (!name(d.name, "an object (bomb, cart, ncart, points, race, drain, it, tiebreak, bracket, duelmods)")) return false;
            if (objectByName(d.name) < 0) return fail(L->toks[ti - 1].col, "'" + d.name + "' is not an object (bomb, cart, ncart, points, race, drain, it, tiebreak, bracket, duelmods)");
        } else if (w == "state") {
            d.kind = Decl::STATE;
            std::string life, t;
            if (!name(life, "match or round")) return false;
            if (life != "match" && life != "round") return fail(L->toks[ti - 1].col, "state lasts a match or a round");
            d.life = life == "round";
            if (!name(d.name, "a state name")) return false;
            if (isOp("[")) {
                ti++;
                std::string per;
                if (!name(per, "team or player") ) return false;
                if (per != "team" && per != "player") return fail(L->toks[ti - 1].col, "state is per team or per player");
                d.per = per == "team" ? 1 : 2;
                if (!expectOp("]")) return false;
            }
            if (!name(t, "a type (bool, int, f32, f64, team, player)")) return false;
            d.ptype = typeByName(t);
            if (d.ptype < 0) return fail(L->toks[ti - 1].col, "a state type is bool, int, f32, f64, team or player");
            if (!expectOp("=") || !dataValue(d.value, d.ptype)) return false;
        } else if (w == "timer") {
            d.kind = Decl::TIMER;
            if (!name(d.name, "a timer name")) return false;
        } else if (w == "proc") {
            d.kind = Decl::PROC;
            if (!name(d.name, "a proc name")) return false;
            if (isOp("(")) {
                ti++;
                for (;;) {
                    std::string an, at;
                    if (!name(an, "a parameter name") || !name(at, "its type (bool, int, f32, f64, team, player)")) return false;
                    int ty = typeByName(at);
                    if (ty < 0) return fail(L->toks[ti - 1].col, "a proc parameter is bool, int, f32, f64, team or player");
                    for (auto& a : d.args) if (a.first == an) return fail(L->toks[ti - 2].col, "'" + an + "' twice");
                    d.args.push_back({an, ty});
                    if (isOp(",")) { ti++; continue; }
                    break;
                }
                if (!expectOp(")")) return false;
                if (d.args.size() > 4) return fail(tok().col, "at most 4 parameters");
            }
            if (!bodyBlock(d)) return false;
            f.decls.push_back(std::move(d));
            return true;
        } else if (w == "slot") {
            d.kind = Decl::SLOT;
            int col = tok().col;
            std::string sn;
            if (!name(sn, "a slot name")) return false;
            if (sn == "timer") {
                d.slot = MSLOT_TIMER;
                if (!name(d.name, "the timer's name")) return false;
            } else {
                for (int k = 0; k < MSLOT_TIMER; k++) if (sn == kSlots[k].name) d.slot = k;
                if (d.slot < 0) return fail(col, "'" + sn + "' is not a slot");
            }
            if (!bodyBlock(d)) return false;
            f.decls.push_back(std::move(d));
            return true;
        } else {
            return fail(L->toks[0].col, "expected a declaration (param, curve, use, state, timer, proc, slot)");
        }
        if (!expectEnd()) return false;
        f.decls.push_back(std::move(d));
        li++;
        return true;
    }

    bool file(const std::string& text, int firstLine, File& f) {
        if (!lexLines(text, firstLine, lines, f.tail, err)) { failed = true; return false; }
        if (lines.empty()) { err = {firstLine > 0 ? firstLine : 1, 1, "empty logic: it starts with 'logic N'"}; failed = true; return false; }
        for (const Line& l : lines)
            if (l.toks.size() > 512) { err = {l.no, l.toks[512].col, "line too long: over 512 tokens"}; failed = true; return false; }
        L = &lines[0]; ti = 0;
        if (!L->comments.empty() || !L->trail.empty()) return fail(1, "the logic starts with 'logic N' (no comments before or on it)");
        if (!isName("logic")) return fail(tok().col, "the logic starts with 'logic N'");
        ti++;
        if (tok().t != Tok::NUM || tok().s.find_first_not_of("0123456789") != std::string::npos) return fail(tok().col, "a version number");
        f.version = atoi(tok().s.c_str()); ti++;
        if (!expectEnd()) return false;
        li = 1;
        if (li < lines.size() && !lines[li].toks.empty() && lines[li].toks[0].t == Tok::NAME && lines[li].toks[0].s == "base") {
            L = &lines[li]; ti = 1;
            if (!L->comments.empty() || !L->trail.empty()) return fail(1, "no comments in the header");
            if (!name(f.base, "a mode (vs bomb race koth payload ffa npayload duel arena tourney hide tag points), or common")) return false;
            if (f.base != "common" && modeBaseByName(f.base) < 0) return fail(L->toks[1].col, "'" + f.base + "' is not a mode");
            if (!expectEnd()) return false;
            li++;
        }
        while (li < lines.size()) if (!decl(f)) return false;
        return true;
    }
};

// =====================================================================================================
// Canonical printer
// =====================================================================================================

struct Printer {
    bool comments = true;
    std::string out;

    int prec(const Node& n) {
        switch (n.op) {
            case N_COND: return 1;
            case N_BIN: return binPrec(n.bin);
            case N_NOT: return 4;
            case N_INSET: return 5;
            case N_NEG: return 8;
            case N_NUM: return n.v < 0 || (n.v == 0 && std::signbit(n.v)) ? 8 : 9;
            default: return 9;
        }
    }
    std::string sub(const Node& c, int minPrec, bool force = false) {
        std::string s = ex(c);
        return force || prec(c) < minPrec ? "(" + s + ")" : s;
    }
    std::string set(const Node& s) {
        if (s.bin == MSET_TEAM) return "team(" + (s.k.empty() ? std::string("?") : ex(s.k[0])) + ")";
        return kSetNames[s.bin < 3 ? s.bin : 0];
    }
    std::string ex(const Node& n) {
        switch (n.op) {
            case N_NUM: return fmtLiteral(n.v, n.ntype);
            case N_BOOL: return n.v ? "yes" : "no";
            case N_NAME: return n.name;
            case N_STR: return "\"" + n.name + "\"";
            case N_NEG: return "-" + sub(n.k[0], 8);
            case N_NOT: {
                const Node& a = n.k[0];
                bool force = a.op == N_INSET || (a.op == N_BIN && binPrec(a.bin) == 5);
                return "not " + sub(a, 4, force);
            }
            case N_INSET: return sub(n.k[0], 6) + " in " + set(n.k[1]);
            case N_COND: return sub(n.k[0], 2, n.k[0].op == N_COND) + " ? " + sub(n.k[1], 1, n.k[1].op == N_COND) + " : " + sub(n.k[2], 1);
            case N_CALL: {
                std::string s = n.name + "(";
                for (size_t i = 0; i < n.k.size(); i++) s += (i ? ", " : "") + ex(n.k[i]);
                return s + ")";
            }
            case N_AGG: {
                std::string s = n.name + "(" + n.var + " in " + set(n.k[0]);
                if (n.hasWhere) s += " where " + ex(n.k[1]);
                if (n.hasBy) s += " by " + ex(n.k[2]);
                return s + ")";
            }
            case N_INDEX: return n.name + "[" + ex(n.k[0]) + "]";
            case N_BIN: {
                int p = binPrec(n.bin);
                const Node &a = n.k[0], &b = n.k[1];
                if (p == 5) return sub(a, 6) + " " + kBinText[n.bin] + " " + sub(b, 6);
                bool fa = n.bin == B_OR && a.op == N_BIN && a.bin == B_AND, fb = n.bin == B_OR && b.op == N_BIN && b.bin == B_AND;
                return sub(a, p, fa) + " " + kBinText[n.bin] + " " + sub(b, p + 1, fb);
            }
            default: return "?";
        }
    }
    void commentLines(const std::vector<std::string>& c, const std::string& ind) {
        if (!comments) return;
        for (auto& l : c) out += l.empty() ? "\n" : ind + l + "\n";
    }
    std::string trailOf(const std::string& t) { return comments && !t.empty() ? " " + t : ""; }
    std::string simpleText(const Node& s) {
        switch (s.op) {
            case N_SET: return s.name + (s.hasIndex ? "[" + ex(s.k[1]) + "]" : "") + " = " + ex(s.k[0]);
            case N_RETURN: return s.k.empty() ? "return" : "return " + ex(s.k[0]);
            case N_BREAK: return "break";
            case N_DO: {
                std::string r = s.name + "(";
                for (size_t i = 0; i < s.k.size(); i++) r += (i ? ", " : "") + ex(s.k[i]);
                return r + ")";
            }
            default: return "pass";
        }
    }
    bool inlineArm(const Node::Arm& a) {
        return a.body.size() == 1 && a.body[0].op != N_IF && a.body[0].op != N_FOR && (!comments || (a.body[0].comments.empty() && a.body[0].trail.empty()));
    }
    void arm(const std::string& head, const Node::Arm& a, const std::string& ind) {
        if (inlineArm(a)) { out += ind + head + " " + simpleText(a.body[0]) + trailOf(a.trail) + "\n"; return; }
        out += ind + head + trailOf(a.trail) + "\n";
        block(a.body, ind + "  ");
    }
    void stmt(const Node& s, const std::string& ind) {
        commentLines(s.comments, ind);
        if (s.op == N_FOR) {
            out += ind + "for " + s.var + " in " + set(s.k[0]) + (s.hasWhere ? " where " + ex(s.k[1]) : "") + ":" + trailOf(s.arms[0].trail) + "\n";
            block(s.arms[0].body, ind + "  ");
            return;
        }
        if (s.op != N_IF) { out += ind + simpleText(s) + trailOf(s.trail) + "\n"; return; }
        for (size_t i = 0; i < s.arms.size(); i++) {
            if (i) commentLines(s.arms[i].comments, ind);
            arm(std::string(i ? "elif " : "if ") + ex(s.k[i]) + ":", s.arms[i], ind);
        }
        if (s.hasElse) { commentLines(s.elseArm.comments, ind); arm("else:", s.elseArm, ind); }
    }
    void block(const std::vector<Node>& b, const std::string& ind) { for (auto& s : b) stmt(s, ind); }
    std::string value(double v, int t) {
        if (t == T_BOOL) return v ? "yes" : "no";
        if (t == T_TEAM) return v == 0 ? "A" : v == 1 ? "B" : "none";
        if (t == T_PLAYER) return "none";
        return fmtNumber(v, t);
    }
    void decl(const Decl& d) {
        commentLines(d.comments, "");
        switch (d.kind) {
            case Decl::PARAM: out += "param " + d.name + " " + typeName(d.ptype) + " = " + value(d.value, d.ptype); break;
            case Decl::CURVE: {
                out += "curve " + d.name + "(" + d.arg + " " + typeName(d.ptype) + ") = ";
                if (d.form == 0) out += ex(d.body[0]);
                else {
                    out += d.form == 1 ? "steps " + fmtNumber(d.base, T_F32) : "points";
                    for (size_t i = 0; i < d.pts.size(); i++)
                        out += std::string(i || d.form == 1 ? ", " : " ") + fmtNumber(d.pts[i].first, T_F32) + ": " + fmtNumber(d.pts[i].second, T_F32);
                }
                break;
            }
            case Decl::USE: out += "use " + d.name; break;
            case Decl::STATE:
                out += std::string("state ") + (d.life ? "round " : "match ") + d.name + (d.per == 1 ? "[team]" : d.per == 2 ? "[player]" : "") + " " + typeName(d.ptype) + " = " + value(d.value, d.ptype);
                break;
            case Decl::TIMER: out += "timer " + d.name; break;
            case Decl::PROC: {
                out += "proc " + d.name;
                if (!d.args.empty()) {
                    out += "(";
                    for (size_t i = 0; i < d.args.size(); i++) out += (i ? ", " : "") + d.args[i].first + " " + typeName(d.args[i].second);
                    out += ")";
                }
                out += ":" + trailOf(d.trail) + "\n";
                block(d.body, "  ");
                return;
            }
            case Decl::SLOT:
                out += "slot " + (d.slot == MSLOT_TIMER ? "timer " + d.name : std::string(kSlots[d.slot].name)) + ":" + trailOf(d.trail) + "\n";
                block(d.body, "  ");
                return;
        }
        out += trailOf(d.trail) + "\n";
    }
    std::string logic(const File& f) {
        out = "logic " + std::to_string(f.version) + "\n";
        if (!f.base.empty()) out += "base " + f.base + "\n";
        for (auto& d : f.decls) decl(d);
        commentLines(f.tail, "");
        return out;
    }
};

// =====================================================================================================
// JSON
// =====================================================================================================

std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

struct JsonAst {
    int nextId = 0;
    std::string id() { return "\"id\":\"n" + std::to_string(++nextId) + "\""; }
    std::string commentsOf(const std::vector<std::string>& c, const std::string& trail) {
        std::string o;
        if (!c.empty()) { o += ",\"comments\":["; for (size_t i = 0; i < c.size(); i++) o += (i ? "," : "") + jstr(c[i]); o += "]"; }
        if (!trail.empty()) o += ",\"trail\":" + jstr(trail);
        return o;
    }
    std::function<std::string(const std::string&)> refOf;
    std::string set(const Node& s) {
        std::string o = "{\"set\":\"" + std::string(kSetNames[s.bin < 3 ? s.bin : 0]) + "\"";
        if (s.bin == MSET_TEAM && !s.k.empty()) o += ",\"team\":" + ex(s.k[0]);
        return o + "}";
    }
    std::string ex(const Node& n) {
        std::string o = "{" + id() + ",";
        switch (n.op) {
            case N_NUM: return o + "\"op\":\"num\",\"type\":\"" + typeName(n.ntype) + "\",\"v\":" + fmtNumber(n.v, n.ntype) + "}";
            case N_BOOL: return o + "\"op\":\"bool\",\"v\":" + (n.v ? "true" : "false") + "}";
            case N_STR: return o + "\"op\":\"str\",\"v\":" + jstr(n.name) + "}";
            case N_NAME: {
                std::string r = refOf ? refOf(n.name) : "";
                return o + "\"op\":\"name\",\"name\":" + jstr(n.name) + (r.empty() ? "" : ",\"ref\":" + jstr(r)) + "}";
            }
            case N_NEG: return o + "\"op\":\"neg\",\"a\":" + ex(n.k[0]) + "}";
            case N_NOT: return o + "\"op\":\"not\",\"a\":" + ex(n.k[0]) + "}";
            case N_COND: return o + "\"op\":\"?:\",\"c\":" + ex(n.k[0]) + ",\"a\":" + ex(n.k[1]) + ",\"b\":" + ex(n.k[2]) + "}";
            case N_INSET: return o + "\"op\":\"in\",\"a\":" + ex(n.k[0]) + ",\"set\":" + set(n.k[1]) + "}";
            case N_CALL: {
                o += "\"op\":\"call\",\"fn\":" + jstr(n.name) + ",\"args\":[";
                for (size_t i = 0; i < n.k.size(); i++) o += (i ? "," : "") + ex(n.k[i]);
                return o + "]}";
            }
            case N_AGG: {
                o += "\"op\":\"agg\",\"fn\":" + jstr(n.name) + ",\"var\":" + jstr(n.var) + ",\"set\":" + set(n.k[0]);
                if (n.hasWhere) o += ",\"where\":" + ex(n.k[1]);
                if (n.hasBy) o += ",\"by\":" + ex(n.k[2]);
                return o + "}";
            }
            case N_INDEX: return o + "\"op\":\"index\",\"array\":" + jstr(n.name) + ",\"index\":" + ex(n.k[0]) + "}";
            case N_BIN: return o + "\"op\":\"" + kBinText[n.bin] + "\",\"a\":" + ex(n.k[0]) + ",\"b\":" + ex(n.k[1]) + "}";
            default: return o + "\"op\":\"?\"}";
        }
    }
    std::string body(const std::vector<Node>& b) {
        std::string o = "[";
        for (size_t i = 0; i < b.size(); i++) o += (i ? "," : "") + st(b[i]);
        return o + "]";
    }
    std::string st(const Node& s) {
        std::string o = "{" + id() + ",";
        switch (s.op) {
            case N_SET:
                o += "\"op\":\"set\",\"name\":" + jstr(s.name);
                if (s.hasIndex) o += ",\"index\":" + ex(s.k[1]);
                o += ",\"value\":" + ex(s.k[0]);
                break;
            case N_RETURN: o += "\"op\":\"return\",\"values\":[" + (s.k.empty() ? std::string() : ex(s.k[0])) + "]"; break;
            case N_PASS: o += "\"op\":\"pass\""; break;
            case N_BREAK: o += "\"op\":\"break\""; break;
            case N_DO: {
                o += "\"op\":\"do\",\"fn\":" + jstr(s.name) + ",\"args\":[";
                for (size_t i = 0; i < s.k.size(); i++) o += (i ? "," : "") + ex(s.k[i]);
                o += "]";
                break;
            }
            case N_FOR:
                o += "\"op\":\"for\",\"var\":" + jstr(s.var) + ",\"set\":" + set(s.k[0]);
                if (s.hasWhere) o += ",\"where\":" + ex(s.k[1]);
                o += ",\"body\":" + body(s.arms[0].body);
                break;
            case N_IF: {
                o += "\"op\":\"if\",\"arms\":[";
                for (size_t i = 0; i < s.arms.size(); i++)
                    o += std::string(i ? "," : "") + "{\"cond\":" + ex(s.k[i]) + ",\"body\":" + body(s.arms[i].body) + commentsOf(i ? s.arms[i].comments : std::vector<std::string>{}, s.arms[i].trail) + "}";
                o += "]";
                if (s.hasElse) o += ",\"else\":{\"body\":" + body(s.elseArm.body) + commentsOf(s.elseArm.comments, s.elseArm.trail) + "}";
                break;
            }
            default: break;
        }
        return o + commentsOf(s.comments, s.op == N_IF ? "" : s.op == N_FOR ? s.arms[0].trail : s.trail) + "}";
    }
    std::string value(double v, int t) {
        if (t == T_BOOL) return v ? "true" : "false";
        if (t == T_TEAM) return v == 0 ? "\"A\"" : v == 1 ? "\"B\"" : "\"none\"";
        if (t == T_PLAYER) return "\"none\"";
        return fmtNumber(v, t);
    }
    std::string decl(const Decl& d) {
        std::string o = "{" + id() + ",";
        switch (d.kind) {
            case Decl::PARAM: o += "\"kind\":\"param\",\"name\":" + jstr(d.name) + ",\"type\":\"" + typeName(d.ptype) + "\",\"value\":" + value(d.value, d.ptype); break;
            case Decl::CURVE:
                o += "\"kind\":\"curve\",\"name\":" + jstr(d.name) + ",\"arg\":" + jstr(d.arg) + ",\"argType\":\"" + typeName(d.ptype) + "\",\"form\":\"" + (d.form == 0 ? "expr" : d.form == 1 ? "steps" : "points") + "\"";
                if (d.form == 0) o += ",\"body\":" + ex(d.body[0]);
                else {
                    if (d.form == 1) o += ",\"base\":" + fmtNumber(d.base, T_F32);
                    o += ",\"points\":[";
                    for (size_t i = 0; i < d.pts.size(); i++) o += std::string(i ? "," : "") + "[" + fmtNumber(d.pts[i].first, T_F32) + "," + fmtNumber(d.pts[i].second, T_F32) + "]";
                    o += "]";
                }
                break;
            case Decl::USE: o += "\"kind\":\"use\",\"object\":" + jstr(d.name); break;
            case Decl::STATE:
                o += "\"kind\":\"state\",\"life\":\"" + std::string(d.life ? "round" : "match") + "\",\"name\":" + jstr(d.name) + ",\"per\":" +
                     (d.per == 1 ? "\"team\"" : d.per == 2 ? "\"player\"" : "null") + ",\"type\":\"" + typeName(d.ptype) + "\",\"value\":" + value(d.value, d.ptype);
                break;
            case Decl::TIMER: o += "\"kind\":\"timer\",\"name\":" + jstr(d.name); break;
            case Decl::PROC: {
                o += "\"kind\":\"proc\",\"name\":" + jstr(d.name) + ",\"params\":[";
                for (size_t i = 0; i < d.args.size(); i++) o += std::string(i ? "," : "") + "[" + jstr(d.args[i].first) + ",\"" + typeName(d.args[i].second) + "\"]";
                o += "],\"body\":" + body(d.body);
                break;
            }
            case Decl::SLOT:
                o += "\"kind\":\"slot\",\"name\":" + jstr(d.slot == MSLOT_TIMER ? "timer" : kSlots[d.slot].name);
                if (d.slot == MSLOT_TIMER) o += ",\"timer\":" + jstr(d.name);
                o += ",\"body\":" + body(d.body);
                break;
        }
        return o + commentsOf(d.comments, d.trail) + "}";
    }
    std::string file(const File& f) {
        std::string o = "{\"format\":\"qmode-logic-ast\",\"version\":" + std::to_string(f.version) + ",\"base\":" + (f.base.empty() ? std::string("null") : jstr(f.base)) + ",\"keys\":[";
        for (size_t i = 0; i < f.keys.size(); i++) o += std::string(i ? "," : "") + "[" + jstr(f.keys[i].first) + "," + jstr(f.keys[i].second) + "]";
        o += "],\"decls\":[";
        for (size_t i = 0; i < f.decls.size(); i++) o += (i ? ",\n" : "\n") + decl(f.decls[i]);
        o += "],\"tail\":[";
        for (size_t i = 0; i < f.tail.size(); i++) o += (i ? "," : "") + jstr(f.tail[i]);
        return o + "]}";
    }
};

// =====================================================================================================
// The .qmode around the logic: key=value lines, then `logic N` to the end
// =====================================================================================================

struct Split {
    int logicLine = 0;                               // 1-based (0: no logic section)
    int version = 0;
    std::string prefix;                              // the lines before it, verbatim (with their line ends)
    std::vector<std::pair<std::string, std::string>> keys;
};
bool isLogicLine(const std::string& raw, int* version) {
    std::string l = rtrim(raw);
    if (l.compare(0, 6, "logic ") != 0 || l.size() < 7) return false;
    for (size_t i = 6; i < l.size(); i++) if (!isdigit((unsigned char)l[i])) return false;
    if (version) *version = atoi(l.c_str() + 6);
    return true;
}
Split splitQmode(const std::string& text) {
    Split s;
    size_t pos = 0;
    int no = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        size_t next = e == std::string::npos ? text.size() : e + 1;
        std::string raw = text.substr(pos, (e == std::string::npos ? text.size() : e) - pos);
        no++;
        if (isLogicLine(raw, &s.version)) { s.logicLine = no; break; }
        s.prefix += text.substr(pos, next - pos);
        std::string l = rtrim(raw);
        size_t eq = l.find('=');
        if (eq != std::string::npos) s.keys.push_back({l.substr(0, eq), l.substr(eq + 1)});
        pos = next;
    }
    return s;
}

} // namespace

// =====================================================================================================
// Compiler: type check, definite assignment, limits, code for the interpreter
// =====================================================================================================

namespace {

enum Op : uint8_t {
    OP_LDK, OP_LDP, OP_LDV, OP_STV, OP_LDVI, OP_STVI, OP_ARG, OP_GET0, OP_GET1, OP_GET2, OP_SETF, OP_MOV,
    OP_ADDF, OP_SUBF, OP_MULF, OP_DIVF, OP_ADDD, OP_SUBD, OP_MULD, OP_DIVD, OP_ADDI, OP_SUBI, OP_MULI, OP_MODI,
    OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE, OP_NOT, OP_NEG, OP_TOF, OP_TOI,
    OP_ABS, OP_SQRTF, OP_SQRTD, OP_EXPF, OP_EXPD, OP_MIN, OP_MAX, OP_CLAMP, OP_OTHER, OP_CURVE,
    OP_MASK, OP_INSET, OP_NEXT, OP_BIT, OP_PICK, OP_TLEFT, OP_TSTART, OP_TSTOP, OP_ACT, OP_SAY,
    OP_JMP, OP_JZ, OP_JNZ, OP_RET
};

struct Curve { uint8_t kind = 0, n = 0; float base = 0; float x[16] = {}, y[16] = {}; };

const char* originFile(int origin, int base) {
    static const char* const files[MODE_COUNT] = {"vs.qlogic", "bomb.qlogic", "race.qlogic", "koth.qlogic", "payload.qlogic", "ffa.qlogic", "npayload.qlogic",
                                                  "duel.qlogic", "arena.qlogic", "tourney.qlogic", "hide.qlogic", "tag.qlogic", "points.qlogic", "surf.qlogic"};
    if (origin == 2) return "common.qlogic";
    return base >= 0 && base < MODE_COUNT ? files[base] : "the default";
}

struct Merged {
    File file;
    std::map<std::string, size_t> byKey;         // Decl::key() -> index
};

struct LogicExtra { std::vector<Curve> curves; };   // (ModeLogic has no curve table: kept beside it)

struct Compiler {
    ModeLogic& L;
    ModeDiag& diag;
    const Merged& M;
    int base;
    std::vector<Curve>& curves;
    std::map<std::string, int> params;           // name -> P index
    std::vector<int> ptype;
    std::map<std::string, const Decl*> exprCurves;
    std::map<std::string, int> dataCurves;
    std::map<std::string, int> curveType;
    std::map<std::string, int> states;           // name -> L.state index
    std::map<std::string, int> timers;
    std::map<std::string, const Decl*> procs;
    std::set<std::string> calledProcs;
    // per slot
    int slot = 0;
    int origin = 0;
    struct Var { int reg, type; bool output; };
    std::map<std::string, Var> vars;
    std::set<std::string> assigned;
    struct Bind { std::string name; int reg, type; };
    std::vector<Bind> binds;
    int curveDepth = 0;
    std::vector<std::string> curveStack, procStack;
    int nextReg = 0, nextLocal = kModeRegs, firstTemp = 0, maxReg = 0, minLocal = kModeRegs;
    uint64_t cost = 0, mult = 1;
    int loopDepth = 0, aggDepth = 0, textDepth = 0;
    std::vector<std::vector<int>*> breaks, procRets;
    std::vector<std::pair<const std::vector<Node>*, size_t>> blocks;
    int base_ = 0;
    bool usesEndMatch = false;

    Compiler(ModeLogic& l, ModeDiag& d, const Merged& m, int b, std::vector<Curve>& c) : L(l), diag(d), M(m), base(b), curves(c) {}

    std::string note() const { return origin ? std::string(" (in ") + originFile(origin, base) + ")" : ""; }
    void error(int line, int col, const std::string& m) { diag.errors.push_back({line, col, m + note()}); }
    void error(const Node& n, const std::string& m) { error(n.line, n.col, m); }
    void warning(int line, int col, const std::string& m) { diag.warnings.push_back({line, col, m + note()}); }
    int emit(uint8_t op, int d = 0, int a = 0, int b = 0, int k = 0) {
        L.code.push_back({op, (uint8_t)d, (uint8_t)a, (uint8_t)b, k});
        cost += mult;
        return (int)L.code.size() - 1 - base_;
    }
    int here() const { return (int)L.code.size() - base_; }
    void patch(int at, int target) { L.code[base_ + at].k = target; }
    int konst(double v) {
        for (size_t i = 0; i < L.konst.size(); i++) if (memcmp(&L.konst[i], &v, sizeof v) == 0) return (int)i;
        L.konst.push_back(v);
        return (int)L.konst.size() - 1;
    }
    int tmp(const Node& n) {
        if (nextReg >= nextLocal) { error(n, "too complex: the slot needs more than 64 registers"); return 0; }
        int r = nextReg++;
        maxReg = std::max(maxReg, nextReg);
        return r;
    }
    int local(const Node& n) {
        if (nextLocal - 1 < nextReg || nextLocal - 1 < firstTemp) { error(n, "too complex: the slot needs more than 64 registers"); return kModeRegs - 1; }
        --nextLocal;
        minLocal = std::min(minLocal, nextLocal);
        return nextLocal;
    }
    int ldk(const Node& n, double v) { int d = tmp(n); emit(OP_LDK, d, 0, 0, konst(v)); return d; }

    static int promote(int a, int b) {
        if (a == T_NONE || b == T_NONE) return T_NONE;
        if (isNumeric(a) && isNumeric(b)) return a == T_F64 || b == T_F64 ? T_F64 : a == T_F32 || b == T_F32 ? T_F32 : T_INT;
        if (a == T_NONELIT && isNullable(b)) return b;
        if (b == T_NONELIT && isNullable(a)) return a;
        return a == b ? a : -1;
    }
    int convert(const Node& n, int reg, int from, int to, const std::string& what) {
        if (from == to || from == T_NONE || to == T_NONE) return reg;
        if (from == T_NONELIT && isNullable(to)) return to == T_BOMBSTATE ? ldk(n, BOMB_NONE) : reg;
        if (from == T_INT && to == T_F32) { int d = tmp(n); emit(OP_TOF, d, reg); return d; }
        if ((from == T_INT || from == T_F32) && to == T_F64) return reg;
        if (from == T_F64 && to == T_F32) error(n, what + ": an f64 doesn't go into an f32 without f32(...)");
        else if (from == T_NONELIT) error(n, what + ": none where a " + typeName(to) + " is wanted");
        else error(n, what + ": a " + std::string(typeName(from)) + " where a " + typeName(to) + " is wanted");
        return reg;
    }

    // ---- names
    enum Ref { R_NONE, R_BIND, R_VAR, R_ARG, R_FIELD, R_PARAM, R_STATE, R_CONST, R_TIMER };
    Ref resolve(const std::string& nm, int& idx, int& type) {
        for (auto it = binds.rbegin(); it != binds.rend(); ++it) if (it->name == nm) { idx = it->reg; type = it->type; return R_BIND; }
        if (!curveDepth) {
            auto v = vars.find(nm);
            if (v != vars.end()) { idx = v->second.reg; type = v->second.type; return R_VAR; }
            const MSlotInfo& si = slotInfo(slot);
            for (size_t k = 0; k < si.args.size(); k++) if (nm == si.args[k].name) { idx = (int)k; type = si.args[k].type; return R_ARG; }
            for (int f = 0; f < kNumFields; f++) if (nm == kFields[f].name) { idx = f; type = kFields[f].type; return R_FIELD; }
        }
        auto p = params.find(nm);
        if (p != params.end()) { idx = p->second; type = ptype[p->second]; return R_PARAM; }
        if (!curveDepth) {
            auto s = states.find(nm);
            if (s != states.end()) { idx = s->second; type = L.state[s->second].type; return R_STATE; }
        }
        if (nm == "none") { idx = -1; type = T_NONELIT; return R_CONST; }
        if (const ConstInfo* c = constByName(nm)) { idx = (int)(c - kConsts); type = c->type; return R_CONST; }
        int w = weaponByName(nm);
        if (w >= 0) { idx = -100 - w; type = T_WEAPON; return R_CONST; }
        if (!curveDepth) {
            auto t = timers.find(nm);
            if (t != timers.end()) { idx = t->second; type = T_TIMER; return R_TIMER; }
        }
        return R_NONE;
    }
    double constValue(int idx) { return idx == -1 ? -1 : idx <= -100 ? -100 - idx : kConsts[idx].v; }
    const ArrayInfo* engineArray(const std::string& n) { for (auto& a : kArrays) if (n == a.name) return &a; return nullptr; }
    const FuncInfo* func(const std::string& n) { for (auto& f : kFuncs) if (n == f.name) return &f; return nullptr; }
    bool isBuiltin(const std::string& n) { for (auto& b : kBuiltins) if (n == b.name) return true; return false; }

    // ---- type of an expression (no code)
    int typeOf(const Node& n) {
        switch (n.op) {
            case N_NUM: return n.ntype;
            case N_BOOL: return T_BOOL;
            case N_STR: return T_TEXT;
            case N_NAME: { int idx, t; Ref r = resolve(n.name, idx, t); return r == R_NONE ? T_NONE : t; }
            case N_NEG: return typeOf(n.k[0]);
            case N_NOT: case N_INSET: return T_BOOL;
            case N_INDEX: {
                auto s = states.find(n.name);
                if (s != states.end()) return L.state[s->second].type;
                const ArrayInfo* a = engineArray(n.name);
                return a ? (int)a->type : (int)T_NONE;
            }
            case N_COND: { int t = promote(typeOf(n.k[1]), typeOf(n.k[2])); return t < 0 ? T_NONE : t; }
            case N_BIN:
                if (n.bin >= B_LT) return T_BOOL;
                { int t = promote(typeOf(n.k[0]), typeOf(n.k[1])); return t < 0 ? T_NONE : t; }
            case N_AGG:
                if (n.name == "count") return T_INT;
                if (n.name == "any") return T_BOOL;
                if (n.name == "names") return T_TEXT;
                return T_PLAYER;
            case N_CALL: {
                const std::string& f = n.name;
                if (f == "f32") return T_F32;
                if (f == "f64") return T_F64;
                if (f == "int") return T_INT;
                if (f == "abs" || f == "sqrt" || f == "exp") return n.k.empty() ? T_NONE : typeOf(n.k[0]);
                if (f == "min" || f == "max" || f == "clamp") {
                    int t = n.k.empty() ? T_NONE : typeOf(n.k[0]);
                    for (size_t i = 1; i < n.k.size(); i++) { t = promote(t, typeOf(n.k[i])); if (t < 0) return T_NONE; }
                    return t;
                }
                if (const FuncInfo* fi = func(f)) return fi->ret;
                if (const ActionInfo* a = actionByName(f)) return a->ret;
                if (dataCurves.count(f)) return T_F32;
                auto c = curveType.find(f);
                if (c != curveType.end()) return c->second;
                auto e = exprCurves.find(f);
                if (e != exprCurves.end()) {
                    for (auto& s : curveStack) if (s == f) return T_NONE;
                    curveStack.push_back(f);
                    binds.push_back({e->second->arg, 0, e->second->ptype});
                    curveDepth++;
                    int t = typeOf(e->second->body[0]);
                    curveDepth--;
                    binds.pop_back();
                    curveStack.pop_back();
                    curveType[f] = t;
                    return t;
                }
                return T_NONE;
            }
            default: return T_NONE;
        }
    }

    // ---- code for an expression: the register holding it
    int ce(const Node& n, int& type) {
        type = T_NONE;
        switch (n.op) {
            case N_NUM: type = n.ntype; return ldk(n, n.v);
            case N_BOOL: type = T_BOOL; return ldk(n, n.v);
            case N_STR: error(n, "text only goes into announce, log, endRound, endRoundNoScore, endRoundText and endMatch"); return ldk(n, 0);
            case N_NAME: {
                int idx, t;
                Ref r = resolve(n.name, idx, t);
                type = t;
                switch (r) {
                    case R_NONE: {
                        if (curveDepth) {
                            int save = curveDepth; curveDepth = 0;
                            int i2, t2;
                            Ref r2 = resolve(n.name, i2, t2);
                            curveDepth = save;
                            if (r2 != R_NONE) { error(n, "a curve sees only its argument, params, constants and other curves: '" + n.name + "' isn't one"); type = T_NONE; return ldk(n, 0); }
                        }
                        type = T_NONE;
                        if (engineArray(n.name) || states.count(n.name)) error(n, "'" + n.name + "' is an array: " + n.name + "[...]");
                        else if (func(n.name) || actionByName(n.name) || procs.count(n.name)) error(n, "'" + n.name + "' is called: " + n.name + "(...)");
                        else error(n, "unknown name '" + n.name + "'");
                        return ldk(n, 0);
                    }
                    case R_BIND: return idx;
                    case R_VAR:
                        if (!assigned.count(n.name)) error(n, "'" + n.name + "' may not be set here");
                        return idx;
                    case R_ARG: { int d = tmp(n); emit(OP_ARG, d, 0, 0, idx); return d; }
                    case R_FIELD: { int d = tmp(n); emit(OP_GET0, d, 0, 0, kFields[idx].field); return d; }
                    case R_PARAM: { int d = tmp(n); emit(OP_LDP, d, 0, 0, idx); return d; }
                    case R_STATE: {
                        const ModeStateVar& s = L.state[idx];
                        if (s.per) { error(n, "'" + n.name + "' is per " + (s.per == 1 ? "team" : "player") + ": " + n.name + "[...]"); return ldk(n, 0); }
                        int d = tmp(n); emit(OP_LDV, d, 0, 0, s.base); return d;
                    }
                    case R_CONST:
                        if (idx >= 0 && kConsts[idx].type == T_ZONE) L.needs |= kConsts[idx].v == MZ_HILL ? MN_HILL : MN_SITES;
                        return ldk(n, constValue(idx));
                    case R_TIMER: return ldk(n, idx);
                }
                return ldk(n, 0);
            }
            case N_NEG: {
                int t, a = ce(n.k[0], t);
                if (!isNumeric(t) && t != T_NONE) error(n, std::string("'-' of a ") + typeName(t));
                int d = tmp(n);
                emit(OP_NEG, d, a);
                type = t;
                return d;
            }
            case N_NOT: {
                int t, a = ce(n.k[0], t);
                if (t != T_BOOL && t != T_NONE) error(n, std::string("'not' of a ") + typeName(t) + " (it takes a bool)");
                int d = tmp(n);
                emit(OP_NOT, d, a);
                type = T_BOOL;
                return d;
            }
            case N_INSET: {
                int t, a = ce(n.k[0], t);
                a = convert(n.k[0], a, t, T_PLAYER, "'in' tests a player");
                int m = setMask(n.k[1]);
                int d = tmp(n);
                emit(OP_INSET, d, a, m);
                type = T_BOOL;
                return d;
            }
            case N_INDEX: return index(n, type);
            case N_COND: {
                int tc, c = ce(n.k[0], tc);
                if (tc != T_BOOL && tc != T_NONE) error(n.k[0], "a condition is a bool");
                int ta = typeOf(n.k[1]), tb = typeOf(n.k[2]);
                int t = promote(ta, tb);
                if (t < 0 || t == T_TEXT || t == T_TIMER) { error(n, std::string("'?:' branches are a ") + typeName(ta) + " and a " + typeName(tb)); t = T_NONE; }
                int d = tmp(n);
                int jz = emit(OP_JZ, 0, c);
                int t1, a = ce(n.k[1], t1);
                a = convert(n.k[1], a, t1, t, "'?:'");
                emit(OP_MOV, d, a);
                int jmp = emit(OP_JMP);
                patch(jz, here());
                int t2, b = ce(n.k[2], t2);
                b = convert(n.k[2], b, t2, t, "'?:'");
                emit(OP_MOV, d, b);
                patch(jmp, here());
                type = t;
                return d;
            }
            case N_BIN: return bin(n, type);
            case N_CALL: return call(n, type);
            case N_AGG: return agg(n, type);
            default: error(n, "not a value"); return ldk(n, 0);
        }
    }

    int setMask(const Node& s) {
        int team = 0;
        if (s.bin == MSET_TEAM) {
            int t;
            team = ce(s.k[0], t);
            team = convert(s.k[0], team, t, T_TEAM, "team(...) takes a team");
        }
        int d = tmp(s);
        emit(OP_MASK, d, team, 0, s.bin);
        return d;
    }

    int index(const Node& n, int& type) {
        auto s = states.find(n.name);
        int t, a = ce(n.k[0], t);
        if (s != states.end()) {
            const ModeStateVar& v = L.state[s->second];
            type = v.type;
            if (!v.per) { error(n, "'" + n.name + "' is one value (no [...])"); return ldk(n, 0); }
            a = convert(n.k[0], a, t, v.per == 1 ? T_TEAM : T_PLAYER, "'" + n.name + "[...]'");
            int d = tmp(n);
            emit(OP_LDVI, d, a, v.per == 1 ? 2 : 32, v.base);
            return d;
        }
        const ArrayInfo* ar = engineArray(n.name);
        if (!ar) { error(n, "unknown array '" + n.name + "'"); type = T_NONE; return ldk(n, 0); }
        type = ar->type;
        a = convert(n.k[0], a, t, ar->index, "'" + n.name + "[...]'");
        int d = tmp(n);
        emit(OP_GET1, d, a, 0, ar->field);
        return d;
    }

    int bin(const Node& n, int& type) {
        const Node &A = n.k[0], &Bn = n.k[1];
        if (n.bin == B_AND || n.bin == B_OR) {
            int ta, a = ce(A, ta);
            if (ta != T_BOOL && ta != T_NONE) error(A, std::string("'") + kBinText[n.bin] + "' takes bools");
            int d = tmp(n);
            emit(OP_MOV, d, a);
            int j = emit(n.bin == B_AND ? OP_JZ : OP_JNZ, 0, d);
            int tb, b = ce(Bn, tb);
            if (tb != T_BOOL && tb != T_NONE) error(Bn, std::string("'") + kBinText[n.bin] + "' takes bools");
            emit(OP_MOV, d, b);
            patch(j, here());
            type = T_BOOL;
            return d;
        }
        int ta0 = typeOf(A), tb0 = typeOf(Bn);
        int t = promote(ta0, tb0);
        bool cmp = n.bin >= B_LT;
        bool bad = t < 0 || t == T_TEXT || t == T_TIMER || (t == T_NONELIT);
        if (!bad && t != T_NONE && !isNumeric(t)) bad = !cmp || (n.bin != B_EQ && n.bin != B_NE);
        if (bad) {
            error(n, std::string("'") + kBinText[n.bin] + "' of a " + typeName(ta0) + " and a " + typeName(tb0));
            t = T_NONE;
        }
        if (n.bin == B_DIV && t == T_INT) error(n, "int / int: write a decimal (1.0 / 2)");
        if (n.bin == B_MOD && t != T_INT && t != T_NONE) error(n, "'%' takes ints");
        int ta, a = ce(A, ta);
        a = convert(A, a, ta, t, kBinText[n.bin]);
        int tb, b = ce(Bn, tb);
        b = convert(Bn, b, tb, t, kBinText[n.bin]);
        int d = tmp(n);
        if (cmp) { emit((uint8_t)(OP_LT + (n.bin - B_LT)), d, a, b); type = T_BOOL; return d; }
        if (n.bin == B_MOD) { emit(OP_MODI, d, a, b); type = T_INT; return d; }
        int base3 = t == T_F64 ? OP_ADDD : t == T_INT ? OP_ADDI : OP_ADDF;
        int off = n.bin - B_ADD;
        if (t == T_INT && off == 3) off = 2;
        emit((uint8_t)(base3 + off), d, a, b);
        type = t;
        return d;
    }

    bool nargs(const Node& n, size_t want) {
        if (n.k.size() != want) { error(n, n.name + "() takes " + std::to_string(want) + " argument" + (want == 1 ? "" : "s")); return false; }
        return true;
    }
    int call(const Node& n, int& type) {
        const std::string& f = n.name;
        if (f == "min" || f == "max" || f == "clamp") {
            size_t want = f == "clamp" ? 3 : 2;
            if (!nargs(n, want)) { type = T_NONE; return ldk(n, 0); }
            int t = typeOf(n);
            if (t != T_NONE && !isNumeric(t)) { error(n, f + "() takes numbers"); t = T_NONE; }
            int r[3];
            for (size_t i = 0; i < want; i++) { int ti; r[i] = ce(n.k[i], ti); r[i] = convert(n.k[i], r[i], ti, t, f + "()"); }
            int d = tmp(n);
            if (f == "clamp") emit(OP_CLAMP, d, r[0], r[1], r[2]);
            else emit(f == "min" ? OP_MIN : OP_MAX, d, r[0], r[1]);
            type = t;
            return d;
        }
        if (f == "abs" || f == "sqrt" || f == "exp") {
            if (!nargs(n, 1)) { type = T_NONE; return ldk(n, 0); }
            int t, a = ce(n.k[0], t);
            if (t != T_F32 && t != T_F64 && t != T_NONE) error(n, f + "() takes an f32 or f64 (write 2.0f, not 2)");
            int d = tmp(n);
            if (f == "abs") emit(OP_ABS, d, a);
            else if (f == "sqrt") emit(t == T_F64 ? OP_SQRTD : OP_SQRTF, d, a);
            else emit(t == T_F64 ? OP_EXPD : OP_EXPF, d, a);
            type = t;
            return d;
        }
        if (f == "f32" || f == "f64" || f == "int") {
            if (!nargs(n, 1)) { type = T_NONE; return ldk(n, 0); }
            int t, a = ce(n.k[0], t);
            if (!isNumeric(t) && t != T_NONE) error(n, f + "() converts a number");
            type = f == "f32" ? T_F32 : f == "f64" ? T_F64 : T_INT;
            if (type == t || (f == "f64")) return a;
            int d = tmp(n);
            emit(f == "int" ? OP_TOI : OP_TOF, d, a);
            return d;
        }
        if (const FuncInfo* fi = func(f)) {
            type = fi->ret;
            if (fi->ret == T_TEXT && !textDepth) { error(n, f + "() is text: only as an argument of announce, log, endRound..."); return ldk(n, 0); }
            if (!nargs(n, fi->args.size())) return ldk(n, 0);
            if (fi->special == FS_TIMERLEFT) {
                int idx, t;
                if (n.k[0].op != N_NAME || resolve(n.k[0].name, idx, t) != R_TIMER) { error(n.k[0], "timerLeft() takes a timer's name"); return ldk(n, 0); }
                int d = tmp(n);
                emit(OP_TLEFT, d, 0, 0, idx);
                return d;
            }
            int r[2] = {0, 0};
            for (size_t i = 0; i < fi->args.size(); i++) { int ti; r[i] = ce(n.k[i], ti); r[i] = convert(n.k[i], r[i], ti, fi->args[i], f + "()"); }
            int d = tmp(n);
            switch (fi->special) {
                case FS_FIELD:
                    if (fi->field == MF_NEAR_CART) L.needs |= MN_CART;
                    if (fi->args.size() == 1) emit(OP_GET1, d, r[0], 0, fi->field);
                    else emit(OP_GET2, d, r[0], r[1], fi->field);
                    break;
                case FS_NAME: case FS_LETTER: emit(OP_MOV, d, r[0]); break;
                case FS_OTHER: emit(OP_OTHER, d, r[0]); break;
                case FS_SITE: case FS_POINT: case FS_CHECKPOINT: {
                    int kind = fi->special == FS_SITE ? MZ_SITE : fi->special == FS_POINT ? MZ_POINT : MZ_CHECKPOINT;
                    L.needs |= fi->special == FS_SITE ? MN_SITES : fi->special == FS_POINT ? MN_POINTS : MN_CHECKPOINTS;
                    int eight = ldk(n, 8), kk = ldk(n, kind), m = tmp(n);
                    emit(OP_MULI, m, r[0], eight);
                    emit(OP_ADDI, d, m, kk);
                    break;
                }
                default: break;
            }
            return d;
        }
        if (const ActionInfo* a = actionByName(f)) {
            if (a->ret == T_VOID) { error(n, f + "() is an action: a statement of its own"); type = T_NONE; return ldk(n, 0); }
            return action(n, *a, type);
        }
        auto dc = dataCurves.find(f);
        if (dc != dataCurves.end()) {
            type = T_F32;
            if (!nargs(n, 1)) return ldk(n, 0);
            int t, a = ce(n.k[0], t);
            a = convert(n.k[0], a, t, T_F32, f + "()");
            int d = tmp(n);
            emit(OP_CURVE, d, a, 0, dc->second);
            return d;
        }
        auto ec = exprCurves.find(f);
        if (ec != exprCurves.end()) {
            const Decl& cd = *ec->second;
            for (auto& s : curveStack) if (s == f) { error(n, "curve '" + f + "' uses itself"); type = T_NONE; return ldk(n, 0); }
            if (!nargs(n, 1)) { type = T_NONE; return ldk(n, 0); }
            int t, a = ce(n.k[0], t);
            a = convert(n.k[0], a, t, cd.ptype, f + "()");
            curveStack.push_back(f);
            binds.push_back({cd.arg, a, cd.ptype});
            curveDepth++;
            int r = ce(cd.body[0], type);
            curveDepth--;
            binds.pop_back();
            curveStack.pop_back();
            return r;
        }
        if (procs.count(f)) error(n, "proc " + f + "() is a statement of its own");
        else if (isAggregate(f)) error(n, f + "(p in <set> ...): a player variable and a set");
        else error(n, "unknown function '" + f + "'");
        type = T_NONE;
        return ldk(n, 0);
    }

    // an aggregate over the player slots: count any first random best names
    int agg(const Node& n, int& type) {
        type = typeOf(n);
        if (aggDepth) { error(n, "an aggregate can't be inside another aggregate"); return ldk(n, 0); }
        if (n.name == "names" && !textDepth) { error(n, "names(...) is text: only as an argument of announce, log, endRound..."); return ldk(n, 0); }
        if (n.name == "best" && !n.hasBy) { error(n, "best(p in <set> by <value>): the value to rank by"); return ldk(n, 0); }
        if (n.name != "best" && n.hasBy) { error(n.k[2], "'by' is for best(...)"); return ldk(n, 0); }
        if (!checkNewName(n, n.var)) return ldk(n, 0);
        int mask = setMask(n.k[0]);
        int var = tmp(n), acc = tmp(n), bestV = 0, one = 0, minus = 0;
        const std::string& f = n.name;
        emit(OP_LDK, acc, 0, 0, konst(f == "first" || f == "best" ? -1 : 0));
        if (f == "count") one = ldk(n, 1);
        if (f == "best") { bestV = tmp(n); minus = ldk(n, -1); }
        binds.push_back({n.var, var, T_PLAYER});
        aggDepth++;
        uint64_t saveMult = mult;
        mult *= kModePlayers;
        int top = here();
        int nx = emit(OP_NEXT, var, mask);
        std::vector<int> exits;
        if (n.hasWhere) {
            int t, c = ce(n.k[1], t);
            if (t != T_BOOL && t != T_NONE) error(n.k[1], "'where' needs a bool");
            emit(OP_JZ, 0, c, 0, top);
        }
        if (f == "count") emit(OP_ADDI, acc, acc, one);
        else if (f == "any") { emit(OP_LDK, acc, 0, 0, konst(1)); exits.push_back(emit(OP_JMP)); }
        else if (f == "first") { emit(OP_MOV, acc, var); exits.push_back(emit(OP_JMP)); }
        else if (f == "random" || f == "names") emit(OP_BIT, acc, var);
        else {   // best: the first highest
            int t, v = ce(n.k[2], t);
            if (!isNumeric(t) && t != T_NONE) error(n.k[2], "best(...) ranks by a number");
            int c1 = tmp(n), c2 = tmp(n);
            emit(OP_EQ, c1, acc, minus);
            int jt = emit(OP_JNZ, 0, c1);
            emit(OP_GT, c2, v, bestV);
            emit(OP_JZ, 0, c2, 0, top);
            patch(jt, here());
            emit(OP_MOV, acc, var);
            emit(OP_MOV, bestV, v);
        }
        emit(OP_JMP, 0, 0, 0, top);
        patch(nx, here());
        for (int e : exits) patch(e, here());
        mult = saveMult;
        aggDepth--;
        binds.pop_back();
        if (f == "random") emit(OP_PICK, acc, acc);
        return acc;
    }

    // ---- actions (statements, and the ones with a value in expressions)
    int winnerType() const { return isRaceMode(base) ? T_PLAYER : T_TEAM; }
    int matchWinnerType() const { return teamBased(base) ? T_TEAM : T_PLAYER; }
    int action(const Node& n, const ActionInfo& a, int& type) {
        type = a.ret;
        if (!a.available) { error(n, n.name + "() isn't available yet"); return ldk(n, 0); }
        if (a.obj >= 0 && !(L.uses >> a.obj & 1)) error(n, n.name + "() needs 'use " + kObjects[(int)a.obj].name + "'");
        bool texts = !a.args.empty() && a.args.back().type == T_TEXTS;
        size_t fixed = texts ? a.args.size() - 1 : a.args.size();
        if (texts ? n.k.size() < fixed : n.k.size() != fixed) {
            error(n, n.name + "() takes " + (texts ? "at least " : "") + std::to_string(fixed) + " argument" + (fixed == 1 ? "" : "s"));
            return ldk(n, 0);
        }
        if (a.act == MA_END_MATCH) usesEndMatch = true;
        if (texts) {
            ModeTextDesc td;
            td.action = a.act;
            if (fixed) {
                int t, w = ce(n.k[0], t);
                int want = a.act == MA_END_MATCH ? matchWinnerType() : winnerType();
                td.winner = (int16_t)convert(n.k[0], w, t, want, n.name + "()'s winner");
            }
            if (n.k.size() - fixed > (size_t)kModeTextParts) { error(n, "at most " + std::to_string(kModeTextParts) + " text arguments"); return 0; }
            textDepth++;
            for (size_t i = fixed; i < n.k.size(); i++) {
                const Node& x = n.k[i];
                ModeTextDesc::Part& p = td.part[td.n++];
                if (x.op == N_STR) {
                    p.kind = ModeTextPart::STR;
                    size_t si = 0;
                    for (; si < L.strings.size(); si++) if (L.strings[si] == x.name) break;
                    if (si == L.strings.size()) L.strings.push_back(x.name);
                    p.str = (uint16_t)si;
                    continue;
                }
                bool isName_ = x.op == N_CALL && x.name == "name", isLetter = x.op == N_CALL && x.name == "letter", isNames = x.op == N_AGG && x.name == "names";
                int t, r = ce(x, t);
                p.reg = (uint8_t)r;
                if (isNames) p.kind = ModeTextPart::NAMES;
                else if (isLetter) p.kind = ModeTextPart::LETTER;
                else if (isName_ || t == T_PLAYER) p.kind = ModeTextPart::PLAYER;
                else if (t == T_TEAM) p.kind = ModeTextPart::TEAM;
                else if (t == T_INT) p.kind = ModeTextPart::INT;
                else if (t == T_NONELIT) { p.kind = ModeTextPart::PLAYER; }
                else if (t == T_F32 || t == T_F64) error(x, std::string("an ") + typeName(t) + " in text: write int(...)");
                else if (t != T_NONE) error(x, std::string("a ") + typeName(t) + " can't be text (strings, players, teams, ints)");
            }
            textDepth--;
            L.texts.push_back(td);
            emit(OP_SAY, 0, 0, 0, (int)L.texts.size() - 1);
            if (a.ends) emit(OP_RET);
            return 0;
        }
        switch (a.special) {
            case AS_ADD_SCORE: case AS_ADD_TEAM_SCORE: {
                int field = a.special == AS_ADD_SCORE ? MF_SCORE : MF_TEAM_SCORE;
                int ti, i = ce(n.k[0], ti);
                i = convert(n.k[0], i, ti, a.args[0].type, n.name + "()");
                int tv, v = ce(n.k[1], tv);
                v = convert(n.k[1], v, tv, T_INT, n.name + "()");
                int cur = tmp(n), sum = tmp(n);
                emit(OP_GET1, cur, i, 0, field);
                emit(OP_ADDI, sum, cur, v);
                emit(OP_SETF, 0, i, sum, field);
                return 0;
            }
            case AS_START_TIMER: case AS_STOP_TIMER: {
                int idx, t;
                if (n.k[0].op != N_NAME || resolve(n.k[0].name, idx, t) != R_TIMER) { error(n.k[0], n.name + "() takes a timer's name"); return 0; }
                if (a.special == AS_STOP_TIMER) { emit(OP_TSTOP, 0, 0, 0, idx); return 0; }
                int ts, s = ce(n.k[1], ts);
                s = convert(n.k[1], s, ts, T_F32, n.name + "()");
                emit(OP_TSTART, 0, s, 0, idx);
                return 0;
            }
            default: break;
        }
        int nArgs = (int)a.args.size();
        int argRegs[4] = {0, 0, 0, 0};
        for (int i = 0; i < nArgs; i++) {
            int t, r = ce(n.k[i], t);
            argRegs[i] = convert(n.k[i], r, t, a.args[i].type, n.name + "()");
        }
        int first = nextReg;
        for (int i = 0; i < nArgs; i++) { int d = tmp(n); emit(OP_MOV, d, argRegs[i]); }
        int d = tmp(n);
        emit(OP_ACT, d, nArgs ? first : 0, nArgs, a.act);
        return d;
    }

    // ---- statements; true if the block always ends the slot (or the proc)
    bool checkNewName(const Node& n, const std::string& nm) {
        int idx, t;
        if (isKeyword(nm) || resolve(nm, idx, t) != R_NONE || engineArray(nm) || func(nm) || actionByName(nm) || procs.count(nm) || isBuiltin(nm) || isAggregate(nm)) {
            error(n, "'" + nm + "' is already a name here: pick another");
            return false;
        }
        return true;
    }
    int inferLater(const std::string& nm) {
        std::function<int(const std::vector<Node>&, size_t)> scan = [&](const std::vector<Node>& b, size_t from) -> int {
            for (size_t i = from; i < b.size(); i++) {
                const Node& s = b[i];
                if (s.op == N_SET && s.name == nm && !s.hasIndex) {
                    int t = typeOf(s.k[0]);
                    if (t != T_NONELIT && t != T_NONE) return t;
                }
                for (auto& a : s.arms) { int t = scan(a.body, 0); if (t >= 0) return t; }
                if (s.hasElse) { int t = scan(s.elseArm.body, 0); if (t >= 0) return t; }
            }
            return -1;
        };
        for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) { int t = scan(*it->first, it->second + 1); if (t >= 0) return t; }
        return -1;
    }
    bool stmt(const Node& s) {
        nextReg = firstTemp;
        switch (s.op) {
            case N_PASS: return false;
            case N_BREAK:
                if (breaks.empty()) { error(s, "break is for a for loop"); return true; }
                breaks.back()->push_back(emit(OP_JMP));
                return true;
            case N_SET: return assign(s);
            case N_RETURN: {
                const MSlotInfo& si = slotInfo(slot);
                if (!procRets.empty()) {
                    if (!s.k.empty()) error(s, "a proc returns no value: plain return");
                    procRets.back()->push_back(emit(OP_JMP));
                    return true;
                }
                if (!s.k.empty()) {
                    if (!si.query) { error(s, std::string("slot ") + modeSlotName(slot) + " is an event: plain return (no value)"); return true; }
                    int t, a = ce(s.k[0], t);
                    a = convert(s.k[0], a, t, si.out.type, "the result");
                    emit(OP_MOV, 0, a);
                }
                emit(OP_RET);
                return true;
            }
            case N_DO: {
                if (procs.count(s.name)) { procCall(s); return false; }
                const ActionInfo* a = actionByName(s.name);
                if (!a) {
                    if (func(s.name) || isBuiltin(s.name)) error(s, s.name + "() is a value, not an action");
                    else error(s, "unknown action or proc '" + s.name + "'");
                    return false;
                }
                int t;
                action(s, *a, t);
                return a->ends;
            }
            case N_FOR: return forLoop(s);
            case N_IF: {
                std::set<std::string> in = assigned, outSet;
                bool any = false, allTerm = true;
                std::vector<int> ends;
                auto meet = [&](bool term) {
                    if (term) return;
                    if (!any) outSet = assigned;
                    else { std::set<std::string> x; for (auto& n : outSet) if (assigned.count(n)) x.insert(n); outSet = x; }
                    any = true; allTerm = false;
                };
                for (size_t i = 0; i < s.arms.size(); i++) {
                    assigned = in;
                    nextReg = firstTemp;
                    int t, c = ce(s.k[i], t);
                    if (t != T_BOOL && t != T_NONE) error(s.k[i], std::string("an if needs a bool, not a ") + typeName(t));
                    int jz = emit(OP_JZ, 0, c);
                    meet(block(s.arms[i].body));
                    if (i + 1 < s.arms.size() || s.hasElse) ends.push_back(emit(OP_JMP));
                    patch(jz, here());
                }
                if (s.hasElse) { assigned = in; meet(block(s.elseArm.body)); }
                else {
                    allTerm = false;
                    if (!any) outSet = in;
                    else { std::set<std::string> x; for (auto& n : outSet) if (in.count(n)) x.insert(n); outSet = x; }
                }
                for (int j : ends) patch(j, here());
                assigned = allTerm ? in : outSet;
                return allTerm;
            }
            default: error(s, "not a statement"); return false;
        }
    }
    bool assign(const Node& s) {
        int idx, t0;
        Ref r = resolve(s.name, idx, t0);
        if (s.hasIndex) {
            auto st = states.find(s.name);
            int ti, i = ce(s.k[1], ti);
            if (st != states.end()) {
                const ModeStateVar& v = L.state[st->second];
                if (!v.per) { error(s, "'" + s.name + "' is one value (no [...])"); return false; }
                i = convert(s.k[1], i, ti, v.per == 1 ? T_TEAM : T_PLAYER, "'" + s.name + "[...]'");
                int tv, val = ce(s.k[0], tv);
                val = convert(s.k[0], val, tv, v.type, "'" + s.name + "'");
                emit(OP_STVI, val, i, v.per == 1 ? 2 : 32, v.base);
                return false;
            }
            const ArrayInfo* ar = engineArray(s.name);
            if (!ar) { error(s, "unknown array '" + s.name + "'"); return false; }
            if (!ar->rw) { error(s, "'" + s.name + "' is read-only"); return false; }
            i = convert(s.k[1], i, ti, ar->index, "'" + s.name + "[...]'");
            int tv, val = ce(s.k[0], tv);
            val = convert(s.k[0], val, tv, ar->type, "'" + s.name + "'");
            emit(OP_SETF, 0, i, val, ar->field);
            return false;
        }
        switch (r) {
            case R_VAR: {
                int t, a = ce(s.k[0], t);
                a = convert(s.k[0], a, t, t0, "'" + s.name + "'");
                emit(OP_MOV, idx, a);
                assigned.insert(s.name);
                return false;
            }
            case R_STATE: {
                const ModeStateVar& v = L.state[idx];
                if (v.per) { error(s, "'" + s.name + "' is per " + (v.per == 1 ? "team" : "player") + ": " + s.name + "[...] = ..."); return false; }
                int t, a = ce(s.k[0], t);
                a = convert(s.k[0], a, t, v.type, "'" + s.name + "'");
                emit(OP_STV, a, 0, 0, v.base);
                return false;
            }
            case R_FIELD: {
                const FieldInfo& f = kFields[idx];
                if (!f.rw) { error(s, "'" + s.name + "' is read-only"); return false; }
                int t, a = ce(s.k[0], t);
                a = convert(s.k[0], a, t, f.type, "'" + s.name + "'");
                emit(OP_SETF, 0, 0, a, f.field);
                return false;
            }
            case R_NONE: break;
            default:
                error(s, "'" + s.name + "' is " + (r == R_PARAM ? "a param" : r == R_ARG ? "the slot's argument" : r == R_BIND ? "a loop variable or proc parameter" : r == R_CONST ? "a constant" : "a timer") +
                         ": it can't be assigned");
                return false;
        }
        // a new local
        if (!checkNewName(s, s.name)) return false;
        int t, a = ce(s.k[0], t);
        if (t == T_NONELIT) {
            t = inferLater(s.name);
            if (t < 0) { error(s, "'" + s.name + " = none': none needs a type here (assign the team or player first)"); t = T_NONE; }
            else a = convert(s.k[0], a, T_NONELIT, t, "'" + s.name + "'");
        }
        if (t == T_TEXT || t == T_TIMER || t == T_VOID) { error(s, std::string("a local can't hold a ") + typeName(t)); return false; }
        int reg = local(s);
        vars[s.name] = {reg, t, false};
        emit(OP_MOV, reg, a);
        assigned.insert(s.name);
        return false;
    }
    bool forLoop(const Node& s) {
        if (loopDepth || aggDepth) { error(s, "a for can't be inside another for"); return false; }
        if (!checkNewName(s, s.var)) return false;
        int team = 0;
        const Node& set = s.k[0];
        int maskReg = local(s);
        if (set.bin == MSET_TEAM) {
            int t;
            team = ce(set.k[0], t);
            team = convert(set.k[0], team, t, T_TEAM, "team(...) takes a team");
        }
        emit(OP_MASK, maskReg, team, 0, set.bin);
        int var = local(s);
        binds.push_back({s.var, var, T_PLAYER});
        loopDepth++;
        uint64_t saveMult = mult;
        mult *= kModePlayers;
        int top = here();
        int nx = emit(OP_NEXT, var, maskReg);
        if (s.hasWhere) {
            nextReg = firstTemp;
            int t, c = ce(s.k[1], t);
            if (t != T_BOOL && t != T_NONE) error(s.k[1], "'where' needs a bool");
            emit(OP_JZ, 0, c, 0, top);
        }
        std::vector<int> brk;
        breaks.push_back(&brk);
        std::set<std::string> in = assigned;
        blocks.push_back({&s.arms[0].body, (size_t)-1});
        block(s.arms[0].body);
        blocks.pop_back();
        assigned = in;
        breaks.pop_back();
        emit(OP_JMP, 0, 0, 0, top);
        patch(nx, here());
        for (int b : brk) patch(b, here());
        mult = saveMult;
        loopDepth--;
        binds.pop_back();
        return false;
    }
    void procCall(const Node& s) {
        const Decl& p = *procs[s.name];
        calledProcs.insert(s.name);
        for (auto& n : procStack) if (n == s.name) { error(s, "proc " + s.name + "() calls itself"); return; }
        if (s.k.size() != p.args.size()) { error(s, "proc " + s.name + "() takes " + std::to_string(p.args.size()) + " argument" + (p.args.size() == 1 ? "" : "s")); return; }
        int saveLocal = nextLocal;
        std::vector<Bind> args;
        for (size_t i = 0; i < p.args.size(); i++) {
            int t, a = ce(s.k[i], t);
            a = convert(s.k[i], a, t, p.args[i].second, "proc " + s.name + "()'s " + p.args[i].first);
            int reg = local(s);
            emit(OP_MOV, reg, a);
            args.push_back({p.args[i].first, reg, p.args[i].second});
        }
        // the proc sees its parameters, the slot's arguments, sensors, params and state: not the caller's locals
        auto saveVars = vars;
        auto saveAssigned = assigned;
        auto saveBinds = binds;
        int saveOrigin = origin;
        for (auto it = vars.begin(); it != vars.end();) { if (!it->second.output) it = vars.erase(it); else ++it; }
        binds.clear();
        if (p.args.size()) for (auto& a : args) {
            int i0, t0;
            if (resolve(a.name, i0, t0) != R_NONE || isKeyword(a.name)) { error(p.line, p.col, "proc " + s.name + ": parameter '" + a.name + "' is already a name here"); }
            binds.push_back(a);
        }
        origin = p.origin;
        procStack.push_back(s.name);
        std::vector<int> rets;
        procRets.push_back(&rets);
        blocks.push_back({&p.body, (size_t)-1});
        block(p.body);
        blocks.pop_back();
        procRets.pop_back();
        procStack.pop_back();
        for (int r : rets) patch(r, here());
        origin = saveOrigin;
        binds = saveBinds;
        vars = saveVars;
        assigned = saveAssigned;
        nextLocal = saveLocal;
    }
    bool block(const std::vector<Node>& b) {
        bool term = false;
        for (size_t i = 0; i < b.size(); i++) {
            const Node& s = b[i];
            if (term) warning(s.line, s.col, "never runs (the slot has ended above)");
            if (!blocks.empty() && blocks.back().first == &b) blocks.back().second = i;
            else { blocks.push_back({&b, i}); bool t = stmt(s); blocks.pop_back(); term = t || term; continue; }
            term = stmt(s) || term;
        }
        return term;
    }

    void compileSlot(int sl, const Decl& d) {
        slot = sl;
        origin = d.origin;
        vars.clear(); binds.clear(); assigned.clear(); curveStack.clear(); procStack.clear(); breaks.clear(); procRets.clear(); blocks.clear();
        curveDepth = loopDepth = aggDepth = textDepth = 0;
        base_ = (int)L.code.size();
        L.start[sl] = base_;
        cost = 0; mult = 1;
        const MSlotInfo& si = slotInfo(sl);
        int nOut = si.query ? 1 : 0;
        if (nOut) { vars[si.out.name] = {0, si.out.type, true}; assigned.insert(si.out.name); }
        firstTemp = nextReg = maxReg = nOut;
        nextLocal = minLocal = kModeRegs;
        if (nOut) emit(OP_LDK, 0, 0, 0, konst(0));
        block(d.body);
        emit(OP_RET);
        int n = (int)L.code.size() - base_;
        L.insns[sl] = (uint16_t)std::min(n, 65535);
        L.cost[sl] = (uint32_t)std::min<uint64_t>(cost, 0xFFFFFFFFu);
        L.regs[sl] = (uint8_t)std::min(kModeRegs, maxReg + (kModeRegs - minLocal));
        if (n > kModeMaxInsns) error(d.line, d.col, std::string("slot ") + modeSlotName(sl) + ": " + std::to_string(n) + " instructions (at most " + std::to_string(kModeMaxInsns) + ")");
        if (cost > (uint64_t)kModeMaxCost)
            error(d.line, d.col, std::string("slot ") + modeSlotName(sl) + ": up to " + std::to_string(cost) + " instructions a call (at most " + std::to_string(kModeMaxCost) +
                                     "; a for or aggregate body counts 32 times)");
        if (si.query) {
            bool sets = false;
            std::function<void(const std::vector<Node>&)> scan = [&](const std::vector<Node>& b) {
                for (auto& s : b) {
                    if ((s.op == N_RETURN && !s.k.empty()) || (s.op == N_SET && s.name == si.out.name)) sets = true;
                    for (auto& a : s.arms) scan(a.body);
                    if (s.hasElse) scan(s.elseArm.body);
                }
            };
            scan(d.body);
            if (!sets) warning(d.line, d.col, std::string("slot ") + modeSlotName(sl) + ": the result is never set (stays 0 / no)");
        }
        for (size_t i = base_; i < L.code.size(); i++) {
            ModeInsn& in = L.code[i];
            if (in.op == OP_JMP || in.op == OP_JZ || in.op == OP_JNZ || in.op == OP_NEXT) in.k += base_;
        }
    }
};

// ---- the text a slot runs (procs, curves, state and timers it uses written out): equal to the default's -> native
std::string expandedText(const Decl& d, const std::map<std::string, const Decl*>& named) {
    Printer p;
    p.comments = false;
    p.block(d.body, "  ");
    std::string s = p.out;
    std::set<std::string> seen;
    std::function<void(const Node&)> walk;
    auto use = [&](const std::string& n) {
        auto c = named.find(n);
        if (c == named.end() || seen.count(n)) return;
        seen.insert(n);
        const Decl& x = *c->second;
        Printer q; q.comments = false;
        Decl copy = x;
        copy.comments.clear(); copy.trail.clear();
        q.decl(copy);
        s += q.out;
        for (auto& b : x.body) walk(b);
    };
    walk = [&](const Node& n) {
        if (n.op == N_CALL || n.op == N_DO || n.op == N_NAME || n.op == N_INDEX || n.op == N_SET) use(n.name);
        for (auto& k : n.k) walk(k);
        for (auto& a : n.arms) for (auto& b : a.body) walk(b);
        if (n.hasElse) for (auto& b : n.elseArm.body) walk(b);
    };
    for (auto& st : d.body) walk(st);
    return s;
}

// =====================================================================================================
// Format versions: one converter per older version (like maps' kMapUpgrades and brains' kBrainUpgrades)
// =====================================================================================================

using ModeLogicUpgrade = std::string (*)(const std::string& logic);   // version N -> N + 1 (the logic section, from its `logic` line)
// kModeLogicUpgrades[k] converts version k + 1 to k + 2. Version 1 is the first: none yet.
const ModeLogicUpgrade kModeLogicUpgrades[] = {nullptr};
constexpr int kNumModeLogicUpgrades = 0;
static_assert(kNumModeLogicUpgrades == kModeLogicVersion - 1, "every older logic version needs a converter in kModeLogicUpgrades");

const char* const kDefaultNames[MODE_COUNT + 1] = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "arena", "tourney", "hide", "tag", "points", "surf", "common"};
const char* defaultText(int mode) {   // (-1: common)
    const char* want = mode < 0 || mode >= MODE_COUNT ? "common" : kDefaultNames[mode];
    for (auto& d : kDefaultModeTexts) if (strcmp(d.name, want) == 0) return d.text;
    return "";
}

// parse a whole text (the .qmode's key lines kept), no merging
std::shared_ptr<File> parseText(const std::string& text, ModeDiag& diag) {
    auto f = std::make_shared<File>();
    Split sp = splitQmode(text);
    f->prefix = sp.prefix;
    f->keys = sp.keys;
    if (!sp.logicLine) { f->hasLogic = false; return f; }
    f->hasLogic = true;
    Parser p;
    if (!p.file(text, sp.logicLine, *f)) { diag.errors.push_back({p.err.line, p.err.col, p.err.msg}); return nullptr; }
    if (f->version != kModeLogicVersion) { diag.errors.push_back({sp.logicLine, 7, "logic " + std::to_string(f->version) + ": this game reads logic " + std::to_string(kModeLogicVersion)}); return nullptr; }
    return f;
}

std::shared_ptr<const File> defaultFile(int mode) {   // (-1: common)
    static std::mutex mu;
    static std::shared_ptr<const File> cache[MODE_COUNT + 1];
    std::lock_guard<std::mutex> lock(mu);
    int i = mode < 0 || mode >= MODE_COUNT ? MODE_COUNT : mode;
    if (!cache[i]) {
        ModeDiag d;
        auto f = parseText(defaultText(mode), d);
        if (!f) { fprintf(stderr, "%s.qlogic: %d:%d: %s\n", kDefaultNames[i], d.errors[0].line, d.errors[0].col, d.errors[0].msg.c_str()); f = std::make_shared<File>(); }
        cache[i] = f;
    }
    return cache[i];
}

const char* declKindName(int k) { static const char* n[] = {"param", "curve", "use", "state", "timer", "proc", "slot"}; return n[k]; }

bool mergeInto(Merged& m, const File& f, int origin, int base, ModeDiag& diag) {
    std::set<std::string> seen;
    bool ok = true;
    for (const Decl& d0 : f.decls) {
        Decl d = d0;
        d.origin = origin;
        std::string key = d.key();
        if (seen.count(key)) { diag.errors.push_back({d.line, d.col, (d.kind == Decl::SLOT ? key : "'" + d.name + "'") + " declared twice"}); ok = false; continue; }
        seen.insert(key);
        auto it = m.byKey.find(key);
        if (it != m.byKey.end()) {
            Decl& old = m.file.decls[it->second];
            if (old.kind != d.kind) {
                diag.errors.push_back({d.line, d.col, "'" + d.name + "' is a " + declKindName(old.kind) + " of " + (old.origin ? originFile(old.origin, base) : "this file") + ": a " + declKindName(d.kind) + " can't take its name"});
                ok = false; continue;
            }
            if ((d.kind == Decl::PARAM || d.kind == Decl::STATE) && d.ptype != old.ptype) {
                diag.errors.push_back({d.line, d.col, std::string(declKindName(d.kind)) + " '" + d.name + "' is a " + typeName(old.ptype) + " (the built-in modes read it as one)"});
                ok = false; continue;
            }
            old = d;
        } else {
            m.byKey[key] = m.file.decls.size();
            m.file.decls.push_back(d);
        }
    }
    return ok;
}

bool reservedName(const std::string& n) {
    if (isKeyword(n) || isAggregate(n) || weaponByName(n) >= 0 || constByName(n) || n == "steps" || n == "points" || n == "everyone" || n == "playing") return true;
    for (auto& b : kBuiltins) if (n == b.name) return true;
    for (auto& f : kFields) if (n == f.name) return true;
    for (auto& a : kArrays) if (n == a.name) return true;
    for (auto& f : kFuncs) if (n == f.name) return true;
    for (auto& a : kActions) if (n == a.name) return true;
    for (auto& s : kSlots) { for (auto& a : s.args) if (n == a.name) return true; if (s.out.name && n == s.out.name) return true; }
    return n.find('.') != std::string::npos;
}

std::string keysText(const std::vector<std::pair<std::string, std::string>>& keys) {
    std::string s;
    for (auto& k : keys) if (k.first != "custommode" && k.first != "maps" && k.first != "label") s += k.first + "=" + k.second + "\n";
    return s;
}

// Build a logic from a parsed file. base: MODE_*.
std::shared_ptr<ModeLogic> build(const File& user, int base, ModeDiag& diag, bool forceInterpreter) {
    Merged m;
    auto common = defaultFile(-1), def = defaultFile(base);
    mergeInto(m, *common, 2, base, diag);
    mergeInto(m, *def, 1, base, diag);
    if (!diag.errors.empty()) return nullptr;
    Merged defM = m;   // (the default alone: native comparison)
    if (!mergeInto(m, user, 0, base, diag)) return nullptr;
    m.file.version = kModeLogicVersion;
    m.file.base = kBaseNames[base];
    m.file.tail.clear();

    auto L = std::make_shared<ModeLogic>();
    L->base = base;
    L->name = kBaseNames[base];
    for (auto& k : user.keys) if (k.first == "custommode" && !k.second.empty()) L->name = k.second;
    for (auto& s : L->start) s = -1;
    std::vector<Curve> curves;
    Compiler c(*L, diag, m, base, curves);
    // declarations
    int nParams = 0, nStates = 0, nProcs = 0;
    for (const Decl& d : m.file.decls) {
        c.origin = d.origin;
        if (d.kind == Decl::SLOT || d.kind == Decl::USE) continue;
        const ParamInfo* pi = d.kind == Decl::PARAM ? paramInfo(d.name) : nullptr;
        if (!pi && reservedName(d.name)) { c.error(d.line, d.col, "'" + d.name + "' is a reserved name (a keyword, constant, sensor, field, action or slot argument)"); continue; }
        if (pi && pi->type != d.ptype) { c.error(d.line, d.col, "param '" + d.name + "' is a " + typeName(pi->type) + " (the game reads it as one)"); continue; }
        switch (d.kind) {
            case Decl::PARAM:
                nParams++;
                c.params[d.name] = (int)c.ptype.size(); c.ptype.push_back(d.ptype);
                L->P.push_back(d.value);
                L->params.push_back({d.name, d.ptype, d.value, pi != nullptr});
                break;
            case Decl::CURVE:
                if (d.form == 0) c.exprCurves[d.name] = &d;
                else {
                    Curve cv;
                    cv.kind = d.form == 1 ? 0 : 1;
                    cv.base = (float)d.base;
                    cv.n = (uint8_t)d.pts.size();
                    for (size_t k = 0; k < d.pts.size(); k++) { cv.x[k] = (float)d.pts[k].first; cv.y[k] = (float)d.pts[k].second; }
                    c.dataCurves[d.name] = (int)curves.size();
                    curves.push_back(cv);
                }
                break;
            case Decl::STATE: {
                nStates++;
                ModeStateVar v;
                v.name = d.name; v.life = (uint8_t)d.life; v.per = (uint8_t)d.per; v.type = (uint8_t)d.ptype; v.init = d.value;
                v.base = L->stateValues;
                L->stateValues += d.per == 1 ? 2 : d.per == 2 ? kModePlayers : 1;
                c.states[d.name] = (int)L->state.size();
                L->state.push_back(v);
                break;
            }
            case Decl::TIMER:
                c.timers[d.name] = (int)L->timers.size();
                L->timers.push_back(d.name);
                break;
            case Decl::PROC: nProcs++; c.procs[d.name] = &d; break;
            default: break;
        }
    }
    c.origin = 0;
    for (const Decl& d : m.file.decls)
        if (d.kind == Decl::USE) {
            int o = objectByName(d.name);
            L->uses |= 1u << o;
            L->needs |= kObjects[o].needs;
            if (!(kObjects[o].modes >> base & 1) && d.origin == 0)
                diag.warnings.push_back({d.line, d.col, std::string("use ") + d.name + ": " + kBaseNames[base] + " doesn't use it natively, so the HUD won't show it and bots won't play it"});
        }
    // objects' params nobody declared: their defaults (the native code reads them)
    for (const ParamInfo& p : kParams)
        if (p.obj >= 0 && (L->uses >> p.obj & 1) && !c.params.count(p.name)) {
            c.params[p.name] = (int)c.ptype.size(); c.ptype.push_back(p.type);
            L->P.push_back(p.def);
            L->params.push_back({p.name, p.type, p.def, true});
        }
    auto limit = [&](int n, int max, const char* what) { if (n > max) diag.errors.push_back({1, 1, std::to_string(n) + " " + what + " (at most " + std::to_string(max) + ")"}); };
    limit(nParams, 64, "params");
    limit(nStates, 32, "state declarations");
    limit((int)L->timers.size(), kModeTimers, "timers");
    limit(nProcs, 16, "procs");
    limit(L->stateValues, kModeStateValues, "state values");
    if (!diag.errors.empty()) return nullptr;
    // slots
    std::map<std::string, const Decl*> named, defNamed;
    for (auto& d : m.file.decls) if (d.kind != Decl::SLOT && d.kind != Decl::PARAM && d.kind != Decl::USE) named[d.name] = &d;
    for (auto& d : defM.file.decls) if (d.kind != Decl::SLOT && d.kind != Decl::PARAM && d.kind != Decl::USE) defNamed[d.name] = &d;
    for (const Decl& d : m.file.decls) {
        if (d.kind != Decl::SLOT) continue;
        int sl = d.slot;
        if (sl == MSLOT_TIMER) {
            auto t = c.timers.find(d.name);
            if (t == c.timers.end()) { c.origin = d.origin; c.error(d.line, d.col, "slot timer " + d.name + ": no 'timer " + d.name + "'"); continue; }
            sl = MSLOT_TIMER + t->second;
        }
        L->has[sl] = true;
        c.compileSlot(sl, d);
        bool interp = forceInterpreter || sl >= MSLOT_TIMER;
        if (!interp) {
            auto dk = defM.byKey.find(d.key());
            interp = dk == defM.byKey.end() || expandedText(d, named) != expandedText(defM.file.decls[dk->second], defNamed);
        }
        L->interp[sl] = interp;
        L->anyInterp |= interp;
    }
    for (auto& pr : c.procs)
        if (!c.calledProcs.count(pr.first) && pr.second->origin == 0)
            diag.warnings.push_back({pr.second->line, pr.second->col, "proc " + pr.first + " is never called (so it isn't checked)"});
    if (!c.usesEndMatch && !(L->uses & ((1u << MO_BRACKET) | (1u << MO_TIEBREAK))))
        diag.warnings.push_back({1, 1, "no path ends the match: no slot calls endMatch"});
    if (!diag.errors.empty()) return nullptr;
    // the data curves live after the constants (the interpreter reads them from konst: kind, n, base, x[n], y[n])
    if (!curves.empty()) {
        std::vector<int> at;
        for (auto& cv : curves) {
            at.push_back((int)L->konst.size());
            L->konst.push_back(cv.kind); L->konst.push_back(cv.n); L->konst.push_back(cv.base);
            for (int k = 0; k < 16; k++) L->konst.push_back(cv.x[k]);
            for (int k = 0; k < 16; k++) L->konst.push_back(cv.y[k]);
        }
        for (auto& in : L->code) if (in.op == OP_CURVE) in.k = at[in.k];
    }
    // canonical text and the hash of what it does
    Printer pr;
    File u = user;
    L->text = user.hasLogic ? pr.logic(u) : "";
    Printer ph;
    ph.comments = false;
    L->hash = sha256Hex(keysText(user.keys) + "\n" + ph.logic(m.file)).substr(0, 8);
    return L;
}

double toI(double x) { return std::isnan(x) ? 0 : x >= 2147483647.0 ? 2147483647.0 : x <= -2147483648.0 ? -2147483648.0 : (double)(int)x; }

} // namespace

// =====================================================================================================
// Interpreter
// =====================================================================================================

void initModeState(const ModeLogic& L, ModeLogicState& st) {
    memset(st.vars, 0, sizeof st.vars);
    for (auto& v : L.state) {
        int n = v.per == 1 ? 2 : v.per == 2 ? kModePlayers : 1;
        for (int i = 0; i < n; i++) st.vars[v.base + i] = v.init;
    }
    for (auto& t : st.timerAt) t = 0;
    st.depth = 0;
    for (auto& a : st.announceAt) a = -1e9;
    st.announceNext = 0;
    st.announceDropped = st.depthLogged = false;
    st.frozenMask = 0;
}

void resetModeState(const ModeLogic& L, ModeLogicState& st, int life) {
    for (auto& v : L.state) {
        if (v.life != life) continue;
        int n = v.per == 1 ? 2 : v.per == 2 ? kModePlayers : 1;
        for (int i = 0; i < n; i++) st.vars[v.base + i] = v.init;
    }
    if (life == 0) for (auto& t : st.timerAt) t = 0;
}

static void sayText(const ModeLogic& L, ModeLogicState& st, ModeHost& h, const ModeTextDesc& td, const double* r) {
    ModeTextPart parts[kModeTextParts];
    for (int i = 0; i < td.n; i++) {
        const ModeTextDesc::Part& p = td.part[i];
        parts[i].kind = (ModeTextPart::Kind)p.kind;
        if (p.kind == ModeTextPart::STR) { const std::string& s = L.strings[p.str]; parts[i].s = s.data(); parts[i].len = (uint32_t)s.size(); }
        else parts[i].v = (int64_t)r[p.reg];
    }
    if (td.action == MA_ANNOUNCE) {   // 4 a second at most: past that, dropped (logged once)
        double now = h.get(MF_NOW, 0, 0);
        if (now - st.announceAt[st.announceNext] < 1.0) {
            if (!st.announceDropped) {
                st.announceDropped = true;
                static const char m[] = "mode logic: announcements past 4 a second are dropped";
                ModeTextPart p; p.kind = ModeTextPart::STR; p.s = m; p.len = sizeof m - 1;
                h.say(MA_LOG, -1, &p, 1);
            }
            return;
        }
        st.announceAt[st.announceNext] = now;
        st.announceNext = (st.announceNext + 1) & 3;
    }
    h.say(td.action, td.winner >= 0 ? (int)r[td.winner] : -1, parts, td.n);
}

bool runModeSlot(const ModeLogic& L, ModeLogicState& st, ModeHost& h, int slot, const ModeArgs& args, double* result) {
    if (result) *result = 0;
    if (slot == MSLOT_RESET || slot == MSLOT_MATCH_START) resetModeState(L, st, 0);   // (even with no code: the state's lifetime)
    else if (slot == MSLOT_ROUND_SETUP) resetModeState(L, st, 1);
    if (slot < 0 || slot >= MSLOT_COUNT || L.start[slot] < 0) return true;
    if (st.depth >= kModeFrames) {
        if (!st.depthLogged) {
            st.depthLogged = true;
            static const char m[] = "mode logic: slots nested more than 4 deep: the native code ran instead";
            ModeTextPart p; p.kind = ModeTextPart::STR; p.s = m; p.len = sizeof m - 1;
            h.say(MA_LOG, -1, &p, 1);
        }
        return false;
    }
    double* r = st.frames[st.depth++];
    const ModeInsn* code = L.code.data();
    const double* P = L.P.data();
    const double* K = L.konst.data();
    double* V = st.vars;
    const double phase0 = h.get(MF_PHASE, 0, 0);
    for (int pc = L.start[slot];;) {
        const ModeInsn& i = code[pc++];
        switch (i.op) {
            case OP_LDK: r[i.d] = K[i.k]; break;
            case OP_LDP: r[i.d] = P[i.k]; break;
            case OP_LDV: r[i.d] = V[i.k]; break;
            case OP_STV: V[i.k] = r[i.d]; break;
            case OP_LDVI: { int x = (int)r[i.a]; r[i.d] = (unsigned)x < i.b ? V[i.k + x] : 0; break; }
            case OP_STVI: { int x = (int)r[i.a]; if ((unsigned)x < i.b) V[i.k + x] = r[i.d]; break; }
            case OP_ARG: r[i.d] = args.v[i.k]; break;
            case OP_GET0: r[i.d] = h.get(i.k, 0, 0); break;
            case OP_GET1: r[i.d] = h.get(i.k, (int)r[i.a], 0); break;
            case OP_GET2: r[i.d] = h.get(i.k, (int)r[i.a], (int)r[i.b]); break;
            case OP_SETF: h.set(i.k, (int)r[i.a], r[i.b]); break;
            case OP_MOV: r[i.d] = r[i.a]; break;
            case OP_ADDF: r[i.d] = (float)r[i.a] + (float)r[i.b]; break;
            case OP_SUBF: r[i.d] = (float)r[i.a] - (float)r[i.b]; break;
            case OP_MULF: r[i.d] = (float)r[i.a] * (float)r[i.b]; break;
            case OP_DIVF: r[i.d] = (float)r[i.a] / (float)r[i.b]; break;
            case OP_ADDD: r[i.d] = r[i.a] + r[i.b]; break;
            case OP_SUBD: r[i.d] = r[i.a] - r[i.b]; break;
            case OP_MULD: r[i.d] = r[i.a] * r[i.b]; break;
            case OP_DIVD: r[i.d] = r[i.a] / r[i.b]; break;
            case OP_ADDI: r[i.d] = (double)(int32_t)(uint32_t)((int64_t)r[i.a] + (int64_t)r[i.b]); break;
            case OP_SUBI: r[i.d] = (double)(int32_t)(uint32_t)((int64_t)r[i.a] - (int64_t)r[i.b]); break;
            case OP_MULI: r[i.d] = (double)(int32_t)(uint32_t)((int64_t)r[i.a] * (int64_t)r[i.b]); break;
            case OP_MODI: { int64_t b = (int64_t)r[i.b]; r[i.d] = b == 0 ? 0 : (double)((int64_t)r[i.a] % b); break; }
            case OP_LT: r[i.d] = r[i.a] < r[i.b]; break;
            case OP_LE: r[i.d] = r[i.a] <= r[i.b]; break;
            case OP_GT: r[i.d] = r[i.a] > r[i.b]; break;
            case OP_GE: r[i.d] = r[i.a] >= r[i.b]; break;
            case OP_EQ: r[i.d] = r[i.a] == r[i.b]; break;
            case OP_NE: r[i.d] = r[i.a] != r[i.b]; break;
            case OP_NOT: r[i.d] = r[i.a] == 0; break;
            case OP_NEG: r[i.d] = -r[i.a]; break;
            case OP_TOF: r[i.d] = (float)r[i.a]; break;
            case OP_TOI: r[i.d] = toI(r[i.a]); break;
            case OP_ABS: r[i.d] = std::fabs(r[i.a]); break;
            case OP_SQRTF: r[i.d] = std::sqrt((float)r[i.a]); break;
            case OP_SQRTD: r[i.d] = std::sqrt(r[i.a]); break;
            case OP_EXPF: r[i.d] = std::exp((float)r[i.a]); break;
            case OP_EXPD: r[i.d] = std::exp(r[i.a]); break;
            case OP_MIN: r[i.d] = r[i.b] < r[i.a] ? r[i.b] : r[i.a]; break;
            case OP_MAX: r[i.d] = r[i.a] < r[i.b] ? r[i.b] : r[i.a]; break;
            case OP_CLAMP: { double v = r[i.a], lo = r[i.b], hi = r[i.k]; r[i.d] = v >= lo ? (v > hi ? hi : v) : lo; break; }
            case OP_OTHER: r[i.d] = r[i.a] == 0 ? 1 : 0; break;
            case OP_CURVE: {
                const double* c = K + i.k;
                float x = (float)r[i.a], v;
                int n = (int)c[1];
                const double *xs = c + 3, *ys = c + 19;
                if (c[0] == 0) { v = (float)c[2]; for (int k = 0; k < n; k++) if (x > (float)xs[k]) v = (float)ys[k]; }
                else if (n == 0) v = 0;
                else if (!(x > (float)xs[0])) v = (float)ys[0];
                else {
                    v = (float)ys[n - 1];
                    for (int k = 0; k + 1 < n; k++)
                        if (x <= (float)xs[k + 1]) { v = (float)ys[k] + ((float)ys[k + 1] - (float)ys[k]) * ((x - (float)xs[k]) / ((float)xs[k + 1] - (float)xs[k])); break; }
                }
                r[i.d] = v;
                break;
            }
            case OP_MASK: r[i.d] = (double)h.players(i.k, (int)r[i.a]); break;
            case OP_INSET: { int x = (int)r[i.a]; r[i.d] = (unsigned)x < 32u && (((uint32_t)r[i.b] >> x) & 1u); break; }
            case OP_NEXT: {
                uint32_t m = (uint32_t)r[i.a];
                if (!m) { pc = i.k; break; }
                r[i.d] = (double)__builtin_ctz(m);
                r[i.a] = (double)(m & (m - 1));
                break;
            }
            case OP_BIT: r[i.d] = (double)((uint32_t)r[i.d] | (1u << ((int)r[i.a] & 31))); break;
            case OP_PICK: {
                uint32_t m = (uint32_t)r[i.a];
                if (!m) { r[i.d] = -1; break; }
                uint32_t k = h.qrand() % (uint32_t)__builtin_popcount(m);
                while (k--) m &= m - 1;
                r[i.d] = (double)__builtin_ctz(m);
                break;
            }
            case OP_TLEFT: {
                double at = st.timerAt[i.k];
                r[i.d] = at > 0 ? (float)std::max(0.0, at - h.get(MF_NOW, 0, 0)) : 0.0f;
                break;
            }
            case OP_TSTART: { double at = h.get(MF_NOW, 0, 0) + (float)r[i.a]; st.timerAt[i.k] = at > 0 ? at : 1e-9; break; }
            case OP_TSTOP: st.timerAt[i.k] = 0; break;
            case OP_ACT:
                r[i.d] = h.act(i.k, r + i.a, i.b);
                if (h.get(MF_PHASE, 0, 0) != phase0) goto done;
                break;
            case OP_SAY:
                sayText(L, st, h, L.texts[i.k], r);
                if (h.get(MF_PHASE, 0, 0) != phase0) goto done;
                break;
            case OP_JMP: pc = i.k; break;
            case OP_JZ: if (r[i.a] == 0) pc = i.k; break;
            case OP_JNZ: if (r[i.a] != 0) pc = i.k; break;
            case OP_RET: goto done;
        }
    }
done:
    if (result && slotInfo(slot).query) *result = r[0];
    st.depth--;
    return true;
}

void runModeTimers(const ModeLogic& L, ModeLogicState& st, ModeHost& h) {
    if (L.timers.empty()) return;
    double now = h.get(MF_NOW, 0, 0);
    for (size_t t = 0; t < L.timers.size(); t++) {
        if (st.timerAt[t] <= 0 || now < st.timerAt[t]) continue;
        st.timerAt[t] = 0;
        if (L.has[MSLOT_TIMER + t]) runModeSlot(L, st, h, MSLOT_TIMER + (int)t);
    }
}

// =====================================================================================================
// API
// =====================================================================================================

const char* modeSlotName(int slot) { return slot >= 0 && slot < MSLOT_COUNT ? slotInfo(slot).name : "?"; }
const char* modeTypeName(int type) { return typeName(type); }
const char* modeBaseName(int mode) { return mode >= 0 && mode < MODE_COUNT ? kBaseNames[mode] : "?"; }
int modeBaseByName(const std::string& n) {
    if (n == "1v1") return MODE_ARENA;
    for (int i = 0; i < MODE_COUNT; i++) if (n == kBaseNames[i]) return i;
    return -1;
}
const char* defaultModeText(int mode) { return defaultText(mode); }

std::string upgradeModeLogicText(const std::string& text, std::string* err, int* fromVersion) {
    Split sp = splitQmode(text);
    if (fromVersion) *fromVersion = sp.logicLine ? sp.version : kModeLogicVersion;
    if (!sp.logicLine) return text;   // (no logic: nothing to convert)
    int v = sp.version;
    if (v < 1 || v > kModeLogicVersion) {
        if (err) *err = v < 1 ? "logic 0: versions start at 1" : "logic " + std::to_string(v) + " is newer than this game reads (" + std::to_string(kModeLogicVersion) + ")";
        return "";
    }
    std::string logic = text.substr(sp.prefix.size());
    for (int k = v; k < kModeLogicVersion; k++) logic = kModeLogicUpgrades[k - 1](logic);
    return sp.prefix + logic;
}

std::shared_ptr<const ModeLogic> compileModeLogic(const std::string& text, int base, ModeDiag* diagOut, bool forceInterpreter) {
    ModeDiag local;
    ModeDiag& diag = diagOut ? *diagOut : local;
    std::string err;
    int from = 0;
    std::string t = upgradeModeLogicText(text, &err, &from);
    if (t.empty() && !text.empty()) { diag.errors.push_back({splitQmode(text).logicLine, 7, err}); return nullptr; }
    auto f = parseText(t, diag);
    if (!f) return nullptr;
    int keyBase = -1;
    for (auto& k : f->keys) if (k.first == "mode") keyBase = modeBaseByName(k.second);
    int fileBase = f->base.empty() || f->base == "common" ? -1 : modeBaseByName(f->base);
    if (f->base == "common") { diag.errors.push_back({splitQmode(t).logicLine + 1, 6, "base common is the shared part: it's merged under a mode, not played"}); return nullptr; }
    if (!f->hasLogic && f->keys.empty()) { diag.errors.push_back({1, 1, "no logic: a .qmode's key=value lines, then 'logic 1'"}); return nullptr; }
    if (base < 0) base = keyBase >= 0 ? keyBase : fileBase;
    if (base < 0 || base >= MODE_COUNT) { diag.errors.push_back({1, 1, "which mode does it play as? (a 'base' line, or the .qmode's mode= key)"}); return nullptr; }
    if (fileBase >= 0 && fileBase != base) {
        diag.errors.push_back({splitQmode(t).logicLine + 1, 6, std::string("base ") + f->base + ", but the mode plays as " + kBaseNames[base]});
        return nullptr;
    }
    return build(*f, base, diag, forceInterpreter);
}

std::shared_ptr<const ModeLogic> defaultModeLogic(int mode) {
    if (mode < 0 || mode >= MODE_COUNT) return nullptr;
    static std::mutex mu;
    static std::shared_ptr<const ModeLogic> cache[MODE_COUNT];
    {
        std::lock_guard<std::mutex> lock(mu);
        if (cache[mode]) return cache[mode];
    }
    ModeDiag d;
    auto L = compileModeLogic(defaultText(mode), mode, &d);
    if (!L) fprintf(stderr, "%s.qlogic doesn't compile: %s\n", kBaseNames[mode], d.errors.empty() ? "?" : d.errors[0].msg.c_str());
    std::lock_guard<std::mutex> lock(mu);
    if (!cache[mode]) cache[mode] = L;
    return cache[mode];
}

std::string modeLabel(const ModeLogic& L) { return L.name + "#" + L.hash; }

std::string modeDiagJson(const ModeDiag& d, const ModeLogic* L) {
    auto msgs = [](const std::vector<ModeDiag::Msg>& v) {
        std::string o = "[";
        for (size_t i = 0; i < v.size(); i++)
            o += std::string(i ? "," : "") + "{\"line\":" + std::to_string(v[i].line) + ",\"col\":" + std::to_string(v[i].col) + ",\"msg\":" + jstr(v[i].msg) + "}";
        return o + "]";
    };
    std::string o = std::string("{\"ok\":") + (d.errors.empty() && L ? "true" : "false") + ",\"errors\":" + msgs(d.errors) + ",\"warnings\":" + msgs(d.warnings);
    if (L) {
        o += ",\"name\":" + jstr(L->name) + ",\"base\":" + jstr(kBaseNames[L->base]) + ",\"hash\":" + jstr(L->hash) + ",\"label\":" + jstr(modeLabel(*L)) + ",\"uses\":[";
        bool first = true;
        for (int i = 0; i < MO_COUNT; i++) if (L->uses >> i & 1) { o += std::string(first ? "" : ",") + jstr(kObjects[i].name); first = false; }
        o += "],\"interpreted\":[";
        first = true;
        for (int s = 0; s < MSLOT_COUNT; s++)
            if (L->interp[s]) { o += std::string(first ? "" : ",") + jstr(s >= MSLOT_TIMER ? "timer " + L->timers[s - MSLOT_TIMER] : modeSlotName(s)); first = false; }
        o += "],\"slots\":[";
        first = true;
        for (int s = 0; s < MSLOT_COUNT; s++) {
            if (!L->has[s]) continue;
            o += std::string(first ? "" : ",") + "{\"slot\":" + jstr(s >= MSLOT_TIMER ? "timer " + L->timers[s - MSLOT_TIMER] : modeSlotName(s)) +
                 ",\"interpreted\":" + (L->interp[s] ? "true" : "false") + ",\"instructions\":" + std::to_string(L->insns[s]) + ",\"worstCase\":" + std::to_string(L->cost[s]) +
                 ",\"registers\":" + std::to_string(L->regs[s]) + "}";
            first = false;
        }
        o += "],\"params\":[";
        for (size_t i = 0; i < L->params.size(); i++) {
            const ModeParamValue& p = L->params[i];
            o += std::string(i ? "," : "") + "{\"name\":" + jstr(p.name) + ",\"type\":\"" + typeName(p.type) + "\",\"value\":" +
                 (p.type == T_BOOL ? std::string(p.value ? "true" : "false") : fmtNumber(p.value, p.type)) + "}";
        }
        o += "],\"needs\":[";
        static const char* nn[] = {"hill", "sites", "cart", "points", "checkpoints"};
        first = true;
        for (int i = 0; i < 5; i++) if (L->needs >> i & 1) { o += std::string(first ? "" : ",") + jstr(nn[i]); first = false; }
        o += "]";
    }
    return o + "}";
}

std::string modeAstJson(const std::string& text, ModeDiag* diagOut) {
    ModeDiag local;
    ModeDiag& diag = diagOut ? *diagOut : local;
    auto f = parseText(text, diag);
    if (!f) return "";
    JsonAst j;
    return j.file(*f);
}

std::string printModeText(const std::string& text, ModeDiag* diagOut) {
    ModeDiag local;
    ModeDiag& diag = diagOut ? *diagOut : local;
    auto f = parseText(text, diag);
    if (!f) return "";
    if (!f->hasLogic) return f->prefix;
    Printer p;
    return f->prefix + p.logic(*f);
}

std::string exportModeJson() {
    std::string o = "{\"format\":\"qmode-export\",\"version\":" + std::to_string(kModeLogicVersion) + ",\n\"catalog\":{";
    o += "\"types\":[\"bool\",\"int\",\"f32\",\"f64\",\"team\",\"player\",\"zone\",\"phase\",\"bombstate\",\"spawn\",\"weapon\",\"text\",\"timer\"],\n";
    o += "\"keywords\":[";
    for (size_t k = 0; k < sizeof kKeywords / sizeof kKeywords[0]; k++) o += std::string(k ? "," : "") + jstr(kKeywords[k]);
    o += "],\n\"limits\":{\"params\":64,\"states\":32,\"timers\":" + std::to_string(kModeTimers) + ",\"procs\":16,\"stateValues\":" + std::to_string(kModeStateValues) +
         ",\"instructionsPerSlot\":" + std::to_string(kModeMaxInsns) + ",\"worstCasePerCall\":" + std::to_string(kModeMaxCost) + ",\"nestedSlots\":" + std::to_string(kModeFrames) +
         ",\"textArguments\":" + std::to_string(kModeTextParts) + ",\"announcementsPerSecond\":4,\"stringBytes\":120,\"procParams\":4,\"registers\":" + std::to_string(kModeRegs) + "},\n";
    o += "\"slots\":[";
    for (int s = 0; s <= MSLOT_TIMER; s++) {
        const MSlotInfo& si = kSlots[s];
        o += std::string(s ? ",\n" : "\n") + "{\"name\":" + jstr(si.name) + ",\"kind\":\"" + (si.query ? "query" : "event") + "\",\"args\":[";
        for (size_t k = 0; k < si.args.size(); k++) o += std::string(k ? "," : "") + "{\"name\":" + jstr(si.args[k].name) + ",\"type\":\"" + typeName(si.args[k].type) + "\",\"doc\":" + jstr(si.args[k].doc) + "}";
        o += "],\"output\":" + (si.query ? "{\"name\":" + jstr(si.out.name) + ",\"type\":\"" + typeName(si.out.type) + "\",\"doc\":" + jstr(si.out.doc) + "}" : std::string("null"));
        o += ",\"when\":" + jstr(si.when) + ",\"doc\":" + jstr(si.doc) + (s == MSLOT_TIMER ? ",\"timer\":true" : "") + "}";
    }
    o += "],\n\"fields\":[";
    for (int f = 0; f < kNumFields; f++)
        o += std::string(f ? ",\n" : "\n") + "{\"name\":" + jstr(kFields[f].name) + ",\"type\":\"" + typeName(kFields[f].type) + "\",\"rw\":" + (kFields[f].rw ? "true" : "false") +
             ",\"unit\":" + jstr(kFields[f].unit) + ",\"doc\":" + jstr(kFields[f].doc) + "}";
    o += "],\n\"arrays\":[";
    for (int a = 0; a < kNumArrays; a++)
        o += std::string(a ? ",\n" : "\n") + "{\"name\":" + jstr(kArrays[a].name) + ",\"index\":\"" + typeName(kArrays[a].index) + "\",\"type\":\"" + typeName(kArrays[a].type) +
             "\",\"rw\":" + (kArrays[a].rw ? "true" : "false") + ",\"unit\":" + jstr(kArrays[a].unit) + ",\"doc\":" + jstr(kArrays[a].doc) + "}";
    o += "],\n\"functions\":[";
    for (size_t f = 0; f < sizeof kFuncs / sizeof kFuncs[0]; f++) {
        o += std::string(f ? ",\n" : "\n") + "{\"name\":" + jstr(kFuncs[f].name) + ",\"args\":[";
        for (size_t k = 0; k < kFuncs[f].args.size(); k++) o += std::string(k ? "," : "") + "\"" + typeName(kFuncs[f].args[k]) + "\"";
        o += "],\"type\":\"" + std::string(typeName(kFuncs[f].ret)) + "\",\"doc\":" + jstr(kFuncs[f].doc) + "}";
    }
    o += "],\n\"builtins\":[";
    for (size_t k = 0; k < sizeof kBuiltins / sizeof kBuiltins[0]; k++)
        o += std::string(k ? "," : "") + "{\"name\":" + jstr(kBuiltins[k].name) + ",\"sig\":" + jstr(kBuiltins[k].sig) + ",\"doc\":" + jstr(kBuiltins[k].doc) + "}";
    o += "],\n\"aggregates\":["
         "{\"name\":\"count\",\"type\":\"int\",\"doc\":\"players in the set (where the condition holds)\"},"
         "{\"name\":\"any\",\"type\":\"bool\",\"doc\":\"a player in the set (where...): stops at the first\"},"
         "{\"name\":\"first\",\"type\":\"player\",\"doc\":\"the first player in slot order (where...); none if nobody\"},"
         "{\"name\":\"random\",\"type\":\"player\",\"doc\":\"a random one (where...): one qrand() draw when there is anyone, qrand() % n; none if nobody\",\"draws\":1},"
         "{\"name\":\"best\",\"type\":\"player\",\"doc\":\"the first highest by the value (where...); none if nobody\"},"
         "{\"name\":\"names\",\"type\":\"text\",\"doc\":\"their names joined with ', ' (text arguments only)\"}],\n";
    o += "\"sets\":[{\"name\":\"everyone\",\"doc\":\"connected players\"},{\"name\":\"playing\",\"doc\":\"connected, not spectating\"},{\"name\":\"team\",\"doc\":\"team(t): connected and on team t\"}],\n";
    o += "\"actions\":[";
    for (int k = 0; k < kNumActions; k++) {
        const ActionInfo& a = kActions[k];
        o += std::string(k ? ",\n" : "\n") + "{\"name\":" + jstr(a.name) + ",\"args\":[";
        for (size_t i = 0; i < a.args.size(); i++) o += std::string(i ? "," : "") + "{\"name\":" + jstr(a.args[i].name) + ",\"type\":\"" + typeName(a.args[i].type) + "\",\"doc\":" + jstr(a.args[i].doc) + "}";
        o += "],\"returns\":\"" + std::string(typeName(a.ret)) + "\",\"ends\":" + (a.ends ? "true" : "false") + ",\"object\":" + (a.obj >= 0 ? jstr(kObjects[(int)a.obj].name) : std::string("null")) +
             ",\"available\":" + (a.available ? "true" : "false") + ",\"draws\":" + std::to_string(a.draws) + ",\"doc\":" + jstr(a.doc) + "}";
    }
    o += "],\n\"objects\":[";
    for (int k = 0; k < MO_COUNT; k++) {
        const ObjectInfo& ob = kObjects[k];
        auto list = [](const std::vector<const char*>& v) { std::string s = "["; for (size_t i = 0; i < v.size(); i++) s += std::string(i ? "," : "") + jstr(v[i]); return s + "]"; };
        o += std::string(k ? ",\n" : "\n") + "{\"name\":" + jstr(ob.name) + ",\"doc\":" + jstr(ob.doc) + ",\"steps\":" + list(ob.steps) + ",\"events\":" + list(ob.events) +
             ",\"params\":" + list(ob.params) + ",\"modes\":[";
        bool first = true;
        for (int m = 0; m < MODE_COUNT; m++) if (ob.modes >> m & 1) { o += std::string(first ? "" : ",") + jstr(kBaseNames[m]); first = false; }
        o += "]}";
    }
    o += "],\n\"params\":[";
    for (int k = 0; k < kNumParams; k++) {
        const ParamInfo& p = kParams[k];
        o += std::string(k ? ",\n" : "\n") + "{\"name\":" + jstr(p.name) + ",\"type\":\"" + typeName(p.type) + "\",\"unit\":" + jstr(p.unit) + ",\"min\":" + fmtNumber(p.lo, T_F64) +
             ",\"max\":" + fmtNumber(p.hi, T_F64) + ",\"object\":" + (p.obj >= 0 ? jstr(kObjects[(int)p.obj].name) : std::string("null")) + ",\"doc\":" + jstr(p.doc) +
             ",\"objectDefault\":" + fmtNumber(p.def, p.type) + ",\"default\":{";
        bool first = true;
        for (int m = 0; m < MODE_COUNT; m++) {
            auto L = defaultModeLogic(m);
            if (!L) continue;
            for (auto& pv : L->params)
                if (pv.name == p.name) { o += std::string(first ? "" : ",") + jstr(kBaseNames[m]) + ":" + fmtNumber(pv.value, p.type); first = false; }
        }
        o += "}}";
    }
    o += "],\n\"constants\":[";
    for (size_t k = 0; k < sizeof kConsts / sizeof kConsts[0]; k++)
        o += std::string(k ? "," : "") + "{\"name\":" + jstr(kConsts[k].name) + ",\"type\":\"" + typeName(kConsts[k].type) + "\",\"value\":" + fmtNumber(kConsts[k].v, T_F64) + ",\"doc\":" + jstr(kConsts[k].doc) + "}";
    o += ",{\"name\":\"none\",\"type\":\"none\",\"value\":-1,\"doc\":\"no team / player / weapon / bomb (typed from where it goes)\"}";
    o += "],\n\"weapons\":[";
    for (int w = 0; w < WPN_COUNT; w++) o += std::string(w ? "," : "") + jstr(kWeaponNames[w]);
    o += "],\"modes\":[";
    for (int m = 0; m < MODE_COUNT; m++) o += std::string(m ? "," : "") + jstr(kBaseNames[m]);
    o += "]},\n\"defaults\":{";
    for (int m = -1; m < MODE_COUNT; m++) {
        auto f = defaultFile(m);
        JsonAst j;
        std::string nm = m < 0 ? "common" : kBaseNames[m];
        o += std::string(m >= 0 ? ",\n" : "\n") + jstr(nm) + ":{\"text\":" + jstr(defaultText(m)) + ",\"ast\":" + j.file(*f);
        if (m >= 0) {
            auto L = defaultModeLogic(m);
            if (L) {
                o += ",\"hash\":" + jstr(L->hash) + ",\"slots\":[";
                bool first = true;
                for (int s = 0; s < MSLOT_COUNT; s++)
                    if (L->has[s]) { o += std::string(first ? "" : ",") + "{\"slot\":" + jstr(modeSlotName(s)) + ",\"worstCase\":" + std::to_string(L->cost[s]) + "}"; first = false; }
                o += "]";
            }
        }
        o += "}";
    }
    o += "}}\n";
    return o;
}

// ---- for tests (tests/mode_tests.cpp)
std::string modeFmtNumberForTests(double v, bool f32) { return fmtNumber(v, f32 ? T_F32 : T_F64); }
