// Client settings, persisted as key=value lines in quark.cfg.
#pragma once
constexpr int kOutlineColors = 9;   // (the last: CUSTOM, its colours on VIDEO + AUDIO > MORE 4)
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
    // 1.19.4 settings, batch 3 (the 2s): VIDEO + AUDIO > MORE 2 / 4 / 5 / 6 and CROSSHAIR
    bool toneCurve = true;         // MORE 2: the filmic (ACES-style) curve on the lit scene; off: a straight clamp (flat / linear)
    bool dither = false;           // ...a tiny noise before the 8-bit output: no banding in the sky and fog
    bool out10 = false;            // ...a 10-bit window where the driver gives one (at the next start)
    bool cleanSurfaces = false;    // ...the palette's walls and floors without panel seams or shade noise
    bool flatLight = false;        // ...no baked light: every face at full sun and sky (no shadows, no bounce)
    bool simpleSky = false;        // ...the sky one flat colour (no sun glow, no stars)
    bool farScenery = true;        // ...the distant backdrop (far scenery) drawn
    int drawDist = 400;            // ...metres to the far plane (150 / 250 / 400): past it nothing is drawn
    int shadowFilter = 1;          // MORE 4: player shadows' edge: 0 hard (1 tap), 1 soft (4), 2 softer (16)
    int shadowDist = 32;           // ...metres the shadow map reaches around you (16 / 32 / 64: farther = blurrier)
    bool shadowWorld = true, shadowPlayers = true;   // ...cast onto the map, onto players
    bool ownShadow = true;         // ...your own body's shadow (first person)
    float playerEmit = 0.3f;       // ...models' own light (their colour's share that shadow and shade don't touch)
    bool playersLit = false;       // ...players never darkened by the map's shade (full sun on them)
    Vec3 outlineFriendlyColor = {0, 0, 0}, outlineEnemyColor = {0, 0, 0};   // ...the CUSTOM outline colours
    float outlineAlpha = 1.0f;     // ...the player outline's opacity
    float outlineTeamW = -1, outlineEnemyW = -1;   // ...widths for your team / enemies (-1: PLAYER OUTLINE WIDTH)
    bool fixedTeams = false;       // ...spectating / demos: team A and B in their own fixed colours
    Vec3 teamAColor = {0.3f, 0.55f, 1.0f}, teamBColor = {1.0f, 0.6f, 0.15f};
    int tracerMode = 0;            // MORE 2: tracers' colour: 0 the classic pale yellow, 1 the shooter's team, 2 custom
    Vec3 tracerColor = {1.0f, 0.95f, 0.7f};
    Vec3 ropeColor = {0.1f, 0.1f, 0.1f};   // MORE 4: grapple ropes
    int trails = 2;                // MORE 2: projectile trails: 0 off, 1 short, 2 full
    float trailOpacity = 1.0f;
    bool reduceFlash = false;      // MORE 4: no muzzle flashes or blinking, softer explosions and damage flash
    bool safeColors = false;       // ...OK / error colours blue and orange (not green and red)
    bool itPalette = false;        // ...tag / hide and seek: "it" in your enemy colour, the rest in your team's
    bool splitPalette = false, splitWords = false;   // ...race splits in your palette; AHEAD / BEHIND spelled out
    std::string colorPreset[3];    // ...your colour schemes (colorCode()), saved in slots 1 - 3
    std::string colorCode() const;             // the colours as one line to share (MORE 4: COPY COLORS)
    bool colorFromCode(const std::string& code);   // ...and back: false if it holds none
    int fovMode = 0;               // MORE 6: FOV as 0 horizontal (this screen), 1 vertical, 2 horizontal at 4:3
    int channels = 0;              // MORE 5: 0 stereo, 1 mono, 2 left / right swapped
    float bgVolume = 1.0f;         // ...the volume while unfocused (x VOLUME; MUTE IN BACKGROUND still mutes)
    float stereoWidth = 0.8f;      // ...how far a sound to one side pans (0: centred, 1: all the way)
    float behindMuffle = 1.0f;     // ...the low-pass and dip on sounds behind you (x the default)
    float hearRange = 90;          // ...metres past which a sound isn't heard
    bool occlusion = false;        // ...sounds behind walls muffled (you hear less: it affects gameplay)
    int dynRange = 1;              // ...0 clean (barely limited), 1 normal, 2 night (compressed: quiet sounds up, loud down)
    bool volDb = false;            // ...volumes shown in dB
    bool volPerceptual = false;    // ...VOLUME's slider on a perceptual curve (the gain is its square)
    int audioLatency = 1;          // ...0 low, 1 normal, 2 safe (more queued: no crackle on a busy PC)
    bool forcePack = false;        // ...everyone's sounds in your SOUND PACK
    int streakCalls = 0;           // ...killstreaks: 0 everyone's, 1 only yours, 2 none
    int timeWarn = 1;              // ...time left: 0 off, 1 one minute + 30 s, 2 also a count of the last 10 s
    bool timeWarnText = false;     // ...and as text on screen
    int cueMute = 0;               // ...cue sounds turned off (a bit per sound id below 32)
    long long annMute = 0;         // ...announcer lines turned off (a bit per line, SND_VOICE_THREE = bit 0)
    bool soundViz = false;         // ...nearby sounds shown as marks around the crosshair
    bool netGraph = false;         // MORE 6: ping, loss and rates bottom right (apart from F3)
    int cmdRedundancy = 4;         // ...each command in this many packets (2 / 4 / 8)
    bool mapDownloads = true;      // ...a server's map you don't have is downloaded
    int mapCacheMax = 50;          // ...downloaded maps kept (oldest go)
    int lightCacheMax = 32;        // ...light bakes kept
    int bakeThreads = 0;           // ...threads a bake uses (0: all but one core)
    int latentSafetyUs = 100;      // ...latent sync: this much spare before the blank
    int latentSlow = 0;            // ...a frame that misses its blank: 0 flips now (a tear), 1 waits for the next
    int bypassCompositor = 0;      // ...Linux X11: 0 GLFW's (fullscreen only), 1 always, 2 never
    int cursorSize = 0;            // ...the menu cursor: 0 the system's, 1 x1, 2 x1.5, 3 x2
    int cursorColor = 0;           // ...0 white, 1 black, 2 yellow, 3 your team colour
    bool useToggle = false;        // ...USE pressed once to hold, again to let go
    // 1.19.4 settings, batch 4 (the 1s): VIDEO + AUDIO > MORE 2 / 4 / 5 / 6
    int shotFormat = 0;            // MORE 6: screenshots as 0 PNG, 1 TGA (written at once, no compression pass)
    int shotFolder = 0;            // ...in 0 the data folder's screenshots/, 1 Pictures/Quark
    bool shotHideHud = false;      // ...the screenshot key leaves the HUD out (the 3D view only)
    bool liquidFlow = true;        // MORE 2: water / lava / acid flow (off: still)
    float glowStrength = 1.0f;     // ...the map's glowing parts, x
    Vec3 worldEdgeColor = {0, 0, 0};   // MORE 4: the map's outline colour
    bool hitTiers = true;          // MORE 5: the hitsound gets heavier with the damage (off: one sound)
    bool ffaColors = false;        // MORE 4: free for all: each opponent their own colour
    bool ownFx = false;            // ...your projectiles, laser and rope in OWN EFFECTS COLOR
    Vec3 ownFxColor = {0.3f, 1.0f, 0.5f};
    int hbLow = 30;                // ...health bars turn LOW COLOR at or below this health
    Vec3 hbLowColor = {1, 0.35f, 0.3f};
    bool hbCustom = false;         // ...health bars over players in HEALTH BAR COLOR (not the team's)
    Vec3 hbColor = {1, 1, 1};
    int tagColor = 0;              // ...teammates' name tags: 0 team colour, 1 white, 2 custom
    Vec3 tagCustom = {1, 1, 1};
    float tagAlpha = 1.0f, tagSize = 1.0f;
    int fxColorMode = 0;           // MORE 2: explosions / sparks: 0 their own, 1 your team colour, 2 custom
    Vec3 fxColor = {1.0f, 0.85f, 0.5f};
    bool objPalette = false;       // MORE 4: the bomb's blink and unreached checkpoints in your enemy colour
    bool skyOverride = false, glowOverride = false;   // ...the map's sky / glow colour replaced
    Vec3 skyColor = {0.62f, 0.66f, 0.72f}, glowColor = {1.0f, 0.55f, 0.15f};
    bool highContrast = false;     // ...lighter walls and floors, darker trim
    bool f3Safe = false;           // ...F3's pipeline colours told apart under colour blindness (Okabe-Ito)
    bool onTop = false;            // MORE 6: the window always on top (windowed)
    int gunColor = 0;              // MORE 4: the gun's body: 0 the map's metal, 1 your team colour, 2 custom
    Vec3 gunCustom = {0.3f, 0.33f, 0.37f};
    float objOpacity = 1.0f;       // ...objective zones' opacity, x
    bool objPulse = true;          // ...and their pulse (off: steady)
    float dashBuffer = 0.1f;       // MORE 6: a dash pressed this long before it's allowed is held till then (0 / 0.05 / 0.1 / 0.15 s)
    float smoothAmt = 1.0f;        // MORE 2: SMOOTH anti-aliasing's strength
    bool lightmapNearest = false;  // ...LIGHTMAP FILTER: nearest (blocky light)
    bool wallbangTint = false;     // MORE 4: wallbang walls in WALLBANG COLOR (not dark grey)
    Vec3 wallbangColor = {0.55f, 0.25f, 0.6f};
    int ultraMarginUs = 150;       // MORE 6: LOW LATENCY ULTRA: the frame starts this much early
    bool nvThreaded = false;       // ...Windows NVIDIA: threaded optimization off (driver profile)
    bool nvDxgi = false;           // ...Windows NVIDIA: present through DXGI (driver profile)
    bool quipCaptions = false;     // MORE 5: quips shown as captions
    float annDuck = 0.0f;          // ...effects ducked by this much while the announcer speaks (0: none, 1: silent)
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
    bool chSpread = false;      // a circle showing the held weapon's spread (shotgun)
    bool chCooldown = false;    // a ring filling as the held weapon's cooldown runs out
    bool chPixels = false;      // sizes in exact screen pixels (off: scaled with the resolution, as at 720p)
    std::string crosshairCode() const;          // the ch_ settings as one line to share (CROSSHAIR > EXPORT)
    bool crosshairFromCode(const std::string& code);   // ...and back (IMPORT): false if it holds no crosshair setting
    // HUD layout: offset (pixels at 720p) from the default spot, size, and an optional color override
    // (keys, then the grapple launcher: after the weapon model, so saved layouts keep their indices; then the score box,
    // kill feed and chat, which could only be themed and moved from 1.19.0.testing on)
    enum { HUD_HEALTH, HUD_WEAPON, HUD_BARS, HUD_SPEED, HUD_SLOTS, HUD_VIEWMODEL, HUD_KEYS, HUD_GRAPPLE, HUD_SCORE, HUD_KILLFEED, HUD_CHAT,
           HUD_COUNT };
    // x / y / scale move and size an element (720p units); hidden removes it. For the two first-person models (the
    // weapon, the grapple launcher), x / y / depth move it in front of the camera (depth: z, + = further away), pitch /
    // yaw / roll tilt it about its pivot (degrees) and colour tints its accent parts (the launcher: its stripe).
    struct HudElem {
        float x = 0, y = 0, scale = 1; bool custom = false; Vec3 color = {1, 1, 1}; bool hidden = false; float z = 0;
        float pitch = 0, yaw = 0, roll = 0;
    };
    static bool isModelElem(int el) { return el == HUD_VIEWMODEL || el == HUD_GRAPPLE; }
    HudElem hud[HUD_COUNT];
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
    Settings() { binds.setDefaults(); for (int& w : chWStyle) w = -1; }
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
