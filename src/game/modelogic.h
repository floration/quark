// Game mode logic: a mode's rules as data (tools/modes/GRAMMAR.md is the format; tools/modes/common.qlogic and
// tools/modes/<mode>.qlogic the built-in modes, which describe what gamemodes.cpp does natively).
//
// The language half: lexer, parser, canonical printer, JSON AST, checker, compiler and an allocation-free
// interpreter. It has no dependency on Server: the game binds it through ModeHost (sensors, fields, actions and the
// objects' steps), and the tests through a test double. Copied and adapted from botbrain.cpp's language half (a
// shared `qlang` is a later refactor).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

constexpr int kModeLogicVersion = 1;   // the `logic N` line (older versions are converted: upgradeModeLogicText)
constexpr int kModePlayers = 32;       // player slots (kMaxPlayers); a `for` walks them in order
constexpr int kModeTimers = 8;         // `timer` declarations
constexpr int kModeStateValues = 512;  // state storage (a per-player array takes 32, a per-team one 2)
constexpr int kModeFrames = 4;         // nested slot calls at run time (kill() in tick runs slot kill, ...)
constexpr int kModeRegs = 64;          // registers per frame
constexpr int kModeMaxInsns = 4096;    // instructions per slot
constexpr int kModeMaxCost = 8192;     // worst-case instructions per slot call (a loop's body counts 32 times)
constexpr int kModeTextParts = 8;      // arguments of a text action

// ---- slots: the points where the engine calls the logic (GRAMMAR.md section 5)
enum ModeSlot : uint8_t {
    // events (statements)
    MSLOT_RESET, MSLOT_WARMUP, MSLOT_MATCH_START, MSLOT_ROUND_SETUP, MSLOT_ROUND_START, MSLOT_LIVE, MSLOT_TICK,
    MSLOT_TIME_UP, MSLOT_KILL, MSLOT_STAB, MSLOT_LEAVE, MSLOT_SPECTATE, MSLOT_ROUND_OVER,
    MSLOT_PLANT, MSLOT_DEFUSE, MSLOT_EXPLODE, MSLOT_CART_CHECKPOINT, MSLOT_DELIVERED, MSLOT_CAPTURE, MSLOT_CHECKPOINT,
    MSLOT_FINISH,
    // queries (`return <value>`: the result)
    MSLOT_FREEZE_LENGTH, MSLOT_ROUND_LENGTH, MSLOT_ROUNDS_TO_WIN, MSLOT_RESPAWN_DELAY, MSLOT_CAN_DAMAGE, MSLOT_CAN_HIT,
    MSLOT_FROZEN, MSLOT_SPAWN_TYPE, MSLOT_TEAM_LOCKED, MSLOT_CAN_AUTO_START,
    MSLOT_TIMER,                                 // + timer index: `slot timer <name>` (a timer ran out)
    MSLOT_COUNT = MSLOT_TIMER + kModeTimers
};
// A slot's arguments (ModeArgs.v[k], in this order): kill [victim, killer, weapon]; stab [attacker, victim];
// leave, spectate, plant, defuse, finish, respawnDelay, frozen, spawnType [p]; cartCheckpoint, capture [i];
// checkpoint [p, i]; canDamage, canHit [attacker, victim, weapon]. Players are 0-31 (-1: none), weapons the
// WPN_* ids or a kill cause (250 dissolved (acid), 251 grapple, 252 fell, 253 bombed, 254 drained; -1 none).
struct ModeArgs { int v[3] = {-1, -1, -1}; };

// ---- what the logic reads and writes through the host: ModeHost::get / set (GRAMMAR.md section 6)
// Values are doubles holding the field's type: bool 0/1, team 0 (A) / 1 (B) / 2 (spectator) / -1 (none), player
// 0-31 / -1, phase Phase, bombstate BombState (none 0), spawn SpawnTeam (spawn.attack = 4, spawn.defend = 5).
enum ModeField : uint8_t {
    // scalars (a, b unused)
    MF_NOW, MF_DT, MF_PHASE, MF_ROUND_NO, MF_PHASE_END, MF_LIVE_SINCE, MF_ROUND_START_TIME, MF_LAST_PROGRESS, MF_TEAMS,
    MF_RANKED, MF_TICK_RATE, MF_CFG_TIME_LIMIT, MF_CFG_SCORE_LIMIT, MF_CFG_ROUNDS_TO_WIN, MF_HAS_HILL, MF_HAS_CART,
    MF_SITE_COUNT, MF_POINT_COUNT, MF_CHECKPOINT_COUNT,
    MF_ATTACKERS,                                // rw
    MF_HUD_CLOCK, MF_HUD_OVERTIME, MF_HUD_CONTESTED, MF_HUD_DRAIN, MF_HUD_TARGET, MF_HUD_OWNER,   // rw
    MF_HUD_ROUNDS_TO_WIN,
    MF_BOMB_STATE, MF_BOMB_CARRIER, MF_BOMB_ACTING, MF_BOMB_TIMER, MF_CART_DIST, MF_CART_LENGTH, MF_CART_CHECKPOINT,
    MF_POINT_INDEX, MF_POINT_CAPTURE, MF_CART_CHECKPOINT_COUNT, MF_STAGED_SPAWNS,
    // per team (a = team; out of range: get 0, set ignored)
    MF_ALIVE_COUNT, MF_TEAM_COUNT, MF_TEAM_SCORE /*rw*/, MF_CART_BEST /*rw*/, MF_CART_COMPLETED /*rw*/, MF_CART_NEAR, MF_CART_BEST_TIME,
    MF_TEAM_POOL /*rw*/, MF_TEAM_POOL_MAX,
    // per player (a = player; out of range: get 0, set ignored)
    MF_ALIVE, MF_TEAM, MF_IS_BOT, MF_ON_GROUND, MF_USE_HELD, MF_HEALTH, MF_NEAR_CART, MF_MAP_SPAWN,
    MF_SCORE /*rw*/, MF_OBJ_TIME /*rw*/, MF_IT /*rw*/, MF_OUT /*rw*/, MF_FOUND /*rw*/, MF_SOUGHT /*rw*/, MF_KILLS,
    MF_DEATHS, MF_FREEZE_UNTIL, MF_RACE_CHECKPOINT, MF_RACE_FINISHED,
    MF_IN_ZONE,                                  // a = player, b = zone (ModeZone)
    MF_COUNT
};
// A zone value: kind + 8 * index (hill and sites take no index).
enum ModeZone : uint8_t { MZ_HILL = 0, MZ_SITES, MZ_SITE, MZ_POINT, MZ_CHECKPOINT };
inline int modeZoneKind(int z) { return z & 7; }
inline int modeZoneIndex(int z) { return z >> 3; }
// Player sets (ModeHost::players): connected; connected and not spectating; connected and on team `team`.
enum ModeSet : uint8_t { MSET_EVERYONE, MSET_PLAYING, MSET_TEAM };

// Actions the host does (ModeHost::act; text ones: ModeHost::say). Arguments in catalog order (GRAMMAR.md 7).
enum ModeAction : uint8_t {
    MA_SET_CLOCK, MA_ADD_TIME, MA_TOUCH_OBJECTIVE, MA_HOLD_OVERTIME,
    MA_KILL, MA_DAMAGE, MA_RESPAWN, MA_FREEZE, MA_SWAP_TEAMS,
    MA_BOMB_STEP, MA_GIVE_BOMB, MA_CART_STEP, MA_NEUTRAL_CART_STEP, MA_POINT_STEP, MA_RACE_STEP, MA_RACE_PRACTICE,
    MA_STALEMATE_DRAIN, MA_PICK_SEEKER, MA_PICK_RANDOM_IT, MA_TAG_OUT, MA_PASS_IT, MA_START_TIEBREAK,
    MA_TOURNEY_BEGIN, MA_TOURNEY_WALKOVER, MA_TOURNEY_ADVANCE, MA_PICK_DUEL_MODIFIER, MA_POOL_STEP,
    // text actions (say)
    MA_END_ROUND, MA_END_ROUND_NO_SCORE, MA_END_ROUND_TEXT, MA_END_MATCH, MA_ANNOUNCE, MA_LOG,
    MA_COUNT
};
// Native mechanics a mode switches on with `use` (bit per object in ModeLogic::uses).
enum ModeObject : uint8_t { MO_BOMB, MO_CART, MO_NCART, MO_POINTS, MO_RACE, MO_DRAIN, MO_IT, MO_TIEBREAK, MO_BRACKET, MO_DUELMODS, MO_POOL, MO_COUNT };

// One argument of a text action, as the host gets it (the host builds the string, like Server::message).
struct ModeTextPart {
    enum Kind : uint8_t { STR, PLAYER, TEAM, INT, LETTER, NAMES } kind = STR;
    int64_t v = 0;                               // PLAYER: id (-1: "nobody"); TEAM: 0/1 ("TEAM A"/"TEAM B", else
                                                 // "nobody"); INT: the number; LETTER: 0 = 'A'; NAMES: player mask
    const char* s = nullptr; uint32_t len = 0;   // STR (not NUL-terminated)
};

// The game side. Called only while a slot runs; nothing here is called per player per tick unless the logic asks.
class ModeHost {
public:
    virtual ~ModeHost() = default;
    virtual double get(int field, int a, int b) = 0;     // ModeField
    virtual void set(int field, int a, double v) = 0;    // a rw ModeField (a: the index for arrays)
    virtual uint32_t players(int set, int team) = 0;     // ModeSet membership, bit per player slot
    // A ModeAction that isn't a text action; args in catalog order; the value of the ones that return something
    // (bool 0/1), else 0. An action may run slots itself (kill() runs slot kill: re-entrant runModeSlot calls).
    virtual double act(int action, const double* args, int n) = 0;
    // A text action: winner (endRound / endRoundNoScore / endMatch: a team, or a player where the base mode scores
    // players; -1 none), the parts in order.
    virtual void say(int action, int winner, const ModeTextPart* parts, int n) = 0;
    virtual uint32_t qrand() = 0;                        // the game's qrand() (random(...) draws one, % n)
};

struct ModeInsn { uint8_t op, d, a, b; int32_t k; };   // 8 bytes

// A text action's arguments, laid out at compile time (the strings live in ModeLogic::strings).
struct ModeTextDesc {
    uint8_t action = 0, n = 0;
    int16_t winner = -1;                         // the register holding the winner (-1: none)
    struct Part { uint8_t kind = 0, reg = 0; uint16_t str = 0; } part[kModeTextParts];
};

struct ModeParamValue { std::string name; int type = 0; double value = 0; bool catalog = false; };  // type: "f32" etc. (modeTypeName)
struct ModeStateVar { std::string name; uint8_t life = 0 /*0 match, 1 round*/, per = 0 /*0 scalar, 1 team, 2 player*/, type = 0; int base = 0; double init = 0; };

// A compiled mode logic: the built-in defaults merged with a file. Shared (const) by the servers that play it.
struct ModeLogic {
    std::string name;                            // the .qmode's custommode= key (else the base mode's name)
    int base = -1;                               // MODE_* it plays as
    std::string hash;                            // 8 hex: GRAMMAR.md 9 (the key lines and the merged logic)
    std::string text;                            // the file's logic, canonical (no key lines)
    bool has[MSLOT_COUNT] = {};                  // the merged logic writes the slot
    bool interp[MSLOT_COUNT] = {};               // ...and it differs from the base mode's default: interpreted
    bool anyInterp = false;
    uint32_t uses = 0;                           // ModeObject bits
    uint32_t needs = 0;                          // ModeNeed bits: map features it reads
    std::vector<ModeParamValue> params;          // every param (merged; objects' params at their defaults if unset)
    std::vector<std::string> timers;             // timer names (index = MSLOT_TIMER + i)
    std::vector<ModeStateVar> state;
    int stateValues = 0;
    // the program (every slot of the merged logic is compiled, interpreted or not)
    std::vector<ModeInsn> code;
    int32_t start[MSLOT_COUNT];                  // -1: no slot
    uint16_t insns[MSLOT_COUNT] = {};            // instructions
    uint32_t cost[MSLOT_COUNT] = {};             // worst-case instructions per call
    uint8_t regs[MSLOT_COUNT] = {};
    std::vector<double> konst, P;                // constants; param values (by param index)
    std::vector<ModeTextDesc> texts;
    std::vector<std::string> strings;
    double param(const std::string& n, double def = 0) const { for (auto& p : params) if (p.name == n) return p.value; return def; }
    bool uses_(int obj) const { return (uses >> obj & 1) != 0; }
};

// Run-time state of one server's logic: state values, timers, the register frames. No allocation after init.
struct ModeLogicState {
    double vars[kModeStateValues];
    double timerAt[kModeTimers];                 // game time a timer runs out (0: stopped)
    double frames[kModeFrames][kModeRegs];
    int depth = 0;
    double announceAt[4] = {-1e9, -1e9, -1e9, -1e9};   // the last 4 announcements (4 a second at most)
    int announceNext = 0;
    bool announceDropped = false, depthLogged = false;
    uint32_t frozenMask = 0;                     // (for the host: an interpreted `frozen`, worked out once a tick)
};
// Every state value to its declared value, timers stopped.
void initModeState(const ModeLogic& L, ModeLogicState& st);
// State with lifetime `life` (0 match, 1 round) back to its declared values (match also stops the timers). runModeSlot
// does this itself before running reset / matchStart (match) and roundSetup (round); call it at those points when the
// slot runs natively.
void resetModeState(const ModeLogic& L, ModeLogicState& st, int life);
// Run an interpreted slot. *result: a query's value (0 if it never returned one). False when the nested call limit
// (kModeFrames) is hit: the caller runs the native code instead (logged once).
bool runModeSlot(const ModeLogic& L, ModeLogicState& st, ModeHost& h, int slot, const ModeArgs& args = ModeArgs(), double* result = nullptr);
// Timers that ran out: their `slot timer <name>` (once a tick, before tick).
void runModeTimers(const ModeLogic& L, ModeLogicState& st, ModeHost& h);

// ---- compiling, checking, the editor's tools
struct ModeDiag {
    struct Msg { int line = 0, col = 0; std::string msg; };
    std::vector<Msg> errors, warnings;
};
// Compile a .qmode (key=value lines, then `logic N` and the logic) or a bare logic text (`logic N` first). base: MODE_*
// it plays as, or -1 for the text's `mode=` key / `base` line. Older logic versions are converted first. Null on errors
// (in *diag, with line:col of the whole text). forceInterpreter: every slot is interpreted (lockstep tests, cost).
std::shared_ptr<const ModeLogic> compileModeLogic(const std::string& text, int base = -1, ModeDiag* diag = nullptr, bool forceInterpreter = false);
// The built-in default (common merged under the mode), compiled (cached). Every slot native.
std::shared_ptr<const ModeLogic> defaultModeLogic(int mode);
const char* defaultModeText(int mode);           // tools/modes/<mode>.qlogic (-1: common.qlogic)
// Logic text of an older version converted to the current one (whole .qmode text in, out; unchanged if current;
// "" + *err if it can't be).
std::string upgradeModeLogicText(const std::string& text, std::string* err = nullptr, int* fromVersion = nullptr);
std::string modeLabel(const ModeLogic& L);       // "name#hash"
std::string modeDiagJson(const ModeDiag& d, const ModeLogic* L);   // checkmode's JSON
std::string modeAstJson(const std::string& text, ModeDiag* diag);   // the JSON AST ("" on parse errors)
std::string printModeText(const std::string& text, ModeDiag* diag); // canonical text: key lines verbatim, then the logic ("" on errors)
std::string exportModeJson();                    // catalog + every default's text and AST (quark_tests exportmode)
// Map features the logic needs (ModeNeed bits): checkmode --map
enum ModeNeed : uint8_t { MN_HILL = 1, MN_SITES = 2, MN_CART = 4, MN_POINTS = 8, MN_CHECKPOINTS = 16 };
inline uint32_t modeLogicNeeds(const ModeLogic& L) { return L.needs; }
const char* modeSlotName(int slot);              // "tick", "timer" for the timer slots
const char* modeTypeName(int type);
const char* modeBaseName(int mode);              // the logic's mode names: vs bomb ... arena (= 1v1) ... points
int modeBaseByName(const std::string& n);        // also "1v1" (the .qmode key's spelling); -1
