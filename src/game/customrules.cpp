#include <cmath>
#include "customrules.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "map.h"

namespace {
thread_local CustomRules g_rules;   // per thread: a hosted server thread and the client each play by their own copy
thread_local WeaponDef g_weapons[WPN_COUNT];
thread_local MoveTuning g_move;
thread_local bool g_init = false;

void rebuild() {
    for (int w = 0; w < WPN_COUNT; w++) {
        WeaponDef d = baseWeaponDef(w);
        if (g_rules.active()) {
            d.refire /= std::max(0.05f, g_rules.fireRate[w]);
            d.damage *= g_rules.damage[w];
            d.projSpeed *= g_rules.projSpeed[w];
        }
        g_weapons[w] = d;
    }
    g_move = MoveTuning();
    if (g_rules.active()) {
        g_move.gravity = g_rules.gravity; g_move.speed = g_rules.moveSpeed; g_move.jump = g_rules.jump;
        g_move.dash = g_rules.dash; g_move.grapple = g_rules.grapple;
        for (int k = 0; k < MV_COUNT; k++) g_move.m[k] = g_rules.move[k];
    }
    g_init = true;
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

const WeaponDef& weaponDef(int w) {
    if (!g_init) rebuild();
    return g_weapons[w < 0 || w >= WPN_COUNT ? 0 : w];
}
float tunedDamage(int w) { return g_rules.active() && w >= 0 && w < WPN_COUNT ? g_rules.damage[w] : 1.0f; }
const MoveTuning& moveTuning() { if (!g_init) rebuild(); return g_move; }
const CustomRules& activeRules() { return g_rules; }
void setActiveRules(const CustomRules& r) { g_rules = r; rebuild(); }

std::string CustomRules::toText() const {
    std::ostringstream o;
    o << "custommode=" << name << "\nmode=" << modeName(baseMode) << "\nweapons=" << weapons
      << "\ngravity=" << gravity << "\nspeed=" << moveSpeed << "\njump=" << jump << "\nknockback=" << knockback
      << "\nhealth=" << health << "\ndash=" << dash << "\ngrapple=" << grapple << "\nselfdamage=" << selfDamage
      << "\ninstagib=" << instagib << "\nvampire=" << vampire << "\nregen=" << regen
      << "\ntimelimit=" << timeLimit << "\nscorelimit=" << scoreLimit << "\nrounds=" << roundsToWin;
    for (int k = 0; k < MV_COUNT; k++) if (move[k] != 1) o << "\nmove_" << moveParamKey(k) << "=" << move[k];   // (only the changed ones)
    for (int w = 0; w < WPN_COUNT; w++)
        o << "\nweapon=" << w << "," << fireRate[w] << "," << damage[w] << "," << projSpeed[w];
    o << "\nmaps=";
    for (size_t i = 0; i < maps.size(); i++) o << (i ? "," : "") << maps[i];
    o << "\n";
    return o.str();
}

bool CustomRules::fromText(const std::string& text) {
    *this = CustomRules();
    std::istringstream in(text);
    std::string line;
    bool any = false;
    // out of range: the nearest bound; not a number (nan, inf): the default (the member's value from CustomRules())
    auto clampf_ = [](float v, float a, float b, float def) { return !std::isfinite(v) ? def : v < a ? a : v > b ? b : v; };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        float f = 0;   // (sscanf, not atof: the Windows CRT's atof reads "nan" / "inf" as 0, so a value meant to fall
        if (sscanf(v.c_str(), "%f", &f) != 1) f = 0;   //  back to the default was clamped instead, and differed from Linux)
        int i = atoi(v.c_str());
        if (k == "custommode") { name = safeName(v); any = true; }
        else if (k == "mode") { int m = modeFromName(v.c_str()); if (m >= 0 && m != MODE_TOURNEY) baseMode = m; }
        else if (k == "weapons") weapons = (uint16_t)(i & ((1 << WPN_COUNT) - 1));
        else if (k == "gravity") gravity = clampf_(f, 0.1f, 3, gravity);
        else if (k == "speed") moveSpeed = clampf_(f, 0.25f, 3, moveSpeed);
        else if (k == "jump") jump = clampf_(f, 0.25f, 3, jump);
        else if (k == "knockback") knockback = clampf_(f, 0, 5, knockback);
        else if (k.rfind("move_", 0) == 0) { for (int m = 0; m < MV_COUNT; m++) if (k.compare(5, std::string::npos, moveParamKey(m)) == 0) move[m] = clampf_(f, 0.1f, 5, move[m]); }
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
            while (std::getline(ms, m, ',')) if (!m.empty() && maps.size() < 32) maps.push_back(safeName(m));
        }
    }
    if (name.empty()) any = false;
    return any;
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
    std::ifstream f(customModesDir() + "/" + safeName(name) + ".qmode");
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return out.fromText(ss.str());
}

bool saveCustomMode(const CustomRules& r) {
    if (!r.active()) return false;
    std::error_code ec;
    std::filesystem::create_directories(customModesDir(), ec);
    return writeFileAtomic(customModesDir() + "/" + safeName(r.name) + ".qmode", r.toText());
}

bool deleteCustomMode(const std::string& name) {
    std::error_code ec;
    return std::filesystem::remove(customModesDir() + "/" + safeName(name) + ".qmode", ec);
}
