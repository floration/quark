#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "../game/map.h"

static std::string cfgPath() { return findDataPath("maps") + "/../quark.cfg"; }

static Vec3 parseColor(const std::string& s, Vec3 def) {
    unsigned v;
    if (s.size() == 6 && sscanf(s.c_str(), "%x", &v) == 1)
        return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f};
    return def;
}
static std::string colorHex(const Vec3& c) {
    char b[8];
    snprintf(b, sizeof b, "%02x%02x%02x", (int)(clampf(c.x, 0, 1) * 255), (int)(clampf(c.y, 0, 1) * 255), (int)(clampf(c.z, 0, 1) * 255));
    return b;
}

void Settings::load() {
    std::ifstream f(cfgPath());
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        float fv = (float)atof(v.c_str());
        int iv = atoi(v.c_str());
        if (k == "name") name = v;
        else if (k == "sensitivity") sensitivity = fv;
        else if (k == "fov") fov = clampf(fv, 60, 140);
        else if (k == "vsync") presentMode = iv != 0 ? 1 : 0; // old setting
        else if (k == "display_mode") displayMode = std::min(std::max(iv, 0), 2);
        else if (k == "present_mode") presentMode = std::min(std::max(iv, 0), 2);
        else if (k == "low_latency") lowLatency = std::min(std::max(iv, 0), 2);
        else if (k == "maxfps") maxFps = iv;
        else if (k == "fullscreen") { if (iv) displayMode = 1; }
        else if (k == "width") width = iv;
        else if (k == "height") height = iv;
        else if (k == "shadows") shadows = iv != 0;
        else if (k == "shadowres") shadowRes = iv;
        else if (k == "color_friendly") colorFriendly = parseColor(v, colorFriendly);
        else if (k == "color_enemy") colorEnemy = parseColor(v, colorEnemy);
        else if (k == "last_address") lastAddress = v;
        else if (k == "host_name") hostName = v;
        else if (k == "host_map") hostMap = v;
        else if (k == "host_mode") hostMode = iv;
        else if (k == "host_port") hostPort = iv;
        else if (k == "host_max") hostMaxPlayers = iv;
        else if (k == "host_upnp") hostUpnp = iv != 0;
        else if (k == "host_balance_bots") hostBalanceBots = iv != 0;
        else if (k == "bot_skill") botSkill = std::min(std::max(iv, 0), 3);
        else if (k == "bot_enemies") botEnemies = std::min(std::max(iv, 1), 7);
        else if (k == "bot_allies") botAllies = std::min(std::max(iv, 0), 3);
        else if (k == "bot_mode") botMode = std::min(std::max(iv, 0), 5);
    }
    if (width < 320) width = 1280;
    if (height < 240) height = 720;
}

void Settings::save() const {
    std::ofstream f(cfgPath());
    f << "name=" << name << "\nsensitivity=" << sensitivity << "\nfov=" << fov << "\ndisplay_mode=" << displayMode
      << "\npresent_mode=" << presentMode << "\nlow_latency=" << lowLatency
      << "\nmaxfps=" << maxFps << "\nwidth=" << width << "\nheight=" << height
      << "\nshadows=" << shadows << "\nshadowres=" << shadowRes
      << "\ncolor_friendly=" << colorHex(colorFriendly) << "\ncolor_enemy=" << colorHex(colorEnemy)
      << "\nlast_address=" << lastAddress << "\nhost_name=" << hostName << "\nhost_map=" << hostMap
      << "\nhost_mode=" << hostMode << "\nhost_port=" << hostPort << "\nhost_max=" << hostMaxPlayers
      << "\nhost_upnp=" << hostUpnp << "\nhost_balance_bots=" << hostBalanceBots << "\nbot_skill=" << botSkill
      << "\nbot_enemies=" << botEnemies << "\nbot_allies=" << botAllies << "\nbot_mode=" << botMode << "\n";
}
