// Client settings, persisted as key=value lines in quark.cfg.
#pragma once
constexpr int kOutlineColors = 8;
extern const char* const kOutlineNames[kOutlineColors];
#include <string>
#include "../common/qmath.h"
#include "binds.h"
#include "../game/gamedefs.h"

struct Settings {
    std::string name = "player";
    float sensitivity = 2.0f;   // degrees per 100 mouse counts (x 0.022 deg per count: same scale as CS / Quake)
    int dpi = 800;              // mouse DPI, only used to show / set cm per 360
    float fov = 100.0f;         // horizontal degrees
    float brightness = 1.0f;    // scales the lit scene (0.6 - 1.8)
    int displayMode = 0;        // 0 windowed, 1 borderless fullscreen, 2 exclusive fullscreen (legacy)
    int interpMs = 0;           // other players' interpolation delay: 0 = adaptive, else fixed ms (ranked: always 10)
    int presentMode = 0;        // 0 immediate (vsync off, tearing), 1 vsync, 2 adaptive vsync (tears only when late)
    int lowLatency = 1;         // 0 off, 1 on (max 1 frame queued), 2 ultra (0 queued, just-in-time input)
    int maxFps = 0;             // 0 = unlimited
    int width = 1280, height = 720;
    bool shadows = true;
    int model = 0;              // player model (cosmetic): STANDARD, HEAVY, SCOUT, ROBOT, KNIGHT
    int soundPack = 0;          // STANDARD, ARCADE, HEAVY, RETRO, SOFT
    int socd = 0;               // opposite movement keys held together: 0 cancel out, 1 last pressed wins, 2 first pressed wins
    bool altToggle = false;     // alt-fire (scope) toggles on a tap instead of hold
    bool hudVerbose = false;    // the info block top right (clock, version, fps min, speed, players alive, ...)
    bool hudWidgets = true;
    // per-mode rules: a huds/ or controls/ profile file used automatically in that mode ("" = your default)
    std::string modeHud[MODE_COUNT], modeCtl[MODE_COUNT];     // draw the widgets from hud.cfg
    int msaa = 0;               // anti-aliasing: 0 off, 1 smooth edges (FXAA-style pass), 2 / 4 MSAA
    float volume = 0.6f;
    bool hitsounds = true;
    int outlineFriendly = 0, outlineEnemy = 0; // player outline presets (kOutlineNames: black, white, team, ...)
    int announcerVoice = 0;     // 0 male, 1 female (both deep, in a big hall)
    int announcer = 2;          // voice lines: 0 off, 1 the round countdown only, 2 also the mode's calls (bomb planted, hill captured...)
    int shadowRes = 2048;
    // The two-color palette: your team, and the enemy (everyone else in FFA/race).
    Vec3 colorFriendly = {0.25f, 0.85f, 1.0f};
    Vec3 colorEnemy = {1.0f, 0.3f, 0.25f};
    std::string lastAddress = "127.0.0.1";
    // hosting
    std::string hostName = "quark server";
    std::string hostMap = "bastion";
    int hostMode = 5;
    int hostPort = 27500;
    int hostMaxPlayers = 8;
    bool hostUpnp = true;
    bool hostBalanceBots = true;
    int hostLog = 0;            // server game log: 0 off, 1 simple, 2 verbose, 3 extreme
    int hostDuelSize = 1;       // duel: 1 = 1v1, 2 = 2v2
    bool hostPublic = true;     // show hosted games on the internet server list
    bool hostRanked = false;    // rated matches (no bots)
    std::string hostCustom;     // custom mode to host (empty = none)
    int hostArenaVariant = 0, botArenaVariant = 0; // 1v1 ruleset
    // crosshair
    int chStyle = 0;            // 0 cross, 1 cross + dot, 2 dot, 3 circle, 4 T
    float chLength = 8, chThickness = 2, chGap = 4; // in pixels at 720p
    bool chOutline = true;      // 1 px black outline
    bool chTeamColor = true;    // use your team color, else chColor
    Vec3 chColor = {1, 1, 1};
    // HUD layout: offset (pixels at 720p) from the default spot, size, and an optional color override
    enum { HUD_HEALTH, HUD_WEAPON, HUD_BARS, HUD_SPEED, HUD_SLOTS, HUD_VIEWMODEL, HUD_KEYS, HUD_COUNT }; // (keys: after the model, so saved layouts keep their indices)
    // x / y / scale move and size an element (720p units); hidden removes it. For the weapon model, x / y / depth
    // move it in front of the camera (depth: z, + = further away) and colour tints its accent parts.
    struct HudElem { float x = 0, y = 0, scale = 1; bool custom = false; Vec3 color = {1, 1, 1}; bool hidden = false; float z = 0; };
    HudElem hud[HUD_COUNT];
    // play vs bots
    int botSkill = 1;           // 0 beginner .. 3 expert
    int botEnemies = 3, botAllies = 1;
    int botMode = 5;            // ffa

    Binds binds;
    Settings() { binds.setDefaults(); }
    void load();
    void save() const;
};
