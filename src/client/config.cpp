#include "config.h"
#include "devices.h"
#include "../common/u8file.h"
#include "../game/gamedefs.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "../game/map.h"

static std::string cfgPath() { return userDataPath("quark.cfg"); }

static Vec3 parseColor(const std::string& s, Vec3 def) {
    unsigned v;
    if (s.size() == 6 && sscanf(s.c_str(), "%x", &v) == 1)
        return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f};
    return def;
}
static std::string colorHex(const Vec3& c) {
    char b[8];
    snprintf(b, sizeof b, "%02x%02x%02x", (int)std::lround(clampf(c.x, 0, 1) * 255), (int)std::lround(clampf(c.y, 0, 1) * 255), (int)std::lround(clampf(c.z, 0, 1) * 255));   // (rounded: cut, 137 came back 136)
    return b;
}

void Settings::load() {
    std::ifstream f(utf8Path(cfgPath()));
    std::string line;
    bindVersion = 0;   // (a file from before bind_version: its binds get the migrations)
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        set(line.substr(0, eq), line.substr(eq + 1));
    }
    finishLoad();
}

// One setting from text (the config file's key=value, or the console's "key value").
void Settings::set(const std::string& k, const std::string& v) {
    {
        float fv = (float)atof(v.c_str());
        int iv = atoi(v.c_str());
        if (k == "name") name = v;
        else if (k == "sensitivity") { if (std::isfinite(fv)) sensitivity = clampf(fv, 0.01f, 50.0f); }   // (the settings menu's range)
        else if (k == "dpi") dpi = std::min(std::max(iv, 100), 32000);
        else if (k == "fov") { if (std::isfinite(fv)) fov = clampf(fv, 60, 140); }
        else if (k == "brightness") brightness = clampf(fv, 0.6f, 1.8f);
        else if (k == "vsync") presentMode = iv != 0 ? 1 : 0; // old setting
        else if (k == "display_mode") displayMode = std::min(std::max(iv, 0), 2);
        else if (k == "interp_ms") interpMs = std::min(std::max(iv, -1), kMaxInterpMs);
        else if (k == "present_mode") presentMode = std::min(std::max(iv, 0), 3);
        else if (k == "latent_offset_us") latentOffsetUs = std::min(std::max(iv, -4000), 4000);
        else if (k == "gpu_max_clocks") gpuMaxClocks = iv != 0;
        else if (k == "low_latency") lowLatency = std::min(std::max(iv, 0), 2);
        else if (k == "maxfps") maxFps = iv;
        else if (k == "fullscreen") { if (iv) displayMode = 1; }
        else if (k == "width") width = iv;
        else if (k == "height") height = iv;
        else if (k == "shadows") shadows = iv != 0;
        else if (k == "model") model = (iv >= 0 && iv < kPlayerModels) ? iv : 0;
        else if (k == "soundpack") soundPack = (iv >= 0 && iv < 5) ? iv : 0;
        else if (k == "name_color") { char* e = nullptr; long c = strtol(v.c_str(), &e, 16); nameColor = !v.empty() && v != "-1" && e && !*e && c >= 0 && c <= 0xFFFFFF ? (int)c : -1; }
        else if (k == "health_bars_117") hbMigrated = iv != 0;
        else if (k == "socd") socd = (iv >= 0 && iv <= 2) ? iv : 0;
        else if (k == "alt_toggle") altToggle = iv != 0;
        else if (k == "hud_verbose") hudVerbose = iv != 0;
        else if (k == "hud_widgets") hudWidgets = iv != 0;
        else if (k.rfind("mode_hud_", 0) == 0) { int m = modeFromName(k.c_str() + 9); if (m >= 0 && (v.empty() || plainFileName(v))) modeHud[m] = v; }   // (a file in huds/)
        else if (k.rfind("mode_controls_", 0) == 0) { int m = modeFromName(k.c_str() + 14); if (m >= 0 && (v.empty() || plainFileName(v))) modeCtl[m] = v; }
        else if (k == "msaa") msaa = (iv == 1 || iv == 2 || iv == 4) ? iv : 0;
        else if (k == "volume") volume = clampf(fv, 0, 1);
        else if (k == "volume_effects") { if (std::isfinite(fv)) volumeEffects = clampf(fv, 0, 1); }
        else if (k == "volume_announcer") { if (std::isfinite(fv)) volumeAnnouncer = clampf(fv, 0, 1); }
        else if (k == "volume_quips") { if (std::isfinite(fv)) volumeQuips = clampf(fv, 0, 1); }
        else if (k == "quips") quips = iv != 0;
        else if (k == "audio_output") audioOutput = deviceCfgValue(v);
        else if (k == "audio_input") audioInput = deviceCfgValue(v);
        else if (k == "monitor") monitor = deviceCfgValue(v);
        else if (k == "fullscreen_hz") fullscreenHz = std::min(std::max(iv, 0), 1000);
        else if (k == "hitsounds") hitsounds = iv != 0;
        else if (k == "exclusive_audio") exclusiveAudio = iv != 0;
        else if (k == "fullbright") fullbright = iv != 0;
        else if (k == "own_shots") ownShots = std::min(std::max(iv, 0), 2);
        else if (k == "enemy_shots") enemyShots = iv != 0;
        else if (k == "impact_sparks") impactSparks = iv != 0;
        else if (k == "muzzle_flash") muzzleFlash = iv != 0;
        else if (k == "health_bars") healthBars = iv != 0;
        else if (k == "health_bars_bottom") healthBarsBottom = iv != 0;
        else if (k == "scope_opacity") scopeOpacity = clampf(fv, 0, 1);
        else if (k == "sticky_counter") stickyCounter = iv != 0;
        else if (k == "crowns") crowns = iv != 0;
        else if (k == "chat_off") chatOff = iv != 0;
        else if (k == "dmg_nums") dmgNums = iv != 0;
        else if (k == "dmg_num_scale") { if (std::isfinite(fv)) dmgNumScale = clampf(fv, 0.5f, 3.0f); }
        else if (k == "damage_flash") { if (std::isfinite(fv)) damageFlash = clampf(fv, 0, 1); }
        else if (k == "zoom_fov_sniper") { if (std::isfinite(fv)) zoomFovSniper = clampf(fv, 10, 140); }
        else if (k == "zoom_fov_rifle") { if (std::isfinite(fv)) zoomFovRifle = clampf(fv, 10, 140); }
        else if (k == "fullbright_color") fullbrightColor = parseColor(v, fullbrightColor);
        else if (k == "discord") discordName = v.substr(0, 40);
        else if (k == "ui_font") {   // (Settings > FONT: a built-in's id or a mods/fonts file name)
            std::string f = v.substr(0, 128);
            while (!f.empty() && (f.back() == '\r' || f.back() == ' ')) f.pop_back();
            uiFont = f.empty() ? "rubik" : f;
        }
        else if (k == "demo_full_rate") demoFullRate = iv != 0;
        else if (k == "outline_friendly") outlineFriendly = iv;
        else if (k == "outline_enemy") outlineEnemy = iv;
        else if (k == "outline_player_width") outlinePlayer = clampf(fv, 0, 6);
        else if (k == "outline_world_width") outlineWorld = clampf(fv, 0, 6);
        else if (k == "voice") announcerVoice = iv >= 0 && iv < 7 ? iv : 0;
        else if (k == "announcer") announcer = iv < 0 ? 0 : iv > 2 ? 2 : iv;
        else if (k == "shadowres") shadowRes = std::min(std::max(iv, 256), 8192);   // (a square depth texture: 8192 is 256 MB)
        else if (k == "color_friendly") colorFriendly = parseColor(v, colorFriendly);
        else if (k == "color_enemy") colorEnemy = parseColor(v, colorEnemy);
        else if (k == "ch_style") chStyle = std::min(std::max(iv, 0), 4);
        else if (k == "ch_length") chLength = clampf(fv, 0, 30);
        else if (k == "ch_thickness") chThickness = clampf(fv, 1, 8);
        else if (k == "ch_gap") chGap = clampf(fv, 0, 20);
        else if (k == "ch_outline") chOutline = iv != 0;
        else if (k == "ch_team_color") chTeamColor = iv != 0;
        else if (k == "ch_color") chColor = parseColor(v, chColor);
        else if (k == "ch_hit_flash") chHitFlash = iv != 0;
        else if (k == "ch_hit_color") chHitColor = parseColor(v, chHitColor);
        else if (k == "last_address") lastAddress = v;
        else if (k == "rank_hello") rankHello = v;
        else if (k == "host_name") hostName = v;
        else if (k == "host_prober") hostProber = v;
        else if (k == "host_map") hostMap = v;
        else if (k == "host_mode") hostMode = iv;
        else if (k == "host_port") hostPort = iv;
        else if (k == "host_max") hostMaxPlayers = iv;
        else if (k == "host_upnp") hostUpnp = iv != 0;
        else if (k == "host_balance_bots") hostBalanceBots = iv != 0;
        else if (k == "host_public") hostPublic = iv != 0;
        else if (k == "host_ranked") hostRanked = iv != 0;
        else if (k == "host_custom") hostCustom = v.substr(0, 40);
        else if (k == "host_bot_brain") {   // (a brain name loadBrain takes, else the built-in brain)
            hostBotBrain = v.substr(0, 64);
            for (char ch : hostBotBrain) if (!(isalnum((unsigned char)ch) || ch == '_' || ch == '-')) { hostBotBrain.clear(); break; }
            if (hostBotBrain == "default") hostBotBrain.clear();
        }
        else if (k == "host_variant") hostArenaVariant = std::min(std::max(iv, 0), kArenaVariants - 1);
        else if (k == "bot_variant") botArenaVariant = std::min(std::max(iv, 0), kArenaVariants - 1);
        else if (k == "host_duel_size") hostDuelSize = iv == 2 ? 2 : 1;
        else if (k == "host_log") hostLog = std::min(std::max(iv, 0), 3);
        else if (k == "bot_skill") botSkill = std::min(std::max(iv, 0), 3);
        else if (k == "bot_enemies") botEnemies = std::min(std::max(iv, 1), 7);
        else if (k == "bot_allies") botAllies = std::min(std::max(iv, 0), 3);
        else if (k == "bot_mode") botMode = std::min(std::max(iv, 0), MODE_COUNT - 1);   // (the vs-bots picker offers every mode)
        else if (k.rfind("hud_", 0) == 0) {
            int i = atoi(k.c_str() + 4), c = 0, hid = 0; HudElem e;
            // (the tilt, pitch / yaw / roll, was added at the end: an older line without it loads untilted)
            if (i >= 0 && i < HUD_COUNT && sscanf(v.c_str(), "%f,%f,%f,%d,%f,%f,%f,%d,%f,%f,%f,%f", &e.x, &e.y, &e.scale, &c, &e.color.x, &e.color.y, &e.color.z, &hid, &e.z,
                                                  &e.pitch, &e.yaw, &e.roll) >= 7) {
                e.x = clampf(e.x, -1280, 1280); e.y = clampf(e.y, -720, 720); e.scale = clampf(e.scale, 0.5f, 3); e.custom = c != 0;
                e.hidden = hid != 0; e.z = clampf(e.z, -300, 300);
                e.pitch = std::isfinite(e.pitch) ? clampf(e.pitch, -90, 90) : 0; e.yaw = std::isfinite(e.yaw) ? clampf(e.yaw, -90, 90) : 0;
                e.roll = std::isfinite(e.roll) ? clampf(e.roll, -90, 90) : 0;
                hud[i] = e;
            }
        }
        else if (k == "bind_version") bindVersion = iv;
        else if (k.rfind("bind_", 0) == 0) {
            int a = atoi(k.c_str() + 5), c0 = 0, c1 = 0;
            if (a >= 0 && a < A_COUNT && sscanf(v.c_str(), "%d,%d", &c0, &c1) >= 1) { binds.code[a][0] = c0; binds.code[a][1] = c1; }
        }
    }
}

void Settings::finishLoad() {
    if (bindVersion < 2) {   // binds from an older config (see bindVersion); from now on they're this version's
        // slots 9 and 10 (knife, syringe) came later: unbound in an older config, they get keys 9 and 0
        if (!binds.code[A_WEAPON9][0] && !binds.code[A_WEAPON9][1]) binds.code[A_WEAPON9][0] = 57;    // (GLFW 9)
        if (!binds.code[A_WEAPON10][0] && !binds.code[A_WEAPON10][1]) binds.code[A_WEAPON10][0] = 48;   // (GLFW 0)
        if (!binds.code[A_QUIP][0] && !binds.code[A_QUIP][1]) binds.code[A_QUIP][0] = 71;               // (GLFW G; added 1.12)
        // old default chat keys (T / Y) move to the new default, Enter; a key someone picked themselves stays
        if (binds.code[A_CHAT][0] == 84 && binds.code[A_CHAT][1] == 89) { binds.code[A_CHAT][0] = 257; binds.code[A_CHAT][1] = 335; } // GLFW T, Y -> ENTER, KP_ENTER
        bindVersion = 2;
    }
    if (bindVersion < 3) {   // F4 became LATENCY (the mouse to photon number): VOTE START, if still on F4, moves to F5
        for (int& c : binds.code[A_VOTESTART]) if (c == 293) c = 294;   // (GLFW F4 -> F5)
        bool f4Used = false;
        for (int a = 0; a < A_COUNT; a++) if (a != A_PERFMINI && binds.has(a, 293)) f4Used = true;
        binds.code[A_PERFMINI][0] = f4Used ? 0 : 293;
        binds.code[A_PERFMINI][1] = 0;
        bindVersion = 3;
    }
    if (!hbMigrated) { healthBars = true; hbMigrated = true; }   // 1.17 turned health bars on by default: on once for everyone (owner), off again stays off
    if (width < 320) width = 1280;
    if (height < 240) height = 720;
}

void Settings::save() const {
    writeFileAtomic(cfgPath(), toText());   // (whole or not at all: a crash mid-write used to lose every bind)
}

std::string Settings::toText() const {
    std::ostringstream f;
    f << "name=" << name << "\nsensitivity=" << sensitivity << "\ndpi=" << dpi << "\nfov=" << fov << "\nbrightness=" << brightness << "\ndisplay_mode=" << displayMode
      << "\npresent_mode=" << presentMode << "\nlatent_offset_us=" << latentOffsetUs << "\ngpu_max_clocks=" << gpuMaxClocks << "\nlow_latency=" << lowLatency << "\ninterp_ms=" << interpMs
      << "\nmaxfps=" << maxFps << "\nwidth=" << width << "\nheight=" << height
      << "\nshadows=" << shadows << "\nmsaa=" << msaa << "\nmodel=" << model << "\nsoundpack=" << soundPack << "\nname_color=" << nameColorText() << "\nhealth_bars_117=1" << "\nsocd=" << socd << "\nalt_toggle=" << altToggle
      << "\nhud_verbose=" << hudVerbose << "\nhud_widgets=" << hudWidgets << "\nshadowres=" << shadowRes << "\nvolume=" << volume << "\nvolume_effects=" << volumeEffects << "\nvolume_announcer=" << volumeAnnouncer << "\nquips=" << quips << "\nvolume_quips=" << volumeQuips << "\nhitsounds=" << hitsounds << "\nexclusive_audio=" << exclusiveAudio
      << "\naudio_output=" << audioOutput << "\naudio_input=" << audioInput << "\nmonitor=" << monitor << "\nfullscreen_hz=" << fullscreenHz << "\nannouncer=" << announcer << "\nvoice=" << announcerVoice << "\nfullbright=" << fullbright << "\nown_shots=" << ownShots << "\nenemy_shots=" << enemyShots << "\nimpact_sparks=" << impactSparks << "\nmuzzle_flash=" << muzzleFlash << "\nhealth_bars=" << healthBars << "\nhealth_bars_bottom=" << healthBarsBottom
      << "\nscope_opacity=" << scopeOpacity << "\nsticky_counter=" << stickyCounter << "\ncrowns=" << crowns << "\nchat_off=" << chatOff << "\ndmg_nums=" << dmgNums << "\ndmg_num_scale=" << dmgNumScale << "\ndamage_flash=" << damageFlash << "\nzoom_fov_sniper=" << zoomFovSniper << "\nzoom_fov_rifle=" << zoomFovRifle << "\nfullbright_color=" << colorHex(fullbrightColor) << "\ndiscord=" << discordName << "\nui_font=" << uiFont << "\ndemo_full_rate=" << demoFullRate << "\noutline_friendly=" << outlineFriendly << "\noutline_enemy=" << outlineEnemy << "\noutline_player_width=" << outlinePlayer << "\noutline_world_width=" << outlineWorld
      << "\ncolor_friendly=" << colorHex(colorFriendly) << "\ncolor_enemy=" << colorHex(colorEnemy)
      << "\nch_style=" << chStyle << "\nch_length=" << chLength << "\nch_thickness=" << chThickness << "\nch_gap=" << chGap
      << "\nch_outline=" << chOutline << "\nch_team_color=" << chTeamColor << "\nch_color=" << colorHex(chColor)
      << "\nch_hit_flash=" << chHitFlash << "\nch_hit_color=" << colorHex(chHitColor)
      << "\nlast_address=" << lastAddress << "\nrank_hello=" << rankHello << "\nhost_name=" << hostName << "\nhost_prober=" << hostProber << "\nhost_map=" << hostMap
      << "\nhost_mode=" << hostMode << "\nhost_port=" << hostPort << "\nhost_max=" << hostMaxPlayers
      << "\nhost_upnp=" << hostUpnp << "\nhost_balance_bots=" << hostBalanceBots << "\nhost_log=" << hostLog << "\nhost_duel_size=" << hostDuelSize << "\nhost_public=" << hostPublic << "\nhost_ranked=" << hostRanked << "\nhost_custom=" << hostCustom << "\nhost_bot_brain=" << hostBotBrain << "\nhost_variant=" << hostArenaVariant << "\nbot_variant=" << botArenaVariant << "\nbot_skill=" << botSkill
      << "\nbot_enemies=" << botEnemies << "\nbot_allies=" << botAllies << "\nbot_mode=" << botMode << "\n";
    for (int i = 0; i < HUD_COUNT; i++)
        f << "hud_" << i << "=" << hud[i].x << "," << hud[i].y << "," << hud[i].scale << "," << hud[i].custom << ","
          << hud[i].color.x << "," << hud[i].color.y << "," << hud[i].color.z << "," << hud[i].hidden << "," << hud[i].z << ","
          << hud[i].pitch << "," << hud[i].yaw << "," << hud[i].roll << "\n";
    for (int m = 0; m < MODE_COUNT; m++) {
        if (!modeHud[m].empty()) f << "mode_hud_" << modeName(m) << "=" << modeHud[m] << "\n";
        if (!modeCtl[m].empty()) f << "mode_controls_" << modeName(m) << "=" << modeCtl[m] << "\n";
    }
    f << "bind_version=3\n";
    for (int a = 0; a < A_COUNT; a++) f << "bind_" << a << "=" << binds.code[a][0] << "," << binds.code[a][1] << "\n";
    return f.str();
}
