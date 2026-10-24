#include "../common/u8file.h"
#include "customrules.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "map.h"
#include "modelogic.h"
#include "weaponset.h"

namespace {
// Per thread: a hosted server thread and the client each play by their own copy. One trivially initialised
// thread_local pointer, so each accessor is a single TLS lookup: MinGW compiles thread_local as emutls (a call per
// access, plus one per init guard), and moveTuning()/weaponDef() run in every 1 ms movement and projectile step.
struct Tuned {
    CustomRules rules; WeaponDef weapons[WPN_COUNT]; MoveTuning move;
    WeaponExtra extra[WPN_COUNT]; WeaponAlt alt;    // (weaponset.h: the rest of each weapon's numbers)
    std::shared_ptr<const WeaponSet> set;           // the rules' weapon set, compiled (null: the standard weapons)
    const WeaponSet* logic = nullptr;               // set, when it has hit / kill slots (the server's per-hit check)
    char labels[WPN_COUNT][32] = {};                // a set's weapon names (WeaponDef::name points here)
};
thread_local Tuned* g_t = nullptr;

void rebuild(Tuned& t) {
    // The weapon set first (absolute values: the standard ones where it writes none), then the mode's multipliers
    // on top. Compiled here, once per rules change: nothing per shot. A set that doesn't compile: the standard weapons.
    t.set.reset(); t.logic = nullptr;
    if (t.rules.active() && !t.rules.weaponSetText.empty()) t.set = compileWeaponSet(t.rules.weaponSetText);
    const WeaponValues& v = t.set ? weaponSetValues(*t.set) : standardWeaponValues();
    if (t.set && weaponSetHasLogic(*t.set)) t.logic = t.set.get();
    for (int w = 0; w < WPN_COUNT; w++) {
        WeaponDef d = v.def[w];
        WeaponExtra e = v.ex[w];
        if (t.set && !weaponSetLabel(*t.set, w).empty()) {
            snprintf(t.labels[w], sizeof t.labels[w], "%s", weaponSetLabel(*t.set, w).c_str());
            d.name = t.labels[w];
        }
        if (t.rules.active()) {
            d.refire /= std::max(0.05f, t.rules.fireRate[w]);
            d.damage *= t.rules.damage[w];
            d.projSpeed *= t.rules.projSpeed[w];
            e.headDamage *= t.rules.damage[w];     // (headshots and direct hits too: the multiplier is the weapon's damage)
            e.directDamage *= t.rules.damage[w];
        }
        t.weapons[w] = d;
        t.extra[w] = e;
    }
    t.alt = v.alt;
    if (t.rules.active()) t.alt.syringeHeal *= t.rules.damage[WPN_SYRINGE];
    t.move = MoveTuning();
    if (t.rules.active()) {
        t.move.gravity = t.rules.gravity; t.move.speed = t.rules.moveSpeed; t.move.jump = t.rules.jump;
        t.move.dash = t.rules.dash; t.move.grapple = t.rules.grapple;
        for (int k = 0; k < MV_COUNT; k++) t.move.m[k] = t.rules.move[k];
    }
}

Tuned& tuned() {
    Tuned* t = g_t;
    // (on the heap, never freed: a thread_local object with a destructor crashed at thread exit under MinGW, its
    // emutls block freed before the destructor ran, a heap corruption (UI test); a few KB per thread that plays)
    if (__builtin_expect(t == nullptr, 0)) { t = new Tuned(); rebuild(*t); g_t = t; }
    return *t;
}

std::string safeName(const std::string& n) {
    std::string s;
    for (char c : n) s += (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    if (s.size() > 40) s.resize(40);   // (cut first, then trim: else 40 spaces + "x" left an all-space name)
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
} // namespace

CustomRules::CustomRules() {
    for (float& v : move) v = 1;
    for (int w = 0; w < WPN_COUNT; w++) fireRate[w] = damage[w] = projSpeed[w] = 1;
}

const WeaponDef& weaponDef(int w) { return tuned().weapons[w < 0 || w >= WPN_COUNT ? 0 : w]; }
const WeaponExtra& weaponExtra(int w) { return tuned().extra[w < 0 || w >= WPN_COUNT ? 0 : w]; }
const WeaponAlt& weaponAlt() { return tuned().alt; }
const WeaponSet* activeWeaponLogic() { return tuned().logic; }
float tunedDamage(int w) { const Tuned& t = tuned(); return t.rules.active() && w >= 0 && w < WPN_COUNT ? t.rules.damage[w] : 1.0f; }
const MoveTuning& moveTuning() { return tuned().move; }
const CustomRules& activeRules() { return tuned().rules; }
void setActiveRules(const CustomRules& r) { Tuned& t = tuned(); t.rules = r; rebuild(t); }

bool isModeLogicLine(const std::string& line) {
    // (any `logic` line starts the block, even with a bad version: the logic compiler reports that, and the lines
    // after it must not be read as keys)
    return line.compare(0, 5, "logic") == 0 && (line.size() == 5 || line[5] == ' ' || line[5] == '\t' || line[5] == '\r');
}

std::string CustomRules::toNetText() const {
    std::ostringstream o;
    o << "custommode=" << name << "\nmode=" << modeName(baseMode) << "\nweapons=" << weapons
      << "\ngravity=" << gravity << "\nspeed=" << moveSpeed << "\njump=" << jump << "\nknockback=" << knockback
      << "\nhealth=" << health << "\ndash=" << dash << "\ngrapple=" << grapple << "\nselfdamage=" << selfDamage
      << "\ninstagib=" << instagib << "\nvampire=" << vampire << "\nregen=" << regen
      << "\ntimelimit=" << timeLimit << "\nscorelimit=" << scoreLimit << "\nrounds=" << roundsToWin;
    for (int k = 0; k < MV_COUNT; k++) if (move[k] != 1) o << "\nmove_" << moveParamKey(k) << "=" << move[k];   // (only the changed ones)
    for (int w = 0; w < WPN_COUNT; w++)
        o << "\nweapon=" << w << "," << fireRate[w] << "," << damage[w] << "," << projSpeed[w];
    o << weaponSetRulesLines(*this);   // (weaponset= / wsline=: weaponset.cpp)
    o << "\nmaps=";
    for (size_t i = 0; i < maps.size(); i++) o << (i ? "," : "") << maps[i];
    o << "\n";
    if (!label.empty()) o << "label=" << label << "\n";   // (the logic's name#hash8: never part of the hash)
    return o.str();
}

std::string CustomRules::toText() const {
    // (no label=: the file's label would go stale at the next edit of its logic; the server works it out at start)
    CustomRules keys;
    std::string t;
    if (label.empty()) t = toNetText();
    else { keys = *this; keys.label.clear(); t = keys.toNetText(); }
    for (const std::string& l : extraKeys) { t += l; t += '\n'; }
    return t + logic;
}

bool CustomRules::fromText(const std::string& text) {
    *this = CustomRules();
    std::string line;
    bool any = false;
    // out of range: the nearest bound; not a number (nan, inf): the default (the member's value from CustomRules())
    auto clampf_ = [](float v, float a, float b, float def) { return !std::isfinite(v) ? def : v < a ? a : v > b ? b : v; };
    int fileLine = 1;
    for (size_t at = 0; at < text.size();) {
        size_t nl = text.find('\n', at);
        line.assign(text, at, nl == std::string::npos ? std::string::npos : nl - at);
        if (isModeLogicLine(line)) { logic.assign(text, at, std::string::npos); logicLine = fileLine; break; }   // (the rest is the logic's)
        at = nl == std::string::npos ? text.size() : nl + 1;
        fileLine++;
        while (!line.empty() && line.back() == '\r') line.pop_back();   // (every \r: a kept line must read back the same)
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        float f = 0;   // (sscanf, not atof: the Windows CRT's atof reads "nan" / "inf" as 0, so a value meant to fall
        if (sscanf(v.c_str(), "%f", &f) != 1) f = 0;   //  back to the default was clamped instead, and differed from Linux)
        int i = atoi(v.c_str());
        if (readWeaponSetRulesLine(*this, k, v)) {}   // weaponset= / wsline= (weaponset.cpp)
        else if (k == "custommode") { name = safeName(v); any = true; }
        else if (k == "mode") { int m = modeFromName(v.c_str()); if (m >= 0 && m != MODE_TOURNEY) baseMode = m; }
        else if (k == "weapons") weapons = (uint16_t)(i & ((1 << WPN_COUNT) - 1));
        else if (k == "gravity") gravity = clampf_(f, 0.1f, 3, gravity);
        else if (k == "speed") moveSpeed = clampf_(f, 0.25f, 3, moveSpeed);
        else if (k == "jump") jump = clampf_(f, 0.25f, 3, jump);
        else if (k == "knockback") knockback = clampf_(f, 0, 5, knockback);
        else if (k.rfind("move_", 0) == 0) {
            bool known = false;
            for (int m = 0; m < MV_COUNT; m++) if (k.compare(5, std::string::npos, moveParamKey(m)) == 0) { move[m] = clampf_(f, 0.1f, 5, move[m]); known = true; }
            if (!known) extraKeys.push_back(line);   // (a movement constant a newer game or the physics editor added)
        }
        else if (k == "health") health = std::max(1, std::min(1000, i));
        else if (k == "dash") dash = i != 0;
        else if (k == "grapple") grapple = i != 0;
        else if (k == "selfdamage") selfDamage = i != 0;
        else if (k == "instagib") instagib = i != 0;
        else if (k == "vampire") vampire = i != 0;
        else if (k == "regen") regen = i != 0;
        else if (k == "timelimit") timeLimit = std::max(0, std::min(3600, i));
        else if (k == "scorelimit") scoreLimit = std::max(0, std::min(10000, i));
        else if (k == "rounds") roundsToWin = std::max(0, std::min(50, i));
        else if (k == "weapon") {
            int w = -1; float a = 1, b = 1, c = 1;
            if (sscanf(v.c_str(), "%d,%f,%f,%f", &w, &a, &b, &c) == 4 && w >= 0 && w < WPN_COUNT) {
                fireRate[w] = clampf_(a, 0.1f, 10, fireRate[w]); damage[w] = clampf_(b, 0, 10, damage[w]); projSpeed[w] = clampf_(c, 0.1f, 5, projSpeed[w]);
            }
        } else if (k == "maps") {
            maps.clear();
            std::istringstream ms(v);
            std::string m;
            // (a name with nothing safe in it is dropped, not kept as "": found by tests/fuzz/fuzz_rules)
            while (std::getline(ms, m, ',')) if (maps.size() < 32 && !(m = safeName(m)).empty()) maps.push_back(m);
        } else if (k == "label") {   // (name#hash8: shown, never trusted; the server recomputes it from the logic)
            label.clear();
            for (char c : v) if (label.size() < 80 && (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ' || c == '#')) label += c;
        } else extraKeys.push_back(line);   // a key this build doesn't know: kept verbatim for the next save
    }
    if (name.empty()) any = false;
    return any;
}

std::shared_ptr<const ModeLogic> compileCustomLogic(const CustomRules& r, std::string* err) {
    if (err) err->clear();
    if (r.logic.empty()) return nullptr;
    CustomRules c = r;
    c.label.clear();   // (the hash leaves label= out anyway)
    ModeDiag d;
    const std::string text = c.toText();
    auto L = compileModeLogic(text, c.baseMode, &d);
    if (!L && err) {
        *err = "the mode logic doesn't compile";
        if (!d.errors.empty()) {
            // (the canonical key lines aren't the file's: a line in the logic is told as the file's line, when known)
            int line = d.errors[0].line;
            int keyLines = (int)std::count(text.begin(), text.end() - (std::ptrdiff_t)c.logic.size(), '\n');
            if (r.logicLine > 0 && line > keyLines) line = line - keyLines - 1 + r.logicLine;
            *err += ": " + std::to_string(line) + ":" + std::to_string(d.errors[0].col) + " " + d.errors[0].msg;
        }
    }
    return L;
}

std::string customModesDir() { return userDataPath("custom_modes"); }

std::vector<std::string> listCustomModes() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(customModesDir(), ec))
        if (e.path().extension() == ".qmode") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

bool loadCustomMode(const std::string& name, CustomRules& out) {
    std::ifstream f(utf8Path(customModesDir() + "/" + safeName(name) + ".qmode"));
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    if (!out.fromText(ss.str())) return false;
    std::string why;   // weaponset=<name>: its file's text (no file / not valid: the standard weapons, and why)
    if (!resolveWeaponSet(out, &why) && !why.empty()) fprintf(stderr, "custom mode '%s': %s\n", out.name.c_str(), why.c_str());
    return true;
}

bool saveCustomMode(const CustomRules& r) {
    if (!r.active()) return false;
    std::error_code ec;
    std::filesystem::create_directories(customModesDir(), ec);
    CustomRules disk = r;
    disk.weaponSetText.clear();   // (the file names its weapon set; the set's text stays in custom_weapons/)
    return writeFileAtomic(customModesDir() + "/" + safeName(r.name) + ".qmode", disk.toText());
}

bool deleteCustomMode(const std::string& name) {
    std::error_code ec;
    return std::filesystem::remove(customModesDir() + "/" + safeName(name) + ".qmode", ec);
}
