// Client settings, persisted as key=value lines in quark.cfg.
#pragma once
#include <string>
#include "../common/qmath.h"
#include "binds.h"

struct Settings {
    std::string name = "player";
    float sensitivity = 2.0f;   // degrees per 100 mouse counts (x 0.022 deg per count: same scale as CS / Quake)
    int dpi = 800;              // mouse DPI, only used to show / set cm per 360
    float fov = 100.0f;         // horizontal degrees
    int displayMode = 0;        // 0 windowed, 1 borderless fullscreen, 2 exclusive fullscreen (legacy)
    int presentMode = 0;        // 0 immediate (vsync off, tearing), 1 vsync, 2 adaptive vsync (tears only when late)
    int lowLatency = 1;         // 0 off, 1 on (max 1 frame queued), 2 ultra (0 queued, just-in-time input)
    int maxFps = 0;             // 0 = unlimited
    int width = 1280, height = 720;
    bool shadows = true;
    int msaa = 0;               // anti-aliasing: 0 off, 1 smooth edges (FXAA-style pass), 2 / 4 MSAA
    float volume = 0.6f;
    bool hitsounds = true;
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
    int hostArenaVariant = 0, botArenaVariant = 0; // 1v1 ruleset
    // crosshair
    int chStyle = 0;            // 0 cross, 1 cross + dot, 2 dot, 3 circle, 4 T
    float chLength = 8, chThickness = 2, chGap = 4; // in pixels at 720p
    bool chOutline = true;      // 1 px black outline
    bool chTeamColor = true;    // use your team color, else chColor
    Vec3 chColor = {1, 1, 1};
    // HUD layout: offset (pixels at 720p) from the default spot, size, and an optional color override
    enum { HUD_HEALTH, HUD_WEAPON, HUD_BARS, HUD_SPEED, HUD_SLOTS, HUD_COUNT };
    struct HudElem { float x = 0, y = 0, scale = 1; bool custom = false; Vec3 color = {1, 1, 1}; };
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
