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
#include <vector>
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
    if (setBatch3(k, v)) return;
    {
        float fv = (float)atof(v.c_str());
        int iv = atoi(v.c_str());
        if (k == "name") name = v;
        else if (k == "sensitivity") { if (std::isfinite(fv)) sensitivity = clampf(fv, 0.01f, 50.0f); }   // (the settings menu's range)
        else if (k == "dpi") dpi = std::min(std::max(iv, 100), 32000);
        else if (k == "sens_y") { if (std::isfinite(fv)) sensY = clampf(fv, 0.1f, 10.0f); }
        else if (k == "invert_y") invertY = iv != 0;
        else if (k == "zoom_sens") { if (std::isfinite(fv)) zoomSens = clampf(fv, 0.1f, 10.0f); }
        else if (k == "score_toggle") scoreToggle = iv != 0;
        else if (k == "browser_sort") browserSort = std::min(std::max(iv, 0), 2);
        else if (k == "browser_hide_full") browserHideFull = iv != 0;
        else if (k == "browser_hide_empty") browserHideEmpty = iv != 0;
        else if (k == "browser_max_ping") browserMaxPing = std::min(std::max(iv, 0), 1000);
        else if (k == "fav_servers") favServers = v;
        else if (k == "recent_servers") recentServers = v;
        else if (k == "hit_volume") { if (std::isfinite(fv)) hitVolume = clampf(fv, 0, 2); }
        else if (k == "hit_pack") hitPack = std::min(std::max(iv, -1), 4);
        else if (k == "headshot_sound") headshotSound = iv != 0;
        else if (k == "kill_sound") killSound = iv != 0;
        else if (k == "hud_size") { if (std::isfinite(fv)) hudSize = clampf(fv, 0.5f, 2.0f); }
        else if (k == "feed_time") feedTime = std::min(std::max(iv, 1), 60);
        else if (k == "feed_lines") feedLines = std::min(std::max(iv, 1), 24);
        else if (k == "feed_mine") feedMine = iv != 0;
        else if (k == "chat_time") chatTime = std::min(std::max(iv, 1), 120);
        else if (k == "chat_lines") chatLines = std::min(std::max(iv, 1), 30);
        else if (k == "chat_opacity") { if (std::isfinite(fv)) chatOpacity = clampf(fv, 0.1f, 1.0f); }
        else if (k == "show_fps") showFps = iv != 0;
        else if (k == "show_ping") showPing = iv != 0;
        else if (k == "hide_hud_crosshair") hideHudCrosshair = iv != 0;
        else if (k == "fog") { if (std::isfinite(fv)) fog = clampf(fv, 0, 3); }
        else if (k == "gamma") { if (std::isfinite(fv)) gamma = clampf(fv, 0.5f, 2.0f); }
        else if (k == "contrast") { if (std::isfinite(fv)) contrast = clampf(fv, 0.5f, 2.0f); }
        else if (k == "saturation") { if (std::isfinite(fv)) saturation = clampf(fv, 0, 2.0f); }
        else if (k == "cpu_priority") cpuPriority = iv == 2 ? 2 : 1;
        else if (k == "scroll_wrap") scrollWrap = iv != 0;
        else if (k == "scroll_skip") scrollSkip = iv;
        else if (k == "auto_iconify") autoIconify = iv != 0;
        else if (k == "remember_tab") rememberTab = iv != 0;
        else if (k == "last_tab") lastTab = std::min(std::max(iv, 0), 6);   // (6: SEARCH)
        else if (k == "confirm_leave") confirmLeave = iv != 0;
        else if (k == "vrr") vrr = iv != 0;
        else if (k == "captions") captions = iv != 0;
        else if (k == "hp_scaled") hpScaled = iv != 0;
        else if (k == "vol_steps") { if (std::isfinite(fv)) volSteps = clampf(fv, 0, 1); }
        else if (k == "vol_others_steps") { if (std::isfinite(fv)) volOthersSteps = clampf(fv, 0, 2); }
        else if (k == "vol_objective") { if (std::isfinite(fv)) volObjective = clampf(fv, 0, 1); }
        else if (k == "vol_weapons") { if (std::isfinite(fv)) volWeapons = clampf(fv, 0, 1); }
        else if (k == "enemy_model") enemyModel = iv >= 0 && iv < kPlayerModels ? iv : -1;
        else if (k == "grapple_toggle") grappleToggle = iv != 0;
        else if (k == "left_handed") leftHanded = iv != 0;
        else if (k == "auto_reconnect") autoReconnect = iv != 0;
        else if (k == "queue_flash") queueFlash = iv != 0;
        else if (k == "ghost") ghost = iv != 0;
        else if (k == "team_panel") teamPanel = iv != 0;
        else if (k == "match_summary") matchSummary = iv != 0;
        else if (k.rfind("cbind_", 0) == 0) { const int c = atoi(k.c_str() + 6); if (c > 0) { if (v.empty()) cmdBinds.erase(c); else cmdBinds[c] = v; } }
        else if (k.rfind("alias_", 0) == 0 && k.size() > 6) { if (v.empty()) aliases.erase(k.substr(6)); else aliases[k.substr(6)] = v; }
        else if (k == "fs_width") fsWidth = std::min(std::max(iv, 0), 16384);
        else if (k == "fs_height") fsHeight = std::min(std::max(iv, 0), 16384);
        else if (k == "auto_record") autoRecord = std::min(std::max(iv, 0), 2);
        else if (k == "demo_keep") demoKeep = std::min(std::max(iv, 0), 1000);
        else if (k == "demo_sort") demoSort = std::min(std::max(iv, 0), 3);
        else if (k == "video_preset") videoPreset = std::min(std::max(iv, 0), 4);
        else if (k == "fx_opacity") { if (std::isfinite(fv)) fxOpacity = clampf(fv, 0, 1); }
        else if (k == "kill_bursts") killBursts = iv != 0;
        else if (k == "tracers") tracers = iv != 0;
        else if (k == "view_recoil") viewRecoil = iv != 0;
        else if (k == "shot_format") shotFormat = iv == 1 ? 1 : 0;
        else if (k == "shot_folder") shotFolder = iv == 1 ? 1 : 0;
        else if (k == "shot_hide_hud") shotHideHud = iv != 0;
        else if (k == "liquid_flow") liquidFlow = iv != 0;
        else if (k == "glow_strength") { if (std::isfinite(fv)) glowStrength = clampf(fv, 0, 2); }
        else if (k == "world_edge_color") worldEdgeColor = parseColor(v, worldEdgeColor);
        else if (k == "hit_tiers") hitTiers = iv != 0;
        else if (k == "ffa_colors") ffaColors = iv != 0;
        else if (k == "own_fx") ownFx = iv != 0;
        else if (k == "own_fx_color") ownFxColor = parseColor(v, ownFxColor);
        else if (k == "hb_low") hbLow = std::min(std::max(iv, 0), 100);
        else if (k == "hb_low_color") hbLowColor = parseColor(v, hbLowColor);
        else if (k == "hb_custom") hbCustom = iv != 0;
        else if (k == "hb_color") hbColor = parseColor(v, hbColor);
        else if (k == "tag_color") tagColor = std::min(std::max(iv, 0), 2);
        else if (k == "tag_custom") tagCustom = parseColor(v, tagCustom);
        else if (k == "tag_alpha") { if (std::isfinite(fv)) tagAlpha = clampf(fv, 0.1f, 1); }
        else if (k == "tag_size") { if (std::isfinite(fv)) tagSize = clampf(fv, 0.5f, 3); }
        else if (k == "fx_color_mode") fxColorMode = std::min(std::max(iv, 0), 2);
        else if (k == "fx_color") fxColor = parseColor(v, fxColor);
        else if (k == "obj_palette") objPalette = iv != 0;
        else if (k == "sky_override") skyOverride = iv != 0;
        else if (k == "glow_override") glowOverride = iv != 0;
        else if (k == "sky_color") skyColor = parseColor(v, skyColor);
        else if (k == "glow_color") glowColor = parseColor(v, glowColor);
        else if (k == "high_contrast") highContrast = iv != 0;
        else if (k == "f3_safe") f3Safe = iv != 0;
        else if (k == "on_top") onTop = iv != 0;
        else if (k == "gun_color") gunColor = std::min(std::max(iv, 0), 2);
        else if (k == "gun_custom") gunCustom = parseColor(v, gunCustom);
        else if (k == "obj_opacity") { if (std::isfinite(fv)) objOpacity = clampf(fv, 0, 2); }
        else if (k == "obj_pulse") objPulse = iv != 0;
        else if (k == "dash_buffer") { if (std::isfinite(fv)) dashBuffer = fv < 0.025f ? 0.0f : fv < 0.075f ? 0.05f : fv < 0.125f ? 0.1f : 0.15f; }
        else if (k == "smooth_amt") { if (std::isfinite(fv)) smoothAmt = clampf(fv, 0.25f, 1); }
        else if (k == "lightmap_nearest") lightmapNearest = iv != 0;
        else if (k == "wallbang_tint") wallbangTint = iv != 0;
        else if (k == "wallbang_color") wallbangColor = parseColor(v, wallbangColor);
        else if (k == "ultra_margin_us") ultraMarginUs = std::min(std::max(iv, 0), 2000);
        else if (k == "nv_threaded") nvThreaded = iv != 0;
        else if (k == "nv_dxgi") nvDxgi = iv != 0;
        else if (k == "quip_captions") quipCaptions = iv != 0;
        else if (k == "ann_duck") { if (std::isfinite(fv)) annDuck = clampf(fv, 0, 1); }
        else if (k == "tone_curve") toneCurve = iv != 0;
        else if (k == "dither") dither = iv != 0;
        else if (k == "out10") out10 = iv != 0;
        else if (k == "clean_surfaces") cleanSurfaces = iv != 0;
        else if (k == "flat_light") flatLight = iv != 0;
        else if (k == "simple_sky") simpleSky = iv != 0;
        else if (k == "far_scenery") farScenery = iv != 0;
        else if (k == "draw_dist") drawDist = iv <= 150 ? 150 : iv <= 250 ? 250 : 400;
        else if (k == "shadow_filter") shadowFilter = std::min(std::max(iv, 0), 2);
        else if (k == "shadow_dist") shadowDist = iv <= 16 ? 16 : iv <= 32 ? 32 : 64;
        else if (k == "shadow_world") shadowWorld = iv != 0;
        else if (k == "shadow_players") shadowPlayers = iv != 0;
        else if (k == "own_shadow") ownShadow = iv != 0;
        else if (k == "player_emit") { if (std::isfinite(fv)) playerEmit = clampf(fv, 0, 1); }
        else if (k == "players_lit") playersLit = iv != 0;
        else if (k == "outline_friendly_color") outlineFriendlyColor = parseColor(v, outlineFriendlyColor);
        else if (k == "outline_enemy_color") outlineEnemyColor = parseColor(v, outlineEnemyColor);
        else if (k == "outline_alpha") { if (std::isfinite(fv)) outlineAlpha = clampf(fv, 0, 1); }
        else if (k == "outline_team_width") { if (std::isfinite(fv)) outlineTeamW = fv < 0 ? -1 : clampf(fv, 0, 6); }
        else if (k == "outline_enemy_width") { if (std::isfinite(fv)) outlineEnemyW = fv < 0 ? -1 : clampf(fv, 0, 6); }
        else if (k == "fixed_teams") fixedTeams = iv != 0;
        else if (k == "team_a_color") teamAColor = parseColor(v, teamAColor);
        else if (k == "team_b_color") teamBColor = parseColor(v, teamBColor);
        else if (k == "tracer_mode") tracerMode = std::min(std::max(iv, 0), 2);
        else if (k == "tracer_color") tracerColor = parseColor(v, tracerColor);
        else if (k == "rope_color") ropeColor = parseColor(v, ropeColor);
        else if (k == "trails") trails = std::min(std::max(iv, 0), 2);
        else if (k == "trail_opacity") { if (std::isfinite(fv)) trailOpacity = clampf(fv, 0, 1); }
        else if (k == "reduce_flash") reduceFlash = iv != 0;
        else if (k == "safe_colors") safeColors = iv != 0;
        else if (k == "it_palette") itPalette = iv != 0;
        else if (k == "split_palette") splitPalette = iv != 0;
        else if (k == "split_words") splitWords = iv != 0;
        else if (k.rfind("color_preset_", 0) == 0) { const int i = atoi(k.c_str() + 13) - 1; if (i >= 0 && i < 3) colorPreset[i] = v.substr(0, 400); }
        else if (k == "fov_mode") fovMode = std::min(std::max(iv, 0), 2);
        else if (k == "channels") channels = std::min(std::max(iv, 0), 2);
        else if (k == "bg_volume") { if (std::isfinite(fv)) bgVolume = clampf(fv, 0, 1); }
        else if (k == "stereo_width") { if (std::isfinite(fv)) stereoWidth = clampf(fv, 0, 1); }
        else if (k == "behind_muffle") { if (std::isfinite(fv)) behindMuffle = clampf(fv, 0, 1); }
        else if (k == "hear_range") { if (std::isfinite(fv)) hearRange = clampf(fv, 30, 150); }
        else if (k == "occlusion") occlusion = iv != 0;
        else if (k == "dyn_range") dynRange = std::min(std::max(iv, 0), 2);
        else if (k == "vol_db") volDb = iv != 0;
        else if (k == "vol_perceptual") volPerceptual = iv != 0;
        else if (k == "audio_latency") audioLatency = std::min(std::max(iv, 0), 2);
        else if (k == "force_pack") forcePack = iv != 0;
        else if (k == "streak_calls") streakCalls = std::min(std::max(iv, 0), 2);
        else if (k == "time_warn") timeWarn = std::min(std::max(iv, 0), 2);
        else if (k == "time_warn_text") timeWarnText = iv != 0;
        else if (k == "cue_mute") cueMute = iv;
        else if (k == "ann_mute") annMute = atoll(v.c_str());
        else if (k == "sound_viz") soundViz = iv != 0;
        else if (k == "net_graph") netGraph = iv != 0;
        else if (k == "cmd_redundancy") cmdRedundancy = iv <= 2 ? 2 : iv <= 4 ? 4 : 8;
        else if (k == "map_downloads") mapDownloads = iv != 0;
        else if (k == "map_cache_max") mapCacheMax = std::min(std::max(iv, 1), 1000);
        else if (k == "light_cache_max") lightCacheMax = std::min(std::max(iv, 4), 500);
        else if (k == "bake_threads") bakeThreads = std::min(std::max(iv, 0), 256);
        else if (k == "latent_safety_us") latentSafetyUs = std::min(std::max(iv, 0), 3000);
        else if (k == "latent_slow") latentSlow = iv != 0;
        else if (k == "bypass_compositor") bypassCompositor = std::min(std::max(iv, 0), 2);
        else if (k == "cursor_size") cursorSize = std::min(std::max(iv, 0), 3);
        else if (k == "cursor_color") cursorColor = std::min(std::max(iv, 0), 3);
        else if (k == "use_toggle") useToggle = iv != 0;
        else if (k == "ch_spread") chSpread = iv != 0;
        else if (k == "ch_cooldown") chCooldown = iv != 0;
        else if (k == "ch_pixels") chPixels = iv != 0;
        else if (k == "ch_dot") { if (std::isfinite(fv)) chDot = clampf(std::round(fv), 0, 12); }
        else if (k == "ch_opacity") { if (std::isfinite(fv)) chOpacity = clampf(fv, 0.1f, 1); }
        else if (k == "ch_outline_w") { if (std::isfinite(fv)) chOutlineW = clampf(fv, 0.5f, 3); }
        else if (k == "ch_outline_a") { if (std::isfinite(fv)) chOutlineA = clampf(fv, 0, 1); }
        else if (k.rfind("ch_wstyle_", 0) == 0) {   // ch_wstyle_<weapon number>
            int w = atoi(k.c_str() + 10);
            if (w >= 0 && w < WPN_COUNT) chWStyle[w] = std::min(std::max(iv, -1), 4);
        }
        else if (k == "win_x") winX = iv;
        else if (k == "win_y") winY = iv;
        else if (k == "bg_fps") bgFps = std::min(std::max(iv, 0), 1000);
        else if (k == "bg_mute") bgMute = iv != 0;
        else if (k == "fov") { if (std::isfinite(fv)) fov = clampf(fv, 60, 140); }
        else if (k == "brightness") brightness = clampf(fv, 0.6f, 1.8f);
        else if (k == "vsync") presentMode = iv != 0 ? 1 : 0; // old setting
        else if (k == "display_mode") displayMode = std::min(std::max(iv, 0), 2);
        else if (k == "interp_ms") interpMs = std::min(std::max(iv, -1), kMaxInterpMs);
        else if (k == "present_mode") presentMode = std::min(std::max(iv, 0), 3);
        else if (k == "latent_offset_us") latentOffsetUs = std::min(std::max(iv, -4000), 4000);
        else if (k == "gpu_max_clocks") gpuMaxClocks = iv != 0;
        else if (k == "prioritize") prioritize = iv != 0;
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
        else if (k == "msaa") msaa = (iv == 1 || iv == 2 || iv == 4 || iv == 8) ? iv : 0;
        else if (k == "render_scale") renderScale = std::max(25, std::min(400, iv));
        else if (k == "render_sharpen") renderSharpen = iv != 0;
        else if (k == "volume") { if (std::isfinite(fv)) volume = clampf(fv, 0, 1.5f); }   // (up to 150%: 1.19.4)
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
        else if (k == "viewmodel_opacity") { if (std::isfinite(fv)) viewmodelOpacity = clampf(fv, 0, 1); }
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
        else if (k == "ch_head_color") chHeadColor = parseColor(v, chHeadColor);
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
        else if (k == "bot_enemies") botEnemies = std::min(std::max(iv, 1), 15);   // (1.19.4: up to 15 / 15)
        else if (k == "bot_allies") botAllies = std::min(std::max(iv, 0), 15);
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
            int a = atoi(k.c_str() + 5), c0 = 0, c1 = 0, c2 = 0;
            if (a >= 0 && a < A_COUNT && sscanf(v.c_str(), "%d,%d,%d", &c0, &c1, &c2) >= 1) { binds.code[a][0] = c0; binds.code[a][1] = c1; binds.code[a][2] = c2; }
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
    if (bindVersion < 4) {   // RACE RESET (R) came in 1.19.4: unbound if R already does something else
        bool rUsed = false;
        for (int a = 0; a < A_COUNT; a++) if (a != A_RACERESET && binds.has(a, 82)) rUsed = true;   // (GLFW R)
        binds.code[A_RACERESET][0] = rUsed ? 0 : 82;
        binds.code[A_RACERESET][1] = 0;
        bindVersion = 4;
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
    f << "name=" << name << "\nsensitivity=" << sensitivity << "\ndpi=" << dpi << "\nsens_y=" << sensY << "\ninvert_y=" << invertY << "\nzoom_sens=" << zoomSens << "\nscore_toggle=" << scoreToggle << "\nhit_volume=" << hitVolume << "\nhit_pack=" << hitPack << "\nheadshot_sound=" << headshotSound << "\nkill_sound=" << killSound << "\nhud_size=" << hudSize << "\nfeed_time=" << feedTime << "\nfeed_lines=" << feedLines << "\nfeed_mine=" << feedMine << "\nchat_time=" << chatTime << "\nchat_lines=" << chatLines << "\nchat_opacity=" << chatOpacity
      << "\nshow_fps=" << showFps << "\nshow_ping=" << showPing << "\nhide_hud_crosshair=" << hideHudCrosshair << "\nfog=" << fog << "\ngamma=" << gamma << "\ncontrast=" << contrast << "\nsaturation=" << saturation << "\ncpu_priority=" << cpuPriority << "\nscroll_wrap=" << scrollWrap << "\nscroll_skip=" << scrollSkip
      << "\nauto_iconify=" << autoIconify << "\nremember_tab=" << rememberTab << "\nlast_tab=" << lastTab << "\nconfirm_leave=" << confirmLeave << "\nvrr=" << vrr << "\ncaptions=" << captions << "\nhp_scaled=" << hpScaled << "\nvol_steps=" << volSteps << "\nvol_others_steps=" << volOthersSteps << "\nvol_weapons=" << volWeapons << "\nvol_objective=" << volObjective << "\nenemy_model=" << enemyModel << "\ngrapple_toggle=" << grappleToggle << "\nleft_handed=" << leftHanded << "\nauto_reconnect=" << autoReconnect << "\nqueue_flash=" << queueFlash << "\nghost=" << ghost << "\nteam_panel=" << teamPanel << "\nmatch_summary=" << matchSummary;
    f << "\ntone_curve=" << toneCurve << "\ndither=" << dither << "\nout10=" << out10 << "\nclean_surfaces=" << cleanSurfaces << "\nflat_light=" << flatLight << "\nsimple_sky=" << simpleSky << "\nfar_scenery=" << farScenery << "\ndraw_dist=" << drawDist
      << "\nshadow_filter=" << shadowFilter << "\nshadow_dist=" << shadowDist << "\nshadow_world=" << shadowWorld << "\nshadow_players=" << shadowPlayers << "\nown_shadow=" << ownShadow << "\nplayer_emit=" << playerEmit << "\nplayers_lit=" << playersLit
      << "\noutline_friendly_color=" << colorHex(outlineFriendlyColor) << "\noutline_enemy_color=" << colorHex(outlineEnemyColor) << "\noutline_alpha=" << outlineAlpha << "\noutline_team_width=" << outlineTeamW << "\noutline_enemy_width=" << outlineEnemyW
      << "\nfixed_teams=" << fixedTeams << "\nteam_a_color=" << colorHex(teamAColor) << "\nteam_b_color=" << colorHex(teamBColor) << "\ntracer_mode=" << tracerMode << "\ntracer_color=" << colorHex(tracerColor) << "\nrope_color=" << colorHex(ropeColor)
      << "\ntrails=" << trails << "\ntrail_opacity=" << trailOpacity << "\nreduce_flash=" << reduceFlash << "\nsafe_colors=" << safeColors << "\nit_palette=" << itPalette << "\nsplit_palette=" << splitPalette << "\nsplit_words=" << splitWords
      << "\nfov_mode=" << fovMode << "\nchannels=" << channels << "\nbg_volume=" << bgVolume << "\nstereo_width=" << stereoWidth << "\nbehind_muffle=" << behindMuffle << "\nhear_range=" << hearRange << "\nocclusion=" << occlusion << "\ndyn_range=" << dynRange
      << "\nvol_db=" << volDb << "\nvol_perceptual=" << volPerceptual << "\naudio_latency=" << audioLatency << "\nforce_pack=" << forcePack << "\nstreak_calls=" << streakCalls << "\ntime_warn=" << timeWarn << "\ntime_warn_text=" << timeWarnText
      << "\ncue_mute=" << cueMute << "\nann_mute=" << annMute << "\nsound_viz=" << soundViz << "\nnet_graph=" << netGraph << "\ncmd_redundancy=" << cmdRedundancy << "\nmap_downloads=" << mapDownloads << "\nmap_cache_max=" << mapCacheMax
      << "\nlight_cache_max=" << lightCacheMax << "\nbake_threads=" << bakeThreads << "\nlatent_safety_us=" << latentSafetyUs << "\nlatent_slow=" << latentSlow << "\nbypass_compositor=" << bypassCompositor << "\ncursor_size=" << cursorSize
      << "\ncursor_color=" << cursorColor << "\nuse_toggle=" << useToggle << "\nch_spread=" << chSpread << "\nch_cooldown=" << chCooldown << "\nch_pixels=" << chPixels;
    f << "\nshot_format=" << shotFormat << "\nshot_folder=" << shotFolder << "\nshot_hide_hud=" << shotHideHud << "\nliquid_flow=" << liquidFlow << "\nglow_strength=" << glowStrength << "\nworld_edge_color=" << colorHex(worldEdgeColor)
      << "\nhit_tiers=" << hitTiers << "\nffa_colors=" << ffaColors << "\nown_fx=" << ownFx << "\nown_fx_color=" << colorHex(ownFxColor) << "\nhb_low=" << hbLow << "\nhb_low_color=" << colorHex(hbLowColor)
      << "\nhb_custom=" << hbCustom << "\nhb_color=" << colorHex(hbColor) << "\ntag_color=" << tagColor << "\ntag_custom=" << colorHex(tagCustom) << "\ntag_alpha=" << tagAlpha << "\ntag_size=" << tagSize
      << "\nfx_color_mode=" << fxColorMode << "\nfx_color=" << colorHex(fxColor) << "\nobj_palette=" << objPalette << "\nsky_override=" << skyOverride << "\nglow_override=" << glowOverride
      << "\nsky_color=" << colorHex(skyColor) << "\nglow_color=" << colorHex(glowColor) << "\nhigh_contrast=" << highContrast << "\nf3_safe=" << f3Safe << "\non_top=" << onTop << "\ngun_color=" << gunColor
      << "\ngun_custom=" << colorHex(gunCustom) << "\nobj_opacity=" << objOpacity << "\nobj_pulse=" << objPulse << "\ndash_buffer=" << dashBuffer << "\nsmooth_amt=" << smoothAmt << "\nlightmap_nearest=" << lightmapNearest
      << "\nwallbang_tint=" << wallbangTint << "\nwallbang_color=" << colorHex(wallbangColor) << "\nultra_margin_us=" << ultraMarginUs << "\nnv_threaded=" << nvThreaded << "\nnv_dxgi=" << nvDxgi
      << "\nquip_captions=" << quipCaptions << "\nann_duck=" << annDuck;
    for (int i = 0; i < 3; i++) if (!colorPreset[i].empty()) f << "\ncolor_preset_" << i + 1 << "=" << colorPreset[i];
    for (auto& b : cmdBinds) f << "\ncbind_" << b.first << "=" << b.second;
    for (auto& a : aliases) f << "\nalias_" << a.first << "=" << a.second;
    f << "\nfs_width=" << fsWidth << "\nfs_height=" << fsHeight << "\nauto_record=" << autoRecord << "\ndemo_keep=" << demoKeep << "\ndemo_sort=" << demoSort << "\nvideo_preset=" << videoPreset << "\nfx_opacity=" << fxOpacity << "\nkill_bursts=" << killBursts << "\ntracers=" << tracers << "\nview_recoil=" << viewRecoil
       << "\nfov=" << fov << "\nbrightness=" << brightness << "\ndisplay_mode=" << displayMode
      << "\npresent_mode=" << presentMode << "\nlatent_offset_us=" << latentOffsetUs << "\ngpu_max_clocks=" << gpuMaxClocks << "\nprioritize=" << prioritize << "\nlow_latency=" << lowLatency << "\ninterp_ms=" << interpMs
      << "\nmaxfps=" << maxFps << "\nwidth=" << width << "\nheight=" << height << "\nwin_x=" << winX << "\nwin_y=" << winY << "\nbg_fps=" << bgFps << "\nbg_mute=" << bgMute
      << "\nshadows=" << shadows << "\nmsaa=" << msaa << "\nrender_scale=" << renderScale << "\nrender_sharpen=" << renderSharpen << "\nmodel=" << model << "\nsoundpack=" << soundPack << "\nname_color=" << nameColorText() << "\nhealth_bars_117=1" << "\nsocd=" << socd << "\nalt_toggle=" << altToggle
      << "\nhud_verbose=" << hudVerbose << "\nhud_widgets=" << hudWidgets << "\nshadowres=" << shadowRes << "\nvolume=" << volume << "\nvolume_effects=" << volumeEffects << "\nvolume_announcer=" << volumeAnnouncer << "\nquips=" << quips << "\nvolume_quips=" << volumeQuips << "\nhitsounds=" << hitsounds << "\nexclusive_audio=" << exclusiveAudio
      << "\naudio_output=" << audioOutput << "\naudio_input=" << audioInput << "\nmonitor=" << monitor << "\nfullscreen_hz=" << fullscreenHz << "\nannouncer=" << announcer << "\nvoice=" << announcerVoice << "\nfullbright=" << fullbright << "\nown_shots=" << ownShots << "\nenemy_shots=" << enemyShots << "\nimpact_sparks=" << impactSparks << "\nmuzzle_flash=" << muzzleFlash << "\nviewmodel_opacity=" << viewmodelOpacity << "\nhealth_bars=" << healthBars << "\nhealth_bars_bottom=" << healthBarsBottom
      << "\nscope_opacity=" << scopeOpacity << "\nsticky_counter=" << stickyCounter << "\ncrowns=" << crowns << "\nchat_off=" << chatOff << "\ndmg_nums=" << dmgNums << "\ndmg_num_scale=" << dmgNumScale << "\ndamage_flash=" << damageFlash << "\nzoom_fov_sniper=" << zoomFovSniper << "\nzoom_fov_rifle=" << zoomFovRifle << "\nfullbright_color=" << colorHex(fullbrightColor) << "\ndiscord=" << discordName << "\nui_font=" << uiFont << "\ndemo_full_rate=" << demoFullRate << "\noutline_friendly=" << outlineFriendly << "\noutline_enemy=" << outlineEnemy << "\noutline_player_width=" << outlinePlayer << "\noutline_world_width=" << outlineWorld
      << "\ncolor_friendly=" << colorHex(colorFriendly) << "\ncolor_enemy=" << colorHex(colorEnemy)
      << "\nch_style=" << chStyle << "\nch_length=" << chLength << "\nch_thickness=" << chThickness << "\nch_gap=" << chGap
      << "\nch_outline=" << chOutline << "\nch_team_color=" << chTeamColor << "\nch_color=" << colorHex(chColor)
      << "\nch_hit_flash=" << chHitFlash << "\nch_hit_color=" << colorHex(chHitColor) << "\nch_head_color=" << colorHex(chHeadColor) << "\nch_dot=" << chDot << "\nch_opacity=" << chOpacity << "\nch_outline_w=" << chOutlineW << "\nch_outline_a=" << chOutlineA
      << "\nlast_address=" << lastAddress << "\nbrowser_sort=" << browserSort << "\nbrowser_hide_full=" << browserHideFull << "\nbrowser_hide_empty=" << browserHideEmpty << "\nbrowser_max_ping=" << browserMaxPing << "\nfav_servers=" << favServers << "\nrecent_servers=" << recentServers << "\nrank_hello=" << rankHello << "\nhost_name=" << hostName << "\nhost_prober=" << hostProber << "\nhost_map=" << hostMap
      << "\nhost_mode=" << hostMode << "\nhost_port=" << hostPort << "\nhost_max=" << hostMaxPlayers
      << "\nhost_upnp=" << hostUpnp << "\nhost_balance_bots=" << hostBalanceBots << "\nhost_log=" << hostLog << "\nhost_duel_size=" << hostDuelSize << "\nhost_public=" << hostPublic << "\nhost_ranked=" << hostRanked << "\nhost_custom=" << hostCustom << "\nhost_bot_brain=" << hostBotBrain << "\nhost_variant=" << hostArenaVariant << "\nbot_variant=" << botArenaVariant << "\nbot_skill=" << botSkill
      << "\nbot_enemies=" << botEnemies << "\nbot_allies=" << botAllies << "\nbot_mode=" << botMode << "\n";
    for (int w = 0; w < WPN_COUNT; w++) if (chWStyle[w] >= 0) f << "ch_wstyle_" << w << "=" << chWStyle[w] << "\n";
    for (int i = 0; i < HUD_COUNT; i++)
        f << "hud_" << i << "=" << hud[i].x << "," << hud[i].y << "," << hud[i].scale << "," << hud[i].custom << ","
          << hud[i].color.x << "," << hud[i].color.y << "," << hud[i].color.z << "," << hud[i].hidden << "," << hud[i].z << ","
          << hud[i].pitch << "," << hud[i].yaw << "," << hud[i].roll << "\n";
    for (int m = 0; m < MODE_COUNT; m++) {
        if (!modeHud[m].empty()) f << "mode_hud_" << modeName(m) << "=" << modeHud[m] << "\n";
        if (!modeCtl[m].empty()) f << "mode_controls_" << modeName(m) << "=" << modeCtl[m] << "\n";
    }
    f << batch3Text();
    f << "bind_version=4\n";
    for (int a = 0; a < A_COUNT; a++) f << "bind_" << a << "=" << binds.code[a][0] << "," << binds.code[a][1] << "," << binds.code[a][2] << "\n";   // (older games read the first two)
    return f.str();
}

// CROSSHAIR > EXPORT / IMPORT: the ch_ lines of the settings, ';'-separated after "quark-crosshair:", so a crosshair can
// be pasted into chat or Discord in one line. Only ch_ keys are read back (a code can't change anything else).
std::string Settings::crosshairCode() const {
    std::istringstream in(toText());
    std::string line, out = "quark-crosshair:";
    bool first = true;
    while (std::getline(in, line))
        if (line.rfind("ch_", 0) == 0) { out += (first ? "" : ";") + line; first = false; }
    return out;
}

bool Settings::crosshairFromCode(const std::string& code) {
    size_t at = code.find("quark-crosshair:");
    std::string body = at == std::string::npos ? code : code.substr(at + 16);
    std::istringstream in(body);
    std::string item;
    int n = 0;
    for (int& w : chWStyle) w = -1;   // (a code without per-weapon styles: none)
    while (std::getline(in, item, ';')) {
        while (!item.empty() && (item.back() == '\r' || item.back() == '\n' || item.back() == ' ')) item.pop_back();
        size_t eq = item.find('=');
        if (eq == std::string::npos || item.rfind("ch_", 0) != 0) continue;
        set(item.substr(0, eq), item.substr(eq + 1));
        n++;
    }
    return n > 0;
}


// ---- batch 3 (1.19.4, the 2s): one table of keys, read and written together (each value clamped to its range)
namespace {
struct B3 {
    enum T { B, I, F, C, S } t;
    const char* key;
    void* p;
    float lo, hi;
};
}
static std::vector<B3> b3Keys(Settings& s) {
    using T = B3;
    std::vector<B3> v = {
        {T::C, "dmg_num_color", &s.dmgNumColor, 0, 0}, {T::I, "dmg_num_stack", &s.dmgNumStack, 0, 2}, {T::F, "dmg_num_time", &s.dmgNumTime, 0.3f, 3},
        {T::B, "dmg_arcs", &s.dmgArcs, 0, 1}, {T::C, "dmg_arc_color", &s.dmgArcColor, 0, 0}, {T::F, "dmg_arc_size", &s.dmgArcSize, 0.5f, 2},
        {T::F, "dmg_arc_time", &s.dmgArcTime, 0.3f, 3}, {T::B, "kill_mark", &s.killMark, 0, 1}, {T::F, "kill_mark_time", &s.killMarkTime, 0.2f, 2},
        {T::C, "kill_mark_color", &s.killMarkColor, 0, 0}, {T::F, "hit_flash_time", &s.hitFlashTime, 0.05f, 1}, {T::B, "kill_text", &s.killText, 0, 1},
        {T::B, "heal_nums", &s.healNums, 0, 1}, {T::F, "low_hp", &s.lowHp, 0, 0.9f}, {T::C, "low_hp_color", &s.lowHpColor, 0, 0},
        {T::B, "low_hp_pulse", &s.lowHpPulse, 0, 1}, {T::B, "low_hp_sound", &s.lowHpSound, 0, 1},
        {T::I, "speed_unit", &s.speedUnit, 0, 2}, {T::F, "speed_scale", &s.speedScale, 0.25f, 4}, {T::B, "speed_ticks", &s.speedTicks, 0, 1},
        {T::I, "bar_caps", &s.barCaps, 0, 15}, {T::B, "clock_12h", &s.clock12, 0, 1}, {T::I, "clock_mode", &s.clockMode, 0, 1},
        {T::B, "clock_tenths", &s.clockTenths, 0, 1}, {T::I, "verbose_lines", &s.verboseLines, 0, 0x3FFF}, {T::B, "net_icons", &s.netIcons, 0, 1}, {T::B, "drain_warn", &s.drainWarn, 0, 1},
        {T::I, "autohide_health", &s.autoHideHealth, 0, 2}, {T::I, "autohide_slots", &s.autoHideSlots, 0, 1}, {T::I, "autohide_bars", &s.autoHideBars, 0, 1},
        {T::F, "obj_size", &s.objSize, 0.5f, 2}, {T::F, "obj_mark_opacity", &s.objMarkOpacity, 0.1f, 1}, {T::B, "obj_distance", &s.objDistance, 0, 1},
        {T::B, "obj_arrow", &s.objArrow, 0, 1}, {T::F, "scope_size", &s.scopeSize, 0.5f, 1.2f}, {T::C, "scope_color", &s.scopeColor, 0, 0},
        {T::F, "scope_line", &s.scopeLine, 1, 4}, {T::I, "spec_hud", &s.specHud, 0, 1}, {T::B, "feed_headshot", &s.feedHeadshot, 0, 1},
        {T::I, "score_sort", &s.scoreSort, 0, 4}, {T::I, "score_cols", &s.scoreCols, 0, 0xFF},
        {T::B, "chat_one_color", &s.chatOneColor, 0, 1}, {T::C, "chat_color", &s.chatColor, 0, 0}, {T::C, "sys_color", &s.sysColor, 0, 0},
        {T::B, "chat_box", &s.chatBox, 0, 1}, {T::F, "chat_box_opacity", &s.chatBoxOpacity, 0.1f, 1}, {T::B, "chat_stamps", &s.chatStamps, 0, 1},
        {T::B, "mention_hl", &s.mentionHl, 0, 1}, {T::B, "mention_sound", &s.mentionSound, 0, 1}, {T::B, "chat_filter", &s.chatFilter, 0, 1},
        {T::B, "team_chat_only", &s.teamChatOnly, 0, 1}, {T::B, "other_name_colors", &s.otherNameColors, 0, 1}, {T::B, "streamer", &s.streamer, 0, 1},
        {T::I, "sys_msgs", &s.sysMsgs, 0, 2}, {T::I, "alerts", &s.alerts, 0, 15}, {T::I, "on_join", &s.onJoin, 0, 2},
        {T::I, "death_cam", &s.deathCam, 0, 2}, {T::F, "kill_cam_time", &s.killCamTime, 0, kKillCamTime}, {T::F, "recoil_amount", &s.recoilAmount, 0, 2},
        {T::F, "spec_fov", &s.specFov, 0, 140}, {T::B, "spec_third", &s.specThird, 0, 1}, {T::S, "quip_block", &s.quipBlock, 0, 0},
        {T::I, "quip_pick", &s.quipPick, -1, 255}, {T::B, "use_overrides", &s.useOverrides, 0, 1}, {T::B, "legacy_maps", &s.legacyMaps, 0, 1},
        {T::F, "mouse_accel", &s.mouseAccel, 0, 1}, {T::B, "raw_input", &s.rawInput, 0, 1}, {T::I, "alt_toggle_weapons", &s.altToggleWpns, -1, 0xFFFF},
        {T::I, "swap_fire", &s.swapFire, 0, 0xFFFF}, {T::B, "auto_hop", &s.autoHop, 0, 1}, {T::I, "console_key", &s.consoleKey, 0, 999},
        {T::B, "console_menus", &s.consoleMenus, 0, 1}, {T::B, "ranked_alt_toggle", &s.rankedAltToggle, 0, 1},
        {T::I, "menu_bg", &s.menuBg, 0, 2}, {T::S, "menu_bg_map", &s.menuBgMap, 0, 0}, {T::C, "menu_bg_color", &s.menuBgColor, 0, 0},
        {T::I, "menu_theme", &s.menuTheme, 0, 1}, {T::F, "panel_dark", &s.panelDark, 0.5f, 1.5f}, {T::F, "text_bright", &s.textBright, 0.5f, 1},
        {T::B, "menu_accent_on", &s.menuAccentOn, 0, 1}, {T::C, "menu_accent", &s.menuAccent, 0, 0},
        {T::B, "hide_ratings", &s.hideRatings, 0, 1}, {T::B, "remember_queues", &s.rememberQueues, 0, 1}, {T::S, "last_queues", &s.lastQueues, 0, 0},
        {T::F, "ed_fly_speed", &s.edFlySpeed, 1, 100}, {T::F, "ed_fast_mul", &s.edFastMul, 1, 10}, {T::F, "ed_grid_custom", &s.edGridCustom, 0, 16},
        {T::F, "ed_rot_step", &s.edRotStep, 1, 90}, {T::I, "ed_grid", &s.edGridIdx, 0, 6}, {T::B, "ed_fly_binds", &s.edFlyBinds, 0, 1},
        {T::S, "ed_light", &s.edLight, 0, 0}, {T::I, "race_split_vs", &s.raceSplitVs, 0, 2}, {T::F, "race_split_time", &s.raceSplitTime, 0.5f, 10},
        {T::I, "race_date", &s.raceDate, 0, 2}, {T::I, "host_tick", &s.hostTick, 0, 3}, {T::I, "host_snap", &s.hostSnap, 0, 4},
        {T::I, "host_upload", &s.hostUpload, 0, 100000}, {T::B, "host_auto_start", &s.hostAutoStart, 0, 1}, {T::I, "host_bot_skill", &s.hostBotSkill, 0, 3}, {T::I, "ed_test_mode", &s.edTestMode, -1, MODE_COUNT - 1}, {T::I, "ed_test_bots", &s.edTestBots, 0, 7},
        {T::B, "ed_test_at_cam", &s.edTestAtCam, 0, 1}, {T::B, "ed_test_no_warmup", &s.edTestNoWarmup, 0, 1},
        // batch 4b (the 1s)
        {T::C, "bar_recharge_color", &s.barRechargeColor, 0, 0}, {T::C, "speed_low_color", &s.speedLowColor, 0, 0},
        {T::C, "speed_high_color", &s.speedHighColor, 0, 0}, {T::C, "slot_off_color", &s.slotOffColor, 0, 0},
        {T::C, "sticky_full_color", &s.stickyFullColor, 0, 0}, {T::C, "warn_color", &s.warnColor, 0, 0}, {T::C, "info_color", &s.infoColor, 0, 0},
        {T::C, "dmg_head_color", &s.dmgHeadColor, 0, 0}, {T::C, "dmg_big_color", &s.dmgBigColor, 0, 0},
        {T::F, "text_outline", &s.textOutline, 0, 1.5f}, {T::I, "notice_lines", &s.noticeLines, 1, 8}, {T::F, "notice_time", &s.noticeTime, 0.5f, 3},
        {T::B, "compass", &s.compass, 0, 1}, {T::B, "radar", &s.radar, 0, 1}, {T::F, "radar_range", &s.radarRange, 10, 120},
        {T::B, "hide_rec", &s.hideRec, 0, 1}, {T::I, "tourney_tab", &s.tourneyTab, 0, 1}, {T::B, "step_smooth", &s.stepSmooth, 0, 1},
        {T::B, "send_typing", &s.sendTyping, 0, 1}, {T::B, "rps_click", &s.rpsClick, 0, 1}, {T::B, "fixed_logo", &s.fixedLogo, 0, 1},
        {T::I, "bot_looks", &s.botLooks, 0, 1}, {T::C, "clip_color", &s.clipColor, 0, 0},
    };
    return v;
}

bool Settings::setBatch3(const std::string& k, const std::string& v) {
    if (k.rfind("wpn_sens_", 0) == 0) {   // wpn_sens_<weapon number>
        const int w = atoi(k.c_str() + 9);
        const float f = (float)atof(v.c_str());
        if (w >= 0 && w < WPN_COUNT && std::isfinite(f)) wpnSens[w] = clampf(f, 0.1f, 5);
        return true;
    }
    if (k.rfind("wpn_vm_", 0) == 0) {   // wpn_vm_<weapon number>=<shown 0/1>,<x>,<y>,<depth>
        const int w = atoi(k.c_str() + 7);
        int sh = 1;
        float o[3] = {0, 0, 0};
        if (w >= 0 && w < WPN_COUNT && sscanf(v.c_str(), "%d,%f,%f,%f", &sh, &o[0], &o[1], &o[2]) >= 1) {
            wpnVmHide[w] = sh == 0;
            for (int i = 0; i < 3; i++) wpnVmOff[w][i] = std::isfinite(o[i]) ? clampf(o[i], -200, 200) : 0;
        }
        return true;
    }
    if (k.size() < 4) return false;
    for (const B3& e : b3Keys(*this)) {
        if (k != e.key) continue;
        const float f = (float)atof(v.c_str());
        const int i = atoi(v.c_str());
        switch (e.t) {
            case B3::B: *(bool*)e.p = i != 0; break;
            case B3::I: *(int*)e.p = std::min(std::max(i, (int)e.lo), (int)e.hi); break;
            case B3::F: if (std::isfinite(f)) *(float*)e.p = clampf(f, e.lo, e.hi); break;
            case B3::C: *(Vec3*)e.p = parseColor(v, *(Vec3*)e.p); break;
            case B3::S: *(std::string*)e.p = v.substr(0, 400); break;
        }
        return true;
    }
    return false;
}

std::string Settings::batch3Text() const {
    std::ostringstream f;
    for (const B3& e : b3Keys(const_cast<Settings&>(*this))) {
        f << e.key << "=";
        switch (e.t) {
            case B3::B: f << *(const bool*)e.p; break;
            case B3::I: f << *(const int*)e.p; break;
            case B3::F: f << *(const float*)e.p; break;
            case B3::C: f << colorHex(*(const Vec3*)e.p); break;
            case B3::S: f << *(const std::string*)e.p; break;
        }
        f << "\n";
    }
    for (int w = 0; w < WPN_COUNT; w++) if (wpnSens[w] != 1.0f) f << "wpn_sens_" << w << "=" << wpnSens[w] << "\n";
    for (int w = 0; w < WPN_COUNT; w++)
        if (wpnVmHide[w] || wpnVmOff[w][0] != 0 || wpnVmOff[w][1] != 0 || wpnVmOff[w][2] != 0)
            f << "wpn_vm_" << w << "=" << (wpnVmHide[w] ? 0 : 1) << "," << wpnVmOff[w][0] << "," << wpnVmOff[w][1] << "," << wpnVmOff[w][2] << "\n";
    return f.str();
}

// VIDEO + AUDIO > MORE 4: COPY / PASTE COLORS and the preset slots: every colour setting as one line, like the
// crosshair's code ("quark-colors:" then key=value pairs, ';'-separated). Only these keys are read back.
static const char* const kColorKeys[] = {"color_friendly", "color_enemy", "outline_friendly", "outline_enemy", "outline_friendly_color",
                                         "outline_enemy_color", "outline_alpha", "team_a_color", "team_b_color", "tracer_mode", "tracer_color",
                                         "rope_color", "fullbright_color", "world_edge_color", "own_fx_color", "hb_low_color", "hb_color", "tag_custom", "fx_color", "sky_color", "glow_color", "gun_custom", "wallbang_color", "ch_team_color", "ch_color", "ch_hit_color", "ch_head_color", "name_color"};
std::string Settings::colorCode() const {
    std::istringstream in(toText());
    std::string line, out = "quark-colors:";
    bool first = true;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        for (const char* c : kColorKeys) if (k == c) { out += (first ? "" : ";") + line; first = false; }
    }
    return out;
}

bool Settings::colorFromCode(const std::string& code) {
    const size_t at = code.find("quark-colors:");
    if (at == std::string::npos) return false;
    std::istringstream in(code.substr(at + 13));
    std::string item;
    int n = 0;
    while (std::getline(in, item, ';')) {
        while (!item.empty() && (item.back() == '\r' || item.back() == '\n' || item.back() == ' ')) item.pop_back();
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = item.substr(0, eq);
        for (const char* c : kColorKeys) if (k == c) { set(k, item.substr(eq + 1)); n++; }
    }
    return n > 0;
}
