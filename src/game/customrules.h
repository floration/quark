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
    CustomRules();
    bool active() const { return !name.empty(); }
    std::string toText() const;       // key=value lines (the file and the network format)
    bool fromText(const std::string& text);  // false if it isn't a custom mode
};

const CustomRules& activeRules();
void setActiveRules(const CustomRules& r);   // the rules everything in this process plays by (and the weapon table)
std::string customModesDir();                 // where presets are saved
std::vector<std::string> listCustomModes();   // preset names, sorted
bool loadCustomMode(const std::string& name, CustomRules& out);
bool saveCustomMode(const CustomRules& r);
bool deleteCustomMode(const std::string& name);
