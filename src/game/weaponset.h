// Weapon sets (.qweapons, tools/maptool/weapon/DESIGN.md sections 1-3): every weapon number as a field a set can
// change, the .qweapons parser / canonical printer / JSON AST, the type checker and the small interpreter that runs
// a set's `hit` / `kill` slots on the server. No dependency on Server: server.cpp fills a WsCall and runs the slot.
//
// A custom mode names a set (`weaponset=<name>`: <user data>/custom_weapons/<name>.qweapons); the host loads its text
// into CustomRules::weaponSetText, which goes to clients as `wsline=` lines, and every machine compiles the same text
// when the rules are applied (customrules.cpp rebuild: once per rules change, never per shot). No set: the standard
// weapons, exactly (a test).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "gamedefs.h"

struct CustomRules;

constexpr int kWeaponSetFormatVersion = 1;
constexpr size_t kMaxWeaponSetBytes = 20000;   // a set's text at most (it travels in EV_RULES: kMaxRulesTextBytes below; the
                                               // standard set with every field is ~8 KB)

// ---- every weapon number, as values
struct WeaponValues {
    WeaponDef def[WPN_COUNT];
    WeaponExtra ex[WPN_COUNT];
    WeaponAlt alt;
};
const WeaponValues& standardWeaponValues();   // today's game: baseWeaponDef + the gamedefs.h constants
const char* weaponKey(int w);                 // "rifle" ... "locator" (the names in a set)
int weaponFromKey(const std::string& s);      // -1 if none

// ---- the fields (`<weapon>.<field>` keys): what a set can write
enum WeaponFieldType : uint8_t { WF_F32, WF_INT, WF_BOOL };
struct WeaponFieldInfo {
    int weapon;
    std::string field, key;    // "damage", "rocket.damage"
    uint8_t type;              // WeaponFieldType
    const char* unit;
    double lo, hi;             // clamped to (out of range: the nearest bound, with a warning)
    const char* doc;
    bool alt;                  // an alt-fire field
    bool readOnly;             // the weapon's kind (projectile / beam / sticky): shown, never written
    uint8_t store;             // (internal: which part of WeaponValues)
    size_t off;
};
int weaponFieldCount();
const WeaponFieldInfo& weaponField(int i);
int weaponFieldIndex(const std::string& key);           // -1 if not a field
double getWeaponField(const WeaponValues& v, int i);    // bool / int as 0/1 / the number
void setWeaponField(WeaponValues& v, int i, double x);  // clamped to the field's range (nan: unchanged)

// ---- the logic slots
enum WsSlot : uint8_t { WS_HIT = 0, WS_KILL = 1, WS_SLOTS = 2 };
enum WsSensor : uint8_t {
    WSS_WEAPON, WSS_ALT, WSS_DIST, WSS_HEAD, WSS_DIRECT, WSS_SPLASH, WSS_SELF, WSS_AIRBORNE, WSS_VICTIM_HEALTH,
    WSS_SHOOTER_HEALTH, WSS_VICTIM_SPEED, WSS_SHOOTER_SPEED, WSS_TIME, WSS_STREAK, WSS_KILLS, WSS_COUNT
};
enum WsOutput : uint8_t { WSO_DAMAGE = 0, WSO_KNOCK = 1, WSO_HEAL = 2, WSO_REFUND = 3, WSO_COUNT = 4 };
// One call: the server fills `in` (bool 0/1, weapon its id) and the outputs' starting values (hit: damage = what the
// game worked out, knock 1, heal 0; kill: heal 0, refund 0), runs the slot, reads `out`.
struct WsCall {
    double in[WSS_COUNT] = {};
    double out[WSO_COUNT] = {};
};

struct WeaponSet;   // a compiled set (opaque): values, labels, logic
struct WsDiag {
    struct Msg { int line = 0, col = 0; std::string msg; };
    std::vector<Msg> errors, warnings;
};

const char* standardWeaponSetText();   // the standard set as .qweapons text (every field; canonical)
// Parse + check + compile. nullptr on errors (diag has them, with line:col). Warnings: values clamped etc.
std::shared_ptr<const WeaponSet> compileWeaponSet(const std::string& text, WsDiag* diag = nullptr);
const std::string& weaponSetName(const WeaponSet& s);          // `weapons "<name>"`
const WeaponValues& weaponSetValues(const WeaponSet& s);       // every field (standard where the set doesn't write it)
bool weaponSetWrites(const WeaponSet& s, int field);           // the set writes this field
const std::string& weaponSetLabel(const WeaponSet& s, int w);  // `label <w> "..."` ("" = the standard name)
bool weaponSetHasLogic(const WeaponSet& s);                    // any hit / kill slot
bool weaponSetHasSlot(const WeaponSet& s, int slot, int w);    // a slot runs for weapon w (its own or the general one)
void runWeaponSlot(const WeaponSet& s, int slot, int w, WsCall& c);   // no allocation; a no-op when no slot runs

// Per server: what the logic's `streak` and `kills` sensors count (Server::weaponHit).
struct WsTracker {
    double lastHit[kMaxPlayers][kMaxPlayers] = {};   // [shooter][victim]: the time of the last hit
    uint16_t streak[kMaxPlayers][kMaxPlayers] = {};
    int lifeKills[kMaxPlayers] = {}, lifeDeaths[kMaxPlayers] = {};   // kills this life (counted since `deaths` was lifeDeaths)
};

// The calling thread's active set when it has logic (customrules.cpp; nullptr otherwise: the server's fast path).
const WeaponSet* activeWeaponLogic();

// ---- the editor's tools (quark_tests exportweapons / checkweapons / weaponsast / printweapons)
std::string exportWeaponsJson();                                   // the catalog (DESIGN section 3)
std::string weaponSetDiagJson(const WsDiag& d);                    // {ok, errors:[{line,col,msg}], warnings:[...]}
std::string weaponSetAstJson(const std::string& text, WsDiag* d);  // "" on a parse error
std::string printWeaponSetText(const std::string& text, WsDiag* d);// canonical text; "" on a parse error
std::string weaponValuesJson(const WeaponValues& v);               // {"rocket.damage": 34, ...} every field

// ---- numbers the editor shows (the Values table): per weapon and fire mode, from a set's values
struct WeaponStats {
    int weapon = 0;
    bool alt = false;          // the alt-fire (only weapons whose alt-fire is a shot of its own, or the scope)
    const char* mode = "";     // "primary", "scoped", "lob", "throw", "bounce", "pull", "stream", "heal"
    float dmgBody = 0, dmgHead = 0;   // one shot (every pellet / bubble landing; a rocket: a direct hit)
    float refire = 0;          // s between shots
    int shotsBody = 0, shotsHead = 0; // to kill `health` (0: can't)
    float ttkBody = 0, ttkHead = 0;   // s (the first shot at 0)
    float dpsBody = 0, dpsHead = 0;   // sustained, from refire
    float range = 0, projSpeed = 0, splash = 0;
    float travel[3] = {};      // s to 10 / 20 / 40 m (projectiles; 0 hitscan)
    float falloff[3] = {};     // damage x at 10 / 20 / 40 m
};
std::vector<WeaponStats> computeWeaponStats(const WeaponValues& v, float health = kMaxHealth);
std::string weaponStatsJson(const WeaponValues& v, float health = kMaxHealth);

// ---- files and the rules
std::string customWeaponsDir();                     // <user data>/custom_weapons
std::vector<std::string> listWeaponSets();          // names, sorted
bool loadWeaponSetFile(const std::string& name, std::string& text);
bool saveWeaponSetFile(const std::string& name, const std::string& text);
std::string safeWeaponSetName(const std::string& n);
// CustomRules' weapon set lines: `weaponset=<name>` and (network form) one `wsline=<line>` per line of the text.
std::string weaponSetRulesLines(const CustomRules& r);   // "\nweaponset=...\nwsline=..." ("" without a set)
bool readWeaponSetRulesLine(CustomRules& r, const std::string& k, const std::string& v);   // true if it was one
// After loading a custom mode: resolve weaponset=<name> to its file (no file, an invalid set, or rules too big to
// send: the standard weapons, and *err says why). True when the rules play a set.
bool resolveWeaponSet(CustomRules& r, std::string* err = nullptr);
// The rules' network text at most: EV_RULES sends it in 240 byte chunks, all queued at once when a player joins, next
// to the welcome (up to 62 events); a client with 256 unacknowledged events is dropped (server.cpp kMaxUnackedEvents),
// so 100 chunks, not the 255 the chunk count's byte could hold.
constexpr size_t kMaxRulesTextBytes = 100 * 240;
