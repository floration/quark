// Custom game modes: a named set of rules built in the game (Host > Custom modes) and hosted like any mode.
// The host's server applies them and sends them to everyone who joins (EV_RULES), so every client predicts
// movement and weapon timing with the same numbers. Stored as text: custom_modes/<name>.qmode.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "gamedefs.h"
struct ModeLogic;

struct CustomRules {
    std::string name;                 // empty = no custom mode (the standard rules)
    int baseMode = MODE_FFA;          // the mode it plays as (objectives, teams, rounds)
    float fireRate[WPN_COUNT];        // per weapon: x shots per second (2 = twice as fast)
    float damage[WPN_COUNT];          // per weapon: x damage (headshots too)
    float projSpeed[WPN_COUNT];       // per weapon: x projectile speed
    uint16_t weapons = 0;             // weapons allowed (bit per weapon id); 0 = all
    float gravity = 1, moveSpeed = 1, jump = 1, knockback = 1;
    float move[MV_COUNT];             // every other movement constant: x the normal value (MOVEMENT page)
    int health = 100;
    bool dash = true, grapple = true, selfDamage = true, instagib = false, vampire = false, regen = false;
    bool rocketRide = true;           // the grapple catches rockets in flight (rocket riding)
    bool fly = false;                 // 3D FLYING (owner, 2026-11-02: a modifier, on any mode or map): zero-g flight, MOVE UP / DOWN
    int timeLimit = 0, scoreLimit = 0, roundsToWin = 0;   // 0 = the mode's default
    std::vector<std::string> maps;    // map pool (rotates after each match); empty = the host's map only
    // Lines this build doesn't read but must not lose: a save from Host > Custom modes rewrites the known keys and
    // keeps these as they were, so another editor's keys (the physics editor, a newer game) and the mode editor's
    // logic survive it.
    std::vector<std::string> extraKeys;   // key=value lines with a key this build doesn't know, verbatim, in file order
    std::string logic;                // the mode logic: everything from the first `logic` line to the end of the file,
                                      // verbatim ("" = none); it starts with that line. Never parsed for keys.
    std::string label;                // the logic's name#hash8 (GRAMMAR.md 9; "" = no logic): the server fills it in
                                      // and sends it as label=; read back for display only, never trusted
    int logicLine = 0;                // fromText: the file line the logic starts on (1 = the first; 0: none / not read)
    // A weapon set (weaponset.h; the weapons editor's): `weaponset=<name>` names custom_weapons/<name>.qweapons, whose
    // text loadCustomMode reads into weaponSetText. The network form carries the text (`wsline=` lines, so clients
    // compile the same set); the file form only the name. Empty: the standard weapons.
    std::string weaponSetName, weaponSetText;
    // movement equations (moveexpr.h): {key, expression} from `expr.<key>=` lines (gravity, speed, jump or a move key),
    // kept as written, one per key, in file order. setActiveRules resolves / compiles them; the plain values above stay
    // what the file says (the fallback, and what older versions play)
    std::vector<std::pair<std::string, std::string>> exprs;
    CustomRules();
    bool active() const { return !name.empty(); }
    std::string toText() const;       // the .qmode file: the known keys (canonical order, no label=), then extraKeys, then logic
    std::string toNetText() const;    // what EV_RULES sends: the known keys and label= (clients never run logic)
    bool fromText(const std::string& text);  // false if it isn't a custom mode
};
bool isModeLogicLine(const std::string& line);   // `logic` or `logic <anything>`: where a .qmode's logic block starts
// The rules' logic compiled as a server plays it (toText(), base = baseMode); null with no logic, or on an error (then
// *err: "the mode logic doesn't compile: line:col message"). Its label: modeLabel (modelogic.h).
std::shared_ptr<const ModeLogic> compileCustomLogic(const CustomRules& r, std::string* err = nullptr);

// A vote's modifiers ("gravity=1.5\nvampire=1\n...", Server::applyVotedRules) as the vote shows them: "GRAVITY X1.5, VAMPIRE"
std::string votedModsTitle(const std::string& mods);

const CustomRules& activeRules();
void setActiveRules(const CustomRules& r);   // the rules the CALLING THREAD plays by (and its weapon table); per thread, so a hosted server and the client never share them
std::string customModesDir();                 // where presets are saved
std::vector<std::string> listCustomModes();   // preset names, sorted
bool loadCustomMode(const std::string& name, CustomRules& out);
bool saveCustomMode(const CustomRules& r);
bool deleteCustomMode(const std::string& name);
