// The weapons editor's pieces of quark_maptool (README.md here; ../DESIGN.md section 4): the rules a request asks for
// (`weapons` = a .qweapons text, `qmode` = a custom mode text, `weaponsAllowed` = the weapon pool), applied the way a
// hosted custom mode applies them (CustomRules + setActiveRules, per thread), and the per weapon tally the bot
// editor's sim / simbatch report with them. Commands: wpnparams, wpncheck, wpnstats (wpn_stats.cpp), wpnrange
// (wpn_range.cpp). Everything is in namespace wpn so these files never collide with the other editors' (one executable).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "json.h"
#include "game/customrules.h"
#include "game/gamedefs.h"
#include "game/weaponset.h"

class Server;

namespace wpn {

// ---- the request's rules -------------------------------------------------------------------------------------------
struct Rules {
    bool any = false;              // the request asked for something (else: the standard rules, nothing applied)
    CustomRules rules;             // name "wpn-editor" unless the qmode names itself
    std::string setName;           // the weapon set's `weapons "<name>"` ("" = none: the standard weapons)
    bool hasLogic = false;         // the set has hit / kill slots
    uint16_t allowed = 0;          // weaponsAllowed as a mask (0: every weapon)
    std::string qmodeName;         // the qmode's custommode= ("" = no qmode)
    std::vector<std::string> warnings;   // set warnings (clamped values ...), a qmode's weaponset= that didn't load
};
// `weapons` (text, or {text}), `qmode` (text, or {text}), `weaponsAllowed` ([names] or a bitmask). Resolves a qmode's
// `weaponset=<name>` from the player's custom_weapons folder (call before anything points the user data folder at a
// temp folder). false: a set with errors / an unknown weapon name (err says which, with line:col).
bool requestRules(const Json& req, Rules& out, std::string& err);
// A Server that BotSim (or anything) started without them: cfg_.custom = the rules (the server thread applies them
// with setActiveRules on its next tick, as Server::frame does), and the lives already running fixed up to them (a
// weapon the pool doesn't allow swapped for the first one it does, a custom health). No-op when !r.any.
void applyToServer(Server& s, const Rules& r);
// What a start line echoes: weaponSet (name or null), weaponLogic, weaponsAllowed ([names] or null), qmode (name or
// null), and `weaponWarnings` when there are any.
void startJson(Json& line, const Rules& r);
uint16_t maskFromJson(const Json& j, std::string& err);   // [names] / a number / "all"
Json maskJson(uint16_t mask);                              // [names] (null: every weapon)
int weaponFromName(const std::string& s);                  // "rocket", "Rocket launcher", a set's label (active rules) ... -1

// ---- per weapon numbers of a match (sim / simbatch) -------------------------------------------------------------
// Sampled between steps: each player's shots, hits and damage counters (ServerPlayer::shotsFired / shotsHit /
// dmgDealt) grow; the growth goes to the weapon the player held at the previous sample. Kills come from the kill
// events (exact). Time to kill: from the start of the victim's fight (its first damage after 3 s without any, as
// sampled) to the kill.
struct Tally {
    struct W { int kills = 0, shots = 0, hits = 0, deaths = 0, ttkN = 0; double damage = 0, ttkSum = 0, held = 0; };
    W w[WPN_COUNT];
    // per player (by id): the last sample
    struct P { bool seen = false, alive = false; int weapon = -1, shots = 0, hits = 0; float dmg = 0, health = 0; double firstDmg = -1, lastDmg = -1; };
    P p[kMaxPlayers];
    double lastT = 0;
    void observe(const Server& s, double t);                       // after a step: counters, alive / damage state
    void kill(int killer, int victim, const std::string& weaponName, double t);   // a kill event (before observe)
    void merge(const Tally& o);
    Json json() const;   // [{weapon, kills, damage, shots, hits, accuracy, ttk, ttkSamples, deaths, heldSeconds}] (weapons used)
};

// ---- the weapon numbers (wpn_stats.cpp) ---------------------------------------------------------------------------
// The Values table's rows (the page's stats.js rows, same ids): one per weapon and fire mode from a set's values.
Json statsRows(const WeaponValues& v, const WeaponSet* set, float health, float dist);

}  // namespace wpn
