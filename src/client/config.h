// Client settings, persisted as key=value lines in quark.cfg.
#pragma once
#include <string>
#include "../common/qmath.h"

struct Settings {
    std::string name = "player";
    float sensitivity = 2.0f;   // degrees per 100 mouse counts
    float fov = 100.0f;         // horizontal degrees
    int displayMode = 0;        // 0 windowed, 1 borderless fullscreen, 2 exclusive fullscreen (legacy)
    int presentMode = 0;        // 0 immediate (vsync off, tearing), 1 vsync, 2 adaptive vsync (tears only when late)
    int lowLatency = 1;         // 0 off, 1 on (max 1 frame queued), 2 ultra (0 queued, just-in-time input)
    int maxFps = 0;             // 0 = unlimited
    int width = 1280, height = 720;
    bool shadows = true;
    int shadowRes = 2048;
    // The two-color palette: your team, and the enemy (everyone else in FFA/race).
    Vec3 colorFriendly = {0.25f, 0.85f, 1.0f};
    Vec3 colorEnemy = {1.0f, 0.3f, 0.25f};
    std::string lastAddress = "127.0.0.1";
    // hosting
    std::string hostName = "quark server";
    std::string hostMap = "arena";
    int hostMode = 5;
    int hostPort = 27500;
    int hostMaxPlayers = 8;
    bool hostUpnp = true;

    void load();
    void save() const;
};
