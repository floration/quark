// Client settings, persisted as key=value lines in quark.cfg.
#pragma once
constexpr int kOutlineColors = 8;
extern const char* const kOutlineNames[kOutlineColors];
#include <cstdio>
#include <map>
#include <string>
#include "../common/qmath.h"
#include "binds.h"
#include "../game/gamedefs.h"

struct Settings {
    std::string name = "player";
    float sensitivity = 2.0f;   // degrees per 100 mouse counts (x 0.022 deg per count: same scale as CS / Quake)
    int dpi = 800;              // mouse DPI, only used to show / set cm per 360
    // CONTROLS > MORE (1.19.4): vertical sensitivity as a multiple of SENSITIVITY, invert Y, and the zoomed sensitivity
    // as a multiple of the "same feel" zoom (1 = turning the same angle on screen as unzoomed)
    float sensY = 1.0f;
    bool invertY = false;
    float zoomSens = 1.0f;
    bool scoreToggle = false;   // the scoreboard key: hold (off) or press to show / hide
    float fov = 100.0f;         // horizontal degrees
    float brightness = 1.0f;    // scales the lit scene (0.6 - 1.8)
    int displayMode = 0;        // 0 windowed, 1 borderless fullscreen, 2 exclusive fullscreen (legacy)
    int interpMs = 0;           // other players' interpolation delay: 0 = adaptive, else fixed ms (ranked: always 10)
#ifdef _WIN32
    int presentMode = 3;        // (the default: latent sync; a typical PC runs many times the refresh rate)
#else
    int presentMode = 0;        // (the default: no vsync; latent sync needs Windows' scanline)
#endif
                                // 0 immediate (vsync off, tearing), 1 vsync, 2 adaptive vsync (tears only when late),
                                // 3 latent sync (Windows: a frame a refresh, flipped as the blank begins; Game::latentWait)
    int latentOffsetUs = 0;     // latent sync: the flip this much later (+) or sooner (-), moving the tear line
    bool gpuMaxClocks = false;  // Windows, NVIDIA: "Prefer maximum performance" in quark's driver profile (gpuprefs.h)
    bool prioritize = true;     // Windows: GPU + CPU priority raised while the game has focus (setGamePriority)
    int lowLatency = 1;         // 0 off, 1 on (max 1 frame queued), 2 ultra (0 queued, just-in-time input)
    int maxFps = 0;             // 0 = unlimited
    int width = 1280, height = 720;
    int winX = -100000, winY = -100000;   // the window's last position when windowed (-100000: never moved; 100 px in)
    int bgFps = 60;             // VIDEO > MORE: the frame cap while the window isn't focused or is minimized (0 = FPS LIMIT as usual)
    bool bgMute = false;        // ...and the sound muted then
    bool shadows = true;
    int model = 0;              // player model (cosmetic): STANDARD, HEAVY, SIMPLE, ROBOT, KNIGHT, WIZARD, ZOMBIE, FIGHTER, PIRATE, NINJA, BANDIT
    int soundPack = 0;          // DEFAULT, HEAVY METAL, MAGICAL, RETRO, WOODEN
    int nameColor = -1;         // your name's colour as others see it (0xRRGGBB; -1 = the default)
    bool hbMigrated = false;    // (1.17: health bars were switched on once for every existing config)
    int socd = 0;               // opposite movement keys held together: 0 cancel out, 1 last pressed wins, 2 first pressed wins
    bool altToggle = false;     // alt-fire (scope) toggles on a tap instead of hold
    bool hudVerbose = false;    // the info block top right (clock, version, fps min, speed, players alive, ...)
    bool hudWidgets = true;
    // per-mode rules: a huds/ or controls/ profile file used automatically in that mode ("" = your default)
    std::string modeHud[MODE_COUNT], modeCtl[MODE_COUNT];     // draw the widgets from hud.cfg
    int msaa = 0;               // anti-aliasing: 0 off, 1 smooth edges (FXAA-style pass), 2 / 4 MSAA
    int renderScale = 100;      // RENDER SCALE: the 3D scene drawn at the window's size times this % (50 - 200), the HUD at the window's
    bool renderSharpen = true;  // ...and below 100% sharpened as it's stretched (SHARPEN)
    float volume = 0.6f;
    float volumeEffects = 1.0f;    // x volume: every sound effect (the hitsound too)
    float volumeAnnouncer = 1.0f;  // x volume: the announcer's lines
    bool quips = true;             // players' quips (the QUIP key) heard at all: their own setting, apart from the announcer's
    float volumeQuips = 1.0f;      // x volume: quips
    bool hitsounds = true;
    float volObjective = 1.0f;         // VIDEO > MORE 3: the planted bomb's beep (faster as it runs down)
    float volSteps = 1.0f, volOthersSteps = 1.0f, volWeapons = 1.0f;   // VIDEO > MORE 3: your footsteps, everyone else's (up to 2: louder), gunfire
    float hitVolume = 1.0f;        // VIDEO > MORE: the hitsound's own volume (x EFFECTS VOLUME; up to 2)
    int hitPack = -1;              // ...which sound pack's hitsound (-1 = your SOUNDS pack)
    bool headshotSound = true;     // ...a higher ding on a headshot
    bool killSound = true;         // ...the kill sound (its own switch; was only with HITSOUND)
    float hudSize = 1.0f;          // VIDEO > MORE: every HUD element's size at once (x each element's own SIZE)
    // 1.19.4 settings, batch 2 (the 3s)
    int feedTime = 6, feedLines = 6;   // GAMEPLAY > MORE: the kill feed's seconds a line and most lines
    bool feedMine = false;             // ...only kills you're in
    int chatTime = 10, chatLines = 8;  // ...chat's seconds a line and most lines
    float chatOpacity = 1.0f;          // ...chat's opacity
    bool showFps = false, showPing = false;   // GAMEPLAY > MORE: FPS / ping in the top right corner, with no widget
    bool hideHudCrosshair = true;      // the HIDE HUD key keeps the crosshair
    float fog = 1.0f;
    float gamma = 1.0f, contrast = 1.0f, saturation = 1.0f;   // VIDEO > MORE 2: PICTURE (the 3D scene; the HUD as it is)                  // VIDEO > MORE: the distance fog, x the default (0 = none)
    int cpuPriority = 1;               // PRIORITIZE GAME's CPU class: 1 above normal, 2 high
    bool scrollWrap = true;            // NEXT / PREV WEAPON wraps from the last weapon to the first
    int scrollSkip = 0;                // ...and skips these weapons (bit per weapon id)
    bool autoIconify = true;           // fullscreen: minimized when it loses focus (alt-tab)
    bool rememberTab = false;          // settings opens on the tab it was left on
    int lastTab = 0;
    bool confirmLeave = false;         // ask before LEAVE / QUIT / STOP HOSTING
    bool vrr = false;                  // G-SYNC / FREESYNC: vsync on and the frame cap 3 under the refresh
    bool captions = false;             // the announcer's lines as text too
    int videoPreset = 0;               // VIDEO > MORE 3: the preset last picked (0 custom, 1 low, 2 medium, 3 high, 4 competitive)
    float fxOpacity = 1.0f;            // VIDEO > MORE 2: explosions, sparks and bursts' opacity (0 = none drawn)
    bool killBursts = true;            // ...the red burst where someone dies
    bool tracers = true;               // ...the bullet tracers (beams are YOUR / ENEMY SHOTS)
    bool viewRecoil = true;            // ...the gun model's kick on a shot
    std::map<int, std::string> cmdBinds;   // the console's "bind <key> <commands>": a key that runs console lines (cbind_<code>)
    std::map<std::string, std::string> aliases;   // "alias <name> <commands>": a word that runs them (alias_<name>)
    bool matchSummary = true;          // GAMEPLAY > MORE: the match's end: your shots, hits, accuracy, damage and best streak
    bool teamPanel = false;            // GAMEPLAY > MORE: team modes: your teammates' health and the enemies left, at the left
    bool ghost = true;                 // GAMEPLAY > MORE: race / surf: your best run's ghost runs beside you
    int autoRecord = 0;                // DEMOS: record every match you join (1), only ranked ones (2), or not (0)
    int demoKeep = 0;                  // ...keep only the newest this many demos (0: all); older ones are deleted
    int demoSort = 0;                  // ...the list: newest first, oldest, name, biggest
    bool queueFlash = true;            // GAMEPLAY > MORE: a filled queue flashes the taskbar button (the game in the background)
    bool autoReconnect = true;         // GAMEPLAY > MORE: a lost connection (not a kick) tries the server again, 3 times
    bool leftHanded = false;           // CONTROLS > MORE: HAND: the gun on the left, the grapple launcher on the right
    bool grappleToggle = false;        // CONTROLS > MORE: GRAPPLE press to hook, press again to let go (not held)
    int enemyModel = -1;               // GAMEPLAY > MORE: every enemy drawn as this player model (-1: their own)
    bool hpScaled = true;              // health bars against the mode's max health (not 100)
    bool exclusiveAudio = false;   // Windows: the sound device to the game alone (lower latency; other programs silent)
    // Settings > VIDEO + AUDIO > DEVICES (devices.h): stable ids, "" = the default; one that's gone falls back to it
    std::string audioOutput;       // the sound output's id (a Windows endpoint id, an ALSA name, a CoreAudio UID)
    std::string audioInput;        // the microphone's id (nothing records yet: kept for voice chat)
    std::string monitor;           // fullscreen / borderless on this monitor (its name, "#2" for a second of the same); "" = primary
    int fsWidth = 0, fsHeight = 0;     // EXCLUSIVE FULLSCREEN's resolution (VIDEO > MORE 3); 0 = the monitor's desktop size
    int fullscreenHz = 0;          // EXCLUSIVE FULLSCREEN's refresh rate; 0 = the monitor's desktop rate
    // VISUALS
    bool fullbright = false;    // flat white world, black outlines, no lighting
    int ownShots = 0;           // your tracers / beam: 0 with the weapon model, 1 always, 2 never
    bool enemyShots = true;     // other players' tracers and beams
    bool impactSparks = true;   // a spark where a hitscan shot meets a wall or a player (yours and everyone's)
    bool muzzleFlash = true;    // a flash at the gun's muzzle on each shot (yours and everyone's)
    float viewmodelOpacity = 1.0f; // the first-person gun and grapple launcher: 1 solid, 0 not drawn (see-through between)
    bool healthBars = true;     // health bars over other players (not in ranked; on by default, owner 1.17)
    bool healthBarsBottom = false;  // ... at their feet instead of over the head
    float scopeOpacity = 1.0f;  // the sniper scope's black surround and reticle (0 = off: just the zoom, with the normal crosshair)
    bool stickyCounter = true;  // "STICKIES 3/6" while you hold the sticky launcher or have any out
    bool demoFullRate = false;  // demos: every snapshot (up to 1000 Hz) instead of 62.5 Hz
    std::string uiFont = "rubik";   // Settings > FONT (uifonts.h): a built-in's id (rubik, worksans, classic, ...) or a mods/fonts file name
    std::string discordName;    // linked on the master for the Discord bot (roles, queue pings)
    bool crowns = true;         // gold / silver crowns on ranked leaders
    bool chatOff = false;       // other players' chat hidden (settings "DISABLE CHAT", or the TOGGLE CHAT key)
    bool dmgNums = true;        // the numbers over whoever you hit (VISUALS > DAMAGE NUMBERS)
    float dmgNumScale = 1.0f;   // ...and their size (0.5 - 3; 1 = the HUD text size)
    float damageFlash = 0.0f;   // the screen-edge flash when you're hit (VISUALS > DAMAGE FLASH; 0 = off, the default (owner), 1 = full)
    // the sniper's / rifle's alt-fire zoom (horizontal degrees; the weapon table's zoomFov by default; client-side only)
    float zoomFovSniper = baseWeaponDef(WPN_SNIPER).zoomFov, zoomFovRifle = baseWeaponDef(WPN_RIFLE).zoomFov;
    Vec3 fullbrightColor = {1, 1, 1};   // FULLBRIGHT's flat colour (white: the classic look)
    int outlineFriendly = 0, outlineEnemy = 0; // player outline presets (kOutlineNames: black, white, team, ...)
    float outlinePlayer = 1.0f, outlineWorld = 1.0f; // outline widths in pixels (players' models / the map's edges); 0 = none
    int announcerVoice = 0;     // 0 male, 1 female: recorded lines, the generated voice for any not recorded yet
    int announcer = 2;          // voice lines: 0 off, 1 the round countdown only, 2 also the mode's calls (bomb planted, hill captured...)
    int shadowRes = 2048;
    // The two-color palette: your team, and the enemy (everyone else in FFA/race).
    Vec3 colorFriendly = {0.25f, 0.85f, 1.0f};
    Vec3 colorEnemy = {1.0f, 0.3f, 0.25f};
    std::string lastAddress = "127.0.0.1";
    // JOIN's server list (1.19.4): sort (0 ping, 1 players, 2 name), hide full / empty, a ping cap (0 = any), and your
    // favourite and recent servers ("host:port" lists, ';'-separated; recent: the last 8 joined, newest first)
    int browserSort = 0;
    bool browserHideFull = false, browserHideEmpty = false;
    int browserMaxPing = 0;
    std::string favServers, recentServers;
    std::string rankHello;      // the ranked id the Worker confirmed a /hello for (its old rows moved): not sent again
    // hosting
    std::string hostName = "quark server";
    std::string hostProber;     // "host:port" that checks our port is reachable (empty: QUARK_PROBER, else a listed server)
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
    std::string hostBotBrain;   // bots/<name>.qbrain the hosted game's bots play (empty = the built-in brain; never ranked)
    int hostArenaVariant = 0, botArenaVariant = 0; // 1v1 ruleset
    // crosshair
    int chStyle = 0;            // 0 cross, 1 cross + dot, 2 dot, 3 circle, 4 T
    float chLength = 8, chThickness = 2, chGap = 4; // in pixels at 720p
    bool chOutline = true;      // 1 px black outline
    bool chTeamColor = true;    // use your team color, else chColor
    Vec3 chColor = {1, 1, 1};
    bool chHitFlash = true;     // a hit flashes the crosshair in chHitColor (and a center square on styles without a dot)
    Vec3 chHitColor = {1, 1, 1};
    Vec3 chHeadColor = {1, 0.25f, 0.2f};   // ...a headshot's flash colour (CROSSHAIR > ON HIT: HEADSHOT)
    float chDot = 0;            // the dot's size in pixels at 720p (0 = THICKNESS)
    float chOpacity = 1;        // the crosshair's opacity
    float chOutlineW = 1;       // the outline's thickness in pixels (1 - 3)
    float chOutlineA = 0.9f;    // ...and its opacity
    int chWStyle[WPN_COUNT];    // per weapon: its own STYLE (-1 = the crosshair's STYLE)
    std::string crosshairCode() const;          // the ch_ settings as one line to share (CROSSHAIR > EXPORT)
    bool crosshairFromCode(const std::string& code);   // ...and back (IMPORT): false if it holds no crosshair setting
    // HUD layout: offset (pixels at 720p) from the default spot, size, and an optional color override
    // (keys, then the grapple launcher: after the weapon model, so saved layouts keep their indices; then the score box,
    // kill feed and chat, which could only be themed and moved from 1.19.0.testing on)
    // (1.19.4: the objective line, the notices and the scoreboard, appended so older hud_<n> lines keep their meaning)
    enum { HUD_HEALTH, HUD_WEAPON, HUD_BARS, HUD_SPEED, HUD_SLOTS, HUD_VIEWMODEL, HUD_KEYS, HUD_GRAPPLE, HUD_SCORE, HUD_KILLFEED, HUD_CHAT,
           HUD_OBJECTIVE, HUD_NOTICES, HUD_SCOREBOARD, HUD_COUNT };
    // x / y / scale move and size an element (720p units); hidden removes it. For the two first-person models (the
    // weapon, the grapple launcher), x / y / depth move it in front of the camera (depth: z, + = further away), pitch /
    // yaw / roll tilt it about its pivot (degrees) and colour tints its accent parts (the launcher: its stripe).
    struct HudElem {
        float x = 0, y = 0, scale = 1; bool custom = false; Vec3 color = {1, 1, 1}; bool hidden = false; float z = 0;
        float pitch = 0, yaw = 0, roll = 0;
    };
    static bool isModelElem(int el) { return el == HUD_VIEWMODEL || el == HUD_GRAPPLE; }
    HudElem hud[HUD_COUNT];
    // ---- 1.19.4 settings, batch 3 (the 2s): HUD, social, controls, editor, menus (setBatch3 / batch3Text in config.cpp)
    // GAMEPLAY > MORE 2 (feedback): damage numbers, damage arcs, the kill mark, the hit flash, low health
    Vec3 dmgNumColor = {1, 1, 1};      // a normal hit's number (headshots stay red, big hits gold)
    int dmgNumStack = 0;               // 0 quick hits add up, 1 a number a hit, 2 one running total a target until it fades
    float dmgNumTime = 0.9f;           // seconds a number shows
    bool dmgArcs = true;               // the red arcs toward whoever hit you
    Vec3 dmgArcColor = {1, 0.12f, 0.08f};
    float dmgArcSize = 1.0f, dmgArcTime = 1.0f;
    bool killMark = true;              // the X on the crosshair on your kill
    float killMarkTime = 0.45f;
    Vec3 killMarkColor = {1, 0.12f, 0.1f};
    float hitFlashTime = 0.15f;        // the crosshair's hit flash
    bool killText = false;             // "KILLED <NAME>" under the crosshair
    bool healNums = true;              // green "+N" when you heal
    float lowHp = 0.3f;                // low health below this share of the max: the number + bar in lowHpColor
    Vec3 lowHpColor = {1, 0.35f, 0.3f};
    bool lowHpPulse = false, lowHpSound = false;   // ...pulsing; a beep as you drop under it
    // GAMEPLAY > MORE 3 (HUD info)
    int speedUnit = 0;                 // the speed meter / verbose speed: 0 m/s, 1 km/h, 2 ups (Quake units, 39.37 a metre)
    float speedScale = 1.0f;           // the speed meter's bar: x the speed it takes to fill
    bool speedTicks = true;            // ...its tick marks
    int barCaps = 15;                  // captions shown: bit 0 WEAPON, 1 DASH, 2 HOOK, 3 SPEED
    bool clock12 = false;              // the verbose TIME as 12 h (am / pm)
    int clockMode = 0;                 // the round clock: 0 time left, 1 time played
    bool clockTenths = false;          // ...tenths in its last 10 s
    int verboseLines = 0x3FFF;         // the verbose info block's lines (bit per line, kVerboseNames in game.cpp)
    bool drainWarn = true;             // vs / duel / 1v1 / tourney: "HEALTH DRAINS IN N" over the last 10 s, a beep at 10 and 5
    bool netIcons = true;              // warnings for high ping / packet loss / a stalled connection
    int autoHideHealth = 0;            // 0 always, 1 for 3 s after it changes (and while low), 2 only while low
    int autoHideSlots = 0;             // the weapon list: 0 always, 1 for 3 s after a switch
    int autoHideBars = 0;              // the cooldown bars: 0 always, 1 only while one isn't ready
    float objSize = 1.0f, objOpacity = 1.0f;   // objective markers (bomb, hill, cart, points)
    bool objDistance = true, objArrow = true;  // ...their distance in metres; an arrow at the screen's edge when off screen
    float scopeSize = 1.0f;            // the sniper scope's circle (x the default)
    Vec3 scopeColor = {0, 0, 0};       // ...its reticle lines
    float scopeLine = 1.0f;            // ...their thickness (px at 720p)
    int specHud = 0;                   // spectating: 0 the full HUD, 1 minimal
    bool feedHeadshot = true;          // the kill feed: a headshot marked "(HS)"
    int scoreSort = 0;                 // the scoreboard: 0 score, 1 kills, 2 deaths, 3 ping, 4 name
    int scoreCols = 0xFF;              // ...the columns shown (bit per column)
    // GAMEPLAY > MORE 4 (social, death camera)
    bool chatOneColor = false;         // chat lines in chatColor (not by team)
    Vec3 chatColor = {1, 1, 1};
    Vec3 sysColor = {0.7f, 0.72f, 0.75f};   // server / system lines
    bool chatBox = false;              // a dark box behind chat (not only while typing)
    float chatBoxOpacity = 0.4f;
    bool chatStamps = false;           // [12:34] before each line
    bool mentionHl = true, mentionSound = true;   // a line with your name: gold, and a ping
    bool chatFilter = false;           // rude words starred out (a short list)
    bool teamChatOnly = false;         // only your team's chat (and the server's lines)
    bool otherNameColors = true;       // other players' custom name colours (off: the team colour)
    bool streamer = false;             // hide addresses, codes and other players' names
    int sysMsgs = 0;                   // server messages: 0 all, 1 important, 2 off
    int alerts = 0;                    // unfocused: flash the taskbar on bit 0 match start, 1 round start, 2 a vote, 3 a join
    int onJoin = 0;                    // joining a match: 0 play, 1 spectate, 2 ask
    int deathCam = 0;                  // dead: 0 watch the killer, 1 your body, 2 a teammate
    float killCamTime = 1.5f;          // team modes: how long the killer can be watched (0 - kKillCamTime)
    float recoilAmount = 1.0f;         // the gun model's kick (x the default; 0 = none)
    float specFov = 0;                 // spectating: the FOV (0 = yours)
    bool specThird = false;            // spectating: behind the player, not their eyes
    std::string quipBlock;
    int quipPick = -1;                 // PLAYER > MORE: MY QUIP: the QUIP key says this one (-1: a random one)
    bool useOverrides = true;          // PLAYER > MORE: MODS: your overrides folder used (next start)             // players whose quips you don't hear (names, ';'-separated)
    // CONTROLS > MORE 2
    float wpnSens[WPN_COUNT];          // per weapon: x SENSITIVITY (1 = the same)
    bool wpnVmHide[WPN_COUNT] = {};    // per weapon: its model hidden (GAMEPLAY > MORE 2: WEAPON MODEL)
    float wpnVmOff[WPN_COUNT][3] = {}; // per weapon: added to the WEAPON MODEL element's x / y (720p units) / depth (mm)
    float mouseAccel = 0;              // sensitivity x (1 + accel x counts per ms), at most x4 (0 = off)
    bool rawInput = true;              // raw mouse input (off: the OS's pointer motion, with its acceleration)
    int altToggleWpns = -1;            // ALT-FIRE: TOGGLE applies to these weapons (bits; -1: the ones that zoom)
    int swapFire = 0;                  // these weapons swap FIRE and ALT FIRE (bits)
    bool autoHop = true;               // holding jump jumps again on landing (off: a press a jump)
    int consoleKey = 96;               // the console's key (GLFW code; ` by default)
    bool consoleMenus = false;         // ...also opens in the menus
    bool rankedAltToggle = true;       // ALT-FIRE: TOGGLE in ranked too
    // PLAYER > MORE (menus, ranked)
    int menuBg = 0;                    // the menu's background: 0 the last map, 1 a map picked (menuBgMap), 2 a plain colour
    std::string menuBgMap;
    Vec3 menuBgColor = {0.06f, 0.07f, 0.09f};
    int menuTheme = 0;                 // 0 dark, 1 high contrast
    bool menuAccentOn = false;         // the menus' accent: menuAccent (off: your team colour)
    Vec3 menuAccent = {1.0f, 0.75f, 0.25f};
    float panelDark = 1.0f, textBright = 1.0f;   // the panels' darkness, the menu text's brightness
    bool hideRatings = false;          // ranked: ratings hidden on the scoreboard
    bool rememberQueues = false;       // the ranked queues you're in, joined again at the next start
    std::string lastQueues;            // (those queues, ','-separated)
    // race: splits compared with your best run, your best segments or your last run; how long a split shows; the
    // race stats page's date format (0 month-day, 1 day-month, 2 year-month-day)
    int raceSplitVs = 0;
    float raceSplitTime = 2.5f;
    int raceDate = 0;
    // HOST > ADVANCED: the tick rate (0 1000, 1 500, 2 250, 3 125 Hz; the highest: it still steps down if the PC can't
    // keep up), the snapshot rate (0 as the tick rate, 1 500, 2 250, 3 125, 4 62.5), an upload cap (kbit/s, 0 none),
    // the match starting by itself once both sides have players, balance bots' skill
    int hostTick = 0, hostSnap = 0, hostUpload = 0;
    bool hostAutoStart = false;
    int hostBotSkill = 1;
    // EDITOR
    float edFlySpeed = 12.0f, edFastMul = 3.0f, edGridCustom = 0, edRotStep = 45.0f;   // (ROTATION STEP: Q / E on a spawn; its slider drags in 15 or less)
    int edGridIdx = 2;
    bool edFlyBinds = false;           // the fly camera's keys follow your FORWARD / BACK / LEFT / RIGHT / JUMP / CROUCH binds
    std::string edLight;               // the last light's values ("radius intensity"), "" = the default
    int edTestMode = -1;               // TEST PLAY: the mode (-1: the map's own)
    int edTestBots = 0;                // ...enemy bots
    bool edTestAtCam = false;          // ...start where the editor's camera is
    bool edTestNoWarmup = false;       // ...the match starts at once
    // play vs bots
    int botSkill = 1;           // 0 beginner .. 3 expert
    int botEnemies = 3, botAllies = 1;
    int botMode = 5;            // ffa

    Binds binds;
    // 2: the binds were written by this version, so a slot that's unbound was unbound by the player and stays that way.
    // Older files (no bind_version) get the migrations in finishLoad (knife / syringe / quip keys, chat T / Y -> Enter),
    // which used to run on every load: unbinding the knife, syringe or quip came back after a restart (UI test).
    // 3: F4 shows the mouse to photon number (LATENCY); VOTE START moved from F4 to F5.
    // 4: RACE RESET (R) added; unbound if R was taken.
    int bindVersion = 4;
    Settings() { binds.setDefaults(); for (int& w : chWStyle) w = -1; for (float& s : wpnSens) s = 1.0f; }
    bool setBatch3(const std::string& key, const std::string& value);   // (config.cpp: the batch 3 keys)
    std::string batch3Text() const;
    void load();
    void save() const;
    void set(const std::string& key, const std::string& value); // one setting from text (file or console)
    std::string toText() const;                                 // every setting as key=value lines
    std::string nameColorText() const {                         // name_color's value: RRGGBB, or -1
        if (nameColor < 0) return "-1";
        char b[8]; snprintf(b, sizeof b, "%06X", nameColor & 0xFFFFFF); return b;
    }
    uint16_t nameColor565() const {                             // as sent to the server (RGB565; 0 = the default)
        if (nameColor < 0) return 0;
        int r = (nameColor >> 16) & 255, g = (nameColor >> 8) & 255, b = nameColor & 255;
        uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        return c ? c : 0x0821;   // (black isn't "default")
    }
    void finishLoad();
};
