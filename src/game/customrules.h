// Custom game modes: a named set of rules built in the game (Host > Custom modes) and hosted like any mode.
// The host's server applies them and sends them to everyone who joins (EV_RULES), so every client predicts
// movement and weapon timing with the same numbers. Stored as text: custom_modes/<name>.qmode.
#pragma once
#include <string>
#include <vector>
#include "gamedefs.h"

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
    int timeLimit = 0, scoreLimit = 0, roundsToWin = 0;   // 0 = the mode's default
    std::vector<std::string> maps;    // map pool (rotates after each match); empty = the host's map only
    // Lines this build doesn't read but must not lose: a save from Host > Custom modes rewrites the known keys and
    // keeps these as they were, so another editor's keys (the physics editor, a newer game) and the mode editor's
    // logic survive it.
    std::vector<std::string> extraKeys;   // key=value lines with a key this build doesn't know, verbatim, in file order
    std::string logic;                // the mode logic: everything from the first `logic` line to the end of the file,
                                      // verbatim ("" = none); it starts with that line. Never parsed for keys.
    CustomRules();
    bool active() const { return !name.empty(); }
    std::string toText() const;       // the .qmode file: the known keys (canonical order), then extraKeys, then logic
    std::string toNetText() const;    // what EV_RULES sends: the known keys only (clients never run logic)
    bool fromText(const std::string& text);  // false if it isn't a custom mode
};
bool isModeLogicLine(const std::string& line);   // `logic` or `logic <anything>`: where a .qmode's logic block starts

const CustomRules& activeRules();
void setActiveRules(const CustomRules& r);   // the rules the CALLING THREAD plays by (and its weapon table); per thread, so a hosted server and the client never share them
std::string customModesDir();                 // where presets are saved
std::vector<std::string> listCustomModes();   // preset names, sorted
bool loadCustomMode(const std::string& name, CustomRules& out);
bool saveCustomMode(const CustomRules& r);
bool deleteCustomMode(const std::string& name);
