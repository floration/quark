#include "config.h"
#include "../game/gamedefs.h"
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
        else if (k == "dpi") dpi = std::min(std::max(iv, 100), 32000);
        else if (k == "fov") fov = clampf(fv, 60, 140);
        else if (k == "vsync") presentMode = iv != 0 ? 1 : 0; // old setting
        else if (k == "display_mode") displayMode = std::min(std::max(iv, 0), 2);
        else if (k == "interp_ms") interpMs = std::min(std::max(iv, 0), 200);
        else if (k == "present_mode") presentMode = std::min(std::max(iv, 0), 2);
        else if (k == "low_latency") lowLatency = std::min(std::max(iv, 0), 2);
        else if (k == "maxfps") maxFps = iv;
        else if (k == "fullscreen") { if (iv) displayMode = 1; }
        else if (k == "width") width = iv;
        else if (k == "height") height = iv;
        else if (k == "shadows") shadows = iv != 0;
        else if (k == "model") model = (iv >= 0 && iv < kPlayerModels) ? iv : 0;
        else if (k == "soundpack") soundPack = (iv >= 0 && iv < 5) ? iv : 0;
        else if (k == "socd") socd = (iv >= 0 && iv <= 2) ? iv : 0;
        else if (k == "alt_toggle") altToggle = iv != 0;
        else if (k == "hud_verbose") hudVerbose = iv != 0;
        else if (k == "hud_widgets") hudWidgets = iv != 0;
        else if (k.rfind("mode_hud_", 0) == 0) { int m = modeFromName(k.c_str() + 9); if (m >= 0) modeHud[m] = v; }
        else if (k.rfind("mode_controls_", 0) == 0) { int m = modeFromName(k.c_str() + 14); if (m >= 0) modeCtl[m] = v; }
        else if (k == "msaa") msaa = (iv == 1 || iv == 2 || iv == 4) ? iv : 0;
        else if (k == "volume") volume = clampf(fv, 0, 1);
        else if (k == "hitsounds") hitsounds = iv != 0;
        else if (k == "shadowres") shadowRes = iv;
        else if (k == "color_friendly") colorFriendly = parseColor(v, colorFriendly);
        else if (k == "color_enemy") colorEnemy = parseColor(v, colorEnemy);
        else if (k == "ch_style") chStyle = std::min(std::max(iv, 0), 4);
        else if (k == "ch_length") chLength = clampf(fv, 0, 30);
        else if (k == "ch_thickness") chThickness = clampf(fv, 1, 8);
        else if (k == "ch_gap") chGap = clampf(fv, 0, 20);
        else if (k == "ch_outline") chOutline = iv != 0;
        else if (k == "ch_team_color") chTeamColor = iv != 0;
        else if (k == "ch_color") chColor = parseColor(v, chColor);
        else if (k == "last_address") lastAddress = v;
        else if (k == "host_name") hostName = v;
        else if (k == "host_map") hostMap = v;
        else if (k == "host_mode") hostMode = iv;
        else if (k == "host_port") hostPort = iv;
        else if (k == "host_max") hostMaxPlayers = iv;
        else if (k == "host_upnp") hostUpnp = iv != 0;
        else if (k == "host_balance_bots") hostBalanceBots = iv != 0;
        else if (k == "host_public") hostPublic = iv != 0;
        else if (k == "host_ranked") hostRanked = iv != 0;
        else if (k == "host_variant") hostArenaVariant = std::min(std::max(iv, 0), kArenaVariants - 1);
        else if (k == "bot_variant") botArenaVariant = std::min(std::max(iv, 0), kArenaVariants - 1);
        else if (k == "host_duel_size") hostDuelSize = iv == 2 ? 2 : 1;
        else if (k == "host_log") hostLog = std::min(std::max(iv, 0), 3);
        else if (k == "bot_skill") botSkill = std::min(std::max(iv, 0), 3);
        else if (k == "bot_enemies") botEnemies = std::min(std::max(iv, 1), 7);
        else if (k == "bot_allies") botAllies = std::min(std::max(iv, 0), 3);
        else if (k == "bot_mode") botMode = std::min(std::max(iv, 0), 5);
        else if (k.rfind("hud_", 0) == 0) {
            int i = atoi(k.c_str() + 4), c = 0; HudElem e;
            if (i >= 0 && i < HUD_COUNT && sscanf(v.c_str(), "%f,%f,%f,%d,%f,%f,%f", &e.x, &e.y, &e.scale, &c, &e.color.x, &e.color.y, &e.color.z) == 7) {
                e.x = clampf(e.x, -1280, 1280); e.y = clampf(e.y, -720, 720); e.scale = clampf(e.scale, 0.5f, 3); e.custom = c != 0;
                hud[i] = e;
            }
        }
        else if (k.rfind("bind_", 0) == 0) {
            int a = atoi(k.c_str() + 5), c0 = 0, c1 = 0;
            if (a >= 0 && a < A_COUNT && sscanf(v.c_str(), "%d,%d", &c0, &c1) >= 1) { binds.code[a][0] = c0; binds.code[a][1] = c1; }
        }
    }
    // old default chat keys (T / Y) move to the new default, Enter; a key someone picked themselves stays
    if (binds.code[A_CHAT][0] == 84 && binds.code[A_CHAT][1] == 89) { binds.code[A_CHAT][0] = 257; binds.code[A_CHAT][1] = 335; } // GLFW T, Y -> ENTER, KP_ENTER
    if (width < 320) width = 1280;
    if (height < 240) height = 720;
}

void Settings::save() const {
    std::ofstream f(cfgPath());
    f << "name=" << name << "\nsensitivity=" << sensitivity << "\ndpi=" << dpi << "\nfov=" << fov << "\ndisplay_mode=" << displayMode
      << "\npresent_mode=" << presentMode << "\nlow_latency=" << lowLatency << "\ninterp_ms=" << interpMs
      << "\nmaxfps=" << maxFps << "\nwidth=" << width << "\nheight=" << height
      << "\nshadows=" << shadows << "\nmsaa=" << msaa << "\nmodel=" << model << "\nsoundpack=" << soundPack << "\nsocd=" << socd << "\nalt_toggle=" << altToggle
      << "\nhud_verbose=" << hudVerbose << "\nhud_widgets=" << hudWidgets << "\nshadowres=" << shadowRes << "\nvolume=" << volume << "\nhitsounds=" << hitsounds
      << "\ncolor_friendly=" << colorHex(colorFriendly) << "\ncolor_enemy=" << colorHex(colorEnemy)
      << "\nch_style=" << chStyle << "\nch_length=" << chLength << "\nch_thickness=" << chThickness << "\nch_gap=" << chGap
      << "\nch_outline=" << chOutline << "\nch_team_color=" << chTeamColor << "\nch_color=" << colorHex(chColor)
      << "\nlast_address=" << lastAddress << "\nhost_name=" << hostName << "\nhost_map=" << hostMap
      << "\nhost_mode=" << hostMode << "\nhost_port=" << hostPort << "\nhost_max=" << hostMaxPlayers
      << "\nhost_upnp=" << hostUpnp << "\nhost_balance_bots=" << hostBalanceBots << "\nhost_log=" << hostLog << "\nhost_duel_size=" << hostDuelSize << "\nhost_public=" << hostPublic << "\nhost_ranked=" << hostRanked << "\nhost_variant=" << hostArenaVariant << "\nbot_variant=" << botArenaVariant << "\nbot_skill=" << botSkill
      << "\nbot_enemies=" << botEnemies << "\nbot_allies=" << botAllies << "\nbot_mode=" << botMode << "\n";
    for (int i = 0; i < HUD_COUNT; i++)
        f << "hud_" << i << "=" << hud[i].x << "," << hud[i].y << "," << hud[i].scale << "," << hud[i].custom << ","
          << hud[i].color.x << "," << hud[i].color.y << "," << hud[i].color.z << "\n";
    for (int m = 0; m < MODE_COUNT; m++) {
        if (!modeHud[m].empty()) f << "mode_hud_" << modeName(m) << "=" << modeHud[m] << "\n";
        if (!modeCtl[m].empty()) f << "mode_controls_" << modeName(m) << "=" << modeCtl[m] << "\n";
    }
    for (int a = 0; a < A_COUNT; a++) f << "bind_" << a << "=" << binds.code[a][0] << "," << binds.code[a][1] << "\n";
}
