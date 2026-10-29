#include <fstream>
#include "../common/u8file.h"
#include "../common/rng.h"
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <sstream>
#include "../common/profile.h"
#include "gpuprofile.h"
#include <array>
#include "game.h"
#include "complete.h"
#include "openurl.h"
#include "../game/benchmark.h"
#include "profiles.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <climits>
#include "../game/demo.h"
#include "../game/gamemodes.h"
#include "../common/version.h"
#include "gltf.h"
#include "overrides.h"
#include "hudclock.h"
#include "modecalls.h"

static std::string fmtRace(int ms);
static std::string hudCfgPath() { return userDataPath("hud.cfg"); } // scriptable widgets, next to quark.cfg

namespace {
const Color kWhite = {1, 1, 1, 1};
const Color kGrey = {0.7f, 0.72f, 0.75f, 1};
const Color kRed = {1, 0.35f, 0.3f, 1};
const Color kGreenOk = {0.35f, 0.9f, 0.45f, 1};

struct Palette { const char* name; Vec3 friendly, enemy; };
const Palette kPalettes[] = {
    {"CYAN / RED", {0.25f, 0.85f, 1.0f}, {1.0f, 0.3f, 0.25f}},
    {"BLUE / ORANGE", {0.3f, 0.55f, 1.0f}, {1.0f, 0.6f, 0.15f}},
    {"GREEN / MAGENTA", {0.35f, 1.0f, 0.45f}, {1.0f, 0.3f, 0.85f}},
    {"WHITE / YELLOW", {0.95f, 0.95f, 0.95f}, {1.0f, 0.85f, 0.1f}},
};
constexpr int kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

std::string fmtTime(float s) {
    int t = (int)std::ceil(std::max(0.0f, s));
    char b[16];
    snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}

const ModelPalette kNoSlots;   // (props with no colour slots: the arms, the bomb's backpack)

const char* weaponName(int w) {
    if (w == kDamageGrapple) return "grapple";
    if (w == kDamageAcid) return "dissolved";
    if (w == kDamageGlass) return "glass";
    if (w == 252) return "fell";
    if (w == 253) return "bomb";
    if (w == 254) return "drained";
    if (w == 255) return "suicide";
    return weaponDef(w).name;
}

// GLFW's monitor callback (no user pointer): a flag the game thread picks up in gameMonitor (it runs every frame anyway)
bool g_monitorEvent = false;
void onMonitorEvent(GLFWmonitor*, int) { g_monitorEvent = true; }
}

// The owner's asset overrides (docs/ASSETS.md) the game reads itself: glTF models in place of the box models, and the
// window icon. (Sounds: Audio; the font: Renderer; maps: the build.) Nothing is read when there are none.
void Game::loadAssetOverrides() {
    if (!overrides::any()) return;
    loadModelOverrides(modelOv_);
    std::vector<uint8_t> icon;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (overrides::find("images/icon", {".png", ".jpg", ".jpeg"}, icon) && decodeImage(icon.data(), icon.size(), w, h, rgba) && w > 0 && h > 0) {
        GLFWimage img{w, h, rgba.data()};
        glfwSetWindowIcon(win_, 1, &img);
    }
}

void Game::overrideTints(int kind, int idx, const ModelPalette& drawn, Vec3* tint) const {
    const ModelPalette& ref = referencePalette(kind, idx);
    const ModelPalette pal = drivenPalette(kind, idx, drawn);   // (a slot this model's drawing doesn't colour: 1)
    tint[0] = {1, 1, 1};
    auto ratio = [](float a, float b) { return b > 1e-3f ? a / b : a; };
    for (int s = 1; s < TINT_COUNT; s++)
        tint[s] = {ratio(pal.c[s].x, ref.c[s].x), ratio(pal.c[s].y, ref.c[s].y), ratio(pal.c[s].z, ref.c[s].z)};
}

bool Game::init(GLFWwindow* win, const LaunchOptions& opts, std::string* err) {
    win_ = win;
    opts_ = opts;
    cfg_.load();
    int w, h;
    glfwGetFramebufferSize(win, &w, &h);
    r_.resize(w, h);
    if (!r_.init(cfg_.shadowRes, err)) return false;
    loadAssetOverrides();
    applyUiFontSetting();   // (Settings > FONT; the default needs nothing)
    parts_.reserve(std::max(kMaxDefaultParts, modelOv_.maxBoxes()));   // (the box list every model is drawn from, box models' too: never grown while drawing)
    rescanMaps();
    rankKey_ = loadOrCreateRankKey(userDataPath("quark.key")); // ranked identity: stays on this computer
    net_.setRankKey(rankKey_);   // (servers check the id against it; the key itself never goes to them)
    // 1.17 ranked ids come from the key's public half: the Worker moves this key's old id (ratings, race times, Discord
    // link, lifetime stats) over the first time it sees the secret. Sent in the background, retried until it takes
    // (REVIEW 3.6), then recorded in the config so later starts don't send it again.
    if (!rankKey_.secret.empty() && opts_.screenshot.empty() && cfg_.rankHello != rankKey_.id) { rank_.hello(rankKey_); helloPending_ = true; }
    ui_.steadyCaret = !opts_.screenshot.empty();
    if (opts_.hostMode < 0 && opts_.vsBotsMode < 0 && opts_.connect.empty() && opts_.editMap.empty()) loadMenuBackdrop();
    if (opts_.debugInfo) { setScreen(Screen::Info); infoTab_ = opts_.debugInfo - 1; }
    if (opts_.debugFonts) { setScreen(Screen::Settings); fontsOpen_ = opts_.debugFonts == 1; debugSettingsTab(); }
    if (opts_.debugCustom) { rescanMaps(); editRules_ = CustomRules(); editRules_.name = "my mode"; editRules_.move[MV_AIR_ACCEL] = 2; editRules_.gravity = 0.5f; customTab_ = opts_.debugCustom - 1; setScreen(Screen::CustomMode); }
    if (!opts_.demo.empty()) playDemo(opts_.demo);
    if (opts_.debugLeaderboard) { setScreen(Screen::Leaderboard); raceStatsView_ = opts_.debugRaceStats; }
    if (!opts_.editMap.empty()) { openEditor(opts_.editMap); if (opts_.editSession) edHostSession(); if (opts_.editSel >= 0) edSel_ = {ED_BRUSH, opts_.editSel}; if (opts_.edFocus) { edTitle_ = ed_.title; ui_.focus(41); } }
    if (!opts_.editScript.empty()) { std::stringstream ss(opts_.editScript); std::string st; while (std::getline(ss, st, ';')) if (!st.empty()) edScript_.push_back(st); }
    for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == cfg_.hostMap) hostMapIdx_ = (int)i;
    hostModeIdx_ = clampf((float)cfg_.hostMode, 0, MODE_COUNT - 1);
    cfg_.hostMaxPlayers = std::min(std::max(cfg_.hostMaxPlayers, 2), kMaxPlayers);
    hostPortStr_ = std::to_string(cfg_.hostPort);
    joinAddr_ = cfg_.lastAddress;
    if (!opts_.menuPage.empty()) debugMenuPage();   // (after the host / bots settings are read: a picker can set them)
    ui_.accent = rgba(cfg_.colorFriendly);
    glfwSetMonitorCallback(onMonitorEvent);   // (a monitor plugged in or out: DEVICES' MONITOR found again)
    applyDisplayMode();
    applyPresentMode();
    for (auto& q : latQ_) glGenQueries(1, &q.q);
    audio_.setAnnouncer(cfg_.announcerVoice);
    audio_.setExclusive(cfg_.exclusiveAudio);
    if (!opts_.uiTest) setGamePriority(cfg_.prioritize && glfwGetWindowAttrib(win_, GLFW_FOCUSED) != 0);
    audio_.setOutputDevice(cfg_.audioOutput);   // (Settings > DEVICES; "" the default)
    if (!opts_.uiTest) rawMouse_.start();   // (the UI test drives the mouse through GLFW)
    if (!audio_.start()) printf("audio: no output device - running without sound\n");
    audio_.setMasterVolume(cfg_.volume);
    audio_.setEffectsVolume(cfg_.volumeEffects);
    audio_.setAnnouncerVolume(cfg_.volumeAnnouncer);
    audio_.setQuips(cfg_.quips);
    audio_.setQuipVolume(cfg_.volumeQuips);

    botMapIdx_ = bestMapFor(cfg_.botMode);
    if (!opts_.uiTest && opts_.screenshot.empty() && !opts_.benchmark) benchRetryUploads();   // (runs whose upload didn't get through)
    if (opts_.benchmark) benchStart();   // --benchmark: the main menu's, printed then quit
    else if (opts_.timeTrial) startTimeTrial();
    else if (opts_.vsBotsMode >= 0) {
        cfg_.botMode = opts_.vsBotsMode;
        botMapIdx_ = bestMapFor(cfg_.botMode);
        if (!opts_.hostMap.empty())
            for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == opts_.hostMap) botMapIdx_ = (int)i;
        startHost(true);
    } else if (opts_.hostMode >= 0) {
        hostModeIdx_ = opts_.hostMode;
        if (!opts_.hostMap.empty())
            for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == opts_.hostMap) hostMapIdx_ = (int)i;
        cfg_.hostUpnp = false;
        cfg_.hostPublic = false; // command-line test hosts stay off the internet list
        startHost();
    } else if (opts_.debugPicker) {
        setScreen(Screen::Host);
        variantCtx_ = 0;
        picker_ = opts_.debugPicker < 50 ? opts_.debugPicker : PICK_NONE; // 50+: just the host menu
    } else if (!opts_.connect.empty()) {
        connectTo(opts_.connect);
    }
    return true;
}

void Game::shutdown() {
    audio_.stop();
    if (mouseHeld_) rawMouse_.hold(win_, false);   // (let the cursor go)
    rawMouse_.stop();
    leaveGame();
    cfg_.save();
    if (raceWriter_.t.joinable()) raceWriter_.t.join();   // (the last finished run's line)
}

// ---------------------------------------------------------------------------
// hosting / connection

static std::string upper(std::string t);   // (below)

void Game::startHost(bool vsBots) {
    stopHost();
    if (maps_.empty()) { error_ = "no maps found in maps/"; return; }
    ServerConfig sc;
    sc.name = cfg_.hostName;
    sc.map = maps_[hostMapIdx_ % maps_.size()];
    sc.mode = hostModeIdx_;
    sc.port = (uint16_t)atoi(hostPortStr_.c_str());
    if (sc.port == 0) sc.port = kDefaultPort;
    sc.maxPlayers = cfg_.hostMaxPlayers;
    sc.upnp = cfg_.hostUpnp;
    sc.probe = true; sc.prober = cfg_.hostProber;   // tell the host whether friends can actually reach the port
    sc.balanceBots = cfg_.hostBalanceBots;
    sc.logLevel = cfg_.hostLog;
    sc.duelTeamSize = cfg_.hostDuelSize;
    sc.arenaVariant = cfg_.hostArenaVariant;
    sc.publicList = cfg_.hostPublic;
    sc.ranked = cfg_.hostRanked;
    if (!vsBots && !sc.ranked && !cfg_.hostCustom.empty()) { // a custom mode: its rules, its mode, a map from its pool
        if (loadCustomMode(cfg_.hostCustom, sc.custom)) {
            sc.mode = sc.custom.baseMode;
            if (!sc.custom.maps.empty() && std::find(sc.custom.maps.begin(), sc.custom.maps.end(), sc.map) == sc.custom.maps.end()) sc.map = sc.custom.maps[0];
        } else cfg_.hostCustom.clear();
    }
    if (!vsBots && !sc.ranked && !cfg_.hostBotBrain.empty()) {   // HOST > ADVANCED's bot brain (ranked: never; the server refuses too)
        std::string err;
        if (loadBrain(cfg_.hostBotBrain, &err)) sc.botBrain = cfg_.hostBotBrain;
        else { checkHostBrain(); notify("BOT BRAIN " + upper(cfg_.hostBotBrain) + ": " + hostBrainNote_, 6); }   // (hosts with the built-in one)
    }
    if (sc.ranked) { // 1v1 on a ladder ruleset (the 1v1 ruleset picked, if it's one of them)
        sc.mode = MODE_ARENA;
        if (!arenaVariantRanked(sc.arenaVariant)) sc.arenaVariant = 0;
    }
    if (vsBots && timeTrialStart_) { // solo ranked race: Viaduct, nobody else, rounds forever (each one is a run)
        timeTrialStart_ = false;
        sc.name = "time trial";
        sc.map = "viaduct";
        sc.mode = MODE_RACE;
        sc.port = 0;
        sc.upnp = false;
        sc.lanOnly = true;
        sc.publicList = false;
        sc.maxPlayers = 1;
        sc.vsBots = true;
        sc.autoStart = true;
        sc.ranked = false;
        sc.timeTrial = true;
        sc.botsEnemy = sc.botsAlly = 0;
        sc.roundsToWin = 10000;
    } else if (vsBots) { // private local game: any free port, no port forwarding, fixed bots
        sc.name = "vs bots";
        sc.map = maps_[botMapIdx_ % maps_.size()];
        sc.mode = cfg_.botMode;
        sc.port = 0;
        sc.upnp = false;
        sc.lanOnly = true;
        sc.maxPlayers = kMaxPlayers;
        sc.vsBots = true;
        sc.ranked = false; // never ranked, whatever Host Game is set to
        sc.botSkill = cfg_.botSkill;
        sc.botsEnemy = cfg_.botEnemies;
        sc.botsAlly = modeIsTeamBased(sc.mode) ? cfg_.botAllies : 0;
        sc.botsEnemy = std::min(sc.botsEnemy, kMaxPlayers - 1 - sc.botsAlly);
        sc.arenaVariant = cfg_.botArenaVariant;
    }
    if (!testMap_.empty()) { // from the editor: a private play-test, or an edit session others can join
        sc = ServerConfig();
        sc.map = testMap_;
        sc.mode = testMode_;
        sc.maxPlayers = kMaxPlayers;
        if (testEdit_) {
            sc.name = cfg_.hostName + " (editing " + testMap_ + ")";
            sc.editMode = true;
            sc.port = (uint16_t)atoi(hostPortStr_.c_str());
            if (sc.port == 0) sc.port = kDefaultPort;
            sc.upnp = cfg_.hostUpnp;
            sc.probe = true; sc.prober = cfg_.hostProber;
            sc.publicList = cfg_.hostPublic;
            sc.maxPlayers = cfg_.hostMaxPlayers;
        } else {
            sc.name = "map test";
            sc.port = 0;
            sc.upnp = false;
            sc.lanOnly = true;
            sc.publicList = false;
        }
        vsBots = true; // don't remember it as the host settings
    }
    if (uiTest_) { sc.upnp = false; sc.publicList = false; }   // (the UI test stays on this machine; its probe asks 127.0.0.1:9: "not reachable")
    host_ = std::make_unique<Server>();
    host_->setLog([](const std::string& s) { printf("[host] %s\n", s.c_str()); });
    std::string e;
    if (!host_->start(sc, &e)) {
        error_ = "could not host: " + e;
        host_.reset();
        if (!testMap_.empty()) { std::string m = testMap_; testMap_.clear(); testEdit_ = false; openEditor(m); edNotify(error_); return; }
        setScreen(vsBots ? Screen::VsBots : Screen::Host);
        return;
    }
    if (!vsBots) {
        cfg_.hostMap = sc.map;
        cfg_.hostMode = sc.mode;
        cfg_.hostPort = sc.port;
    }
    cfg_.save();
    hostQuit_ = false;
    Server* srv = host_.get();
    hostThread_ = std::thread([this, srv] { raiseThreadPriority(); srv->run(hostQuit_); });
    connectTo("127.0.0.1:" + std::to_string(host_->port()));
    host_->setHostNonce(net_.sessionNonce()); // this client (only) is admin on its own server
}

void Game::stopHost() {
    if (!host_) return;
    hostQuit_ = true;
    if (hostThread_.joinable()) hostThread_.join();
    host_->stop();
    host_.reset();
}

void Game::connectTo(const std::string& rawAddr) {
    error_.clear();
    std::string addr = rawAddr; // pasted addresses often carry spaces / newlines
    while (!addr.empty() && (unsigned char)addr.back() <= ' ') addr.pop_back();
    while (!addr.empty() && (unsigned char)addr[0] <= ' ') addr.erase(0, 1);
    if (!net_.connect(addr, cfg_.name)) { error_ = net_.error(); return; }
    edSession_ = edOnline_ = false;
    unratedServer_ = false;
    rankBefore_ = rankGamesBefore_ = -1;
    if (!host_) { cfg_.lastAddress = addr; cfg_.save(); }
    setScreen(Screen::Connecting);
}

void Game::leaveGame(const std::string& error) {
    if (edSession_ && net_.mapReady()) {
        // leaving an edit session: offer to keep a copy (never saved without asking, B3). Someone else's session: always
        // (unless it's a built-in map nobody changed). Your own: when it's a built-in map you changed, which the server
        // never saves (a session on your own map is autosaved as it goes).
        const Map& m = net_.map();
        bool official = isOfficialMapName(m.name), changed = !official || !isOfficialMap(m);
        if (changed && (!host_ || official)) {
            edPrompt_ = true;
            edPromptMap_ = m;
            edPromptOfficial_ = official;
            edPromptThen_ = ED_THEN_NONE;
            edPromptName_ = official ? edFreshName(m.name) : m.name;
        }
    }
    bool wasEditing = edOnline_;
    edSession_ = edOnline_ = false;
    bench_.phase = BenchRun::OFF;   // (a benchmark that lost its game: nothing saved)
    stopBake();
    net_.disconnect();
    stopHost();
    setActiveRules(CustomRules());   // back to the standard rules (a custom server set its own)
    annPrevValid_ = false;
    rulesParts_.clear();
    error_ = error;
    paused_ = chatting_ = console_ = false;
    // in-game menus start closed next time (a dropped connection mid-menu left them open)
    if (widgetEdit_) widgets_.save(activeHudPath());
    hudEditClose();
    if (settingsInGame_) cfg_.save();
    settingsInGame_ = voteMenu_ = specPick_ = queueMenu_ = fontsOpen_ = devicesOpen_ = widgetEdit_ = false;
    setTab_ = TAB_PLAYER; controlsSub_ = 0;
    ui_.closePopup();
    picker_ = PICK_NONE;
    filePick_ = 0;
    captureAction_ = -1;
    killFeed_.clear(); chatFeed_.clear(); chatLog_.clear(); notices_.clear();
    for (int i = 0; i < kMaxPlayers; i++) { streak_[i] = 0; typingUntil_[i] = 0; }
    tracers_.clear(); explosions_.clear(); lastProjectiles_.clear();
    loadedMapGen_ = 0;
    if (!testMap_.empty()) { std::string m = testMap_; testMap_.clear(); testEdit_ = false; openEditor(m); return; } // back to the editor (autosaved)
    setScreen(Screen::Main);
    if (wasEditing) loadMenuBackdrop(); // the editor dropped the baked world
}

void Game::setScreen(Screen s) {
    screen_ = s;
    uiIn_.pressed = uiIn_.rpressed = false; // the click that changed screens doesn't also press whatever is under it on the new one
    ui_.clearFocus();
    captureMouse(s == Screen::InGame && !paused_ && !chatting_);
    if (s == Screen::Join) { browser_.refresh(); joinScroll_ = 0; }
    if (s == Screen::Demos) demoScroll_ = 0;
    if (s == Screen::Benchmarks) { benchScroll_ = 0; benchListAt_ = 0; benchRetryUploads(); }
    if (s == Screen::Host || s == Screen::VsBots) rescanMaps();
    if (s == Screen::Host) refreshHostBrains();
    if (s != Screen::Host) hostAdvanced_ = false;
}

void Game::refreshHostBrains() {
    hostBrains_ = listBrains();
    if (!cfg_.hostBotBrain.empty() && std::find(hostBrains_.begin(), hostBrains_.end(), cfg_.hostBotBrain) == hostBrains_.end())
        cfg_.hostBotBrain.clear();   // (its file is gone: the built-in brain, as hosting would fall back to anyway)
    checkHostBrain();
}

void Game::checkHostBrain() {
    hostBrainNote_.clear();
    if (cfg_.hostBotBrain.empty()) return;
    std::string err;
    if (loadBrain(cfg_.hostBotBrain, &err)) return;
    // err: "<path>:<line>:<col>: <message>" (or the file can't be read); the menu shows the line, the console all of it
    printf("bot brain %s: %s\n", cfg_.hostBotBrain.c_str(), err.c_str());
    const std::string path = userDataPath("bots") + "/" + cfg_.hostBotBrain + ".qbrain:";
    int line = err.compare(0, path.size(), path) == 0 ? atoi(err.c_str() + path.size()) : 0;
    hostBrainNote_ = (line > 0 ? "ERROR ON LINE " + std::to_string(line) : std::string("CAN'T BE READ")) + " - BOTS PLAY THE BUILT-IN BRAIN";
}

void Game::captureMouse(bool on) {
    if (on == mouseCaptured_) return;
    mouseCaptured_ = on;
    // With the raw input thread: the cursor hidden and held at the window's centre (rawMouse_.hold), not GLFW's
    // captured mode, which re-centres it every frame. Without (not Windows): GLFW's captured mode and raw motion
    // (Windows sends a process's raw mouse input to one window: GLFW registering its own would take it from the thread).
    if (rawMouse_.active()) {
        glfwSetInputMode(win_, GLFW_CURSOR, on ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
        // (only with the focus: captured behind another window, after a bake or a connect, it took the other
        // program's cursor to the game's centre; readMouse holds it once the game is in front)
        const bool hold = on && glfwGetWindowAttrib(win_, GLFW_FOCUSED) != 0;
        rawMouse_.hold(win_, hold);
        mouseHeld_ = hold;
        mouseHeldAt_ = time_;
    } else {
        glfwSetInputMode(win_, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(win_, GLFW_RAW_MOUSE_MOTION, on ? GLFW_TRUE : GLFW_FALSE);
    }
    { double a, b, c; int n; rawMouse_.take(&a, &b, &n, &c); }   // (motion from before: not part of this)
    firstMouse_ = true;
}

static const char* kDiscordUrl = "https://discord.gg/RERYWNWSXg";

static std::string upper(std::string t) {
    for (auto& c : t) c = (char)toupper((unsigned char)c);
    return t;
}

// The connection's map changed (joined, map vote, the host switched): bake its lighting on a worker thread
// (PERF-3). Called every frame while in game; the frame loop goes on (network, input events, a loading bar) and
// the new world replaces the old one only once it's ready. A newer map arriving mid-bake cancels it and restarts.
void Game::loadMapIfNeeded() {
    if (!net_.mapReady()) return;
    const uint32_t gen = net_.mapGeneration();
    if (bake_) {
        if (!bake_->done.load()) { if (bake_->gen != gen) bake_->cancel = true; return; }
        bake_->th.join();
        std::unique_ptr<BakeJob> job = std::move(bake_);
        if (job->gen == gen && !job->out.bw.cancelled) {
            r_.uploadWorld(job->map, job->out);
            loadedMapGen_ = gen;
            setBackdropFrame(job->map);
            if (net_.playback()) net_.holdPlaybackClock();
            return;
        }
    }
    if (gen == loadedMapGen_) return;
    // a race map: your past runs now, behind the loading screen (it used to be read on the first checkpoint, mid-run)
    if (!raceRunsLoaded_ && !net_.map().checkpoints.empty()) loadRaceRuns();
    bake_ = std::make_unique<BakeJob>();
    bake_->map = net_.map();
    bake_->map.dynSolids.clear();   // (the payload cart's box where it was at join time: no shadow of it in the cached light)
    bake_->gen = gen;
    BakeJob* j = bake_.get();
    j->th = std::thread([j] {
        Renderer::prepareWorld(j->map, j->out, [j](float p) { j->progress = p; }, &j->cancel);
        j->done = true;
    });
}

void Game::stopBake() {
    if (!bake_) return;
    bake_->cancel = true;
    if (bake_->th.joinable()) bake_->th.join();
    bake_.reset();
}

void Game::drawBakeProgress(const std::string& name, float p) {
    drawBakeScreen(name, p);
    r_.flush2D();
    glfwSwapBuffers(win_);
}

void Game::drawBakeScreen(const std::string& name, float p) {
    r_.rect(0, 0, (float)r_.width(), (float)r_.height(), {0.05f, 0.06f, 0.08f, 1});
    float bw = r_.width() * 0.4f, bx = (r_.width() - bw) * 0.5f, by = r_.height() * 0.55f;
    r_.rect(bx, by, bw, 8, {0.2f, 0.2f, 0.25f, 1});
    r_.rect(bx, by, bw * p, 8, rgba(cfg_.colorFriendly));
    float ts = std::max(2.0f, std::round(r_.height() / 360.0f));
    std::string t = "BAKING LIGHT - " + upper(name);
    r_.text((r_.width() - r_.textWidth(t, ts)) * 0.5f, by - 30 * ts / 2, ts, t, kWhite);
}

// The map behind the main menu (lighting is baked once, then cached like any map).
void Game::rescanMaps() {
    std::string hostName = maps_.empty() ? cfg_.hostMap : maps_[hostMapIdx_ % maps_.size()];
    std::string botName = maps_.empty() ? "" : maps_[botMapIdx_ % maps_.size()];
    maps_ = listMaps();
    mapInfo_.clear();
    // each map's title / modes / ranked ladders: parsed once per file version (it used to load and parse every map,
    // collision grid and all, each time a menu, the editor or the in-game picker opened: a visible hitch)
    for (auto& name : maps_) {
        std::string path = mapFilePath(name);
        std::error_code ec;
        std::filesystem::path fp = std::filesystem::u8path(path);
        uintmax_t size = std::filesystem::file_size(fp, ec);
        if (ec) size = (uintmax_t)-1;
        auto mt = std::filesystem::last_write_time(fp, ec);
        long long mtime = ec ? -1 : (long long)mt.time_since_epoch().count();
        auto it = mapInfoCache_.find(path);
        if (it != mapInfoCache_.end() && it->second.size == size && it->second.mtime == mtime && size != (uintmax_t)-1) {
            mapInfo_.push_back(it->second.info);
            continue;
        }
        Map m;
        MapInfo mi;
        if (m.load(name)) {
            mi.title = m.title.empty() ? name : m.title;
            mi.preferredMode = m.preferredMode;
            for (int k = 0; k < MODE_COUNT; k++) if (m.supportsMode(k)) mi.modes |= 1u << k;
            for (int v = 0; v < 16; v++) if (isRankedMap(m, v)) mi.ranked |= (uint16_t)(1u << v);
        }
        mapInfoCache_[path] = {mtime, size, mi};
        mapInfo_.push_back(mi);
    }
    for (size_t i = 0; i < maps_.size(); i++) {
        if (maps_[i] == hostName) hostMapIdx_ = (int)i;
        if (maps_[i] == botName) botMapIdx_ = (int)i;
    }
    // (a map that's gone: back to the first; the pickers index mapInfo_ with these unguarded)
    if (hostMapIdx_ < 0 || hostMapIdx_ >= (int)maps_.size()) hostMapIdx_ = 0;
    if (botMapIdx_ < 0 || botMapIdx_ >= (int)maps_.size()) botMapIdx_ = 0;
}

void Game::loadMenuBackdrop() {
    if (uiTest_) return;   // (the UI test: menus over a plain background, no bake)
    std::string name = !opts_.backdrop.empty() ? opts_.backdrop : "clockwork";
    if (!menuMap_.load(name) && (maps_.empty() || !menuMap_.load(maps_[0]))) return;
    r_.loadWorld(menuMap_, [&](float p) { drawBakeProgress(menuMap_.name, p); });
    setBackdropFrame(menuMap_);
}

// ---------------------------------------------------------------------------
// input

void Game::onChar(unsigned cp) {
    if (swallowChar_) { swallowChar_ = false; return; }   // the character of the key press that opened the chat / console
    if (cp < 128) uiIn_.chars += (char)cp;
}

int Game::bestRankedMap(int ladder) const {
    int any = -1;
    for (size_t i = 0; i < mapInfo_.size(); i++) {
        if (!(mapInfo_[i].ranked >> ladder & 1)) continue;
        if (mapInfo_[i].ranked == (1u << ladder)) return (int)i;
        if (any < 0) any = (int)i;
    }
    return any < 0 ? 0 : any;
}

// The map built for this mode if there is one, else the first map that supports it.
int Game::bestMapFor(int mode) const {
    int fallback = -1;
    // from rescanMaps' map info (the same files' modes and preferred mode): this used to load and parse maps, collision
    // grid and all, until it found one, on the frame you picked a mode (in a match too)
    const bool info = mapInfo_.size() == maps_.size();
    for (size_t i = 0; i < maps_.size(); i++) {
        int preferred;
        if (info) {
            if (mode < 0 || mode >= MODE_COUNT || !(mapInfo_[i].modes >> mode & 1)) continue;
            preferred = mapInfo_[i].preferredMode;
        } else {
            Map m;
            if (!m.load(maps_[i]) || !m.supportsMode(mode)) continue;
            preferred = m.preferredMode;
        }
        if (preferred == mode || (mode == MODE_TAG && preferred == MODE_HIDE)) return (int)i; // tag plays on the hide and seek map
        if (fallback < 0) fallback = (int)i;
    }
    return fallback < 0 ? 0 : fallback;
}

bool Game::isDown(int action) const {
    if (testHeld_ >> action & 1) return true;   // (the UI test holding e.g. the scoreboard key; 0 in play)
    for (int k = 0; k < 2; k++) {
        int c = binds().code[action][k];
        if (c >= kMouseCode && c < kMouseCode + 16) { if (!(swallowedBtn_ >> (c - kMouseCode) & 1) && glfwGetMouseButton(win_, c - kMouseCode) == GLFW_PRESS) return true; }
        else if (c > 0 && c < kMouseCode && glfwGetKey(win_, c) == GLFW_PRESS) return true;
        else if ((c == kWheelUp || c == kWheelDown) && timeNow() < wheelHeldUntil_[c - kWheelUp]) return true;
    }
    return false;
}

// A key, mouse button or scroll step was pressed: rebinding capture, or the bound one-shot actions.
bool Game::pressInput(int code) {
    if (captureAction_ >= 0) {
        if (code == GLFW_KEY_ESCAPE) { captureAction_ = -1; return true; }
        int& slot = cfg_.binds.code[captureAction_][captureSlot_];
        slot = (code == GLFW_KEY_BACKSPACE || code == GLFW_KEY_DELETE) ? 0 : code;
        // An input does one thing: binding it here removes it from every other action (otherwise the
        // default wheel = next/prev weapon kept firing alongside e.g. wheel = jump).
        if (slot)
            for (int a = 0; a < A_COUNT; a++)
                for (int k = 0; k < 2; k++)
                    if ((a != captureAction_ || k != captureSlot_) && cfg_.binds.code[a][k] == slot) cfg_.binds.code[a][k] = 0;
        captureAction_ = -1;
        cfg_.save();
        return true;
    }
    const Binds& b = binds();
    if (b.has(A_PERF, code)) { const bool was = perfOn(); showPerf_ = !showPerf_; if (!was) pipeReset(); return true; }
    if (b.has(A_PERFMINI, code)) { const bool was = perfOn(); perfMini_ = !perfMini_; if (!was) pipeReset(); return true; }
    if (b.has(A_SCREENSHOT, code)) {   // screenshots/<date>_<time>.png in your data folder (was screenshot_<n>.png in
        // the working directory with n from 0 each launch: the first one of every session overwrote the last session's)
        const std::string dir = userDataPath("screenshots");
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
        time_t t = time(nullptr);
        char stamp[32];
        strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", localtime(&t));
        std::string name = std::string(stamp) + ".png";
        for (int k = 2; std::filesystem::exists(std::filesystem::u8path(dir + "/" + name), ec); k++)   // (two in one second)
            name = std::string(stamp) + "_" + std::to_string(k) + ".png";
        notify(r_.screenshot(dir + "/" + name) ? "SCREENSHOT SAVED: SCREENSHOTS/" + name : "COULDN'T SAVE THE SCREENSHOT", 3);
        return true;
    }
    if (screen_ != Screen::InGame || chatting_) return false;
    if (b.has(A_TOGGLECHAT, code)) { toggleChat(); return true; }
    if (b.has(A_VOTEYES, code)) { net_.sendCommand("vote yes"); return true; }
    if (b.has(A_VOTENO, code)) { net_.sendCommand("vote no"); return true; }
    if (b.has(A_VOTESTART, code)) { net_.sendCommand("callvote start"); return true; }
    if (b.has(A_QUIP, code) && !paused_) { net_.sendCommand("quip"); return true; }
    if (paused_) return false;
    if (b.has(A_CHAT, code)) {
        chatting_ = true;
        chatText_.clear();
        uiIn_.chars.clear();
        captureMouse(false);
        return true;
    }
    if (rpsPicking())   // the hide and seek tiebreak: slot keys 1 / 2 / 3 throw rock / paper / scissors
        for (int k = 0; k < 3; k++)
            if (b.has(slotAction(k), code)) {
                rpsMine_ = RPS_ROCK + k;
                net_.sendCommand(std::string("rps ") + rpsName(rpsMine_));
                audio_.play(SND_TYPING, 0.9f, 0.8f);
                return true;
            }
    // weapons outside this mode's pool (1v1 rulesets, duel modifiers) are skipped
    uint16_t pool = net_.mode().weaponMask;
    const int mode = net_.mode().mode;
    const PlayerSnap* self = net_.findPlayer(net_.clientId());
    bool emptyHanded = isItMode(mode) && !(self && (self->flags & PS_HASBOMB));   // hide and seek / tag: only "it" holds anything
    auto allowed = [&](int w) { return (!pool || (pool >> w & 1)) && weaponInMode(w, mode) && !emptyHanded && weaponAwayLeft(w) <= 0; }; // a thrown knife / syringe is away for a bit
    bool slotKey = false;
    for (int k = 0; k < WPN_COUNT; k++) // slot keys 1-9 and 0, in kWeaponSlots order (0: the syringe, or the locator)
        if (b.has(slotAction(k), code)) { slotKey = true; int w = kWeaponSlots[k]; if (allowed(w)) { wp_.want(w); return true; } }
    if (slotKey) return true;
    int cur = weaponSlot(wp_.desired >= 0 ? wp_.desired : heldWeapon_);
    for (int dir : {1, -1}) {
        if (!b.has(dir > 0 ? A_NEXTWEAPON : A_PREVWEAPON, code)) continue;
        for (int k = 1; k <= WPN_COUNT; k++) {
            int w = kWeaponSlots[(cur + dir * k + WPN_COUNT * 2) % WPN_COUNT];
            if (allowed(w)) { wp_.want(w); break; }
        }
        return true;
    }
    if (b.has(A_FIRE, code)) fireLatched_ = true; // a click shorter than a tick still fires
    // ...and so does a jump, dash or grapple tap (it went down and up between two ticks: no command saw it held)
    if (b.has(A_JUMP, code)) moveLatched_ |= BTN_JUMP;
    if (b.has(A_DASH, code)) { moveLatched_ |= BTN_DASH; dashHoldUntil_ = time_ + kDashPressBuffer; }
    if (b.has(A_GRAPPLE, code)) moveLatched_ |= BTN_GRAPPLE;
    return false;
}

void Game::onKey(int key, int action, int mods) {
    markInput();
    if (action == GLFW_RELEASE) { if (key == textOpenKey_) textOpenKey_ = -1; return; }
    if (action == GLFW_REPEAT && key == textOpenKey_) { swallowChar_ = true; return; }   // holding the key that opened the chat
    // Alt+Enter: borderless fullscreen <-> windowed (exclusive fullscreen counts as fullscreen)
    // (key repeats while held are swallowed too: they must not fall through as a plain Enter)
    if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && (mods & GLFW_MOD_ALT)) {
        if (action == GLFW_PRESS) {
            cfg_.displayMode = cfg_.displayMode == 0 ? 1 : 0;
            applyDisplayMode();
            cfg_.save();
        }
        return;
    }
    if (bench_.phase != BenchRun::OFF && (screen_ == Screen::InGame || screen_ == Screen::Connecting)) {      // BENCHMARK: Esc cancels, nothing else
        if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) benchCancel();
        return;
    }
    if (key == GLFW_KEY_BACKSPACE) uiIn_.backspace = true;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) uiIn_.enter = true;
    if (key == GLFW_KEY_TAB) uiIn_.tab = true;
    // clipboard: Ctrl+V / Cmd+V pastes into the focused text field or chat, Ctrl+C copies the field
    bool cmd = (mods & GLFW_MOD_CONTROL) || (mods & GLFW_MOD_SUPER);
    if (cmd && key == GLFW_KEY_V && (ui_.typing() || chatting_)) {
        if (const char* clip = glfwGetClipboardString(win_))
            for (const char* c = clip; *c; c++) if (*c >= 32 && *c < 127) uiIn_.chars += *c;
        return;
    }
    if (cmd && key == GLFW_KEY_C && (ui_.typing() || chatting_)) {
        if (console_ && chatting_ && (chatText_.empty() || (mods & GLFW_MOD_SHIFT))) {   // console: copy the whole log
            std::string all;
            for (auto& l : consoleLog_) all += l + "\n";
            glfwSetClipboardString(win_, all.c_str());
        } else if (chatting_) glfwSetClipboardString(win_, chatText_.c_str());
        else uiIn_.copy = true;
        return;
    }
    if (screen_ == Screen::Editor) {
        if (edOnline_ && chatting_) {   // chat while editing together: Enter sends, Esc cancels
            if (chatCaretKey(key)) return;
            if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) { chatting_ = false; chatText_.clear(); }
            else if (action == GLFW_PRESS && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) {
                if (!chatText_.empty()) net_.sendCommand(chatText_[0] == '/' ? chatText_.substr(1) : "say " + chatText_);
                chatText_.clear();
                chatting_ = false;
            }
            return;
        }
        if (edOnline_ && action == GLFW_PRESS && !ui_.typing()) {   // ...and the chat key and the vote keys
            const Binds& eb = binds();
            if (eb.has(A_CHAT, key)) { chatting_ = true; chatText_.clear(); openedTextBy(key); return; }
            if (eb.has(A_VOTEYES, key)) { net_.sendCommand("vote yes"); return; }
            if (eb.has(A_VOTENO, key)) { net_.sendCommand("vote no"); return; }
        }
        if (key == GLFW_KEY_ESCAPE) uiIn_.escape = true;
        if (ui_.typing()) { if (key == GLFW_KEY_ESCAPE) ui_.clearFocus(); return; }
        editorKey(key, mods); // presses and key repeat (hold an arrow to keep nudging)
        return;
    }
    if (hudEdit_ && setTab_ == TAB_HUD && (screen_ == Screen::Settings || (screen_ == Screen::InGame && settingsInGame_)) && !ui_.typing() &&
        !ui_.popupOpen() && key != GLFW_KEY_ESCAPE && hudEditKey(key, action, mods)) return;   // (the on-screen HUD editor's keys)
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE && ui_.typing() && !chatting_) { uiIn_.escape = true; return; } // just stop typing, stay on the screen
    bool typing = chatting_ || ui_.typing();
    if (captureAction_ >= 0 || !typing) {
        bool wasChatting = chatting_;
        if (pressInput(key)) { if (!wasChatting && chatting_) openedTextBy(key); return; }
    }
    if (screen_ == Screen::InGame && key == GLFW_KEY_GRAVE_ACCENT && (!chatting_ || console_)) { // the console: ` opens / closes
        console_ = !console_;
        chatting_ = console_;
        consoleWords_.clear();   // (setting names are re-listed when it opens)
        chatText_.clear();
        openedTextBy(key);
        captureMouse(!console_ && !paused_);
        return;
    }
    if (screen_ == Screen::InGame) {
        if (chatting_ && chatCaretKey(key)) return;
        if (chatting_ && console_) {
            if (key == GLFW_KEY_ESCAPE) { console_ = chatting_ = false; chatText_.clear(); captureMouse(!paused_); }
            else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                if (!chatText_.empty() && (consoleHistory_.empty() || consoleHistory_.back() != chatText_)) consoleHistory_.push_back(chatText_);
                if (consoleHistory_.size() > 50) consoleHistory_.erase(consoleHistory_.begin());
                consoleHistoryAt_ = -1;
                runConsole(chatText_); chatText_.clear();
            }
            else if (key == GLFW_KEY_TAB) consoleComplete();
            else if ((key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) && !consoleHistory_.empty()) {   // history
                int n = (int)consoleHistory_.size();
                consoleHistoryAt_ = key == GLFW_KEY_UP ? (consoleHistoryAt_ < 0 ? n - 1 : std::max(0, consoleHistoryAt_ - 1))
                                                       : (consoleHistoryAt_ < 0 ? -1 : consoleHistoryAt_ + 1);
                if (consoleHistoryAt_ >= n) consoleHistoryAt_ = -1;
                chatText_ = consoleHistoryAt_ < 0 ? "" : consoleHistory_[consoleHistoryAt_];
            }
            return;
        }
        if (chatting_) {
            if (key == GLFW_KEY_ESCAPE) { chatting_ = false; chatText_.clear(); captureMouse(!paused_); }
            else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                if (!chatText_.empty()) {
                    if (chatText_[0] == '/') net_.sendCommand(chatText_.substr(1));
                    else net_.sendCommand("say " + chatText_);
                }
                chatText_.clear();
                chatting_ = false;
                captureMouse(!paused_);
            }
            return;
        }
        if (key == GLFW_KEY_ESCAPE) {
            if (ui_.popupOpen()) { ui_.closePopup(); return; }   // (a colour picker / dropdown first)
            if (filePick_) { filePick_ = 0; return; }
            if (widgetEdit_) { widgetEdit_ = false; widgets_.save(activeHudPath()); return; }
            if (hudEdit_) { hudEditClose(); return; }   // (EDIT ON SCREEN: back to the HUD tab)
            if (fontsOpen_) { fontsOpen_ = false; cfg_.save(); return; }   // (FONT: back to PLAYER)
            if (devicesOpen_) { devicesOpen_ = false; cfg_.save(); return; }   // (DEVICES: back to VIDEO + AUDIO)
            if (settingsInGame_) { leaveSettings(); return; }   // (any tab: as BACK)
            if (voteMenu_ || queueMenu_ || picker_ != PICK_NONE) { voteMenu_ = queueMenu_ = specPick_ = false; picker_ = PICK_NONE; return; }
            paused_ = !paused_;
            captureMouse(!paused_);
            return;
        }
    } else if (key == GLFW_KEY_ESCAPE) {
        uiIn_.escape = true;
        if (edPrompt_ && screen_ == Screen::Main) { edPrompt_ = false; return; }   // "save your copy?": Esc = DON'T SAVE (it did nothing - UI test)
        if (ui_.popupOpen()) { ui_.closePopup(); return; }   // (a colour picker / dropdown first)
        if (filePick_) { filePick_ = 0; return; }
        if (widgetEdit_) { widgetEdit_ = false; widgets_.save(activeHudPath()); return; }
        if (hudEdit_) { hudEditClose(); return; }   // (EDIT ON SCREEN: back to the HUD tab)
        if (fontsOpen_) { fontsOpen_ = false; cfg_.save(); return; }   // (FONT: back to PLAYER)
        if (devicesOpen_) { devicesOpen_ = false; cfg_.save(); return; }   // (DEVICES: back to VIDEO + AUDIO)
        if (screen_ == Screen::Settings) { leaveSettings(); return; }   // (any tab: as BACK)
        if (picker_ != PICK_NONE) { picker_ = PICK_NONE; return; }
        if (screen_ == Screen::Leaderboard && raceStatsView_) { raceStatsView_ = false; return; } // race stats: back to the board
        if (screen_ == Screen::CustomMode) { setScreen(Screen::Host); return; }                  // same as its BACK (unsaved changes dropped)
        if (screen_ == Screen::Host && hostAdvanced_) { hostAdvanced_ = false; cfg_.save(); return; }   // ADVANCED: back to HOST, as its BACK
        if (screen_ == Screen::BenchmarkResult) { setScreen(Screen::Benchmarks); return; }   // (as its BACK; a name being typed is saved as the field closes)
        if (screen_ == Screen::Join || screen_ == Screen::Host || screen_ == Screen::VsBots || screen_ == Screen::Leaderboard || screen_ == Screen::Demos || screen_ == Screen::Info || screen_ == Screen::Benchmarks) {
            cfg_.save();
            setScreen(Screen::Main);
        } else if (screen_ == Screen::Connecting) {
            leaveGame();
        }
    }
}

void Game::onFocus(bool focused) {
    if (focused) focusGained_ = true; else swallowedBtn_ = 0;   // (let go while away: no release comes)
    if (!opts_.uiTest) setGamePriority(cfg_.prioritize && focused);   // (alt-tabbed: the other programs get their share back)
}

void Game::onMouseButton(int button, int action) {
    // The click that brings the game back from another window (alt-tab, then a click on it) fired the weapon: a
    // press in the same event poll as the focus coming back is the window's, and its button isn't down until let go.
    if (button >= 0 && button < 16) {
        if (action == GLFW_RELEASE && (swallowedBtn_ >> button & 1)) { swallowedBtn_ &= (uint16_t)~(1u << button); return; }
        if (action == GLFW_PRESS && focusGained_ && screen_ == Screen::InGame && !paused_ && !chatting_) { swallowedBtn_ |= (uint16_t)(1u << button); return; }
    }
    markInput();
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) { uiIn_.pressed = captureAction_ < 0; uiIn_.down = true; }
        else if (action == GLFW_RELEASE) uiIn_.down = false;
    }
    if (screen_ == Screen::Editor) { // right button held: mouse look + fly (over the side panel: the UI's, a slider's reset)
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS && uiIn_.mx >= edPanelX_ && !edLook_) { uiIn_.rpressed = true; return; }
            edLook_ = action == GLFW_PRESS; captureMouse(edLook_);
        }
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS && (screen_ != Screen::InGame || paused_) && captureAction_ < 0) { uiIn_.rpressed = true; return; }
    if (action == GLFW_PRESS) {
        // in menus the left button belongs to the UI (except when rebinding)
        bool uiClick = button == GLFW_MOUSE_BUTTON_LEFT && (screen_ != Screen::InGame || paused_) && captureAction_ < 0;
        if (!uiClick) pressInput(kMouseCode + button);
    }
}

// Mouse motion (counts) into the view.
void Game::turn(double dx, double dy) {
    float k = deg2rad(cfg_.sensitivity) * 0.01f * 2.2f;
    if (scoped_) k *= std::tan(deg2rad(viewFov()) * 0.5f) / std::tan(deg2rad(cfg_.fov) * 0.5f); // same feel when zoomed
    yaw_ -= (float)dx * k;
    pitch_ = clampf(pitch_ - (float)dy * k, -1.55f, 1.55f);
    yaw_ = std::remainder(yaw_, 2 * PI);
}

// The mouse's motion since the last frame, from the raw input thread (rawmouse.h): with a fast mouse GLFW's event
// poll handled every report as its own message (60-80 us a frame at 8 kHz on the owner's PC). Called before that poll
// and again just before the camera in ULTRA. Without the thread (not Windows), GLFW's raw motion comes via onCursor.
// late: the late latch's and ULTRA's reads, inside the frame: only the motion (the focus can't change before the next
// event poll, and the cursor hold's syscalls stay out of the stretch between the read and the swap).
void Game::readMouse(bool late) {
    if (!rawMouse_.active()) return;
    double dx = 0, dy = 0, firstAt = 0;
    int n = 0;
    // The window must have the focus: the thread hears the mouse whatever's in front (alt-tabbed: nothing turns, and
    // the cursor is let go; back: held again). Re-held twice a second in case the window moved or Windows let it go
    // (and at once after a resize or a display mode change: mouseHeld_ cleared there).
    if (!late) {
        mouseFocused_ = glfwGetWindowAttrib(win_, GLFW_FOCUSED) != 0;
        if (mouseCaptured_ && mouseFocused_ && (!mouseHeld_ || time_ - mouseHeldAt_ > 0.5)) { rawMouse_.hold(win_, true); mouseHeld_ = true; mouseHeldAt_ = time_; }
        else if (mouseHeld_ && (!mouseCaptured_ || !mouseFocused_)) { rawMouse_.hold(win_, false); mouseHeld_ = false; }
    }
    if (!rawMouse_.take(&dx, &dy, &n, &firstAt) || !mouseCaptured_ || !mouseFocused_) return;   // (motion meanwhile: dropped)
    // (F3: when it arrived; 0: a report raced the take and lost its stamp, motion kept)
    if (firstAt > 0 && (pendingInput_ < 0 || firstAt < pendingInput_)) pendingInput_ = firstAt;
    mouseEventsPoll_ += n;   // (F3: the mouse's report rate)
    turn(dx, dy);
}

void Game::onCursor(double x, double y) {
    markInput();
    if (mouseCaptured_) {
        if (!rawMouse_.active()) mouseEventsPoll_++;   // (F3: the mouse's report rate)
        if (firstMouse_) { lastMx_ = x; lastMy_ = y; firstMouse_ = false; return; }
        double dx = x - lastMx_, dy = y - lastMy_;
        lastMx_ = x; lastMy_ = y;
        if (rawMouse_.active()) return;   // (the raw input thread has the motion: this is GLFW's cursor, not raw)
        turn(dx, dy);
    } else {
        // UI coordinates are framebuffer pixels (handles HiDPI).
        int ww, wh, fw, fh;
        glfwGetWindowSize(win_, &ww, &wh);
        glfwGetFramebufferSize(win_, &fw, &fh);
        uiIn_.mx = (float)(x * fw / std::max(1, ww));
        uiIn_.my = (float)(y * fh / std::max(1, wh));
    }
}

void Game::onResize(int w, int h) { r_.resize(w, h); mouseHeld_ = false; vmStale_ = true; }   // (the cursor held at the new centre; the mode read again)

void Game::onScroll(double dy) {
    markInput();
    if (dy == 0) return;
    if (screen_ == Screen::Editor) { edSpeed_ = clampf(edSpeed_ * (float)std::pow(1.25, dy), 2, 80); return; }
    if (screen_ == Screen::Info) { infoScroll_ = std::max(0, infoScroll_ - (dy > 0 ? 3 : -3)); return; }   // (3 lines a notch; clamped when drawn)
    if (screen_ == Screen::Benchmarks) { benchScroll_ = std::max(0, benchScroll_ - (dy > 0 ? 1 : -1)); return; }   // (clamped when drawn)
    if (screen_ == Screen::Demos && picker_ == PICK_NONE) { demoScroll_ = std::max(0, demoScroll_ - (dy > 0 ? 1 : -1)); return; }   // clamped when drawn
    if (screen_ == Screen::Join) { joinScroll_ = std::max(0, joinScroll_ - (dy > 0 ? 1 : -1)); return; }
    if (hudEdit_ && setTab_ == TAB_HUD && (screen_ == Screen::Settings || (screen_ == Screen::InGame && settingsInGame_))) { hudEdWheel_ += dy; return; }
    if (fontsOpen_ && (screen_ == Screen::Settings || (screen_ == Screen::InGame && settingsInGame_))) { fontScroll_ = std::max(0, fontScroll_ - (dy > 0 ? 1 : -1)); return; }   // (clamped when drawn)
    if (devicesOpen_ && (screen_ == Screen::Settings || (screen_ == Screen::InGame && settingsInGame_))) {   // (the list under the mouse)
        for (int k = 0; k < 3; k++) {
            const float* b = devListBox_[k];
            if (uiIn_.mx >= b[0] && uiIn_.mx < b[0] + b[2] && uiIn_.my >= b[1] && uiIn_.my < b[1] + b[3]) devScroll_[k] = std::max(0, devScroll_[k] - (dy > 0 ? 1 : -1));
        }
        return;   // (clamped when drawn)
    }
    // One action per whole notch: smooth-scrolling trackpads and high-res wheels send many small
    // steps, which used to skip through several weapons at once. Reversing direction starts over.
    if ((dy > 0) != (wheelAccum_ > 0)) wheelAccum_ = 0;
    wheelAccum_ += dy;
    while (std::fabs(wheelAccum_) >= 0.999) {
        int code = wheelAccum_ > 0 ? kWheelUp : kWheelDown;
        wheelAccum_ -= wheelAccum_ > 0 ? 1.0 : -1.0;
        wheelHeldUntil_[code - kWheelUp] = timeNow() + 0.03;
        pressInput(code);
        if (captureAction_ < 0 && screen_ != Screen::InGame) break;
    }
}

UserCmd Game::buildCmd() {
    UserCmd c;
    if (opts_.bench > 0) { yaw_ = (float)std::fmod(time_ * 0.6, 2 * PI); pitch_ = -0.15f; } // benchmark: a slow look around
    c.yaw = yaw_;
    c.pitch = pitch_;
    // a weapon key's choice goes out until the server has run it (also while paused / typing: the switch you pressed
    // still happens, as the view model already shows it)
    if (net_.hasSnapshot() && !net_.playback()) wp_.fill(c, net_.latest().ackCmdSeq, net_.nextSeq());
    bool block = paused_ || chatting_ || screen_ != Screen::InGame;
    if (block) { // keep key edge tracking current, so keys held through a menu aren't fresh presses after it
        socdPrev_[0] = isDown(A_FORWARD); socdPrev_[1] = isDown(A_BACK); socdPrev_[2] = isDown(A_RIGHT); socdPrev_[3] = isDown(A_LEFT);
        altPrev_ = isDown(A_ALTFIRE);
        fireLatched_ = false;   // (a click just before Esc / chat / console fired on the first tick after it)
        moveLatched_ = 0;
        dashHoldUntil_ = -1;
        return c;
    }
    // Opposite movement keys held together (SOCD): cancel out (default), or the last / first pressed wins.
    // Ranked always uses the default.
    bool ranked = fairRules();
    int socd = ranked ? 0 : modeCtlActive_ ? modeSocd_ : cfg_.socd;
    auto axis = [&](int pos, int neg, int& lastDir, bool& pPrev, bool& nPrev) {
        bool p = isDown(pos), n = isDown(neg);
        if (p && !pPrev) lastDir = 1;   // which key went down most recently
        if (n && !nPrev) lastDir = -1;
        pPrev = p; nPrev = n;
        if (p && n) return socd == 1 ? lastDir * 127 : socd == 2 ? -lastDir * 127 : 0;
        return p ? 127 : n ? -127 : 0;
    };
    int f = axis(A_FORWARD, A_BACK, socdLastF_, socdPrev_[0], socdPrev_[1]);
    int s = axis(A_RIGHT, A_LEFT, socdLastS_, socdPrev_[2], socdPrev_[3]);
    c.forward = (int8_t)clampf((float)f, -127, 127);
    c.side = (int8_t)clampf((float)s, -127, 127);
    if (isDown(A_JUMP)) c.buttons |= BTN_JUMP;
    if (isDown(A_USE)) c.buttons |= BTN_USE;
    if (isDown(A_DASH)) c.buttons |= BTN_DASH;
    if (isDown(A_GRAPPLE)) c.buttons |= BTN_GRAPPLE;
    c.buttons |= moveLatched_;   // (a tap since the last command: in this one)
    if (time_ <= dashHoldUntil_) c.buttons |= BTN_DASH;   // (a dash press up to 0.1 s before one is allowed: held till then, T2)
    moveLatched_ = 0;
    if (isDown(A_FIRE) || fireLatched_) c.buttons |= BTN_FIRE;
    bool alt = isDown(A_ALTFIRE), altEdge = alt && !altPrev_;
    altPrev_ = alt;
    if (!(modeCtlActive_ ? modeAlt_ : cfg_.altToggle) || ranked) altToggled_ = false;
    else { // tap to toggle alt-fire (scope) instead of holding it
        if (altEdge) altToggled_ = !altToggled_;
        if ((wp_.desired >= 0 && wp_.desired != heldWeapon_) || !net_.selfAlive()) altToggled_ = false; // switching weapons / dying drops the scope
        alt = altToggled_;
    }
    if (alt) c.buttons |= BTN_ALTFIRE;
    fireLatched_ = false;
    if (opts_.debugWeapon >= 1 && opts_.debugWeapon <= WPN_COUNT) c.weapon = (uint8_t)(opts_.debugWeapon - 1);
    if (opts_.debugScope) c.buttons |= BTN_ALTFIRE;
    if (opts_.debugFire) c.buttons |= BTN_FIRE;
    if (opts_.debugHook && !opts_.debugModels) c.buttons |= BTN_GRAPPLE;   // (with --models: only the lineup hangs on ropes)
    if (opts_.debugPulse && (std::fmod(time_, 0.6) < 0.3 || (shotAt_ > 0 && time_ > shotAt_ - 0.4))) c.buttons &= (uint16_t)~(BTN_FIRE | BTN_ALTFIRE);
    if (opts_.debugPulse && shotAt_ > 0 && time_ > shotAt_ - 0.4) c.side = 127;   // (then a step aside: a bolt seen end on is a dot)
    return c;
}

void Game::toggleChat() {
    cfg_.chatOff = !cfg_.chatOff;
    if (cfg_.chatOff) for (auto& t : typingUntil_) t = 0;
    cfg_.save();
    const int* k = binds().code[A_TOGGLECHAT];
    notify(cfg_.chatOff ? "CHAT OFF - " + inputName(k[0] ? k[0] : k[1]) + " TO SHOW IT AGAIN" : "CHAT ON", 2);
}

void Game::notify(const std::string& s, float seconds) {
    notices_.push_back({s, kWhite, time_ + seconds});
    while (notices_.size() > 4) notices_.pop_front();
}

bool Game::isFriendly(int id) const {
    if (id == net_.clientId()) return true;
    const PlayerSnap* me = net_.findPlayer(net_.clientId());
    const PlayerSnap* p = net_.findPlayer(id);
    if (!me || !p || !net_.hasSnapshot()) return false;
    if (!modeIsTeamBased(net_.mode().mode)) return false;
    int myTeam = me->team() == TEAM_SPECTATOR ? TEAM_A : me->team();
    return p->team() == myTeam;
}

// The announcer: 3-2-1 and "fight" as each round starts, and (setting) the mode's calls as they happen, read off
// the mode state each frame (bomb planted / defused, the hill taken / lost, a point taken / lost, overtime).
void Game::updateAnnouncer() {
    if (!net_.hasSnapshot()) return;
    const ModeState& ms = net_.mode();
    auto say = [&](int id, int level) { if (cfg_.announcer >= level) audio_.play(id, 1.0f); };
    if (ms.phase == PHASE_FREEZE) {
        int c = (int)std::ceil(ms.timeLeft - 0.02f);
        if (c != annCount_ && c >= 1 && c <= 3) say(SND_VOICE_ONE - (c - 1), 1);
        annCount_ = c;
    } else annCount_ = 0;
    if (annPrevValid_) {
        const ModeState& pv = annPrev_;
        const PlayerSnap* me = net_.findPlayer(net_.clientId());
        int myTeam = me && me->team() != TEAM_SPECTATOR ? me->team() : -1;
        if (pv.phase == PHASE_FREEZE && ms.phase == PHASE_LIVE) say(SND_VOICE_GO, 1);
        if (pv.phase != PHASE_LIVE && ms.phase == PHASE_LIVE) liveSeenAt_ = time_;
        if (pv.mode == ms.mode && pv.round == ms.round && ms.phase == PHASE_LIVE) {
            if (ms.mode == MODE_BOMB && pv.bombState != ms.bombState) {
                if (ms.bombState == BOMB_PLANTED) say(SND_VOICE_BOMB_PLANTED, 2);
                else if (ms.bombState == BOMB_DEFUSED) say(SND_VOICE_BOMB_DEFUSED, 2);
            }
            if (ms.mode == MODE_KOTH && pv.hillOwner != ms.hillOwner && ms.hillOwner != 255 && myTeam >= 0)
                say(ms.hillOwner == myTeam ? SND_VOICE_HILL_TAKEN : SND_VOICE_HILL_LOST, 2);
            if (ms.mode == MODE_POINTS && ms.point > pv.point && myTeam >= 0)
                say(myTeam == ms.attackers ? SND_VOICE_POINT_TAKEN : SND_VOICE_POINT_LOST, 2);
            if ((ms.flags & MS_OVERTIME) && !(pv.flags & MS_OVERTIME)) say(SND_VOICE_OVERTIME, 2);
            if (!(ms.flags & MS_TIMETRIAL) && pv.phase == PHASE_LIVE && ms.timeLeft > 0) { // time left: 1 min, 30 s
                static const struct { float t; int snd; } marks[] = {{60, SND_VOICE_ONE_MINUTE}, {30, SND_VOICE_THIRTY_SECONDS}};
                for (auto& m : marks) if (pv.timeLeft > m.t && ms.timeLeft <= m.t && pv.timeLeft - ms.timeLeft < 2) say(m.snd, 2);
            }
            if (ms.mode == MODE_FFA && me && myTeam >= 0) {   // free for all: your own lead only (a clear one, not a tie; X10)
                int of = 0; bool tied = false;
                const bool lead = ffaPlace(net_.latest().players, net_.clientId(), &of, &tied) == 1 && !tied && me->score > 0;
                const int c = annLeader_ == -2 ? -1 : ffaLeadCall(annLeader_ == 1, lead);
                if (c == 1) { notify("YOU TOOK THE LEAD", 2); say(SND_VOICE_NEW_LEADER, 2); }
                else if (c == 0) notify("YOU LOST THE LEAD", 2);
                annLeader_ = lead ? 1 : 0;
            }
        }
        // bomb: your side's job as each round's freeze begins, a halftime swap, the bomb dropped / picked up; payload:
        // a checkpoint as the point taken / lost line (modecalls.h, X10)
        ModeCall calls[4];
        for (int k = 0, n = modeCalls(pv, ms, myTeam, SND_VOICE_POINT_TAKEN, SND_VOICE_POINT_LOST, calls, 4); k < n; k++) {
            if (calls[k].text[0]) notify(calls[k].text, 4);
            if (calls[k].voice >= 0) say(calls[k].voice, calls[k].level);
        }
    }
    {   // mode sounds the server sends no event for (modecalls.h): the bomb going off, "it" passed on in tag
        const int itNow = ms.mode == MODE_TAG ? tagItOf(net_.latest().players) : -1;
        if (annPrevValid_ && bombWentOff(annPrev_, ms))   // a deep, long boom at the bomb, louder than a rocket's (it ends the round)
            audio_.play3D(SND_EXPLOSION, ms.bombPos, 2.5f, 0.6f);
        const int tagger = annPrevValid_ ? tagPassFrom(annPrev_, ms, tagIt_, itNow, net_.latest().players) : -1;
        if (tagger >= 0 && itNow == net_.clientId()) {   // you're it: the stab, and the sting of something stuck on you
            audio_.play(SND_HURT, 1.0f);
            audio_.play(SND_STICKY_ATTACH, 1.0f, 1.6f);
        } else if (tagger >= 0 && tagger == net_.clientId()) {   // you tagged them: a heavy hit's sound (no EV_HIT for a tag)
            if (cfg_.hitsounds && hitGate_.pass(time_, SND_HIT_HEAVY)) audio_.play(SND_HIT_HEAVY, 0.9f);
        } else if (tagger >= 0) {   // someone else tagged: their hurt sound from the new "it"
            const PlayerSnap* v = net_.findPlayer(itNow);
            if (v && !v->hidden) audio_.play3D(SND_HURT, v->pos + Vec3(0, 1.2f, 0), 0.8f, 1.0f, net_.playerSoundPack(itNow));
        }
        tagIt_ = itNow;
    }
    if (ms.phase != PHASE_LIVE) annLeader_ = -2;   // (a new match: the first lead isn't "new")
    else if (annLeader_ == -2) annLeader_ = -1;
    // Killstreaks are per match and per player: a new match (back to warmup, or another mode) starts them again, and a
    // slot's new player doesn't inherit the last one's (they only reset on leaving the server, so a spree carried over)
    // (and as one starts: kills in warmup, or on the end screen, don't carry into it)
    if (annPrevValid_ && (ms.mode != annPrev_.mode || (annPrev_.phase != PHASE_WARMUP && ms.phase == PHASE_WARMUP) ||
                          ((annPrev_.phase == PHASE_WARMUP || annPrev_.phase == PHASE_MATCH_END) && ms.phase != annPrev_.phase) ||
                          ms.round < annPrev_.round))
        for (int& k : streak_) k = 0;
    for (int i = 0; i < kMaxPlayers; i++)
        if (streak_[i] && !net_.findPlayer(i)) streak_[i] = 0;
    annPrev_ = ms;
    annPrevValid_ = true;
}

Vec3 Game::teamColor(int id) const {
    if (net_.mode().mode == MODE_TAG) { // tag: the tagger alone is red (on every screen, theirs too), everyone else blue
        const PlayerSnap* p = net_.findPlayer(id);
        return p && (p->flags & PS_HASBOMB) ? Vec3(0.9f, 0.15f, 0.12f) : Vec3(0.2f, 0.42f, 1.0f);
    }
    return isFriendly(id) ? cfg_.colorFriendly : cfg_.colorEnemy;
}
const char* const kOutlineNames[kOutlineColors] = {"BLACK", "WHITE", "TEAM", "RED", "YELLOW", "GREEN", "CYAN", "MAGENTA"};
Vec3 Game::outlineColor(int id) const {
    static const Vec3 c[kOutlineColors] = {{0, 0, 0}, {1, 1, 1}, {0, 0, 0}, {1, 0.1f, 0.1f}, {1, 0.9f, 0.1f}, {0.1f, 1, 0.2f}, {0.1f, 0.95f, 1}, {1, 0.2f, 1}};
    int k = isFriendly(id) ? cfg_.outlineFriendly : cfg_.outlineEnemy;
    if (k < 0 || k >= kOutlineColors) k = 0;
    return k == 2 ? teamColor(id) : c[k];
}

// Kill feedback: your death, your style kills (headshot, air rocket, airborne), killstreaks (yours called out,
// anyone else's a sting and a notice).
void Game::onKillFeedback(int killer, int victim, int weapon, bool head, bool airborne) {
    const int me = net_.clientId();
    if (victim >= 0 && victim < kMaxPlayers) streak_[victim] = 0;
    if (victim == me) {
        audio_.play(SND_DEATH, 0.8f);
        if (cfg_.announcer >= 2) audio_.play(SND_VOICE_YOU_DIED, 1.0f);
    }
    if (killer < 0 || killer >= kMaxPlayers || killer == victim || (modeIsTeamBased(net_.mode().mode) && isFriendly(killer) == isFriendly(victim))) return;
    int n = ++streak_[killer];
    static const struct { int kills; int snd; const char* what; } streaks[] = {
        {3, SND_VOICE_SPREE, "IS ON A KILLING SPREE"}, {5, SND_VOICE_RAMPAGE, "IS ON A RAMPAGE"},
        {7, SND_VOICE_UNSTOPPABLE, "IS UNSTOPPABLE"}, {10, SND_VOICE_GODLIKE, "IS GODLIKE"}};
    int streakSnd = -1;
    for (auto& st : streaks) if (n == st.kills) {
        streakSnd = st.snd;
        if (killer != me) { notify(net_.playerName(killer) + " " + st.what, 3); audio_.play(SND_STREAK, 0.7f); }
        else notify(std::string("YOU'RE ") + (st.what + 3), 3);   // ("IS ..." -> "YOU'RE ...")
    }
    if (killer != me || cfg_.announcer < 2) return;
    if (streakSnd >= 0) audio_.play(streakSnd, 1.0f);
    else if (head) audio_.play(SND_VOICE_HEADSHOT, 1.0f);
    else if (airborne && weapon == WPN_ROCKET) audio_.play(SND_VOICE_AIR_ROCKET, 1.0f);
}

void Game::handleEvents() {
    net_.takeEvents(evScratch_);
    for (auto& e : evScratch_) {
        switch (e.type) {
            case EV_KILL: {
                const int wpn = e.c >= 0 ? (e.c & KILL_WEAPON) : e.c;
                std::string killer = e.a == 255 ? "" : net_.playerName(e.a);
                std::string t = killer.empty() || e.a == e.b ? net_.playerName(e.b) + " [" + weaponName(wpn) + "]"
                                                             : killer + " [" + weaponName(wpn) + "] " + net_.playerName(e.b);
                onKillFeedback(e.a, e.b, wpn, e.c >= 0 && (e.c & KILL_HEADSHOT), e.c >= 0 && (e.c & KILL_AIRBORNE));
                Color c = e.a == net_.clientId() ? rgba(cfg_.colorFriendly) : e.b == net_.clientId() ? rgba(cfg_.colorEnemy) : kWhite;
                Feed f{t, c, time_ + 6};
                Color nc;
                if (!killer.empty() && e.a != e.b && nameColorOf(e.a, nc)) f.spans.push_back({0, killer.size(), nc});
                if (nameColorOf(e.b, nc)) { size_t vn = net_.playerName(e.b).size(); f.spans.push_back({killer.empty() || e.a == e.b ? 0 : t.size() - vn, vn, nc}); }
                killFeed_.push_back(f);
                while (killFeed_.size() > 6) killFeed_.pop_front();
                if (e.b == net_.clientId() && e.a < kMaxPlayers && e.a != e.b) { specTarget_ = e.a; killCamUntil_ = time_ + kKillCamTime; }
                if (e.b == net_.clientId()) {   // the death screen: the victim's copy carries "<killer hp> <respawn ms>"
                    deathValid_ = true;
                    deathKiller_ = e.a < kMaxPlayers && e.a != e.b ? e.a : -1;
                    deathWeapon_ = wpn;
                    deathKillerHp_ = -1;
                    deathRespawnAt_ = -1;
                    int hp = -1, ms = -1;
                    if (!e.text.empty() && std::sscanf(e.text.c_str(), "%d %d", &hp, &ms) == 2) {
                        deathKillerHp_ = hp >= 0 && hp <= 9999 ? hp : -1;
                        if (ms >= 0 && ms <= 600000) deathRespawnAt_ = time_ + ms * 0.001;
                    }
                }
                if (e.a == net_.clientId() && e.b != e.a && cfg_.hitsounds) audio_.play(SND_KILL, 0.9f);
                if (e.a == net_.clientId() && e.b != e.a) killMarkUntil_ = time_ + 0.45;   // X6: a kill looks unlike a hit
                if (e.b != net_.clientId() && e.b < kMaxPlayers) {   // X6: where they fell (in sight only), a thud and a red burst
                    const PlayerSnap* v = net_.findPlayer(e.b);
                    if (v && !v->hidden) {
                        audio_.play3D(SND_DEATH, v->pos + Vec3(0, 1, 0), 0.8f, 1.0f, net_.playerSoundPack(e.b));
                        explosions_.push_back({v->pos + Vec3(0, 1, 0), time_, 0.35f, false, true});
                    }
                }
                break;
            }
            case EV_CHAT:   // (chat off: dropped, not just hidden - turning it back on doesn't replay what was missed)
                if (!cfg_.chatOff) {
                    Feed f{net_.playerName(e.a) + ": " + e.text, isFriendly(e.a) ? rgba(cfg_.colorFriendly) : rgba(cfg_.colorEnemy), time_ + 10, crownOf(e.a), true};
                    Color nc;
                    if (nameColorOf(e.a, nc)) f.spans.push_back({0, net_.playerName(e.a).size(), nc});
                    addChat(f);
                }
                break;
            case EV_MESSAGE:
                if (e.text.rfind(kUnratedNotice, 0) == 0) unratedServer_ = true;   // (a testing server: the HUD says NOT RATED)
                addChat({e.text, kGrey, time_ + 8});
                break;
            case EV_RANKED: {
                std::vector<std::string> ids, players;
                std::string ladder;
                for (int i = 0; i < kMaxPlayers; i++) ids.push_back(net_.rankId(i));
                std::string body = buildRankReport(e.text, ids, rankKey_, &players, &ladder, cfg_.name);
                if (body.empty()) break;
                if (!isRankedMap(net_.map(), arenaVariantFromName(ladder.c_str()))) { notify("NOT REPORTED - NOT A RANKED MAP FOR THIS LADDER", 6); break; } // a host on another map gets no result
                {   // the host's result must match the score we saw ourselves, else we don't vouch for it
                    std::vector<SeenPlayer> seen(kMaxPlayers);
                    for (int i = 0; i < kMaxPlayers; i++)
                        if (const PlayerSnap* ps = net_.findPlayer(i))
                            if ((ps->flags & PS_CONNECTED) && ps->team() != TEAM_SPECTATOR) seen[i] = {true, ps->team(), ps->score, ps->kills};
                    const ModeState& ms = net_.mode();
                    std::string why;
                    if (!rankedResultMatchesView(e.text, seen, modeIsTeamBased(ms.mode), ms.scoreA, ms.scoreB, &why)) {
                        // we don't vouch for it: the report says so, and the match becomes a dispute instead of being rated
                        rank_.submit(body + "&dispute=1", ladder, players);
                        notify("DISPUTED - THE HOST'S RESULT DOESN'T MATCH THE GAME (" + why + ")", 8);
                        break;
                    }
                }
                Rating r;
                if (rank_.rating(ladder, rankKey_.id, r)) { rankBefore_ = r.display(); rankGamesBefore_ = r.games; }
                rank_.submit(body, ladder, players);
                notify("RANKED MATCH OVER - REPORTING THE RESULT", 4);
                break;
            }
            case EV_QUIP: {   // someone's quip, from where they are: one of your voice's quips (never a call-out), QUIPS on
                // (e.b: the sender's pick, the same byte for everyone; each listener plays pick modulo their voice's set,
                // so everyone with the same voice and quip files hears the same quip)
                const int q = audio_.quipFor(e.b);
                if (!cfg_.quips || q < 0 || e.a >= kMaxPlayers) break;
                const int line = SND_QUIP + q;
                if (e.a == net_.clientId()) audio_.play(line, 0.9f);
                else if (const PlayerSnap* qp = net_.findPlayer(e.a); qp && !qp->hidden) audio_.play3D(line, qp->pos + Vec3(0, 1.6f, 0), 1.0f);
                break;
            }
            case EV_LOCATE: {   // the locator found someone: show them through walls for a second
                std::istringstream ls(e.text);
                Vec3 p;
                if (ls >> p.x >> p.y >> p.z) {
                    locatePos_ = p; locateId_ = e.a; locateUntil_ = time_ + kLocatorShow; audio_.play(SND_GRAPPLE_HIT, 0.6f, 1.4f);
                    locatorReady_ = std::max(locatorReady_, time_ + kLocatorCooldown - 0.1);   // (the server's clock for it, less the trip here)
                }
                break;
            }
            case EV_SOUND: {   // someone else's sound, from where they are (even out of sight): the seeker's / tagger's locator ping
                std::istringstream ls(e.text);
                Vec3 p;
                if (e.c == WPN_LOCATOR && e.a < kMaxPlayers && e.a != net_.clientId() && (ls >> p.x >> p.y >> p.z))
                    audio_.play3D(SND_LOCATOR, p + Vec3(0, 1.5f, 0), 1.0f, 1.0f, net_.playerSoundPack(e.a));   // (their pack, like their shots)
                break;
            }
            case EV_RPS: {   // the hide and seek tiebreak: who's in, who has chosen, the reveal
                RpsView v;
                v.stage = e.a; v.throwNo = e.b; v.at = time_;
                size_t nl = e.text.find('\n');
                std::istringstream rs(e.text.substr(0, nl));
                int bo3 = 0;
                rs >> bo3 >> v.winner;
                v.bestOf3 = bo3 != 0;
                std::string tok;
                while (rs >> tok) {
                    RpsView::P p;
                    int out = 0;
                    if (sscanf(tok.c_str(), "%d:%d:%d:%d", &p.id, &p.shape, &p.wins, &out) == 4 && p.id >= 0 && p.id < kMaxPlayers) { p.out = out != 0; v.players.push_back(p); }
                }
                v.result = nl == std::string::npos ? "" : e.text.substr(nl + 1);
                const int me = net_.clientId();
                auto mine = [&](const RpsView& r) -> const RpsView::P* { for (auto& p : r.players) if (p.id == me) return &p; return nullptr; };
                const RpsView::P* was = mine(rps_);
                const RpsView::P* now = mine(v);
                if (v.stage == RPS_PICK && (v.throwNo != rps_.throwNo || rps_.stage != RPS_PICK)) {   // a new throw
                    rpsMine_ = RPS_NONE;
                    audio_.play(SND_CHECKPOINT, 0.7f, v.throwNo == 1 ? 0.8f : 1.0f);
                } else if (v.stage == RPS_REVEAL && rps_.stage == RPS_PICK) {   // the reveal: a clack, then how it went for us
                    audio_.play(SND_BOLT_HIT, 0.9f, 0.9f);
                    if (now && was && now->out && !was->out) audio_.play(SND_HURT, 0.8f);
                    else if (now && was && now->wins > was->wins) audio_.play(SND_FINISH, 0.6f, 1.2f);
                }
                rps_ = v;
                break;
            }
            case EV_TYPING:   // someone's typing: soft key clicks from them and "..." over their head
                if (e.a < kMaxPlayers && !cfg_.chatOff) {   // (chat off: no "..." or clicks either)
                    typingUntil_[e.a] = time_ + 0.8;
                    const PlayerSnap* tp = net_.findPlayer(e.a);
                    if (tp && !tp->hidden) audio_.play3D(SND_TYPING, tp->pos + Vec3(0, 1.5f, 0), 0.6f, 0.9f + (qrand() % 20) * 0.01f);
                    else audio_.play(SND_TYPING, 0.4f);
                }
                break;
            case EV_HEALED:   // a teammate's syringe hit someone
                if (e.a == net_.clientId()) {
                    notify("HEALED BY " + net_.playerName(e.b) + " - COOLDOWNS RESET", 2); audio_.play(SND_SYRINGE, 0.8f, 1.3f);
                    localNextFire_ = localKnifeBack_ = localSyringeBack_ = 0;   // (mirror the server's reset: weapon, thrown knife / syringe)
                }
                else if (e.b == net_.clientId()) notify("YOU HEALED " + net_.playerName(e.a), 2);
                break;
            case EV_RACE_SPLIT:
                if (e.a == net_.clientId()) onRaceSplit(e.b, atoi(e.text.c_str()));
                break;
            case EV_RULES: { // a custom mode's rules, in pieces: play by them once they're all here
                if (e.b == 0 || e.a >= e.b) break;
                if ((int)rulesParts_.size() != e.b) rulesParts_.assign(e.b, std::string("\x01"));
                rulesParts_[e.a] = e.text;
                std::string all;
                bool done = true;
                for (auto& p : rulesParts_) { if (p == "\x01") { done = false; break; } all += p; }
                if (!done) break;
                CustomRules r;
                if (r.fromText(all)) { setActiveRules(r); notify("CUSTOM MODE: " + upper(r.name) + (r.label.empty() ? "" : " - LOGIC: " + r.label), 4); }
                rulesParts_.clear();
                break;
            }
            case EV_ROUND_WIN: { // the announcer: "you win / you lose" when the match ends, "round won / lost" between rounds
                const PlayerSnap* me = net_.findPlayer(net_.clientId());
                bool teams = modeIsTeamBased(net_.mode().mode) && net_.mode().mode != MODE_TOURNEY;   // (a = the winning team, else the winning player)
                if (me && me->team() != TEAM_SPECTATOR && e.a != 255) {
                    bool won = teams ? e.a == me->team() : e.a == net_.clientId();
                    if (e.b && cfg_.announcer >= 1) audio_.play(won ? SND_VOICE_YOU_WIN : SND_VOICE_YOU_LOSE, 1.0f);
                    else if (!e.b && teams && cfg_.announcer >= 2) audio_.play(won ? SND_VOICE_ROUND_WON : SND_VOICE_ROUND_LOST, 1.0f);
                    else if (!e.b && cfg_.announcer >= 2) audio_.play(SND_VOICE_ROUND_OVER, 1.0f);
                } else if (me && me->team() != TEAM_SPECTATOR && !e.b && cfg_.announcer >= 2 && (net_.mode().mode == MODE_PAYLOAD || net_.mode().mode == MODE_POINTS))
                    audio_.play(SND_VOICE_ROUND_OVER, 1.0f);   // (a stopwatch pair's first round: a mark to beat, nobody's won yet)
                centerText_ = e.text;
                centerUntil_ = time_ + (e.b ? 8.0 : 3.5);
                break;
            }
            case EV_GUNFIRE: {   // X9: a shot out of sight, roughly where: the weapon's sound, quieter and lower (muffled)
                Vec3 p;
                if (e.c >= 0 && e.c < WPN_COUNT && std::sscanf(e.text.c_str(), "%f %f %f", &p.x, &p.y, &p.z) == 3)
                    audio_.play3D(SND_RIFLE + e.c, p, 0.45f, 0.8f);
                break;
            }
            case EV_BRACKET: {
                parseBracket(e.text, bracket_, bracketRound_, bracketMatch_);
                for (int i = 0; i < kMaxPlayers; i++) { std::string n = net_.playerName(i); if (!n.empty()) bracketNames_[i] = n; }
                break;
            }
            case EV_VOTE:
                voteText_ = e.text;
                break;
            case EV_HIT:
                if (e.b == net_.clientId() && e.a != e.b && e.text.size() > 1 && e.text[0] == '@') {   // hit by someone: the arc
                    long x = 0, z = 0;
                    if (sscanf(e.text.c_str() + 1, "%ld %ld", &x, &z) == 2) {
                        DmgDir* slot = &dmgDirs_[0];   // (theirs again, else the oldest)
                        for (auto& d : dmgDirs_) { if (d.from == e.a) { slot = &d; break; } if (d.at < slot->at) slot = &d; }
                        *slot = {Vec3(x * 0.1f, 0, z * 0.1f), time_, e.a};
                    }
                    break;
                }
                if (e.a == net_.clientId()) {
                    float dmg = e.c / 100.0f; // hundredths of a point
                    bool head = e.text == "h"; // a headshot: its own red number
                    hitUntil_ = time_ + 0.15;
                    // four tiers by this hit's damage, each heavier and louder (hitSoundFor); hits landing together make one
                    if (cfg_.hitsounds) { int snd = hitSoundFor(dmg); if (hitGate_.pass(time_, snd)) audio_.play(snd, 0.9f); }
                    // hits on the same target in quick succession add up into one number
                    bool merged = false;
                    for (auto& d : dmgNums_)
                        if (d.victim == e.b && d.head == head && time_ - d.start < 0.35) { d.amount += dmg; d.start = time_; merged = true; }
                    if (!merged) {
                        const PlayerSnap* v = net_.findPlayer(e.b);
                        dmgNums_.push_back({e.b, dmg, time_, v ? v->pos : Vec3(), head});
                    }
                }
                break;
            case EV_SPAWN:
                if (e.a == net_.clientId()) {
                    for (auto& d : dmgDirs_) d = DmgDir();   // (the last life's hits)
                    yaw_ = e.c / 1000.0f;
                    pitch_ = 0;
                    if (opts_.spawnYaw > -999) { yaw_ = deg2rad(opts_.spawnYaw); pitch_ = deg2rad(opts_.spawnPitch); }
                }
                break;
        }
    }
}

// Knives, bolts and syringes stuck where they landed (NetClient::stuckProps, cosmetic): the flying projectile's model,
// still, along the way it flew, centred on the spot it hit (half of it in the wall / body). Off screen or far away:
// skipped. One in a player rides on them as a sticky does, and isn't drawn while they're out of sight.
void Game::drawStuckProps(const Vec3& eye, float alpha) {
    QZONE("drawStuckProps");
    const auto& props = net_.stuckProps();
    if (props.empty()) return;
    constexpr float kFar2 = 50.0f * 50.0f, kNear2 = 10.0f * 10.0f;   // (half a metre long: a few pixels at 50 m)
    const int me = net_.clientId();
    const bool hideOwn = net_.selfAlive() && !net_.playback();   // (in you: not drawn in your face)
    // One in a player rides on them as drawn this frame: framePlayers() is net_.attachedPos's interpolation, done
    // once (your own, the one case attachedPos treats apart, is never drawn: hideOwn). Team colours: once per owner.
    const auto& players = framePlayers();
    const float bs = net_.bodyScale();
    Vec3 ownerCol[kMaxPlayers];
    uint32_t haveCol = 0;
    for (const StuckProp& sp : props) {
        Vec3 pos = sp.pos;
        if (sp.player != PROP_WORLD) {
            if (hideOwn && sp.player == me) continue;
            if (sp.player >= kMaxPlayers) continue;
            const InterpPlayer& ip = players[sp.player];
            if (!ip.valid || !(ip.snap.flags & PS_ALIVE)) continue;   // (out of sight, or dead)
            pos = stickyOnPlayerPos(ip.snap.pos, sp.pos, (ip.snap.flags & PS_CROUCHED) != 0, bs);
        }
        const Vec3 d = pos - eye;
        const float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (d2 > kFar2 || !r_.inView(pos, 0.6f)) continue;
        const bool far = d2 > kNear2;
        const int mt = modelExists(MODEL_PROJECTILE, sp.type) ? sp.type : MODEL_PROJ_ORB;
        const float yaw = std::atan2(-sp.dir.x, -sp.dir.z), pitch = std::asin(clampf(sp.dir.y, -1, 1));
        ModelPalette ppal;
        if (sp.owner < kMaxPlayers) {
            if (!(haveCol >> sp.owner & 1)) { ownerCol[sp.owner] = teamColor(sp.owner); haveCol |= 1u << sp.owner; }
            ppal.c[TINT_TEAM] = ownerCol[sp.owner];
        } else ppal.c[TINT_TEAM] = teamColor(sp.owner);
        ppal.c[TINT_GLOW] = r_.glowColor;
        if (const ModelMesh* ov = modelOverride(MODEL_PROJECTILE, mt)) {
            Vec3 tint[TINT_COUNT];
            overrideTints(MODEL_PROJECTILE, mt, ppal, tint);
            r_.mesh(*ov, Mat4::translate(pos) * Mat4::rotY(yaw) * Mat4::rotX(pitch), tint, 1.0f);
            continue;
        }
        if (const ModelParts* bx = boxOverride(MODEL_PROJECTILE, mt)) overrideBoxParts(MODEL_PROJECTILE, mt, *bx, ppal, 1.0f, parts_);
        else projectileParts(mt, ppal, 1, 1, 0, parts_);
        for (const ModelPart& q : parts_) {   // (opaque parts only: a stuck one has no glow; far off: its main part only)
            if (q.alpha < 1) continue;
            r_.boxPitched(pos, q.c, q.h, yaw, pitch, q.color, 1.0f, false);   // (no shadow: see boxPitched)
            if (far) break;
        }
    }
}

// ---------------------------------------------------------------------------
// main loop

void Game::frame(double dt) {
    focusGained_ = false;   // (the event poll that saw the focus come back is over)
    QZONE("Game::frame");
    struct CpuTime { double t0 = timeNow(); double* out; ~CpuTime() { *out = timeNow() - t0; } } cpuTime{timeNow(), &lastCpu_}; // --bench: CPU part of the frame
    time_ += dt;
    if (cfg_.uiFont != fontTried_) applyUiFontSetting();   // (a string compare; the font is built only when it changed)
    if (g_monitorEvent) { g_monitorEvent = false; monitorsChanged(); }   // (a bool; set by a monitor plugged in or out)
    if (audio_.takeOutputMissing()) notify("THE CHOSEN AUDIO OUTPUT IS GONE: USING THE SYSTEM DEFAULT", 5);   // (an atomic, set by the audio thread)
    swallowChar_ = false;   // (a key that opened the chat and typed no character: don't eat a later one)
    framePlayersOk_ = false;
    if (screen_ == Screen::InGame && (!opts_.chatShot.empty() || !opts_.consoleShot.empty())) {
        bool con = !opts_.consoleShot.empty();
        chatting_ = true; console_ = con;
        chatText_ = con ? opts_.consoleShot : opts_.chatShot;
        if (con) { consolePrint("> help"); consoleComplete(); chatCursor_ = chatText_.size(); }
        else { addChat({"bot: nice shot", Color{1, 1, 1, 1}, time_ + 1e9}); chatSync(); chatCursor_ = chatText_.size() / 2; }
        chatSeen_ = chatText_;
        time_ = std::floor(time_); // caret visible
        opts_.chatShot.clear(); opts_.consoleShot.clear();
    }
    if (opts_.textHeavy && screen_ == Screen::InGame) {
        showPerf_ = true;
        while (chatFeed_.size() < 8) addChat({"Player" + std::to_string(chatFeed_.size()) + ": the quick brown fox jumps over the lazy dog 0123456789", Color{1, 1, 1, 1}, time_ + 1e9});
    }
    if (opts_.feedShot && screen_ == Screen::InGame && killFeed_.empty()) {   // (kill feed and chat lines that stay)
        static const char* kills[] = {"wraith [ROCKET] lynx", "player [RIFLE] ghost", "ghost [KNIFE] player", "ember [SNIPER] wraith"};
        for (const char* k : kills) killFeed_.push_back(Feed{k, std::string(k).rfind("player", 0) == 0 ? rgba(cfg_.colorFriendly) : kWhite, time_ + 1e9});
        addChat({"wraith: nice shot", Color{1, 1, 1, 1}, time_ + 1e9, 0, true});
        addChat({"ember: gg", Color{1, 1, 1, 1}, time_ + 1e9, 0, true});
    }
    if (opts_.debugPerf) { showPerf_ = true; if (pendingInput_ < 0) pendingInput_ = timeNow(); }
    if (opts_.debugPause && screen_ == Screen::InGame && !paused_) { paused_ = true; voteMenu_ = opts_.debugVote; opts_.debugPause = false; }
    if (opts_.debugSettings && screen_ == Screen::InGame && !settingsInGame_) {
        paused_ = true; settingsInGame_ = true;
        setTab_ = opts_.debugControls ? TAB_CONTROLS : opts_.debugCrosshair ? TAB_CROSSHAIR : opts_.debugWidgets || opts_.debugHudLayout ? TAB_HUD : TAB_PLAYER;
        widgetEdit_ = opts_.debugWidgets; widgetSel_ = opts_.debugWidgets ? 0 : -1; opts_.debugSettings = false;
        debugSettingsTab();
    }
    r_.setMsaa(cfg_.msaa);
    r_.setRenderScale(cfg_.renderScale, cfg_.renderSharpen);
    {   // per-mode HUD / controls rules follow the running mode (back to your defaults outside a game)
        // ranked never takes a rule: the default HUD and your own binds, no control scripts
        int m = net_.state() == ConnState::Connected && net_.hasSnapshot() && !fairRules() ? (int)net_.mode().mode : -1;
        if (m != appliedMode_) applyModeRules(m);
    }
    if (net_.state() == ConnState::Connected && net_.hasSnapshot() && time_ >= modelSentAt_ + 1.0 &&
        ((!fairRules() && net_.playerModel(net_.clientId()) != cfg_.model) || net_.playerSoundPack(net_.clientId()) != cfg_.soundPack || net_.playerNameColor(net_.clientId()) != cfg_.nameColor565())) {
        net_.sendCommand("model " + std::to_string(cfg_.model) + " " + std::to_string(cfg_.soundPack) + " " + std::to_string(cfg_.nameColor565())); // everyone sees / hears your picks (cosmetic only)
        modelSentAt_ = time_;
    }
    if (audio_.pack() != cfg_.soundPack) audio_.setPack(cfg_.soundPack);
    // input packets: one every tick (latency first, and each command rides in 4 packets against loss); batching
    // them per frame saved sendto calls but cut that redundancy to 2 and delayed commands by up to a tick
    net_.setInputBatch(1);
    net_.setReportHits(true);   // (your hits are yours: Server::clientHitReport)
    net_.setInterpFixed(fairRules() ? 0.010f : cfg_.interpMs > 0 ? cfg_.interpMs * 0.001f : cfg_.interpMs < 0 ? 0.0f : -1.0f);
    frameInput_ = pendingInput_; // the oldest input this frame will show
    pendingInput_ = -1;
    frameDt_ = dt;
    if (opts_.bench > 0 && screen_ == Screen::InGame && !bake_) benchFrame(dt);
    if (bench_.phase != BenchRun::OFF) benchFrameTime(dt);   // (BENCHMARK: one store a frame while it measures)
    frameTimes_[frameIdx_] = (float)dt;
    frameIdx_ = (frameIdx_ + 1) % frameTimes_.size();
    frameCount_ = std::min(frameCount_ + 1, frameTimes_.size());
    if (!polledEarly_) {   // (normally done by preInput, before this frame's input was read)
        if (bake_ && net_.playback()) net_.holdPlaybackClock();   // a demo waits for its map's bake
        QZONE("net poll"); net_.poll();
    }
    polledEarly_ = false;
    updateCommunity();
    if (!benchSending_.empty() && bench_.phase == BenchRun::OFF) benchUploads();   // (BENCHMARK: answers to the runs' uploads; never mid-run)
    if (((screen_ == Screen::InGame && !bake_) || screen_ == Screen::Editor || screen_ == Screen::Leaderboard || opts_.debugPicker || !opts_.backdrop.empty() || opts_.menuShot) && !opts_.screenshot.empty() && shotAt_ < 0) shotAt_ = time_ + opts_.shotDelay;

    if (screen_ == Screen::Join) browser_.poll();
    if (screen_ == Screen::Connecting || screen_ == Screen::InGame || (screen_ == Screen::Editor && edOnline_)) {
        if (net_.state() == ConnState::Disconnected) {
            bool demoEnd = net_.error() == "demo finished";
            leaveGame(net_.error().empty() ? "disconnected" : net_.error());
            if (demoEnd) { error_.clear(); setScreen(Screen::Demos); }
        } else if (net_.state() == ConnState::Connected && net_.mapReady() && (screen_ != Screen::Connecting || net_.hasSnapshot())) {
            bool editing = net_.hasSnapshot() && (net_.mode().flags & MS_EDITING);
            if (editing) { // an edit session (or the host switched back to editing)
                if (screen_ != Screen::Editor) enterOnlineEditor();
            } else {
                if (screen_ == Screen::Editor) { edOnline_ = false; loadedMapGen_ = 0; } // the host switched to playing it: bake it
                loadMapIfNeeded();
                if (screen_ != Screen::InGame) setScreen(Screen::InGame);
            }
        }
    }

    if (helloPending_ && rank_.helloDone()) { helloPending_ = false; cfg_.rankHello = rankKey_.id; cfg_.save(); }   // (any screen)
    if (screen_ == Screen::InGame) {
        handleEvents();
        updateAnnouncer();
        updateRanked();
        // fixed-rate input + prediction
        tickAcc_ += dt;
        if (tickAcc_ > 0.25) tickAcc_ = 0.25;
        if (net_.playback()) tickAcc_ = 0;   // watching a demo: no input
        const double period = net_.tickPeriod();
        const bool aliveBefore = fireAlive_;   // (alive through the last frame too: F18's spread below needs it)
        {   // just spawned: the server fires nothing for kSpawnFireLock, from its spawn. We see the spawn half a round trip
            // late and our commands reach it half a round trip after we send them: the lock here ends a round trip sooner
            // (a click before then played a shot the server never fired)
            const bool aliveNow = net_.selfAlive() && !net_.playback();
            if (aliveNow && !fireAlive_) {
                const PlayerSnap* mp = net_.findPlayer(net_.clientId());
                const double rtt = mp ? mp->ping * 0.001 : 0.0;
                const double lock = std::max(0.0, (double)kSpawnFireLock - rtt);
                if (time_ + lock > localNextFire_) { localNextFire_ = time_ + lock; localRefire_ = (float)lock; }
            }
            fireAlive_ = aliveNow;
        }
        QZONE("input + prediction ticks");
        // F18: this frame's turn spread over its ticks (clientfeel.h spreadYaw): the last tick has now's exact yaw, each
        // earlier one its share of the way there; a tick with fire, alt-fire or a grapple shot held, and every tick after
        // it, takes the exact aim (what you see is what you shoot). Not across a spawn or while dead (the yaw is set).
        const int frameTicks = (int)(tickAcc_ / period);
        const bool spread = frameTicks > 1 && aliveBefore && fireAlive_;
        const float yawFrom = spread ? tickYaw_ : yaw_;
        bool exactAim = !spread;
        int tickNo = 0;
        while (tickAcc_ >= period) {
            if (bake_) {   // the world isn't there yet: keep the connection alive, nothing else
                fireLatched_ = false;   // (no stray shot when the new map is ready)
                moveLatched_ = 0;
                UserCmd k; k.yaw = yaw_; k.pitch = pitch_;
                net_.tick(k);
                tickAcc_ -= period;
                continue;
            }
            UserCmd c = buildCmd();
            tickNo++;
            if (c.buttons & (BTN_FIRE | BTN_ALTFIRE)) exactAim = true;
            if ((c.buttons & BTN_GRAPPLE) && !(net_.predicted().flags & MF_HOOKED)) exactAim = true;
            if (!exactAim) c.yaw = spreadYaw(yawFrom, yaw_, tickNo, frameTicks);
            // the weapon the server will hold after this command (predicted: a switch shows now, not a round trip
            // later); switching with fire held locks fire until it's let go, as the server does (runPlayerCmd)
            bool switchLock = false;
            const int hw = heldWeapon_ = wp_.run(c, weaponView(), &switchLock);
            if (switchLock) localFireLocked_ = true;
            // local fire feedback (server is authoritative for damage)
            bool altShot = altFires(hw) && (c.buttons & BTN_ALTFIRE) && // knife throw / shotgun bounce (no throws in hide and seek / tag)
                           !(hw == WPN_KNIFE && net_.hasSnapshot() && isItMode(net_.mode().mode));
            bool wantFire = ((c.buttons & BTN_FIRE) != 0 || altShot) && holdFireLeft() <= 0 // (the map's holdfire: no shots yet)
                            && weaponAwayLeft(hw) <= 0;                        // (a thrown knife / syringe: disabled)
            if (net_.hasSnapshot() && isItMode(net_.mode().mode)) {   // hide and seek / tag: only "it" has the knife (no swing, no slash for the rest)
                const PlayerSnap* mp = net_.findPlayer(net_.clientId());
                if (!mp || !(mp->flags & PS_HASBOMB)) wantFire = false;
            }
            if (hw == WPN_LOCATOR) {   // the locator: a ping every kLocatorCooldown (the server answers with EV_LOCATE)
                if (wantFire && !localFireLocked_ && net_.selfAlive() && net_.mode().phase == PHASE_LIVE && time_ >= locatorReady_) {
                    locatorReady_ = time_ + kLocatorCooldown;
                    audio_.play(SND_LOCATOR, 0.8f);
                }
                wantFire = false;
            }
            if (localFireLocked_) { // mirror the server: after a knife throw or a switch, let go before the next weapon fires
                if (!(c.buttons & (BTN_FIRE | BTN_ALTFIRE))) localFireLocked_ = false;
                else wantFire = false;
            }
            if (switchLock || localFireLocked_ || hw != localTapWeapon_) localFireQueued_ = -1;
            if (tapBuffered(hw) && !localFireLocked_ && holdFireLeft() <= 0 && weaponAwayLeft(hw) <= 0) {   // mirror the server's tap buffer (T13)
                if (wantFire && !altShot && (c.buttons & BTN_FIRE) && !(localPrevButtons_ & BTN_FIRE) && time_ < localNextFire_) { localFireQueued_ = time_ + kTapBuffer; localTapWeapon_ = hw; }
                if (!wantFire && time_ <= localFireQueued_) wantFire = true;
            }
            if (weaponDef(hw).semiAuto) { // mirror the server: one shot per click, short buffer
                if (wantFire && !localFireHeld_) { localFireQueued_ = time_ + kFireBuffer; localTapWeapon_ = hw; }
                localFireHeld_ = wantFire;
                wantFire = time_ <= localFireQueued_;
            } else localFireHeld_ = wantFire;
            if (wantFire && net_.selfAlive() && time_ >= localNextFire_ && net_.mode().phase != PHASE_FREEZE) {
                localFireQueued_ = -1;
                int w = hw;
                bool alt = (c.buttons & BTN_ALTFIRE) != 0 && ((w != WPN_KNIFE && w != WPN_SYRINGE) || altShot); // same throw rule as above
                // the shot's place in a held burst, like the server's (it picks the spread: burstOffset, NET-9)
                bool held = (localPrevButtons_ & (altShot ? BTN_ALTFIRE : BTN_FIRE)) != 0;
                const bool cadence = held && time_ - localNextFire_ < 0.1;   // (held: on the beat, as the server keeps it)
                localBurst_ = cadence && w == localBurstWeapon_ ? localBurst_ + 1 : 0;
                localBurstWeapon_ = w;
                // the next shot one refire after this one's slot, not after this frame: re-basing on the frame's time made
                // every held shot up to a frame late, adding up (a rifle burst's last shot heard ~70 ms after the real one)
                localRefire_ = shotRefire(w, alt);   // (the HUD's weapon bar: this shot's own, alt-fire's too)
                localNextFire_ = (cadence ? localNextFire_ : time_) + localRefire_;
                if (isThrow(w, alt)) (w == WPN_KNIFE ? localKnifeBack_ : localSyringeBack_) = time_ + (w == WPN_KNIFE ? weaponAlt().knifeAway : weaponAlt().syringeAway);
                if (w != WPN_LASER || time_ >= nextLaserSound_) { // laser fires every 10 ms: sound at 20 Hz
                    audio_.play(SND_RIFLE + w, w == WPN_LASER ? 0.6f : 0.8f, 0.97f + (qrand() % 7) * 0.01f);
                    nextLaserSound_ = time_ + 0.05;
                }
                if (w == WPN_KNIFE && !alt) knifeSlash_[net_.clientId() % kMaxPlayers] = time_;
                if (isThrow(w, alt)) { localFireLocked_ = true; heldWeapon_ = wp_.thrown(weaponView()); }   // (and out comes the next weapon)
                recoil_ = w == WPN_LASER ? 0.15 : w == WPN_SNIPER ? 1.6 : 1.0;
                if (w != WPN_KNIFE && w != WPN_SYRINGE && w != WPN_LOCATOR) muzzleUntil_ = time_ + (w == WPN_SNIPER ? 0.07 : 0.05);
                if (w == WPN_ROCKET || w == WPN_CROSSBOW || (w == WPN_KNIFE && alt)) spawnPredictedProjectile(w, alt, c);   // X5
                if (!weaponDef(w).projectile && w != WPN_KNIFE && w != WPN_SYRINGE && w != WPN_LOCATOR && !(w == WPN_SHOTGUN && alt) &&
                    (w != WPN_LASER || time_ >= nextLocalSpark_) && cfg_.impactSparks)
                    localImpactSparks(w, c);   // X4: a spark on an enemy you hit, at once (the sound and number: the server's)
                if (!weaponDef(w).projectile && w != WPN_SYRINGE && w != WPN_LOCATOR && !(w == WPN_SHOTGUN && alt) && !isThrow(w, alt) &&
                    !net_.map().glass.empty())
                    predictShotGlass(w, c);
                if (!weaponDef(w).projectile && w != WPN_SYRINGE && w != WPN_LOCATOR && !(w == WPN_SHOTGUN && alt) && !isThrow(w, alt))
                    reportShotHits(w, c);   // (your hits count as you saw them; a ricochet's stay the server's)
                bool showShot = cfg_.ownShots == 1 || (cfg_.ownShots == 0 && !cfg_.hud[Settings::HUD_VIEWMODEL].hidden); // VISUALS > YOUR SHOTS
                if (w == WPN_LASER) laserUntil_ = showShot ? time_ + 0.07 : 0;
                else if (showShot && !weaponDef(w).projectile && w != WPN_KNIFE && w != WPN_SYRINGE) { // (knife throw: the projectile shows it)
                    const MoveState& m = net_.predicted();
                    Vec3 eye = m.pos + Vec3(0, ((m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
                    Vec3 aim = angleForward(yaw_, pitch_);
                    const WeaponDef& wd = weaponDef(w);
                    float spread = (wd.scopedSpread >= 0 && (c.buttons & BTN_ALTFIRE)) ? wd.scopedSpread : wd.spread;
                    for (int k = 0; k < wd.pellets; k++) { // the server's fixed spread pattern, so tracers show the real shot
                        Vec3 d = spreadAim(aim, spread, k, wd.pellets, localBurst_);
                        Vec3 from = muzzlePoint(w, eye);
                        if (w == WPN_SHOTGUN && alt) { // ricochet: draw each leg (players ignored, like the server's walls)
                            Vec3 o = eye;
                            float range = wd.range;
                            for (int b = 0; b <= weaponAlt().bounces && range > 0.01f; b++) {
                                Vec3 n;
                                float t = net_.map().raycast(o, d, range, &n);
                                Vec3 to = o + d * (t < 0 ? range : t);
                                tracers_.push_back({b ? o : from, to, time_ + 0.3});
                                if (t < 0) break;
                                if (cfg_.impactSparks) explosions_.push_back({to - d * 0.03f, time_, 0.06f, false, false, false, true, 0.2f});
                                o = to + n * 0.01f; d = d - n * (2.0f * dot(d, n)); range -= t;
                            }
                            continue;
                        }
                        float bangT, t = net_.map().raycastBullet(eye, d, wd.range, &bangT);
                        if (t < 0) t = wd.range;
                        tracers_.push_back({from, eye + d * t, time_ + (w == WPN_SNIPER ? 0.9 : 0.2)});
                    }
                }
            }
            localPrevButtons_ = c.buttons;
            {   // rocket riding: your rockets not handed over yet can be caught too (only while the grapple's held)
                NetClient::RocketCopy rc[HookProbe::kRockets];
                int n = 0;
                if ((c.buttons & BTN_GRAPPLE) && moveTuning().rocketRide)
                    for (const auto& q : predProj_)
                        if (q.live && !q.atWall && q.type == WPN_ROCKET && n < HookProbe::kRockets) {
                            const float g = projectileGravity(WPN_ROCKET, q.lob ? kRocketLobGravity : 1.0f);
                            Vec3 p = q.pos0, v = q.vel0;
                            projectileExtrapolate(p, v, g, (float)(time_ - q.born));
                            rc[n++] = {p, v, g};
                        }
                net_.setRocketCopies(rc, n);
            }
            net_.tick(c);
            if (net_.predicted().events & ME_DASH) dashHoldUntil_ = -1;   // (the press made its dash)
            if (net_.predicted().glassBrush >= 0) {   // (dashed or grappled through a pane: broken now, as the server will)
                const MoveState& pm = net_.predicted();
                net_.predictGlass(pm.glassBrush, pm.glassHow, pm.glassAt, pm.glassDir);
            }
            tickAcc_ -= period;
        }
        tickYaw_ = yaw_;   // (the last tick's: now's)
        if (net_.playback()) wp_.reset();   // (a demo: what the recorded player held)
        heldWeapon_ = wp_.current(weaponView());   // (no tick this frame: the last one's, or the snapshot's when dead)
        flushLaserHits(false);   // (the laser's last 50 ms of hits)
        net_.flushInput();   // (PERF-12: one input packet per frame, or per 2 ticks when a frame runs more)
        net_.updateVisualPos(bake_ ? 0.0 : tickAcc_, yaw_, pitch_);   // your own position as of now, for drawing
        pipeMark(PM_TICKS);
        framePlayersOk_ = false;   // (the ticks moved the render time on)
        scopeOverlay_ = heldWeapon_ == WPN_SNIPER; // crossbow: just a light zoom, normal crosshair
        {   // the zoom follows what buildCmd sends: with ALT TOGGLE, the toggled scope (it zoomed only while held, so the
            // view and the server's scoped spread disagreed after a tap)
            const bool tog = (modeCtlActive_ ? modeAlt_ : cfg_.altToggle) && !fairRules();
            scoped_ = net_.selfAlive() && !paused_ && !chatting_ && weaponDef(heldWeapon_).scopedSpread >= 0 &&
                      ((tog ? altToggled_ : isDown(A_ALTFIRE)) || opts_.debugScope);
        }
        // explosions: projectiles that vanished
        net_.interpolatedProjectiles(projScratch_);   // (projScratch_ and lastProjectiles_ trade memory: no allocations)
        const auto& projs = projScratch_;
        for (auto& old : lastProjectiles_) {
            bool still = false;
            for (auto& p : projs) still |= p.id == old.id;
            if (!still && net_.projectileHidden(old.id)) continue;   // out of sight now, still flying: not an explosion (NET-10)
            if (!still && old.owner == net_.clientId()) {   // yours: already shown at once (localBoom), if it blew up near there
                bool shown = false;
                for (size_t k = 0; k < localBooms_.size() && !shown; k++) {
                    const LocalBoom& b = localBooms_[k];
                    if ((b.id == old.id || (b.id == 0xFFFF && b.type == old.type)) && length(b.at - old.pos) < 4.0f) {
                        shown = true; localBooms_.erase(localBooms_.begin() + k);
                    }
                }
                if (shown) continue;
                bool mine = false;   // (one that blew up on your screen far from here: its hits went from there)
                for (auto& st : projStarts_) mine |= st.id == old.id && st.boomed;
                if (!mine && (old.type == WPN_ROCKET || old.type == WPN_CROSSBOW)) {
                    // the server's copy landed before yours did on your screen: its hits as you see them, from where it was
                    const float sp = length(old.vel);
                    const Vec3 dir = sp > 0.01f ? old.vel / sp : Vec3(0, 0, -1);
                    float et;
                    int victim = -1;
                    bool vHead = false;
                    const Vec3 from = old.pos - dir * 0.5f, to = old.pos + dir * 0.5f;
                    if (!enemyOnSegment(from, to, &et, &victim, &vHead)) victim = -1;
                    reportImpact(old.type, (old.flags & PROJ_LOB) != 0, from, victim >= 0 ? from + (to - from) * et : old.pos, victim, vHead);
                }
            }
            if (!still && old.type == WPN_BUBBLE) { explosions_.push_back({old.pos, time_, 0.45f, true}); audio_.play3D(SND_BUBBLE, old.pos, 0.7f, 1.7f, net_.playerSoundPack(old.owner)); } // pop
            else if (!still && weaponDef(old.type).splashRadius > 0) { explosions_.push_back({old.pos, time_}); audio_.play3D(SND_EXPLOSION, old.pos, 1.25f, 1.0f, net_.playerSoundPack(old.owner)); }
            else if (!still && old.type == WPN_CROSSBOW) audio_.play3D(SND_BOLT_HIT, old.pos, 0.8f, 1.0f, net_.playerSoundPack(old.owner));
            if (still && old.type == WPN_STICKY && length(old.vel) > 0.01f)
                for (auto& p : projs) if (p.id == old.id && length(p.vel) < 0.01f) audio_.play3D(SND_STICKY_ATTACH, p.pos, 0.8f, 1.0f, net_.playerSoundPack(old.owner));
        }
        for (auto& p : projs) {   // an enemy sticky just stuck on you: the beep (and "STICKY ON YOU" while it's there)
            if (!(p.flags & PROJ_ON_PLAYER) || p.onPlayer != net_.clientId()) continue;
            bool was = false;
            for (auto& old : lastProjectiles_) was |= old.id == p.id && (old.flags & PROJ_ON_PLAYER) && old.onPlayer == p.onPlayer;
            if (!was) audio_.play(SND_STICKY_ATTACH, 1.0f, 1.6f);
        }
        lastProjectiles_.swap(projScratch_);
        for (size_t k = 0; k < localBooms_.size();)   // (one the server never blew up near there: forgotten after 2 s)
            if (time_ - localBooms_[k].t > 2.0) localBooms_.erase(localBooms_.begin() + k); else k++;
        // your sticky detonate, at once: alt-fire with the launcher out sets off your armed stickies (the server's rule,
        // server.cpp; armed: seen for the arm time, so never one the server would still hold back)
        if (heldWeapon_ == WPN_STICKY && isDown(A_ALTFIRE) && !localFireLocked_ && net_.selfAlive() && !paused_ && !chatting_)
            for (auto& st : projStarts_) {
                if (st.boomed || time_ - st.seen < weaponAlt().stickyArmTime) continue;
                for (auto& p : lastProjectiles_)
                    if (p.id == st.id && p.type == WPN_STICKY) {
                        Vec3 at = p.pos;
                        if ((p.flags & PROJ_ON_PLAYER) && p.onPlayer == net_.clientId()) net_.attachedPos(p, 1.0f, &at);
                        localBoom(p.id, WPN_STICKY, at); st.boomed = true;
                    }
            }

        int hp = net_.selfHealth();
        {   // the hurt sound: one per 0.15 s at most, louder for more damage (clientfeel.h; it played on every drop)
            const bool hurt = hp < lastHealth_ && net_.selfAlive();
            if (hurt) damageUntil_ = time_ + 0.25;
            if (!net_.selfAlive()) hurtGate_.reset();
            else if (float v = hurtGate_.frame(time_, hurt ? lastHealth_ - hp : 0); v > 0) audio_.play(SND_HURT, v);
        }
        lastHealth_ = hp;
        if (net_.selfAlive()) deathValid_ = false;   // (the death screen is this death's only)
        if (!net_.selfAlive() && uiIn_.pressed && !paused_) {
            // cycle spectate target
            const auto& ps = framePlayers();
            for (int k = 1; k <= kMaxPlayers; k++) {
                int id = (specTarget_ + k + kMaxPlayers) % kMaxPlayers;
                if (ps[id].valid && (ps[id].snap.flags & PS_ALIVE) && id != net_.clientId() && (!modeIsTeamBased(net_.mode().mode) || isFriendly(id))) { specTarget_ = id; break; }
            }
        }
        captureMouse(!paused_ && !chatting_ && !bake_);
    }

    // ---- render ----
    if (screen_ == Screen::Editor) {
        if (!edScript_.empty()) edScriptStep();
        editorFrame(dt);
    } else if (screen_ == Screen::InGame && bake_) {
        drawBakeScreen(bake_->map.name, bake_->progress.load());
        // (a loading bar needs ~30 fps: the cores go to the bake; under software GL every frame costs them. In 1 ms
        // steps, ending as the bake does: a finished one waited up to 30 ms for the next frame, ~15 ms every load)
        for (int k = 0; k < 30 && bake_ && !bake_->done.load(); k++) sleepMs(1);
    } else if (screen_ == Screen::InGame && net_.hasSnapshot() && r_.worldLoaded()) {
        drawWorld((float)clampf((float)(tickAcc_ / net_.tickPeriod()), 0, 1));   // (and the HUD, before its late read)
    } else {
        // slow orbit around the backdrop map, looking slightly down at its center
        float ang = opts_.backdropAngle >= 0 ? deg2rad(opts_.backdropAngle) : (float)time_ * 0.04f;
        Vec3 camPos = bgCenter_ + Vec3(std::sin(ang) * bgRadius_, bgRadius_ * 0.6f, std::cos(ang) * bgRadius_);
        camPos.y = std::max(camPos.y, bgTop_ + 3.0f); // above the walls (pits make the bounds deep)
        Vec3 look = bgCenter_;
        if (bgIndoor_) { // under the roof: circle just inside the walls, high up, looking across the floor
            camPos = {bgCenter_.x + std::sin(ang) * (bgHalfX_ - 5), bgCeil_ - 5.5f, bgCenter_.z + std::cos(ang) * (bgHalfZ_ - 5)};
            look = {bgCenter_.x, 0, bgCenter_.z};
        }
        Vec3 to = look - camPos;
        if (opts_.camSet) r_.setCamera(opts_.camPos, deg2rad(opts_.camYaw), deg2rad(opts_.camPitch), 90);
        else r_.setCamera(camPos, std::atan2(-to.x, -to.z), std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)), 75);
        if (r_.worldLoaded()) { sceneSettings(); r_.drawScene(false); }
        else {
            r_.rect(0, 0, (float)r_.width(), (float)r_.height(), {0.05f, 0.06f, 0.08f, 1});
        }
    }
    if (opts_.backdrop.empty() || screen_ == Screen::InGame) drawMenu();
    std::string copied = ui_.takeCopy();
    if (!copied.empty()) glfwSetClipboardString(win_, copied.c_str());
    if (!hudBuilt_) {
        if (showPerf_) drawPerf();
        else if (perfMini_) drawPerfMini();
    }
    hudBuilt_ = false;
    { QZONE("flush2D"); QGPUZONE("GPU HUD"); r_.flush2D(); }
    pipeMark(PM_HUD);
    pipeGpuEnd();

    if (!benchShot_.empty()) {   // BENCHMARK > EXPORT PNG: the result page as drawn this frame
        if (screen_ == Screen::BenchmarkResult) notify((r_.screenshot(benchShot_) ? "SAVED " : "COULD NOT WRITE ") + upper(benchShot_), 6);
        benchShot_.clear();
    }
    if (shotAt_ > 0 && time_ >= shotAt_ && (screen_ == Screen::InGame || screen_ == Screen::Editor || screen_ == Screen::Leaderboard || opts_.debugPicker || !opts_.backdrop.empty() || opts_.menuShot)) {
        r_.screenshot(opts_.screenshot);
        printf("screenshot written to %s\n", opts_.screenshot.c_str());
        quit_ = true;
    }
    uiIn_.pressed = uiIn_.rpressed = false;
    uiIn_.chars.clear();
    uiIn_.backspace = uiIn_.enter = uiIn_.tab = uiIn_.escape = uiIn_.copy = false;
}

// ---------------------------------------------------------------------------
// world rendering

// The barrel tip of each weapon model, in the model's own units (camera space: +x right, +y up, -z forward),
// matching the boxes drawn in drawWorld's viewmodel switch.
static const Vec3 kMuzzle[WPN_COUNT] = {
    {0, 0.008f, -0.29f},    // rifle
    {0, 0, -0.25f},         // rocket
    {0, 0, -0.21f},         // knife
    {0, 0.006f, -0.448f},   // sniper
    {0, 0, -0.193f},        // laser
    {0, 0.012f, -0.3f},     // shotgun
    {0, 0, -0.212f},        // sticky
    {0, 0.032f, -0.23f},    // crossbow
    {0, 0.004f, -0.257f},   // bubble
    {0, 0, -0.2f},          // syringe
    {0, 0, -0.125f},        // locator (its emitter)
};

// The first-person models (the weapon, the grapple launcher): their pivots in camera space (+x right, +y up, -z ahead)
// in the reference view, 16:9 at a 100 degree horizontal FOV (the default), low in the bottom corners; the HUD
// element's x / y (720p units, 0.5 mm each, +y down the screen) and depth (mm) move it from there.
static constexpr Vec3 kWeaponBase = {0.33f, -0.28f, -0.66f};    // (was 0.2, -0.17, -0.55)
static constexpr Vec3 kHookBase = {-0.33f, -0.295f, -0.68f};    // (was -0.2, -0.19, -0.58)
static constexpr float kHookViewScale = 0.7f;                   // the launcher at 70% of a weapon's size (the off hand)
static constexpr float kRefAspect = 16.0f / 9.0f, kRefTanY = 1.19175359f / kRefAspect;   // (tan 50 degrees / aspect)

// A first-person model's pivot (camera space) on this screen: kept at the same spot on the screen (as a fraction of
// it) and the same height, whatever the aspect or FOV: x follows the aspect (out to the corner on an ultrawide, in on
// 4:3), depth the vertical FOV (so a wider FOV or a zoom doesn't shrink or grow the model).
Vec3 Game::viewAnchor(const Settings::HudElem& e, const Vec3& base) const {
    const Vec3 p = base + Vec3(e.x * 0.0005f, -e.y * 0.0005f, -e.z * 0.001f);
    const float tx = r_.tanX(), ty = r_.tanY();
    if (!(tx > 0) || !(ty > 0)) return p;
    return {p.x * (tx / ty) / kRefAspect, p.y, p.z * kRefTanY / ty};
}

// A HUD model element's tilt about its pivot (degrees: + pitch = up, + yaw = right, + roll = clockwise)
static Mat4 viewTilt(const Settings::HudElem& e) {
    const float r = -deg2rad(e.roll), c = std::cos(r), s = std::sin(r);
    Mat4 rz;
    rz.m[0] = c; rz.m[1] = s; rz.m[4] = -s; rz.m[5] = c;
    return Mat4::rotY(-deg2rad(e.yaw)) * Mat4::rotX(deg2rad(e.pitch)) * rz;
}
static bool tilted(const Settings::HudElem& e) { return e.pitch != 0 || e.yaw != 0 || e.roll != 0; }

// The weapon model's frame (weapon space x its size -> camera space): at its pivot, pushed back by the kick, tilted
Mat4 Game::weaponFrame(float kick) const {
    const auto& vm = cfg_.hud[Settings::HUD_VIEWMODEL];
    Vec3 at = viewAnchor(vm, kWeaponBase);
    at.z += kick;
    return tilted(vm) ? Mat4::translate(at) * viewTilt(vm) : Mat4::translate(at);
}

// Where your shots come out: the muzzle of the weapon model wherever the HUD settings put it (x / y / depth /
// size / tilt), so beams, tracers and your projectiles leave the gun you see. Hidden model: a spot low and right.
Vec3 Game::muzzlePoint(int weapon, const Vec3& eye) const {
    const auto& vm = cfg_.hud[Settings::HUD_VIEWMODEL];
    Vec3 d = angleForward(yaw_, pitch_), right = normalize(cross(d, {0, 1, 0}));
    if (weapon < 0 || weapon >= WPN_COUNT) return eye + d * 0.5f + right * 0.15f - Vec3(0, 0.12f, 0); // (hidden model: where it would be)
    const Mat4 f = weaponFrame((float)recoil_ * 0.06f);
    const Vec3 m = kMuzzle[weapon] * vm.scale;
    Vec3 o = {f.m[0] * m.x + f.m[4] * m.y + f.m[8] * m.z + f.m[12], f.m[1] * m.x + f.m[5] * m.y + f.m[9] * m.z + f.m[13],
              f.m[2] * m.x + f.m[6] * m.y + f.m[10] * m.z + f.m[14]};
    float cp = std::cos(pitch_), sp = std::sin(pitch_), cy = std::cos(yaw_), sy = std::sin(yaw_);
    Vec3 p = {o.x, cp * o.y - sp * o.z, sp * o.y + cp * o.z};           // the viewmodel's own transform (rotX, then rotY)
    return eye + Vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
}

// A point in an arm prop's space (offset from the shoulder, models.h) in the world: turned by the aim (rotX(pitch), then
// rotY(yaw)), as drawArm turns the arm
static Vec3 armPoint(const Vec3& shoulder, float yaw, float pitch, const Vec3& o) {
    float cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    Vec3 p = {o.x, cp * o.y - sp * o.z, sp * o.y + cp * o.z};
    return shoulder + Vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
}
static Vec3 xfPoint(const Mat4& m, const Vec3& p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

void Game::drawWorld(float alpha) {
    QZONE("drawWorld");
    drawFrame_++;
    r_.shadowCasters = cfg_.shadows;   // (shadows off: nothing off screen is built just to be thrown away)
    const Map& map = net_.map();
    const ModeState& ms = net_.mode();
    auto& players = framePlayers();
    int me = net_.clientId();
    bool alive = net_.selfAlive();
    fairModels_ = rankedPlayer();   // (ranked: nobody's own model files, the shipped ones only: decided once a frame)
    // demo playback: through the recorder's eyes (their recorded view, interpolated like everyone else)
    const bool demoView = net_.playback() && me >= 0 && me < kMaxPlayers && players[me].valid;
    const bool lateLatch = alive && !demoView && mouseCaptured_ && !opts_.camSet;   // (your own view: the aim is re-read before drawing)
    r_.cullPad = lateLatch ? 0.06f : 0.0f;   // (~3.5 degrees: what a fast flick turns in the time until then)
    if (demoView) { yaw_ = players[me].snap.yaw; pitch_ = players[me].snap.pitch; }

    // ULTRA low latency: read the mouse again just before the camera is set (the frame's network, ticks and the work
    // above ran since the input was read). The command already sent this frame keeps the aim it had; the next one
    // carries this. Raw mouse messages only: keys and clicks wait for the next frame, as before.
    if (cfg_.lowLatency == 2 && alive && !demoView && mouseCaptured_) {
        readMouse(true);
        if (pendingInput_ >= 0) { if (frameInput_ < 0) frameInput_ = pendingInput_; pendingInput_ = -1; }   // (F3: shown this frame)
    }
    // camera: own prediction when alive, else spectate someone
    Vec3 eye;
    float camYaw = yaw_, camPitch = pitch_;
    int viewing = me;
    if (alive) {
        Vec3 p = demoView ? players[me].snap.pos : net_.smoothedPos(alpha);
        bool crouch = net_.predicted().flags & MF_CROUCHED;
        eye = p + Vec3(0, (crouch ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
        {   // X7: a stair step moves you up to 0.55 m in one tick: the camera (only: shots still leave from the real eye)
            // starts where it was and closes the gap with an 8 ms time constant (95% in 25 ms), up and down
            const bool gr = !demoView && (net_.predicted().flags & MF_ONGROUND);
            stepCam_ *= (float)std::exp(-frameDt_ / 0.008);
            if (gr && stepGround_) {
                const float dy = p.y - stepFeetY_;
                if (std::fabs(dy) > 0.02f && std::fabs(dy) < 0.6f * bodyScale()) stepCam_ = clampf(stepCam_ - dy, -0.6f, 0.6f);
            }
            if (std::fabs(stepCam_) < 1e-4f) stepCam_ = 0;
            stepFeetY_ = p.y; stepGround_ = gr;
            eye.y += stepCam_;
        }
    } else {
        stepCam_ = 0; stepGround_ = false;
        // team modes: an enemy (the killer) only for kKillCamTime, then a teammate (F11)
        const bool enemyCam = specTarget_ >= 0 && modeIsTeamBased(ms.mode) && !isFriendly(specTarget_) && time_ >= killCamUntil_;
        if (enemyCam || specTarget_ < 0 || !players[specTarget_].valid || !(players[specTarget_].snap.flags & PS_ALIVE)) {
            specTarget_ = -1;
            for (int i = 0; i < kMaxPlayers; i++)
                if (i != me && players[i].valid && (players[i].snap.flags & PS_ALIVE) && (isFriendly(i) || !modeIsTeamBased(ms.mode))) { specTarget_ = i; break; }
        }
        if (specTarget_ >= 0) {
            const PlayerSnap& t = players[specTarget_].snap;
            viewing = specTarget_;
            camYaw = t.yaw;
            camPitch = t.pitch;
            eye = t.pos + Vec3(0, ((t.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
        } else {
            const PlayerSnap* mp = net_.findPlayer(me);
            Vec3 base = mp ? mp->pos : Vec3(0, 0, 0);
            eye = base + Vec3(0, 3.0f, 0);
        }
    }
    // the server sends a dead player what the one it watches can see (NET-10): tell it who that is
    if (alive || net_.playback() || net_.state() != ConnState::Connected) specSent_ = -2;
    else if (specTarget_ != specSent_) { net_.sendCommand("spectate " + std::to_string(specTarget_)); specSent_ = specTarget_; }
    if (opts_.camSet) { eye = opts_.camPos; camYaw = deg2rad(opts_.camYaw); camPitch = deg2rad(opts_.camPitch); }   // (--cam: screenshots of a match from a fixed spot)
    r_.setCamera(eye, camYaw, camPitch, alive ? viewFov() : cfg_.fov);
    audio_.setListener(eye, camYaw);
    updateSounds(players);

    Vec3 dbgBombPos;   // (--models in bomb mode: a dropped bomb in front of the lineup, which carries it)
    const bool dbgBomb = opts_.debugModels && alive && ms.mode == MODE_BOMB;
    if (opts_.debugModels && alive) { // model lineup for screenshots: the last slots, 5 m ahead, facing you
        Vec3 f = {-std::sin(yaw_), 0, -std::cos(yaw_)}, rt = {std::cos(yaw_), 0, -std::sin(yaw_)};
        dbgBombPos = net_.smoothedPos(alpha) + f * 3.0f;
        for (int k = 0; k < kPlayerModels; k++) {
            auto& ip = players[kMaxPlayers - kPlayerModels + k];
            ip.valid = true;
            ip.snap = PlayerSnap{};
            ip.snap.id = (uint8_t)(kMaxPlayers - kPlayerModels + k);
            ip.snap.flags = PS_ALIVE | (k % 2 ? PS_TEAM_B : 0);
            ip.snap.pos = net_.smoothedPos(alpha) + f * 5.0f + rt * ((k - 2) * 1.5f);
            ip.snap.yaw = yaw_ + 3.14159265f;
            if (dbgBomb) { ip.snap.flags |= PS_HASBOMB; ip.snap.yaw -= 2.3f; }   // (turned away: the backpack shows)
            if (opts_.debugHook) {   // --hook: turned side-on (the launcher arm toward you); every other one hangs on a
                                     // rope to a point up behind them, the rest have the claw in the barrel
                ip.snap.yaw -= 1.3f;
                if (k % 2) {
                    ip.snap.hooked = 1;
                    ip.snap.hookPoint = ip.snap.pos + f * 6.0f + rt * ((k - 2) * 2.0f) + Vec3(0, 7.0f, 0);
                }
            }
            if (isItMode(net_.mode().mode)) {   // hide and seek / tag: each as "it", holding the knife (or --weapon)
                ip.snap.flags |= PS_HASBOMB;
                ip.snap.yaw -= 1.0f;   // (turned aside: the weapon side-on)
                ip.snap.weapon = (uint8_t)(opts_.debugWeapon >= 1 && opts_.debugWeapon <= WPN_COUNT ? opts_.debugWeapon - 1 : WPN_KNIFE);
            }
        }
    }
    // players (duel "double size" rounds draw everyone at twice the size, matching the server hitboxes)
    const float bs = bodyScale();
    const bool ranked = rankedPlayer();
    const bool grappleOn = moveTuning().grapple;   // (the mode's rules: no grapple, no launcher on anyone's arm)
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!players[i].valid) continue;
        const PlayerSnap& p = players[i].snap;
        if (!(p.flags & PS_ALIVE)) continue;
        bool self = i == me;
        bool firstPerson = (self && alive) || i == viewing;
        Vec3 pos = self && alive ? net_.smoothedPos(alpha) : p.pos;
        float yaw = self && alive ? yaw_ : p.yaw;
        float pitch = self && alive ? pitch_ : p.pitch;
        bool crouch = self && alive ? (net_.predicted().flags & MF_CROUCHED) : (p.flags & PS_CROUCHED);
        float hs = crouch ? kPlayerCrouchHeight / kPlayerHeight : 1.0f;
        Vec3 col = teamColor(i);
        r_.outlineColor = outlineColor(i);
        r_.outlineWidth = cfg_.outlinePlayer;
        bool vis = !firstPerson;
        auto at = [&](float h) { return pos + Vec3(0, h * hs * bs, 0); };
        Vec3 fwd = {-std::sin(yaw), 0, -std::cos(yaw)}, rgt = {std::cos(yaw), 0, -std::sin(yaw)};
        // the player model (cosmetic: every model fits the same hitbox); ranked / time trial: the default for everyone
        const int pm = ranked ? 0 : opts_.debugModels && i >= kMaxPlayers - kPlayerModels ? i - (kMaxPlayers - kPlayerModels) : net_.playerModel(i);
        const ModelMesh* ov = modelOverride(MODEL_PLAYER, pm);
        // sunlight on them: only coloured geometry uses it (the shadow pass doesn't), so not for yourself in first
        // person, nor for a box model out of view (its boxes and what it holds, all within 2.5 m: shadow pass only).
        // A long ray each (to the map's top): it was the costliest thing per player here.
        float sv = 1.0f;
        if (vis && (ov || r_.inView(at(1.0f), 2.5f * bs))) sv = sunVisibility(map, pos + Vec3(0, 1.0f * hs * bs, 0));
        ModelPalette pal;
        pal.c[TINT_TEAM] = col;
        if (ov) {   // the owner's model (docs/ASSETS.md)
            Vec3 tint[TINT_COUNT];
            overrideTints(MODEL_PLAYER, pm, pal, tint);
            Mat4 sc;
            sc.m[0] = bs; sc.m[5] = bs * hs; sc.m[10] = bs;
            r_.mesh(*ov, Mat4::translate(pos) * Mat4::rotY(yaw) * sc, tint, sv, true, vis);
        } else {
            if (const ModelParts* bx = boxOverride(MODEL_PLAYER, pm)) overrideBoxParts(MODEL_PLAYER, pm, *bx, pal, hs, parts_);
            else playerModelParts(pm, hs, pal, parts_);
            const float cy = std::cos(yaw), sy = std::sin(yaw);
            for (const ModelPart& q : parts_) {
                if (q.visOnly && !vis) continue;
                Vec3 c = q.c * bs;
                r_.box(pos + Vec3(cy * c.x + sy * c.z, c.y, -sy * c.x + cy * c.z), q.h * bs, yaw, q.color, sv, q.shadow, vis);
            }
        }
        if (vis && isItMode(net_.mode().mode)) {   // hide and seek / tag: only "it" holds anything - their weapon's own model
            if (p.flags & PS_HASBOMB) drawHeldWeapon(i, p.weapon, at(1.3f) + rgt * (0.3f * bs), yaw, pitch, bs, col, sv);
        } else if (vis) {
            Vec3 shoulder = at(1.3f) + Vec3(std::cos(yaw), 0, -std::sin(yaw)) * (0.3f * bs);
            drawArm(PROP_GUN_ARM, shoulder, yaw, pitch, bs, sv, kNoSlots);
            if (cfg_.muzzleFlash && time_ < remoteMuzzleUntil_[i])   // (the gun arm is 0.7 long, along the aim)
                drawMuzzleFlash(shoulder + angleForward(yaw, pitch) * (0.74f * bs), p.weapon, teamColor(i),
                                (float)std::min(1.0, (remoteMuzzleUntil_[i] - time_) / 0.05), bs);
            if ((p.flags & PS_HASBOMB) && !isItMode(net_.mode().mode)) {   // the bomb on its carrier's back (player space)
                if (const ModelMesh* bov = modelOverride(MODEL_PROP, PROP_BOMB_PACK)) {
                    Vec3 tint[TINT_COUNT];
                    overrideTints(MODEL_PROP, PROP_BOMB_PACK, kNoSlots, tint);
                    Mat4 sc;
                    sc.m[0] = bs; sc.m[5] = bs * hs; sc.m[10] = bs;
                    r_.mesh(*bov, Mat4::translate(pos) * Mat4::rotY(yaw) * sc, tint, sv, true);
                } else {   // (the box keeps its size when crouched: only its height follows)
                    if (const ModelParts* bx = boxOverride(MODEL_PROP, PROP_BOMB_PACK)) overrideBoxParts(MODEL_PROP, PROP_BOMB_PACK, *bx, kNoSlots, 1.0f, parts_);
                    else propParts(PROP_BOMB_PACK, kNoSlots, parts_);
                    const float cy = std::cos(yaw), sy = std::sin(yaw);
                    for (const ModelPart& q : parts_) {
                        const Vec3 c = {q.c.x * bs, q.c.y * hs * bs, q.c.z * bs};
                        r_.box(pos + Vec3(cy * c.x + sy * c.z, c.y, -sy * c.x + cy * c.z), q.h * bs, yaw, q.color, sv, q.shadow);
                    }
                }
            }
        }
        if (vis && grappleOn) {   // the grapple launcher on the left arm: the claw in its barrel, or out on the rope
            const Vec3 lsh = at(1.3f) - rgt * (0.3f * bs);
            ModelPalette hpal = referencePalette(MODEL_PROP, PROP_HOOK_ARM);
            hpal.c[TINT_TEAM] = col;
            drawArm(PROP_HOOK_ARM, lsh, yaw, pitch, bs, sv, hpal);
            const bool hookOut = self && alive ? (net_.predicted().flags & MF_HOOKED) != 0 : p.hooked != 0;
            if (!hookOut) drawHookClaw(lsh, (kHookMuzzle + kHookSeat) * bs, yaw, pitch, bs, sv);
            hookMuzzle_[i] = armPoint(lsh, yaw, pitch, kHookMuzzle * bs);   // (their rope starts here)
            hookMuzzleFrame_[i] = drawFrame_;
        }
        // hide and seek: the seeker is marked only where you can see them (hiders are never shown where the seeker is,
        // owner B8); tag: the HUD marks the tagger everywhere, through walls (drawTaggerMarker)
        if (!self && (p.flags & PS_HASBOMB) && net_.mode().mode == MODE_HIDE) {
            float sx, sy;
            Vec3 head = pos + Vec3(0, 1.7f * bs, 0), cam = r_.cameraPos(), d = head - cam;
            float dist = length(d);
            if (dist > 0.01f && !map.raycastAny(cam, d / dist, dist - 0.3f) && r_.project(pos + Vec3(0, 2.3f, 0), sx, sy)) {
                float ts = std::max(1.0f, std::round(ui_.textScale()));
                r_.text(sx - r_.textWidth("SEEKER", ts) * 0.5f, sy, ts, "SEEKER", kRed);
            }
        }
        if (vis && !ranked) if (int place = crownOf(i)) { // leaderboard crown on their head (a model; not in ranked)
            Vec3 top = pos + Vec3(0, kPlayerHeight * hs * bs + 0.04f * bs, 0);
            ModelPalette cpal;
            cpal.c[TINT_TEAM] = crownColor(place);
            if (const ModelMesh* ov = modelOverride(MODEL_CROWN, 0)) {
                Vec3 tint[TINT_COUNT];
                overrideTints(MODEL_CROWN, 0, cpal, tint);
                Mat4 sc;
                sc.m[0] = sc.m[5] = sc.m[10] = bs;
                r_.mesh(*ov, Mat4::translate(top) * Mat4::rotY(yaw) * sc, tint, sv, false);
            } else {
                if (const ModelParts* bx = boxOverride(MODEL_CROWN, 0)) overrideBoxParts(MODEL_CROWN, 0, *bx, cpal, 1.0f, parts_);
                else crownParts(cpal, parts_);                              // a band and four points
                const float cy = std::cos(yaw), sy = std::sin(yaw);
                for (const ModelPart& q : parts_) {
                    Vec3 c = q.c * bs;
                    r_.box(top + Vec3(cy * c.x + sy * c.z, c.y, -sy * c.x + cy * c.z), q.h * bs, yaw, q.color, sv, q.shadow);
                }
            }
            // the board's letter in the front of the band: raised blocks from a 3x5 pixel font (Q, L, G, I, R)
            const char* letter = crownLetter(i);
            static const struct { char c; uint16_t rows[5]; } kGlyphs[] = {   // 3 bits a row, top row first
                {'Q', {7, 5, 5, 7, 1}}, {'L', {4, 4, 4, 4, 7}}, {'G', {7, 4, 5, 5, 7}}, {'I', {7, 2, 2, 2, 7}}, {'R', {6, 5, 6, 5, 5}}};
            int nl = (int)strlen(letter);
            const float px = 0.022f * bs, gw = 4 * px;                      // pixel size, glyph advance
            Vec3 face = top + Vec3(0, 0.06f * bs, 0) + fwd * (0.175f * bs);   // the band's front face, its middle
            const Vec3 dark = place == 1 ? Vec3{0.35f, 0.18f, 0.02f} : Vec3{0.2f, 0.22f, 0.26f};
            for (int li = 0; li < nl; li++)
                for (auto& g : kGlyphs) {
                    if (g.c != letter[li]) continue;
                    float x0 = (li - (nl - 1) * 0.5f) * gw - px;               // the glyph's left column
                    for (int r = 0; r < 5; r++)
                        for (int c = 0; c < 3; c++)
                            if (g.rows[r] >> (2 - c) & 1)
                                r_.box(face + rgt * (x0 + c * px) + Vec3(0, (2 - r) * px, 0), Vec3(px * 0.5f, px * 0.5f, 0.012f * bs), yaw, dark, sv, false);
                }
        }
        // health bars over other players (VISUALS > HEALTH BARS; not in ranked)
        // only when the whole body is in sight (no seeing a bar through a wall or over cover: every corner and the
        // middle of the hitbox has a clear line from the camera), and exactly as wide as the hitbox on screen
        auto wholeBodySeen = [&] {
            AABB hb = playerBox(pos, crouch, bs);
            const Vec3 cam = r_.cameraPos();
            // (all 9 must be clear, so the order doesn't change the answer: the ray that last found them hidden goes
            // first, and a player behind cover usually costs one ray, not up to nine)
            const int first = barRay_[i] % 9;
            for (int j = 0; j < 9; j++) {
                const int k = (first + j) % 9;
                Vec3 q = k == 8 ? (hb.mn + hb.mx) * 0.5f : Vec3(k & 1 ? hb.mx.x - 0.02f : hb.mn.x + 0.02f, k & 2 ? hb.mx.y - 0.02f : hb.mn.y + 0.05f, k & 4 ? hb.mx.z - 0.02f : hb.mn.z + 0.02f);
                Vec3 d = q - cam;
                float dist = length(d);
                if (dist > 0.01f && map.raycastAny(cam, d / dist, dist - 0.05f)) { barRay_[i] = (uint8_t)k; return false; }
            }
            return true;
        };
        if (cfg_.healthBars && !ranked && !self && i != viewing) {
            float sx, sy, lx, ly, rx, ry;   // over their head, or at their feet (VISUALS > HEALTH BAR POSITION)
            Vec3 at = cfg_.healthBarsBottom ? pos + Vec3(0, -0.15f, 0) : pos + Vec3(0, 2.0f * bs + 0.35f, 0);
            Vec3 side = Vec3(std::cos(camYaw), 0, -std::sin(camYaw)) * (kPlayerHalfWidth * bs);   // (the hitbox's half width, across the view)
            // (on screen first: the 9 sight rays only for a bar that would be drawn)
            // (project only rejects points behind the camera: a bar wholly off screen, left, right, above or below, draws
            // nothing, so its rays are skipped too)
            float bw = 0, bh = 0;
            if (r_.project(at, sx, sy) && r_.project(at - side, lx, ly) && r_.project(at + side, rx, ry) &&
                (bw = std::max(4.0f, std::fabs(rx - lx)), bh = std::max(2.0f, std::min(5 * ui_.s(), bw * 0.15f)),
                 sx + bw * 0.5f + 1 >= 0 && sx - bw * 0.5f - 1 <= (float)r_.width() && sy + bh + 1 >= 0 && sy - 1 <= (float)r_.height()) &&
                wholeBodySeen()) {
                float f = clampf(p.health / 100.0f, 0, 1);
                r_.rect(sx - bw * 0.5f - 1, sy - 1, bw + 2, bh + 2, {0, 0, 0, 0.7f});
                r_.rect(sx - bw * 0.5f, sy, bw * f, bh, p.health > 30 ? rgba(col) : kRed);
            }
        }
        if (!self && time_ < typingUntil_[i] && !ranked) { // typing in chat (not in ranked)
            float sx, sy;
            if (r_.project(pos + Vec3(0, 2.0f * bs + 0.7f, 0), sx, sy)) {
                float tts = std::max(1.0f, std::round(ui_.textScale()));
                const char* dots = std::fmod(time_, 0.9) < 0.3 ? "." : std::fmod(time_, 0.9) < 0.6 ? ".." : "...";
                r_.text(sx - r_.textWidth("...", tts) * 0.5f, sy, tts, dots, kWhite);
            }
        }
        // name tags for teammates
        if (!self && isFriendly(i) && i != viewing) {
            float sx, sy;
            if (r_.project(pos + Vec3(0, 2.1f, 0), sx, sy)) {
                std::string n = net_.playerName(i);
                float ts = ui_.textScale() * 0.5f;
                ts = std::max(1.0f, std::round(ts));
                r_.text(sx - r_.textWidth(n, ts) * 0.5f, sy, ts, n, rgba(col));
            }
        }
    }

    r_.outlineColor = {0, 0, 0};
    r_.outlineWidth = 1.0f;
    // grapple ropes
    // (a hook latched onto a player ends on their body as drawn here: their interpolated position, or our own)
    auto ropeEnd = [&](int target, const Vec3& anchor, uint16_t rocket = 0) {
        if (target == kHookRocket - 1) {   // riding a rocket: to it as it's drawn (yours: the anchor, drawn as far ahead)
            if (rocket) for (const auto& pr : lastProjectiles_) if (pr.id == rocket && pr.owner != me) return pr.pos;
            return anchor;
        }
        if (target < 0 || target >= kMaxPlayers || !players[target].valid || players[target].snap.hidden || !(players[target].snap.flags & PS_ALIVE)) return anchor;
        Vec3 at = target == me && alive ? net_.smoothedPos(alpha) : players[target].snap.pos;
        return at + Vec3(0, 1.0f * bs, 0);
    };
    // The first-person grapple launcher, low on the left (its own HUD element, GRAPPLE MODEL: x / y / depth / size /
    // tilt / accent colour): placed here, before the ropes, so your rope leaves its muzzle; drawn with the viewmodel
    // below. Not on a mode without the grapple, nor while hidden or scoped. While hooked the claw is out on the rope and
    // the arm comes up a little and turns toward the hook (eased).
    const bool specGun = !alive && viewing != me && viewing >= 0 && players[viewing].valid;
    const auto& vm = cfg_.hud[Settings::HUD_VIEWMODEL];
    const auto& gm = cfg_.hud[Settings::HUD_GRAPPLE];
    const bool fpHook = grappleOn && (alive || specGun) && !(alive && (net_.predicted().mods & MOVE_RUN_ONLY)) &&
                        !(scoped_ && scopeOverlay_ && !specGun) && !gm.hidden;
    bool fpHooked = false;
    Vec3 fpHookTo;
    if (alive && (net_.predicted().flags & MF_HOOKED)) {
        fpHooked = true;
        fpHookTo = ropeEnd((int)net_.predicted().hookTarget - 1, net_.predicted().hookPoint, net_.predicted().hookProj);
    } else if (specGun && players[viewing].snap.hooked) {
        fpHooked = true;
        fpHookTo = ropeEnd((int)players[viewing].snap.hooked - 2, players[viewing].snap.hookPoint, players[viewing].snap.hookProj);
    }
    const float hk = gm.scale * kHookViewScale;
    const Vec3 hookHousing = {0, 0.015f, -0.42f};      // (PROP_HOOK_ARM's housing: the point the arm is placed and turned by)
    Mat4 fpHookXf;                                    // PROP_HOOK_ARM space (x hk) -> camera space
    Vec3 fpMuzzle;                                    // its muzzle in the world
    if (fpHook) {
        const Vec3 base = viewAnchor(gm, kHookBase);
        float tYaw = -0.06f, tPitch = 0, tRaise = 0;  // (at rest: the barrel turned in a touch, toward the crosshair)
        if (fpHooked) {   // toward the hook, from where the launcher is (camera space), at most ~30 degrees
            const Vec3 f = angleForward(camYaw, camPitch), rt = normalize(cross(f, {0, 1, 0})), up = cross(rt, f);
            const Vec3 d = fpHookTo - r_.viewPoint(base);
            const Vec3 c = {dot(d, rt), dot(d, up), -dot(d, f)};
            const float l = length(c);
            if (l > 0.3f) {
                tYaw = clampf(std::atan2(-c.x, -c.z), -0.5f, 0.5f);
                tPitch = clampf(std::asin(clampf(c.y / l, -1, 1)), -0.5f, 0.5f);
            }
            tRaise = 1;
        }
        const float e = 1.0f - std::exp(-14.0f * (float)frameDt_);
        hookAimYaw_ += (tYaw - hookAimYaw_) * e;
        hookAimPitch_ += (tPitch - hookAimPitch_) * e;
        hookRaise_ += (tRaise - hookRaise_) * e;
        fpHookXf = Mat4::translate(base + Vec3(0.01f, 0.035f, -0.03f) * hookRaise_) * Mat4::rotY(hookAimYaw_) * Mat4::rotX(hookAimPitch_);
        if (tilted(gm)) fpHookXf = fpHookXf * viewTilt(gm);   // (about the housing, inside the turn toward the hook)
        fpHookXf = fpHookXf * Mat4::translate(hookHousing * -hk);
        fpMuzzle = r_.viewPoint(xfPoint(fpHookXf, kHookMuzzle * hk));
    } else {
        hookAimYaw_ = -0.06f; hookAimPitch_ = hookRaise_ = 0;   // (shown again: from rest, not from where it was)
    }
    // a rope, and the claw at its far end (sunk into what it caught, turned along the rope)
    auto rope = [&](const Vec3& from, const Vec3& to, float width) {
        r_.line(from, to, width, {0.1f, 0.1f, 0.1f, 1});
        Vec3 d = to - from;
        const float l = length(d);
        if (l < 0.1f) return;
        d = d / l;
        drawHookClaw(to - d * (0.06f * bs), {0, 0, 0}, std::atan2(-d.x, -d.z), std::asin(clampf(d.y, -1, 1)), bs, 1.0f);
    };
    if (alive && fpHooked) {   // yours: from the first-person launcher (hidden viewmodel: low left of the eye)
        Vec3 right = normalize(cross(angleForward(yaw_, pitch_), {0, 1, 0}));
        rope(fpHook ? fpMuzzle : eye - Vec3(0, 0.25f, 0) - right * 0.15f, fpHookTo, 0.03f);
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == me || !players[i].valid || !players[i].snap.hooked) continue;
        const Vec3 from = i == viewing && fpHook ? fpMuzzle : hookMuzzleFrame_[i] == drawFrame_ ? hookMuzzle_[i] : players[i].snap.pos + Vec3(0, 1.2f, 0);
        rope(from, ropeEnd((int)players[i].snap.hooked - 2, players[i].snap.hookPoint, players[i].snap.hookProj), 0.04f);
    }

    // other players' shots: laser beams while firing, a tracer when a sniper fires
    for (int i = 0; i < kMaxPlayers; i++) {
        bool firing = players[i].valid && (players[i].snap.flags & PS_FIRING) && (players[i].snap.flags & PS_ALIVE) && i != me;
        if (firing) {
            const PlayerSnap& p = players[i].snap;
            int w = p.weapon;
            if (w == WPN_KNIFE && !remoteFiring_[i]) knifeSlash_[i] = time_;
            // muzzle flash: each shot's firing edge (the laser's for as long as it fires)
            if (w != WPN_KNIFE && w != WPN_SYRINGE && w != WPN_LOCATOR && (!remoteFiring_[i] || w == WPN_LASER))
                remoteMuzzleUntil_[i] = time_ + (w == WPN_SNIPER ? 0.07 : 0.05);
            if (w == WPN_SHOTGUN && !remoteFiring_[i] && (cfg_.enemyShots || cfg_.impactSparks)) { // pellet tracers in the server's fixed pattern
                const PlayerSnap& q = players[i].snap;
                Vec3 eyeP = q.pos + Vec3(0, ((q.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);   // (giant / double size: their eye, not a body below it)
                Vec3 aim = angleForward(q.yaw, q.pitch);
                const WeaponDef& wd = weaponDef(w);
                for (int k = 0; k < wd.pellets; k++) {
                    Vec3 d = spreadAim(aim, wd.spread, k, wd.pellets, 0);
                    float bangT, t = map.raycastBullet(eyeP, d, wd.range, &bangT);
                    if (cfg_.enemyShots) tracers_.push_back({eyeP + d * 0.6f, eyeP + d * (t < 0 ? wd.range : t), time_ + 0.2});
                    if (cfg_.impactSparks) shotSpark(eyeP, d, t, wd.range, i);
                }
            }
            // (the rifle too, X3: each of its shots is its own firing edge, 0.15 s apart against the flag's 0.1 s)
            if ((cfg_.enemyShots || cfg_.impactSparks) && (w == WPN_LASER || ((w == WPN_SNIPER || w == WPN_RIFLE) && !remoteFiring_[i]))) {
                Vec3 eyeP = p.pos + Vec3(0, ((p.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
                Vec3 d = angleForward(p.yaw, p.pitch);
                float range = weaponDef(w).range;
                float bangT, t = map.raycastBullet(eyeP, d, range, &bangT);
                Vec3 end = eyeP + d * (t < 0 ? range : t);
                Vec3 start = eyeP + d * 0.6f;
                // (watching through their eyes: a line from the eye along the view is edge on, invisible; from the
                // gun's place, low and to the right, like your own)
                if (i == viewing && !alive) start += Vec3(std::cos(p.yaw), 0, -std::sin(p.yaw)) * 0.2f + Vec3(0, -0.17f, 0);
                if (!cfg_.enemyShots) {}
                else if (w == WPN_LASER) r_.line(start, end, 0.06f, rgba(teamColor(i), 0.85f));
                else tracers_.push_back({start, end, time_ + (w == WPN_SNIPER ? 0.9 : 0.2)});
                if (cfg_.impactSparks && (w != WPN_LASER || time_ >= remoteSparkAt_[i]) && shotSpark(eyeP, d, t, range, i) && w == WPN_LASER)
                    remoteSparkAt_[i] = time_ + 0.05;   // (the laser fires every 10 ms: a spark at 20 Hz, like yours)
            }
        }
        remoteFiring_[i] = firing;
    }
    // your muzzle flash, at the weapon model's muzzle (none with the model hidden, or scoped: the scope covers it)
    if (alive && cfg_.muzzleFlash && time_ < muzzleUntil_ && !scoped_ && !cfg_.hud[Settings::HUD_VIEWMODEL].hidden)
        drawMuzzleFlash(muzzlePoint(heldWeapon_, eye), heldWeapon_, cfg_.colorFriendly, (float)std::min(1.0, (muzzleUntil_ - time_) / 0.05),
                        cfg_.hud[Settings::HUD_VIEWMODEL].scale * 0.5f);
    // own laser beam follows the crosshair while held; alt-fire (pull) adds pulses running back toward you
    if (alive && time_ < laserUntil_ && heldWeapon_ == WPN_LASER) {
        Vec3 d = angleForward(yaw_, pitch_);
        float range = weaponDef(WPN_LASER).range;
        float bangT, t = map.raycastBullet(eye, d, range, &bangT);
        Vec3 a = muzzlePoint(WPN_LASER, eye);   // from the laser model's emitter
        Vec3 b = eye + d * (t < 0 ? range : t);
        r_.line(a, b, 0.05f, rgba(cfg_.colorFriendly, 0.9f));
        if (isDown(A_ALTFIRE))
            for (int k = 0; k < 6; k++) {
                float f = 1.0f - std::fmod((float)time_ * 2.5f + k / 6.0f, 1.0f); // moving from target to you
                r_.box(a + (b - a) * f, {0.06f, 0.06f, 0.06f}, (float)time_ * 8, cfg_.colorFriendly * 1.4f, 1.0f, false);
            }
    }
    // knife: a short slash trail sweeping across the knife's reach (its hit ray ends at the tip)
    for (int i = 0; i < kMaxPlayers; i++) {
        float t = (float)(time_ - knifeSlash_[i]) / 0.18f;
        if (knifeSlash_[i] <= 0 || t >= 1 || !players[i].valid) continue;
        bool selfView = i == me && alive;
        const PlayerSnap& p = players[i].snap;
        Vec3 e = selfView ? eye : p.pos + Vec3(0, ((p.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
        // (yours comes out of the knife you hold, like any weapon's shot; others' from their knife hand)
        drawKnifeSlash(e, selfView ? yaw_ : p.yaw, selfView ? pitch_ : p.pitch, t, teamColor(i),
                       selfView ? muzzlePoint(WPN_KNIFE, eye) : Vec3());
    }

    // projectiles (yours: from your weapon model's muzzle, blending onto the real path over 0.2 s; X5: your rockets,
    // bolts and thrown knives fly from the click, a predicted copy, until the server's arrives and takes over as far
    // along as the copy was)
    auto projGravity = [](int type, bool lob) { return projectileGravity(type, lob ? kRocketLobGravity : 1.0f); };
    if (!alive) for (auto& q : predProj_) q.live = false;
    for (auto& q : predProj_) {   // the copies, where they'd be now (stopped at the first wall: the server's word follows)
        if (!q.live) continue;
        const double age = time_ - q.born;
        if (age > 0.6) { q.live = false; continue; }   // (no copy came: a shot the server refused)
        if (q.atWall) continue;
        Vec3 p = q.pos0, v = q.vel0;
        projectileExtrapolate(p, v, projGravity(q.type, q.lob), (float)age);
        const Vec3 d = p - q.pos;
        const float len = length(d);
        Vec3 hn;
        int hm = -1;
        float hit = len > 1e-4f ? map.raycast(q.pos, d / len, len, &hn, &hm) : -1.0f, et;
        int victim = -1;
        bool vHead = false;
        if (q.type != WPN_KNIFE && len > 1e-4f && enemyOnSegment(q.pos, p, &et, &victim, &vHead) && (hit < 0 || et * len < hit)) { hit = et * len; hm = -1; }
        else victim = -1;
        if (hit >= 0 && hm == kMatRubber && q.type == WPN_ROCKET && dot(v, hn) < 0) {   // (off rubber: on from there, as the server's does)
            q.pos0 = q.pos + d / len * std::max(0.0f, hit - 0.05f);
            q.vel0 = v - hn * (dot(v, hn) * (1.0f + kProjRubberBounce));
            q.born = time_;
            q.pos = q.pos0;
            continue;
        }
        if (hit >= 0) {   // (hidden; the server's copy still takes over from it, and isn't drawn once this one blew up)
            q.atWall = true;
            if (q.type != WPN_KNIFE) {
                localBoom(0xFFFF, q.type, q.pos + d / len * hit); q.boomed = true;
                reportImpact(q.type, q.lob, q.pos, q.pos + d / len * hit, victim, vHead);
            }
            continue;
        }
        q.pos = p;
        if (!q.started) { q.started = true; q.offset = muzzlePoint(q.type, eye) - q.pos0; }
    }
    {
        std::vector<ProjStart>& keep = projStartsKeep_;   // (the two trade memory each frame)
        keep.clear();
        for (auto& pr : lastProjectiles_) {
            if (pr.owner != me) continue;
            ProjStart st{pr.id, time_, {}};
            bool found = false;
            for (auto& o : projStarts_) if (o.id == pr.id) { st = o; found = true; }
            PredProj* copy = nullptr;   // a new one of yours: the oldest copy of its kind takes it
            if (!found && alive && !(pr.flags & (PROJ_STUCK | PROJ_ON_PLAYER)))
                for (auto& q : predProj_) if (q.live && q.type == pr.type && (!copy || q.born < copy->born)) copy = &q;
            if (copy) {
                const float sp = length(pr.vel);
                if (sp > 0.1f) st.lead = clampf(dot(copy->pos - pr.pos, pr.vel / sp) / sp, 0.0f, 0.4f);
                Vec3 p = pr.pos, v = pr.vel;
                projectileExtrapolate(p, v, projGravity(pr.type, (pr.flags & PROJ_LOB) != 0), st.lead);
                st.offset = copy->pos - p;   // (what's left: blended out like the muzzle start)
                if (length(st.offset) > 3.0f) st.offset = {};
                if (copy->boomed) {   // it blew up on your screen before the server's copy came: that one is it
                    st.boomed = true;
                    for (auto& b : localBooms_) if (b.id == 0xFFFF && b.type == pr.type) { b.id = pr.id; break; }
                }
                copy->live = false;
            } else if (!found && alive) {
                // where the weapon that fired it points out of the screen (thrown knife: from the knife hand)
                int w = pr.type < WPN_COUNT ? pr.type : heldWeapon_;
                Vec3 muz = muzzlePoint(w, eye);
                if (length(pr.pos - eye) < 3.0f) st.offset = muz - pr.pos;   // only shots just fired
            }
            keep.push_back(st);
        }
        projStarts_.swap(keep);
    }
    auto drawProj = [&](ProjectileSnap pr, bool copy) {
        // (on you: where you are this frame; on anyone else, interpolatedProjectiles already put it on them)
        if ((pr.flags & PROJ_ON_PLAYER) && pr.onPlayer == me) net_.attachedPos(pr, alpha, &pr.pos);
        if (!copy)
            for (auto& st : projStarts_)
                if (st.id == pr.id) { float f = 1.0f - (float)(time_ - st.seen) / 0.2f; if (f > 0) pr.pos = pr.pos + st.offset * f; }
        Vec3 col = teamColor(pr.owner);
        float sp = length(pr.vel);
        Vec3 dir = sp > 0.01f ? pr.vel / sp : Vec3(0, 0, -1);
        float yaw = std::atan2(-dir.x, -dir.z);
        float pitch = std::asin(clampf(dir.y, -1, 1));
        // the model (models.h, or the owner's override of it), turned the way it flies; trails stay lines
        float fl = 1, core = 1, wob = 0, my = yaw, mp = pitch;
        int mt = modelExists(MODEL_PROJECTILE, pr.type) && pr.type != MODEL_PROJ_ORB ? pr.type : MODEL_PROJ_ORB;
        if (mt == WPN_STICKY) { // a spiked mine: dark shell, team-coloured core, spikes; stuck ones blink their core
            bool resting = sp < 0.01f;
            my = (float)time_ * (resting ? 0 : 6); mp = 0;
            core = resting ? 0.6f + 0.4f * (std::fmod((float)time_ * 2.5f, 1.0f) < 0.5f ? 1.0f : 0.0f) : 1.0f;
        } else if (mt == WPN_ROCKET) fl = 0.8f + 0.2f * std::sin((float)time_ * 60 + pr.id);   // a flickering flame
        else if (mt == WPN_KNIFE) mp = (float)time_ * 25.0f;                                     // spins end over end
        else if (mt == WPN_BUBBLE) { wob = 0.03f * std::sin((float)time_ * 14 + pr.id); my = mp = 0; }   // wobbles
        else if (mt == MODEL_PROJ_ORB) my = mp = 0;
        ModelPalette ppal;
        ppal.c[TINT_TEAM] = col;
        ppal.c[TINT_GLOW] = r_.glowColor;
        if (const ModelMesh* ov = modelOverride(MODEL_PROJECTILE, mt)) {
            Vec3 tint[TINT_COUNT];
            overrideTints(MODEL_PROJECTILE, mt, ppal, tint);
            r_.mesh(*ov, Mat4::translate(pr.pos) * Mat4::rotY(my) * Mat4::rotX(mp), tint, 1.0f);
        } else {
            if (const ModelParts* bx = boxOverride(MODEL_PROJECTILE, mt)) overrideBoxParts(MODEL_PROJECTILE, mt, *bx, ppal, 1.0f, parts_);   // (still: no flicker / blink / wobble)
            else projectileParts(mt, ppal, fl, core, wob, parts_);
            for (const ModelPart& q : parts_) {
                if (q.alpha < 1) r_.translucentBox({pr.pos + q.c - q.h, pr.pos + q.c + q.h}, rgba(q.color, q.alpha));
                else r_.boxPitched(pr.pos, q.c, q.h, my, mp, q.color, 1.0f);
            }
        }
        if (mt == WPN_ROCKET) r_.line(pr.pos + dir * -0.35f, pr.pos - dir * 3.5f, 0.1f, rgba(col, 0.4f));
        else if (mt == WPN_SYRINGE) r_.line(pr.pos, pr.pos - dir * 1.5f, 0.03f, rgba(col, 0.5f));
        else if (mt == WPN_KNIFE) r_.line(pr.pos, pr.pos - dir * 2.0f, 0.03f, rgba(col, 0.4f));
        else if (mt == WPN_CROSSBOW) r_.line(pr.pos, pr.pos - dir * 4.0f, 0.04f, rgba(col, 0.5f));
        else if (mt == MODEL_PROJ_ORB) r_.line(pr.pos, pr.pos - dir * 3.5f, 0.12f, rgba(col, 0.5f));
        };
    for (auto pr : lastProjectiles_) {
        bool boomed = false;
        for (auto& st : projStarts_)
            if (st.id == pr.id && st.lead > 0 && !(pr.flags & (PROJ_STUCK | PROJ_ON_PLAYER)) && !st.boomed) {   // (X5: as far along as its copy was, short of the wall)
                Vec3 p = pr.pos, v = pr.vel;
                projectileExtrapolate(p, v, projGravity(pr.type, (pr.flags & PROJ_LOB) != 0), st.lead);
                const Vec3 d = p - pr.pos;
                const float len = length(d);
                Vec3 hn;
                int hm = -1;
                float hit = len > 1e-4f ? map.raycast(pr.pos, d / len, len, &hn, &hm) : -1.0f, et;
                const bool blows = pr.type == WPN_ROCKET || pr.type == WPN_CROSSBOW;   // (stickies stick; knives: the server's word)
                int victim = -1;
                bool vHead = false;
                if (blows && len > 1e-4f && enemyOnSegment(pr.pos, p, &et, &victim, &vHead) && (hit < 0 || et * len < hit)) { hit = et * len; hm = -1; }
                else victim = -1;
                if (hit >= 0 && hm == kMatRubber && pr.type == WPN_ROCKET && dot(v, hn) < 0) {
                    // off rubber (as the server's does): drawn on along the bounce, not blown up there (it vanished)
                    const Vec3 at = pr.pos + d / len * std::max(0.0f, hit - 0.05f);
                    const Vec3 rv = v - hn * (dot(v, hn) * (1.0f + kProjRubberBounce));
                    const float rs = length(rv), rest = std::max(0.0f, len - hit);
                    pr.pos = at;
                    if (rs > 1e-4f && map.raycast(at, rv / rs, rest) < 0) pr.pos = at + rv / rs * rest;
                    pr.vel = rv;
                    continue;   // (the loop over projStarts_: this one's done)
                }
                if (blows && hit >= 0) {   // where it lands, as you see it (and its hits: yours)
                    localBoom(pr.id, pr.type, pr.pos + d / len * hit); st.boomed = true;
                    reportImpact(pr.type, (pr.flags & PROJ_LOB) != 0, pr.pos, pr.pos + d / len * hit, victim, vHead);
                }
                pr.pos = hit >= 0 ? pr.pos + d / len * std::max(0.0f, hit - 0.05f) : p;
                pr.vel = v;
            }
        for (auto& st : projStarts_) boomed |= st.id == pr.id && st.boomed;
        if (!boomed) drawProj(pr, false);
    }
    for (auto& q : predProj_) {
        if (!q.live || q.atWall) continue;
        ProjectileSnap pr;
        pr.type = q.type; pr.owner = (uint8_t)me; pr.id = 0xFFFF;
        Vec3 v = q.vel0, p = q.pos0;
        projectileExtrapolate(p, v, projGravity(q.type, q.lob), (float)(time_ - q.born));
        pr.vel = v;
        float f = 1.0f - (float)(time_ - q.born) / 0.2f;   // (from the muzzle, like the server's copy did)
        pr.pos = q.pos + (f > 0 ? q.offset * f : Vec3());
        if (length(pr.pos - eye) < 0.8f) continue;   // (its first ~10 ms: the model would fill the view from the muzzle)
        drawProj(pr, true);
    }
    drawStuckProps(eye, alpha);
    // explosions
    for (size_t i = 0; i < explosions_.size();) {
        float t = (float)(time_ - explosions_[i].start) / explosions_[i].life;
        if (t >= 1) { explosions_.erase(explosions_.begin() + i); continue; }
        float r = (0.5f + t * 3.0f) * explosions_[i].size;
        Vec3 c = explosions_[i].at;
        Color ec = explosions_[i].wall ? Color{1.0f, 0.82f, 0.45f, 0.85f * (1 - t)} : explosions_[i].bubble ? rgba(r_.glowColor, 0.5f * (1 - t)) : explosions_[i].spark ? Color{1.0f, 0.35f, 0.2f, 0.9f * (1 - t)}
                 : explosions_[i].kill ? Color{0.85f, 0.08f, 0.06f, 0.6f * (1 - t)} : Color{1.0f, 0.85f, 0.5f, 0.55f * (1 - t)};
        r_.translucentBox({c - Vec3(r, r, r), c + Vec3(r, r, r)}, ec);
        i++;
    }
    for (size_t i = 0; i < tracers_.size();) {
        if (time_ > tracers_[i].until) { tracers_.erase(tracers_.begin() + i); continue; }
        float life = (float)std::min(1.0, (tracers_[i].until - time_) / 0.25); // fade out over the last quarter second
        r_.line(tracers_[i].a, tracers_[i].b, 0.03f, {1.0f, 0.95f, 0.7f, 0.8f * life});
        i++;
    }

    updateDestruction(map);   // (glass, shards, holes)
    // objectives
    Vec3 friendly = cfg_.colorFriendly, enemy = cfg_.colorEnemy;
    const PlayerSnap* mep = net_.findPlayer(me);
    int myTeam = mep ? (mep->team() == TEAM_SPECTATOR ? TEAM_A : mep->team()) : TEAM_A;
    auto teamCol = [&](int t) { return t == myTeam ? friendly : enemy; };
    auto labelAt = [&](const Vec3& p, const std::string& s, const Color& c) {
        float sx, sy;
        if (r_.project(p, sx, sy)) {
            float ts = ui_.textScale();
            r_.text(sx - r_.textWidth(s, ts) * 0.5f, sy, ts, s, c);
        }
    };
    if (ms.mode == MODE_BOMB) {
        const char* names[] = {"A", "B", "C", "D"};
        for (size_t i = 0; i < map.bombSites.size(); i++) {
            r_.translucentBox(map.bombSites[i], rgba(teamCol(ms.attackers), 0.07f));
            Vec3 c = map.bombSites[i].center();
            labelAt({c.x, map.bombSites[i].mx.y + 0.5f, c.z}, std::string("SITE ") + names[i % 4], kWhite);
        }
        if (ms.bombState == BOMB_DROPPED || ms.bombState == BOMB_PLANTED || dbgBomb) {
            const Vec3 bombPos = dbgBomb ? dbgBombPos : ms.bombPos;
            bool blink = ms.bombState == BOMB_PLANTED && std::fmod(time_, std::max(0.15, ms.bombTimer / 20.0)) < 0.08;
            ModelPalette bpal;
            bpal.c[TINT_GLOW] = blink ? Vec3(1, 0.2f, 0.2f) : Vec3(0.15f, 0.15f, 0.15f);
            drawProp(PROP_BOMB, bpal, bombPos, 0, {1, 0, 0}, {0, 0, 1}, 0, 1.0f);
            labelAt(bombPos + Vec3(0, 0.8f, 0), ms.bombState == BOMB_PLANTED ? "BOMB" : "BOMB (DROPPED)", rgba(teamCol(ms.attackers)));
        }
    } else if (ms.mode == MODE_POINTS && !map.points.empty()) {
        // the point under attack glows (attacker colour while it fills); taken points are dim; later ones outlined
        for (size_t i = 0; i < map.points.size(); i++) {
            const AABB& z = map.points[i];
            std::string name = std::string("POINT ") + char('A' + i);
            Vec3 top = {z.center().x, z.mx.y + 0.5f, z.center().z};
            if ((int)i < ms.point) { r_.translucentBox(z, rgba(teamCol(ms.attackers), 0.04f)); continue; }
            if ((int)i > ms.point) { r_.translucentBox(z, rgba({1, 1, 1}, 0.03f)); continue; }
            Vec3 c = (ms.flags & MS_CONTESTED) ? Vec3(1, 1, 1) : teamCol(ms.attackers);
            r_.translucentBox(z, rgba(c, 0.1f + 0.06f * ms.capture + 0.04f * std::sin((float)time_ * 4)));
            labelAt(top, name + ((ms.flags & MS_CONTESTED) ? " - CONTESTED" : ms.capture > 0 ? " - " + std::to_string((int)(ms.capture * 100)) + "%" : ""), rgba(c));
        }
    } else if (ms.mode == MODE_KOTH && map.hasHill) {
        Vec3 c = ms.hillOwner == 255 ? Vec3(1, 1, 1) : teamCol(ms.hillOwner);
        r_.translucentBox(map.hill, rgba(c, 0.12f + 0.05f * std::sin((float)time_ * 4)));
        labelAt({map.hill.center().x, map.hill.mx.y + 0.5f, map.hill.center().z}, (ms.flags & MS_CONTESTED) ? "CONTESTED" : "HILL", rgba(c));
    } else if (isPayloadMode(ms.mode) && map.payloadPath.size() >= 2) {
        bool neutral = ms.mode == MODE_NPAYLOAD;
        int cartTeam = neutral ? (ms.hillOwner == 255 ? -1 : ms.hillOwner) : ms.attackers;
        Vec3 cartCol = cartTeam < 0 ? Vec3(0.75f, 0.75f, 0.78f) : teamCol(cartTeam);
        for (size_t i = 1; i < map.payloadPath.size(); i++)
            r_.line(map.payloadPath[i - 1] + Vec3(0, 0.05f, 0), map.payloadPath[i] + Vec3(0, 0.05f, 0), 0.25f, rgba(neutral ? Vec3(1, 1, 1) : teamCol(ms.attackers), 0.35f));
        // checkpoints along the track: a gate each (lit once the cart has reached it)
        float plen = map.payloadLength();
        for (size_t k = 0; k < map.cartChecks.size() && !neutral; k++) {
            Vec3 at = map.cartPoint(ms.mode, ms.attackers, map.cartChecks[k] * plen), ah = map.cartPoint(ms.mode, ms.attackers, map.cartChecks[k] * plen + 0.5f) - at;
            Vec3 side = normalize(Vec3(-ah.z, 0, ah.x));   // (to the right of the cart's way)
            bool reached = (int)k < ms.point;
            Vec3 gc = reached ? teamCol(ms.attackers) : Vec3(0.8f, 0.8f, 0.8f);
            ModelPalette gpal;
            gpal.c[TINT_TEAM] = gc;
            drawProp(PROP_GATE, gpal, at, std::atan2(-ah.x, -ah.z), side, Vec3(-side.z, 0, side.x), 0, 1.0f);   // (box posts: square to the world)
            labelAt(at + Vec3(0, 3.2f, 0), "CHECKPOINT " + std::to_string(k + 1), rgba(gc));
        }
        Vec3 cart = map.cartPoint(ms.mode, ms.attackers, ms.payloadDist);
        Vec3 ahead = map.cartPoint(ms.mode, ms.attackers, ms.payloadDist + 0.5f);
        Vec3 d = ahead - cart;
        float yaw = (length(d) > 1e-3f) ? std::atan2(-d.x, -d.z) : 0;
        float sv = sunVisibility(map, cart + Vec3(0, 1, 0));
        // the ore cart (models.h) in the map's palette
        ModelPalette cpal;
        cpal.c[TINT_METAL] = cartMetal(r_.metalColor);
        cpal.c[TINT_ACCENT] = cartAccent(r_.accentColor);
        cpal.c[TINT_TEAM] = cartCol;
        cpal.c[TINT_GLOW] = r_.glowColor;
        const float cy = std::cos(yaw), syw = std::sin(yaw);
        drawProp(PROP_CART, cpal, cart, yaw, {cy, 0, -syw}, {syw, 0, cy}, yaw, sv);
    } else if (isRaceMode(ms.mode)) {
        int next = mep ? mep->checkpoint : 0;
        for (size_t i = 0; i < map.checkpoints.size(); i++) {
            bool isNext = (int)i == next;
            bool finish = i + 1 == map.checkpoints.size();
            Color c = isNext ? rgba(friendly, 0.2f) : rgba({1, 1, 1}, (int)i < next ? 0.03f : 0.07f);
            r_.translucentBox(map.checkpoints[i], c);
            if (isNext) {
                Vec3 cc = map.checkpoints[i].center();
                labelAt({cc.x, map.checkpoints[i].mx.y + 0.3f, cc.z}, finish ? "FINISH" : "CHECKPOINT " + std::to_string(i + 1), rgba(friendly));
            }
        }
    }

    // viewmodel (hide and seek / tag: only "it" holds anything)
    // (spectating: the gun of whoever we're watching)
    r_.viewOpacity = cfg_.viewmodelOpacity;   // (GAMEPLAY > WEAPON MODEL OPACITY: the gun and the launcher, in the view pass)
    const PlayerSnap* holder = specGun ? &players[viewing].snap : mep;
    bool unarmed = isItMode(net_.mode().mode) && !(holder && (holder->flags & PS_HASBOMB)) && !(opts_.debugWeapon > 0 && !specGun);   // (--weapon: shown anyway, for screenshots)
    // (per second: it was per frame, so the kick lasted 8 ms at 1000 fps; outside the view model's test: scoped, hidden
    // or dead it never decayed, and unscoping replayed the whole kick, and the muzzle stayed pushed back)
    recoil_ = std::max(0.0, recoil_ - 8.0 * frameDt_);
    if ((alive || specGun) && !(scoped_ && scopeOverlay_ && !specGun) && !unarmed && !vm.hidden) {
        int w = specGun ? (int)players[viewing].snap.weapon : heldWeapon_;
        float kick = (float)recoil_ * 0.06f;
        // Each weapon is a handful of boxes around `at` (camera space: +x right, +y up, -z forward), models.h.
        // the map's own palette: metal body, base-coloured highlights, its glow colour on emitters / sights
        ModelPalette vpal;
        vpal.c[TINT_METAL] = viewmodelMetal(r_.metalColor);
        vpal.c[TINT_BASE] = viewmodelBase(r_.baseColor);
        vpal.c[TINT_TEAM] = vm.custom ? vm.color : cfg_.colorFriendly;
        vpal.c[TINT_GLOW] = r_.glowColor;
        // placed from the HUD settings: x / y in 720p units (+y down the screen), depth in mm, scale, tilt
        // (untilted: the plain camera-space boxes, no matrix per box)
        const Mat4 wf = weaponFrame(kick);
        const bool tilt = tilted(vm);
        const Vec3 at = {wf.m[12], wf.m[13], wf.m[14]};
        const float k = vm.scale;
        if (const ModelMesh* ov = modelOverride(MODEL_WEAPON, w)) {   // the owner's model (docs/ASSETS.md)
            Vec3 tint[TINT_COUNT];
            overrideTints(MODEL_WEAPON, w, vpal, tint);
            if (tilt) {
                Mat4 sc;
                sc.m[0] = sc.m[5] = sc.m[10] = k;
                r_.viewMeshXf(*ov, wf * sc, tint);
            } else r_.viewMesh(*ov, at, k, tint);
        } else {
            if (const ModelParts* bx = boxOverride(MODEL_WEAPON, w)) overrideBoxParts(MODEL_WEAPON, w, *bx, vpal, 1.0f, parts_);
            else viewmodelParts(w, vpal, parts_);
            // the locator's screen: dim while it recharges, pulsing when it's ready
            float glow = w != WPN_LOCATOR || specGun ? 1.0f : time_ >= locatorReady_ ? 0.85f + 0.35f * (float)std::sin(time_ * 6.0) : 0.3f;
            for (const ModelPart& q : parts_) {
                const Vec3 c = q.slot == TINT_GLOW ? q.color * glow : q.color;
                if (tilt) r_.viewBoxXf(wf * Mat4::translate(q.c * k), q.h * k, c);
                else r_.viewBox(at + q.c * k, q.h * k, c);
            }
        }
    }
    if (fpHook) {   // the grapple launcher (placed above, with the ropes); the claw in its barrel unless it's out
        ModelPalette hpal = referencePalette(MODEL_PROP, PROP_HOOK_ARM);
        hpal.c[TINT_TEAM] = gm.custom ? gm.color : cfg_.colorFriendly;   // (its stripe: the element's accent colour)
        Mat4 sc;
        sc.m[0] = sc.m[5] = sc.m[10] = hk;
        if (const ModelMesh* ov = modelOverride(MODEL_PROP, PROP_HOOK_ARM)) {   // the owner's model (docs/ASSETS.md)
            Vec3 tint[TINT_COUNT];
            overrideTints(MODEL_PROP, PROP_HOOK_ARM, hpal, tint);
            r_.viewMeshXf(*ov, fpHookXf * sc, tint);
        } else {
            if (const ModelParts* bx = boxOverride(MODEL_PROP, PROP_HOOK_ARM)) overrideBoxParts(MODEL_PROP, PROP_HOOK_ARM, *bx, hpal, 1.0f, parts_);
            else propParts(PROP_HOOK_ARM, hpal, parts_);
            for (const ModelPart& q : parts_) r_.viewBoxXf(fpHookXf * Mat4::translate(q.c * hk), q.h * hk, q.color);
        }
        if (!fpHooked) {
            const Mat4 seat = fpHookXf * Mat4::translate((kHookMuzzle + kHookSeat) * hk);
            const ModelPalette& cpal = referencePalette(MODEL_PROP, PROP_HOOK);
            if (const ModelMesh* ov = modelOverride(MODEL_PROP, PROP_HOOK)) {
                Vec3 tint[TINT_COUNT];
                overrideTints(MODEL_PROP, PROP_HOOK, cpal, tint);
                r_.viewMeshXf(*ov, seat * sc, tint);
            } else {
                if (const ModelParts* bx = boxOverride(MODEL_PROP, PROP_HOOK)) overrideBoxParts(MODEL_PROP, PROP_HOOK, *bx, cpal, 1.0f, parts_);
                else propParts(PROP_HOOK, cpal, parts_);
                for (const ModelPart& q : parts_) r_.viewBoxXf(seat * Mat4::translate(q.c * hk), q.h * hk, q.color);
            }
        }
    }
    sceneSettings();
    // The HUD, F3 / F4 and their upload, before the late read: off the mouse to photon path (it used to sit between the
    // read and the swap, ~87 us at 4000 fps). What it places in the 3D view (damage numbers, the locator box) uses the
    // aim from before the read, a sliver of a turn behind, like the name tags above (owner: the UI may lag the aim).
    if (bench_.phase != BenchRun::OFF) drawBenchOverlay((float)r_.width(), (float)r_.height(), r_.height() / 720.0f);   // (a camera: no HUD)
    else drawHud();
    if (showPerf_) drawPerf();
    else if (perfMini_) drawPerfMini();
    r_.prepare2D();
    hudBuilt_ = true;
    // The dynamic geometry's upload and the shadow pass don't depend on the aim either: before the read too (ULTRA:
    // flushed, so the GPU draws the shadow map meanwhile). After the read: only the view's own draws.
    r_.prepareScene(cfg_.shadows);
    if (ultraPaced()) glFlush();
    // Late latch: the newest mouse motion, read just before the world is drawn (everything above, world, players and
    // effects, was built in world space: only the view turns). The command this frame sent keeps its aim; the next
    // carries this. What was projected onto the screen above (name tags, health bars) is off by this sliver of a turn.
    if (lateLatch) {
        readMouse(true);
        r_.relatch(yaw_, pitch_);
        if (pendingInput_ >= 0) { if (frameInput_ < 0) frameInput_ = pendingInput_; pendingInput_ = -1; }   // (F3: shown this frame, not the next)
        pipeMark(PM_LATCH);
    } else pipeNow_[PM_LATCH] = pipeNow_[PM_POLL];   // (no late read: the mouse is fixed at the frame's read)
    pipeMark(PM_SCENE0);
    pipeGpuBegin();
    r_.drawScene(cfg_.shadows);
    if (ultraPaced()) glFlush();
    pipeMark(PM_SCENE1);   // (ULTRA: the GPU starts the world while the HUD is built; it sits idle otherwise)
}

// ---------------------------------------------------------------------------
// HUD

// Tag (owner B8): every runner always knows where the tagger is - a marker over them through walls with the distance,
// or an arrow at the screen edge pointing their way. (The server always sends the tagger's position in tag.)
void Game::drawTaggerMarker(float W, float H, float s) {
    const int me = net_.clientId();
    const auto& ip = framePlayers();   // (drawWorld's, this frame's: not a second interpolation)
    int it = -1;
    for (int i = 0; i < kMaxPlayers; i++)
        if (i != me && ip[i].valid && (ip[i].snap.flags & PS_ALIVE) && (ip[i].snap.flags & PS_HASBOMB)) it = i;
    if (it < 0) return;
    const Vec3 at = ip[it].snap.pos + Vec3(0, 2.25f * bodyScale(), 0), cam = r_.cameraPos();
    const Vec3 d = at - cam;
    const float dist = length(d);
    const float ts = ui_.textScale(), small = std::max(1.0f, std::round(ts * 0.9f));
    const Color red = {1, 0.25f, 0.2f, 1};
    const float m = 28 * s;   // (kept this far inside the screen edge)
    float sx, sy;
    bool on = r_.project(at, sx, sy) && sx > m && sx < W - m && sy > m && sy < H - m;
    std::string label = "TAGGER " + std::to_string((int)std::lround(dist)) + "M";
    if (on) {   // a downward wedge over their head, their name and distance above it
        float k = 14 * s;
        for (int r = 0; r < 5; r++) r_.rect(sx - k * (1 - r / 5.0f), sy - k + r * k / 5, 2 * k * (1 - r / 5.0f), k / 5 + 1, red);
        r_.text(sx - r_.textWidth(label, small) * 0.5f, sy - k - r_.textHeight(small) - 4 * s, small, label, red);
        return;
    }
    // off screen (or behind): an arrow at the edge, in their direction as seen from here
    const float yaw = r_.cameraYaw(), pitch = r_.cameraPitch();
    Vec3 fwd = angleForward(yaw, pitch), right = normalize(cross(fwd, {0, 1, 0})), up = cross(right, fwd);
    float x = dot(d, right), y = -dot(d, up);
    if (dot(d, fwd) < 0 && std::fabs(x) < 1e-3f) x = 1e-3f;   // (straight behind: off to a side)
    if (dot(d, fwd) < 0) y = std::max(y, 0.0f);                // (behind: along the bottom half)
    float len = std::sqrt(x * x + y * y);
    if (len < 1e-4f) return;
    x /= len; y /= len;
    float t = std::min((W * 0.5f - m) / std::max(1e-4f, std::fabs(x)), (H * 0.5f - m) / std::max(1e-4f, std::fabs(y)));
    float ex = W * 0.5f + x * t, ey = H * 0.5f + y * t;
    for (float side : {-0.65f, 0.65f})   // a chevron pointing their way: two arms of squares back from the tip
        for (int k = 0; k < 7; k++) {
            float c = std::cos(side), sn = std::sin(side), ax = -(x * c - y * sn), ay = -(x * sn + y * c);
            float px = ex + ax * k * 3 * s, py = ey + ay * k * 3 * s;
            r_.rect(px - 2.5f * s, py - 2.5f * s, 5 * s, 5 * s, red);
        }
    const float lw = r_.textWidth(label, small), lh = r_.textHeight(small);   // (the label just inside the arrow)
    float lx = ex - x * (26 * s + lw * 0.5f) - lw * 0.5f, ly = ey - y * (26 * s + lh * 0.5f) - lh * 0.5f;
    lx = clampf(lx, 4 * s, W - r_.textWidth(label, small) - 4 * s);
    ly = clampf(ly, 4 * s, H - r_.textHeight(small) - 4 * s);
    r_.text(lx, ly, small, label, red);
}

bool Game::rpsPicking() const {
    if (!net_.hasSnapshot() || !(net_.mode().flags & MS_RPS) || rps_.stage != RPS_PICK || net_.playback()) return false;
    for (auto& p : rps_.players) if (p.id == net_.clientId()) return !p.out;
    return false;
}

// A shape as a few flat blocks in a size x size square: a rock, a sheet of paper, open scissors.
void Game::drawRpsShape(float x, float y, float z, int shape, Color c) {
    Color dark = {c.r * 0.45f, c.g * 0.45f, c.b * 0.45f, c.a};
    auto R = [&](float a, float b, float w, float h, Color k) { r_.rect(x + a * z, y + b * z, w * z, h * z, k); };
    switch (shape) {
        case RPS_ROCK:
            R(0.22f, 0.18f, 0.56f, 0.66f, c); R(0.12f, 0.32f, 0.76f, 0.42f, c); R(0.16f, 0.24f, 0.68f, 0.56f, c);
            R(0.3f, 0.3f, 0.12f, 0.08f, dark); R(0.56f, 0.5f, 0.14f, 0.08f, dark); R(0.36f, 0.62f, 0.1f, 0.07f, dark);
            break;
        case RPS_PAPER:
            R(0.2f, 0.1f, 0.6f, 0.8f, c); R(0.62f, 0.1f, 0.18f, 0.18f, dark);
            for (int k = 0; k < 4; k++) R(0.3f, 0.32f + k * 0.13f, 0.4f, 0.04f, dark);
            break;
        case RPS_SCISSORS:
            for (int k = 0; k <= 10; k++) {   // two blades crossing, then the handle rings
                float t = k / 10.0f;
                R(0.18f + t * 0.5f, 0.1f + t * 0.5f, 0.1f, 0.1f, c);
                R(0.72f - t * 0.5f, 0.1f + t * 0.5f, 0.1f, 0.1f, c);
            }
            for (float hx : {0.12f, 0.6f}) {
                R(hx, 0.66f, 0.28f, 0.06f, c); R(hx, 0.84f, 0.28f, 0.06f, c);
                R(hx, 0.66f, 0.06f, 0.24f, c); R(hx + 0.22f, 0.66f, 0.06f, 0.24f, c);
            }
            break;
        default:   // chosen but hidden: a card back; not chosen yet: an empty outline
            if (shape == RPS_PICKED) { R(0.2f, 0.1f, 0.6f, 0.8f, dark); R(0.28f, 0.18f, 0.44f, 0.64f, c); }
            else { R(0.2f, 0.1f, 0.6f, 0.04f, dark); R(0.2f, 0.86f, 0.6f, 0.04f, dark); R(0.2f, 0.1f, 0.04f, 0.8f, dark); R(0.76f, 0.1f, 0.04f, 0.8f, dark); }
    }
}

// The hide and seek tiebreak: everyone tied for first, their throws (hidden until the reveal), best-of-3 wins, who's
// out; our three choices while the throw is open. A compact block from y top (304 at 720p, under the scoreboard when
// it's held): the title at text 3, cards 120 x 112, captions at text 1.5.
void Game::drawRps(float W, float H, float s, float top) {
    const float ts = ui_.textScale(), cx = W * 0.5f;
    const int me = net_.clientId();
    const int n = (int)rps_.players.size();
    const float card = 120 * s, cardH = 112 * s, gap = 12 * s;
    const Color gold = {1, 0.85f, 0.3f, 1};
    bool in = false;   // (we're still in it)
    for (auto& p : rps_.players) in |= p.id == me && !p.out;
    float pw = std::min(W - 32 * s, std::max(560 * s, n * (card + gap) + 40 * s));
    // title 16 + 24, sub-line, cards (64..176), the line under them at 188; then the choices / the winner
    float ph = (rps_.stage == RPS_PICK ? (in ? 258 : 216) : rps_.winner >= 0 ? 248 : 216) * s;
    float px = cx - pw * 0.5f, py = std::max(0.0f, std::min(top, H - ph - 8 * s));
    r_.rect(px, py, pw, ph, {0.03f, 0.035f, 0.05f, 0.92f});
    r_.rect(px, py, pw, 3 * s, gold);
    float big = std::max(1.0f, std::round(ts * 1.4f)), mid = std::max(1.0f, std::round(ts * 0.9f)), small = ui_.hintScale();
    auto centered = [&](const std::string& t, float y, float k, Color c) { r_.text(cx - r_.textWidth(t, k) * 0.5f, y, k, t, c); };
    centered("TIEBREAK - ROCK PAPER SCISSORS", py + 16 * s, big, gold);
    std::string sub = rps_.bestOf3 ? "BEST OF 3 - FIRST TO 2 WINS THE MATCH" : "EVERYONE THROWS - THE LOSING SHAPE IS OUT";
    sub += "   THROW " + std::to_string(std::max(1, rps_.throwNo));
    centered(sub, py + 48 * s, small, kGrey);
    // the players, a card each (squeezed together when there are many)
    float step = n > 1 ? std::min(card + gap, (pw - 20 * s - card) / (n - 1)) : 0;
    float x0 = cx - (card + step * std::max(0, n - 1)) * 0.5f, y0 = py + 64 * s;
    const bool reveal = rps_.stage != RPS_PICK;
    for (int k = 0; k < n; k++) {
        const auto& p = rps_.players[k];
        float x = x0 + k * step;
        bool lost = p.out, won = rps_.winner == p.id;
        Color frame = won ? kGreenOk : lost ? Color{0.45f, 0.2f, 0.2f, 1} : p.id == me ? rgba(cfg_.colorFriendly) : Color{0.3f, 0.32f, 0.36f, 1};
        r_.rect(x, y0, card, cardH, frame);
        r_.rect(x + 2 * s, y0 + 2 * s, card - 4 * s, cardH - 4 * s, {0.07f, 0.08f, 0.1f, 1});
        std::string nm = upper(net_.playerName(p.id));
        if (nm.empty()) nm = "P" + std::to_string(p.id);
        while (nm.size() > 3 && r_.textWidth(nm, small) > card - 12 * s) nm.pop_back();
        r_.text(x + (card - r_.textWidth(nm, small)) * 0.5f, y0 + 8 * s, small, nm, p.id == me ? rgba(cfg_.colorFriendly) : kWhite);
        int shape = p.shape;
        if (!reveal && p.id == me && rpsMine_ != RPS_NONE) shape = rpsMine_;   // (our own pick, before the reveal)
        Color sc = lost ? Color{0.5f, 0.3f, 0.3f, 1} : won ? kGreenOk : Color{0.92f, 0.9f, 0.85f, 1};
        float icon = 64 * s;
        drawRpsShape(x + (card - icon) * 0.5f, y0 + 24 * s, icon, shape, sc);
        std::string st = lost ? "OUT" : reveal || (p.id == me && shape != RPS_NONE) ? rpsName(shape) : shape == RPS_NONE ? "CHOOSING..." : "READY";
        r_.text(x + (card - r_.textWidth(st, small)) * 0.5f, y0 + 88 * s, small, st, lost ? kRed : reveal ? kWhite : shape == RPS_NONE ? kGrey : kGreenOk);
        if (rps_.bestOf3 && !lost)   // best of 3: a pip per throw won (in the card's last strip)
            for (int w = 0; w < 2; w++) {
                float bx = x + card * 0.5f - 13 * s + w * 16 * s;
                r_.rect(bx, y0 + cardH - 13 * s, 10 * s, 8 * s, {0.25f, 0.27f, 0.3f, 1});
                if (w < p.wins) r_.rect(bx + 2 * s, y0 + cardH - 11 * s, 6 * s, 4 * s, gold);
            }
    }
    float y = y0 + cardH + 12 * s;
    if (rps_.stage == RPS_PICK) {
        float left = opts_.debugRps ? kRpsPickTime : net_.mode().timeLeft;
        centered((in ? "CHOOSE - " : "THEY'RE CHOOSING - ") + std::to_string((int)std::ceil(left)), y, mid, kWhite);
        if (in) {   // our three choices, on the weapon slot keys
            float bw = 150 * s, bx = cx - (3 * bw + 2 * gap) * 0.5f, by = y + 24 * s;
            for (int k = 0; k < 3; k++) {
                int shape = RPS_ROCK + k;
                bool sel = rpsMine_ == shape;
                float x = bx + k * (bw + gap);
                r_.rect(x, by, bw, 34 * s, sel ? gold : Color{0.3f, 0.32f, 0.36f, 1});
                r_.rect(x + 2 * s, by + 2 * s, bw - 4 * s, 30 * s, sel ? Color{0.3f, 0.24f, 0.05f, 1} : Color{0.07f, 0.08f, 0.1f, 1});
                std::string key = upper(inputName(binds().code[slotAction(k)][0]));
                std::string t2 = (key.empty() ? "" : "[" + key + "] ") + rpsName(shape);
                r_.text(x + (bw - r_.textWidth(t2, small)) * 0.5f, by + 17 * s - r_.textHeight(small) * 0.5f, small, t2, sel ? gold : kWhite);
            }
        }
    } else {
        centered(upper(rps_.result), y, mid, rps_.winner >= 0 ? kGreenOk : kWhite);
        if (rps_.winner >= 0) centered(upper(net_.playerName(rps_.winner)) + " WINS THE MATCH", y + r_.textHeight(mid) + 8 * s, big, kGreenOk);
    }
}

// ---- The HUD's elements: each drawn from its values, so the live HUD and Settings > HUD's editor (sample values in
// the main menu, the live ones in game) are the same code; each returns its box on screen (draw false: only that, for
// the editor's outlines and hit tests).
//
// Each element is laid out by the layout engine (layout.h) in a Layout of its own, so it re-solves only when its own
// content, settings or theme change (most frames reuse the last solve): a floating box anchored to the screen's
// corner, edge or centre it belongs to, holding its content. ref is where the element's box puts that anchor point in
// the default look (its HUD offsets included); the theme (theme.cfg, theme.h) adds padding inside the box (it grows
// away from the screen edge it's anchored to, around the content on a centred axis), a margin that moves it, a
// background and border with rounded corners, a colour, a text size and an opacity. With no theme every element is
// where and as it always was.

namespace {
const char* const kHudElemLabels[Settings::HUD_COUNT] = {"HEALTH", "WEAPON NAME", "COOLDOWN BARS", "SPEED METER", "WEAPON LIST",
                                                        "WEAPON MODEL", "KEY OVERLAY", "GRAPPLE MODEL", "SCORE BOX", "KILL FEED", "CHAT"};
// a HUD element's theme opacity, for the draws of one scope (Renderer::setAlpha)
struct AlphaScope {
    Renderer& r; float old;
    AlphaScope(Renderer& rr, float a) : r(rr), old(rr.alpha()) { r.setAlpha(old * a); }
    ~AlphaScope() { r.setAlpha(old); }
};
}

// theme.cfg, looked at once a second (the file's key comes from the watcher thread: filewatch.h); a new error shows once
void Game::pollTheme() {
    if (std::fabs(time_ - themePolledAt_) < 1.0) return;   // (the frame's clock: no clock read on the other frames)
    themePolledAt_ = time_;
    if (themePath_.empty()) themePath_ = userDataPath("theme.cfg");
    theme_.poll(themePath_);
    if (theme_.error() != themeErrShown_) {
        themeErrShown_ = theme_.error();
        if (!themeErrShown_.empty()) notify(themeErrShown_, 6);
    }
}

const HudStyle& Game::hudStyle(int el) const {
    static const HudStyle kNone;
    return el >= 0 && el < Settings::HUD_COUNT ? theme_.style(el) : kNone;
}

float Game::hudOpacity(int el) const {
    const HudStyle& st = hudStyle(el);
    const float a = st.has(HudStyle::OPACITY) ? st.opacity : 1.0f;
    return rankedPlayer() ? std::max(a, kRankedMinOpacity) : a;   // (ranked: a theme can dim the HUD, not hide it)
}

// The element's colour: its own HUD COLOR when that's on, else the theme's, else the built-in one (c; its alpha kept)
Color Game::hudTint(int el, Color c) const {
    const Settings::HudElem& e = cfg_.hud[el];
    if (e.custom) return {e.color.x, e.color.y, e.color.z, c.a};
    const HudStyle& st = hudStyle(el);
    if (!st.has(HudStyle::COLOR)) return c;
    const Color t = st.color.get(rgba(cfg_.colorFriendly), rgba(cfg_.colorEnemy));
    return {t.r, t.g, t.b, c.a * t.a};
}

lay::Layout& Game::hudOpen(int el, float W, float H, float ax, float ay, float refX, float refY, float k, lay::Decl d, const float* defPad, bool themePad) {
    const HudStyle& st = hudStyle(el);
    lay::Layout& L = hudLay_[el];
    L.begin(W, H);
    float pad[4] = {0, 0, 0, 0};
    if (themePad && st.has(HudStyle::PADDING)) for (int i = 0; i < 4; i++) pad[i] = st.pad[i] * k;
    else if (defPad) for (int i = 0; i < 4; i++) pad[i] = defPad[i];
    float mx = 0, my = 0;
    if (st.has(HudStyle::MARGIN)) { mx = (st.margin[0] - st.margin[2]) * k; my = (st.margin[1] - st.margin[3]) * k; }
    d.padding(pad[0], pad[1], pad[2], pad[3]).floatAt(ax, ay, ax, ay, refX - ax * W + mx, refY - ay * H + my);
    L.open(lay::id("hud"), d, kHudElemLabels[el]);
    return L;
}

// True when element el's declaration would be the same as last frame's (the screen, the theme's version and v: what
// its declaration is made from), so its last layout stands and it declares nothing (Layout::same: most frames).
bool Game::hudSame(int el, float W, float H, const float* v, int n) {
    float key[lay::Layout::kMaxKey / sizeof(float)];
    if (n + 3 > (int)(lay::Layout::kMaxKey / sizeof(float))) { hudLay_[el].same(nullptr, lay::Layout::kMaxKey + 1); return false; }
    const uint32_t ver = theme_.version();
    key[0] = W; key[1] = H; std::memcpy(&key[2], &ver, 4);
    std::memcpy(key + 3, v, sizeof(float) * (size_t)n);
    return hudLay_[el].same(key, (n + 3) * (int)sizeof(float));
}

// An element of one box of content (cw x ch), its anchor point at (refX, refY): its box, and the content's in c.
lay::Rect Game::hudSimple(int el, float W, float H, float ax, float ay, float refX, float refY, float k, float cw, float ch, lay::Rect& c) {
    const float v[] = {refX, refY, k, cw, ch};
    lay::Layout& L = hudLay_[el];
    if (!hudSame(el, W, H, v, 5)) {
        hudOpen(el, W, H, ax, ay, refX, refY, k, lay::Decl());
        L.leaf(lay::id("content"), lay::Decl().size(lay::Size::fixed(cw), lay::Size::fixed(ch)));
        hudClose(el);
    }
    c = L.rect(1);
    return hudRect_[el] = L.rect(0);
}

lay::Rect Game::hudClose(int el) {
    lay::Layout& L = hudLay_[el];
    L.close();
    L.solve();
    return hudRect_[el] = L.rect(0);
}

// The element's own box: the theme's background (else def; alpha 0: none) and border, its corners rounded.
void Game::hudBoxDraw(int el, const lay::Rect& r, float k, Color def) {
    const HudStyle& st = hudStyle(el);
    const Color fc = rgba(cfg_.colorFriendly), ec = rgba(cfg_.colorEnemy);
    const Color bg = st.has(HudStyle::BACKGROUND) ? st.background.get(fc, ec) : def;
    const float rad = st.has(HudStyle::RADIUS) ? st.radius * k : 0;
    if (bg.a > 0) r_.roundRect(r.x, r.y, r.w, r.h, rad, bg);
    if (st.has(HudStyle::BORDER) && st.borderW > 0) r_.roundRect(r.x, r.y, r.w, r.h, rad, st.border.get(fc, ec), std::max(1.0f, st.borderW * k));
}

float Game::hudFont(int el) const {
    const HudStyle& st = hudStyle(el);
    return (st.has(HudStyle::FONT) ? st.fontScale : 1.0f) * cfg_.hud[el].scale;
}

Game::HudRect Game::drawHudHealth(const Settings::HudElem& e, float W, float H, float s, int hp, Color num, Color bar, bool draw) {
    // health: number + bar, bottom left (the content 200 x 50, its bottom 20 over the screen's)
    const int el = Settings::HUD_HEALTH;
    const float k = s * e.scale;
    lay::Rect c;
    const lay::Rect r = hudSimple(el, W, H, 0, 1, 24 * s + e.x * s, H - 20 * k + e.y * s, k, 200 * k, 50 * k, c);
    if (draw) {
        AlphaScope a(r_, hudOpacity(el));
        hudBoxDraw(el, r, k, {0, 0, 0, 0});
        float hts = std::max(1.0f, std::round(ui_.textScale() * 2 * hudFont(el)));
        r_.text(c.x, c.y, hts, std::to_string(hp), hp > 30 ? num : kRed);
        r_.rect(c.x, c.y + 42 * k, 200 * k, 8 * k, {0, 0, 0, 0.5f});
        r_.rect(c.x, c.y + 42 * k, 200 * k * clampf(hp / 100.0f, 0, 1), 8 * k, hp > 30 ? bar : kRed);
    }
    return {r.x, r.y, r.w, r.h};
}

Game::HudRect Game::drawHudWeaponName(const Settings::HudElem& e, float W, float H, float s, const std::string& wn, Color c, bool draw) {
    // current weapon name, bottom right (its right edge 24 in, its top 50 up)
    const int el = Settings::HUD_WEAPON;
    const float k = s * e.scale;
    float wts = std::max(1.0f, std::round(ui_.textScale() * 1.5f * hudFont(el))), tw = hudWidth(hudCapW_[0], wn.c_str(), wts), th = r_.textHeight(wts);
    lay::Rect t;
    const lay::Rect r = hudSimple(el, W, H, 1, 1, W - 24 * s + e.x * s, H - 50 * k + th + e.y * s, k, tw, th, t);
    if (draw) {
        AlphaScope a(r_, hudOpacity(el));
        hudBoxDraw(el, r, k, {0, 0, 0, 0});
        r_.text(t.x, t.y, wts, wn, c);
    }
    return {r.x, r.y, r.w, r.h};
}

Game::HudRect Game::drawHudBars(const Settings::HudElem& e, float W, float H, float s, const HudBarVals& v, Color full, bool draw) {
    // dash + grapple cooldown bars under the crosshair (full = ready)
    const int el = Settings::HUD_BARS;
    const HudStyle& st = hudStyle(el);
    const float k = s * e.scale, bcx = W * 0.5f + e.x * s, bcy = H * 0.5f + e.y * s;
    // (rows 16 apart, captions at text 1.5 centred on their bar: text 1 was below the redesign's smallest)
    const float lts = std::max(r_.minTextScale(), ui_.textScale() * 0.75f * hudFont(el)), lh = r_.textHeight(lts);
    const float bw = 90 * k, bh = 5 * k, wW = hudWidth(hudCapW_[1], "WEAPON", lts), capW = wW + 6 * k;   // (the widest caption)
    const float rowStep = bh + (st.has(HudStyle::GAP) ? st.gap * k : 11 * k);
    // the content: captions + bars, from the widest caption's left to the bars' outline, the first bar's outline down
    const float cw = capW + bw + 1, ch = 2 * rowStep + bh + 2;
    const float left = bcx - bw * 0.5f - capW, top = bcy + 36 * k - 1;
    lay::Rect c;
    const lay::Rect r = hudSimple(el, W, H, 0.5f, 0.5f, left + cw * 0.5f, top + ch * 0.5f, k, cw, ch, c);
    if (!draw) return {r.x, r.y, r.w, r.h};
    AlphaScope a(r_, hudOpacity(el));
    hudBoxDraw(el, r, k, {0, 0, 0, 0});
    const float bx = c.x + capW, y0 = c.y + 1;
    auto bar = [&](float y, const char* label, float lw, float remaining, float total) {
        float f = clampf(1.0f - remaining / total, 0, 1);
        r_.rect(bx - 1, y - 1, bw + 2, bh + 2, {0, 0, 0, 0.8f});
        r_.rect(bx, y, bw * f, bh, f >= 1 ? full : Color{0.6f, 0.62f, 0.66f, 0.9f});
        r_.text(bx - lw - 6 * k, y + (bh - lh) * 0.5f, lts, label, f >= 1 ? kWhite : kGrey);
    };
    bar(y0, "WEAPON", wW, v.wRem, v.wTot);
    {   // dash: a pip per charge (the ground refills them, a bhop landing one): a spent charge is empty; a charge
        // that's there but can't be dashed yet (the dash still running, or the 0.6 s cooldown since it ended) is a
        // whole pip in the off color (light grey, no progress fill), and the moment that dash is allowed it turns
        // wholly to the on color; a spent charge stays empty while it recharges (1.2 s) and after, until a refill
        // (owner 1.18.7: "light grey that turns to blue, the whole bar not the slider"; a fill growing from empty
        // made one dash look like it had spent both charges)
        const int n = v.dashN, have = std::min(v.dashHave, n);
        const Color pip = v.dashReady ? full : Color{0.6f, 0.62f, 0.66f, 0.9f};
        float y = y0 + rowStep, gap = 4 * k, pw = (bw - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++) {
            float px = bx + i * (pw + gap);
            r_.rect(px - 1, y - 1, pw + 2, bh + 2, {0, 0, 0, 0.8f});
            if (i < have) r_.rect(px, y, pw, bh, pip);
        }
        r_.text(bx - hudWidth(hudCapW_[2], "DASH", lts) - 6 * k, y + (bh - lh) * 0.5f, lts, "DASH", have > 0 && v.dashReady ? kWhite : kGrey);
    }
    bar(y0 + 2 * rowStep, "HOOK", hudWidth(hudCapW_[3], "HOOK", lts), v.hookRem, v.hookTot);
    return {r.x, r.y, r.w, r.h};
}

Game::HudRect Game::drawHudSpeed(const Settings::HudElem& e, float W, float H, float s, float v, bool draw) {
    // speed meter: speed is unbounded, so the bar fills on a saturating exponential curve (1 - e^(-v/20)): half full
    // at ~14 m/s, 90% at ~46 m/s, always room to grow. Number shows the real value.
    const int el = Settings::HUD_SPEED;
    const float k = s * e.scale, bcx = W * 0.5f + e.x * s, y = H * 0.5f + 84 * k + e.y * s;
    const float bw = 90 * k, bh = 5 * k, bx = bcx - bw * 0.5f;
    const float lts = std::max(r_.minTextScale(), ui_.textScale() * 0.75f * hudFont(el)), lh = r_.textHeight(lts), ly = y + (bh - lh) * 0.5f;
    char buf[16]; snprintf(buf, sizeof buf, "%.1f", v);
    // the content: caption, bar and room for the number ("99.9": the box doesn't move as the number changes)
    const float capW = hudWidth(hudCapW_[4], "SPEED", lts) + 6 * k, numW = 6 * k + hudWidth(hudCapW_[5], "99.9", lts);
    const float top = std::min(y - 1, ly), bottom = std::max(y + bh + 1, ly + lh);
    const float cw = capW + bw + numW, ch = bottom - top, left = bx - capW;
    lay::Rect c;
    const lay::Rect r = hudSimple(el, W, H, 0.5f, 0.5f, left + cw * 0.5f, top + ch * 0.5f, k, cw, ch, c);
    if (!draw) return {r.x, r.y, r.w, r.h};
    AlphaScope a(r_, hudOpacity(el));
    hudBoxDraw(el, r, k, {0, 0, 0, 0});
    const float x0 = c.x + capW, y0 = c.y + (y - top), ly0 = c.y + (ly - top);
    float f = 1.0f - std::exp(-v / 20.0f);
    Color col = hudTint(el, Color{1.0f, 0.85f - 0.6f * f, 0.3f - 0.2f * f, 0.95f});
    r_.rect(x0 - 1, y0 - 1, bw + 2, bh + 2, {0, 0, 0, 0.8f});
    r_.rect(x0, y0, bw * f, bh, col);
    for (float m : {5.0f, 10.0f, 20.0f, 40.0f, 80.0f}) // tick marks at doubling speeds
        r_.rect(x0 + bw * (1.0f - std::exp(-m / 20.0f)), y0, 1, bh, {0, 0, 0, 0.6f});
    r_.text(c.x, ly0, lts, "SPEED", kWhite);
    r_.text(x0 + bw + 6 * k, ly0, lts, buf, kWhite);
    return {r.x, r.y, r.w, r.h};
}

Game::HudRect Game::drawHudSlots(const Settings::HudElem& e, float W, float H, float s, int mode, uint32_t mask, int held, Color heldC, bool live, bool draw) {
    // weapon slots in kWeaponSlots order: 1 rocket, 2 rifle, 3 laser, ... a row ending 38 in from the right, 74 up
    const int el = Settings::HUD_SLOTS;
    const HudStyle& st = hudStyle(el);
    const float k = s * e.scale;
    const float sts = std::max(1.0f, std::round(ui_.textScale() * 0.75f * hudFont(el))), th = r_.textHeight(sts);
    struct Slot { int kk, wi; bool has; double away; };
    Slot slots[WPN_COUNT];
    int n = 0;
    SlotLabels& sl = hudSlotW_;   // (each slot's label and width, kept: measured again only when its text changes)
    if (sl.ts != sts || sl.gen != r_.fontGeneration()) { sl.ts = sts; sl.gen = r_.fontGeneration(); for (int& a : sl.away) a = INT_MIN; }
    for (int kk = 0; kk < WPN_COUNT; kk++) {
        int wi = kWeaponSlots[kk];
        bool has = !mask || (mask >> wi & 1);
        if (!weaponInMode(wi, mode) || (isItMode(mode) && !has)) continue;   // (hide and seek / tag: just the knife and the locator)
        double awayLeft = live ? weaponAwayLeft(wi) : 0;   // a thrown knife / syringe: disabled, greyed with its timer
        // (the label: formatted and measured only when its seconds change; -1: none)
        const int away = awayLeft > 0 ? (int)std::ceil(awayLeft - 1e-6) : -1;
        if (sl.away[kk] != away) {
            sl.away[kk] = away;
            if (away >= 0) snprintf(sl.t[kk], sizeof sl.t[kk], "%d %s %d", weaponSlotKey(kk), weaponDef(wi).name, away);   // (seconds until it's back)
            else snprintf(sl.t[kk], sizeof sl.t[kk], "%d %s", weaponSlotKey(kk), weaponDef(wi).name);
            sl.w[kk] = r_.textWidth(sl.t[kk], sts);
        }
        slots[n++] = {kk, wi, has, awayLeft};
    }
    const float refX = W - 24 * s - 14 * k + e.x * s, refY = H - 74 * k + th + e.y * s;
    float key[5 + WPN_COUNT] = {refX, refY, k, th};   // (which slots, as a bit each, and how wide each is)
    uint32_t which = 0;
    for (int i = 0; i < n; i++) { which |= 1u << slots[i].kk; key[5 + i] = sl.w[slots[i].kk]; }
    std::memcpy(&key[4], &which, 4);
    lay::Layout& L = hudLay_[el];
    if (!hudSame(el, W, H, key, 5 + n)) {
        hudOpen(el, W, H, 1, 1, refX, refY, k, lay::Decl().row().spacing(st.has(HudStyle::GAP) ? st.gap * k : 14 * k));
        for (int i = 0; i < n; i++)
            L.leaf(lay::id(lay::id("slot"), (uint32_t)slots[i].kk), lay::Decl().size(lay::Size::fixed(sl.w[slots[i].kk]), lay::Size::fixed(th)));
        hudClose(el);
    }
    const lay::Rect r = hudRect_[el] = L.rect(0);
    if (draw && n + 1 <= L.count()) {
        AlphaScope a(r_, hudOpacity(el));
        hudBoxDraw(el, r, k, {0, 0, 0, 0});
        // (drawn from the right edge, each weapon's text left of the last as the solver placed them, but with the
        // same float steps the hand-placed row took: not a glyph moves by a rounding)
        float bx = W - 24 * s + e.x * s;   // (the row's right edge + one gap)
        if (st.has(HudStyle::PADDING)) bx -= st.pad[2] * k;
        if (st.has(HudStyle::MARGIN)) bx += (st.margin[0] - st.margin[2]) * k;
        const float gap = st.has(HudStyle::GAP) ? st.gap * k : 14 * k;
        if (st.has(HudStyle::GAP)) bx += gap - 14 * k;
        const float y = L.rect(1).y;
        for (int i = n - 1; i >= 0; i--) {
            const Slot& q = slots[i];
            bx -= L.rect(i + 1).w + gap;
            const bool inPool = q.has && q.away <= 0;
            r_.text(bx, y, sts, sl.t[q.kk], q.wi == held && q.away <= 0 ? heldC : inPool ? Color{0.85f, 0.87f, 0.9f, 0.9f} : Color{0.5f, 0.52f, 0.55f, 0.35f});
        }
    }
    return {r.x, r.y, r.w, r.h};
}

// The score box (top centre): your score / your team's on the left, the best other / the enemy's on the right, the
// mode's title and phase between; at least 440 wide and 64 high, as wide as the screen at most (sized from the real text
// sizes: HUD text is rounded to whole steps, so it's relatively bigger at small heights). Ranked: a gold stripe and tag.
Game::HudRect Game::drawHudScore(const ScoreText& t, float W, float H, float s, bool draw) {
    const int el = Settings::HUD_SCORE;
    const Settings::HudElem& e = cfg_.hud[el];
    const HudStyle& st = hudStyle(el);
    const float k = s * e.scale, fm = hudFont(el);
    const float ts = std::max(1.0f, std::round(ui_.textScale() * fm)), sts = ts * 1.5f, th = r_.textHeight(ts), sh = r_.textHeight(sts);
    // (the lines' widths: measured again only when a line, the size or the font changes)
    ScoreWidths& cw = hudScoreW_;
    if (cw.ts != ts || cw.gen != r_.fontGeneration()) { cw.ts = ts; cw.gen = r_.fontGeneration(); cw.title = "\x01"; cw.phase = cw.scoreA = cw.scoreB = cw.title; }
    if (cw.title != t.title) { cw.title = t.title; cw.wTitle = r_.textWidth(t.title, ts); }
    if (cw.phase != t.phase) { cw.phase = t.phase; cw.wPhase = r_.textWidth(t.phase, ts); }
    if (cw.scoreA != t.scoreA) { cw.scoreA = t.scoreA; cw.wA = r_.textWidth(t.scoreA, sts); }
    if (cw.scoreB != t.scoreB) { cw.scoreB = t.scoreB; cw.wB = r_.textWidth(t.scoreB, sts); }
    const float scoreW = std::max(cw.wA, cw.wB);
    const float defPad[4] = {20 * k, 8 * k, 20 * k, 8 * k};
    const float key[] = {W * 0.5f + e.x * s, 8 * s + e.y * s, k, s, scoreW, sh, cw.wTitle, cw.wPhase, th};
    lay::Layout& L = hudLay_[el];
    if (!hudSame(el, W, H, key, 9)) {
        hudOpen(el, W, H, 0.5f, 0, W * 0.5f + e.x * s, 8 * s + e.y * s, k,
                                 lay::Decl().row().size(lay::Size::fit(440 * k, W - 16 * s), lay::Size::fit(64 * k))
                                     .spacing(st.has(HudStyle::GAP) ? st.gap * k : 12 * k).align(lay::BETWEEN, lay::START), defPad);
        L.leaf(lay::id("a"), lay::Decl().size(lay::Size::fixed(scoreW), lay::Size::fixed(sh)).alignSelf(lay::CENTER));
        L.open(lay::id("mid"), lay::Decl().col().align(lay::START, lay::CENTER));
        L.leaf(lay::id("title"), lay::Decl().size(lay::Size::fixed(cw.wTitle), lay::Size::fixed(std::max(22 * k, th + 4 * k))));
        L.leaf(lay::id("phase"), lay::Decl().size(lay::Size::fixed(cw.wPhase), lay::Size::fixed(th)));
        L.close();
        L.leaf(lay::id("b"), lay::Decl().size(lay::Size::fixed(scoreW), lay::Size::fixed(sh)).alignSelf(lay::CENTER));
        hudClose(el);
    }
    const lay::Rect r = hudRect_[el] = L.rect(0);
    if (!draw || L.count() < 6) return {r.x, r.y, r.w, r.h};
    AlphaScope a(r_, hudOpacity(el));
    const Color fc = rgba(cfg_.colorFriendly), ec = rgba(cfg_.colorEnemy);
    hudBoxDraw(el, r, k, {0.05f, 0.06f, 0.08f, 0.55f});
    const float rad = st.has(HudStyle::RADIUS) ? std::min(st.radius * k, std::min(r.w, r.h) * 0.5f) : 0;
    const float stripeH = st.has(HudStyle::STRIPE) ? st.stripeW * k : 2 * ui_.s();   // (the panel's accent line)
    if (stripeH > 0) r_.rect(r.x + rad, r.y, r.w - 2 * rad, stripeH, st.has(HudStyle::STRIPE) ? st.stripe.get(fc, ec) : ui_.accent);
    ui_.box(r.x, r.x + r.w);   // (the title and phase fit inside it)
    const Color gold = {1.0f, 0.78f, 0.25f, 1};
    if (t.ranked) { // always visible on a ranked server
        const float hs = ui_.hintScale(), hh = r_.textHeight(hs);
        r_.rect(r.x + rad, r.y, r.w - 2 * rad, 3 * s, gold);
        const char* tag = t.unrated ? "RANKED - NOT RATED" : "RANKED";
        float tw = r_.textWidth(tag, hs) + 12 * s; // tag on the panel's right edge, clear of the objective line
        r_.rect(r.x + r.w + 4 * s, r.y, tw, hh + 8 * s, gold);
        r_.text(r.x + r.w + 10 * s, r.y + 4 * s, hs, tag, {0.1f, 0.08f, 0.02f, 1});
    }
    const lay::Rect& m = L.rect(2);
    const float cx = m.x + m.w * 0.5f;
    ui_.labelCentered(cx, L.rect(3).y, t.title, hudTint(el, kWhite), fm);
    ui_.labelCentered(cx, L.rect(4).y, t.phase, kGrey, fm);
    if (!t.scoreA.empty()) {
        const lay::Rect &qa = L.rect(1), &qb = L.rect(5);
        r_.text(qa.x, qa.y, sts, t.scoreA, fc);
        r_.text(qb.x + qb.w - cw.wB, qb.y, sts, t.scoreB, ec);
    }
    ui_.noBox();   // (the notice lines under it are as wide as the screen)
    return {r.x, r.y, r.w, r.h};
}

// The score box's text: the mode's title, its phase line, the two scores (sample: a made-up free for all, the HUD
// editor outside a game).
void Game::scoreText(ScoreText& t, bool sample) const {
    // (every line formatted into a buffer and assigned: the strings keep their capacity, so no allocation per frame)
    t.ranked = t.unrated = false;
    if (sample) {
        t.title = modeTitle(MODE_FFA); t.phase = "2:00  -  1ST OF 6"; t.scoreA = "12"; t.scoreB = "9";
        return;
    }
    const ModeState& ms = net_.mode();
    const int me = net_.clientId();
    const PlayerSnap* mep = net_.findPlayer(me);
    const int myTeam = mep ? (mep->team() == TEAM_SPECTATOR ? TEAM_A : mep->team()) : TEAM_A;
    char b[160];
    if (ms.flags & MS_TIMETRIAL) t.title = "TIME TRIAL - VIADUCT";
    else if (ms.flags & MS_RANKED) { snprintf(b, sizeof b, "%s - %s", rankedKind(ms.arenaVariant), arenaVariant(ms.arenaVariant).title); t.title = b; }
    else if (ms.mode == MODE_ARENA || (ms.mode == MODE_TOURNEY && ms.phase != PHASE_WARMUP)) {
        snprintf(b, sizeof b, "%s - %s", modeTitle(ms.mode), arenaVariant(ms.arenaVariant).title); t.title = b;
    } else t.title = modeTitle(ms.mode);
    b[0] = 0;
    switch (ms.phase) {
        case PHASE_WARMUP: {
            if (ms.timeLeft > 0) { snprintf(b, sizeof b, "MATCH STARTS IN %d", (int)std::ceil(ms.timeLeft)); break; }
            const int code = binds().code[A_VOTESTART][0];   // (the key's name: looked up when the bind changes)
            if (code != voteKeyCode_) { voteKeyCode_ = code; voteKeyName_ = inputName(code); }
            snprintf(b, sizeof b, isRaceMode(ms.mode) ? "PRACTICE (UNTIMED) - %s TO START THE RACE" : "WARMUP - %s TO VOTE START", voteKeyName_.c_str());
            break;
        }
        case PHASE_FREEZE: // free for all is one long round: just the countdown
            if (ms.mode == MODE_FFA) snprintf(b, sizeof b, "STARTING IN %d", (int)std::ceil(ms.timeLeft));
            else snprintf(b, sizeof b, "ROUND %d - STARTS IN %d", ms.round, (int)std::ceil(ms.timeLeft));
            break;
        case PHASE_LIVE: {   // the round clock in every timed mode (hudclock.h)
            char line[32];
            hudLiveLine(ms, line, sizeof line);
            snprintf(b, sizeof b, "%s", line);
            if (ms.mode == MODE_FFA) {   // your place (X10): "2ND OF 8", "=2ND" when shared
                int of = 0; bool tied = false;
                if (int k = ffaPlace(net_.latest().players, net_.clientId(), &of, &tied); k > 0 && of > 1)
                    snprintf(b, sizeof b, "%s%s%s%d%s OF %d", line, line[0] ? "  -  " : "", tied ? "=" : "", k, ordinalSuffix(k), of);
            }
            break;
        }
        case PHASE_ROUND_END: snprintf(b, sizeof b, "ROUND OVER"); break;
        case PHASE_MATCH_END: snprintf(b, sizeof b, "MATCH OVER"); break;
    }
    t.phase = b;
    t.ranked = (ms.flags & MS_RANKED) != 0;
    t.unrated = unratedServer_;
    t.scoreA.clear(); t.scoreB.clear();
    if (modeIsTeamBased(ms.mode)) {
        snprintf(b, sizeof b, "%d", myTeam == TEAM_A ? ms.scoreA : ms.scoreB); t.scoreA = b;
        snprintf(b, sizeof b, "%d", myTeam == TEAM_A ? ms.scoreB : ms.scoreA); t.scoreB = b;
    } else if (mep) {
        int best = -9999;
        for (auto& p : net_.latest().players) if (p.id != me && !(p.flags & PS_SPECTATOR)) best = std::max<int>(best, p.score);
        snprintf(b, sizeof b, "%d", (int)mep->score); t.scoreA = b;
        if (best == -9999) t.scoreB = "-"; else { snprintf(b, sizeof b, "%d", best); t.scoreB = b; }
    }
}

// The kill feed (top right, under the verbose info block: top is where it starts, 12 at 720p without it): a line per
// kill, each on its own dark box (the theme's background, border and padding style each line), 2 apart.
Game::HudRect Game::drawHudKillFeed(float W, float H, float s, float top, bool sample, bool draw) {
    const int el = Settings::HUD_KILLFEED;
    const Settings::HudElem& e = cfg_.hud[el];
    const HudStyle& st = hudStyle(el);
    const float k = s * e.scale, ts = ui_.textScale() * hudFont(el), th = r_.textHeight(ts);
    static const Feed kSample[] = {{"wraith [ROCKET] lynx", {1, 1, 1, 1}, 0}, {"ghost [RIFLE] ember", {1, 1, 1, 1}, 0}, {"lynx [KNIFE] ghost", {1, 1, 1, 1}, 0}};
    const Feed* lines[24];
    int n = 0;
    if (sample) for (const Feed& f : kSample) lines[n++] = &f;
    else for (const Feed& f : killFeed_) if (n < 24) lines[n++] = &f;
    const lay::Align al = !st.has(HudStyle::ALIGN) ? lay::END : st.align == 0 ? lay::START : st.align == 1 ? lay::CENTER : lay::END;
    float pad[4] = {6 * k, 3 * k, 6 * k, 3 * k};
    if (st.has(HudStyle::PADDING)) for (int i = 0; i < 4; i++) pad[i] = st.pad[i] * k;
    float key[24] = {W - 8 * s + e.x * s, top - 3 * s + e.y * s, k, ts, th, (float)n};
    for (int i = 0; i < n && 6 + i < 24; i++) key[6 + i] = feedWidth(*lines[i], ts);
    lay::Layout& L = hudLay_[el];
    if (!hudSame(el, W, H, key, n <= 18 ? 6 + n : 99)) {
        hudOpen(el, W, H, 1, 0, W - 8 * s + e.x * s, top - 3 * s + e.y * s, k,
                lay::Decl().col().spacing(st.has(HudStyle::GAP) ? st.gap * k : 2 * k).align(lay::START, al), nullptr, false);
        for (int i = 0; i < n; i++) {
            L.open(lay::id(lay::id("line"), (uint32_t)i), lay::Decl().padding(pad[0], pad[1], pad[2], pad[3]));
            L.leaf(lay::id(lay::id("text"), (uint32_t)i), lay::Decl().size(lay::Size::fixed(feedWidth(*lines[i], ts)), lay::Size::fixed(th)));
            L.close();
        }
        hudClose(el);
    }
    const lay::Rect r = hudRect_[el] = L.rect(0);
    if (draw) {
        AlphaScope a(r_, hudOpacity(el));
        for (int i = 0; i < n && 2 + 2 * i < L.count(); i++) {
            hudBoxDraw(el, L.rect(1 + 2 * i), k, {0, 0, 0, 0.4f});
            const lay::Rect& q = L.rect(2 + 2 * i);
            drawFeedText(q.x, q.y, ts, *lines[i]);
        }
    }
    return {r.x, r.y, r.w, r.h};
}

// Chat (bottom left): the recent lines (typing: the whole recent history, on a dark box), the SAY line under them.
Game::HudRect Game::drawChatBox(float W, float H, float s, float ts0, bool sample, bool draw) {
    const int el = Settings::HUD_CHAT;
    const Settings::HudElem& e = cfg_.hud[el];
    const HudStyle& st = hudStyle(el);
    const float k = s * e.scale, ts = ts0 * hudFont(el), th = r_.textHeight(ts);
    if (draw) while (!chatFeed_.empty() && chatFeed_.front().until < time_ && !chatting_) chatFeed_.pop_front();
    static const Feed kSample[] = {{"wraith: nice shot", {1, 1, 1, 1}, 0, 0, true}, {"ember: gg", {1, 1, 1, 1}, 0, 0, true}};
    const Feed* lines[48];
    int n = 0;
    const bool showLines = !e.hidden || chatting_;   // (hidden: only while typing)
    if (sample) for (const Feed& f : kSample) lines[n++] = &f;
    else if (showLines)
        for (const Feed& c : chatting_ ? chatLog_ : chatFeed_)   // (chat off: players' lines are hidden, server messages stay)
            if (!(c.player && cfg_.chatOff) && n < 48) lines[n++] = &c;
    const lay::Align al = !st.has(HudStyle::ALIGN) ? lay::START : st.align == 0 ? lay::START : st.align == 1 ? lay::CENTER : lay::END;
    const float defPad[4] = {6 * k, 4 * k, 6 * k, 4 * k};
    auto lineW = [&](int i) { const float cw = lines[i]->crown ? th * 1.2f : 0; return (cw ? cw + 4 * s : 0) + feedWidth(*lines[i], ts); };
    float key[24] = {18 * s + e.x * s, H - 110 * s + e.y * s, k, s, th, (float)n};
    for (int i = 0; i < n && 6 + i < 24; i++) key[6 + i] = lineW(i);
    lay::Layout& L = hudLay_[el];
    if (!hudSame(el, W, H, key, n <= 18 ? 6 + n : 99)) {
        hudOpen(el, W, H, 0, 1, 18 * s + e.x * s, H - 110 * s + e.y * s, k,
                lay::Decl().col().size(lay::Size::fixed(520 * k), lay::Size::fit()).spacing(st.has(HudStyle::GAP) ? st.gap * k : 4 * k)
                    .align(lay::START, al), defPad);
        for (int i = 0; i < n; i++) L.leaf(lay::id(lay::id("line"), (uint32_t)i), lay::Decl().size(lay::Size::fixed(lineW(i)), lay::Size::fixed(th)));
        hudClose(el);
    }
    const lay::Rect r = hudRect_[el] = L.rect(0);
    if (!draw) return {r.x, r.y, r.w, r.h};
    {
        AlphaScope a(r_, hudOpacity(el));
        if (n > 0 && (chatting_ || st.has(HudStyle::BACKGROUND) || st.has(HudStyle::BORDER))) hudBoxDraw(el, r, k, chatting_ ? Color{0, 0, 0, 0.4f} : Color{0, 0, 0, 0});
        for (int i = 0; i < n && i + 1 < L.count(); i++) {
            const Feed& c = *lines[i];
            const lay::Rect& q = L.rect(i + 1);
            float cw = c.crown ? th * 1.2f : 0;
            if (c.crown) drawCrown(q.x, q.y + th * 0.1f, cw, c.crown);
            drawFeedText(q.x + (cw ? cw + 4 * s : 0), q.y, ts, c);
        }
    }
    if (sample) return {r.x, r.y, r.w, r.h};
    if (chatting_ && console_) drawConsole(W, H, s, ts0);
    else if (chatting_) {
        if ((!uiIn_.chars.empty() || uiIn_.backspace) && time_ >= nextTypingSend_) { net_.sendCommand("typing"); nextTypingSend_ = time_ + 0.25; }
        chatEdit(120, false);
        float lts = std::max(1.0f, std::round(ui_.textScale()));
        const float x = r.x + (st.has(HudStyle::PADDING) ? st.pad[0] * k : defPad[0]), y = r.y + r.h + 10 * k;
        ui_.label(x, y, "SAY: " + chatText_, kWhite);
        drawCaret(x, y, lts, false, "SAY: " + chatText_.substr(0, chatCursor_));
    }
    return {r.x, r.y, r.w, r.h};
}

Game::HudRect Game::drawKeyOverlay(float W, float H, float s, bool sample, bool draw) {
    const int el = Settings::HUD_KEYS;
    const auto& e = cfg_.hud[el];
    const HudStyle& st = hudStyle(el);
    const float k = s * e.scale, u = 30 * k, g = st.has(HudStyle::GAP) ? st.gap * k : 4 * k, side = 4 * u + 3 * g;
    lay::Rect c;
    const lay::Rect r = hudSimple(el, W, H, 0.5f, 1, W * 0.5f + e.x * s, H - 230 * s + side + e.y * s, k, side, side, c);
    if (!draw) return {r.x, r.y, r.w, r.h};
    AlphaScope a(r_, hudOpacity(el));
    hudBoxDraw(el, r, k, {0, 0, 0, 0});
    const float ox = c.x, oy = c.y, rad = st.has(HudStyle::RADIUS) ? st.radius * k : 0;
    Color lit = hudTint(el, rgba(cfg_.colorFriendly, 0.9f));
    float ts = std::max(1.0f, std::round(ui_.textScale() * 0.5f * hudFont(el)));
    // alive: our own keys; spectating: what the watched player is holding (from the snapshot)
    // (sample: Settings > HUD outside a game, nothing held)
    const int spec = sample || net_.selfAlive() ? -1 : specKeysOf();
    const uint16_t held = spec >= 0 ? net_.findPlayer(spec)->keys : 0;
    auto key = [&](float col, float row, float wUnits, const char* label, int action, uint16_t bit) {
        float x = ox + col * (u + g), y = oy + row * (u + g), w = wUnits * u + (wUnits - 1) * g;
        bool on = spec >= 0 ? (held & bit) != 0 : !sample && isDown(action);
        r_.roundRect(x, y, w, u, rad, on ? lit : Color{0.05f, 0.06f, 0.08f, 0.55f});
        r_.text(x + (w - r_.textWidth(label, ts)) * 0.5f, y + (u - r_.textHeight(ts)) * 0.5f, ts, label, on ? Color{0, 0, 0, 1} : Color{0.85f, 0.87f, 0.9f, 0.9f});
    };
    key(1, 0, 1, "W", A_FORWARD, KEY_FWD);
    key(0, 1, 1, "A", A_LEFT, KEY_LEFT); key(1, 1, 1, "S", A_BACK, KEY_BACK); key(2, 1, 1, "D", A_RIGHT, KEY_RIGHT);
    key(3, 0, 1, "LMB", A_FIRE, KEY_FIRE); key(3, 1, 1, "RMB", A_ALTFIRE, KEY_ALT);
    key(0, 2, 2, "JUMP", A_JUMP, KEY_JUMP); key(2, 2, 2, "CROUCH", A_CROUCH, KEY_CROUCH);
    key(0, 3, 2, "DASH", A_DASH, KEY_DASH); key(2, 3, 2, "HOOK", A_GRAPPLE, KEY_HOOK);
    return {r.x, r.y, r.w, r.h};
}

// The HUD while alive (drawHud; preview: Settings > HUD in game, where the cooldown bars and the speed meter show
// although the game is paused)
void Game::drawHudLive(const ModeState& ms, const PlayerSnap* mep, float W, float H, float s, bool preview) {
    const auto* he = cfg_.hud;
    if (!he[Settings::HUD_HEALTH].hidden) drawHudElement(Settings::HUD_HEALTH, W, H, s, false);
    bool unarmed = isItMode(ms.mode) && !(mep && (mep->flags & PS_HASBOMB));
    if (!unarmed && !he[Settings::HUD_WEAPON].hidden) drawHudElement(Settings::HUD_WEAPON, W, H, s, false);
    const MoveState& mv = net_.predicted();
    // (the bars and the meter: hidden behind menus / the bars without abilities)
    if ((!paused_ || preview) && !(mv.mods & MOVE_RUN_ONLY) && !he[Settings::HUD_BARS].hidden) drawHudElement(Settings::HUD_BARS, W, H, s, false);
    if ((!paused_ || preview) && !he[Settings::HUD_SPEED].hidden) drawHudElement(Settings::HUD_SPEED, W, H, s, false);
    const float ts = ui_.textScale();
    if (cfg_.stickyCounter && !unarmed) {   // stickies out (the 7th removes the oldest)
        int out = 0;
        for (auto& pr : lastProjectiles_) out += pr.type == WPN_STICKY && pr.owner == net_.clientId();   // (this frame's)
        if (out > 0 || heldWeapon_ == WPN_STICKY) {
            const auto& e = he[Settings::HUD_WEAPON];
            float k = std::max(1.0f, std::round(ts * 0.9f * e.scale));
            std::string t = "STICKIES " + std::to_string(out) + "/" + std::to_string(weaponAlt().stickyMax);
            r_.text(W - 24 * s - r_.textWidth(t, k) + e.x * s, H - 128 * s * e.scale + e.y * s, k, t, out >= weaponAlt().stickyMax ? Color{1, 0.6f, 0.2f, 1} : hudTint(Settings::HUD_WEAPON, kWhite));
        }
    }
    {   // enemy stickies stuck on you: a blinking warning under the crosshair
        int on = 0;
        for (auto& pr : lastProjectiles_) on += (pr.flags & PROJ_ON_PLAYER) && pr.onPlayer == net_.clientId();
        if (on > 0) {
            float k = std::max(1.0f, std::round(ts * 1.2f));
            std::string t = on > 1 ? std::to_string(on) + " STICKIES ON YOU" : "STICKY ON YOU";
            float a = std::fmod((float)time_ * 2.5f, 1.0f) < 0.5f ? 1.0f : 0.55f;
            r_.text(W * 0.5f - r_.textWidth(t, k) * 0.5f, H * 0.5f - 90 * s, k, t, Color{1, 0.25f, 0.2f, a});   // (above the crosshair: the cooldown bars are under it)
        }
    }
    if (!unarmed && !he[Settings::HUD_SLOTS].hidden) drawHudElement(Settings::HUD_SLOTS, W, H, s, false);
}

// One element with its live values (sample: made-up ones, as Settings > HUD shows them outside a game: health 100, the
// first slot's weapon held, the bars full with both dash charges ready, 12 m/s, free for all's weapon list, no key lit)
Game::HudRect Game::drawHudElement(int el, float W, float H, float s, bool sample, bool draw) {
    const auto& e = cfg_.hud[el];
    const Color fc = rgba(cfg_.colorFriendly);
    switch (el) {
        case Settings::HUD_HEALTH:
            return drawHudHealth(e, W, H, s, sample ? 100 : net_.selfHealth(), hudTint(el, kWhite), hudTint(el, fc), draw);
        case Settings::HUD_WEAPON:
            return drawHudWeaponName(e, W, H, s, weaponDef(sample ? kWeaponSlots[0] : heldWeapon_).name, hudTint(el, kWhite), draw);
        case Settings::HUD_BARS: {
            HudBarVals v;
            if (!sample) {
                const MoveState& mv = net_.predicted();
                const int cw = heldWeapon_;
                // (the locator: its recharge; else the last shot's refire: a scoped rifle's or the bubble stream's, F23)
                v.wRem = (float)std::max(0.0, (cw == WPN_LOCATOR ? locatorReady_ : localNextFire_) - time_);
                v.wTot = cw == WPN_LOCATOR || localRefire_ <= 0 ? weaponDef(cw).refire : localRefire_;
                v.dashN = dashMaxCharges(mv, &net_.map());
                v.dashHave = mv.dashCharges;
                v.dashReady = dashReady(mv);   // (pmove's own rule; mid-burst a press is dropped, so not ready either: no
                                               // blue flash before the cooldown starts)
                v.hookRem = mv.hookCooldown;
                v.hookTot = kHookCooldown * moveValue(mv, MV_HOOK_COOLDOWN, &net_.map());
            }
            return drawHudBars(e, W, H, s, v, hudTint(el, fc), draw);
        }
        case Settings::HUD_SPEED: {
            const MoveState& mv = net_.predicted();
            return drawHudSpeed(e, W, H, s, sample ? 12.0f : std::sqrt(mv.vel.x * mv.vel.x + mv.vel.z * mv.vel.z), draw);
        }
        case Settings::HUD_SLOTS: {
            const ModeState& ms = net_.mode();
            return drawHudSlots(e, W, H, s, sample ? (int)MODE_FFA : (int)ms.mode, sample ? 0u : ms.weaponMask,
                                sample ? kWeaponSlots[0] : heldWeapon_, hudTint(el, fc), !sample, draw);
        }
        case Settings::HUD_KEYS: return drawKeyOverlay(W, H, s, sample, draw);
        case Settings::HUD_SCORE: {
            scoreText(hudScoreText_, sample);
            return drawHudScore(hudScoreText_, W, H, s, draw);
        }
        case Settings::HUD_KILLFEED: return drawHudKillFeed(W, H, s, hudFeedTop_ * s, sample, draw);
        case Settings::HUD_CHAT: return drawChatBox(W, H, s, ui_.textScale(), sample, draw);
        default: return {};
    }
}

// Settings > HUD outside a game (and in one while dead): every shown element with sample values where it really goes
void Game::drawHudSample(float W, float H, float s) {
    if (theme_.pollDue()) { if (themePath_.empty()) themePath_ = userDataPath("theme.cfg"); theme_.poll(themePath_); }   // (drawHud polls it in game)
    static const int els[] = {Settings::HUD_HEALTH, Settings::HUD_WEAPON, Settings::HUD_BARS, Settings::HUD_SPEED, Settings::HUD_SLOTS};
    for (int el : els) if (!cfg_.hud[el].hidden) drawHudElement(el, W, H, s, true);
    if (cfg_.hudVerbose && !cfg_.hud[Settings::HUD_KEYS].hidden) drawKeyOverlay(W, H, s, true);   // (verbose only, as in game)
}

// Settings > HUD, over the shell (drawSettings, before its popups): the selected element again at full strength while
// it changes (a slider held, or for 1.5 s after any change or a new pick), so a move / resize / recolour shows even where
// the panel covers it; then a thin accent outline round it (dashed and dim when it's hidden). Not the weapon model (3D).
void Game::drawHudPreviewTop() {
    const auto& e = cfg_.hud[hudSel_];
    const bool same = hudSel_ == hudPrevSel_ && e.x == hudPrevElem_.x && e.y == hudPrevElem_.y && e.scale == hudPrevElem_.scale &&
                      e.custom == hudPrevElem_.custom && e.hidden == hudPrevElem_.hidden && e.z == hudPrevElem_.z &&
                      e.pitch == hudPrevElem_.pitch && e.yaw == hudPrevElem_.yaw && e.roll == hudPrevElem_.roll &&
                      e.color.x == hudPrevElem_.color.x && e.color.y == hudPrevElem_.color.y && e.color.z == hudPrevElem_.color.z;
    if (!same) { hudChangedAt_ = time_; hudPrevSel_ = hudSel_; hudPrevElem_ = e; }
    if (Settings::isModelElem(hudSel_)) return;
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f;
    const bool sample = !settingsInGame_ || !net_.selfAlive();
    const bool shown = !e.hidden && (hudSel_ != Settings::HUD_KEYS || cfg_.hudVerbose);
    if (shown && (ui_.sliderHeld() || time_ - hudChangedAt_ < 1.5)) drawHudElement(hudSel_, W, H, s, sample);
    HudRect rc = drawHudElement(hudSel_, W, H, s, sample, false);
    const float pad = 4 * s, t = std::max(1.0f, std::round(1.5f * s));
    const float x = rc.x - pad, y = rc.y - pad, w = rc.w + 2 * pad, h = rc.h + 2 * pad;
    const Color& a = ui_.accent;
    if (shown) {   // a solid frame, gently pulsing
        const Color c = {a.r, a.g, a.b, 0.7f + 0.3f * std::sin((float)time_ * 5.0f)};
        r_.rect(x, y, w, t, c); r_.rect(x, y + h - t, w, t, c);
        r_.rect(x, y + t, t, h - 2 * t, c); r_.rect(x + w - t, y + t, t, h - 2 * t, c);
    } else {       // hidden: where it would be, dashed and dim
        const Color c = {a.r, a.g, a.b, 0.45f};
        const float d = 6 * s;
        for (float u = 0; u < w; u += 2 * d) { float l = std::min(d, w - u); r_.rect(x + u, y, l, t, c); r_.rect(x + u, y + h - t, l, t, c); }
        for (float u = 0; u < h; u += 2 * d) { float l = std::min(d, h - u); r_.rect(x, y + u, t, l, c); r_.rect(x + w - t, y + u, t, l, c); }
    }
}

void Game::drawHud() {
    QZONE("drawHud");
    // (the elements' layouts and styles, fetched together: drawn after the world pass, they're out of the caches, and
    // waiting on them one element at a time cost more than laying the elements out)
    // (two passes: the layouts' own lines and the styles first, all in flight at once; then what their pointers name)
#if defined(__GNUC__) || defined(__clang__)
    for (int el = 0; el < Settings::HUD_COUNT; el++) { __builtin_prefetch(&hudLay_[el]); __builtin_prefetch(&theme_.style(el)); }
#endif
    lay::Layout::prefetchScratch();
    for (int el = 0; el < Settings::HUD_COUNT; el++) hudLay_[el].prefetch();
    const float W = (float)r_.width(), H = (float)r_.height();
    float s = H / 720.0f;
    ui_.begin(&r_, uiIn_, s);
    pollTheme();
    float ts = ui_.textScale();
    if (paused_ && settingsInGame_ && !widgetEdit_ && hudPreviewOn()) {
        // Settings > HUD: the HUD's own elements live under the see-through shell (the world only lightly dimmed, so they
        // read clearly) and an edit shows as it's made; drawSettings outlines the selected one and draws it over the
        // shell while it changes. The score box, feed and chat stay off (they'd sit under the tab row).
        r_.rect(0, 0, W, H, {0.03f, 0.035f, 0.05f, 0.25f});
        drawCrosshair(W * 0.5f, H * 0.5f, s, false);   // (where the cooldown bars and the speed meter hang from)
        if (net_.selfAlive()) {
            drawHudLive(net_.mode(), net_.findPlayer(net_.clientId()), W, H, s, true);
            if (cfg_.hudVerbose && !cfg_.hud[Settings::HUD_KEYS].hidden) drawKeyOverlay(W, H, s);
        } else drawHudSample(W, H, s);   // (dead / spectating: made-up values, as the main menu's)
        if (!rankedPlayer()) {
            const bool wantWidgets = cfg_.hudWidgets;
            if (wantWidgets && widgets_.pollDue()) widgets_.poll(activeHudPath());
            if (cfg_.hudVerbose || (wantWidgets && !widgets_.empty())) computeHudVals();
            float vy = 12 * s;
            if (cfg_.hudVerbose) drawVerboseHud(W, s, vy);
            if (wantWidgets && !widgets_.empty()) widgets_.draw(r_, hudVals_, W, H, s);
        }
        drawNotices(12 * s, ts, s);
        drawPauseMenu(W * 0.5f, 96 * s);
        if (ui_.probing()) uiTestActions();
        return;
    }
    if (paused_ && settingsInGame_ && !widgetEdit_) {
        // settings in game (every tab but HUD): no HUD under the shell (it would cut the score and weapon bars), the
        // world dimmed behind it; notices in the strip over it
        r_.rect(0, 0, W, H, {0.03f, 0.035f, 0.05f, hudEdit_ ? 0.2f : 0.55f});   // (EDIT ON SCREEN: the world only lightly dimmed)
        drawNotices(12 * s, ts, s);
        drawPauseMenu(W * 0.5f, 96 * s);
        if (ui_.probing()) uiTestActions();
        return;
    }
    const ModeState& ms = net_.mode();
    int me = net_.clientId();
    const PlayerSnap* mep = net_.findPlayer(me);
    int myTeam = mep ? (mep->team() == TEAM_SPECTATOR ? TEAM_A : mep->team()) : TEAM_A;
    Color fc = rgba(cfg_.colorFriendly), ec = rgba(cfg_.colorEnemy);
    // (held scoreboard: known up front, the round-end message and the tiebreak make way for it)
    showScores_ = (isDown(A_SCORES) && !chatting_) || opts_.textHeavy || ms.phase == PHASE_MATCH_END;

    float cx = W * 0.5f, cy = H * 0.5f;
    // the sniper scope's mask and reticle; SCOPE OVERLAY at 0 turns them off and the normal crosshair stays (as the
    // rifle's and crossbow's zoom do)
    const bool scopeShown = scoped_ && scopeOverlay_ && cfg_.scopeOpacity > 0.001f;
    if (scopeShown) {
        // circular scope mask built from horizontal strips (one batch, no texture)
        float R = H * 0.46f;
        const Color k = {0, 0, 0, cfg_.scopeOpacity}; // VISUALS > SCOPE OVERLAY
        int strips = 96;
        for (int i = 0; i < strips; i++) {
            float y0 = cy - R + 2 * R * i / strips, y1 = cy - R + 2 * R * (i + 1) / strips;
            float yc = (y0 + y1) * 0.5f - cy;
            float half = std::sqrt(std::max(0.0f, R * R - yc * yc));
            r_.rect(0, y0, cx - half, y1 - y0 + 0.5f, k);
            r_.rect(cx + half, y0, W - cx - half, y1 - y0 + 0.5f, k);
        }
        r_.rect(0, 0, W, cy - R, k);
        r_.rect(0, cy + R, W, H - cy - R, k);
        r_.rect(cx - R, cy - 0.5f, 2 * R, 1, {0, 0, 0, 0.9f * std::max(cfg_.scopeOpacity, 0.3f)});
        r_.rect(cx - 0.5f, cy - R, 1, 2 * R, {0, 0, 0, 0.9f * std::max(cfg_.scopeOpacity, 0.3f)});
    }
    if (net_.selfAlive() && (net_.predicted().flags & MF_PULLED)) {   // another player's grapple holds you (dash or grab to break free)
        float big = std::max(2.0f, std::round(ts * 2));
        r_.text(cx - r_.textWidth("HOOKED", big) * 0.5f, cy - 60 * s, big, "HOOKED", Color{1, 0.45f, 0.3f, 1});
    }
    if (float hold = holdFireLeft(); hold > 0) { // the map's holdfire (the dojo): face each other, no shots yet
        float big = std::max(2.0f, std::round(ts * 2));
        std::string t = "HOLD FIRE " + std::to_string((int)std::ceil(hold));
        r_.text(cx - r_.textWidth(t, big) * 0.5f, cy - 90 * s, big, t, Color{1, 0.85f, 0.3f, 1});
    }
    if (ms.mode == MODE_TAG && (ms.phase == PHASE_FREEZE || ms.phase == PHASE_LIVE) && !(mep && (mep->flags & PS_HASBOMB)))
        drawTaggerMarker(W, H, s);   // tag: runners (and whoever watches) always see where the tagger is
    if (isItMode(ms.mode) && ms.phase == PHASE_LIVE && mep && (mep->flags & PS_HASBOMB)) { // the locator
        float sx, sy;
        if (time_ < locateUntil_ && r_.project(locatePos_ + Vec3(0, 1.0f, 0), sx, sy)) { // through walls: a box around them
            float bw = 26 * s, bh = 60 * s, th = 2 * s;
            Color c = {1, 0.85f, 0.2f, (float)std::min(1.0, (locateUntil_ - time_) * 3)};
            r_.rect(sx - bw, sy - bh, 2 * bw, th, c); r_.rect(sx - bw, sy + bh * 0.6f, 2 * bw, th, c);
            r_.rect(sx - bw, sy - bh, th, bh * 1.6f, c); r_.rect(sx + bw - th, sy - bh, th, bh * 1.6f, c);
            std::string n = net_.playerName(locateId_);
            r_.text(sx - r_.textWidth(n, ts) * 0.5f, sy - bh - r_.textHeight(ts) - 4 * s, ts, n, c);
        }
        std::string key = upper(inputName(binds().code[heldWeapon_ == WPN_LOCATOR ? A_FIRE : slotAction(weaponSlot(WPN_LOCATOR))][0]));
        std::string t = time_ >= locatorReady_ ? "LOCATOR READY" + (key.empty() ? std::string() : " - " + key) : "LOCATOR " + std::to_string((int)std::ceil(locatorReady_ - time_)) + "s";
        float lts = std::max(1.0f, std::round(ts * 0.9f));
        r_.text(cx - r_.textWidth(t, lts) * 0.5f, H - 140 * s, lts, t, time_ >= locatorReady_ ? Color{1, 0.85f, 0.2f, 1} : kGrey);
    }
    // hide and seek / tag: the seeker faces the wall while everyone hides (the countdown, what it's for above it: it
    // ran into the cooldown bars under it); everyone else gets a banner at the top of the notice list (below)
    const bool itBanner = isItMode(ms.mode) && ms.phase != PHASE_WARMUP && mep && mep->team() != TEAM_SPECTATOR;
    const float bts = std::max(1.0f, std::round(ts * 1.2f));
    const bool seekerWall = itBanner && ms.mode == MODE_HIDE && ms.phase == PHASE_FREEZE && (mep->flags & PS_HASBOMB);
    if (seekerWall) {
        r_.rect(0, 0, W, H, {0.02f, 0.02f, 0.03f, 1});
        float big = std::max(2.0f, std::round(ts * 3));
        std::string n = std::to_string((int)std::ceil(ms.timeLeft));
        r_.text(cx - r_.textWidth(n, big) * 0.5f, cy - 40 * s, big, n, kWhite);
        static const std::string kSeek = "YOU'RE SEEKING - FACE THE WALL AND COUNT";
        r_.text(cx - r_.textWidth(kSeek, bts) * 0.5f, cy - 64 * s, bts, kSeek, kWhite);
    }
    if (opts_.debugRps && rps_.at < 0) {   // --rpsshot: made-up players, the tiebreak's panel only
        rps_.players.clear();
        int n = opts_.debugRps == 3 ? 4 : 2;
        static const int shapes[4] = {RPS_ROCK, RPS_SCISSORS, RPS_ROCK, RPS_SCISSORS};
        for (int i = 0; i < n; i++) {
            RpsView::P p; p.id = i;
            p.shape = opts_.debugRps == 1 ? (i == 0 ? RPS_NONE : RPS_PICKED) : shapes[i];
            p.wins = opts_.debugRps == 2 ? (i == 0 ? 1 : 0) : 0;
            p.out = opts_.debugRps == 3 && shapes[i] == RPS_SCISSORS;
            rps_.players.push_back(p);
        }
        if (opts_.debugRps == 1) rps_.players[0].id = me;
        rps_.stage = opts_.debugRps == 1 ? RPS_PICK : RPS_REVEAL;
        rps_.throwNo = opts_.debugRps == 2 ? 2 : 1;
        rps_.bestOf3 = opts_.debugRps != 3;
        rps_.result = opts_.debugRps == 2 ? "p0's ROCK beats SCISSORS - 1-0" : opts_.debugRps == 3 ? "p1, p3 out (SCISSORS) - best of 3 between the last two" : "";
        rps_.at = time_;
        rpsMine_ = opts_.debugRps == 1 ? RPS_PAPER : RPS_NONE;
    }
    const bool rpsOn = (ms.flags & MS_RPS) || opts_.debugRps;   // (drawn over the rest of the HUD, at the end)
    // crosshair (scoped with the overlay on: the scope's reticle replaces it)
    if (!scopeShown && !paused_ && !rpsOn) drawCrosshair(cx, cy, s, time_ < hitUntil_); // menus open: no crosshair through them
    if (!paused_ && !rpsOn && net_.selfAlive()) drawDamageArcs(cx, cy, s);
    if (!paused_ && !rpsOn) drawKillMark(cx, cy, s);
    if (!cfg_.dmgNums) dmgNums_.clear();   // (VISUALS > DAMAGE NUMBERS off)
    if (!dmgNums_.empty()) {   // floating damage numbers above whoever you hit
        const auto& ip = framePlayers();
        for (size_t i = 0; i < dmgNums_.size();) {
            DmgNum& d = dmgNums_[i];
            float t = (float)(time_ - d.start);
            if (t > 0.9f) { dmgNums_.erase(dmgNums_.begin() + i); continue; }
            if (d.victim >= 0 && d.victim < kMaxPlayers && ip[d.victim].valid) d.pos = ip[d.victim].snap.pos;
            float sx, sy;
            if (r_.project(d.pos + Vec3(0, 2.0f + t * 0.8f, 0), sx, sy)) {
                std::string n = std::to_string((int)std::lround(d.amount));
                // (VISUALS > SIZE: whole text steps, never below the renderer's smallest)
                float k = std::max(1.0f, std::round(ts * (d.amount >= 40 ? 1.5f : 1.0f) * cfg_.dmgNumScale));
                Color c = d.head ? Color{1, 0.18f, 0.15f, 1 - t / 0.9f}   // headshots red (owner 5.6), big hits gold, the rest white
                        : d.amount >= 40 ? Color{1, 0.85f, 0.2f, 1 - t / 0.9f} : Color{1, 1, 1, 1 - t / 0.9f};
                r_.text(sx - r_.textWidth(n, k) * 0.5f, sy, k, n, c);
            }
            i++;
        }
    }
    if (time_ < damageUntil_ && cfg_.damageFlash > 0) {   // (VISUALS > DAMAGE FLASH: its strength, 0 = off)
        float a = (float)(damageUntil_ - time_) / 0.25f * 0.35f * cfg_.damageFlash;
        float b = 30 * s;
        r_.rect(0, 0, W, b, {ec.r, ec.g, ec.b, a}); r_.rect(0, H - b, W, b, {ec.r, ec.g, ec.b, a});
        r_.rect(0, 0, b, H, {ec.r, ec.g, ec.b, a}); r_.rect(W - b, 0, b, H, {ec.r, ec.g, ec.b, a});
    }

    // top: mode, timer, scores (the score box: shown in ranked whatever the HUD says, for the RANKED tag)
    float boxH = 64 * s;
    const bool rankedGame = ms.flags & MS_RANKED;
    if (!cfg_.hud[Settings::HUD_SCORE].hidden || rankedGame) {
        scoreText(hudScoreText_, false);
        boxH = drawHudScore(hudScoreText_, W, H, s).h;
    }
    const float th = r_.textHeight(ts);
    const Color gold = {1.0f, 0.78f, 0.25f, 1};
    const float hs = ui_.hintScale(), hh = r_.textHeight(hs);

    // objective line
    std::string obj;
    float progress = -1;
    if (ms.mode == MODE_BOMB && ms.phase == PHASE_LIVE) {
        bool att = myTeam == ms.attackers;
        if (ms.bombState == BOMB_PLANTED) obj = att ? "BOMB PLANTED - DEFEND IT" : "BOMB PLANTED - DEFUSE IT (HOLD E)";
        else if (ms.bombCarrier == me) obj = "YOU HAVE THE BOMB - PLANT AT A SITE (HOLD E)";
        else if (ms.bombState == BOMB_DROPPED) obj = att ? "BOMB DROPPED - PICK IT UP" : "BOMB DROPPED";
        else obj = att ? "ATTACK - PLANT THE BOMB" : "DEFEND THE SITES";
        if (ms.actionPlayer != 255) progress = ms.actionProgress;
    } else if (ms.mode == MODE_KOTH) {
        obj = ms.flags & MS_CONTESTED ? "HILL CONTESTED" : ms.hillOwner == 255 ? "CAPTURE THE HILL" : ms.hillOwner == myTeam ? "HOLDING THE HILL" : "ENEMY HOLDS THE HILL";
    } else if (ms.mode == MODE_PAYLOAD) {
        obj = myTeam == ms.attackers ? "PUSH THE PAYLOAD" : "STOP THE PAYLOAD";
        if (ms.payloadLen > 0) progress = ms.payloadDist / ms.payloadLen;
    } else if (ms.mode == MODE_POINTS && ms.pointCount > 0) {
        bool att = myTeam == ms.attackers;
        std::string pn = std::string("POINT ") + char('A' + std::min<int>(ms.point, ms.pointCount - 1)) + " (" + std::to_string(ms.point + 1) + "/" + std::to_string(ms.pointCount) + ")";
        obj = (ms.flags & MS_CONTESTED) ? pn + " CONTESTED" : (att ? "CAPTURE " : "DEFEND ") + pn;
        if (ms.phase == PHASE_LIVE) progress = ms.capture;
    } else if (ms.mode == MODE_NPAYLOAD) {
        obj = (ms.flags & MS_CONTESTED) ? "PAYLOAD CONTESTED" : ms.hillOwner == 255 ? "PUSH THE PAYLOAD TO THE ENEMY END"
              : ms.hillOwner == myTeam ? "YOUR TEAM IS PUSHING" : "ENEMY IS PUSHING";
    } else if (isRaceMode(ms.mode) && mep) {
        obj = (mep->flags & PS_FINISHED) ? "FINISHED" : "CHECKPOINT " + std::to_string(mep->checkpoint) + " / " + std::to_string(net_.map().checkpoints.size());
    } else if (ms.mode == MODE_VS && ms.phase == PHASE_LIVE) {
        obj = "LAST TEAM STANDING WINS";
    } else if (ms.mode == MODE_ARENA) {
        obj = arenaVariant(ms.arenaVariant).desc;
    } else if (ms.mode == MODE_TOURNEY && ms.phase != PHASE_WARMUP && !bracket_.empty() && bracketRound_ < (int)bracket_.size()) {
        const auto& r = bracket_[bracketRound_];
        int a = 2 * bracketMatch_ < (int)r.size() ? r[2 * bracketMatch_] : -1, b = 2 * bracketMatch_ + 1 < (int)r.size() ? r[2 * bracketMatch_ + 1] : -1;
        obj = (a >= 0 ? bracketNames_[a] : "?") + " VS " + (b >= 0 ? bracketNames_[b] : "?") + " - FIRST TO " + std::to_string(ms.roundsToWin);
    } else if (ms.mode == MODE_TOURNEY && ms.phase == PHASE_WARMUP) {
        obj = "TOURNEY - VOTE START (" + inputName(binds().code[A_VOTESTART][0]) + "), PICK MAPS AND RULESETS IN THE VOTE MENU";
    } else if (ms.mode == MODE_DUEL && ms.phase != PHASE_WARMUP && ms.duelMod < DUEL_MOD_COUNT) {
        obj = std::string("THIS LIFE: ") + duelModTitle(ms.duelMod);
    }
    // spectating: a big banner under the score box, impossible to miss (the objective line would be for a team you're not on)
    // The notice list: one column under the score bar from y 80 (at 720p), each line under the last - the spectator
    // banner, the hide and seek / tag banner, the objective and its bar, the bomb timer, ranked's notes, a running vote
    // and, in the pause menu, the host's lines - so none is drawn over another (they had fixed rows that collided).
    // The scoreboard and the pause menu's panels start under where it ends (noticeEnd_).
    float ny = std::max(80 * s, 16 * s + boxH);
    const bool spectator = mep && mep->team() == TEAM_SPECTATOR;
    if (spectator) {
        obj.clear(); progress = -1;
        float bts = std::max(2.0f, std::round(ts * 1.5f));
        const char* t = "SPECTATOR";
        float bw = r_.textWidth(t, bts) + 48 * s, bh = r_.textHeight(bts) + 16 * s, by = ny;
        r_.rect(cx - bw * 0.5f, by, bw, bh, {0.05f, 0.06f, 0.08f, 0.8f});
        r_.rect(cx - bw * 0.5f, by, bw, 3 * s, rgba(cfg_.colorFriendly));
        r_.text(cx - r_.textWidth(t, bts) * 0.5f, by + 8 * s, bts, t, kWhite);
        ui_.hintCentered(cx, by + bh + 4 * s, "JOIN A TEAM FROM THE MENU (ESC)", kGrey);
        ny = by + bh + 4 * s + hh + 4 * s;
    }
    if (itBanner && !seekerWall) {
        bool it = (mep->flags & PS_HASBOMB) != 0;
        int left = 0;
        for (auto& p : net_.latest().players) if ((p.flags & PS_ALIVE) && !(p.flags & PS_HASBOMB) && p.team() != TEAM_SPECTATOR) left++;
        std::string t = ms.mode == MODE_HIDE ? (it ? "SEEKING - " + std::to_string(left) + " STILL HIDDEN" : ms.phase == PHASE_FREEZE ? "HIDE! THE SEEKER IS COUNTING" : "STAY HIDDEN - " + std::to_string(left) + " LEFT")
                                             : (it ? "YOU'RE THE TAGGER - TAG SOMEONE BEFORE TIME RUNS OUT" : "RUN - DON'T BE THE TAGGER WHEN TIME RUNS OUT");
        if (!(mep->flags & PS_ALIVE) && ms.mode == MODE_HIDE) t = "FOUND - WATCH THE REST OF THE ROUND";
        r_.text(cx - r_.textWidth(t, bts) * 0.5f, ny, bts, t, it ? kRed : kWhite);
        ny += r_.textHeight(bts) + 4 * s;
    }
    if (!obj.empty()) { ui_.labelCentered(cx, ny, obj, kWhite); ny += th + 4 * s; }
    const bool tug = ms.mode == MODE_NPAYLOAD && ms.payloadLen > 0;
    if (progress >= 0 || tug) {   // the objective's bar (8 high; the payload's marks stick out 4 each side)
        const float bw = 300 * s, by = ny + 4 * s;
        if (tug) {   // tug-of-war bar: your side's end on the left
            float f = ms.payloadDist / ms.payloadLen;
            if (myTeam == TEAM_A) f = 1 - f; // team A pushes toward the end of the track
            r_.rect(cx - bw * 0.5f, by, bw, 8 * s, rgba(cfg_.colorEnemy, 0.8f));
            r_.rect(cx - bw * 0.5f, by, bw * clampf(1 - f, 0, 1), 8 * s, rgba(cfg_.colorFriendly, 0.9f));
            r_.rect(cx - 1, by - 4 * s, 2, 16 * s, kWhite);
        } else {
            r_.rect(cx - bw * 0.5f, by, bw, 8 * s, {0, 0, 0, 0.5f});
            r_.rect(cx - bw * 0.5f, by, bw * clampf(progress, 0, 1), 8 * s, (ms.mode == MODE_PAYLOAD || ms.mode == MODE_POINTS) ? rgba(myTeam == ms.attackers ? cfg_.colorFriendly : cfg_.colorEnemy) : kWhite);
            if (ms.mode == MODE_PAYLOAD)   // the checkpoints on the bar
                for (float f : net_.map().cartChecks) r_.rect(cx - 150 * s + 300 * s * f - 1, by - 4 * s, 2, 16 * s, kWhite);
        }
        ny = by + 12 * s + 4 * s;
    }
    if (ms.mode == MODE_BOMB && ms.bombState == BOMB_PLANTED && ms.phase == PHASE_LIVE) {
        ui_.labelCentered(cx, ny, fmtTime(ms.bombTimer), kRed, 1.5f);
        ny += r_.textHeight(std::max(1.0f, std::round(ts * 1.5f))) + 4 * s;
    }
    if (rankedGame && (ms.phase == PHASE_WARMUP || ms.phase == PHASE_FREEZE)) {   // (under the ruleset line it explains)
        ui_.hintCentered(cx, ny, "RANKED: DEFAULT MODEL, HUD, CONTROLS AND 10 MS INTERP FOR FAIRNESS", gold, W - 32 * s);
        ny += hh + 4 * s;
    }
    if (rankedGame && unratedServer_) { ui_.hintCentered(cx, ny, std::string(kUnratedNotice) + ": RESULTS DON'T COUNT", gold, W - 32 * s); ny += hh + 4 * s; }

    // health / weapon (each element moved / sized / recolored by the HUD settings); spectating: their health
    const PlayerSnap* specP = !net_.selfAlive() && specTarget_ >= 0 ? net_.findPlayer(specTarget_) : nullptr;
    if (specP && (specP->flags & PS_ALIVE) && !cfg_.hud[Settings::HUD_HEALTH].hidden)
        drawHudHealth(cfg_.hud[Settings::HUD_HEALTH], W, H, s, specP->health, kWhite, rgba(teamColor(specTarget_)));
    if (net_.selfAlive()) {
        drawHudLive(ms, mep, W, H, s, false);
    } else if (ms.phase != PHASE_ROUND_END && ms.phase != PHASE_MATCH_END) {
        const bool spectator = mep && (mep->flags & PS_SPECTATOR);
        std::string t = spectator ? "SPECTATING" : "DEAD";
        if (specTarget_ >= 0) t += " - WATCHING " + net_.playerName(specTarget_) + " (CLICK TO CYCLE)";
        ui_.labelCentered(cx, H - 60 * s, t, kGrey);
        if (!spectator && deathValid_) {   // X1: who, with what, their health; then the respawn countdown
            const float lh = r_.textHeight(std::max(1.0f, std::round(ts))) + 6 * s;
            float y = H - 60 * s - lh;
            if (deathRespawnAt_ >= 0) {
                char b[32];
                std::snprintf(b, sizeof b, "RESPAWN IN %.1f", std::max(0.0, deathRespawnAt_ - time_));
                ui_.labelCentered(cx, y, b, kWhite);
                y -= lh;
            }
            std::string k = deathKiller_ >= 0 ? "KILLED BY " + net_.playerName(deathKiller_) + " [" + upper(weaponName(deathWeapon_)) + "]"
                                              : std::string("YOU DIED [") + upper(weaponName(deathWeapon_)) + "]";
            if (deathKiller_ >= 0 && deathKillerHp_ >= 0) k += " - " + std::to_string(deathKillerHp_) + " HP";
            ui_.labelCentered(cx, y, k, rgba(cfg_.colorEnemy));
        }
    }

    if (time_ < splitUntil_) { // race split, with the difference to your best run
        float k = std::max(1.0f, std::round(ts * 1.2f));
        r_.text(cx - r_.textWidth(splitText_, k) * 0.5f, cy + 120 * s, k, splitText_, splitColor_);
    }
    // kill feed (top right), under the verbose info block; hud.cfg widgets. Ranked: the default HUD only.
    float y = 12 * s;
    const bool hudRanked = rankedPlayer();
    // hud.cfg widgets: the file is looked at once a second (pollDue first: no path string built on the other frames),
    // and the values they show are only worked out when there are widgets (or the verbose HUD) to show them
    const bool wantWidgets = !hudRanked && cfg_.hudWidgets && !widgetEdit_;
    if (wantWidgets && widgets_.pollDue()) widgets_.poll(activeHudPath());
    if (!hudRanked) {
        if (cfg_.hudVerbose || (wantWidgets && !widgets_.empty())) computeHudVals();
        if (cfg_.hudVerbose) drawVerboseHud(W, s, y);
        if (cfg_.hudVerbose && !cfg_.hud[Settings::HUD_KEYS].hidden && (net_.selfAlive() || specKeysOf() >= 0)) drawKeyOverlay(W, H, s);
    }
    {   // demos: what's playing / that you're recording
        float dts = std::max(1.0f, std::round(ts * 0.75f));
        if (net_.playback()) {
            float bw = 260 * s, bx = 24 * s, by = H * 0.5f;
            r_.text(bx, by - 22 * s, dts, "DEMO - ESC TO STOP", kWhite);
            r_.rect(bx, by, bw, 6 * s, {0, 0, 0, 0.5f});
            r_.rect(bx, by, bw * net_.playbackProgress(), 6 * s, fc);
        } else if (net_.recording() && std::fmod(time_, 1.0) < 0.7) {
            r_.text(24 * s, H * 0.5f, dts, "REC " + fmtTime((float)net_.recordingTime()), kRed);
        }
    }
    if (wantWidgets && !widgets_.empty()) widgets_.draw(r_, hudVals_, W, H, s);
    while (!killFeed_.empty() && killFeed_.front().until < time_) killFeed_.pop_front();
    hudFeedTop_ = y / s;   // (where the feed starts: under the verbose block when it's on)
    if (!cfg_.hud[Settings::HUD_KILLFEED].hidden && !killFeed_.empty()) drawHudKillFeed(W, H, s, y, false);
    // chat (bottom left)
    drawChatBox(W, H, s, ts);
    // notices
    drawNotices(H * 0.3f, ts, s);
    // the round's message (hide and seek: who won) in the centre slot over the tiebreak, not while the scoreboard is held
    if (time_ < centerUntil_ && !showScores_) ui_.labelCentered(cx, 256 * s, centerText_, kWhite, 1.5f, W - 32 * s);
    ny = drawVotePanel(cx, s, ts, ny);   // (a running vote: in the notice list)

    // host info, in the notice list under the rest (pause menu; the settings and the map / mode lists fill the screen)
    bool tallMenu = paused_ && (settingsInGame_ || picker_ != PICK_NONE);
    if (host_ && paused_ && !tallMenu) {
        const float lineW = W - 32 * s;
        auto line = [&](const std::string& t, Color c) { ui_.hintCentered(cx, ny, t, c, lineW); ny += hh + 4 * s; };
        line("HOSTING ON UDP PORT " + std::to_string(host_->port()), kGrey);
        if (host_->config().upnp && host_->upnpStatus().size())
            line(upper(host_->upnpStatus()), !host_->upnpDone() ? kGrey : host_->upnpOk() ? kGreenOk : kRed);
        std::string ls = upper(host_->listStatus());
        if (!ls.empty()) line(ls, kGrey);
        Server::Reach rc = host_->reachability();
        if (rc != Server::REACH_OFF) {   // can friends actually get in? (a public server sent a packet to our port)
            const std::string t = upper(host_->reachStatus());
            const Color c = rc == Server::REACH_OK ? kGreenOk : rc == Server::REACH_BLOCKED ? kRed : kGrey;
            if (rc == Server::REACH_CHECKING) line(t, c);
            else {   // CHECK AGAIN at the end of the line it belongs to, the two centred together
                const float bw = 118 * s, bh = 20 * s, gap = 8 * s;
                const float tw = std::min(r_.textWidth(t, hs), lineW - bw - gap), x0 = cx - (tw + gap + bw) * 0.5f;
                ui_.hint(x0, ny, t, c, tw + 1);
                if (ui_.button(x0 + tw + gap, ny + (hh - bh) * 0.5f, bw, bh, "CHECK AGAIN", UiExp::state("reach"))) host_->recheckReachability();
                ny += std::max(hh, bh - 4 * s) + 4 * s;
            }
        }
        // C9: a ranked player dialled an address this server can't see as its own (a forwarded port, a container)
        std::string own = host_->ownAddrHint();
        if (!own.empty()) {
            line("A PLAYER JOINED VIA " + own + ", WHICH THIS PC CAN'T CONFIRM IS ITSELF: NO RANKED ID", kRed);
            line("IF THAT ADDRESS REACHES YOU, HOST A DEDICATED SERVER WITH --own-addr " + own, kGrey);
        }
    }
    noticeEnd_ = ny;

    // tourney: the bracket between matches / rounds and on TAB, so everyone sees who's still in
    bool showBracket = ms.mode == MODE_TOURNEY && !bracket_.empty() &&
                       (showScores_ || ms.phase == PHASE_ROUND_END || (ms.phase == PHASE_FREEZE && ms.round <= 1));
    const bool board = showScores_ && !paused_ && !showBracket;
    if (rpsOn) {   // the hide and seek tiebreak: under the scoreboard when it's held (and under it and the menus in draw order)
        float top = 304 * s;
        if (board) { ScoreboardBox b = scoreboardBox(net_.latest().players.size()); top = std::max(top, b.y + b.h + 16 * s); }
        drawRps(W, H, s, top);
    }
    if (showBracket && !paused_) drawBracket();
    else if (board) drawScoreboard();
    if (paused_) {
        // dim the game behind the menu so HUD text doesn't show through its panels; not on the pages
        // that preview the HUD / crosshair over the game
        if (!(settingsInGame_ && widgetEdit_)) r_.rect(0, 0, W, H, {0.03f, 0.035f, 0.05f, 0.55f});
        drawPauseMenu(cx, tallMenu ? 96 * s : H * 0.25f);
    }
    if (ui_.probing()) uiTestActions();   // (the UI test: keys and clicks that aren't widgets)
}

std::vector<std::string> Game::rankIdsInGame() const {
    std::vector<std::string> ids;
    if (!net_.hasSnapshot()) return ids;
    for (auto& p : net_.latest().players) if (!net_.rankId(p.id).empty()) ids.push_back(net_.rankId(p.id));
    return ids;
}

// Ranked servers: keep everyone's ratings fresh for the scoreboard, pass on report results, and say how
// much your rating moved once the match is rated.
void Game::updateRanked() {
    for (auto& m : rank_.takeMessages()) {
        std::string t = m;
        for (auto& c : t) c = (char)toupper((unsigned char)c);
        addChat({t, kGrey, time_ + 8});
    }
    if (!net_.hasSnapshot() || !(net_.mode().flags & MS_RANKED)) return;
    std::string ladder = arenaVariant(net_.mode().arenaVariant).name;
    if (time_ >= nextRankFetch_) { rank_.fetch(ladder, rankIdsInGame()); nextRankFetch_ = time_ + 20; }
    Rating r;
    if (rankGamesBefore_ >= 0 && rank_.rating(ladder, rankKey_.id, r) && r.games > rankGamesBefore_) {
        int d = r.display() - rankBefore_;
        notify("RATING " + std::to_string(rankBefore_) + " -> " + std::to_string(r.display()) + " (" + (d >= 0 ? "+" : "") + std::to_string(d) + ")", 8);
        rankGamesBefore_ = -1;
    }
}

// Top players of each ranked ladder (from the ranking server), refreshed every 30 s while open.

std::string Game::activeHudPath() const { return modeHudPath_.empty() ? hudCfgPath() : modeHudPath_; }

void Game::applyModeRules(int mode) {
    appliedMode_ = mode;
    if (mode >= MODE_COUNT) mode = -1;
    std::string hud = mode >= 0 ? cfg_.modeHud[mode] : "", ctl = mode >= 0 ? cfg_.modeCtl[mode] : "";
    std::string hudPath = hud.empty() ? "" : profileDir("huds") + "/" + hud;
    if (hudPath != modeHudPath_) {   // (the same file: keep it; edits are still picked up within a second)
        modeHudPath_ = hudPath;
        widgets_ = HudWidgets{}; // forces a reload from the new path
    }
    modeCtlActive_ = false;
    if (!ctl.empty()) {
        modeBinds_ = cfg_.binds; modeSocd_ = cfg_.socd; modeAlt_ = cfg_.altToggle;
        modeCtlActive_ = controlsFromText(readTextFile(profileDir("controls") + "/" + ctl), modeBinds_, modeSocd_, modeAlt_);
    }
}

// The .cfg files in huds/ or controls/ as buttons; returns true with the clicked name.
// listProfiles for menus drawn every frame: the folder is re-read once a second, not every frame.
static bool g_profilesStale = false;   // a profile was just written: list the folder again now (not up to a second later)
static const std::vector<std::string>& profilesCached(const char* sub, double now) {
    struct Entry { double at = -1; std::vector<std::string> files; };
    static Entry huds, controls;
    if (g_profilesStale) { huds.at = controls.at = -1; g_profilesStale = false; }
    Entry& e = std::strcmp(sub, "huds") == 0 ? huds : controls;
    if (now >= e.at || now < e.at - 1.0) { e.files = listProfiles(sub); e.at = now + 1.0; }
    return e.files;
}

bool Game::drawFileList(const char* sub, float x, float y, float w, std::string& picked) {
    float s = ui_.s(), h = 22 * s, gap = 3 * s;
    const bool huds = std::strcmp(sub, "huds") == 0;
    const auto& files = profilesCached(sub, time_);
    ui_.panel(x - 8 * s, y - 8 * s, w + 16 * s, (std::min<int>((int)files.size(), 14) + 2) * (h + gap) + 24 * s, 0.95f);
    ui_.label(x, y, files.empty() ? std::string("NO FILES IN ") + upper(sub) + "/ - USE OPEN FOLDER" : "PICK A FILE TO IMPORT", kWhite, 0.5f);
    y += 16 * s;
    for (int i = 0; i < (int)files.size() && i < 14; i++, y += h + gap)
        if (ui_.button(x, y, w, h, upper(files[i]), UiExp::state(huds ? "widgets" : "ctlimport").withId("FILE"))) { picked = files[i]; filePick_ = 0; return true; }
    if (ui_.button(x, y, w, h, "CANCEL", UiExp::nav(huds ? "~settings/hud/widgets" : "~settings/controls"))) filePick_ = 0;
    return false;
}

void Game::computeHudVals() {
    HudVals& h = hudVals_;
    // (the widget editor opened from the main menu calls this with no game: mode() / latest() are the newest snapshot,
    // and there's none - reading it crashed the game)
    static const Snapshot kNone{};
    const Snapshot& snap = net_.hasSnapshot() ? net_.latest() : kNone;
    const ModeState& ms = snap.ms;
    const PlayerSnap* mep = net_.findPlayer(net_.clientId());
    const MoveState& mv = net_.predicted();
    auto set = [&](int i, float v, float mx) { h.v[i] = v; h.max[i] = mx; };
    set(HV_HEALTH, (float)net_.selfHealth(), 100);
    set(HV_SPEED, std::sqrt(mv.vel.x * mv.vel.x + mv.vel.z * mv.vel.z), 40);
    set(HV_DASH, (float)mv.dashCharges, (float)dashMaxCharges(mv, &net_.map()));   // dash charges left (1.17: was % of the cooldown)
    set(HV_HOOK, 100.0f * (1.0f - clampf(mv.hookCooldown / (kHookCooldown * moveValue(mv, MV_HOOK_COOLDOWN, &net_.map())), 0, 1)), 100);
    {   // fps over the last second: average and the worst frame
        double sum = 0, worst = 0;
        int n = 0;
        size_t N = frameTimes_.size(), at = frameIdx_;
        for (size_t k = 1; k <= frameCount_ && sum < 1.0; k++) {
            at = (at ? at : N) - 1;   // (frameIdx_ - k around the ring, without a division per frame)
            float dt = frameTimes_[at];
            sum += dt; worst = std::max<double>(worst, dt); n++;
        }
        set(HV_FPS, sum > 0 ? (float)std::lround(n / sum) : 0, 500);
        set(HV_FPSMIN, worst > 0 ? (float)std::lround(1.0 / worst) : 0, 500);
    }
    set(HV_PING, mep ? (float)mep->ping : 0, 200);
    set(HV_LOSS, (float)net_.snapshotLoss(), 100);
    set(HV_TIME, std::max(0.0f, ms.timeLeft), 600);
    int myTeam = mep ? mep->team() : TEAM_A, enemies = 0, mates = 0;
    bool team = modeIsTeamBased(ms.mode);
    for (auto& p : snap.players) {
        if (p.team() == TEAM_SPECTATOR || !(p.flags & PS_ALIVE) || (mep && p.id == mep->id)) continue;
        if (team && p.team() == myTeam) mates++; else enemies++;
    }
    set(HV_ENEMIES, (float)enemies, std::max(1, enemies));
    set(HV_TEAMMATES, (float)mates, std::max(1, mates));
    set(HV_KILLS, mep ? mep->kills : 0, 25);
    set(HV_DEATHS, mep ? mep->deaths : 0, 25);
    set(HV_SCORE, mep ? mep->score : 0, std::max(1, (int)ms.target));
    set(HV_TICKRATE, (float)net_.tickRate(), 1000);
}

// Dense info block, top right: one line per value, label left / value right, all snprintf into one buffer.
// Key overlay (part of the verbose HUD): the movement / action keys, lit while held. Moved, sized and
// tinted like any HUD element.
// ---- console (`): edit any setting on the fly ----
void Game::consolePrint(const std::string& t) {
    consoleLog_.push_back(t);
    while (consoleLog_.size() > 200) consoleLog_.pop_front();
}

void Game::runConsole(const std::string& line) {
    consolePrint("> " + line);
    std::istringstream in(line);
    std::string k, v;
    in >> k;
    std::getline(in, v);
    while (!v.empty() && v[0] == ' ') v.erase(0, 1);
    if (k.empty()) return;
    auto lines = [&] { std::vector<std::string> out; std::istringstream t(cfg_.toText()); std::string l; while (std::getline(t, l)) out.push_back(l); return out; };
    if (k == "help") {
        consolePrint("<setting>          show it          <setting> <value>   set it (saved)");
        consolePrint("list [text]        settings (containing text)     clear    empty this log");
        consolePrint("TAB completes   UP/DOWN history   CTRL+V pastes   CTRL+C copies the line (empty line or CTRL+SHIFT+C: the log)");
        consolePrint("/<command>         a server command (callvote, team, kill, ...)");
        consolePrint("e.g.  sensitivity 1.8   fov 110   fullbright 1   scope_opacity 0.3   volume 0.5");
        return;
    }
    if (k == "clear") { consoleLog_.clear(); return; }
    if (k == "list") {
        for (auto& l : lines()) if (v.empty() || l.find(v) != std::string::npos) consolePrint("  " + l);
        return;
    }
    if (k[0] == '/') { net_.sendCommand(line.substr(1)); return; }
    std::string current;
    bool known = k.rfind("mode_hud_", 0) == 0 || k.rfind("mode_controls_", 0) == 0;   // (keys a setting adds)
    for (auto& l : lines()) if (l.rfind(k + "=", 0) == 0) { current = l.substr(k.size() + 1); known = true; }
    // (with a value too: "foo 5" set nothing, saved the file and printed nothing)
    if (!known) { consolePrint("unknown setting '" + k + "' - try: list"); return; }
    if (v.empty()) { consolePrint("  " + k + " = " + current); return; }
    cfg_.set(k, v);
    cfg_.finishLoad();
    cfg_.save();
    audio_.setMasterVolume(cfg_.volume);   // (the ones that need applying; the rest are read every frame)
    audio_.setEffectsVolume(cfg_.volumeEffects);
    audio_.setAnnouncerVolume(cfg_.volumeAnnouncer);
    audio_.setQuips(cfg_.quips);
    audio_.setQuipVolume(cfg_.volumeQuips);
    audio_.setOutputDevice(cfg_.audioOutput);   // (nothing unless it changed)
    if (k == "voice") audio_.setAnnouncer(cfg_.announcerVoice);   // (it resamples every line: only when it's the one set)
    if ((k == "monitor" || k == "fullscreen_hz") && cfg_.displayMode != 0) applyDisplayMode();
    ui_.accent = rgba(cfg_.colorFriendly);
    for (auto& l : lines()) if (l.rfind(k + "=", 0) == 0) consolePrint("  " + k + " = " + l.substr(k.size() + 1));
    consoleWords_.clear();   // (a setting can add a key: mode_hud_*, mode_controls_*)
}

// Every word the console knows: its commands and every setting name.
std::vector<std::string> Game::consoleWords() const {
    std::vector<std::string> out = {"help", "list", "clear"};
    std::istringstream t(cfg_.toText());
    std::string l;
    while (std::getline(t, l)) if (size_t e = l.find('='); e != std::string::npos && e > 0) out.push_back(l.substr(0, e));
    return out;
}

// Tab: completes the word being typed - commands and settings, then each command's arguments (settings,
// server commands and their arguments, maps, modes, ladders, bot skills, player names). One match fills it
// in; several fill the common part and list them all.
std::vector<std::string> Game::consoleCandidates(const std::vector<std::string>& w) const {
    auto modes = [] { std::vector<std::string> o; for (int m = 0; m < MODE_COUNT; m++) o.push_back(modeName(m)); return o; };
    auto skills = [] { std::vector<std::string> o; for (int i = 0; i < BOT_SKILLS; i++) o.push_back(botSkillName(i)); return o; };
    auto ladders = [] { std::vector<std::string> o; for (int v : {0, 1, 5}) o.push_back(arenaVariant(v).name); return o; };
    auto players = [&] { std::vector<std::string> o; for (int i = 0; i < kMaxPlayers; i++) { std::string n = net_.playerName(i); if (!n.empty() && n.find(' ') == std::string::npos) o.push_back(n); } return o; };
    const std::vector<std::string> onOff = {"on", "off"};
    const std::vector<std::string> server = {"/say", "/team", "/name", "/model", "/callvote", "/vote", "/kill", "/login", "/editmode", "/reload",
                                             "/map", "/mode", "/addbot", "/ranked", "/kickbots", "/forcestart", "/restart", "/kick", "/status", "/locate", "/quip"};
    if (w.empty()) { auto o = consoleWords(); o.insert(o.end(), server.begin(), server.end()); return o; }
    const std::string& c = w[0];
    if (w.size() == 1) {
        if (c == "list") { auto o = consoleWords(); o.erase(o.begin(), o.begin() + 3); return o; }
        if (c == "/team") return {"a", "b", "spec"};
        if (c == "/vote") return {"yes", "no"};
        if (c == "/callvote") return {"edit", "ranked", "bots", "botskill", "start", "tmaps", "tmodes", "map", "mode", "spec"};
        if (c == "/map") return maps_;
        if (c == "/mode") return modes();
        if (c == "/editmode" || c == "/ranked") return onOff;
        if (c == "/kick") return players();
        if (c == "/addbot") return {"a", "b"};   // addbot [a|b] [skill] (the server also takes a skill alone)
        // a setting: its current value
        std::istringstream t(cfg_.toText());
        std::string l;
        while (std::getline(t, l)) if (l.rfind(c + "=", 0) == 0 && l.size() > c.size() + 1) return {l.substr(c.size() + 1)};
        return {};
    }
    if (c == "/callvote") {
        const std::string& what = w[1];
        if (w.size() == 2) {
            if (what == "edit" || what == "bots" || what == "ranked") return onOff;
            if (what == "botskill") return skills();
            if (what == "map" || what == "tmaps") return maps_;
            if (what == "mode" || what == "tmodes") return modes();
            if (what == "spec") return players();   // callvote spec <player>: to spectator until the next map
        }
        if (w.size() == 3 && what == "ranked" && w[2] == "on") return ladders();
        if (w.size() == 3 && what == "map") return modes();
    }
    if (c == "/ranked" && w.size() == 2 && w[1] == "on") return ladders();
    if (c == "/addbot" && w.size() == 2 && (w[1] == "a" || w[1] == "b")) return skills();
    return {};
}

void Game::consoleComplete() {
    Completion r = completeLine(chatText_, [&](const std::vector<std::string>& w) { return consoleCandidates(w); });
    chatText_ = r.line;
    if (r.options.empty()) return;
    std::string line;
    for (auto& h : r.options) { if (line.size() + h.size() > 90) { consolePrint("  " + line); line.clear(); } line += h + "  "; }
    if (!line.empty()) consolePrint("  " + line);
}

void Game::drawConsole(float W, float H, float s, float ts) {
    float ch = H * 0.45f, lh = r_.textHeight(ts) + 4 * s;
    r_.rect(0, 0, W, ch, {0.03f, 0.04f, 0.06f, 0.92f});
    r_.rect(0, ch, W, 2 * s, rgba(cfg_.colorFriendly));
    chatEdit(200, true);
    float y = ch - 12 * s - lh;
    // highlighting: the first word green when it's a known setting / command, red when not, a /command in the
    // accent colour; the value white
    const Color good = {0.45f, 0.95f, 0.55f, 1}, bad = {1.0f, 0.4f, 0.35f, 1}, key = {0.55f, 0.8f, 1.0f, 1}, dim = {0.75f, 0.78f, 0.82f, 1};
    // the known words for the highlight: listed once when the console opens / runs a line, not every frame
    // (consoleWords serialises the whole config)
    if (consoleWords_.empty()) consoleWords_ = consoleWords();
    const auto& words = consoleWords_;
    auto known = [&](const std::string& w) { return std::find(words.begin(), words.end(), w) != words.end(); };
    float x = 16 * s;
    r_.textMono(x, y, ts, "> ", kWhite); x += r_.textWidthMono("> ", ts);
    size_t sp = chatText_.find(' ');
    std::string first = chatText_.substr(0, sp), rest = sp == std::string::npos ? "" : chatText_.substr(sp);
    Color fc = first.empty() ? kWhite : first[0] == '/' ? rgba(cfg_.colorFriendly) : known(first) ? good : bad;
    r_.textMono(x, y, ts, first, fc); x += r_.textWidthMono(first, ts);
    r_.textMono(x, y, ts, rest, kWhite);
    drawCaret(16 * s, y, ts, true, "> " + chatText_.substr(0, chatCursor_));
    y -= lh * 1.3f;
    for (auto it = consoleLog_.rbegin(); it != consoleLog_.rend() && y > 4 * s; ++it, y -= lh) {
        const std::string& l = *it;
        size_t eq = l.find(" = ");
        if (l.rfind("> ", 0) == 0) r_.textMono(16 * s, y, ts, l, rgba(cfg_.colorFriendly));                  // what was typed
        else if (l.rfind("unknown", 0) == 0) r_.textMono(16 * s, y, ts, l, bad);
        else if (eq != std::string::npos) {                                                                 // "  key = value"
            std::string k = l.substr(0, eq), v = l.substr(eq);
            r_.textMono(16 * s, y, ts, k, key); r_.textMono(16 * s + r_.textWidthMono(k, ts), y, ts, v, kWhite);
        } else if (size_t e = l.find('='); e != std::string::npos && l.rfind("  ", 0) == 0 && l.find(' ', 2) > e) {   // list: "  key=value"
            std::string k = l.substr(0, e), v = l.substr(e);
            r_.textMono(16 * s, y, ts, k, key); r_.textMono(16 * s + r_.textWidthMono(k, ts), y, ts, v, kWhite);
        } else r_.textMono(16 * s, y, ts, l, dim);
    }
    if (consoleLog_.empty()) r_.textMono(16 * s, y, ts, "CONSOLE - TYPE help (` OR ESC CLOSES)", kGrey);
}

void Game::chatSync() {
    if (chatText_ != chatSeen_) { chatCursor_ = chatText_.size(); chatSeen_ = chatText_; }
    if (chatCursor_ > chatText_.size()) chatCursor_ = chatText_.size();
}

bool Game::chatCaretKey(int key) {
    chatSync();
    size_t n = chatText_.size();
    if (key == GLFW_KEY_LEFT) { if (chatCursor_ > 0) chatCursor_--; }
    else if (key == GLFW_KEY_RIGHT) { if (chatCursor_ < n) chatCursor_++; }
    else if (key == GLFW_KEY_HOME) chatCursor_ = 0;
    else if (key == GLFW_KEY_END) chatCursor_ = n;
    else if (key == GLFW_KEY_DELETE) { if (chatCursor_ < n) chatText_.erase(chatCursor_, 1); chatSeen_ = chatText_; }
    else return false;
    return true;
}

void Game::chatEdit(size_t maxLen, bool console) {
    chatSync();
    if (uiIn_.backspace && chatCursor_ > 0) chatText_.erase(--chatCursor_, 1);
    for (char c : uiIn_.chars)
        if (c >= 32 && c < 127 && !(console && c == '`') && chatText_.size() < maxLen) chatText_.insert(chatCursor_++, 1, c);
    chatSeen_ = chatText_;
}

// The blinking caret: a thin bar after `before` (the text left of it, drawn from x).
void Game::drawCaret(float x, float y, float ts, bool mono, const std::string& before) {
    ui_.caret(x + (mono ? r_.textWidthMono(before, ts) : r_.textWidth(before, ts)), y, ts);   // (screenshots: steady)
}

// The chat lines (all recent history while typing) and the SAY: / console input. In game and while editing together.
bool Game::nameColorOf(int id, Color& out) const {
    uint16_t c = net_.playerNameColor(id);
    if (!c) return false;
    Color k{((c >> 11) & 31) / 31.0f, ((c >> 5) & 63) / 63.0f, (c & 31) / 31.0f, 1};
    // readable on the dark HUD panels: too dark a colour is lifted toward white until it's bright enough
    float lum = 0.2126f * k.r + 0.7152f * k.g + 0.0722f * k.b;
    const float kMinLum = 0.35f;
    if (lum < kMinLum) {
        float t = (kMinLum - lum) / (1 - lum);
        k = {k.r + (1 - k.r) * t, k.g + (1 - k.g) * t, k.b + (1 - k.b) * t, 1};
    }
    out = k;
    return true;
}

// A feed line (kill feed, chat): its colour, with any name spans in their own
void Game::drawFeedText(float x, float y, float ts, const Feed& f) {
    if (f.spans.empty()) { r_.text(x, y, ts, f.text, f.color); return; }
    size_t at = 0;
    std::string& part = feedPart_;   // (reused: no allocation per piece)
    auto run = [&](size_t to, Color c) {
        if (to <= at) return;
        part.assign(f.text, at, to - at);
        r_.text(x, y, ts, part, c);
        x += r_.textWidth(part, ts);
        at = to;
    };
    auto byAt = [](const Span& a, const Span& b) { return a.at < b.at; };
    const std::vector<Span>* spans = &f.spans;   // (made in order: sorted only when they aren't)
    std::vector<Span> sorted;
    if (!std::is_sorted(f.spans.begin(), f.spans.end(), byAt)) { sorted = f.spans; std::sort(sorted.begin(), sorted.end(), byAt); spans = &sorted; }
    for (auto& sp : *spans) {
        if (sp.at < at || sp.at + sp.len > f.text.size()) continue;   // (overlapping or out of range: drawn plain)
        run(sp.at, f.color);
        run(sp.at + sp.len, sp.color);
    }
    run(f.text.size(), f.color);
}

// A running vote: what it is, the count, F1 / F2.
float Game::drawVotePanel(float cx, float s, float ts, float y) {
    if (y < 0) y = 122 * s;
    const ModeState& ms = net_.mode();
    if (!ms.voteActive) return y;
    char vb[200];
    snprintf(vb, sizeof vb, "VOTE: %s   YES %d  NO %d  OF %d   F1 YES / F2 NO   %ds", voteText_.c_str(), ms.voteYes, ms.voteNo,
             ms.voteVoters, (int)std::ceil(ms.voteTimeLeft));
    float vw = r_.textWidth(vb, ts) + 24 * s, vh = r_.textHeight(ts) + 12 * s;
    r_.rect(cx - vw * 0.5f, y, vw, vh, {0, 0, 0, 0.6f});
    ui_.labelCentered(cx, y + 6 * s, vb, kWhite);
    return y + vh + 4 * s;
}

// The player we're spectating, if they're alive and in the snapshot (else -1).
int Game::specKeysOf() const {
    if (specTarget_ < 0) return -1;
    const PlayerSnap* p = net_.findPlayer(specTarget_);
    return p && (p->flags & PS_ALIVE) && !p->hidden ? specTarget_ : -1;
}

void Game::drawVerboseHud(float W, float s, float& y) {
    const HudVals& h = hudVals_;
    const ModeState& ms = net_.mode();
    float ts = std::max(1.0f, std::round(ui_.textScale() * 0.5f));
    float lh = r_.textHeight(ts) + 3 * s, colW = 190 * s, x = W - colW - 12 * s;
    char clock[16];
    {
        time_t t = ::time(nullptr);
        struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        strftime(clock, sizeof clock, "%H:%M:%S", &lt);
    }
    char b[14][40];
    int n = 0;
    const char* lab[14];
    auto line = [&](const char* l, const char* fmt, auto... a) { if (n >= 14) return; lab[n] = l; snprintf(b[n], sizeof b[n], fmt, a...); n++; };
    line("TIME", "%s", clock);
    line("VERSION", "%s", QUARK_VERSION);
    line("FPS", "%.0f", h.v[HV_FPS]);
    line("FPS MIN (1S)", "%.0f", h.v[HV_FPSMIN]);
    line("SPEED", "%.1f", h.v[HV_SPEED]);
    line("PING", "%.0f MS", h.v[HV_PING]);
    line("LOSS", "%.0f%%", h.v[HV_LOSS]);
    line("TICK / SNAP", "%d / %d", net_.tickRate(), net_.snapRate());
    line("ENEMIES ALIVE", "%.0f", h.v[HV_ENEMIES]);
    if (modeIsTeamBased(ms.mode)) line("TEAMMATES ALIVE", "%.0f", h.v[HV_TEAMMATES]);
    line("TIME LEFT", "%d:%02d", (int)h.v[HV_TIME] / 60, (int)h.v[HV_TIME] % 60);
    line("MAP", "%s", net_.map().name.c_str());
    line("MODE", "%s", modeTitle(ms.mode));
    line("K / D", "%.0f / %.0f", h.v[HV_KILLS], h.v[HV_DEATHS]);
    r_.rect(x - 8 * s, y - 4 * s, colW + 8 * s, n * lh + 6 * s, {0, 0, 0, 0.45f});
    for (int i = 0; i < n; i++) {
        r_.text(x, y, ts, lab[i], {0.7f, 0.72f, 0.75f, 1});
        r_.text(x + colW - 8 * s - r_.textWidth(b[i], ts), y, ts, b[i], kWhite);
        y += lh;
    }
    y += 8 * s;
}

// Widget editor: the widgets drawn live on screen, click to select, drag to move (re-anchors to the nearest
// corner so layouts survive resolution changes), and a panel to pick value / style / colours.
void Game::drawWidgetEditor() {
    const float W = (float)r_.width(), H = (float)r_.height(), s = ui_.s();
    widgets_.poll(activeHudPath());
    computeHudVals();
    HudVals preview = hudVals_;
    for (int i = 0; i < HV_COUNT; i++) if (preview.v[i] == 0) preview.v[i] = std::round(preview.max[i] * 0.65f); // something to look at in menus
    auto& L = widgets_.list();
    if (widgetSel_ >= (int)L.size()) widgetSel_ = -1;
    // (the toolbar 24 px from the top and right edges, fitted to what's in it: ui-redesign-spec, settings/hud/widgets)
    float pw = 364 * s, px = W - pw - 36 * s, py = 36 * s, h = 24 * s, gap = 5 * s;
    bool overPanel = uiIn_.mx > px - 12 * s;
    // select / drag
    if (uiIn_.pressed && !overPanel) {
        widgetSel_ = -1;
        for (int i = (int)L.size() - 1; i >= 0; i--) {
            float x, y, fw, fh;
            widgets_.bounds(L[i], preview, r_, W, H, s, x, y, fw, fh);
            if (uiIn_.mx >= x - 4 && uiIn_.mx <= x + fw + 4 && uiIn_.my >= y - 4 && uiIn_.my <= y + fh + 4) {
                widgetSel_ = i; widgetDrag_ = true; dragDX_ = uiIn_.mx - x; dragDY_ = uiIn_.my - y;
                break;
            }
        }
    }
    if (!uiIn_.down) widgetDrag_ = false;
    if (widgetDrag_ && widgetSel_ >= 0) {
        float x, y, fw, fh;
        widgets_.bounds(L[widgetSel_], preview, r_, W, H, s, x, y, fw, fh);
        HudWidgets::setFromScreen(L[widgetSel_], uiIn_.mx - dragDX_, uiIn_.my - dragDY_, fw, fh, W, H, s);
    }
    r_.rect(W * 0.5f - 0.5f, 0, 1, H, {1, 1, 1, 0.08f}); // centre guides
    r_.rect(0, H * 0.5f - 0.5f, W, 1, {1, 1, 1, 0.08f});
    widgets_.draw(r_, preview, W, H, s);
    for (int i = 0; i < (int)L.size(); i++) { // outline every widget, the selected one in the accent colour
        float x, y, fw, fh;
        widgets_.bounds(L[i], preview, r_, W, H, s, x, y, fw, fh);
        Color c = i == widgetSel_ ? ui_.accent : Color{1, 1, 1, 0.25f};
        r_.rect(x - 3, y - 3, fw + 6, 1, c); r_.rect(x - 3, y + fh + 2, fw + 6, 1, c);
        r_.rect(x - 3, y - 3, 1, fh + 6, c); r_.rect(x + fw + 2, y - 3, 1, fh + 6, c);
    }

    // panel
    if (ui_.probing()) ui_.where = settingsInGame_ ? "ingame/pause/settings/hud/widgets" : "settings/hud/widgets";   // (the UI test: also under its lists)
    // rows under VALUE: style, icon, size / max, bar w / h, colour, its R G B A, grade, (low colour, its R G B A,) label, delete
    const int rows = widgetSel_ < 0 ? 0 : 10 + (L[widgetSel_].grade || L[widgetSel_].style == HudWidget::GRADBAR ? 2 : 0);
    ui_.panel(px - 12 * s, py - 12 * s, pw + 24 * s, (137 + 19) * s + rows * (h + gap));
    ui_.hint(px, py, "CLICK A WIDGET TO EDIT, DRAG TO MOVE.", kWhite, pw);
    ui_.hint(px, py + 14 * s, "IMPORT / EXPORT USE THE CLIPBOARD.", kWhite, pw);
    float y = py + 34 * s, hw = pw * 0.5f - gap * 0.5f;
    if (ui_.button(px, y, hw, h, "ADD WIDGET", UiExp::state("widgets"))) {
        HudWidget w;
        w.anchor = 4; w.y = 120;
        L.push_back(w);
        widgetSel_ = (int)L.size() - 1;
    }
    if (ui_.button(px + hw + gap, y, hw, h, "DUPLICATE", UiExp::state("widgets"), widgetSel_ >= 0)) {
        HudWidget w = L[widgetSel_];
        w.y += w.anchor >= 2 && w.anchor <= 3 ? -24 : 24;
        L.push_back(w);
        widgetSel_ = (int)L.size() - 1;
    }
    y += h + gap;
    float tw = (pw - 2 * gap) / 3;
    if (ui_.button(px, y, tw, h, "IMPORT", filePick_ == 1 ? UiExp::nav("~settings/hud/widgets/files") : UiExp::state("widgets"))) { // a layout on the clipboard wins, else pick a file from huds/
        const char* clip = glfwGetClipboardString(win_);
        HudWidgets t;
        int n = clip ? t.fromText(clip) : 0;
        if (n > 0) { L = t.list(); widgetSel_ = -1; widgetMsg_ = "IMPORTED " + std::to_string(n) + " WIDGETS FROM CLIPBOARD"; widgetMsgUntil_ = time_ + 3; }
        else filePick_ = 1;
    }
    if (ui_.button(px + tw + gap, y, tw, h, "EXPORT", UiExp::state("files:huds"))) { // clipboard + a file in huds/
        std::string text = widgets_.toText(), name = stampedName("hud");
        glfwSetClipboardString(win_, text.c_str());
        bool ok = writeTextFile(profileDir("huds") + "/" + name, text);
        g_profilesStale = true;
        widgetMsg_ = ok ? "COPIED + SAVED HUDS/" + upper(name) : "COPIED (COULDN'T WRITE FILE)";
        widgetMsgUntil_ = time_ + 4;
    }
    if (ui_.button(px + 2 * (tw + gap), y, tw, h, "FOLDER", UiExp::url())) openUrl(profileDir("huds"));
    y += h + gap;
    if (ui_.button(px, y, hw, h, "CLEAR ALL", UiExp::state("widgets"), !L.empty())) { L.clear(); widgetSel_ = -1; }
    if (ui_.button(px + hw + gap, y, hw, h, "SAVE + DONE", UiExp::nav("~settings/hud"))) {
        widgets_.save(activeHudPath());
        cfg_.hudWidgets = true;
        widgetEdit_ = false; widgetSel_ = -1;
        cfg_.save();
        return;
    }
    y += h + gap;
    if (filePick_ == 1) {
        std::string f;
        ui_.where = nullptr;
        if (drawFileList("huds", px, y + 4 * s, pw, f)) {
            HudWidgets t;
            int n = t.fromText(readTextFile(profileDir("huds") + "/" + f));
            if (n > 0) L = t.list();
            widgetSel_ = -1;
            widgetMsg_ = n > 0 ? "IMPORTED " + upper(f) : upper(f) + " HAS NO WIDGETS";
            widgetMsgUntil_ = time_ + 3;
        }
        return;
    }
    if (time_ < widgetMsgUntil_) ui_.hint(px, y, widgetMsg_, ui_.accent, pw);
    else if (!widgets_.error().empty()) ui_.hint(px, y, widgets_.error(), kRed, pw);
    y += 16 * s;
    if (widgetSel_ < 0) return;
    HudWidget& w = L[widgetSel_];
    if (ui_.button(px, y, pw, h, std::string("VALUE: ") + upper(hudValueName(w.value)) + (valueDrop_ ? "  ^" : "  V"),
                   UiExp::nav(valueDrop_ ? "~settings/hud/widgets" : "~settings/hud/widgets/values").withId("VALUE"))) valueDrop_ = !valueDrop_;
    y += h + gap;
    if (valueDrop_) { // dropdown: every value in a 2-column grid; modal until one is picked
        ui_.where = nullptr;
        float cw = pw * 0.5f - gap * 0.5f, rh = 22 * s;
        int rowsN = (HV_COUNT + 1) / 2;
        ui_.panel(px - 4 * s, y - 4 * s, pw + 8 * s, rowsN * (rh + 3 * s) + 8 * s, 0.97f);
        for (int i = 0; i < HV_COUNT; i++) {
            float bx = px + (i % 2) * (cw + gap), by = y + (i / 2) * (rh + 3 * s);
            if (ui_.button(bx, by, cw, rh, (i == w.value ? "> " : "") + upper(hudValueName(i)),
                           (i == w.value ? UiExp::nav("~settings/hud/widgets") : UiExp::state("widgets")).withId(hudValueName(i)))) { w.value = i; valueDrop_ = false; }
        }
        return;
    }
    static const std::vector<std::string> stn = {"STYLE: NUMBER", "STYLE: BAR", "STYLE: COLOR-GRADED BAR", "STYLE: ICON (OPACITY)", "STYLE: ICON + BAR"};
    int st = w.style;
    if (ui_.cycle(px, y, pw, h, "STYLE", st, 5, stn, UiExp::state("widgets"))) w.style = (HudWidget::Style)st;
    y += h + gap;
    static const std::vector<std::string> icn = {"ICON: HEART", "ICON: BOLT", "ICON: HOOK", "ICON: SKULL", "ICON: STAR", "ICON: SHIELD", "ICON: CLOCK", "ICON: EYE"};
    ui_.cycle(px, y, pw, h, "ICON", w.icon, 8, icn, UiExp::state("widgets"));
    y += h + gap;
    // (a typed number may go past a slider's end; kept within what's sane)
    static const HudWidget dw;   // (right-click on a slider: a new widget's value)
    ui_.slider(px, y, hw, h, "SIZE", w.size, 0.25f, 4, "%.2f", UiExp::state("widgets"), dw.size); w.size = clampf(w.size, 0.1f, 8);
    ui_.slider(px + hw + gap, y, hw, h, "MAX (0=AUTO)", w.max, 0, 1000, "%.0f", UiExp::state("widgets"), dw.max); w.max = std::max(0.0f, std::round(w.max));
    y += h + gap;
    ui_.slider(px, y, hw, h, "BAR W", w.w, 10, 600, "%.0f", UiExp::state("widgets"), dw.w); w.w = clampf(std::round(w.w), 1, 2000);
    ui_.slider(px + hw + gap, y, hw, h, "BAR H", w.h, 2, 60, "%.0f", UiExp::state("widgets"), dw.h); w.h = clampf(std::round(w.h), 1, 400);
    y += h + gap;
    auto rgba4 = [&](const char* name, Color& c, const Color& def) {
        ui_.hint(px, y + (h - r_.textHeight(ui_.hintScale())) * 0.5f, name, kWhite, pw - 48 * s);
        r_.rect(px + pw - 40 * s, y, 40 * s, h, c);
        y += h + gap;
        float sw = (pw - 3 * gap) / 4;
        float* ch[4] = {&c.r, &c.g, &c.b, &c.a};
        const float dv[4] = {def.r, def.g, def.b, def.a};
        const char* nm[4] = {"R", "G", "B", "A"};
        for (int i = 0; i < 4; i++) {
            float v = *ch[i] * 255;
            if (ui_.slider(px + i * (sw + gap), y, sw, h, nm[i], v, 0, 255, "%.0f", UiExp::state("widgets"), std::round(dv[i] * 255))) *ch[i] = clampf(std::round(v), 0, 255) / 255;
        }
        y += h + gap;
    };
    rgba4("COLOR (FULL)", w.color, dw.color);
    ui_.toggle(px, y, pw, h, "GRADE FROM LOW COLOR", w.grade, UiExp::state("widgets"));
    y += h + gap;
    if (w.grade || w.style == HudWidget::GRADBAR) rgba4("LOW COLOR (EMPTY)", w.low, dw.low);
    ui_.hint(px, y + (h - r_.textHeight(ui_.hintScale())) * 0.5f, "LABEL", kWhite, 54 * s);
    ui_.textField(40, px + 60 * s, y, pw - 60 * s, h, w.label, 24, UiExp::state("widgets").withId("LABEL"));
    std::string lab; // no '#' (comment) or '_' (saved spaces) in labels
    for (char c : w.label) if (c != '#' && c != '_') lab += (char)toupper((unsigned char)c);
    w.label = lab;
    y += h + gap;
    if (ui_.button(px, y, pw, h, "DELETE WIDGET", UiExp::state("widgets"))) { L.erase(L.begin() + widgetSel_); widgetSel_ = -1; }
}

static std::string raceLogPath() { return userDataPath("race_times.log"); }

static std::string fmtRace(int ms) {
    char b[32];
    snprintf(b, sizeof b, "%d:%02d.%03d", ms / 60000, ms / 1000 % 60, ms % 1000);
    return b;
}

// race_times.log: one finished run per line, "unix-time map ms trial(0/1) split,split,..."
void Game::loadRaceRuns() {
    if (raceWriter_.t.joinable()) raceWriter_.t.join();   // (a run still being written: the file as it will be)
    raceRunsLoaded_ = true;
    raceRuns_.clear();
    std::ifstream f(utf8Path(raceLogPath()));
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        RaceRun r;
        int trial = 0;
        std::string sp, tok;
        if (!(ls >> r.when >> r.map >> r.ms >> trial >> sp)) continue;
        r.trial = trial != 0;
        std::istringstream ss(sp);
        while (std::getline(ss, tok, ',')) r.splits.push_back(atoi(tok.c_str()));
        if (!r.splits.empty() && r.splits.back() == r.ms) raceRuns_.push_back(r);
    }
}

const Game::RaceRun* Game::bestRun(const std::string& map, bool trial) const {
    const RaceRun* b = nullptr;
    for (auto& r : raceRuns_) if (r.map == map && (!trial || r.trial) && (!b || r.ms < b->ms)) b = &r;
    return b;
}

void Game::onRaceSplit(int cp, int ms) {
    if (!raceRunsLoaded_) loadRaceRuns();
    if (cp == 1) raceSplits_.clear();
    if ((int)raceSplits_.size() != cp - 1) { raceSplits_.clear(); audio_.play(cp == (int)net_.map().checkpoints.size() ? SND_FINISH : SND_CHECKPOINT, 0.9f); return; } // missed one: this run doesn't count
    raceSplits_.push_back(ms);
    const std::string map = net_.map().name;
    // a time trial run counts (and is compared) only on the shipped Viaduct, unchanged (the server says so too)
    bool trial = (net_.mode().flags & MS_TIMETRIAL) != 0 && map == "viaduct" && isOfficialMap(net_.map());
    const RaceRun* best = bestRun(map, trial);
    int total = (int)net_.map().checkpoints.size();
    audio_.play(cp == total ? SND_FINISH : SND_CHECKPOINT, 0.9f);
    // the split, and how it compares with the same checkpoint on your best run (green = ahead)
    splitText_ = (cp == total ? "FINISH " : "CHECKPOINT " + std::to_string(cp) + "  ") + fmtRace(ms);
    splitColor_ = kWhite;
    if (best && (int)best->splits.size() == total && cp <= total) {
        int d = ms - best->splits[cp - 1];
        char b[32];
        snprintf(b, sizeof b, "  %c%d.%03d", d < 0 ? '-' : '+', std::abs(d) / 1000, std::abs(d) % 1000);
        splitText_ += b;
        splitColor_ = d < 0 ? Color{0.3f, 1.0f, 0.4f, 1} : Color{1.0f, 0.4f, 0.35f, 1};
    }
    splitUntil_ = time_ + 2.5;
    if (cp != total) return;
    RaceRun run;
    run.when = (long long)::time(nullptr);
    run.map = map; run.ms = ms; run.trial = trial; run.splits = raceSplits_;
    raceSplits_.clear();
    {   // every finished run is kept locally, for the stats page (written off the frame: opening and closing a file
        // can take milliseconds on Windows, on the frame you cross the line; the next run starts at once)
        std::ostringstream f;
        f << run.when << " " << run.map << " " << run.ms << " " << (run.trial ? 1 : 0) << " ";
        for (size_t i = 0; i < run.splits.size(); i++) f << (i ? "," : "") << run.splits[i];
        f << "\n";
        if (raceWriter_.t.joinable()) raceWriter_.t.join();   // (one at a time, in order)
        raceWriter_.t = std::thread([path = raceLogPath(), line = f.str()] { std::ofstream(utf8Path(path), std::ios::app) << line; });
    }
    bool pb = !best || ms < best->ms;
    raceRuns_.push_back(run);
    if (trial && pb) { // the game server posts every run itself (the Worker keeps only your best); refresh the board after
        rank_.fetchRaceBoard(map, 3);
        notify("NEW PERSONAL BEST " + fmtRace(ms) + " - THE SERVER SENDS IT TO THE LEADERBOARD", 5);
    } else if (pb) notify("NEW PERSONAL BEST " + fmtRace(ms), 5);
    if (pb && cfg_.announcer >= 1) audio_.play(SND_VOICE_HIGH_SCORE, 1.0f);
}

void Game::startTimeTrial() {
    timeTrialStart_ = true;
    startHost(true);
}

// MY VIADUCT RUNS (leaderboard > MY RACE STATS): your own race history from race_times.log - best, recent runs,
// average, the best possible from your best splits - in the leaderboard's 960 x 522 panel; BACK (to the board) and
// PLAY TIME TRIAL in its footer
void Game::drawRaceStats(const SetLayout& L) {
    if (!raceRunsLoaded_) loadRaceRuns();
    ui_.panel(L.X(160), L.Y(166), L.L(960), L.L(522), 0.85f);
    ui_.labelCentered(L.X(640), L.Y(182), "MY VIADUCT RUNS", kWhite, 1.5f, L.L(928));
    const float y = 222, rowH = 18, hs = ui_.hintScale();
    std::vector<const RaceRun*> runs;
    for (auto& r : raceRuns_) if (r.map == "viaduct") runs.push_back(&r);
    if (runs.empty()) { ui_.labelCentered(L.X(640), L.Y(y), "NO RUNS YET - FINISH VIADUCT IN A TIME TRIAL OR A RACE", kGrey, 1.0f, L.L(928)); return; }
    int total = 0;
    for (auto* r : runs) total = std::max(total, (int)r->splits.size());
    const RaceRun* best = nullptr;
    long long sum = 0;
    for (auto* r : runs) { sum += r->ms; if (!best || r->ms < best->ms) best = r; }
    int recentN = std::min<int>(10, (int)runs.size());
    long long recent = 0;
    for (int i = 0; i < recentN; i++) recent += runs[runs.size() - 1 - i]->ms;
    // best segment times across every complete run: their sum is the best run you've shown you can do
    std::vector<int> bestSeg(total, INT32_MAX);
    for (auto* r : runs) {
        if ((int)r->splits.size() != total) continue;
        for (int i = 0; i < total; i++) bestSeg[i] = std::min(bestSeg[i], r->splits[i] - (i ? r->splits[i - 1] : 0));
    }
    long long possible = 0;
    for (int v : bestSeg) possible += v == INT32_MAX ? 0 : v;
    // the left column (x 176, 400 wide): a name and its value right-aligned, a row every 18
    const float lx = 176, lw = 400;
    auto row = [&](float yy, const std::string& a, const std::string& b, Color c = kWhite) {
        const float bw = r_.textWidth(b, hs);
        ui_.hint(L.X(lx), L.Y(yy), a, kGrey, L.L(lw) - bw - L.L(12));
        ui_.hint(L.X(lx + lw) - bw, L.Y(yy), b, c, bw + 1);
    };
    float yy = y;
    row(yy, "RUNS", std::to_string(runs.size())); yy += rowH;
    row(yy, "TIME TRIAL RUNS", std::to_string(std::count_if(runs.begin(), runs.end(), [](const RaceRun* r) { return r->trial; }))); yy += rowH;
    row(yy, "BEST", fmtRace(best->ms), Color{1.0f, 0.8f, 0.3f, 1}); yy += rowH;
    row(yy, "BEST POSSIBLE (SEGMENTS)", fmtRace((int)possible), ui_.accent); yy += rowH;
    row(yy, "AVERAGE", fmtRace((int)(sum / runs.size()))); yy += rowH;
    row(yy, "AVERAGE, LAST " + std::to_string(recentN), fmtRace((int)(recent / recentN))); yy += rowH;
    row(yy, "LAST RUN", fmtRace(runs.back()->ms)); yy += rowH * 2;
    // best run's splits vs your best segment at each checkpoint (as many as fit above the footer)
    const float c1 = lx, c2 = lx + 120, c3 = lx + 230;
    ui_.hint(L.X(c1), L.Y(yy), "CHECKPOINT", kGrey, L.L(110));
    ui_.hint(L.X(c2), L.Y(yy), "BEST RUN", kGrey, L.L(100));
    ui_.hint(L.X(c3), L.Y(yy), "BEST SEGMENT", kGrey, L.L(lx + lw - c3));
    yy += rowH;
    for (int i = 0; i < total && yy + 12 <= 616; i++, yy += rowH) {
        int seg = (int)best->splits.size() == total ? best->splits[i] - (i ? best->splits[i - 1] : 0) : 0;
        ui_.hint(L.X(c1), L.Y(yy), i == total - 1 ? "FINISH" : "CP " + std::to_string(i + 1), kWhite, L.L(110));
        ui_.hint(L.X(c2), L.Y(yy), (int)best->splits.size() == total ? fmtRace(best->splits[i]) : "-", kWhite, L.L(100));
        ui_.hint(L.X(c3), L.Y(yy), bestSeg[i] == INT32_MAX ? "-" : fmtRace(bestSeg[i]) + (seg > bestSeg[i] ? "  (LOSES " + fmtRace(seg - bestSeg[i]) + ")" : ""), kWhite, L.L(lx + lw - c3));
    }
    // recent runs, newest first, with a bar against your best (x 640..1104)
    yy = y;
    ui_.hint(L.X(640), L.Y(yy), "RECENT RUNS", kGrey, L.L(464));
    yy += rowH;
    for (int i = 0; i < (int)runs.size() && yy + 12 <= 616; i++, yy += rowH) {
        const RaceRun* r = runs[runs.size() - 1 - i];
        char d[24];
        time_t t = (time_t)r->when;
        struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        strftime(d, sizeof d, "%m-%d %H:%M", &lt);
        Color c = r == best ? Color{1.0f, 0.8f, 0.3f, 1} : kWhite;
        ui_.hint(L.X(640), L.Y(yy), d, c, L.L(100));
        if (r->trial) ui_.hint(L.X(748), L.Y(yy), "TT", c, L.L(24));
        ui_.hint(L.X(780), L.Y(yy), fmtRace(r->ms), c, L.L(100));
        float f = clampf((float)best->ms / r->ms, 0, 1);
        r_.rect(L.X(888), L.Y(yy + 1), L.L(216) * f, L.L(10), {c.r, c.g, c.b, 0.5f});
    }
}

// RANKED LEADERBOARD: a column per ladder (and the Viaduct time trial) - its name bar, then its top 15 (rows of 20)
// and your own rating under them; BACK, MY RACE STATS and PLAY TIME TRIAL in the panel's footer
void Game::drawLeaderboard(const SetLayout& L) {
    static const int ladders[3] = {0, 1, 5};
    const float fy = L.Y(632), fw = L.L(176), fh = L.L(40);
    if (raceStatsView_) {
        drawRaceStats(L);
        if (ui_.button(L.X(176), fy, fw, fh, "BACK", UiExp::nav("leaderboard"))) raceStatsView_ = false;
        if (ui_.button(L.X(928), fy, fw, fh, "PLAY TIME TRIAL", UiExp::state("hosting", "1"))) startTimeTrial();
        return;
    }
    if (time_ >= nextBoardFetch_) {
        rank_.fetchRaceBoard("viaduct");
        for (int v : ladders) rank_.fetchLeaderboard(arenaVariant(v).name);
        if (!rankKey_.id.empty()) for (int v : ladders) rank_.fetch(arenaVariant(v).name, {rankKey_.id});
        nextBoardFetch_ = time_ + 30;
    }
    const int kTop = 15;
    const float ts = ui_.textScale(), colW = 220, rowH = 20;
    ui_.panel(L.X(160), L.Y(166), L.L(960), L.L(522), 0.85f);
    ui_.labelCentered(L.X(640), L.Y(182), "RANKED LEADERBOARD", kWhite, 1.5f, L.L(928));
    // a column: its name bar; rows: "1. NAME" and the number right-aligned (names cut to fit: they're data)
    auto column = [&](int c, const char* title, Color bar, std::vector<RankService::LeaderRow>* rows, bool loaded, bool race) {
        const float x = 176 + c * 236, w = L.L(colW);
        r_.rect(L.X(x), L.Y(222), w, L.L(32), bar);
        ui_.labelCentered(L.X(x + colW * 0.5f), L.Y(230), title, kWhite, 1.0f, L.L(colW - 8));
        const float sx = L.X(x + 4), sw = L.L(colW - 8);
        if (!loaded) { ui_.hintWrapped(sx, L.Y(262), "LOADING...", kGrey, sw, L.L(14)); return; }
        if (!rows->empty() && (*rows)[0].rating < 0) { ui_.hintWrapped(sx, L.Y(262), upper((*rows)[0].name), kGrey, sw, L.L(14)); return; }
        if (rows->empty()) ui_.hintWrapped(sx, L.Y(262), race ? "NO TIMES YET" : "NOBODY RANKED YET (3 GAMES)", kGrey, sw, L.L(14));
        for (size_t i = 0; i < rows->size() && i < (size_t)kTop; i++) {
            const RankService::LeaderRow& lr = (*rows)[i];
            std::string v = race ? fmtRace(lr.rating) : std::to_string(lr.rating);
            const float vw = r_.textWidth(v, ts), ry = L.Y(262 + rowH * i);
            std::string nm = r_.textClip(std::to_string(i + 1) + ". " + lr.name, ts, sw - vw - L.L(10));
            Color c2 = i == 0 ? Color{1.0f, 0.8f, 0.3f, 1} : i < 3 ? kWhite : Color{0.8f, 0.82f, 0.85f, 1};
            r_.text(sx, ry, ts, nm, c2);
            r_.text(sx + sw - vw, ry, ts, v, c2);
        }
    };
    const float youY = 262 + rowH * kTop + 8;
    std::vector<RankService::LeaderRow> rows;
    for (int c = 0; c < 3; c++) {
        std::string ladder = arenaVariant(ladders[c]).name;
        rows.clear();
        bool ok = rank_.leaderboard(ladder, rows);
        column(c, arenaVariant(ladders[c]).title, {ui_.accent.r * 0.45f, ui_.accent.g * 0.45f, ui_.accent.b * 0.45f, 0.95f}, &rows, ok, false);
        Rating mine;
        if (ok && !(!rows.empty() && rows[0].rating < 0) && rank_.rating(ladder, rankKey_.id, mine))   // (not under an error line)
            ui_.label(L.X(180 + c * 236), L.Y(youY), "YOU: " + std::to_string(mine.display()) + (mine.games < kPlacementGames ? "? " : " ") + "(" + std::to_string(mine.games) + " GAMES)", ui_.accent, 1.0f, L.L(colW - 8));
    }
    {   // race: fastest Viaduct time trials
        rows.clear();
        bool ok = rank_.raceBoard("viaduct", rows);
        column(3, "VIADUCT TIME TRIAL", {1.0f * 0.45f, 0.78f * 0.45f, 0.25f * 0.45f, 0.95f}, &rows, ok, true);
        if (!raceRunsLoaded_) loadRaceRuns();
        if (const RaceRun* b = bestRun("viaduct", true)) ui_.label(L.X(180 + 3 * 236), L.Y(youY), "YOU: " + fmtRace(b->ms), ui_.accent, 1.0f, L.L(colW - 8));
    }
    if (ui_.button(L.X(176), fy, fw, fh, "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
    if (ui_.button(L.X(744), fy, fw, fh, "MY RACE STATS", UiExp::nav("leaderboard/race-stats"))) raceStatsView_ = true;
    if (ui_.button(L.X(928), fy, fw, fh, "PLAY TIME TRIAL", UiExp::state("hosting", "1"))) startTimeTrial();
}

// The scoreboard's panel: 900 wide, from under the notice list (y 148 at 720p: the objective, banners and host lines
// end by 144); big servers (up to 32) get smaller rows from higher up so everyone fits
Game::ScoreboardBox Game::scoreboardBox(size_t players) const {
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f;
    ScoreboardBox b;
    b.ts = ui_.textScale();
    b.w = std::min(900 * s, W - 32 * s);
    b.x = (W - b.w) * 0.5f;
    b.y = std::max(148 * s, noticeEnd_ + 4 * s);
    if (players > 12) {
        b.ts = std::max(1.0f, std::round(b.ts * 0.75f));
        b.y = std::max(84 * s, noticeEnd_ + 4 * s);
    }
    b.rowH = std::min(r_.textHeight(b.ts) + 10 * s, (H - b.y - 40 * s) / (players + 2));
    b.h = (players + 2) * b.rowH + 10 * s;
    return b;
}

void Game::drawScoreboard() {
    const float H = (float)r_.height(), s = H / 720.0f;
    const ModeState& ms = net_.mode();
    std::vector<PlayerSnap> ps = net_.latest().players;
    bool ranked = ms.flags & MS_RANKED;
    // instagib (1v1 / tourney ruleset or ranked instagib): every hit kills, so only accuracy and gibs (kills) count
    const bool gib = scoreboardGibs(ms);
    auto shown = [&](const PlayerSnap& p) { return scoreboardValue(p, ms); };   // (shown and sorted by: REVIEW M6)
    std::sort(ps.begin(), ps.end(), [&](const PlayerSnap& a, const PlayerSnap& b) {
        if (a.team() != b.team()) return a.team() < b.team();
        return shown(a) > shown(b);
    });
    const ScoreboardBox b = scoreboardBox(ps.size());
    const float x = b.x, y = b.y, w = b.w, ts = b.ts, rowH = b.rowH;
    ui_.panel(x, y, w, b.h, 0.8f);
    float cy = y + 10 * s;
    r_.text(x + 12 * s, cy, ts, "PLAYER", kGrey);
    std::string ladder = arenaVariant(ms.arenaVariant).name;
    if (ranked) r_.text(x + w * 0.31f, cy, ts, "RATING", kGrey);
    // TOTAL K / D: lifetime kills / deaths (every game, live) from the ranking server, for players with a ranked id.
    // Bots have none, and offline (or before the server answered) nobody has: the column only shows when someone has
    // a total, and a row without one says "-" (it was an empty column).
    auto lifeOf = [&](const PlayerSnap& p) {
        const std::string& id = p.id == net_.clientId() ? rankKey_.id : net_.rankId(p.id);
        return id.empty() ? lifetime_.end() : lifetime_.find(id);
    };
    bool totals = false;
    for (auto& p : ps) if (lifeOf(p) != lifetime_.end()) { totals = true; break; }
    if (totals) r_.text(x + w * 0.79f, cy, ts, "TOTAL K / D", kGrey);
    if (gib) {
        r_.text(x + w * 0.53f, cy, ts, "GIBS", kGrey);
        r_.text(x + w * 0.65f, cy, ts, "ACC", kGrey);
    } else {
        r_.text(x + w * 0.40f, cy, ts, isRaceMode(ms.mode) ? "WINS" : "SCORE", kGrey);
        r_.text(x + w * 0.48f, cy, ts, "K", kGrey);
        r_.text(x + w * 0.53f, cy, ts, "D", kGrey);
        r_.text(x + w * 0.58f, cy, ts, "DMG", kGrey);
        r_.text(x + w * 0.65f, cy, ts, "OBJ", kGrey);
        r_.text(x + w * 0.72f, cy, ts, "ACC", kGrey);
    }
    r_.text(x + w * 0.92f, cy, ts, "PING", kGrey);
    const float nextCol = ranked ? 0.31f : gib ? 0.53f : 0.40f;   // where the column right of PLAYER starts
    cy += rowH;
    for (auto& p : ps) {
        Color c = (p.flags & PS_SPECTATOR) ? kGrey : rgba(teamColor(p.id));
        if (!(p.flags & PS_SPECTATOR)) nameColorOf(p.id, c);   // (a chosen name colour; the table already says the team)
        if (p.id == net_.clientId()) r_.rect(x + 4 * s, cy - 4 * s, w - 8 * s, rowH - 2 * s, {1, 1, 1, 0.06f});
        float cw = 0;
        if (int place = crownOf(p.id)) { cw = r_.textHeight(ts) * 1.2f; drawCrown(x + 12 * s, cy + r_.textHeight(ts) * 0.1f, cw, place, crownLetter(p.id)); cw += 5 * s; }
        {   // the name stops short of the next column (clipped with "..."); " (DEAD)" always shows
            const char* dead = (p.flags & PS_ALIVE) ? "" : " (DEAD)";
            float nameW = x + w * nextCol - (x + 12 * s + cw) - 8 * s - r_.textWidth(dead, ts);
            r_.text(x + 12 * s + cw, cy, ts, r_.textClip(net_.playerName(p.id), ts, std::max(0.0f, nameW)) + dead, c);
        }
        Rating rt;
        if (ranked && rank_.rating(ladder, net_.rankId(p.id), rt))
            r_.text(x + w * 0.31f, cy, ts, rt.games < kPlacementGames ? std::to_string(rt.display()) + "?" : std::to_string(rt.display()), kWhite); // ? = still placing
        if (totals) {   // lifetime totals so far + this game's kills / deaths (sent when the game ends), so it counts up live
            auto lt = lifeOf(p);
            if (lt != lifetime_.end()) r_.text(x + w * 0.79f, cy, ts, std::to_string(lt->second.first + p.kills) + " / " + std::to_string(lt->second.second + p.deaths), kWhite);
            else r_.text(x + w * 0.79f, cy, ts, "-", kGrey);
        }
        if (gib) {
            r_.text(x + w * 0.53f, cy, ts, std::to_string(p.kills), kWhite);
            r_.text(x + w * 0.65f, cy, ts, std::to_string(p.acc) + "%", kWhite);
        } else {
            r_.text(x + w * 0.40f, cy, ts, std::to_string(shown(p)), kWhite);
            r_.text(x + w * 0.48f, cy, ts, std::to_string(p.kills), kWhite);
            r_.text(x + w * 0.53f, cy, ts, std::to_string(p.deaths), kWhite);
            r_.text(x + w * 0.58f, cy, ts, std::to_string(p.dmg), kWhite);
            r_.text(x + w * 0.65f, cy, ts, fmtTime(p.obj), kWhite);
            r_.text(x + w * 0.72f, cy, ts, std::to_string(p.acc) + "%", kWhite);
        }
        bool bot = net_.playerName(p.id).rfind("BOT ", 0) == 0;
        r_.text(x + w * 0.92f, cy, ts, bot ? "BOT" : std::to_string(p.ping), kWhite);
        cy += rowH;
    }
}

// Full map / mode list in game: the host switches right away, anyone else calls a vote for it.
void Game::openLivePicker(int kind, bool vote) {
    rescanMaps();
    liveMode_ = net_.mode().mode;
    liveMap_ = 0;
    for (size_t i = 0; i < maps_.size(); i++) if (maps_[i] == net_.map().name) liveMap_ = (int)i;
    livePickVote_ = vote;
    picker_ = kind;
}

// The pause menu's pages in a column (ui-redesign-spec, ingame/pause...): 360 wide at x 460 in a 392 panel, rows 36
// high and 8 apart (two 176 buttons where they pair up), a section header (accent, text 2) 20 px under the row above
// it, the last row after a 20 px gap; centred in the in-game band (Game::ingameLayout).
namespace {
constexpr float kPX = 460, kPW = 360, kPHalf = 176, kPRow = 36, kPGap = 8;
struct PauseCol {
    Ui& ui;
    const SetLayout& L;
    float y;
    float X(int side = 1) const { return L.X(side == 2 ? kPX + kPHalf + kPGap : kPX); }
    float W(bool half) const { return L.L(half ? kPHalf : kPW); }
    bool full(const std::string& t, const UiExp& e, bool en = true) { return ui.button(X(), L.Y(y), W(false), L.L(kPRow), t, e, en); }
    bool half(int side, const std::string& t, const UiExp& e, bool en = true) { return ui.button(X(side), L.Y(y), W(true), L.L(kPRow), t, e, en); }
    void next() { y += kPRow + kPGap; }
    void section(const std::string& t) { y += 20 - kPGap; ui.label(L.X(kPX), L.Y(y), t, ui.accent, 1.0f, L.L(kPW)); y += 24; }
    void last() { y += 20 - kPGap; }
};
// the content's height: rows (with their gaps), sections, and the last row after its gap
float pauseH(int rows, int sections, bool lastRow) { return rows * (kPRow + kPGap) - kPGap + sections * (20 + 24 - kPGap) + (lastRow ? 20 + kPRow : 0); }
}

// Everything players can call a vote on (majority of human players decides): CALL A VOTE at text 3; START THE MATCH
// (warmup), the tourney lists, CHANGE MAP / MODE beside RANKED (ranked: the map on its own, LADDER | RANKED OFF),
// EDIT THE MAP TOGETHER, MOVE A PLAYER TO SPECTATOR; a BOTS section; BACK on the last row.
void Game::drawVoteMenu(float cx, float y) {
    (void)cx; (void)y;
    const ModeState& ms = net_.mode();
    bool warmup = ms.phase == PHASE_WARMUP, ranked = ms.flags & MS_RANKED, tourney = ms.mode == MODE_TOURNEY && !ranked;
    // move a player to spectator (until the next map): anyone in play but you, with 3+ players connected (never a 1v1)
    std::vector<int> specable;
    int humans = 0;
    if (net_.hasSnapshot())
        for (auto& p : net_.latest().players) {
            bool bot = net_.playerName(p.id).rfind("BOT ", 0) == 0;
            humans += !bot;
            if (!bot && p.id != net_.clientId() && p.team() != TEAM_SPECTATOR) specable.push_back(p.id);
        }
    bool canSpec = !ranked && humans >= 3 && !specable.empty();
    if (specPick_ && canSpec) {   // the players in columns of up to 10 (a full server fits), BACK under them
        const int n = (int)specable.size(), cols = (n + 9) / 10, rowsN = (n + cols - 1) / cols;
        const float colW = 360, tw = cols * colW + (cols - 1) * kPGap;
        const SetLayout L = ingameLayout(tw + 32);
        const float kH = 40 + pauseH(rowsN, 0, true), top = ingameTop(L, kH), x0 = 640 - tw * 0.5f;
        ui_.panel(L.X(x0 - 16), L.Y(top - 16), L.L(tw + 32), L.L(kH + 32), 0.95f);
        ui_.labelCentered(L.X(640), L.Y(top), "VOTE: MOVE TO SPECTATOR (UNTIL THE NEXT MAP)", kWhite, 1.0f, L.L(tw));
        for (int k = 0; k < n; k++) {
            float bx = x0 + (k / rowsN) * (colW + kPGap), by = top + 40 + (k % rowsN) * (kPRow + kPGap);
            if (ui_.button(L.X(bx), L.Y(by), L.L(colW), L.L(kPRow), upper(net_.playerName(specable[k])), UiExp::cmd("callvote spec").withId("PLAYER"))) {
                net_.sendCommand("callvote spec " + std::to_string(specable[k]));   // (by id: names can be alike)
                specPick_ = voteMenu_ = paused_ = false;
            }
        }
        const float by = top + 40 + pauseH(rowsN, 0, false) + 20;
        if (ui_.button(L.X(x0), L.Y(by), L.L(kPHalf), L.L(kPRow), "BACK", UiExp::nav("ingame/pause/vote"))) specPick_ = false;
        return;
    }
    specPick_ = false;
    const int rows = (warmup ? 1 : 0) + (warmup && tourney ? 1 : 0) + (ranked ? 2 : 1) + (ranked ? 0 : 1 + (canSpec ? 1 : 0) + 3);
    const float kH = 40 + pauseH(rows, ranked ? 0 : 1, true);
    const SetLayout L = ingameLayout(kPW + 32);
    PauseCol c{ui_, L, ingameTop(L, kH)};
    ui_.panel(L.X(kPX - 16), L.Y(c.y - 16), L.L(kPW + 32), L.L(kH + 32), 0.95f);
    ui_.labelCentered(L.X(640), L.Y(c.y), "CALL A VOTE", kWhite, 1.5f, L.L(kPW));
    c.y += 40;
    auto voted = [&](const char* cmd) { net_.sendCommand(cmd); voteMenu_ = false; paused_ = false; };   // cmd: "callvote ..."
    if (warmup) { if (c.full("START THE MATCH", UiExp::cmd("callvote start"))) voted("callvote start"); c.next(); }
    if (warmup && tourney) {
        if (c.half(1, "TOURNEY MAPS", UiExp::nav("ingame/pause/picker:tmaps"))) { multiSel_.clear(); picker_ = PICK_TMAPS; }
        if (c.half(2, "TOURNEY RULES", UiExp::nav("ingame/pause/picker:tmodes"))) { multiSel_.clear(); picker_ = PICK_TMODES; }
        c.next();
    }
    // one vote for both: pick the mode, then a map for it; ranked: on with a ladder / switch ladder / off (no bots while it's on)
    const UiExp mapMode = UiExp::nav("ingame/pause/picker:livemode"), ladder = UiExp::nav("ingame/pause/picker:ladders");
    auto openLadders = [&] { variantCtx_ = 2; livePickVote_ = true; picker_ = PICK_RANKED; };
    if (ranked) {
        if (c.full("CHANGE MAP / MODE", mapMode) && !maps_.empty()) openLivePicker(PICK_LIVE_MODE, true);
        c.next();
        if (c.half(1, "LADDER", ladder)) openLadders();
        if (c.half(2, "RANKED OFF", UiExp::cmd("callvote ranked off"))) voted("callvote ranked off");
        c.next();
    } else {
        if (c.half(1, "CHANGE MAP / MODE", mapMode) && !maps_.empty()) openLivePicker(PICK_LIVE_MODE, true);
        if (c.half(2, "RANKED", ladder)) openLadders();
        c.next();
        // edit the map together; on a normal server the changes last until the map changes
        if (c.full("EDIT THE MAP TOGETHER", UiExp::cmd("callvote edit on"))) voted("callvote edit on");
        c.next();
        if (canSpec) { if (c.full("MOVE A PLAYER TO SPECTATOR", UiExp::nav("ingame/pause/vote/spec"))) specPick_ = true; c.next(); }
        c.section("BOTS");
        if (c.half(1, "BOTS ON", UiExp::cmd("callvote bots on"))) voted("callvote bots on");
        if (c.half(2, "BOTS OFF", UiExp::cmd("callvote bots off"))) voted("callvote bots off");
        c.next();
        static const char* skills[4] = {"BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"};
        static const char* shortName[4] = {"EASY BOTS", "MEDIUM BOTS", "HARD BOTS", "EXPERT BOTS"};
        for (int k = 0; k < 4; k++) {
            if (c.half(1 + k % 2, shortName[k], UiExp::cmd("callvote botskill"))) {
                net_.sendCommand(std::string("callvote botskill ") + skills[k]); voteMenu_ = false; paused_ = false;
            }
            if (k % 2) c.next();
        }
    }
    c.last();
    if (c.half(1, "BACK", UiExp::nav("ingame/pause"))) voteMenu_ = false;
}

// The pause menu: RESUME; then two to a row JOIN TEAM A / B (or PLAY), SPECTATE, VOTE, QUEUE, RECORD DEMO (one left
// over: a whole row); SETTINGS; the host's controls in a HOST section; STOP HOSTING / DISCONNECT / BACK TO EDITOR +
// QUIT on the last row after a gap. A demo: RESUME, SETTINGS, STOP DEMO + QUIT.
void Game::drawPauseMenu(float cx, float y) {
    if (settingsInGame_) {
        drawSettings();
        return;
    }
    if (picker_ != PICK_NONE) { drawPicker(cx, y); return; }
    if (voteMenu_) { drawVoteMenu(cx, y); return; }
    if (queueMenu_) { drawQueueMenu(cx, y); return; }
    const SetLayout L = ingameLayout(kPW + 32);
    if (net_.playback()) { // watching a demo
        const float kH = pauseH(2, 0, true);
        PauseCol c{ui_, L, ingameTop(L, kH)};
        ui_.panel(L.X(kPX - 16), L.Y(c.y - 16), L.L(kPW + 32), L.L(kH + 32), 0.95f);
        if (c.full("RESUME", UiExp::nav("ingame"))) paused_ = false;
        c.next();
        if (c.full("SETTINGS", UiExp::nav("ingame/pause/settings"))) { settingsInGame_ = true; openSettingsTab(TAB_PLAYER); }
        c.next();
        c.last();
        if (c.half(1, "STOP DEMO", UiExp::nav("demos"))) { leaveGame(); setScreen(Screen::Demos); return; }
        if (c.half(2, "QUIT", UiExp::quit())) quit_ = true;
        return;
    }
    const bool teamMode = modeIsTeamBased(net_.mode().mode), ranked = net_.mode().flags & MS_RANKED;
    const int pairs = (teamMode ? 2 : 1) + 4;   // (JOIN A + B or PLAY), SPECTATE, VOTE, QUEUE, RECORD DEMO
    const int rows = 1 + (pairs + 1) / 2 + 1 + (host_ ? 2 + (ranked ? 0 : 1) : 0);
    const float kH = pauseH(rows, host_ ? 1 : 0, true);
    PauseCol c{ui_, L, ingameTop(L, kH)};
    ui_.panel(L.X(kPX - 16), L.Y(c.y - 16), L.L(kPW + 32), L.L(kH + 32), 0.95f);
    if (c.full("RESUME", UiExp::nav("ingame"))) paused_ = false;
    c.next();
    int k = 0;   // the paired buttons: left, right, next row
    auto pair = [&](const std::string& t, const UiExp& e) {
        const bool alone = k == pairs - 1 && k % 2 == 0;   // (the last one with no partner: the whole row)
        const bool hit = alone ? c.full(t, e) : c.half(1 + k % 2, t, e);
        if (k % 2 || alone) c.next();
        k++;
        return hit;
    };
    if (teamMode) {
        if (pair("JOIN TEAM A", UiExp::cmd("team a"))) { net_.sendCommand("team a"); paused_ = false; }
        if (pair("JOIN TEAM B", UiExp::cmd("team b"))) { net_.sendCommand("team b"); paused_ = false; }
    } else if (pair("PLAY", UiExp::cmd("team a"))) { net_.sendCommand("team a"); paused_ = false; }
    if (pair("SPECTATE", UiExp::cmd("team spec"))) { net_.sendCommand("team spec"); paused_ = false; }
    if (pair("VOTE", UiExp::nav("ingame/pause/vote"))) voteMenu_ = true;
    if (pair(queued_.empty() ? "QUEUE" : "QUEUE (" + std::to_string(queued_.size()) + ")", UiExp::nav("ingame/pause/queue").withId("QUEUE"))) queueMenu_ = true;
    if (net_.recording()) {
        if (pair("STOP RECORDING (" + fmtTime((float)net_.recordingTime()) + ")", UiExp::state("recording", "0").withId("STOP RECORDING"))) { net_.stopRecording(); notify("DEMO SAVED - MAIN MENU > DEMOS", 4); }
    } else if (pair("RECORD DEMO", UiExp::state("recording", "1"))) startRecording();
    if (c.full("SETTINGS", UiExp::nav("ingame/pause/settings"))) { settingsInGame_ = true; openSettingsTab(TAB_PLAYER); }
    c.next();
    if (host_) {   // the host's controls: start now, reload the map file (picks up edits to it), change map / mode live, edit together
        c.section("HOST");
        if (c.half(1, "FORCE START", UiExp::cmd("forcestart"))) { net_.sendCommand("forcestart"); paused_ = false; }
        if (c.half(2, "RELOAD MAP", UiExp::cmd("reload"))) { net_.sendCommand("reload"); paused_ = false; }
        c.next();
        if (c.full("CHANGE MAP / MODE", UiExp::nav("ingame/pause/picker:livemode")) && !maps_.empty()) openLivePicker(PICK_LIVE_MODE, false);
        c.next();
        if (!ranked) {
            if (c.full("EDIT THE MAP TOGETHER", UiExp::cmd("editmode on"))) { net_.sendCommand("editmode on"); paused_ = false; }
            c.next();
        }
    }
    c.last();
    if (c.half(1, !testMap_.empty() ? "BACK TO EDITOR" : host_ ? "STOP HOSTING" : "DISCONNECT", UiExp::nav(!testMap_.empty() ? "editor" : "main"))) { leaveGame(); return; }
    if (c.half(2, "QUIT", UiExp::quit())) quit_ = true;
}

// ---------------------------------------------------------------------------
// menus

// ---- Settings: one shell for every tab ----------------------------------------------------------------------------
// One opaque 776 x 656 panel, 32 px from the top, centred: a tab row (PLAYER / GAMEPLAY / CONTROLS / VIDEO + AUDIO /
// CROSSHAIR / HUD), two 360 px columns of sections (an accent header; controls 32 px tall, 8 apart; a hint at text
// 1.5 under the control it explains), BACK bottom-left (the same place on every tab), the tab's own reset
// bottom-right. The numbers are the design's (ui-redesign-spec, the settings pages) at 720p in a 1280 frame: SetLayout
// puts them on this screen (s = H / 720, centred on W / 2), so the page holds at any size. The menus and the pause
// menu draw the same pages. Nothing here allocates per frame beyond what the widgets did before (static name lists).
namespace {
constexpr float kColL = 268, kColR = 652, kColW = 360, kHalfW = 176, kRowH = 32;
constexpr float kHudFootY = 560;   // (the HUD tab's footer: clear of the bottom row of the HUD under it)
const Color kHintC = {0.7f, 0.72f, 0.75f, 1}, kLabelC = {0.85f, 0.87f, 0.9f, 1};
constexpr int kNameColorOn = 0xFFD040;   // NAME COLOR turned on (the old R / G / B sliders' default)
constexpr int kCustomInterpMs = 10;      // SMOOTHING: CUSTOM starts here (and a right-click on its slider goes back to it)

// A column of a settings page: y runs down it in design px (header 24 high, a row 32 + 8, a hint line 14).
struct SetFlow {
    Ui& ui;
    const SetLayout& L;
    float col, y;
    float x() const { return L.X(col); }
    float top() const { return L.Y(y); }
    float w(float dw = kColW) const { return L.L(dw); }
    float h() const { return L.L(kRowH); }
    // (its text fits the column: Ui's label fitting, so every font fits by itself)
    void header(const std::string& t) { ui.label(x(), top(), t, ui.accent, 1.0f, w()); y += 24; }
    void section(const std::string& t) { y += 12; header(t); }
    void label(const std::string& t) { ui.label(x(), top(), t, kLabelC, 1.0f, w()); y += 20; }
    void next() { y += kRowH + 8; }
    // hint lines under the row just placed (right after next())
    void hint(const std::string& a, const std::string& b = std::string()) {
        ui.hint(x(), L.Y(y - 4), a, kHintC, w());
        if (!b.empty()) { ui.hint(x(), L.Y(y + 10), b, kHintC, w()); y += 14; }
        y += 16;
    }
};
Vec3 rgbOf(int c) { return {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f}; }
int packOf(const Vec3& c) {
    auto ch = [](float v) { return (int)std::lround(clampf(v, 0, 1) * 255); };
    return ch(c.x) << 16 | ch(c.y) << 8 | ch(c.z);
}
}

void Game::openSettingsTab(int tab) {
    hudEditClose();
    setTab_ = tab;
    controlsSub_ = 0;
    fontsOpen_ = devicesOpen_ = widgetEdit_ = false;
    widgetSel_ = -1;
    filePick_ = 0;
    captureAction_ = -1;
    ui_.closePopup();
    cfg_.save();
}

// BACK and Esc: save, send what changed, back where settings was opened from (and on its first tab next time)
void Game::leaveSettings() {
    if (widgetEdit_) widgets_.save(activeHudPath());
    openSettingsTab(TAB_PLAYER);
    linkDiscord();
    if (net_.state() == ConnState::Connected) net_.sendCommand("name " + cfg_.name);
    if (settingsInGame_) settingsInGame_ = false;
    else setScreen(Screen::Main);
}

// --settingstab PAGE[+PICKER]: open that page (screenshots): player, fonts, gameplay, controls, controls/movement,
// controls/files, video, video/devices, crosshair, hud, hud/elements, hud/widgets, hud/<element> (that element picked:
// health, weapon, bars, speed, slots, keys); "+LABEL" opens that colour picker on it
void Game::debugSettingsTab() {
    std::string t = opts_.settingsTab, pick;
    if (t.empty()) return;
    size_t plus = t.find('+');
    if (plus != std::string::npos) { pick = t.substr(plus + 1); t = t.substr(0, plus); }
    static const char* tabs[] = {"player", "gameplay", "controls", "video", "crosshair", "hud"};
    for (int i = 0; i < 6; i++) if (t.rfind(tabs[i], 0) == 0) setTab_ = i;
    if (t == "fonts") fontsOpen_ = true;
    if (t == "video/devices") { devicesOpen_ = true; devListed_ = false; }
    if (t == "controls/movement") controlsSub_ = 1;
    if (t == "controls/files") filePick_ = 2;
    if (t == "hud/elements") ui_.openPopup("ELEMENT", true);
    if (t == "hud/widgets") { widgetEdit_ = true; widgetSel_ = 0; }
    if (t == "hud/edit") hudEditOpen();
    static const char* els[] = {"hud/health", "hud/weapon", "hud/bars", "hud/speed", "hud/slots", "hud/keys", "hud/viewmodel", "hud/grapple"};
    static const int elIds[] = {Settings::HUD_HEALTH, Settings::HUD_WEAPON, Settings::HUD_BARS, Settings::HUD_SPEED, Settings::HUD_SLOTS, Settings::HUD_KEYS,
                                Settings::HUD_VIEWMODEL, Settings::HUD_GRAPPLE};
    for (int i = 0; i < 8; i++) if (t == els[i]) hudSel_ = elIds[i];
    if (!pick.empty()) ui_.openPopup(pick, false);
}

void Game::drawSettings() {
    if (setTab_ == TAB_HUD && widgetEdit_) { drawWidgetEditor(); return; }   // (the HUD's widget editor: the whole screen)
    if (setTab_ == TAB_HUD && hudEdit_) { drawHudEditor(); return; }        // (EDIT ON SCREEN: the whole screen)
    const SetLayout L{ui_.s(), r_.width() * 0.5f};
    const bool modal = setTab_ == TAB_CONTROLS && filePick_ == 2;   // the import list: the page under it takes nothing
    ui_.muted = modal;
    // HUD tab: the HUD under a see-through panel (in game drawHud drew the live one; the main menu's: sample values)
    const bool hudPreview = hudPreviewOn();
    if (hudPreview && !settingsInGame_) drawHudSample((float)r_.width(), (float)r_.height(), (float)r_.height() / 720.0f);
    // (the HUD tab's panel ends higher: its footer stays clear of the HUD's bottom row, the weapon list and health)
    const float footY = setTab_ == TAB_HUD ? kHudFootY : 640;
    ui_.panel(L.X(252), L.Y(32), L.L(776), L.L(footY + 48 - 32), hudPreview ? 0.6f : 1.0f);
    {   // the tab row, an accent line under it
        static const char* names[] = {"PLAYER", "GAMEPLAY", "CONTROLS", "VIDEO + AUDIO", "CROSSHAIR", "HUD"};
        static const char* navs[] = {"~settings", "~settings/gameplay", "~settings/controls", "~settings/video", "~settings/crosshair", "~settings/hud"};
        static const float tx[] = {268, 377, 508, 638, 795, 932}, tw[] = {105, 127, 126, 153, 133, 80};
        int pick = -1;
        for (int t = 0; t < 6; t++)
            if (ui_.tab(L.X(tx[t]), L.Y(40), L.L(tw[t]), L.L(kRowH), names[t], t == setTab_, UiExp::nav(navs[t]))) pick = t;
        r_.rect(L.X(268), L.Y(72), L.L(744), L.L(2), ui_.accent);
        if (pick >= 0) { openSettingsTab(pick); return; }   // (next frame: the new tab)
    }
    if (fontsOpen_) drawFontSettings(L);
    else if (devicesOpen_) drawDeviceSettings(L);
    else switch (setTab_) {
        case TAB_PLAYER: drawPlayerSettings(L); break;
        case TAB_GAMEPLAY: drawGameplaySettings(L); break;
        case TAB_CONTROLS: drawControls(L); break;
        case TAB_VIDEO: drawVideoSettings(L); break;
        case TAB_CROSSHAIR: drawCrosshairSettings(L); break;
        case TAB_HUD: drawHudSettings(L); break;
    }
    if (setTab_ == TAB_HUD && (widgetEdit_ || hudEdit_)) return;   // (WIDGET EDITOR / EDIT ON SCREEN was just clicked)
    // the footer: BACK (FONT: back to PLAYER; DEVICES: to VIDEO + AUDIO), and the page's reset on the right (drawn by the page)
    if (fontsOpen_) {
        if (ui_.button(L.X(268), L.Y(640), L.L(kHalfW), L.L(kRowH), "BACK", UiExp::nav("~settings"))) { fontsOpen_ = false; cfg_.save(); }
    } else if (devicesOpen_) {
        if (ui_.button(L.X(268), L.Y(640), L.L(kHalfW), L.L(kRowH), "BACK", UiExp::nav("~settings/video"))) { devicesOpen_ = false; cfg_.save(); }
    } else if (ui_.button(L.X(268), L.Y(footY), L.L(kHalfW), L.L(kRowH), "BACK", UiExp::nav(settingsInGame_ ? "ingame/pause" : "main"))) {
        leaveSettings();
        return;
    }
    if (modal) drawImportDialog(L);
    if (hudPreview) drawHudPreviewTop();   // (under the dropdown's list)
    ui_.popups();
}

// "In <mode> use <profile>" for HUD layouts (hud) or control profiles: two rows and a hint
static bool profileRows(Ui& ui, SetFlow& f, bool hud, int& ruleMode, std::string* rules, const std::vector<std::string>& files) {
    static std::vector<std::string> mn;
    if (mn.empty()) for (int m = 0; m < MODE_COUNT; m++) mn.push_back(std::string("APPLY IN: ") + upper(modeName(m)));
    ui.cycle(f.x(), f.top(), f.w(), f.h(), "MODE", ruleMode, MODE_COUNT, mn, UiExp::local());
    f.next();
    std::string& rule = rules[ruleMode];
    std::vector<std::string> fn = {hud ? "PROFILE: MY HUD" : "PROFILE: MY CONTROLS"};
    int cur = 0;
    for (auto& fl : files) { fn.push_back("PROFILE: " + upper(fl)); if (fl == rule) cur = (int)fn.size() - 1; }
    if (!rule.empty() && cur == 0) { fn.push_back("PROFILE: " + upper(rule) + " (MISSING)"); cur = (int)fn.size() - 1; }
    bool changed = ui.cycle(f.x(), f.top(), f.w(), f.h(), "PROFILE", cur, (int)fn.size(), fn,
                            (fn.size() < 2 ? UiExp::inert() : UiExp::cfg(hud ? "mode_hud_*" : "mode_controls_*")).clipOk());   // (file names: may be long)
    if (changed) rule = cur == 0 || cur > (int)files.size() ? "" : files[cur - 1];
    f.next();
    f.hint(hud ? "RANKED ALWAYS USES THE DEFAULT HUD" : "RANKED ALWAYS USES THE DEFAULT CONTROLS");
    return changed;
}

void Game::drawPlayerSettings(const SetLayout& L) {
    // --- left: IDENTITY, OUTLINES
    SetFlow f{ui_, L, kColL, 90};
    const float w = f.w(), h = f.h();
    f.header("IDENTITY");
    f.label("NAME");
    ui_.textField(1, f.x(), f.top(), w, h, cfg_.name, 20, UiExp::cfg("name").withId("NAME"));
    f.next();
    // Discord: run /link in the Discord server, type the 6-digit code the bot gives you here ("unlink" to remove it);
    // linking makes the Discord name public next to your game name and ranked id (crowns and queue fills)
    if (cfg_.discordName.empty()) f.label("DISCORD CODE");
    else {
        const float ts = ui_.textScale();
        ui_.label(f.x(), f.top(), "DISCORD: " + r_.textClip(upper(cfg_.discordName), ts, w - r_.textWidth("DISCORD:  (PUBLIC)", ts)) + " (PUBLIC)", kLabelC, 1.0f, w);
        f.y += 20;
    }
    ui_.textField(43, f.x(), f.top(), w, h, discordCode_, 8, UiExp::local().withId("DISCORD CODE"));
    f.next();
    f.hint(cfg_.discordName.empty() ? "GET IT WITH /link - THE CODE IS PUBLIC" : "A NEW CODE FROM /link RELINKS IT - unlink REMOVES IT");
    f.section("OUTLINES");
    {
        static std::vector<std::string> ot, oe;
        if (ot.empty()) for (int i = 0; i < kOutlineColors; i++) { ot.push_back(std::string("TEAM OUTLINE: ") + kOutlineNames[i]); oe.push_back(std::string("ENEMY OUTLINE: ") + kOutlineNames[i]); }
        ui_.cycle(f.x(), f.top(), w, h, "TEAM OUTLINE", cfg_.outlineFriendly, kOutlineColors, ot, UiExp::cfg("outline_friendly"));
        f.next();
        ui_.cycle(f.x(), f.top(), w, h, "ENEMY OUTLINE", cfg_.outlineEnemy, kOutlineColors, oe, UiExp::cfg("outline_enemy"));
        f.next();
        // outline widths (pixels): players' models and the map's edges; 0 = none (dragged: quarter pixels; typed: as typed, up to 6)
        if (ui_.slider(f.x(), f.top(), w, h, "PLAYER OUTLINE WIDTH", cfg_.outlinePlayer, 0, 4, "%.2f", UiExp::cfg("outline_player_width"), defaultSettings().outlinePlayer) && !ui_.typed())
            cfg_.outlinePlayer = std::round(cfg_.outlinePlayer * 4) / 4;
        f.next();
        if (ui_.slider(f.x(), f.top(), w, h, "WORLD OUTLINE WIDTH", cfg_.outlineWorld, 0, 4, "%.2f", UiExp::cfg("outline_world_width"), defaultSettings().outlineWorld) && !ui_.typed())
            cfg_.outlineWorld = std::round(cfg_.outlineWorld * 4) / 4;
        cfg_.outlinePlayer = clampf(cfg_.outlinePlayer, 0, 6);
        cfg_.outlineWorld = clampf(cfg_.outlineWorld, 0, 6);
        f.next();
        f.hint("WIDTH 0 = NO OUTLINE");
    }

    // --- right: LOOK, TEAM COLORS, INTERFACE
    SetFlow g{ui_, L, kColR, 90};
    g.header("LOOK");
    {
        static std::vector<std::string> mn;
        if (mn.empty()) for (int i = 0; i < kPlayerModels; i++) mn.push_back(std::string("PLAYER MODEL: ") + playerModelName(i));
        ui_.cycle(g.x(), g.top(), w, h, "MODEL", cfg_.model, kPlayerModels, mn, UiExp::cfg("model"));
        g.next();
    }
    {   // your name's colour as everyone sees it (scoreboard, chat, kill feed): off (the default), or one picked
        bool custom = cfg_.nameColor >= 0;
        if (ui_.toggle(g.x(), g.top(), w, h, "NAME COLOR", custom, UiExp::cfg("name_color"))) cfg_.nameColor = custom ? kNameColorOn : -1;
        g.next();
        Vec3 nc = rgbOf(cfg_.nameColor >= 0 ? cfg_.nameColor : 0xFFFFFF);
        int r = ui_.colorPicker(g.x(), g.top(), w, h, "CUSTOM NAME COLOR", nc, UiExp::cfg("name_color"), {1, 1, 1});
        if (r == Ui::RESET) cfg_.nameColor = -1;          // (right-click: back to the default, no colour)
        else if (r == Ui::CHANGED) cfg_.nameColor = packOf(nc);   // (picking one turns it on)
        g.next();
    }
    g.section("TEAM COLORS");
    {
        static std::vector<std::string> pn;
        if (pn.empty()) {
            for (int i = 0; i < kPaletteCount; i++) pn.push_back(std::string("TEAM COLORS: ") + kPalettes[i].name);
            pn.push_back("TEAM COLORS: CUSTOM");
        }
        int pi = -1;
        for (int i = 0; i < kPaletteCount; i++)
            if (length(kPalettes[i].friendly - cfg_.colorFriendly) < 0.01f && length(kPalettes[i].enemy - cfg_.colorEnemy) < 0.01f) pi = i;
        // own colours: CUSTOM is one more value, so ">" goes to the first preset and "<" to the last
        const int n = pi < 0 ? kPaletteCount + 1 : kPaletteCount;
        if (pi < 0) pi = kPaletteCount;
        if (ui_.cycle(g.x(), g.top(), w, h, "PRESET", pi, n, pn, UiExp::cfg("color_*")) && pi < kPaletteCount) {
            cfg_.colorFriendly = kPalettes[pi].friendly;
            cfg_.colorEnemy = kPalettes[pi].enemy;
        }
        g.next();
        g.hint("PRESETS SET BOTH COLORS BELOW");
    }
    ui_.colorPicker(g.x(), g.top(), w, h, "TEAM COLOR", cfg_.colorFriendly, UiExp::cfg("color_friendly"), defaultSettings().colorFriendly);
    g.next();
    ui_.colorPicker(g.x(), g.top(), w, h, "ENEMY COLOR", cfg_.colorEnemy, UiExp::cfg("color_enemy"), defaultSettings().colorEnemy);
    g.next();
    g.hint("TEAM COLOR IS ALSO THE MENU ACCENT");
    ui_.accent = rgba(cfg_.colorFriendly);
    g.section("INTERFACE");   // (FONT: a page of its own in this tab)
    if (ui_.button(g.x(), g.top(), w, h, "FONT: " + upper(cfg_.uiFont.empty() ? std::string(uifonts::kDefault) : cfg_.uiFont) + "  >",
                   UiExp::nav("~settings/fonts").withId("FONT"))) { fontsOpen_ = true; fontListed_ = false; fontScroll_ = 0; ui_.closePopup(); }
    g.next();
    g.hint("THE FONT OF THE MENUS AND THE HUD");
}

void Game::drawGameplaySettings(const SetLayout& L) {
    SetFlow f{ui_, L, kColL, 90};
    const float w = f.w(), h = f.h();
    f.header("VIEW");
    ui_.slider(f.x(), f.top(), w, h, "FOV", cfg_.fov, 70, 130, "%.0f", UiExp::cfg("fov"), defaultSettings().fov);
    cfg_.fov = clampf(cfg_.fov, 60, 140);   // (typed: quark.cfg's range)
    f.next();
    ui_.slider(f.x(), f.top(), w, h, "SCOPE OVERLAY", cfg_.scopeOpacity, 0, 1, "%.2f", UiExp::cfg("scope_opacity"), defaultSettings().scopeOpacity);
    cfg_.scopeOpacity = clampf(cfg_.scopeOpacity, 0, 1);
    f.next();
    // the first-person gun (and grapple launcher) see-through: 1 solid, 0 not drawn; HUD > WEAPON MODEL still places it
    ui_.slider(f.x(), f.top(), w, h, "WEAPON MODEL OPACITY", cfg_.viewmodelOpacity, 0, 1, "%.2f", UiExp::cfg("viewmodel_opacity"), defaultSettings().viewmodelOpacity);
    cfg_.viewmodelOpacity = clampf(cfg_.viewmodelOpacity, 0, 1);
    f.next();
    // the sniper's and rifle's alt-fire zoom (degrees, as FOV). Client-side only.
    ui_.slider(f.x(), f.top(), w, h, "SNIPER ZOOM FOV", cfg_.zoomFovSniper, 10, 90, "%.0f", UiExp::cfg("zoom_fov_sniper"), defaultSettings().zoomFovSniper);
    f.next();
    ui_.slider(f.x(), f.top(), w, h, "RIFLE ZOOM FOV", cfg_.zoomFovRifle, 10, 90, "%.0f", UiExp::cfg("zoom_fov_rifle"), defaultSettings().zoomFovRifle);
    cfg_.zoomFovSniper = clampf(cfg_.zoomFovSniper, 10, 140);   // (typed: quark.cfg's range; used up to the normal FOV)
    cfg_.zoomFovRifle = clampf(cfg_.zoomFovRifle, 10, 140);
    f.next();
    {
        static const std::string std = "STANDARD: SNIPER " + std::to_string((int)baseWeaponDef(WPN_SNIPER).zoomFov) + ", RIFLE " + std::to_string((int)baseWeaponDef(WPN_RIFLE).zoomFov);
        f.hint(std);
    }
    f.section("SHOTS");
    static const std::vector<std::string> own = {"YOUR SHOTS: WITH WEAPON MODEL", "YOUR SHOTS: ALWAYS", "YOUR SHOTS: NEVER"};
    ui_.cycle(f.x(), f.top(), w, h, "YOUR SHOTS", cfg_.ownShots, 3, own, UiExp::cfg("own_shots"));
    f.next();
    f.hint("TRACERS + LASER START WHERE THE GUN IS");
    ui_.toggle(f.x(), f.top(), w, h, "OTHER PLAYERS' TRACERS / BEAMS", cfg_.enemyShots, UiExp::cfg("enemy_shots"));
    f.next();
    {   // hit sparks (on walls and players) and muzzle flashes, yours and everyone's: cosmetic, visibility
        const float hw = L.L(kHalfW);
        ui_.toggle(f.x(), f.top(), hw, h, "IMPACT SPARKS", cfg_.impactSparks, UiExp::cfg("impact_sparks"));
        ui_.toggle(L.X(kColL + kColW - kHalfW), f.top(), hw, h, "MUZZLE FLASH", cfg_.muzzleFlash, UiExp::cfg("muzzle_flash"));
        f.next();
    }
    f.section("NETWORK");
    {   // other players' interpolation delay (ranked: always 10 ms): adaptive (the default), custom (the slider), or off
        static const std::vector<std::string> imn = {"SMOOTHING (INTERP): ADAPTIVE", "SMOOTHING (INTERP): CUSTOM", "SMOOTHING (INTERP): OFF"};
        int ii = cfg_.interpMs == 0 ? 0 : cfg_.interpMs < 0 ? 2 : 1;
        if (ui_.cycle(f.x(), f.top(), w, h, "INTERP", ii, 3, imn, UiExp::cfg("interp_ms"))) cfg_.interpMs = ii == 0 ? 0 : ii == 2 ? -1 : kCustomInterpMs;
        f.next();
        f.hint("HOW OTHER PLAYERS' MOVES ARE SMOOTHED");
        float v = (float)(cfg_.interpMs > 0 ? cfg_.interpMs : kCustomInterpMs);   // (moving it picks CUSTOM)
        if (ui_.slider(f.x(), f.top(), w, h, "SMOOTHING DELAY (MS)", v, 1, (float)kMaxInterpMs, "%.0f", UiExp::cfg("interp_ms"), (float)kCustomInterpMs))
            cfg_.interpMs = std::min(std::max(1, (int)std::lround(v)), kMaxInterpMs);
        f.next();
        f.hint("USED WHEN SMOOTHING IS CUSTOM");
    }

    SetFlow g{ui_, L, kColR, 90};
    g.header("FEEDBACK");
    ui_.toggle(g.x(), g.top(), w, h, "DAMAGE NUMBERS", cfg_.dmgNums, UiExp::cfg("dmg_nums"));
    g.next();
    {   // their size, with a sample of the number at that size
        const float sw = L.L(304), bx = L.X(kColR + 312), bw = L.L(48);
        if (ui_.slider(g.x(), g.top(), sw, h, "DAMAGE NUMBER SIZE", cfg_.dmgNumScale, 0.5f, 3.0f, "%.2f", UiExp::cfg("dmg_num_scale"), defaultSettings().dmgNumScale) && !ui_.typed())
            cfg_.dmgNumScale = std::round(cfg_.dmgNumScale * 20) / 20;   // (dragged: 0.05 steps; typed: as typed)
        cfg_.dmgNumScale = clampf(cfg_.dmgNumScale, 0.5f, 3.0f);
        float k = std::max(1.0f, std::round(ui_.textScale() * cfg_.dmgNumScale));
        r_.rect(bx, g.top(), bw, h, {0.42f, 0.43f, 0.45f, 1});
        if (cfg_.dmgNums) r_.text(bx + bw * 0.5f - r_.textWidth("42", k) * 0.5f, g.top() + (h - r_.textHeight(k)) * 0.5f, k, "42", kWhite);
        g.next();
    }
    // the flash round the screen's edges when you're hit (0 = off)
    ui_.slider(g.x(), g.top(), w, h, "DAMAGE FLASH", cfg_.damageFlash, 0, 1, "%.2f", UiExp::cfg("damage_flash"), defaultSettings().damageFlash);
    cfg_.damageFlash = clampf(cfg_.damageFlash, 0, 1);
    g.next();
    g.hint("SCREEN EDGES FLASH WHEN HIT. 0 = OFF");
    ui_.toggle(g.x(), g.top(), w, h, "STICKY COUNTER", cfg_.stickyCounter, UiExp::cfg("sticky_counter"));
    g.next();
    g.section("OTHER PLAYERS");
    ui_.toggle(g.x(), g.top(), w, h, "HEALTH BARS OVER PLAYERS", cfg_.healthBars, UiExp::cfg("health_bars"));
    g.next();
    g.hint("RANKED: NO HEALTH BARS");
    {
        static const std::vector<std::string> hbp = {"HEALTH BAR: OVER THE HEAD", "HEALTH BAR: AT THE FEET"};
        int hi = cfg_.healthBarsBottom ? 1 : 0;
        if (ui_.cycle(g.x(), g.top(), w, h, "HEALTH BAR POSITION", hi, 2, hbp, UiExp::cfg("health_bars_bottom"))) cfg_.healthBarsBottom = hi == 1;
        g.next();
    }
    ui_.toggle(g.x(), g.top(), w, h, "WINNERS CROWN", cfg_.crowns, UiExp::cfg("crowns"));
    g.next();
    g.section("CHAT");
    if (ui_.toggle(g.x(), g.top(), w, h, "DISABLE CHAT", cfg_.chatOff, UiExp::cfg("chat_off")) && cfg_.chatOff) for (auto& t : typingUntil_) t = 0;
    g.next();
    g.hint("HIDES ALL PLAYERS' CHAT MESSAGES");
}

void Game::drawVideoSettings(const SetLayout& L) {
    SetFlow f{ui_, L, kColL, 90};
    const float w = f.w(), h = f.h(), hw = L.L(kHalfW);
    f.header("DISPLAY");
    static const std::vector<std::string> dispNames = {"DISPLAY: WINDOWED", "DISPLAY: BORDERLESS FULLSCREEN", "DISPLAY: EXCLUSIVE FULLSCREEN"};
    if (ui_.cycle(f.x(), f.top(), w, h, "DISPLAY", cfg_.displayMode, 3, dispNames, UiExp::cfg("display_mode"))) applyDisplayMode();
    f.next();
    f.hint("EXCLUSIVE / BORDERLESS SKIP THE DESKTOP", "COMPOSITOR ON MOST DRIVERS");
    {   // any cap, typed or dragged (0: unlimited); right-click: the screen's refresh rate (G-Sync / FreeSync: a few under)
        gameMonitor();
        float v = (float)cfg_.maxFps;
        ui_.slider(f.x(), f.top(), w, h, "FPS LIMIT", v, 0, 1000, cfg_.maxFps <= 0 ? "UNLIMITED" : "%.0f", UiExp::cfg("maxfps"),
                   (float)(vmHz_ > 0 ? vmHz_ : 60));
        cfg_.maxFps = std::min(std::max(0, (int)std::lround(v)), 100000);
        f.next();
    }
    f.section("LATENCY");
#ifdef _WIN32
    static const std::vector<std::string> presNames = {"VSYNC: OFF", "VSYNC: ON", "VSYNC: ADAPTIVE", "VSYNC: LATENT SYNC"};
    const int nPres = 4;
#else
    static const std::vector<std::string> presNames = {"VSYNC: OFF", "VSYNC: ON", "VSYNC: ADAPTIVE"};
    const int nPres = 3;
#endif
    if (ui_.cycle(f.x(), f.top(), w, h, "PRESENT", cfg_.presentMode, nPres, presNames, UiExp::cfg("present_mode"))) applyPresentMode();
    f.next();
    f.hint("OFF = LOWEST LATENCY, MAY TEAR");
    if (cfg_.presentMode == 3) {   // latent sync: where the tear line goes (0: the top of the blank)
        float v = (float)cfg_.latentOffsetUs;
        ui_.slider(f.x(), f.top(), w, h, "TEAR LINE OFFSET (US)", v, -2000, 2000, "%.0f", UiExp::cfg("latent_offset_us"), 0);
        cfg_.latentOffsetUs = (int)std::lround(clampf(v, -4000, 4000));
        f.next();
    }
    static const std::vector<std::string> llNames = {"LOW LATENCY: OFF", "LOW LATENCY: ON", "LOW LATENCY: ULTRA"};
    ui_.cycle(f.x(), f.top(), w, h, "LOW LATENCY", cfg_.lowLatency, 3, llNames, UiExp::cfg("low_latency"));
    f.next();
    f.hint("ON: AT MOST 1 FRAME QUEUED", "ULTRA: 0 QUEUED + LATE INPUT SAMPLING");
#ifdef _WIN32
    // NVIDIA: "Prefer maximum performance" in quark's driver profile (the driver reads it at the next start)
    if (ui_.toggle(f.x(), f.top(), w, h, "GPU MAX CLOCKS", cfg_.gpuMaxClocks, UiExp::cfg("gpu_max_clocks"))) {
        std::string msg = "GPU MAX CLOCKS: NOT CHANGED IN THE UI TEST";
        if (!uiTest_ && !nvMaxClocks(cfg_.gpuMaxClocks, &msg)) cfg_.gpuMaxClocks = false;
        notify(msg);
    }
    f.next();
    f.hint("KEEPS GPU CLOCKS HIGH: LESS LAG, MORE POWER", "F3 SHOWS INPUT->FRAME LATENCY");
    if (ui_.toggle(f.x(), f.top(), w, h, "PRIORITIZE GAME", cfg_.prioritize, UiExp::cfg("prioritize")) && !uiTest_)
        setGamePriority(cfg_.prioritize && glfwGetWindowAttrib(win_, GLFW_FOCUSED) != 0);
    f.next();
    f.hint("GPU + CPU GO TO THE GAME FIRST WHILE FOCUSED", "(BROWSER VIDEOS, ELECTRON APPS WAIT)");
#else
    f.y -= 16;
    f.hint("", "F3 SHOWS INPUT->FRAME LATENCY");
#endif
    f.section("FLAT MAP");
    ui_.toggle(f.x(), f.top(), w, h, "FULLBRIGHT", cfg_.fullbright, UiExp::cfg("fullbright"));
    f.next();
    f.hint("FLAT MAP, NO LIGHTING");
    {   // its colour (white: the classic look)
        Vec3& c = cfg_.fullbrightColor;
        const Vec3& d = defaultSettings().fullbrightColor;
        ui_.colorPicker(f.x(), f.top(), hw, h, "MAP COLOR", c, UiExp::cfg("fullbright_color"), d);
        bool white = c.x == d.x && c.y == d.y && c.z == d.z;
        if (ui_.button(L.X(kColL + 184), f.top(), hw, h, "RESET MAP COLOR", UiExp::cfg("fullbright_color").withId("RESET MAP COLOR"), !white)) c = d;
        f.next();
    }

    SetFlow g{ui_, L, kColR, 90};
    g.header("IMAGE");
    // (BRIGHTNESS and PLAYER SHADOWS share a row, as RENDER SCALE and SHARPEN do: a row more would push SOUND into
    // DEVICES with Windows' EXCLUSIVE AUDIO)
    ui_.slider(g.x(), g.top(), hw, h, "BRIGHTNESS", cfg_.brightness, 0.6f, 1.8f, "%.2f", UiExp::cfg("brightness"), defaultSettings().brightness);
    cfg_.brightness = clampf(cfg_.brightness, 0.6f, 1.8f);   // (typed: quark.cfg's range)
    ui_.toggle(L.X(kColR + kColW - kHalfW), g.top(), hw, h, "PLAYER SHADOWS", cfg_.shadows, UiExp::cfg("shadows"));
    g.next();
    {
        static const int aaModes[] = {0, 1, 2, 4};
        static const std::vector<std::string> aaNames = {"ANTI-ALIASING: OFF", "ANTI-ALIASING: SMOOTH (CHEAP)", "ANTI-ALIASING: MSAA 2X", "ANTI-ALIASING: MSAA 4X"};
        int ai = 0;
        for (int i = 0; i < 4; i++) if (aaModes[i] == cfg_.msaa) ai = i;
        if (ui_.cycle(g.x(), g.top(), w, h, "ANTI-ALIASING", ai, 4, aaNames, UiExp::cfg("msaa"))) cfg_.msaa = aaModes[ai];
        g.next();
    }
    {   // the 3D scene at this % of the window's pixels (the HUD and menus always at 100%): below for frame rate, above
        // as supersampling; SHARPEN: below 100%, sharpened as it's stretched
        float v = (float)cfg_.renderScale;
        if (ui_.slider(g.x(), g.top(), hw, h, "RENDER SCALE", v, 50, 200, "%.0f%%", UiExp::cfg("render_scale"), (float)defaultSettings().renderScale))
            cfg_.renderScale = std::max(50, std::min(200, (int)std::lround(v)));
        ui_.toggle(L.X(kColR + kColW - kHalfW), g.top(), hw, h, "SHARPEN", cfg_.renderSharpen, UiExp::cfg("render_sharpen"));
        g.next();
    }
    g.section("SOUND");
    if (ui_.slider(g.x(), g.top(), w, h, "VOLUME", cfg_.volume, 0, 1, "%.2f", UiExp::cfg("volume"), defaultSettings().volume)) audio_.setMasterVolume(cfg_.volume = clampf(cfg_.volume, 0, 1));
    g.next();
    // on top of VOLUME: every sound effect (the hitsound too), and the announcer's lines
    if (ui_.slider(g.x(), g.top(), w, h, "EFFECTS VOLUME", cfg_.volumeEffects, 0, 1, "%.2f", UiExp::cfg("volume_effects"), defaultSettings().volumeEffects))
        audio_.setEffectsVolume(cfg_.volumeEffects = clampf(cfg_.volumeEffects, 0, 1));
    g.next();
    if (ui_.slider(g.x(), g.top(), w, h, "ANNOUNCER VOLUME", cfg_.volumeAnnouncer, 0, 1, "%.2f", UiExp::cfg("volume_announcer"), defaultSettings().volumeAnnouncer))
        audio_.setAnnouncerVolume(cfg_.volumeAnnouncer = clampf(cfg_.volumeAnnouncer, 0, 1));
    g.next();
    // quips (the QUIP key): their own volume and on / off, apart from the announcer's (QUIPS shares HITSOUND's row:
    // a row more would not fit above DEVICES with Windows' EXCLUSIVE AUDIO)
    if (ui_.slider(g.x(), g.top(), w, h, "QUIPS VOLUME", cfg_.volumeQuips, 0, 1, "%.2f", UiExp::cfg("volume_quips"), defaultSettings().volumeQuips))
        audio_.setQuipVolume(cfg_.volumeQuips = clampf(cfg_.volumeQuips, 0, 1));
    g.next();
    ui_.toggle(g.x(), g.top(), hw, h, "HITSOUND", cfg_.hitsounds, UiExp::cfg("hitsounds"));
    if (ui_.toggle(L.X(kColR + kColW - kHalfW), g.top(), hw, h, "QUIPS", cfg_.quips, UiExp::cfg("quips"))) {
        audio_.setQuips(cfg_.quips);
        if (cfg_.quips && audio_.quipCount() > 0) audio_.play(SND_QUIP, 1.0f);   // (a taste of them)
    }
    g.next();
    {
        static const std::vector<std::string> sn = {"SOUND PACK: DEFAULT", "SOUND PACK: HEAVY METAL", "SOUND PACK: MAGICAL", "SOUND PACK: RETRO", "SOUND PACK: MOUTH"};
        ui_.cycle(g.x(), g.top(), w, h, "SOUNDS", cfg_.soundPack, 5, sn, UiExp::cfg("soundpack"));
        g.next();
    }
#ifdef _WIN32
    // the sound device to the game alone: about 10 ms sooner, and Discord / music go silent while the game runs
    if (ui_.toggle(g.x(), g.top(), w, h, !cfg_.exclusiveAudio || audio_.exclusiveActive() ? "EXCLUSIVE AUDIO" : audio_.exclusiveState() == Audio::kExclUnsupported ? "EXCLUSIVE AUDIO (N/A)" : "EXCLUSIVE AUDIO (BUSY)",   // (N/A: the device takes none of its formats)
                   cfg_.exclusiveAudio, UiExp::cfg("exclusive_audio")))
        audio_.setExclusive(cfg_.exclusiveAudio);
    g.next();
    g.hint("TAKES THE SOUND DEVICE FOR LOWER AUDIO LAG");
#endif
    {   // the announcer (in SOUND: its own section header no longer fit under the two volumes above)
        static const std::vector<std::string> an = {"ANNOUNCER: OFF", "ANNOUNCER: COUNTDOWN", "ANNOUNCER: ALL CALLS"};
        static std::vector<std::string> av;
        if (av.empty()) for (int v = 0; v < kAnnouncerVoices; v++) av.push_back(std::string("VOICE: ") + announcerVoiceName(v));
        if (ui_.cycle(g.x(), g.top(), w, h, "ANNOUNCER", cfg_.announcer, 3, an, UiExp::cfg("announcer")) && cfg_.announcer > 0) audio_.play(SND_VOICE_GO, 1.0f);
        g.next();
        if (ui_.cycle(g.x(), g.top(), w, h, "VOICE", cfg_.announcerVoice, kAnnouncerVoices, av, UiExp::cfg("voice"))) { audio_.setAnnouncer(cfg_.announcerVoice); audio_.play(SND_VOICE_GODLIKE, 1.0f); }
        g.next();
    }
    // the footer's right: DEVICES (sound output, microphone, monitor), a page of its own in this tab
    if (ui_.button(L.X(836), L.Y(640), hw, h, "DEVICES  >", UiExp::nav("~settings/video/devices").withId("DEVICES"))) {
        devicesOpen_ = true; devListed_ = false;
        for (int& s : devScroll_) s = 0;
        ui_.closePopup();
    }
}

// The page's lists, from the OS (a few ms: on opening the page, REFRESH, a monitor plugged in or out). Names upper
// case and unique; monitors with their desktop mode.
void Game::listDevices() {
    auto shown = [](std::vector<DeviceEntry> v) {
        std::vector<std::string> names;
        for (auto& d : v) names.push_back(upper(d.name));
        names = uniqueNames(names);
        for (size_t i = 0; i < v.size(); i++) v[i].name = names[i];
        return v;
    };
    devOut_ = shown(Audio::listDevices(false));
    devIn_ = shown(Audio::listDevices(true));
    devMons_.clear();
    int n = 0;
    GLFWmonitor** ms = glfwGetMonitors(&n);
    std::vector<std::string> names;
    for (int i = 0; i < n; i++) { const char* nm = glfwGetMonitorName(ms[i]); names.push_back(nm ? nm : "MONITOR"); }
    names = uniqueNames(names);   // (the ids, as cfg_.monitor stores them)
    for (int i = 0; i < n; i++) {
        const DeskMode m = desktopMode(ms[i]);
        char mode[48];
        snprintf(mode, sizeof mode, "  %dX%d %d HZ", m.w, m.h, m.hz);
        devMons_.push_back({names[i], upper(names[i]) + mode});
    }
    listRates();
    devListed_ = true;
    devScans_++;
    devRowText();
}

// The rows' ids and in-use labels, once a listing (not per frame). A list that came back the same keeps its strings:
// the UI test holds ids by pointer, across a REFRESH too.
void Game::devRowText() {
    static const char* const prefix[3] = {"OUTPUT", "INPUT", "MONITOR"};
    static const char* const def[3] = {"SYSTEM DEFAULT", "SYSTEM DEFAULT", "PRIMARY MONITOR"};
    const std::vector<DeviceEntry>* devs[3] = {&devOut_, &devIn_, &devMons_};
    for (int k = 0; k < 3; k++) {
        std::vector<std::string> ids, on;
        for (size_t i = 0; i <= devs[k]->size(); i++) {
            const std::string& name = i == 0 ? std::string(def[k]) : (*devs[k])[i - 1].name;
            ids.push_back(std::string(prefix[k]) + ": " + name);
            on.push_back("> " + name + " <");
        }
        if (ids != devRowId_[k]) devRowId_[k].swap(ids);
        if (on != devRowOn_[k]) devRowOn_[k].swap(on);
    }
}

void Game::listRates() {
    devRates_.clear();
    devRateKey_[0] = -1;   // (REFRESH RATE's names: made again)
    GLFWmonitor* mon = chosenMonitor();
    if (!mon) return;
    const DeskMode d = desktopMode(mon);
    int n = 0;
    const GLFWvidmode* modes = glfwGetVideoModes(mon, &n);
    for (int i = 0; i < n; i++)
        if (modes[i].width == d.w && modes[i].height == d.h && modes[i].refreshRate != d.hz &&
            std::find(devRates_.begin(), devRates_.end(), modes[i].refreshRate) == devRates_.end())
            devRates_.push_back(modes[i].refreshRate);
    std::sort(devRates_.rbegin(), devRates_.rend());
}

// DEVICES (VIDEO + AUDIO > DEVICES): a list of rows each for the sound output and the microphone (left), the monitor
// and exclusive fullscreen's refresh rate (right), like the FONT page's rows: the one in use "> NAME <", a click on
// another picks it, saves it and applies it at once (the output reopened, the window moved). The wheel scrolls a list
// longer than its rows. REFRESH (bottom right) asks the OS again.
void Game::drawDeviceSettings(const SetLayout& L) {
    if (!devListed_) listDevices();
    const float w = L.L(kColW), h = L.L(kRowH), hw = L.L(kHalfW);
    const Color warn = {1.0f, 0.55f, 0.3f, 1};
    // one list: the default first, then the OS's; rows at most; returns true when a row was picked (value set)
    auto list = [&](SetFlow& f, int which, const std::string& defName, const std::vector<DeviceEntry>& devs,
                    std::string& value, const char* key, int rows) -> bool {
        const std::vector<std::string>& ids = devRowId_[which];   // the rows' UI test ids "<PREFIX>: <NAME>" (devRowText)
        const std::vector<std::string>& onText = devRowOn_[which];
        const int n = (int)devs.size() + 1;
        devScroll_[which] = std::min(devScroll_[which], std::max(0, n - rows));
        const int first = devScroll_[which], shown = std::min(n - first, rows);
        float* box = devListBox_[which];
        box[0] = f.x(); box[1] = f.top(); box[2] = f.w(); box[3] = L.L(40.0f * shown);
        const int cur = findDevice(devs, value);   // (gone: no row is the current one)
        bool picked = false;
        for (int r = 0; r < shown; r++) {
            const int i = first + r;   // (0: the default)
            const std::string& name = i == 0 ? defName : devs[i - 1].name;
            const bool on = i == 0 ? cur == kDeviceDefault : cur == i - 1;
            const UiExp e = (on ? UiExp::inert() : UiExp::cfg(key)).withId(ids[i].c_str()).clipOk();   // (names: may be long)
            if (ui_.button(f.x(), f.top(), w, h, on ? onText[i] : name, e) && !on && !picked) {
                value = i == 0 ? std::string() : devs[i - 1].id;
                picked = true;
            }
            f.next();
        }
        if (n > rows) f.hint(std::to_string(first + 1) + "-" + std::to_string(first + shown) + " OF " + std::to_string(n) + " - WHEEL TO SCROLL");
        return picked;
    };

    // --- left: AUDIO OUTPUT, AUDIO INPUT
    SetFlow f{ui_, L, kColL, 90};
    f.header("AUDIO OUTPUT");
    if (list(f, 0, "SYSTEM DEFAULT", devOut_, cfg_.audioOutput, "audio_output", 5)) { audio_.setOutputDevice(cfg_.audioOutput); cfg_.save(); }
    if (findDevice(devOut_, cfg_.audioOutput) == kDeviceGone || (audio_.ok() && audio_.outputMissing())) {
        ui_.hint(f.x(), L.Y(f.y - 4), "THE SAVED DEVICE IS GONE: THE DEFAULT PLAYS", warn, f.w());
        f.y += 16;
    } else f.hint("SOUND SWITCHES TO IT AT ONCE");
    f.section("AUDIO INPUT (MICROPHONE)");
    if (list(f, 1, "SYSTEM DEFAULT", devIn_, cfg_.audioInput, "audio_input", 4)) cfg_.save();
    if (findDevice(devIn_, cfg_.audioInput) == kDeviceGone) {
        ui_.hint(f.x(), L.Y(f.y - 4), "THE SAVED MICROPHONE IS GONE: THE DEFAULT", warn, f.w());
        f.y += 16;
    }
    f.hint("NOTHING IN THE GAME RECORDS YET: THIS IS", "SAVED FOR VOICE CHAT LATER");

    // --- right: MONITOR, REFRESH RATE
    SetFlow g{ui_, L, kColR, 90};
    g.header("MONITOR");
    if (list(g, 2, "PRIMARY MONITOR", devMons_, cfg_.monitor, "monitor", 5)) {
        cfg_.save();
        listRates();   // (the new monitor's)
        if (cfg_.displayMode != 0) applyDisplayMode();   // (fullscreen / borderless: the window goes there now)
    }
    if (findDevice(devMons_, cfg_.monitor) == kDeviceGone) {
        ui_.hint(g.x(), L.Y(g.y - 4), "THE SAVED MONITOR IS GONE: THE PRIMARY", warn, g.w());
        g.y += 16;
    } else g.hint("FULLSCREEN AND BORDERLESS OPEN ON IT");
    g.section("REFRESH RATE");
    {   // exclusive fullscreen's: the desktop's, or another the monitor has at that size
        GLFWmonitor* mon = chosenMonitor();
        const int deskHz = mon ? desktopMode(mon).hz : 0;
        std::vector<std::string>& names = devRateNames_;   // (made again only when what they show changes: not per frame)
        if (devRateKey_[0] != deskHz || devRateKey_[1] != cfg_.fullscreenHz) {
            devRateKey_[0] = deskHz; devRateKey_[1] = cfg_.fullscreenHz;
            names.clear();
            names.push_back("REFRESH RATE: DESKTOP (" + std::to_string(deskHz) + " HZ)");
            for (int hz : devRates_) names.push_back("REFRESH RATE: " + std::to_string(hz) + " HZ");
            if (cfg_.fullscreenHz > 0 && cfg_.fullscreenHz != deskHz &&   // (saved for another monitor: kept until changed)
                std::find(devRates_.begin(), devRates_.end(), cfg_.fullscreenHz) == devRates_.end())
                names.push_back("REFRESH RATE: " + std::to_string(cfg_.fullscreenHz) + " HZ (NOT ON THIS MONITOR)");
        }
        int idx = 0;
        for (size_t i = 0; i < devRates_.size(); i++)
            if (devRates_[i] == cfg_.fullscreenHz) idx = (int)i + 1;
        if (idx == 0 && (int)names.size() > (int)devRates_.size() + 1) idx = (int)names.size() - 1;
        if (ui_.cycle(g.x(), g.top(), w, h, "REFRESH RATE", idx, (int)names.size(), names, UiExp::cfg("fullscreen_hz"))) {
            cfg_.fullscreenHz = idx == 0 || idx > (int)devRates_.size() ? 0 : devRates_[idx - 1];
            cfg_.save();
            if (cfg_.displayMode == 2) applyDisplayMode();
        }
        g.next();
        g.hint("EXCLUSIVE FULLSCREEN ONLY");
    }
    // the footer's right: ask the OS again (a device plugged in since); a missing output is tried again
    if (ui_.button(L.X(836), L.Y(640), hw, h, "REFRESH", UiExp::state("devscan"))) { devListed_ = false; audio_.retryOutputDevice(); }
}

void Game::drawControls(const SetLayout& L) {
    const float w = L.L(kColW), h = L.L(kRowH), hw = L.L(kHalfW);
    // the two sub-pages
    if (ui_.tab(L.X(268), L.Y(90), L.L(368), h, "GENERAL + INTERFACE KEYS", controlsSub_ == 0, UiExp::nav("~settings/controls"), true)) { controlsSub_ = 0; captureAction_ = -1; }
    if (ui_.tab(L.X(644), L.Y(90), L.L(368), h, "MOVE + WEAPON KEYS", controlsSub_ == 1, UiExp::nav("~settings/controls/movement"), true)) { controlsSub_ = 1; captureAction_ = -1; }
    ui_.hint(L.X(268), L.Y(126), captureAction_ >= 0 ? "PRESS A KEY, MOUSE BUTTON OR SCROLL - ESC CANCELS, DEL CLEARS"
                                                     : "CLICK A SLOT TO REBIND IT - RIGHT-CLICK OR DEL UNBINDS", kHintC, L.L(744));
    // key rows: a header with KEY 1 / KEY 2 over the slots, then a row an action (label, two 108 px slots)
    static std::string idText[A_COUNT][2];   // the UI test's ids: "<ACTION> KEY 1", "<ACTION> KEY 2"
    static const char* ids[A_COUNT][2];
    if (!ids[0][0])
        for (int a = 0; a < A_COUNT; a++)
            for (int k = 0; k < 2; k++) { idText[a][k] = std::string(actionName(a)) + (k ? " KEY 2" : " KEY 1"); ids[a][k] = idText[a][k].c_str(); }
    auto keyRows = [&](float col, float y, const char* header, std::initializer_list<int> actions) -> float {
        ui_.label(L.X(col), L.Y(y), header, ui_.accent, 1.0f, L.L(128));
        const float hs = ui_.hintScale();
        for (int k = 0; k < 2; k++) {
            const char* cap = k ? "KEY 2" : "KEY 1";
            r_.text(L.X(col + 136 + k * 116 + 54) - r_.textWidth(cap, hs) * 0.5f, L.Y(y + 3), hs, cap, kHintC);
        }
        y += 24;
        for (int a : actions) {
            const char* name = a == A_NEXTWEAPON ? "NEXT WEAPON" : a == A_PREVWEAPON ? "PREV WEAPON" : actionName(a);
            ui_.labelFit(L.X(col), L.Y(y + 8), L.L(128), name, kLabelC);
            for (int k = 0; k < 2; k++) {
                const float bx = L.X(col + 136 + k * 116), by = L.Y(y), bw = L.L(108);
                const bool cap = captureAction_ == a && captureSlot_ == k;
                int& code = cfg_.binds.code[a][k];
                if (ui_.button(bx, by, bw, h, cap ? "PRESS..." : inputName(code), UiExp::bind().withId(ids[a][k]))) { captureAction_ = a; captureSlot_ = k; }
                else if (ui_.rclicked(bx, by, bw, h)) {   // right-click: unbind the slot
                    if (cap) captureAction_ = -1;
                    if (code) { code = 0; cfg_.save(); }
                }
            }
            y += 36;
        }
        return y + 16;   // (the next header: 20 under the last row)
    };
    if (controlsSub_ == 1) {
        float y = keyRows(kColL, 154, "MOVEMENT", {A_FORWARD, A_BACK, A_LEFT, A_RIGHT, A_JUMP, A_DASH, A_GRAPPLE, A_USE});
        keyRows(kColL, y, "ATTACK", {A_FIRE, A_ALTFIRE});
        keyRows(kColR, 154, "WEAPONS", {A_WEAPON1, A_WEAPON2, A_WEAPON3, A_WEAPON4, A_WEAPON5, A_WEAPON6, A_WEAPON7, A_WEAPON8, A_WEAPON9, A_WEAPON10, A_NEXTWEAPON, A_PREVWEAPON});
    } else {
        float y = keyRows(kColR, 154, "CHAT", {A_CHAT, A_TOGGLECHAT, A_QUIP});
        y = keyRows(kColR, y, "VOTES", {A_VOTEYES, A_VOTENO, A_VOTESTART});
        keyRows(kColR, y, "SCREEN", {A_SCORES, A_PERF, A_PERFMINI, A_SCREENSHOT});
        // left: the mouse, key options, per-mode profiles, import / export
        SetFlow f{ui_, L, kColL, 154};
        f.header("MOUSE");
        ui_.slider(f.x(), f.top(), w, h, "SENSITIVITY", cfg_.sensitivity, 0.1f, 10.0f, "%.2f", UiExp::cfg("sensitivity"), defaultSettings().sensitivity);
        cfg_.sensitivity = clampf(cfg_.sensitivity, 0.01f, 50.0f);   // (typed: quark.cfg's range)
        f.next();
        {   // the same thing as cm per 360 at your mouse DPI (drag either); counts per 360 = 360 / (sens * 0.022), cm = counts / dpi * 2.54
            auto cmOf = [&](float sens) { return 360.0f / (sens * 0.022f) / cfg_.dpi * 2.54f; };
            float cm = cmOf(cfg_.sensitivity);
            if (ui_.slider(f.x(), f.top(), hw, h, "CM PER 360", cm, 5, 120, "%.1f", UiExp::cfg("sensitivity"), cmOf(defaultSettings().sensitivity)))
                cfg_.sensitivity = clampf(360.0f / (cm / 2.54f * cfg_.dpi) / 0.022f, 0.01f, 50.0f);
            static std::string dpiText;   // click and type any DPI
            if (!ui_.typing()) dpiText = std::to_string(cfg_.dpi);
            const float dx = L.X(kColL + 184);
            if (ui_.textField(42, dx, f.top(), hw, h, dpiText, 6, UiExp::cfg("dpi").withId("DPI"))) {
                dpiText.erase(std::remove_if(dpiText.begin(), dpiText.end(), [](char ch) { return ch < '0' || ch > '9'; }), dpiText.end());
                int v = atoi(dpiText.c_str());
                if (v >= 50 && v <= 100000) cfg_.dpi = v;
            }
            ui_.label(dx + hw - r_.textWidth("DPI", ui_.textScale()) - 6 * L.s, f.top() + (h - r_.textHeight(ui_.textScale())) * 0.5f, "DPI", {0.6f, 0.62f, 0.66f, 1});
            f.next();
            f.hint("CM PER 360: MOUSE TRAVEL FOR ONE FULL TURN");
        }
        f.section("KEYS");   // control scripts (off in ranked)
        static const std::vector<std::string> sn = {"OPPOSITE KEYS: CANCEL", "OPPOSITE KEYS: LAST WINS", "OPPOSITE KEYS: FIRST WINS"};
        static const std::vector<std::string> an = {"ALT-FIRE: HOLD", "ALT-FIRE: TOGGLE"};
        if (ui_.cycle(f.x(), f.top(), w, h, "SOCD", cfg_.socd, 3, sn, UiExp::cfg("socd"))) cfg_.save();
        f.next();
        f.hint("LEFT + RIGHT HELD: LAST WINS = SNAP TAP");
        int at = cfg_.altToggle;
        if (ui_.cycle(f.x(), f.top(), w, h, "ALT", at, 2, an, UiExp::cfg("alt_toggle"))) { cfg_.altToggle = at; cfg_.save(); }
        f.next();
        f.section("PROFILES");
        if (profileRows(ui_, f, false, ruleMode_, cfg_.modeCtl, profilesCached("controls", time_))) { cfg_.save(); appliedMode_ = -2; }   // (re-applied now)
        const float tw = L.L((kColW - 16) / 3), tg = L.L(8);
        if (ui_.button(f.x(), f.top(), tw, h, "IMPORT", UiExp::state("ctlimport"))) {   // controls on the clipboard win, else pick a file from controls/
            const char* clip = glfwGetClipboardString(win_);
            if (clip && controlsFromText(clip, cfg_.binds, cfg_.socd, cfg_.altToggle)) { cfg_.save(); widgetMsg_ = "IMPORTED CONTROLS FROM CLIPBOARD"; widgetMsgUntil_ = time_ + 3; }
            else filePick_ = 2;
        }
        if (ui_.button(f.x() + tw + tg, f.top(), tw, h, "EXPORT", UiExp::state("files:controls"))) {
            std::string text = controlsToText(cfg_.binds, cfg_.socd, cfg_.altToggle), name = stampedName("controls");
            glfwSetClipboardString(win_, text.c_str());
            bool ok = writeTextFile(profileDir("controls") + "/" + name, text);
            g_profilesStale = true;
            widgetMsg_ = ok ? "COPIED + SAVED CONTROLS/" + upper(name) : "COPIED (COULDN'T WRITE FILE)";
            widgetMsgUntil_ = time_ + 4;
        }
        if (ui_.button(f.x() + 2 * (tw + tg), f.top(), tw, h, "FOLDER", UiExp::url())) openUrl(profileDir("controls"));
        f.next();
        f.hint("CONTROL SCRIPTS ARE OFF IN RANKED");
        if (time_ < widgetMsgUntil_) ui_.hint(f.x(), f.top(), widgetMsg_, ui_.accent, w);
    }
    static const Binds defaults = [] { Binds b; b.setDefaults(); return b; }();
    bool isDefault = std::memcmp(defaults.code, cfg_.binds.code, sizeof defaults.code) == 0;
    if (ui_.button(L.X(836), L.Y(640), hw, h, "RESET ALL KEYS", UiExp::cfg("bind_*"), !isDefault)) { cfg_.binds.setDefaults(); captureAction_ = -1; cfg_.save(); }
}

// CONTROLS > IMPORT with nothing on the clipboard: the files in controls/, in a dialog over the (dimmed) tab
void Game::drawImportDialog(const SetLayout& L) {
    ui_.muted = false;
    r_.rect(L.X(252), L.Y(32), L.L(776), L.L(656), {0, 0, 0, 0.55f});
    const auto& files = profilesCached("controls", time_);
    const int n = std::min<int>((int)files.size(), 12);
    const float ph = 88 + 40.0f * n, py = 360 - ph * 0.5f;   // (centred on the shell)
    const float bw = L.L(kColW), bh = L.L(kRowH), bx = L.X(460);
    ui_.panel(L.X(444), L.Y(py), L.L(392), L.L(ph), 1.0f);
    ui_.label(bx, L.Y(py + 16), files.empty() ? "NO FILES IN CONTROLS/ - USE FOLDER" : "PICK A FILE TO IMPORT", kWhite, 1.0f, bw);
    float y = py + 40;
    for (int i = 0; i < n; i++, y += 40)
        if (ui_.button(bx, L.Y(y), bw, bh, upper(files[i]), UiExp::state("ctlimport").withId("FILE"))) {
            const std::string f = files[i];
            filePick_ = 0;
            bool ok = controlsFromText(readTextFile(profileDir("controls") + "/" + f), cfg_.binds, cfg_.socd, cfg_.altToggle);
            if (ok) cfg_.save();
            widgetMsg_ = ok ? "IMPORTED " + upper(f) : upper(f) + " HAS NO BINDS";
            widgetMsgUntil_ = time_ + 3;
            return;
        }
    if (ui_.button(bx, L.Y(y), bw, bh, "CANCEL", UiExp::nav("~settings/controls"))) filePick_ = 0;
}

void Game::drawCrosshairSettings(const SetLayout& L) {
    const float s = L.s, w = L.L(kColW), h = L.L(kRowH);
    {   // the preview on a mid-grey strip, like the world behind it: as it is, and as a hit flashes it
        const float px = L.X(268), py = L.Y(90), pw = L.L(744), ph = L.L(130), hs = ui_.hintScale();
        r_.rect(px, py, pw, ph, {0.42f, 0.43f, 0.45f, 1});
        const float x1 = px + pw / 3, x2 = px + pw * 2 / 3, cy = py + L.L(65);
        drawCrosshair(x1, cy, s, false);
        drawCrosshair(x2, cy, s, true);
        r_.text(x1 - r_.textWidth("NORMAL", hs) * 0.5f, L.Y(202), hs, "NORMAL", {0.85f, 0.86f, 0.88f, 1});
        r_.text(x2 - r_.textWidth("ON HIT", hs) * 0.5f, L.Y(202), hs, "ON HIT", {0.85f, 0.86f, 0.88f, 1});
    }
    SetFlow f{ui_, L, kColL, 240};
    f.header("SHAPE");
    static const std::vector<std::string> styles = {"STYLE: CROSS", "STYLE: CROSS + DOT", "STYLE: DOT", "STYLE: CIRCLE", "STYLE: T"};
    ui_.cycle(f.x(), f.top(), w, h, "STYLE", cfg_.chStyle, 5, styles, UiExp::cfg("ch_style"));
    f.next();
    ui_.slider(f.x(), f.top(), w, h, "LENGTH", cfg_.chLength, 0, 30, "%.0f", UiExp::cfg("ch_length"), defaultSettings().chLength);
    cfg_.chLength = clampf(std::round(cfg_.chLength), 0, 30);
    f.next();
    ui_.slider(f.x(), f.top(), w, h, "THICKNESS", cfg_.chThickness, 1, 8, "%.0f", UiExp::cfg("ch_thickness"), defaultSettings().chThickness);
    cfg_.chThickness = clampf(std::round(cfg_.chThickness), 1, 8);
    f.next();
    ui_.slider(f.x(), f.top(), w, h, "GAP", cfg_.chGap, 0, 20, "%.0f", UiExp::cfg("ch_gap"), defaultSettings().chGap);
    cfg_.chGap = clampf(std::round(cfg_.chGap), 0, 20);
    f.next();
    ui_.toggle(f.x(), f.top(), w, h, "OUTLINE", cfg_.chOutline, UiExp::cfg("ch_outline"));
    f.next();

    SetFlow g{ui_, L, kColR, 240};
    g.header("COLOR");
    ui_.toggle(g.x(), g.top(), w, h, "TEAM COLOR", cfg_.chTeamColor, UiExp::cfg("ch_team_color"));
    g.next();
    g.hint("ON: USES YOUR TEAM COLOR, NOT THE ONE BELOW");
    ui_.colorPicker(g.x(), g.top(), w, h, "CROSSHAIR COLOR", cfg_.chColor, UiExp::cfg("ch_color"), defaultSettings().chColor);
    g.next();
    g.section("ON HIT");   // HIT FLASH: off, or the colour a hit flashes the crosshair (white by default)
    ui_.toggle(g.x(), g.top(), w, h, "HIT FLASH", cfg_.chHitFlash, UiExp::cfg("ch_hit_flash"));
    g.next();
    ui_.colorPicker(g.x(), g.top(), w, h, "HIT FLASH COLOR", cfg_.chHitColor, UiExp::cfg("ch_hit_color"), defaultSettings().chHitColor);
    g.next();
    const Settings& d = defaultSettings();   // (RESET is greyed out while the crosshair is the default one)
    bool chDefault = cfg_.chStyle == d.chStyle && cfg_.chLength == d.chLength && cfg_.chThickness == d.chThickness && cfg_.chGap == d.chGap &&
                     cfg_.chOutline == d.chOutline && cfg_.chTeamColor == d.chTeamColor && length(cfg_.chColor - d.chColor) == 0 &&
                     cfg_.chHitFlash == d.chHitFlash && length(cfg_.chHitColor - d.chHitColor) == 0;
    if (ui_.button(L.X(836), L.Y(640), L.L(kHalfW), h, "RESET CROSSHAIR", UiExp::cfg("ch_*"), !chDefault)) {
        cfg_.chStyle = d.chStyle; cfg_.chLength = d.chLength; cfg_.chThickness = d.chThickness; cfg_.chGap = d.chGap;
        cfg_.chOutline = d.chOutline; cfg_.chTeamColor = d.chTeamColor; cfg_.chColor = d.chColor;
        cfg_.chHitFlash = d.chHitFlash; cfg_.chHitColor = d.chHitColor;
    }
}

void Game::drawHudSettings(const SetLayout& L) {
    const float w = L.L(kColW), h = L.L(kRowH), hw = L.L(kHalfW), x2 = L.X(kColL + 184);
    // the element: one dropdown (the weapon and grapple models, 3D models and not HUD elements, are its first entries)
    static const int order[] = {Settings::HUD_VIEWMODEL, Settings::HUD_GRAPPLE, Settings::HUD_HEALTH, Settings::HUD_WEAPON, Settings::HUD_BARS,
                                Settings::HUD_SPEED, Settings::HUD_SLOTS, Settings::HUD_KEYS, Settings::HUD_SCORE, Settings::HUD_KILLFEED, Settings::HUD_CHAT};
    static const std::vector<std::string> names = {"WEAPON MODEL", "GRAPPLE MODEL", "HEALTH", "WEAPON NAME", "COOLDOWN BARS", "SPEED METER", "WEAPON LIST",
                                                   "KEY OVERLAY (VERBOSE)", "SCORE BOX", "KILL FEED", "CHAT"};
    static_assert(sizeof(order) / sizeof(order[0]) == Settings::HUD_COUNT, "every HUD element in the dropdown");
    SetFlow f{ui_, L, kColL, 90};
    f.header("ELEMENT");
    const float dropY = f.top();
    f.next();
    auto& e = cfg_.hud[hudSel_];
    const auto& de = defaultSettings().hud[hudSel_];   // (right-click on a slider: the element's default)
    const bool vm = Settings::isModelElem(hudSel_);   // (a 3D model: depth, tilt, an accent colour)
    const UiExp he = UiExp::cfg("hud_*");
    bool shown = !e.hidden;
    ui_.toggle(f.x(), f.top(), w, h, "SHOW", shown, he);
    e.hidden = !shown;
    f.next();
    // (typed numbers: within quark.cfg's ranges)
    ui_.slider(f.x(), f.top(), hw, h, "X OFFSET", e.x, -640, 640, "%.0f", he, de.x); e.x = clampf(std::round(e.x), -1280, 1280);
    ui_.slider(x2, f.top(), hw, h, "Y OFFSET", e.y, -360, 360, "%.0f", he, de.y); e.y = clampf(std::round(e.y), -720, 720);
    f.next();
    ui_.slider(f.x(), f.top(), hw, h, "SIZE", e.scale, 0.5f, 3, "%.2f", he, de.scale); e.scale = clampf(e.scale, 0.5f, 3);
    if (vm) { ui_.slider(x2, f.top(), hw, h, "DEPTH", e.z, -300, 300, "%.0f", he, de.z); e.z = clampf(std::round(e.z), -300, 300); }
    f.next();
    if (vm) {   // the tilt about its pivot, in degrees
        ui_.slider(f.x(), f.top(), hw, h, "PITCH", e.pitch, -90, 90, "%.0f", he, de.pitch); e.pitch = clampf(std::round(e.pitch), -90, 90);
        ui_.slider(x2, f.top(), hw, h, "YAW", e.yaw, -90, 90, "%.0f", he, de.yaw); e.yaw = clampf(std::round(e.yaw), -90, 90);
        f.next();
        ui_.slider(f.x(), f.top(), hw, h, "ROLL", e.roll, -90, 90, "%.0f", he, de.roll); e.roll = clampf(std::round(e.roll), -90, 90);
        f.next();
    }
    // (a RESET with nothing to reset is greyed out)
    auto isDefault = [](const Settings::HudElem& el) {
        Settings::HudElem d;
        return el.x == d.x && el.y == d.y && el.scale == d.scale && el.custom == d.custom && el.hidden == d.hidden && el.z == d.z &&
               el.pitch == d.pitch && el.yaw == d.yaw && el.roll == d.roll && el.color.x == d.color.x && el.color.y == d.color.y && el.color.z == d.color.z;
    };
    if (ui_.button(f.x(), f.top(), w, h, "RESET ELEMENT", he, !isDefault(e))) e = Settings::HudElem{};
    f.next();
    f.section("ELEMENT COLOR");
    ui_.toggle(f.x(), f.top(), w, h, vm ? "CUSTOM ACCENT COLOR" : "CUSTOM COLOR", e.custom, he);
    f.next();
    ui_.colorPicker(f.x(), f.top(), w, h, vm ? "ACCENT COLOR" : "COLOR", e.color, he, de.color);
    f.next();

    SetFlow g{ui_, L, kColR, 90};
    g.header("WHOLE HUD");
    if (ui_.button(g.x(), g.top(), w, h, "EDIT ON SCREEN", UiExp::nav("~settings/hud/edit"))) { hudEditOpen(); return; }
    g.next();
    g.hint("DRAG, SIZE AND STYLE EVERY ELEMENT IN PLACE");
    ui_.toggle(g.x(), g.top(), w, h, "VERBOSE INFO", cfg_.hudVerbose, UiExp::cfg("hud_verbose"));
    g.next();
    ui_.toggle(g.x(), g.top(), w, h, "CUSTOM WIDGETS", cfg_.hudWidgets, UiExp::cfg("hud_widgets"));
    g.next();
    if (ui_.button(g.x(), g.top(), w, h, "WIDGET EDITOR", UiExp::nav("~settings/hud/widgets"))) { widgetEdit_ = true; widgetSel_ = -1; ui_.closePopup(); return; }
    g.next();
    g.hint("DRAG, ADD, IMPORT / EXPORT WIDGETS");
    g.section("PER MODE");
    if (profileRows(ui_, g, true, ruleMode_, cfg_.modeHud, profilesCached("huds", time_))) { cfg_.save(); appliedMode_ = -2; }   // (re-applied now)
    if (ui_.button(L.X(836), L.Y(kHudFootY), hw, h, "RESET WHOLE HUD", he, !std::all_of(std::begin(cfg_.hud), std::end(cfg_.hud), isDefault)))
        for (auto& el : cfg_.hud) el = Settings::HudElem{};
    // the dropdown last: its list opens over the rows under it
    int di = 0;
    for (int k = 0; k < Settings::HUD_COUNT; k++) if (order[k] == hudSel_) di = k;
    if (ui_.dropdown(f.x(), dropY, w, h, "ELEMENT", di, names, UiExp::nav("~settings/hud/elements").withId("HUD ELEMENT"), UiExp::nav("~settings/hud"), UiExp::state("hudsel")))
        hudSel_ = order[di];
}

// FONT (PLAYER > FONT): the UI font (uifonts.h). Built-ins first, then mods/fonts; a click builds that font's atlas once
// and saves it. As many rows as fit above the folder buttons; the wheel scrolls the rest.
void Game::drawFontSettings(const SetLayout& L) {
    if (!fontListed_) { fontList_ = uifonts::scan(); fontListed_ = true; fontScans_++; }
    const float w = L.L(kColW), h = L.L(kRowH), hw = L.L(kHalfW), hs = ui_.hintScale();
    const int n = (int)fontList_.size(), fitRows = 10;
    fontScroll_ = std::min(fontScroll_, std::max(0, n - fitRows));
    const int rows = std::min(n - fontScroll_, fitRows);
    SetFlow f{ui_, L, kColL, 90};
    f.header("UI FONT");
    const std::string current = cfg_.uiFont.empty() ? uifonts::kDefault : cfg_.uiFont;
    for (int k = 0; k < rows; k++) {
        const uifonts::Entry& e = fontList_[fontScroll_ + k];
        const bool on = e.id == current;
        // (its id: the font's name, the same whether it's the one in use or not)
        if (ui_.button(f.x(), f.top(), w, h, on ? "> " + e.name + " <" : e.name, (on ? UiExp::inert() : UiExp::cfg("ui_font")).withId(e.name.c_str())) && !on) {
            std::string err, name;
            if (loadUiFont(e.id, &err, &name)) { cfg_.uiFont = fontTried_ = e.id; cfg_.save(); }
            else notify("FONT " + upper(name) + ": " + upper(err), 5);   // (nothing changes)
            return;   // (fontList_ stays; the next frame draws in the new font)
        }
        if (!e.hint.empty()) ui_.hint(L.X(kColR), f.top() + (h - r_.textHeight(hs)) * 0.5f, e.hint, kHintC, w);
        f.next();
    }
    if (n > rows) f.hint(std::to_string(fontScroll_ + 1) + "-" + std::to_string(fontScroll_ + rows) + " OF " + std::to_string(n) + " - WHEEL TO SCROLL");
    SetFlow b{ui_, L, kColL, 552};
    if (ui_.button(b.x(), b.top(), hw, h, "OPEN FOLDER", UiExp::url())) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(uifonts::modsDir()), ec);
        openUrl(std::filesystem::absolute(std::filesystem::u8path(uifonts::modsDir()), ec).lexically_normal().u8string());
    }
    if (ui_.button(L.X(kColL + 184), b.top(), hw, h, "REFRESH", UiExp::state("fontscan"))) fontListed_ = false;
    b.next();
    b.hint("A VARIABLE FONT IS DRAWN AT ITS DEFAULT WEIGHT", "GAME FONTS: YOUR OWN COPY OF THE REAL FONT IF FOUND");
    if (std::none_of(fontList_.begin(), fontList_.end(), [](const uifonts::Entry& e) { return e.user; })) {
        SetFlow r{ui_, L, kColR, 552};
        r.y += 40;
        r.hint("YOUR OWN: PUT .TTF / .OTF FILES IN MODS/FONTS,", "THEN REFRESH");
    }
}

void Game::drawNotices(float y, float ts, float s) {
    while (!notices_.empty() && notices_.front().until < time_) notices_.pop_front();
    for (auto& n : notices_) { ui_.labelCentered(r_.width() * 0.5f, y, n.text, n.color); y += r_.textHeight(ts) + 6 * s; }
}

// ---- Menus: the main menu and the pages under it ------------------------------------------------------------------
// The redesign's pages (ui-redesign-spec: main, join, host + ADVANCED, vs bots, the pickers, custom mode, info,
// leaderboard, demos, connecting) at its 720p coordinates in a 1280 frame, put on this screen by a SetLayout
// (menuLayout: s = H / 720 around W / 2; narrowed on a squarer screen). The menu column is 360 wide at x 460 in a 392
// panel, rows 40 high and 8 apart; the wide pages fill the 960 x 522 band (y 166..688); BACK bottom-left, the page's
// main action bottom-right; NAME: VALUE on every cycle; hints at text 1.5. A page whose panel is sized to its content
// centres it in the band (bandTop). Nothing here allocates per frame beyond what the widgets did before (static name
// lists; wrapped text comes from Ui::wrap's cache).
namespace {
constexpr float kBandTop = 166, kBandH = 522;
constexpr float kMX = 460, kMW = 360, kMHalf = 176, kMRow = 40;   // the menu column, its half-width buttons, its rows
const Color kBodyC = {0.88f, 0.9f, 0.93f, 1}, kDimC = {0.5f, 0.52f, 0.55f, 1}, kWarnC = {1.0f, 0.55f, 0.3f, 1};
// where content kH design px tall starts when its panel (16 px around it) is centred in the band
float bandTop(float kH) { return kBandTop + std::max(0.0f, (kBandH - (kH + 32)) * 0.5f) + 16; }
}

SetLayout Game::menuLayout(float designW) const {
    const float s = ui_.s(), W = (float)r_.width();
    const float k = std::min(s, W / (designW + 40));   // (a page designW wide keeps 20 px each side; its top stays at y 166)
    return {k, W * 0.5f, kBandTop * (s - k)};
}

// In game (the pause menu and its pages: votes, queues, the live pickers): the band between the notice list (y 80..144)
// and the weapon bar, y 148..638 at 720p; a page's panel (16 px around its content, 95% opaque: the HUD under the
// veil no longer shows through) centred in it, narrowed like the menus' on a squarer screen (around the band's middle)
namespace {
constexpr float kIgTop = 148, kIgH = 490;
}

SetLayout Game::ingameLayout(float designW) const {
    const float s = ui_.s(), W = (float)r_.width();
    const float k = std::min(s, W / (designW + 40));
    return {k, W * 0.5f, (kIgTop + kIgH * 0.5f) * (s - k)};
}

float Game::ingameTop(const SetLayout& L, float kH) const {
    float top = kIgTop + std::max(0.0f, (kIgH - (kH + 32)) * 0.5f) + 16;
    const float under = noticeEnd_ + 4 * ui_.s();   // (more notices than usual: under them all the same)
    if (L.Y(top - 16) < under) top += (under - L.Y(top - 16)) / L.s;
    return top;
}

// The current tab of a tab row (custom mode, info): a 2 px accent frame behind its button
static void tabFrame(Renderer& r, const Ui& ui, float x, float y, float w, float h) {
    const float f = 2 * ui.s();
    r.rect(x - f, y - f, w + 2 * f, h + 2 * f, ui.accent);
}

// --menupage PATH: open that menu page (screenshots), by its Game::uiScreenPath name: main, main/save-copy, join, host,
// host/advanced, host/picker:maps|modes|variants|ladders, vsbots(/picker:...), custom/general|weapons|maps|movement|dash,
// info/modes|controls|hud|hosting|editor|maps (+N: scrolled down N lines), leaderboard(/race-stats), demos, connecting
void Game::debugMenuPage() {
    std::string p = opts_.menuPage;
    int scroll = 0;
    size_t plus = p.find('+');
    if (plus != std::string::npos) { scroll = atoi(p.c_str() + plus + 1); p = p.substr(0, plus); }
    auto under = [&](const char* a) { return p.rfind(a, 0) == 0; };
    if (under("benchmarks")) {   // (benchmarks/result: the newest saved run)
        setScreen(Screen::Benchmarks);
        if (p == "benchmarks/result") {
            std::error_code ec;
            std::string newest;
            for (auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(userDataPath("benchmarks")), ec))
                if (e.path().extension() == ".qbench" && e.path().u8string() > newest) newest = e.path().u8string();
            if (!newest.empty()) benchOpen(newest);
        }
        return;
    }
    auto pickerOf = [&](bool host) {
        size_t c = p.find("/picker:");
        if (c == std::string::npos) return;
        std::string k = p.substr(c + 8);
        picker_ = k == "maps" ? (host ? PICK_HOST_MAP : PICK_BOT_MAP) : k == "modes" ? (host ? PICK_HOST_MODE : PICK_BOT_MODE)
                : k == "variants" ? PICK_VARIANT : k == "ladders" ? PICK_RANKED : PICK_NONE;
        variantCtx_ = host ? 0 : 1;
        if (picker_ == PICK_VARIANT) (host ? hostModeIdx_ : cfg_.botMode) = MODE_ARENA;
    };
    if (p == "main/save-copy") {
        setScreen(Screen::Main);
        edPrompt_ = true; edPromptOfficial_ = true; edPromptThen_ = ED_THEN_NONE;
        edPromptMap_ = Map(); edPromptMap_.name = "harbor"; edPromptName_ = "harbor_copy";
    } else if (p == "join") setScreen(Screen::Join);
    else if (under("host")) { setScreen(Screen::Host); hostAdvanced_ = p == "host/advanced"; pickerOf(true); }
    else if (under("vsbots")) { setScreen(Screen::VsBots); pickerOf(false); }
    else if (under("custom/")) {
        static const char* tabs[] = {"general", "weapons", "maps", "movement", "dash"};
        editRules_ = CustomRules(); editRules_.name = "my mode"; editOrigName_.clear();
        for (int t = 0; t < 5; t++) if (p == std::string("custom/") + tabs[t]) customTab_ = t;
        setScreen(Screen::CustomMode);
    } else if (under("info/")) {
        static const char* info[] = {"info/modes", "info/controls", "info/hud", "info/hosting", "info/editor", "info/maps"};   // (kInfoPaths)
        for (int t = 0; t < 6; t++) if (p == info[t]) infoTab_ = t;
        setScreen(Screen::Info);
        infoScroll_ = scroll;
    } else if (under("leaderboard")) { setScreen(Screen::Leaderboard); raceStatsView_ = p == "leaderboard/race-stats"; }
    else if (p == "demos") setScreen(Screen::Demos);
    else if (p == "connecting") connectTo("127.0.0.1:9");
    else setScreen(Screen::Main);
}

void Game::drawMenu() {
    QZONE("drawMenu");
    if (screen_ == Screen::InGame || screen_ == Screen::Editor) return;
    const float W = (float)r_.width(), H = (float)r_.height();
    float s = H / 720.0f;
    ui_.begin(&r_, uiIn_, s);
    r_.rect(0, 0, W, H, {0.03f, 0.035f, 0.05f, r_.worldLoaded() ? 0.6f : 1.0f});
    drawNotices(12 * s, ui_.textScale(), s); // e.g. "SAVED <mode>": the strip above the title, clear of the buttons
    float cx = W * 0.5f;
    // the title, drawn like the app icon (a baked texture: Renderer::logo); not over the settings shell (it covered it)
    if (screen_ != Screen::Settings) r_.logo(cx, H * 0.12f, 8 * s, rgba(cfg_.colorFriendly), rgba(cfg_.colorEnemy));   // (the Q in both team colours)
    r_.text(8 * s, H - 20 * s, std::max(1.0f, std::round(s)), std::string("V") + QUARK_VERSION, kGrey);

    if (picker_ != PICK_NONE && (screen_ == Screen::Host || screen_ == Screen::VsBots)) { drawPicker(cx, H * 0.12f + 80 * s); return; }
    if (edPrompt_ && screen_ == Screen::Main) { drawEditorSavePrompt(); return; }
    const SetLayout L = menuLayout(kMW + 32);   // (the column pages: 392 wide)
    switch (screen_) {
        case Screen::Main: drawMainMenu(L); break;
        case Screen::Join: drawJoin(menuLayout(592)); break;
        case Screen::Host: if (hostAdvanced_) drawHostAdvanced(L); else drawHost(L); break;
        case Screen::Settings: drawSettings(); break;
        case Screen::VsBots: drawVsBots(L); break;
        case Screen::Leaderboard: drawLeaderboard(menuLayout(960)); break;
        case Screen::Info: drawInfo(menuLayout(960)); break;
        case Screen::Demos: drawDemos(menuLayout(592)); break;
        case Screen::Benchmarks: drawBenchList(menuLayout(592)); break;
        case Screen::BenchmarkResult: drawBenchResults(menuLayout(960)); break;
        case Screen::CustomMode: drawCustomModeEditor(menuLayout(960)); break;
        case Screen::Connecting: drawConnecting(L); break;
        default: break;
    }
}

// The main menu: YOUR NAME, then the menu column (two buttons a row where they pair up), QUIT on its own row last
void Game::drawMainMenu(const SetLayout& L) {
    const float x = L.X(kMX), w = L.L(kMW), hw = L.L(kMHalf), x2 = L.X(kMX + kMW - kMHalf), h = L.L(kMRow);
    ui_.label(x, L.Y(166), "YOUR NAME (CHAT AND SCOREBOARD)", kGrey, 1.0f, w);
    if (ui_.textField(9, x, L.Y(186), w, h, cfg_.name, 20, UiExp::cfg("name").withId("NAME"))) cfg_.save();
    if (ui_.button(x, L.Y(246), hw, h, "PLAY VS BOTS", UiExp::nav("vsbots"))) setScreen(Screen::VsBots);
    if (ui_.button(x2, L.Y(246), hw, h, "TIME TRIAL", UiExp::state("hosting", "1"))) startTimeTrial();
    if (ui_.button(x, L.Y(294), w, h, "JOIN GAME", UiExp::nav("join"))) setScreen(Screen::Join);
    if (ui_.button(x, L.Y(342), w, h, "HOST GAME", UiExp::nav("host"))) setScreen(Screen::Host);
    if (ui_.button(x, L.Y(390), hw, h, "MAP EDITOR", UiExp::nav("editor"))) openEditor("");
    if (ui_.button(x2, L.Y(390), hw, h, "LEADERBOARD", UiExp::nav("leaderboard"))) { nextBoardFetch_ = 0; raceStatsView_ = false; setScreen(Screen::Leaderboard); }
    if (ui_.button(x, L.Y(438), hw, h, "SETTINGS", UiExp::nav("settings"))) { openSettingsTab(TAB_PLAYER); setScreen(Screen::Settings); }
    if (ui_.button(x2, L.Y(438), hw, h, "INFO", UiExp::nav("info/*"))) setScreen(Screen::Info);   // how everything works
    if (ui_.button(x, L.Y(486), hw, h, "DISCORD", UiExp::url())) openUrl(kDiscordUrl);
    if (ui_.button(x2, L.Y(486), hw, h, "DEMOS", UiExp::nav("demos"))) setScreen(Screen::Demos);
    if (ui_.button(x, L.Y(534), w, h, "BENCHMARK", UiExp::nav("benchmarks"))) setScreen(Screen::Benchmarks);
    if (ui_.button(x, L.Y(594), w, h, "QUIT", UiExp::quit())) quit_ = true;   // (destructive: its own row, 20 px below)
    if (!error_.empty()) ui_.labelCentered(L.X(640), L.Y(650), error_, kRed, 1.0f, L.L(960));
}

// JOIN: the address and CONNECT, the server list (column headers, 6 rows of 32, the wheel scrolls the rest), BACK and
// REFRESH LIST in the footer
void Game::drawJoin(const SetLayout& L) {
    const float lx = L.X(360), lw = L.L(560), h = L.L(kMRow), hs = ui_.hintScale();
    ui_.label(lx, L.Y(166), "ADDRESS (IP:PORT)", kGrey, 1.0f, lw);
    ui_.textField(2, lx, L.Y(186), L.L(416), h, joinAddr_, 64, UiExp::local().withId("ADDRESS"));
    if (ui_.button(L.X(784), L.Y(186), L.L(136), h, "CONNECT", UiExp::nav("connecting")) || (uiIn_.enter && !joinAddr_.empty())) connectTo(joinAddr_);
    ui_.label(lx, L.Y(246), "SERVERS (LAN + INTERNET)", ui_.accent, 1.0f, lw);
    ui_.hint(lx, L.Y(266), browser_.internetStatus(), kGrey, lw);
    ui_.panel(lx, L.Y(282), lw, L.L(236), 0.6f);
    // the columns: SERVER, MAP, PLAYERS, PING (design x of each, and how far it may run)
    static const float colX[4] = {372, 622, 752, 852}, colEnd[4] = {612, 744, 844, 908};
    static const char* const heads[4] = {"SERVER", "MAP", "PLAYERS", "PING"};
    for (int c = 0; c < 4; c++) ui_.hint(L.X(colX[c]), L.Y(290), heads[c], kGrey, L.L(colEnd[c] - colX[c]));
    const int kJoinRows = 6;   // rows that fit the list panel; the wheel scrolls the rest
    const int nsv = (int)browser_.servers().size();
    joinScroll_ = std::min(joinScroll_, std::max(0, nsv - kJoinRows));
    const float ts = ui_.textScale();
    for (int k = 0, si = joinScroll_; si < nsv && k < kJoinRows; k++, si++) {
        auto& sv = browser_.servers()[si];
        const float ry = L.Y(306 + 36 * k), rh = L.L(32), ty = ry + (rh - r_.textHeight(ts)) * 0.5f;
        if (ui_.button(L.X(366), ry, L.L(548), rh, "", UiExp::nav("connecting").withId("SERVER").optional())) {
            joinAddr_ = sv.addr.toString();
            connectTo(joinAddr_);
        }
        if (sv.noReply) r_.text(L.X(colX[1]), ty, ts, r_.textClip("NO RESPONSE - CLICK TO TRY", ts, L.L(colEnd[3] - colX[1])), kGrey);
        else {
            char b[96];
            snprintf(b, sizeof b, "%s/%s", sv.map.c_str(), modeName(sv.mode));
            r_.text(L.X(colX[1]), ty, ts, r_.textClip(upper(b), ts, L.L(colEnd[1] - colX[1])), kWhite);
            snprintf(b, sizeof b, "%d/%d", sv.players, sv.maxPlayers);
            r_.text(L.X(colX[2]), ty, ts, b, kWhite);
            snprintf(b, sizeof b, "%dMS", (int)sv.ping);
            r_.text(L.X(colX[3]), ty, ts, b, kWhite);
        }
        // the name: a long one scrolls back and forth so all of it can be read
        std::string name = upper(sv.name);
        const float avail = L.L(colEnd[0] - colX[0]);
        int extra = 0;   // leading characters to drop before the rest fits (measured: W is wider than M)
        while (extra < (int)name.size() && r_.textWidth(name.substr(extra), ts) > avail) extra++;
        if (extra > 0) { // ping-pong: pause 1.5 s at each end, scroll 6 characters a second
            double cycle = 2 * (extra / 6.0 + 1.5), t = std::fmod(time_, cycle);
            double pos = t < 1.5 ? 0 : t < 1.5 + extra / 6.0 ? (t - 1.5) * 6 : t < 3 + extra / 6.0 ? extra : extra - (t - 3 - extra / 6.0) * 6;
            name = name.substr((size_t)std::clamp((int)pos, 0, extra));
            name.resize(r_.textFit(name, ts, avail));
        }
        r_.text(L.X(colX[0]), ty, ts, name, kWhite);
    }
    if (browser_.servers().empty()) ui_.label(L.X(372), L.Y(310), "NO GAMES FOUND", kGrey, 1.0f, L.L(536));
    else if (nsv > kJoinRows) {
        char more[64];
        snprintf(more, sizeof more, "%d-%d OF %d - WHEEL TO SCROLL", joinScroll_ + 1, std::min(nsv, joinScroll_ + kJoinRows), nsv);
        ui_.hint(lx + lw - r_.textWidth(more, hs), L.Y(522), more, kGrey, lw);
    }
    if (ui_.button(lx, L.Y(538), L.L(276), h, "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
    if (ui_.button(L.X(644), L.Y(538), L.L(276), h, "REFRESH LIST", UiExp::state("browser"))) browser_.refresh();
    if (!error_.empty()) ui_.labelCentered(L.X(640), L.Y(594), error_, kRed, 1.0f, lw);
}

// HOST: the server's name, map, custom mode, mode (TEAMS under it in duel), max players, bots / public, then
// ADVANCED > (the network options); BACK and START in the footer
void Game::drawHost(const SetLayout& L) {
    const float x = L.X(kMX), w = L.L(kMW), h = L.L(kMRow), hw = L.L(kMHalf), x2 = L.X(kMX + kMW - kMHalf);
    const bool haveMaps = !maps_.empty();
    // what the rows show, worked out before the panel (its height depends on the row under MODE)
    static std::vector<std::string> saved;   // (custom modes re-listed twice a second at most: not a folder scan every frame)
    static double savedAt = -1;
    if (time_ >= savedAt || time_ < savedAt - 1) { saved = listCustomModes(); savedAt = time_ + 0.5; }
    static CustomRules cur;   // the chosen custom mode, re-read twice a second at most (not a file read every frame)
    static std::string curName;
    static double curAt = -1;
    static bool curOk = false;
    if (curName != cfg_.hostCustom || time_ >= curAt) {
        curName = cfg_.hostCustom; curAt = time_ + 0.5;
        cur = CustomRules();
        curOk = !curName.empty() && loadCustomMode(curName, cur);
    }
    if (haveMaps && !cfg_.hostRanked && !cfg_.hostCustom.empty() && curOk) {
        hostModeIdx_ = cur.baseMode;   // the mode comes from the custom mode
        if (!cur.maps.empty() && std::find(cur.maps.begin(), cur.maps.end(), maps_[hostMapIdx_ % maps_.size()]) == cur.maps.end())
            for (size_t k = 0; k < maps_.size(); k++) if (maps_[k] == cur.maps[0]) hostMapIdx_ = (int)k;
    }
    if (haveMaps && cfg_.hostRanked) { // ranked is always 1v1 on a ladder: the mode button picks the ladder instead
        if (!arenaVariantRanked(cfg_.hostArenaVariant)) cfg_.hostArenaVariant = 0;
        hostModeIdx_ = rankedMode(cfg_.hostArenaVariant);
        if (hostMapIdx_ >= (int)mapInfo_.size() || !(mapInfo_[hostMapIdx_].ranked >> cfg_.hostArenaVariant & 1))
            hostMapIdx_ = bestRankedMap(cfg_.hostArenaVariant);
    }
    const bool built = hostMapIdx_ < (int)mapInfo_.size() && (mapInfo_[hostMapIdx_].modes >> hostModeIdx_ & 1); // (no map file read every frame)
    // the row under MODE: 1 TEAMS (duel), 2 the map isn't built for the mode, 3 ranked's notes; 0 none
    const int extra = !haveMaps ? 0 : cfg_.hostRanked ? 3 : !built ? 2 : hostModeIdx_ == MODE_DUEL ? 1 : 0;
    const float extraH = extra == 1 ? 48 : extra == 2 ? 24 : extra == 3 ? 34 : 0;
    const float kH = 20 + 48 * 4 + extraH + 48 + 44 + 20 + 60 + 40;   // SERVER NAME .. START / BACK
    float y = bandTop(kH);
    ui_.panel(L.X(kMX - 16), L.Y(y - 16), L.L(kMW + 32), L.L(kH + 32));
    ui_.label(x, L.Y(y), "SERVER NAME", kGrey, 1.0f, w);
    y += 20;
    ui_.textField(3, x, L.Y(y), w, h, cfg_.hostName, 32, UiExp::cfg("host_name").withId("SERVER NAME"));
    y += 48;
    if (haveMaps) {
        if (ui_.button(x, L.Y(y), w, h, "MAP: " + upper(maps_[hostMapIdx_ % maps_.size()]) + "  >", UiExp::nav("host/picker:maps").withId("MAP"))) picker_ = PICK_HOST_MAP;
        y += 48;
        // custom mode: click to go through your saved ones (or none); EDIT / NEW open the builder
        const float cw = L.L(200), ew = L.L(72);
        if (cfg_.hostRanked) ui_.button(x, L.Y(y), cw, h, "CUSTOM MODE: OFF (RANKED)", UiExp::inert().withId("CUSTOM MODE"), false);
        else if (ui_.button(x, L.Y(y), cw, h, "CUSTOM MODE: " + (cfg_.hostCustom.empty() ? std::string("NONE") : upper(cfg_.hostCustom)) + "  >",
                            UiExp::cfg("host_custom").withId("CUSTOM MODE"), !saved.empty() || !cfg_.hostCustom.empty())) {
            auto list = listCustomModes();
            auto it = std::find(list.begin(), list.end(), cfg_.hostCustom);
            cfg_.hostCustom = list.empty() ? "" : it == list.end() ? list[0] : it + 1 == list.end() ? "" : *(it + 1);
        }
        if (ui_.button(L.X(kMX + 208), L.Y(y), ew, h, "EDIT", UiExp::nav("custom/general"), !cfg_.hostCustom.empty())) {
            editRules_ = CustomRules();
            if (loadCustomMode(cfg_.hostCustom, editRules_)) { editOrigName_ = editRules_.name; customTab_ = 0; setScreen(Screen::CustomMode); }
        }
        if (ui_.button(L.X(kMX + 288), L.Y(y), ew, h, "NEW", UiExp::nav("custom/general"))) {
            editRules_ = CustomRules(); editRules_.name = "my mode"; editRules_.baseMode = MODE_FFA;
            editOrigName_.clear(); customTab_ = 0; setScreen(Screen::CustomMode);
        }
        y += 48;
        if (cfg_.hostRanked) {
            static const int ladders[3] = {0, 1, 5};
            int k = cfg_.hostArenaVariant == 1 ? 1 : cfg_.hostArenaVariant == 5 ? 2 : 0;
            if (ui_.button(x, L.Y(y), w, h, std::string("MODE: ") + rankedKind(ladders[k]) + " - " + arenaVariant(ladders[k]).title + "  >", UiExp::nav("host/picker:modes").withId("MODE"))) picker_ = PICK_HOST_MODE;
        } else if (ui_.button(x, L.Y(y), w, h, std::string("MODE: ") + modeTitle(hostModeIdx_) + (hostModeIdx_ == MODE_ARENA || hostModeIdx_ == MODE_TOURNEY ? std::string(" - ") + arenaVariant(cfg_.hostArenaVariant).title : "") + "  >",
                              UiExp::nav("host/picker:modes").withId("MODE"))) picker_ = PICK_HOST_MODE;
        y += 48;
        if (extra == 1) {
            static const std::vector<std::string> teams = {"TEAMS: 1V1", "TEAMS: 2V2"};
            int t = cfg_.hostDuelSize == 2 ? 1 : 0;
            if (ui_.cycle(x, L.Y(y), w, h, "TEAMS", t, 2, teams, UiExp::cfg("host_duel_size").withId("TEAMS"))) cfg_.hostDuelSize = t == 1 ? 2 : 1;
        } else if (extra == 2) ui_.label(x, L.Y(y), "NOT BUILT FOR THIS MODE - IT MAY NOT WORK", kRed, 1.0f, w);
        else if (extra == 3) {
            if (testingBuild())   // (a testing build hosts ranked unrated: Server::rated)
                ui_.hint(x, L.Y(y), "TESTING BUILD - RANKED NOT RATED (PLAYED, NEVER COUNTED)", {1.0f, 0.78f, 0.25f, 1}, w);
            else ui_.hint(x, L.Y(y), "RANKED MAPS ONLY - EACH LADDER HAS ITS OWN RATING", kGrey, w);
            ui_.hint(x, L.Y(y + 14), "FAIRNESS: DEFAULT MODEL, HUD, CONTROLS, 10 MS INTERP", kGrey, w);
        }
        y += extraH;
    } else y += 48 * 3;
    float mp = (float)cfg_.hostMaxPlayers;
    if (ui_.slider(x, L.Y(y), w, h, "MAX PLAYERS", mp, 2, (float)kMaxPlayers, "%.0f", UiExp::cfg("host_max"), (float)defaultSettings().hostMaxPlayers))
        cfg_.hostMaxPlayers = std::min(std::max((int)std::round(mp), 2), kMaxPlayers);
    y += 48;
    if (cfg_.hostRanked) ui_.button(x, L.Y(y), hw, h, "BOTS: OFF (RANKED)", UiExp::inert().withId("BALANCE BOTS"), false);
    else ui_.toggle(x, L.Y(y), hw, h, "BALANCE BOTS", cfg_.hostBalanceBots, UiExp::cfg("host_balance_bots"));
    ui_.toggle(x2, L.Y(y), hw, h, "PUBLIC", cfg_.hostPublic, UiExp::cfg("host_public"));
    y += 44;
    ui_.hint(x, L.Y(y), "FRIENDS JOIN WITH YOUR PUBLIC IP:" + hostPortStr_, kGrey, w);
    y += 20;
    if (ui_.button(x, L.Y(y), w, h, "ADVANCED  >", UiExp::nav("host/advanced"))) { hostAdvanced_ = true; refreshHostBrains(); }
    y += 60;
    if (ui_.button(x, L.Y(y), hw, h, "BACK", UiExp::nav("main"))) { cfg_.save(); setScreen(Screen::Main); }
    if (ui_.button(x2, L.Y(y), hw, h, "START", UiExp::state("hosting", "1"))) startHost();
    if (!error_.empty()) ui_.labelCentered(L.X(640), L.Y(y + 64), error_, kRed, 1.0f, L.L(960));
}

// HOST > ADVANCED: the network options most hosts never touch (UDP port, UPnP, the server log); BACK: HOST
void Game::drawHostAdvanced(const SetLayout& L) {
    const float x = L.X(kMX), w = L.L(kMW), h = L.L(kMRow);
    const float kH = 40 + 20 + 44 + 20 + 44 + 20 + 44 + 44 + 36 + 40;
    float y = bandTop(kH);
    ui_.panel(L.X(kMX - 16), L.Y(y - 16), L.L(kMW + 32), L.L(kH + 32));
    ui_.labelCentered(L.X(640), L.Y(y), "ADVANCED", kWhite, 1.5f, w);
    y += 40;
    ui_.label(x, L.Y(y), "UDP PORT", kGrey, 1.0f, w);
    y += 20;
    ui_.textField(4, x, L.Y(y), w, h, hostPortStr_, 5, UiExp::local().withId("UDP PORT"));
    y += 44;
    ui_.hint(x, L.Y(y), "IN GAME, ESC SHOWS IF THE PORT CAN BE REACHED", kGrey, w);
    y += 20;
    ui_.toggle(x, L.Y(y), w, h, "AUTO PORT FORWARD", cfg_.hostUpnp, UiExp::cfg("host_upnp"));
    y += 44;
    ui_.hint(x, L.Y(y), "OPENS THE UDP PORT ON YOUR ROUTER (UPNP)", kGrey, w);
    y += 20;
    static const std::vector<std::string> logNames = {"SERVER LOG: OFF", "SERVER LOG: SIMPLE", "SERVER LOG: VERBOSE (125 HZ)", "SERVER LOG: EXTREME (EVERY TICK)"};
    ui_.cycle(x, L.Y(y), w, h, "SERVER LOG", cfg_.hostLog, 4, logNames, UiExp::cfg("host_log"));
    y += 44;
    // the brain bots play (bots/*.qbrain, tools/bots/GRAMMAR.md): listed when HOST opens; ranked has no bots
    if (cfg_.hostRanked || hostBrains_.empty())
        ui_.button(x, L.Y(y), w, h, cfg_.hostRanked ? "BOT BRAIN: OFF (RANKED)" : "BOT BRAIN: BUILT-IN", UiExp::inert().withId("BOT BRAIN"), false);
    else {
        std::vector<std::string> names = {"BOT BRAIN: BUILT-IN"};
        for (auto& n : hostBrains_) names.push_back("BOT BRAIN: " + upper(n));
        auto it = std::find(hostBrains_.begin(), hostBrains_.end(), cfg_.hostBotBrain);
        int k = it == hostBrains_.end() ? 0 : (int)(it - hostBrains_.begin()) + 1;
        if (ui_.cycle(x, L.Y(y), w, h, "BOT BRAIN", k, (int)names.size(), names, UiExp::cfg("host_bot_brain"))) {
            cfg_.hostBotBrain = k > 0 ? hostBrains_[k - 1] : "";
            checkHostBrain();
        }
    }
    y += 44;
    if (!cfg_.hostRanked && !hostBrainNote_.empty()) ui_.hint(x, L.Y(y), hostBrainNote_, kRed, w);
    else ui_.hint(x, L.Y(y), cfg_.hostRanked ? "RANKED GAMES HAVE NO BOTS" : hostBrains_.empty() ? "PUT .QBRAIN FILES IN THE BOTS FOLDER TO PICK ONE"
                                                : "WHAT THE BOTS PLAY: .QBRAIN FILES IN THE BOTS FOLDER", kGrey, w);
    y += 36;
    if (ui_.button(x, L.Y(y), L.L(kMHalf), h, "BACK", UiExp::nav("host"))) { hostAdvanced_ = false; cfg_.save(); }
}

// VS BOTS: mode, map, difficulty, how many bots (and teammates in a team mode); BACK and START
void Game::drawVsBots(const SetLayout& L) {
    const float x = L.X(kMX), w = L.L(kMW), h = L.L(kMRow), hw = L.L(kMHalf), x2 = L.X(kMX + kMW - kMHalf);
    const bool haveMaps = !maps_.empty(), teams = modeIsTeamBased(cfg_.botMode);
    const int rows = 3 + (haveMaps ? 1 : 0) + (teams ? 1 : 0);
    const float kH = 48 * rows + 52;   // the rows, 20 px, BACK / START
    float y = bandTop(kH);
    ui_.panel(L.X(kMX - 16), L.Y(y - 16), L.L(kMW + 32), L.L(kH + 32));
    if (ui_.button(x, L.Y(y), w, h, std::string("MODE: ") + modeTitle(cfg_.botMode) + (cfg_.botMode == MODE_ARENA ? std::string(" - ") + arenaVariant(cfg_.botArenaVariant).title : "") + "  >",
                   UiExp::nav("vsbots/picker:modes").withId("MODE"))) picker_ = PICK_BOT_MODE;
    y += 48;
    if (haveMaps) {
        // only offer maps that support the chosen mode
        for (size_t k = 0; k < maps_.size(); k++) {   // (from the scan, not a map file read every frame)
            size_t i = botMapIdx_ % maps_.size();
            if (i < mapInfo_.size() && (mapInfo_[i].modes >> cfg_.botMode & 1)) break;
            botMapIdx_ = (botMapIdx_ + 1) % (int)maps_.size();
        }
        if (ui_.button(x, L.Y(y), w, h, "MAP: " + upper(maps_[botMapIdx_ % maps_.size()]) + "  >", UiExp::nav("vsbots/picker:maps").withId("MAP"))) picker_ = PICK_BOT_MAP;
        y += 48;
    }
    static const std::vector<std::string> skills = {"DIFFICULTY: BEGINNER", "DIFFICULTY: INTERMEDIATE", "DIFFICULTY: ADVANCED", "DIFFICULTY: EXPERT"};
    ui_.cycle(x, L.Y(y), w, h, "DIFFICULTY", cfg_.botSkill, 4, skills, UiExp::cfg("bot_skill"));
    y += 48;
    float ne = (float)cfg_.botEnemies, na = (float)cfg_.botAllies;
    if (ui_.slider(x, L.Y(y), w, h, teams ? "ENEMY BOTS" : "BOTS", ne, 1, 7, "%.0f", UiExp::cfg("bot_enemies"), (float)defaultSettings().botEnemies)) cfg_.botEnemies = std::min(std::max((int)std::round(ne), 1), 7);
    y += 48;
    if (teams) {
        if (ui_.slider(x, L.Y(y), w, h, "TEAMMATE BOTS", na, 0, 3, "%.0f", UiExp::cfg("bot_allies"), (float)defaultSettings().botAllies)) cfg_.botAllies = std::min(std::max((int)std::round(na), 0), 3);
        y += 48;
    }
    y += 12;   // (20 px over the footer)
    if (ui_.button(x, L.Y(y), hw, h, "BACK", UiExp::nav("main"))) { cfg_.save(); setScreen(Screen::Main); }
    if (ui_.button(x2, L.Y(y), hw, h, "START", UiExp::state("hosting", "1"))) startHost(true);
    if (!error_.empty()) ui_.labelCentered(L.X(640), L.Y(y + 64), error_, kRed, 1.0f, L.L(960));
}

// CONNECTING: the status line and CANCEL in a small panel in the middle of the band
void Game::drawConnecting(const SetLayout& L) {
    const float kH = 16 + 20 + 40;
    float y = bandTop(kH);
    ui_.panel(L.X(kMX - 16), L.Y(y - 16), L.L(kMW + 32), L.L(kH + 32));
    float dlp = net_.downloadProgress();
    if (dlp >= 0) ui_.labelCentered(L.X(640), L.Y(y), "DOWNLOADING MAP " + upper(net_.downloadName()) + " - " + std::to_string((int)(dlp * 100)) + "%", kWhite, 1.0f, L.L(kMW));
    else ui_.labelCentered(L.X(640), L.Y(y), "CONNECTING TO " + net_.address() + "...", kWhite, 1.0f, L.L(kMW));
    y += 36;
    if (ui_.button(L.X(kMX), L.Y(y), L.L(kMW), L.L(kMRow), "CANCEL", UiExp::nav("main"))) leaveGame();
}

// A custom mode's setting is stored as a multiplier of the standard value; its slider shows the value it makes in this
// mode (standard x multiplier, with its unit: "GRAVITY: 22.0 M/S2"). The standard's own decimals (3 significant
// figures). A typed value is kept as typed (as standard x multiplier); a dragged one snaps to `snap` steps of the
// multiplier. A standard of 0 (nothing to scale from): the multiplier itself.
static bool realSlider(Ui& ui, float x, float y, float w, float h, const char* label, float& mult, float lo, float hi,
                       float stdv, const char* unit, const UiExp& e, float defMult, float snap = 0) {
    if (!(stdv > 0)) {
        float m = mult;
        bool ch = ui.slider(x, y, w, h, label, m, lo, hi, "%.2fX", e, defMult);
        if (ch) mult = snap > 0 && !ui.typed() ? std::round(m / snap) * snap : m;
        return ch;
    }
    char fmt[24], u[16] = {0};
    const int dec = stdv >= 99.95f ? 0 : stdv >= 9.995f ? 1 : stdv >= 0.0995f ? 2 : 3;
    {   // the unit: "X" right after the number, "/S" too, others after a space; upper case
        size_t n = 0;
        if (*unit && unit[0] != '/' && strcmp(unit, "x") != 0) u[n++] = ' ';
        for (const char* c = unit; *c && n + 1 < sizeof u; c++) u[n++] = (char)toupper((unsigned char)*c);
        u[n] = 0;
    }
    snprintf(fmt, sizeof fmt, "%%.%df%s", dec, u);
    float v = mult * stdv;
    bool ch = ui.slider(x, y, w, h, label, v, lo * stdv, hi * stdv, fmt, e, defMult * stdv);
    if (ch) {
        float m = v / stdv;
        mult = snap > 0 && !ui.typed() ? std::round(m / snap) * snap : m;
    }
    return ch;
}

// CUSTOM MODE: one 960 x 522 panel on every tab - the title and NAME, the tab row (GENERAL / WEAPONS / MAPS /
// MOVEMENT / DASH + GRAPPLE, the current one framed), the tab's columns with section headers, and the footer: BACK
// (left), RESET MOVEMENT TO STANDARD (the movement tabs), DELETE, SAVE (right)
void Game::drawCustomModeEditor(const SetLayout& L) {
    CustomRules& r = editRules_;
    const UiExp ce = UiExp::state("custom");
    static const CustomRules kStd;   // (right-click on a slider: the standard value)
    ui_.panel(L.X(160), L.Y(166), L.L(960), L.L(522), 0.85f);
    ui_.label(L.X(176), L.Y(186), "CUSTOM MODE", kWhite, 1.5f, L.L(380));
    {
        const float nw = r_.textWidth("NAME", ui_.textScale());
        ui_.label(L.X(628) - nw, L.Y(190), "NAME", kGrey);
    }
    ui_.textField(901, L.X(640), L.Y(182), L.L(464), L.L(32), r.name, 24, UiExp::state("custom").withId("NAME"));
    static const char* tabs[5] = {"GENERAL", "WEAPONS", "MAPS", "MOVEMENT", "DASH + GRAPPLE"};
    static const char* tabPaths[5] = {"custom/general", "custom/weapons", "custom/maps", "custom/movement", "custom/dash"};
    const float tw = (928 - 4 * 8) / 5.0f;   // 179.2
    customTab_ = std::min(std::max(customTab_, 0), 4);
    for (int t = 0; t < 5; t++) {
        const float bx = L.X(176 + t * (tw + 8)), by = L.Y(222);
        if (t == customTab_) tabFrame(r_, ui_, bx, by, L.L(tw), L.L(32));
        if (ui_.button(bx, by, L.L(tw), L.L(32), tabs[t], UiExp::nav(tabPaths[t]).withId(tabs[t]))) customTab_ = t;
    }
    auto header = [&](float dx, float dy, const char* t, float dw) { ui_.label(L.X(dx), L.Y(dy), t, ui_.accent, 1.0f, L.L(dw)); };
    // a column of rows: design x, the row's design y
    const float c3 = 298.67f, cx3[3] = {176, 490.67f, 805.33f};
    if (customTab_ == 0) {
        header(cx3[0], 270, "RULES", c3);
        header(cx3[1], 270, "PHYSICS", c3);
        header(cx3[2], 270, "ABILITIES", c3);
        const float cw = L.L(c3), h = L.L(32);
        auto at = [&](int c, int row) { return std::make_pair(L.X(cx3[c]), L.Y(294 + 40 * row)); };
        static std::vector<std::string> modes;   // (built once: 13 strings every frame)
        if (modes.empty()) for (int m = 0; m < MODE_COUNT; m++) modes.push_back(std::string("PLAYS AS: ") + upper(modeName(m)));
        auto p = at(0, 0);
        const int before = r.baseMode;
        ui_.cycle(p.first, p.second, cw, h, "PLAYS AS", r.baseMode, MODE_COUNT, modes, ce);
        // tourney and ranked arena keep their own rules: stepped over, onwards in the direction it went (going ">" it
        // was put back to FFA, so TOURNEY's neighbours on, hide and seek, tag and points, couldn't be reached that way)
        const int step = r.baseMode == (before + 1) % MODE_COUNT ? 1 : MODE_COUNT - 1;
        for (int k = 0; k < MODE_COUNT && (r.baseMode == MODE_TOURNEY || r.baseMode == MODE_ARENA); k++) r.baseMode = (r.baseMode + step) % MODE_COUNT;
        // (a typed value is kept as typed, within the custom mode file's ranges - CustomRules::fromText)
        float hp = (float)r.health;
        p = at(0, 1);
        ui_.slider(p.first, p.second, cw, h, "HEALTH", hp, 1, 500, "%.0f", ce, (float)kStd.health); r.health = std::min(std::max((int)std::lround(hp), 1), 1000);
        float tl = (float)r.timeLimit, sl = (float)r.scoreLimit, rw = (float)r.roundsToWin;
        p = at(0, 2);   // (dragged: half minutes; typed: any whole second)
        if (ui_.slider(p.first, p.second, cw, h, "TIME LIMIT (S)", tl, 0, 3600, "%.0f", ce, (float)kStd.timeLimit)) r.timeLimit = std::min(std::max((int)std::lround(ui_.typed() ? tl : std::round(tl / 30) * 30), 0), 3600);
        p = at(0, 3);
        ui_.slider(p.first, p.second, cw, h, "SCORE LIMIT", sl, 0, 200, "%.0f", ce, (float)kStd.scoreLimit); r.scoreLimit = std::min(std::max((int)std::lround(sl), 0), 10000);
        p = at(0, 4);
        ui_.slider(p.first, p.second, cw, h, "ROUNDS", rw, 0, 15, "%.0f", ce, (float)kStd.roundsToWin); r.roundsToWin = std::min(std::max((int)std::lround(rw), 0), 50);
        ui_.hint(L.X(cx3[0]), L.Y(490), "TIME / SCORE / ROUNDS: 0 = THE MODE'S DEFAULT", kGrey, cw);
        if (!r.logic.empty()) {   // a mode editor file with logic: the label a server will play it as (name#hash8), under the hint
            // (re-made four times a second at most, compiled only when the text changed: the hash covers the keys too)
            static std::string text, label;
            static bool ok = false;
            static double when = -1;
            if (time_ >= when || time_ < when - 1) {
                when = time_ + 0.25;
                CustomRules c = r;
                c.label.clear();
                std::string t = c.toText();
                if (t != text) {
                    text.swap(t);
                    std::string err;
                    auto lg = compileCustomLogic(r, &err);
                    ok = lg != nullptr;
                    label = ok ? modeLabel(*lg) : err;
                    if (label.size() > 60) label.resize(60);
                }
            }
            ui_.label(L.X(cx3[0]), L.Y(514), ok ? "LOGIC: " + label : upper(label), ok ? kGrey : kRed, 0.5f);
        }
        p = at(1, 0);
        realSlider(ui_, p.first, p.second, cw, h, "GRAVITY", r.gravity, 0.1f, 3, moveBaseGravity(), "m/s2", ce.withId("GRAVITY"), kStd.gravity);
        p = at(1, 1);
        realSlider(ui_, p.first, p.second, cw, h, "RUN SPEED", r.moveSpeed, 0.25f, 3, moveBaseSpeed(), "m/s", ce.withId("RUN SPEED"), kStd.moveSpeed);
        p = at(1, 2);
        realSlider(ui_, p.first, p.second, cw, h, "JUMP", r.jump, 0.25f, 3, moveBaseJump(), "m/s", ce.withId("JUMP"), kStd.jump);
        p = at(1, 3);
        ui_.slider(p.first, p.second, cw, h, "KNOCKBACK", r.knockback, 0, 5, "%.2fX", ce, kStd.knockback);
        r.gravity = clampf(r.gravity, 0.1f, 3); r.moveSpeed = clampf(r.moveSpeed, 0.25f, 3); r.jump = clampf(r.jump, 0.25f, 3); r.knockback = clampf(r.knockback, 0, 5);
        struct { const char* name; bool* v; } abil[7] = {{"DASH", &r.dash}, {"GRAPPLE", &r.grapple}, {"ROCKET RIDE", &r.rocketRide},
                                                         {"SELF DAMAGE", &r.selfDamage}, {"INSTAGIB", &r.instagib}, {"VAMPIRE", &r.vampire},
                                                         {"REGEN", &r.regen}};
        for (int i = 0; i < 7; i++) { p = at(2, i); ui_.toggle(p.first, p.second, cw, h, abil[i].name, *abil[i].v, ce); }
    } else if (customTab_ == 1) {   // a row per weapon: on / off, then FIRE RATE, DAMAGE, SPEED as this mode makes them
        static const CustomRules kStdW;
        int row = 0;
        for (int i = 0; i < WPN_COUNT; i++) {
            int wp = kWeaponSlots[i];
            if (wp == WPN_LOCATOR) continue;   // (hide and seek / tag only, always "it"'s: not a pick)
            const float y = L.Y(270 + 34 * row++), h = L.L(30), sw = L.L(248);
            bool on = r.weapons == 0 || (r.weapons >> wp & 1);
            bool onlyOne = r.weapons == (1u << wp);   // (the last one on stays on)
            if (ui_.toggle(L.X(176), y, L.L(160), h, upper(baseWeaponDef(wp).name), on, onlyOne ? UiExp::inert() : ce)) {
                uint16_t all = (uint16_t)((1u << WPN_COUNT) - 1), m = r.weapons ? r.weapons : all;
                m = on ? (uint16_t)(m | 1u << wp) : (uint16_t)(m & ~(1u << wp));
                if (m == 0) m = (uint16_t)(1u << wp);   // keep at least one
                r.weapons = m == all ? 0 : m;
            }
            const WeaponDef& bd = baseWeaponDef(wp);   // standard x this mode's multiplier
            realSlider(ui_, L.X(344), y, sw, h, "FIRE RATE", r.fireRate[wp], 0.25f, 4, bd.refire > 0 ? 1.0f / bd.refire : 0, "/s", ce.withId("FIRE RATE"), kStdW.fireRate[wp]);
            realSlider(ui_, L.X(600), y, sw, h, "DAMAGE", r.damage[wp], 0.1f, 5, bd.damage, "", ce.withId("DAMAGE"), kStdW.damage[wp]);
            if (bd.projSpeed > 0)
                realSlider(ui_, L.X(856), y, sw, h, "SPEED", r.projSpeed[wp], 0.25f, 4, bd.projSpeed, "m/s", ce.withId("SPEED"), kStdW.projSpeed[wp]);
            else ui_.button(L.X(856), y, sw, h, "SPEED: HITSCAN", UiExp::inert().withId("SPEED"), false);   // (no projectile: nothing to set)
            r.fireRate[wp] = clampf(r.fireRate[wp], 0.1f, 10); r.damage[wp] = clampf(r.damage[wp], 0, 10); r.projSpeed[wp] = clampf(r.projSpeed[wp], 0.1f, 5);
        }
    } else if (customTab_ == 2) {   // the map pool: every map built for the mode, five to a row
        ui_.label(L.X(176), L.Y(270), "MAP POOL (ROTATES EACH MATCH; NONE = THE HOST'S MAP)", kGrey, 1.0f, L.L(928));
        int n = 0;
        for (size_t i = 0; i < maps_.size() && i < mapInfo_.size(); i++) if (mapInfo_[i].modes >> r.baseMode & 1) n++;
        // rows of 40 down to the footer; with many of your own maps, lower rows instead of off the bottom
        const int rows = std::max(1, (n + 4) / 5);
        const float step = std::min(40.0f, 322.0f / rows), rh = std::max(18.0f, std::min(32.0f, step - 8));
        int k = 0;
        for (size_t i = 0; i < maps_.size() && i < mapInfo_.size(); i++) {
            if (!(mapInfo_[i].modes >> r.baseMode & 1)) continue;
            auto it = std::find(r.maps.begin(), r.maps.end(), maps_[i]);
            bool on = it != r.maps.end();
            if (ui_.toggle(L.X(176 + (k % 5) * (tw + 8)), L.Y(294 + (k / 5) * step), L.L(tw), L.L(rh), upper(maps_[i]), on, ce)) {
                if (on) r.maps.push_back(maps_[i]); else r.maps.erase(it);
            }
            k++;
        }
        // drop maps that no longer support the mode
        r.maps.erase(std::remove_if(r.maps.begin(), r.maps.end(), [&](const std::string& nm) {
            for (size_t i = 0; i < maps_.size() && i < mapInfo_.size(); i++) if (maps_[i] == nm) return !(mapInfo_[i].modes >> r.baseMode & 1);
            return true; }), r.maps.end());
    } else {   // MOVEMENT / DASH + GRAPPLE: every movement constant as this mode makes it (gravity / speed / jump: GENERAL)
        ui_.hint(L.X(176), L.Y(270), "EACH SLIDER SHOWS THE VALUE IN THIS MODE (STANDARD x MULTIPLIER)", kGrey, L.L(928));
        struct Col { float x, w; const char* head; int n; int p[8]; float headY = 294; };
        static const Col move[3] = {
            {176, 452, "GROUND", 5, {MV_CROUCH_SPEED, MV_GROUND_ACCEL, MV_FRICTION, MV_STOP_SPEED, MV_STEP}},
            {176, 452, "LADDER", 2, {MV_LADDER_SPEED, MV_LADDER_SIDE}, 486},
            {652, 452, "AIR", 8, {MV_AIR_ACCEL, MV_AIR_CAP, MV_OVERSTRAFE, MV_AIR_CONTROL, MV_AIR_DRIFT, MV_STRAFE_BOOST, MV_BOUNCE_GAIN, MV_BOUNCE_MAX}}};
        static const Col dash[3] = {
            {176, 298.67f, "DASH", 8, {MV_DASH_SPEED, MV_DASH_TIME, MV_DASH_EXIT_MIN, MV_DASH_EXIT_MAX, MV_DASH_KEEP, MV_DASH_VERTICAL, MV_DASH_CHARGES, MV_DASH_COOLDOWN}},
            {490.67f, 298.67f, "SUPER DASH", 5, {MV_DASH_JUMP_WINDOW, MV_SUPER_DASH_BOOST, MV_SUPER_DASH_JUMP, MV_SUPER_DASH_MAX, MV_DASH_BOUNCE_ANGLE}},
            {805.33f, 298.67f, "GRAPPLE", 6, {MV_HOOK_RANGE, MV_HOOK_ACCEL, MV_HOOK_MAX, MV_HOOK_GRAVITY, MV_HOOK_RELEASE, MV_HOOK_COOLDOWN}}};
        static const CustomRules kStdM;
        for (const Col& c : customTab_ == 3 ? move : dash) {
            header(c.x, c.headY, c.head, c.w);
            for (int i = 0; i < c.n; i++) {
                const int k = c.p[i];
                if (realSlider(ui_, L.X(c.x), L.Y(c.headY + 24 + 32 * i), L.L(c.w), L.L(28), moveParamLabel(k), r.move[k], 0.1f, 5, moveParamBase(k), moveParamUnit(k),
                               UiExp::state("custom").withId(moveParamLabel(k)), kStdM.move[k], 0.05f))   // (dragged: 0.05 steps; typed / reset: as is)
                    r.move[k] = clampf(r.move[k], 0.1f, 5);
            }
        }
        bool standard = std::all_of(std::begin(r.move), std::end(r.move), [](float v) { return v == 1; });
        if (ui_.button(L.X(456), L.Y(632), L.L(280), L.L(40), "RESET MOVEMENT TO STANDARD", ce, !standard)) for (float& v : r.move) v = 1;
    }
    const float fy = L.Y(632), fw = L.L(176), fh = L.L(40);
    if (ui_.button(L.X(176), fy, fw, fh, "BACK", UiExp::nav("host"))) { setScreen(Screen::Host); return; }
    if (ui_.button(L.X(744), fy, fw, fh, "DELETE", UiExp::nav("host"), !editOrigName_.empty())) {
        deleteCustomMode(editOrigName_);
        if (cfg_.hostCustom == editOrigName_) cfg_.hostCustom.clear();
        setScreen(Screen::Host);
        return;
    }
    if (ui_.button(L.X(928), fy, fw, fh, "SAVE", UiExp::nav("host"), !r.name.empty())) {
        if (!editOrigName_.empty() && editOrigName_ != r.name) deleteCustomMode(editOrigName_);
        if (saveCustomMode(r)) { editOrigName_ = r.name; cfg_.hostCustom = r.name; cfg_.save(); notify("SAVED " + upper(r.name), 2); setScreen(Screen::Host); }
        else notify("COULD NOT SAVE", 3);
    }
}

// ---------------------------------------------------------------------------
// INFO: how everything works, one tab per topic (main menu > INFO). Lines starting with '#' are headings.
static const char* const kInfoTabs[] = {"MODES", "CONTROLS", "HUD & SETTINGS", "HOSTING", "EDITOR", "MAPS"};
static const char* const kInfoPaths[] = {"info/modes", "info/controls", "info/hud", "info/hosting", "info/editor", "info/maps"};   // (Game::uiScreenPath)
static const char* const kInfoPages[][48] = {
    {   // MODES
        "# TEAM MODES",
        "VS - one life each per round; last team standing wins the round. If nobody makes progress for 30 s, health drains. First to 5 rounds.",
        "BOMB - attackers carry the bomb to a site and plant it (hold E). Defenders defuse it (hold E) or hold out until time runs out. 7 rounds, then the sides swap.",
        "KING OF THE HILL - stand on the hill with no enemy on it to score a point a second. First to 100.",
        "PAYLOAD - attackers push the cart along its track by standing next to it; defenders stop it. Checkpoints along the track add time and stop it rolling back. Each team attacks once.",
        "NEUTRAL PAYLOAD - either team pushes the cart toward the other team's end. The team whose side it's on when time runs out wins.",
        "MULTI POINT - attackers take the points in order (A, B, C...). Only they capture; a defender on the point holds it. Each point taken adds 90 s and moves both teams' spawns forward. Each team attacks once.",
        "# DUELS",
        "DUEL - 1v1 (or 2v2): every life has a random modifier (low gravity, giants, rockets only...). First to 11.",
        "1V1 - rounds on one ruleset (vanilla, laser vampire, instagib...). First to 5. Ranked 1v1 rates you on its own ladder.",
        "TOURNEY - a single-elimination bracket of 1v1s; everyone else spectates the match in progress.",
        "# EVERYONE FOR THEMSELVES",
        "FREE FOR ALL - most kills when time runs out wins (a tie goes to overtime: next kill wins).",
        "RACE - the course's checkpoints in order. It starts as untimed practice; vote start (F4) to race. TIME TRIAL (main menu) is the ranked race on Viaduct.",
        "HIDE AND SEEK - one seeker with a knife, everyone else hides first. Everyone seeks once: 1 point for each hider you find, 1 for staying hidden to the end of a round. A tie for first is settled by rock paper scissors (keys 1 / 2 / 3).",
        "TAG - whoever is the TAGGER when the timer runs out is eliminated. Last one standing wins.",
        "LOCATOR (hide and seek and tag: the seeker's / tagger's second weapon, key 0) - fire it to see one random hider / runner through walls for a second; it recharges in 5 s. Everyone else hears its ping from where you stand.",
        "# CUSTOM MODES",
        "Host Game > CUSTOM MODE > NEW: any base mode with your own health, gravity, speed, jump, knockback, weapons (fire rate, damage, projectile speed) and map pool. Players who join get the rules automatically.",
        "# MAPS AND MODES",
        "Each map is built for one mode, plus free for all; Lodestone is built for every mode. You can still start any map in any mode: the menus warn when it isn't built for it (it may be missing spawns or objectives).",
    },
    {   // CONTROLS
        "# DEFAULT KEYS (Settings > CONTROLS to change any of them, two keys per action)",
        "W A S D - move.   SPACE - jump (hold it as you land to bounce: bunny hop).",
        "SHIFT - dash: a burst of speed in the direction you're moving, tilted by where you look. You hold 2 dashes, usable 0.6 s apart (from the end of the last one). A spent dash recharges for 1.2 s from the end of the dash that spent it; after that, landing gives it back (a bunny hop gives one).",
        "Q - grapple: hold to hook what you're aiming at and pull toward it; release to let go.",
        "LEFT MOUSE - fire.   RIGHT MOUSE - alt-fire (scope, knife throw, rocket lob, bubble stream, syringe throw...).",
        "E - use (plant / defuse the bomb).   1-9, 0 - weapons.   MOUSE WHEEL - next / previous weapon.",
        "TAB - scoreboard.   ENTER - chat.   F1 / F2 - vote yes / no.   F4 - vote to start the match.",
        "F3 - performance overlay (average, 1% and 0.1% low FPS, input latency).   F12 - screenshot.   ESC - menu, votes, settings.",
        "# MOVEMENT",
        "You run at 9.5 m/s. In the air, holding a direction drifts you up to run speed (a standing jump moves too).",
        "Strafe-jumping builds speed: hold a strafe key, turn the mouse with it, and keep jumping. Above ~13 m/s A / D turns you faster than W alone.",
        "Each dash leaves you 1 m/s faster than you went in (at least 11, at most 32 m/s).",
        "Dash jump: jump while dashing along the floor to keep the dash's full speed through the jump (it leaves you both dashes).",
        "Super dash: look down and dash into the floor while holding jump: you leave 5 m/s faster than you went into the dash (at least 11, at most 36 m/s), with a hop that gets higher the further down you look (it leaves you 1 dash).",
        "Dash into a wall head-on (under 45 degrees) and you bounce off it at 16 m/s (it leaves you 1 dash).",
        "You step up anything 0.55 m high, including stairs mid-jump. Ladders: walk into them, look up or down to climb (6.75 m/s).",
        "# WEAPONS",
        "ROCKET (splash; rocket jump off the floor for 10% damage), RIFLE (automatic; scoped on alt), LASER (a beam; alt pulls), SHOTGUN (alt: ricochet), SNIPER (scope on alt), STICKY (alt detonates), CROSSBOW (headshots), BUBBLE (alt: a stream of bubbles), KNIFE (alt: throw it), SYRINGE (fire heals you 10 hp, once a second; alt throws it: +30 hp and reset cooldowns for a teammate).",
        "A thrown knife or syringe is gone for 4 s (greyed out in the weapon list, with its timer).",
        "Switching weapons with fire held doesn't fire the new one: let go and press again.",
        "# CONTROL PROFILES",
        "Settings > CONTROLS can export your binds and import someone else's (copy / paste the text).",
    },
    {   // HUD & SETTINGS
        "# SETTINGS (main menu, or ESC in game)",
        "Name, mouse sensitivity (or cm/360 with your DPI), field of view, team and enemy colours, volume, hit sounds, the announcer (off / countdown / all calls; male or female voice), player model and sound pack.",
        "Display: window mode, vsync, low latency mode, frame cap, player shadows, brightness, anti-aliasing and interpolation. Lower latency settings cost smoothness: the notes under them say how.",
        "# HUD LAYOUT (Settings > HUD LAYOUT)",
        "Pick an element in the dropdown (health, weapon name, cooldown bars, speed meter, weapon list) and move it, resize it, recolour it or hide it.",
        "WEAPON MODEL and GRAPPLE MODEL (the first two entries): move the gun / the grapple launcher left / right / up / down, change its size and depth, tilt it (pitch / yaw / roll), give it its own accent colour, or hide it. Your shots come out of the gun and your rope out of the launcher wherever you put them.",
        "CUSTOM WIDGETS: build your own HUD pieces (number, bar, icon) from any value, drag them around, and import / export layouts.",
        "# CROSSHAIR (Settings > CROSSHAIR)",
        "Style, length, thickness, gap, colour and outline, with a live preview.",
        "# RANKED",
        "Ranked games use the default model, HUD and controls and 10 ms interpolation, so everyone plays the same game.",
    },
    {   // HOSTING
        "# HOST A GAME (main menu > HOST GAME)",
        "Pick the mode and map, a server name and the max players. The game picks the map built for the mode; any other map works with a warning.",
        "The server listens on UDP port 27500 (change it under UDP PORT). Quark asks your router to forward it automatically (UPnP); if friends can't connect, forward that UDP port on your router by hand.",
        "RANKED BEHIND A FORWARDED PORT OR IN A CONTAINER: ranked ids are signed for the address players dial. If they reach the server at an address it can't see itself (the router forwards another port to it, or it runs in a container), the host screen says so; start a dedicated server with --own-addr host:port (that address) so their ranked ids work.",
        "PUBLIC lists the server on the internet server list (off: friends join with your IP, shown on the host screen). Games on your network show up for everyone on it.",
        "BALANCE BOTS evens out uneven teams with bots. MODE > RANKED runs rated 1v1 on the ranked maps (no bots). SERVER LOG keeps a log of the match.",
        "CUSTOM MODE: pick one of your saved custom modes (or NEW / EDIT to build one).",
        "ADVANCED > BOT BRAIN: what your bots play: the built-in brain, or a .qbrain file from the bots folder in your data folder. A file that doesn't compile shows its error line and the bots play the built-in brain. Never in ranked.",
        "# IN GAME (ESC as host)",
        "CHANGE MAP / MODE (pick the mode, then the map), FORCE START, RELOAD MAP, EDIT MODE (everyone edits the map together).",
        "# JOIN A GAME (main menu > JOIN GAME)",
        "Pick a server from the list (internet and LAN), or type its address as IP:PORT.",
        "# VOTES (ESC > VOTE, then F1 yes / F2 no)",
        "Start the match, change map / mode, ranked on / off, edit the map together, bots on. A majority of the human players decides.",
        "# PLAY VS BOTS",
        "A private game with bots at your chosen level (beginner, intermediate, advanced, expert), on any mode and map.",
    },
    {   // EDITOR
        "# THE MAP EDITOR (main menu > MAP EDITOR)",
        "Hold the RIGHT MOUSE button to look around; W A S D to fly, SPACE / C up and down, SHIFT faster, the wheel sets the speed.",
        "CLICK anything to select it. New things go where the crosshair (the middle of the view) points.",
        "ADD: boxes (base / accent / wallbang: bullets go through wallbangs for half damage), spawns, lights, payload path points, bomb sites, the hill, race checkpoints, and LADDERS (one object: stretch it and the rungs follow).",
        "Drag the squares on a selected box's faces to resize it; drag the arrows to move it. The keyboard works too: ARROWS / PGUP / PGDN move it by the grid, SHIFT + ARROWS grow a side, CTRL + ARROWS shrink it.",
        "M cycles the material (or a spawn's team), N the shape (box, wedge, corner), CTRL+D duplicates, DEL deletes, CTRL+Z undoes, CTRL+S saves. [ and ] change the grid.",
        "BUILT FOR sets the mode the map is made for; the MODES grid sets what it's offered for. PLAYABLE AS shows what it has the pieces for (team modes need A and B spawns).",
        "TEST plays the map right away; BAKE LIGHT previews the real lighting.",
        "HOST EDIT SESSION: friends join and build with you; everyone's camera shows in the view.",
        "Maps are saved to the maps folder (MAPS FOLDER opens it) as .qmap text files. RESET MAPS puts the built-in maps back.",
    },
    {   // MAPS
        "# THE MAPS AND WHAT THEY'RE BUILT FOR",
        "LODESTONE - a working mine, two storeys deep: built for multi point and for every other mode.",
        "MIDTOWN - a city centre with offices you can climb through to the roofs, an elevated railway and a park: multi point.",
        "BASTION / ORBITAL - team vs.   DEPOT - bomb.   SUMMIT / DOWNTOWN - king of the hill.",
        "RAILYARD - payload.   CANAL - neutral payload.   HARBOR - free for all.",
        "CLOCKWORK - duel.   COLOSSEUM - tourney.   FOUNDRY - tag.   HAMLET - hide and seek.   VIADUCT - race (and the ranked time trial).",
        "TEMPLE, FURNACE, GANTRY, ANVIL - ranked (vanilla 1v1, laser vampire 1v1, instagib free for all with a 5 minute limit, all three).",
        "# YOUR OWN MAPS",
        "Anything in the maps folder shows up in the lists. Servers send their map to players who don't have it.",
    },
};

// ---- demos ----
static std::string demoDir() { return userDataPath("demos"); }

void Game::startRecording() {
    std::error_code ec;
    std::filesystem::create_directories(demoDir(), ec);
    time_t t = time(nullptr);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", localtime(&t));
    std::string path = demoDir() + "/" + safeMapName(net_.map().name) + "_" + modeName(net_.mode().mode) + "_" + stamp + ".qdemo";
    if (net_.startRecording(path, cfg_.demoFullRate)) notify(cfg_.demoFullRate ? "RECORDING DEMO (FULL RATE) - ESC > STOP RECORDING" : "RECORDING DEMO (62.5 HZ) - ESC > STOP RECORDING", 3);
    else notify("COULDN'T START RECORDING", 3);
}

bool Game::playDemo(const std::string& path) {
    leaveGame();
    std::string err;
    if (!net_.startPlayback(path, err)) { error_ = "demo: " + err; return false; }
    paused_ = false;
    setScreen(Screen::Connecting);   // (the connection loop bakes the map and switches to the game)
    return true;
}

// DEMOS: newest first, 8 rows (the wheel scrolls the rest), the recording rate, BACK and OPEN FOLDER
void Game::drawDemos(const SetLayout& L) {
    if (time_ >= demoListAt_) { // rescan now and then (a demo you just recorded shows up)
        demoListAt_ = time_ + 2;
        demoFiles_.clear();
        demoNotes_.clear();
        std::error_code ec;
        std::vector<std::pair<std::filesystem::file_time_type, std::string>> fs;
        for (auto& e : std::filesystem::directory_iterator(demoDir(), ec))
            if (e.path().extension() == ".qdemo") fs.push_back({e.last_write_time(ec), e.path().string()});
        std::sort(fs.begin(), fs.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& f : fs) {
            demoFiles_.push_back(f.second);
            uint16_t proto = kProtocolVersion;   // (6 bytes per file every 2 s, only while this page is open)
            std::string note;
            if (demo::readProtocol(f.second, &proto) && proto != kProtocolVersion)
                note = demo::upgradeRunning() && demo::convertible(proto) ? "CONVERTING..." : "RECORDED WITH " + upper(demo::recordedWith(proto));
            demoNotes_.push_back(note);
        }
    }
    const int kRows = 8;
    const int nd = (int)demoFiles_.size();
    demoScroll_ = std::min(demoScroll_, std::max(0, nd - kRows));
    const int rows = std::min(nd - demoScroll_, kRows);
    static const std::string rateHint = "FULL RATE: EVERY SNAPSHOT (UP TO 1000 HZ) - SMOOTHER, MUCH BIGGER FILES";
    const float lx = L.X(360), lw = L.L(560);
    const int hintLines = (int)ui_.wrap(rateHint, ui_.hintScale(), lw).size();
    // title, the list (or the "none yet" line), the scroll line, RATE + its hint, BACK / OPEN FOLDER
    const float listH = std::max(rows, 1) * 36.0f, scrollH = nd > rows ? 28 : 12;
    const float kH = 24 + 16 + listH + scrollH + 44 + hintLines * 14 - 2 + 20 + 40;
    float y = bandTop(kH);
    ui_.panel(L.X(344), L.Y(y - 16), L.L(592), L.L(kH + 32));
    ui_.labelCentered(L.X(640), L.Y(y), "DEMOS", kWhite, 1.5f, lw);
    y += 40;
    if (demoFiles_.empty()) ui_.labelCentered(L.X(640), L.Y(y + 8), "NONE YET - IN A GAME, ESC > RECORD DEMO", kGrey, 1.0f, lw);
    for (int k = 0; k < rows; k++) {
        int i = demoScroll_ + k;
        std::string name = std::filesystem::path(demoFiles_[i]).stem().string();
        for (auto& c : name) c = c == '_' ? ' ' : (char)toupper((unsigned char)c);
        if (i < (int)demoNotes_.size() && !demoNotes_[i].empty()) name += " (" + demoNotes_[i] + ")";
        if (ui_.button(lx, L.Y(y + 36 * k), lw, L.L(32), name, UiExp::state("demo", "1").withId("DEMO"))) { playDemo(demoFiles_[i]); return; }
    }
    y += listH;
    if (nd > rows) {
        char more[64];
        snprintf(more, sizeof more, "%d-%d OF %d - WHEEL TO SCROLL", demoScroll_ + 1, demoScroll_ + rows, nd);
        ui_.hint(L.X(640) - r_.textWidth(more, ui_.hintScale()) * 0.5f, L.Y(y), more, kGrey, lw);
    }
    y += scrollH;
    static const std::vector<std::string> rates = {"RECORDING RATE: 62.5 HZ", "RECORDING RATE: FULL"};
    int rate = cfg_.demoFullRate ? 1 : 0;
    if (ui_.cycle(lx, L.Y(y), lw, L.L(kMRow), "RATE", rate, 2, rates, UiExp::cfg("demo_full_rate"))) { cfg_.demoFullRate = rate == 1; cfg_.save(); }
    y += 44;
    y += ui_.hintWrapped(lx, L.Y(y), rateHint, kGrey, lw, L.L(14)) * 14 - 2 + 20;
    if (ui_.button(lx, L.Y(y), L.L(276), L.L(kMRow), "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
    if (ui_.button(L.X(644), L.Y(y), L.L(276), L.L(kMRow), "OPEN FOLDER", UiExp::url())) {
        std::error_code ec;
        std::filesystem::create_directories(demoDir(), ec);
        openUrl(std::filesystem::absolute(demoDir(), ec).lexically_normal().string());
    }
}

// INFO: a tab per topic over one 960 x 482 panel; the page in a 640 px column at text 2 (at most 80 characters a
// line), headings in the accent colour. Longer than the panel: the wheel scrolls it (3 lines a notch), with a
// "LINES x-y OF n" line under it.
void Game::drawInfo(const SetLayout& L) {
    const int nt = (int)(sizeof kInfoTabs / sizeof kInfoTabs[0]);
    for (int t = 0; t < nt; t++) {
        const float bx = L.X(176 + 156 * t), by = L.Y(166);
        if (t == infoTab_) tabFrame(r_, ui_, bx, by, L.L(148), L.L(32));
        if (ui_.button(bx, by, L.L(148), L.L(32), kInfoTabs[t], UiExp::nav(kInfoPaths[t]).withId(kInfoTabs[t]))) { infoTab_ = t; infoScroll_ = 0; }
    }
    ui_.panel(L.X(160), L.Y(206), L.L(960), L.L(482), 0.85f);
    const float ts = ui_.textScale(), colW = L.L(640);
    // the page wrapped to the column (once per tab, size and font), each line's place in design px: 20 between a
    // paragraph's lines, 26 to the next paragraph, 24 under a heading, 32 to a heading
    std::vector<InfoRow>& rows = infoRows_;
    if (infoRowsTab_ != infoTab_ || infoRowsW_ != colW || infoRowsTs_ != ts || infoRowsFont_ != r_.fontGeneration()) {
        infoRowsTab_ = infoTab_; infoRowsW_ = colW; infoRowsTs_ = ts; infoRowsFont_ = r_.fontGeneration();
        rows.clear();
        float y = 0;
        bool lastHead = false;
        for (const char* line : kInfoPages[infoTab_]) {
            if (!line) break;
            const bool head = line[0] == '#';
            const std::string text = head ? std::string(line + 2) : std::string(line);
            bool first = true;
            for (const std::string& part : ui_.wrap(text, ts, colW, 80)) {
                if (!rows.empty()) y += !first ? 20 : head ? 32 : lastHead ? 24 : 26;
                rows.push_back({part, head, y});
                first = false;
            }
            lastHead = head;
        }
    }
    const float viewH = 616 - 222;   // (lines from y 222 down to the footer)
    const int n = (int)rows.size();
    int maxFirst = 0;   // the first line that still lets the last one show
    while (maxFirst < n - 1 && rows[n - 1].y - rows[maxFirst].y + 16 > viewH) maxFirst++;
    infoScroll_ = std::min(std::max(infoScroll_, 0), maxFirst);
    int last = infoScroll_ - 1;   // the last line that fits (not a heading whose text would start on the next screen)
    while (last + 1 < n && rows[last + 1].y - rows[infoScroll_].y + 16 <= viewH) last++;
    if (last + 1 < n && last > infoScroll_ && rows[last].head) last--;
    for (int i = infoScroll_; i <= last; i++)
        ui_.label(L.X(320), L.Y(222 + rows[i].y - rows[infoScroll_].y), rows[i].text, rows[i].head ? ui_.accent : kBodyC, 1.0f, colW);
    if (ui_.button(L.X(176), L.Y(632), L.L(176), L.L(kMRow), "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
    if (maxFirst > 0) {
        char lines[64];
        snprintf(lines, sizeof lines, "LINES %d-%d OF %d - WHEEL TO SCROLL", infoScroll_ + 1, last + 1, n);
        ui_.hint(L.X(640) - r_.textWidth(lines, ui_.hintScale()) * 0.5f, L.Y(646), lines, kGrey, L.L(400));
    }
}

void Game::drawPerf() {
    if (time_ >= latNextUpdate_) {
        latNextUpdate_ = time_ + 1.0;
        if (latSamples_.empty()) latText_ = "INPUT->FRAME: MOVE THE MOUSE";
        else {
            // (no full sort: this runs on one frame a second, and that frame is in the 0.1% low it measures)
            double sum = 0;
            for (float v : latSamples_) sum += v;
            float mn = *std::min_element(latSamples_.begin(), latSamples_.end());
            size_t k = std::min(latSamples_.size() - 1, (size_t)(latSamples_.size() * 0.99));
            std::nth_element(latSamples_.begin(), latSamples_.begin() + k, latSamples_.end());
            auto r = [](double ms) { return std::round(ms * 10.0) / 10.0; }; // 100 us steps
            char b[160];
            snprintf(b, sizeof b, "INPUT->FRAME AVG %.1fMS  MIN %.1fMS  99%% %.1fMS", r(sum / latSamples_.size()), r(mn), r(latSamples_[k]));
            latText_ = b;
        }
        latSamples_.clear();
    }
    if (time_ >= perfNextUpdate_ && frameCount_ > 10) {
        perfNextUpdate_ = time_ + 0.5;
        // percentiles with nth_element on a reused buffer: no allocation, no full sort (this runs while
        // measuring frame spikes, so it mustn't cause one)
        // (the same maths as BENCHMARK's: src/game/benchmark.h)
        const bench::Stats st = bench::compute(frameTimes_.data(), frameCount_, perfScratch_);
        char b[200];
        snprintf(b, sizeof b, "AVG %.0f  1%% LOW %.0f  0.1%% LOW %.0f", st.avgFps, st.low1, st.low01);
        perfText_ = b;
        snprintf(b, sizeof b, "WORST %.1fMS  PING %d  3D %dX%d", st.worstMs, net_.findPlayer(net_.clientId()) ? net_.findPlayer(net_.clientId())->ping : 0,
                 r_.sceneWidth(), r_.sceneHeight());   // (the world's own resolution: RENDER SCALE)
        perfText2_ = b;
        pipeAverages();
        perfTick_ = std::string("QUARK V") + QUARK_VERSION;   // (with the numbers, not every frame)
        if (net_.hasSnapshot()) perfTick_ += "  SERVER " + std::to_string(net_.tickRate()) + " HZ  LOSS " + std::to_string(net_.snapshotLoss()) + "%";
        // PRIORITIZE GAME: what Windows actually gave (and the focus state is checked again here, twice a second: a
        // window focused before its focus callback was set never got the call)
        if (!uiTest_) setGamePriority(cfg_.prioritize && glfwGetWindowAttrib(win_, GLFW_FOCUSED) != 0);
        perfPrio_ = gamePriorityStatus();
    }
    // four lines in a box at (24, 8) no wider than x 396 at 720p, so it stays left of the score bar (x 420..860; it
    // ran under it): a line too long for it is closed up / shrunk to fit (Ui::label, cached), not cut
    const float s = r_.height() / 720.0f;
    float ts = std::max(1.0f, std::round(s * 2.0f));
    const std::string* lines[5] = {&perfText_, &perfText2_, &latText_, &perfTick_, &perfPrio_};
    const int nl = perfPrio_.empty() ? 4 : 5;   // (the priority line: Windows only)
    const float lh = r_.textHeight(ts) + 4 * s, x = 24 * s, y = 8 * s, maxW = 372 * s - 16 * s;
    float lw[5], w = 0;
    for (int i = 0; i < nl; i++) w = std::max(w, lw[i] = r_.textWidth(*lines[i], ts));
    w = std::min(w, maxW);
    r_.rect(x, y, w + 16 * s, lh * nl + 4 * s, {0, 0, 0, 0.6f});
    const Color yc = {1, 1, 0.4f, 1};
    for (int i = 0; i < nl; i++) {
        if (lw[i] <= maxW) r_.text(x + 8 * s, y + 4 * s + lh * i, ts, *lines[i], yc);
        else ui_.label(x + 8 * s, y + 4 * s + lh * i, *lines[i], yc, ts / ui_.textScale(), maxW);
    }
    if (screen_ == Screen::InGame) drawPipeline(x, y + lh * nl + 16 * s, std::max(1.0f, ts * 0.85f));
}

// F4: only the pipeline graph's mouse to photon number, worked out twice a second. The same measuring as F3 runs
// (marks, two GPU timestamps a frame), but none of F3's text and bars are drawn, so comparing the two shows their cost.
void Game::drawPerfMini() {
    if (time_ >= perfNextUpdate_ && frameCount_ > 10) {
        perfNextUpdate_ = time_ + 0.5;
        pipeAverages();
        double total = 0;
        if (screen_ == Screen::InGame) drawPipeline(0, 0, 1, &total);
        perfMiniText_ = total > 0 ? "MOUSE TO PHOTON ~" + std::to_string((long long)std::llround(total * 1e6)) + " US" : "MOUSE TO PHOTON: IN GAME ONLY";
    }
    if (perfMiniText_.empty()) return;
    const float s = r_.height() / 720.0f, ts = std::max(1.0f, std::round(s * 2.0f));   // (where F3's box is: left of the score bar)
    r_.rect(24 * s, 8 * s, r_.textWidth(perfMiniText_, ts) + 16 * s, r_.textHeight(ts) + 8 * s, {0, 0, 0, 0.6f});
    r_.text(32 * s, 12 * s, ts, perfMiniText_, {1, 1, 0.4f, 1});
}

// ---- F3 pipeline graph -------------------------------------------------------------------------------------------

void Game::pipeAverages() {   // (twice a second, F3 or F4)
    if (pipeN_ > 0) {   // the pipeline graph's averages
        for (int k = 0; k < PM_COUNT + 3; k++) { pipeAvg_[k] = pipeSum_[k] / pipeN_; pipeSum_[k] = 0; }
        pipeN_ = 0;
        pipeHave_ = true;
    }
    if (mouseBurstTime_ > 0.1) {   // the mouse's rate: the highest standard one it has shown (125 Hz x 2^n, from 10% under)
        const double hz = mouseBurstEvents_ / mouseBurstTime_;
        for (double r = 8000; r >= 125; r /= 2) if (hz >= r * 0.9) { mouseHz_ = std::max(mouseHz_, r); break; }
        mouseBurstEvents_ = mouseBurstTime_ = 0;
    }
}

void Game::pipeMark(int m) {
    if (!perfOn()) { if (m == PM_POLL) mouseEventsPoll_ = 0; return; }
    const double now = timeNow();
    pipeNow_[m] = now;
    if (m == PM_POLL && mouseEventsPoll_ > 0) {   // the mouse's rate: events over the time between the polls that got them
        if (mouseLastEventPoll_ >= 0 && now - mouseLastEventPoll_ < 0.05) { mouseBurstEvents_ += mouseEventsPoll_; mouseBurstTime_ += now - mouseLastEventPoll_; }
        mouseLastEventPoll_ = now;
        mouseEventsPoll_ = 0;
    }
}

void Game::pipeGpuBegin() {   // (a GPU timestamp before the frame's 3D work)
    PipeSlot& sl = pipeQ_[pipeNext_];
    if (!perfOn() || sl.used) return;   // (used: still waiting for an older frame's results: skip this one)
    if (!sl.q[0]) glGenQueries(2, sl.q);
    glQueryCounter(sl.q[0], GL_TIMESTAMP);
    pipeGpuStarted_ = true;
}

void Game::pipeGpuEnd() {     // (...and after its last: the HUD)
    if (!pipeGpuStarted_) return;
    glQueryCounter(pipeQ_[pipeNext_].q[1], GL_TIMESTAMP);
    pipeGpuStarted_ = false;
    pipeFrameDone_ = true;
}

void Game::pipeReset() {
    std::fill(pipeSum_, pipeSum_ + PM_COUNT + 3, 0.0);
    pipeN_ = 0;
    pipePrevFrame_ = pipeLastFlip_ = pipeFrameDt_ = 0;
    for (PipeSlot& sl : pipeQ_) sl.used = false;   // (their old results: overwritten by the next queries)
    pipeFrameDone_ = pipeGpuStarted_ = false;
    for (auto& q : latQ_) q.used = false;
    latSamples_.clear();
    gpuCalibAt_ = -1e9;   // (the GPU clock calibrated at once)
}

void Game::pipeCollect() {    // afterSwap: this frame's marks into its slot; finished slots into the averages
    // Vsync: when each frame really flips, from the monitor's blanks (one scanline read a frame, while F3 / F4 are on):
    // the first blank after the GPU finished it and after the frame before it flipped (frames queue, one a blank).
    const bool vsync = cfg_.presentMode == 1 || cfg_.presentMode == 2;
    double refresh = 0, blank = 0;
    pipeFlipOk_ = vsync && rasterBlank(&blank, &refresh);
    if (pipeFlipOk_) pipeBlankAt_ = blank;
    if (pipeFrameDone_) {
        PipeSlot& sl = pipeQ_[pipeNext_];
        std::copy(pipeNow_, pipeNow_ + PM_COUNT, sl.marks);
        sl.used = true;
        pipeNext_ = (pipeNext_ + 1) % 4;
        pipeFrameDone_ = false;
    }
    // Oldest first (from the slot filled next): in array order, after a wrap, a newer frame came before an older one
    // and the vsync queue model ratcheted. Timestamps finish in order: the first not ready ends the look.
    for (int i = 0; i < 4; i++) {
        PipeSlot& sl = pipeQ_[(pipeNext_ + i) % 4];
        if (!sl.used) continue;
        GLint avail = 0;
        glGetQueryObjectiv(sl.q[1], GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) break;
        GLuint64 t0 = 0, t1 = 0;
        glGetQueryObjectui64v(sl.q[0], GL_QUERY_RESULT, &t0);
        glGetQueryObjectui64v(sl.q[1], GL_QUERY_RESULT, &t1);
        sl.used = false;
        const double poll = sl.marks[PM_POLL];
        for (int k = 0; k < PM_COUNT; k++) pipeSum_[k] += sl.marks[k] - poll;
        pipeSum_[PM_COUNT] += t0 * 1e-9 + gpuClockOffset_ - poll;
        pipeSum_[PM_COUNT + 1] += t1 * 1e-9 + gpuClockOffset_ - poll;
        double flip = std::max(t1 * 1e-9 + gpuClockOffset_, sl.marks[PM_SWAP1]);   // (no vsync: as soon as it's done)
        if (pipePrevFrame_ > 0) pipeFrameDt_ += (sl.marks[PM_FRAME] - pipePrevFrame_ - pipeFrameDt_) * 0.1;
        pipePrevFrame_ = sl.marks[PM_FRAME];
        if (pipeFlipOk_) {
            double f = pipeBlankAt_ + std::ceil((flip - pipeBlankAt_) / refresh) * refresh;   // (its first blank)
            // The driver holds every frame, one a blank, only when the game runs at the refresh rate (the swap waits
            // for room in the queue). Faster, the driver keeps the newest and drops the rest (fast sync): no queue.
            // (Queued, at most two blanks behind: drivers queue up to 3 frames.)
            if (pipeFrameDt_ > refresh * 0.95 && f < pipeLastFlip_ + refresh * 0.5) f = std::min(pipeLastFlip_ + refresh, f + 2 * refresh);
            pipeLastFlip_ = flip = f;
        }
        pipeSum_[PM_COUNT + 2] += flip - poll;
        pipeN_++;
    }
}

// The frame's parts as bars on one time line, from the mouse moving to the light leaving the screen at the crosshair:
// measured on this PC (CPU marks, GPU timestamps) and, for what the game can't see, worked out from the mouse's report
// rate, the frame rate and the monitor's refresh rate (marked EST). Averages over the last half second.
// totalOnly: just the mouse to photon time into it, nothing drawn (F4).
void Game::drawPipeline(float x, float y, float ts, double* totalOnly) {
    if (!pipeHave_) return;
    const double* a = pipeAvg_;
    double sumDt = 0;
    for (size_t k = 0; k < frameCount_; k++) sumDt += frameTimes_[k];
    const double frame = frameCount_ ? sumDt / frameCount_ : 0.0;
    gameMonitor();
    const double hz = vmHz_ > 0 ? vmHz_ : 60.0, refresh = 1.0 / hz;
    const bool mouseKnown = mouseHz_ > 50;
    const double mouseRate = mouseKnown ? mouseHz_ : 1000.0;
    struct Row { std::string name; double t0, t1; Color c; };
    std::vector<Row> rows;
    const Color cIn = {0.55f, 0.75f, 1.0f, 1}, cWait = {0.6f, 0.6f, 0.68f, 1}, cCpu = {1.0f, 0.85f, 0.35f, 1}, cGpu = {0.45f, 0.95f, 0.5f, 1}, cEst = {0.85f, 0.6f, 1.0f, 1};
    auto us = [](double sec) { return std::to_string((long long)std::llround(sec * 1e6)); };
    // before the game reads it: the mouse's report (half its interval on average), then the wait for the next poll
    const double queue = frame * 0.5, mouse = 0.5 / mouseRate;
    const double latch = a[PM_LATCH] > 0 ? a[PM_LATCH] : 0.0;   // (the aim is fixed at the late latch, when there's one)
    char mb[64];
    snprintf(mb, sizeof mb, "MOUSE %s %.0f HZ (AVG)", mouseKnown ? "POLLS AT" : "ASSUMED", mouseRate);
    rows.push_back({mb, latch - queue - mouse, latch - queue, cIn});
    rows.push_back({latch > 0 ? "WAITING FOR THE LATE READ (AVG)" : "WAITING FOR THE NEXT READ (AVG)", latch - queue, latch, cWait});
    auto span = [&](const char* name, int m0, int m1, Color c) { if (a[m1] - a[m0] > 0) rows.push_back({name, a[m0], a[m1], c}); };
    if (maxFps() > 0) span("FRAME CAP WAIT", PM_FRAME, PM_CAP, cWait);
    if (cfg_.lowLatency == 1) span("GPU WAIT (LOW LATENCY ON)", PM_CAP, PM_FENCE, cWait);
    if (cfg_.presentMode == 3) span("LATENT SYNC: WAIT FOR THE RASTER", PM_CAP, PM_FENCE, cWait);
    else if (cfg_.lowLatency == 2) span("PACING WAIT (ULTRA)", PM_CAP, PM_FENCE, cWait);
    span("NETWORK READ", PM_FENCE, PM_NET, cCpu);
    span("READ INPUT", PM_NET, PM_POLL, cCpu);
    span("TICKS + PREDICTION", PM_POLL, PM_TICKS, cCpu);
    span("WORLD + HUD + SHADOW MAP BUILD", PM_TICKS, PM_SCENE0, cCpu);
    span("DRAW CALLS", PM_SCENE0, PM_SCENE1, cCpu);
    span("HUD DRAW", PM_SCENE1, PM_HUD, cCpu);
    if (cfg_.presentMode == 3) span("LATENT SYNC: HOLD FOR THE BLANK", PM_HUD, PM_SWAP0, cWait);
    span("SWAP", PM_SWAP0, PM_SWAP1, cCpu);
    if (ultraPaced()) span("GPU WAIT (ULTRA)", PM_SWAP1, PM_AFTER, cWait);
    rows.push_back({"GPU", a[PM_COUNT], a[PM_COUNT + 1], cGpu});
    // after the GPU: how the frame reaches the screen (EST): the compositor in a window, vsync's vblank, then the
    // scanout down to the crosshair (the middle of the screen: half a refresh on average)
    double t = std::max(a[PM_COUNT + 1], a[PM_SWAP1]);
    const bool windowed = cfg_.displayMode != 2, vsync = cfg_.presentMode == 1 || cfg_.presentMode == 2;   // (latent sync: flipped in the blank, no wait)
    if (vsync && pipeFlipOk_) {   // measured: the frame held, finished, until the blank it really flips at
        const double flip = std::max(t, a[PM_COUNT + 2]);
        rows.push_back({"VSYNC: HELD FOR ITS BLANK", t, flip, cWait});
        t = flip;
        if (windowed) { rows.push_back({"COMPOSITED FRAME SHOWN (EST)", t, t + refresh, cEst}); t += refresh; }
    } else {
        if (windowed) { rows.push_back({"COMPOSITOR: NEXT VBLANK (EST)", t, t + refresh * 0.5, cEst}); t += refresh * 0.5; }
        else if (vsync) { rows.push_back({"VSYNC: NEXT VBLANK (EST)", t, t + refresh * 0.5, cEst}); t += refresh * 0.5; }
        if (windowed && vsync) { rows.push_back({"COMPOSITED FRAME SHOWN (EST)", t, t + refresh, cEst}); t += refresh; }
    }
    rows.push_back({"SCANOUT TO THE CROSSHAIR (EST)", t, t + refresh * 0.5, cEst});
    t += refresh * 0.5;
    const double total = t - rows.front().t0;   // (from the mouse to the photon)
    if (totalOnly) { *totalOnly = total; return; }
    double t0 = rows.front().t0;                  // (the graph's left edge: work before the mouse bar too, e.g. pacing)
    for (const Row& r : rows) t0 = std::min(t0, r.t0);
    // the graph: a label and the time on the left, the bar on its own line on the right (a waterfall)
    const float lh = r_.textHeight(ts) + 4, labelW = r_.textWidth("SCANOUT TO THE CROSSHAIR (EST)  000000 US", ts) + 12;
    const float barW = std::max(200.0f, std::min(700.0f * ts, (float)r_.width() - x - labelW - 24));
    const float h = (rows.size() + 2) * lh + 8;
    r_.rect(x, y, labelW + barW + 16, h, {0, 0, 0, 0.6f});
    float yy = y + 4;
    char head[160];
    snprintf(head, sizeof head, "MOUSE TO PHOTON ~%s US  (%.0f HZ DISPLAY, %s, %s)", us(total).c_str(), hz,
             cfg_.displayMode == 2 ? "FULLSCREEN" : cfg_.displayMode == 1 ? "BORDERLESS" : "WINDOWED", cfg_.presentMode == 3 ? "LATENT SYNC" : vsync ? "VSYNC" : "NO VSYNC");
    r_.text(x + 6, yy, ts, head, {1, 1, 0.4f, 1});
    yy += lh;
    const double scale = t - t0 > 0 ? barW / (t - t0) : 0;
    for (const Row& r : rows) {
        r_.text(x + 6, yy, ts, r.name, r.c);
        std::string v = us(r.t1 - r.t0) + " US";
        r_.text(x + labelW - 6 - r_.textWidth(v, ts), yy, ts, v, r.c);
        float bx = x + labelW + (float)((r.t0 - t0) * scale), bw = std::max(2.0f, (float)((r.t1 - r.t0) * scale));
        r_.rect(bx, yy + lh * 0.3f, bw, std::max(2.0f, lh * 0.35f), r.c);
        yy += lh;
    }
    r_.text(x + 6, yy, ts, "ONE TIME LINE; EST = WORKED OUT, NOT MEASURED; MONITOR PROCESSING NOT INCLUDED", {0.7f, 0.7f, 0.75f, 1});
}


// the world's look from the settings (the game, the menu's background, the editor: all the same world shader)
void Game::sceneSettings() {
    r_.brightness = cfg_.brightness;
    r_.fullbright = cfg_.fullbright;
    r_.fullbrightColor = cfg_.fullbrightColor;
    r_.worldOutline = cfg_.outlineWorld;
}

// What the client knows of its own weapon (weaponpredict.h): the server's word, and the thrown knife / syringe away
// as predicted or as the snapshot says. A demo (watching, no input) or no snapshot yet: dead to the predictor.
WeaponView Game::weaponView() const {
    WeaponView v;
    if (!net_.hasSnapshot()) return v;
    const Snapshot& s = net_.latest();
    v.serverWeapon = s.selfWeapon;
    v.ackSeq = s.ackCmdSeq;
    v.alive = !net_.playback() && net_.selfAlive();
    v.mask = s.ms.weaponMask;
    if (weaponAwayLeft(WPN_KNIFE) > 0) v.away |= (uint16_t)(1u << WPN_KNIFE);
    if (weaponAwayLeft(WPN_SYRINGE) > 0) v.away |= (uint16_t)(1u << WPN_SYRINGE);
    return v;
}

float Game::viewFov() const {
    if (!scoped_) return cfg_.fov;
    // the sniper's and rifle's zoom: VISUALS > ZOOM FOV (never wider than the normal fov); the rest: their own
    int w = heldWeapon_;
    if (w == WPN_SNIPER || w == WPN_RIFLE) return std::min(w == WPN_SNIPER ? cfg_.zoomFovSniper : cfg_.zoomFovRifle, cfg_.fov);
    return weaponDef(w).zoomFov;
}

// ---------------------------------------------------------------------------
// display modes, present modes, low latency, latency measurement

void Game::applyDisplayMode() {
    vmStale_ = true;      // (the mode may change: read it again)
    mouseHeld_ = false;   // (and the cursor held at the window's new centre)
    // the monitor chosen in Settings > DEVICES (else the primary), at its desktop size
    GLFWmonitor* mon = chosenMonitor();
    if (!mon) return;
    const DeskMode vm = desktopMode(mon);
    appliedMon_ = mon;
    int mx = 0, my = 0;
    glfwGetMonitorPos(mon, &mx, &my);
    switch (cfg_.displayMode) {
        case 2: // exclusive fullscreen: owns the display, bypasses the desktop compositor (at DEVICES' REFRESH RATE if
                // one is picked: GLFW takes the closest mode the monitor has at that size)
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(win_, mon, 0, 0, vm.w, vm.h, cfg_.fullscreenHz > 0 ? cfg_.fullscreenHz : vm.hz);
            break;
        case 1: { // borderless fullscreen window covering the monitor
            glfwSetWindowMonitor(win_, nullptr, mx, my, vm.w, vm.h, 0);
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowPos(win_, mx, my);
            glfwSetWindowSize(win_, vm.w, vm.h);
            break;
        }
        default:   // (on the chosen monitor too: 100 px in from its corner)
            glfwSetWindowMonitor(win_, nullptr, mx + 100, my + 100, cfg_.width, cfg_.height, 0);
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_TRUE);
            break;
    }
    applyPresentMode(); // some drivers reset the swap interval on mode changes
    // Render at the new size right away (don't wait for a resize event that may arrive late).
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(win_, &fw, &fh);
    if (cfg_.displayMode != 0) { fw = std::max(fw, vm.w); fh = std::max(fh, vm.h); }
    if (fw > 0 && fh > 0) r_.resize(fw, fh);
}

// cfg_.monitor's monitor (devices.h: its name, "#2" for the second of the same name), else the primary. Looked up again
// only when the setting or the monitors change (GLFW's list is its own memory: no OS call either way).
GLFWmonitor* Game::chosenMonitor() {
    if (chosenMon_ && chosenMonId_ == cfg_.monitor) return chosenMon_;
    chosenMonId_ = cfg_.monitor;
    chosenMon_ = nullptr;
    chosenGone_ = false;
    if (!cfg_.monitor.empty()) {
        int n = 0;
        GLFWmonitor** ms = glfwGetMonitors(&n);
        std::vector<std::string> names;
        for (int i = 0; i < n; i++) { const char* nm = glfwGetMonitorName(ms[i]); names.push_back(nm ? nm : "MONITOR"); }
        names = uniqueNames(names);
        for (int i = 0; i < n; i++) if (names[i] == cfg_.monitor) chosenMon_ = ms[i];
        chosenGone_ = !chosenMon_;
        if (chosenGone_ && monGoneNoted_ != cfg_.monitor) { monGoneNoted_ = cfg_.monitor; notify("MONITOR " + upper(cfg_.monitor) + " NOT FOUND: USING THE PRIMARY", 5); }
    }
    if (!chosenMon_) chosenMon_ = glfwGetPrimaryMonitor();
    return chosenMon_;
}

// The monitor's desktop mode: what glfwGetVideoMode says, except while the game is exclusive on it (then that's the
// game's own mode, maybe another refresh rate; the one read before it went exclusive is kept)
Game::DeskMode Game::desktopMode(GLFWmonitor* mon) {
    GLFWmonitor* on = glfwGetWindowMonitor(win_);   // (exclusive on it: its desktop mode is the one kept)
    if (mon && mon == deskMon_ && on == mon) return desk_;
    const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    if (!vm) return desk_;
    const DeskMode m = {vm->width, vm->height, vm->refreshRate};
    if (!on || on != deskMon_) { deskMon_ = mon; desk_ = m; }   // (another monitor read while exclusive keeps the kept one)
    return m;
}

// A monitor plugged in or out: the chosen one found again (the pointers to a gone one are dangling now), the page's
// list made again, and a fullscreen window moved if its monitor is now another one
void Game::monitorsChanged() {
    chosenMon_ = nullptr;
    deskMon_ = nullptr;
    vmStale_ = true;
    if (devicesOpen_) devListed_ = false;
    GLFWmonitor* was = appliedMon_;
    appliedMon_ = nullptr;
    if (chosenMonitor() != was && cfg_.displayMode != 0) applyDisplayMode();
    else appliedMon_ = chosenMonitor();
}

// Latent Sync (PRESENT: LATENT SYNC, like Special K's): one frame a refresh. It's started late enough that its input is
// fresh but early enough that even the slowest recent frame is finished in time; beforeSwap then holds the finished
// frame and flips it (no vsync) just as the refresh's vertical blank begins, so the tear line falls in the blank, off
// screen, however long the frame took. The lead adapts by itself: the slowest frame (input read to finished on the GPU)
// of the last few seconds, plus a margin. The offset moves the flip (+ later) if a driver's flip lands off the blank.
GLFWmonitor* Game::gameMonitor() {
    if (g_monitorEvent) { g_monitorEvent = false; monitorsChanged(); }
    GLFWmonitor* mon = glfwGetWindowMonitor(win_);   // (both cheap: GLFW's own fields; chosenMonitor() a cached pointer)
    if (!mon) mon = chosenMonitor();
    if (vmStale_ || mon != vmMon_) {
        const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
        vmMon_ = mon;
        vmW_ = vm ? vm->width : 0; vmH_ = vm ? vm->height : 0; vmHz_ = vm ? vm->refreshRate : 0;
        vmStale_ = false;
    }
    return mon;
}

bool Game::rasterBlank(double* at, double* refresh) {
    GLFWmonitor* mon = gameMonitor();
    *refresh = 1.0 / (vmHz_ > 0 ? vmHz_ : 60);
    // (opened again for another monitor or mode; one that failed, or stopped answering (a driver reset), is retried
    // a second later, not never)
    if (mon != latentMon_ || vmH_ != latentH_ || vmHz_ != latentHz_ || (!raster_.ok() && time_ - latentOpenAt_ > 1.0)) {
        raster_.open(mon);
        latentMon_ = mon; latentH_ = vmH_; latentHz_ = vmHz_; latentOpenAt_ = time_;
    }
    if (raster_.nextBlank(*refresh, at)) return true;
    raster_.close();
    return false;
}

void Game::latentWait() {
    latentFlip_ = 0;   // (none planned unless the scanline says when)
    double blank = 0, refresh = 0;
    if (!rasterBlank(&blank, &refresh)) return;   // (no scanline from the driver: no vsync)
    const double now = timeNow(), lead = latentLead_ + 0.0001;   // (input read to finished, the slow end lately, + 100 us)
    // A PC whose frames take most of a refresh can't make every blank: no sync (it tears instead of stuttering, like
    // adaptive vsync) until the frames are quick enough again
    if (lead > refresh * 0.9) return;
    double flip = blank + cfg_.latentOffsetUs * 1e-6;
    while (flip - lead < now || flip < latentLastFlip_ + refresh * 0.5) flip += refresh;   // (one frame a refresh)
    latentFlip_ = latentLastFlip_ = flip;
    waitUntil(flip - lead);
}

void Game::beforeSwap() {
    latentHeld_ = 0;
    if (cfg_.presentMode != 3) return;
    double took;
    if (latentFlip_ > 0) {   // the frame finished on the GPU (how long it took from its input read: the lead)...
        GLsync f = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glClientWaitSync(f, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
        glDeleteSync(f);
        took = timeNow() - ultraReadAt_;
    } else took = ultraCpu_ + ultraGpu_;   // (unsynced: no wait for the GPU, ULTRA's learnt times say it)
    if (!bake_) {   // (the loading screen sleeps on purpose: not a frame's time)
        const double refresh = 1.0 / (vmHz_ > 0 ? vmHz_ : 60);
        latentTook_[latentTookN_++ & 255] = (float)std::min(took, refresh);
        float a = 0, b = 0, c = 0;   // (the 3 slowest: a spike or two tear once, they don't hold the lead up)
        for (float v : latentTook_) {
            if (v > a) { c = b; b = a; a = v; } else if (v > b) { c = b; b = v; } else if (v > c) c = v;
        }
        latentLead_ = c;
    }
    // ...then held until its vertical blank (a frame that ran late flips now: its tear line shows, and the lead grows)
    if (latentFlip_ > 0) {
        const double t = timeNow();
        waitUntil(latentFlip_);
        latentHeld_ = timeNow() - t;
    }
}

void Game::applyPresentMode() {
    int interval = 0;
    if (cfg_.presentMode == 1) interval = 1;
    if (cfg_.presentMode == 2) {
        bool tear = glfwExtensionSupported("WGL_EXT_swap_control_tear") || glfwExtensionSupported("GLX_EXT_swap_control_tear");
        interval = tear ? -1 : 1; // adaptive: vsync, but tear instead of waiting when a frame is late
    }
#ifdef _WIN32
    // GLFW 3.3 replaces vsync in a window (windowed and borderless) with DwmFlush before each swap: the frame waits for
    // the compositor's vblank, is handed over only then and shown a vblank later, a refresh behind (and adaptive never
    // tears). The driver's own swap interval instead: GLFW keeps 0, so it adds no DwmFlush.
    using SwapIntervalFn = int(__stdcall*)(int);
    static const auto wglSwapInterval = (SwapIntervalFn)glfwGetProcAddress("wglSwapIntervalEXT");
    glfwSwapInterval(0);
    if (wglSwapInterval) { wglSwapInterval(interval); return; }
#endif
    glfwSwapInterval(interval);
}

// The network read (snapshots, prediction reconcile and its replays) before this frame's input: none of it reads the
// mouse, so doing it after the input was read only made that input older by the time it was drawn (a reconcile that
// replays dozens of commands is a fraction of a millisecond). After beforeInput's GPU wait, so what's drawn is fresh.
void Game::preInput() {
    if (bake_ && net_.playback()) net_.holdPlaybackClock();   // a demo waits for its map's bake
    { QZONE("net poll"); net_.poll(); }
    polledEarly_ = true;
}

void Game::beforeInput() {
    // Low latency "on": allow at most one frame queued on the GPU. We wait for the frame before the
    // previous one to finish before sampling input, so input is never stuck behind a deep queue.
    // (afterSwap just stored the previous frame's fence at fenceIdx_ ^ 1 and flipped the index: fenceIdx_ holds
    // the frame before it, the slot afterSwap overwrites next. Waiting on fenceIdx_ ^ 1 queued nothing: "ultra".)
    if (cfg_.lowLatency == 1 && !ultraPaced()) {   // (Latent Sync paces like ultra: these fences would add nothing)
        int older = fenceIdx_;
        if (fence_[older]) {
            QZONE("low latency: wait for the GPU");   // (an idle CPU here = GPU-bound)
            glClientWaitSync((GLsync)fence_[older], GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
            glDeleteSync((GLsync)fence_[older]);
            fence_[older] = nullptr;
        }
    }
    // Low latency "ultra", paced like NVIDIA Reflex: start the frame (read input) just early enough that it's handed to
    // the GPU as the GPU finishes the last one, so the input is as fresh as with nothing queued, without the GPU idling
    // through every CPU frame (ultra used to wait for the GPU after each swap: CPU and GPU took turns, costing fps).
    // The GPU's time per frame (swap to its fence) and the CPU's (input read to swap) are learnt as it runs.
    if (cfg_.presentMode == 3) latentWait();
    if (ultraPaced() && ultraFence_) {
        constexpr double kMargin = 0.00015;   // (start a little early: a late start is a GPU bubble, an early one 150 us)
        const double target = ultraSwapAt_ + ultraGpu_ - ultraCpu_ - kMargin;
        bool waited = false;
        for (;;) {
            const GLenum st = glClientWaitSync((GLsync)ultraFence_, waited ? 0 : GL_SYNC_FLUSH_COMMANDS_BIT, 0);
            const double now = timeNow();
            if (st == GL_ALREADY_SIGNALED || st == GL_CONDITION_SATISFIED || st == GL_WAIT_FAILED) {
                if (waited) ultraGpu_ += (now - ultraSwapAt_ - ultraGpu_) * 0.1;   // (seen finishing: a measurement)
                glDeleteSync((GLsync)ultraFence_);
                ultraFence_ = nullptr;
                break;
            }
            if (now >= target) break;   // still on the GPU: start now, it'll be done about when this frame is handed over
            waited = true;
            waitUntil(std::min(target, now + 0.0001));
        }
    }
    ultraReadAt_ = timeNow();
}

void Game::afterSwap() {
    // Low latency fences: "on" keeps at most one frame queued (beforeInput waits on the one before); "ultra" fences
    // each frame and beforeInput starts the next one just in time for it.
    if (ultraPaced()) {   // ultra (beforeInput paces it): never more than this frame on the GPU
        // (the CPU's time ends at the swap, before any wait for the last frame below, and without Latent Sync's hold for
        // the blank: counting the wait made the learnt CPU time grow every frame when GPU-bound, until pacing stopped)
        const double swapped = timeNow();
        if (ultraFence_) {   // the last one isn't done yet (started too early): wait for it, and learn its real time
            glClientWaitSync((GLsync)ultraFence_, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
            glDeleteSync((GLsync)ultraFence_);
            ultraFence_ = nullptr;
            ultraGpu_ += (timeNow() - ultraSwapAt_ - ultraGpu_) * 0.1;
        }
        const double now = timeNow();
        ultraCpu_ += (swapped - latentHeld_ - ultraReadAt_ - ultraCpu_) * 0.1;
        ultraFence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        ultraSwapAt_ = now;
    } else if (ultraFence_) { glDeleteSync((GLsync)ultraFence_); ultraFence_ = nullptr; }   // (left ultra)
    if (cfg_.lowLatency == 1 && !ultraPaced()) {
        // (no flush: beforeInput waits on it two frames on, with the flush bit, after the next swap has flushed it)
        GLsync f = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (fence_[fenceIdx_]) glDeleteSync((GLsync)fence_[fenceIdx_]);
        fence_[fenceIdx_] = f;
        fenceIdx_ ^= 1;
    } else for (auto& f : fence_) if (f) { glDeleteSync((GLsync)f); f = nullptr; }   // (left "on")
    if (!perfOn()) return;
    pipeMark(PM_AFTER);
    double now = timeNow();
    if (now - gpuCalibAt_ > 10.0) {   // (a synchronous GPU query: it can stall the driver, so rarely; the clocks drift by microseconds)
        // (the CPU time halfway through the query, the best of 3: the time after it returned put every GPU time late
        // by the call's return; and before the collect, so the first results after F3 / F4 turn on use it)
        double best = 1e9;
        for (int k = 0; k < 3; k++) {
            GLint64 g = 0;
            const double c0 = timeNow();
            glGetInteger64v(GL_TIMESTAMP, &g);
            const double c1 = timeNow();
            if (c1 - c0 < best) { best = c1 - c0; gpuClockOffset_ = 0.5 * (c0 + c1) - g * 1e-9; }
        }
        gpuCalibAt_ = now;
    }
    pipeCollect();
    if (!showPerf_) return;   // (F4: the graph's marks only (and the clocks above); F3's input to frame latency below)
    // Input-to-frame latency: GPU timestamp written after this frame's commands, converted to
    // the CPU clock, minus when the game received the input event from the OS. Read back a few
    // frames later without stalling.
    if (frameInput_ >= 0) {
        LatQuery& q = latQ_[latNext_];
        if (!q.used) {
            glQueryCounter(q.q, GL_TIMESTAMP);
            q.input = frameInput_;
            q.used = true;
            latNext_ = (latNext_ + 1) % 16;
        }
    }
    for (auto& q : latQ_) {
        if (!q.used) continue;
        GLint avail = 0;
        glGetQueryObjectiv(q.q, GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) continue;
        GLuint64 t = 0;
        glGetQueryObjectui64v(q.q, GL_QUERY_RESULT, &t);
        double done = t * 1e-9 + gpuClockOffset_;
        double ms = (done - q.input) * 1000.0;
        if (ms >= 0 && ms < 1000) latSamples_.push_back((float)ms);
        q.used = false;
    }
}

// Every map or mode as a grid of buttons, each with what it is under it (a map: the mode it's built for).
// One card-grid template (ui-redesign-spec, the pickers), in the menus (host, vs bots) and in game (the pause menu's
// live / vote / tourney pickers): a 960 panel centred in the band (the menus' under the title; in game between the
// notices and the weapon bar, 95% opaque), the title at text 3, cards (modes / rulesets / ladders: 3 columns of
// 298.7, 40 high; maps: 5 columns of 176, 32 high) with their description at text 1.5 wrapped under them, the current
// one framed in the accent colour (a tourney list: the picked ones), BACK bottom-left, CALL VOTE (a tourney list)
// bottom-right. A map not built for the mode is tagged NOT FOR THIS MODE under one legend line (the maps built for it
// in the accent colour) instead of an orange two-line warning under almost every map (it ran into the next card).
void Game::drawPicker(float cx, float y) {
    (void)cx; (void)y;
    bool ladders = picker_ == PICK_RANKED; // the three ranked ladders (hosting, or in game: host switches / anyone votes)
    bool serverRanked = net_.state() == ConnState::Connected && net_.hasSnapshot() && (net_.mode().flags & MS_RANKED);
    bool variants = picker_ == PICK_VARIANT || picker_ == PICK_TMODES || ladders;
    bool multi = picker_ == PICK_TMAPS || picker_ == PICK_TMODES; // tourney rotation lists: pick several, then vote
    bool modes = picker_ == PICK_HOST_MODE || picker_ == PICK_BOT_MODE || picker_ == PICK_LIVE_MODE;
    bool live = picker_ == PICK_LIVE_MAP || picker_ == PICK_LIVE_MODE || (picker_ == PICK_VARIANT && variantCtx_ == 2) || (ladders && variantCtx_ == 2);
    if (live && net_.state() != ConnState::Connected) { picker_ = PICK_NONE; return; } // a live (in-game) picker with no game: nothing to pick for
    bool host = !live && (picker_ == PICK_HOST_MAP || picker_ == PICK_HOST_MODE || (picker_ == PICK_VARIANT && variantCtx_ == 0) || ladders);
    int& mode = live ? liveMode_ : host ? hostModeIdx_ : cfg_.botMode;
    int& mapIdx = live ? liveMap_ : host ? hostMapIdx_ : botMapIdx_;
    int& variant = live ? liveVariant_ : host ? cfg_.hostArenaVariant : cfg_.botArenaVariant;
    static const int kLadders[3] = {0, 1, 5};
    bool rankedTile = picker_ == PICK_HOST_MODE || picker_ == PICK_LIVE_MODE; // RANKED is one more choice, holding the ladders
    bool rankedNow = live ? serverRanked : host && cfg_.hostRanked; // vs bots is never ranked
    int n = ladders ? 3 : variants ? kArenaVariants : modes ? MODE_COUNT + (rankedTile ? 1 : 0) : (int)maps_.size();
    // where a choice / BACK leaves you (the UI test checks it): the host / vs bots menu, or the pause menu (or its votes)
    const char* done = host ? "host" : "vsbots";
    const char* back = screen_ == Screen::InGame ? (voteMenu_ ? "ingame/pause/vote" : "ingame/pause") : done;
    if ((int)multiSel_.size() != n) multiSel_.assign(n, false);
    std::string title = picker_ == PICK_TMAPS ? "TOURNEY MAPS (PICK ANY, THEY ROTATE)" : picker_ == PICK_TMODES ? "TOURNEY RULESETS (PICK ANY, THEY ROTATE)"
                      : ladders ? "RANKED - CHOOSE A LADDER"
                      : variants ? std::string(modeTitle(mode)) + " - CHOOSE A RULESET" : modes ? "CHOOSE A MODE"
                      : std::string("CHOOSE A MAP FOR ") + (rankedNow ? "RANKED" : modeTitle(mode));
    if ((live && livePickVote_) || multi) title = "VOTE: " + title;
    // finishing a choice: host/bots remember it; in game it's a map command or a vote
    auto finish = [&]() {
        picker_ = PICK_NONE;
        if (live) {
            std::string cmd = (livePickVote_ ? "callvote map " : "map ") + maps_[mapIdx] + " " + modeName(mode);
            if (mode == MODE_ARENA || mode == MODE_TOURNEY) cmd += std::string(" ") + arenaVariant(variant).name;
            net_.sendCommand(cmd);
            voteMenu_ = false;
            paused_ = false;
        } else cfg_.save();
    };
    // item i: its label, what's said under it, whether it can be picked, whether it's the current one, and what
    // picking it does (built: a map built for the mode; warn: one that isn't)
    struct Item { std::string label, sub; bool ok = true, cur = false, warn = false, built = false; UiExp pe; };
    auto item = [&](int i) {
        Item it;
        if (ladders) {
            it.label = arenaVariant(kLadders[i]).title;
            it.sub = std::string(arenaVariant(kLadders[i]).desc) + " - OWN RATING";
            it.cur = rankedNow && kLadders[i] == (live ? net_.mode().arenaVariant : variant);
        } else if (variants) {
            it.label = arenaVariant(i).title;
            it.sub = arenaVariant(i).desc;
            it.cur = multi ? multiSel_[i] : i == variant;
        } else if (modes && i == MODE_COUNT) {
            it.label = "RANKED";
            it.sub = "LADDERS (1V1, INSTAGIB FFA) - DEFAULT MODEL, HUD, CONTROLS >";
            it.cur = rankedNow;
        } else if (modes) {
            it.label = modeTitle(i);
            it.cur = i == mode && !rankedNow;
            for (auto& mi : mapInfo_) if (mi.preferredMode == i) it.sub = "MAP: " + upper(mi.title);
            if (it.sub.empty() && i == MODE_TAG) for (auto& mi : mapInfo_) if (mi.preferredMode == MODE_HIDE) it.sub = "MAP: " + upper(mi.title);
            if (i == MODE_ARENA) it.sub = "9 RULESETS >";
            if (i == MODE_TOURNEY) it.sub = "BRACKET OF 1V1S - RULESETS >";
        } else {
            const MapInfo& mi = mapInfo_[i];
            it.label = upper(maps_[i]);
            it.cur = multi ? multiSel_[i] : i == mapIdx;
            it.built = (mi.modes >> (multi ? MODE_TOURNEY : mode)) & 1;
            int ladderNow = live ? net_.mode().arenaVariant : cfg_.hostArenaVariant;
            bool rankedOnly = rankedNow && !(mi.ranked >> ladderNow & 1);
            // any map can be picked for any mode; one not built for it says so (it may be missing what the mode needs)
            it.sub = rankedOnly ? "NOT A RANKED MAP FOR THIS LADDER" : !it.built ? "WARNING: NOT BUILT FOR THIS MODE - MAY NOT WORK" : mi.preferredMode >= 0 ? std::string("BUILT FOR ") + modeTitle(mi.preferredMode) : "ALL MODES";
            it.ok = !rankedOnly;
            it.warn = !it.built && !rankedOnly;
        }
        it.pe = multi ? UiExp::state("multisel")
              : ladders && live ? UiExp::cmd(livePickVote_ ? "callvote ranked on" : "ranked on")
              : ladders ? UiExp::nav("host")
              : variants ? (live ? UiExp::nav("ingame/pause/picker:livemap") : UiExp::nav(done))
              : modes && i == MODE_COUNT ? UiExp::nav(live ? "ingame/pause/picker:ladders" : "host/picker:ladders")
              : modes ? (i == MODE_ARENA || i == MODE_TOURNEY ? UiExp::nav(live ? "ingame/pause/picker:variants" : host ? "host/picker:variants" : "vsbots/picker:variants")
                         : live ? UiExp::nav("ingame/pause/picker:livemap") : UiExp::nav(done))
              : live ? UiExp::cmd(livePickVote_ ? "callvote map" : "map") : UiExp::nav(done);
        return it;
    };
    auto choose = [&](int i) {
        if (multi) multiSel_[i] = !multiSel_[i];
        else if (ladders && live) { // host switches right away, anyone else calls a vote
            net_.sendCommand(std::string(livePickVote_ ? "callvote " : "") + "ranked on " + arenaVariant(kLadders[i]).name);
            picker_ = PICK_NONE; voteMenu_ = false; paused_ = false;
        }
        else if (ladders) {
            cfg_.hostRanked = true;
            variant = kLadders[i];
            mode = MODE_ARENA;
            if (!(mapInfo_[mapIdx].ranked >> variant & 1)) mapIdx = bestRankedMap(variant);
            finish();
        }
        else if (variants) { variant = i; if (live) picker_ = PICK_LIVE_MAP; else finish(); }   // in game: then the map
        else if (modes && i == MODE_COUNT) { variantCtx_ = live ? 2 : 0; picker_ = PICK_RANKED; }
        else if (modes) {
            if (host) cfg_.hostRanked = false; // any other mode: unranked
            if (live && serverRanked && !livePickVote_) net_.sendCommand("ranked off"); // host leaves ranked for another mode
            mode = i;
            if (!live || !((mapInfo_[mapIdx].modes >> i) & 1)) mapIdx = bestMapFor(i); // live: keep the map if it can
            if (i == MODE_ARENA || i == MODE_TOURNEY) { // second step: which 1v1 ruleset
                variantCtx_ = live ? 2 : host ? 0 : 1;
                picker_ = PICK_VARIANT;
            } else if (live) picker_ = PICK_LIVE_MAP;    // in game: then a map for it (one vote / change for both)
            else finish();
        } else { mapIdx = i; finish(); }
    };

    {
        const bool inGame = screen_ == Screen::InGame;
        const SetLayout L = inGame ? ingameLayout(960) : menuLayout(960);
        const float bandH = inGame ? kIgH : kBandH;
        const bool mapList = !ladders && !variants && !modes;
        // every card once a frame (labels, tags: what it shows; the buffers are reused)
        static std::vector<Item> items;
        static std::vector<std::string> descs;
        items.resize(n);
        descs.resize(n);
        bool warned = false;   // (a map not built for the mode: the legend line, the built ones in the accent colour)
        for (int i = 0; i < n; i++) {
            items[i] = item(i);
            const Item& it = items[i];
            warned |= it.warn;
            descs[i] = !mapList ? it.sub : it.warn ? "NOT FOR THIS MODE" : it.sub.rfind("BUILT FOR ", 0) == 0 ? it.sub.substr(6) : it.sub;   // ("FOR <MODE>")
        }
        const float hs = ui_.hintScale();
        int cols = mapList ? 5 : 3;
        const float gx = mapList ? 12 : 16, gy = mapList ? 4 : 12, cardH = mapList ? 32 : 40;
        const float headH = 24 + (warned ? 24 : 0) + 16;
        // a row's height: the cards, and their descriptions wrapped at text 1.5 to the card (Ui::wrap's cache)
        float cw = 0;
        auto rowH = [&](int r0) {
            int lines = 0;
            for (int i = r0; i < n && i < r0 + cols; i++) lines = std::max(lines, (int)ui_.wrap(descs[i], hs, L.L(cw - 4)).size());
            return cardH + (lines ? 4 + lines * 12 + (lines - 1) * 2 : 0);
        };
        float gridH = 0;
        auto measure = [&]() {   // the grid's height at this many columns
            cw = (928 - gx * (cols - 1)) / cols;
            gridH = 0;
            for (int r0 = 0; r0 < n; r0 += cols) gridH += rowH(r0) + (r0 + cols < n ? gy : 0);
        };
        measure();
        while (headH + gridH + 60 + 32 > bandH && cols < 8) { cols++; measure(); }   // (many maps of your own: more, narrower columns)
        const float kH = headH + gridH + 20 + 40;
        const float top = inGame ? ingameTop(L, kH) : bandTop(kH);
        ui_.panel(L.X(160), L.Y(top - 16), L.L(960), L.L(kH + 32), inGame ? 0.95f : 0.75f);
        ui_.labelCentered(L.X(640), L.Y(top), title, kWhite, 1.5f, L.L(928));
        if (warned) {
            static const std::string legend = "MAPS MARKED \"NOT FOR THIS MODE\" MAY NOT WORK IN IT";
            ui_.hint(L.X(640) - r_.textWidth(legend, hs) * 0.5f, L.Y(top + 36), legend, kWarnC, L.L(928));
        }
        float ry = top + headH;
        for (int r0 = 0; r0 < n; r0 += cols) {
            const float h = rowH(r0);
            for (int i = r0; i < n && i < r0 + cols; i++) {
                const Item& it = items[i];
                const float bx = 176 + (i - r0) * (cw + gx);
                if (it.cur) tabFrame(r_, ui_, L.X(bx), L.Y(ry), L.L(cw), L.L(cardH));
                if (ui_.button(L.X(bx), L.Y(ry), L.L(cw), L.L(cardH), it.label, it.pe, it.ok)) { choose(i); return; }   // (the next frame: what that led to)
                const Color dc = it.warn ? kDimC : !it.ok ? Color{0.75f, 0.45f, 0.4f, 1} : mapList && warned && it.built ? ui_.accent : kGrey;
                ui_.hintWrapped(L.X(bx + 2), L.Y(ry + cardH + 4), descs[i], dc, L.L(cw - 4), L.L(14));
            }
            ry += h + gy;
        }
        ry += 20 - gy;
        const float fw = std::max(cw, 176.0f);   // (the footer's buttons: a card wide, at least 176)
        if (multi) {   // a tourney rotation list: the picked ones go to one vote
            int count = 0;
            std::string list;
            for (int i = 0; i < n; i++)
                if (multiSel_[i]) { list += (count++ ? "," : "") + std::string(variants ? arenaVariant(i).name : maps_[i]); }
            if (ui_.button(L.X(1104 - fw), L.Y(ry), L.L(fw), L.L(40), "CALL VOTE", UiExp::cmd(picker_ == PICK_TMAPS ? "callvote tmaps" : "callvote tmodes"), count > 0)) {
                net_.sendCommand(std::string("callvote ") + (picker_ == PICK_TMAPS ? "tmaps " : "tmodes ") + list);
                picker_ = PICK_NONE; voteMenu_ = false; paused_ = false;
                return;
            }
        }
        if (ui_.button(L.X(176), L.Y(ry), L.L(fw), L.L(40), "BACK", UiExp::nav(back))) picker_ = PICK_NONE;
    }
}

void Game::drawKnifeSlash(const Vec3& eye, float yaw, float pitch, float t, const Vec3& color, Vec3 from) {
    const float reach = weaponDef(WPN_KNIFE).range;
    Vec3 fwd = angleForward(yaw, pitch);
    Vec3 right = normalize(cross(fwd, {0, 1, 0})), up = cross(right, fwd);
    float fade = 1.0f - t;
    // the hit check (a ray to `reach`), drawn as a blade-width beam from the knife out to the tip (no marker at the
    // end: the owner)
    if (length(from) == 0) from = eye + right * 0.06f - up * 0.1f + fwd * 0.05f;   // (someone else's: their knife hand)
    Vec3 tip = eye + fwd * reach;
    r_.line(from, tip, 0.05f, rgba(color * 1.3f, 0.25f * fade));
    r_.line(from, tip, 0.015f, rgba({1, 1, 1}, 0.7f * fade));
}

void Game::drawProp(int prop, const ModelPalette& pal, const Vec3& origin, float yaw, const Vec3& right, const Vec3& back, float boxYaw, float sv) {
    if (const ModelMesh* ov = modelOverride(MODEL_PROP, prop)) {   // the owner's model (docs/ASSETS.md)
        Vec3 tint[TINT_COUNT];
        overrideTints(MODEL_PROP, prop, pal, tint);
        r_.mesh(*ov, Mat4::translate(origin) * Mat4::rotY(yaw), tint, sv, true);
        return;
    }
    if (const ModelParts* bx = boxOverride(MODEL_PROP, prop)) overrideBoxParts(MODEL_PROP, prop, *bx, pal, 1.0f, parts_);
    else propParts(prop, pal, parts_);
    for (const ModelPart& q : parts_)
        r_.box(origin + Vec3(right.x * q.c.x + back.x * q.c.z, q.c.y, right.z * q.c.x + back.z * q.c.z), q.h, boxYaw, q.color, sv, q.shadow);
}

void Game::drawArm(int prop, const Vec3& shoulder, float yaw, float pitch, float bs, float sv, const ModelPalette& pal) {
    if (const ModelMesh* ov = modelOverride(MODEL_PROP, prop)) {
        Vec3 tint[TINT_COUNT];
        overrideTints(MODEL_PROP, prop, pal, tint);
        Mat4 sc;
        sc.m[0] = sc.m[5] = sc.m[10] = bs;
        r_.mesh(*ov, Mat4::translate(shoulder) * Mat4::rotY(yaw) * Mat4::rotX(pitch) * sc, tint, sv, true);
        return;
    }
    if (const ModelParts* bx = boxOverride(MODEL_PROP, prop)) overrideBoxParts(MODEL_PROP, prop, *bx, pal, 1.0f, parts_);
    else propParts(prop, pal, parts_);
    for (const ModelPart& q : parts_) r_.boxPitched(shoulder, q.c * bs, q.h * bs, yaw, pitch, q.color, sv, q.shadow);
}

void Game::drawHookClaw(const Vec3& pivot, const Vec3& offset, float yaw, float pitch, float bs, float sv) {
    const ModelPalette& pal = referencePalette(MODEL_PROP, PROP_HOOK);   // (fixed steel: no slot follows anything)
    if (const ModelMesh* ov = modelOverride(MODEL_PROP, PROP_HOOK)) {
        Vec3 tint[TINT_COUNT];
        overrideTints(MODEL_PROP, PROP_HOOK, pal, tint);
        Mat4 sc;
        sc.m[0] = sc.m[5] = sc.m[10] = bs;
        r_.mesh(*ov, Mat4::translate(pivot) * Mat4::rotY(yaw) * Mat4::rotX(pitch) * Mat4::translate(offset) * sc, tint, sv, false);
        return;
    }
    if (const ModelParts* bx = boxOverride(MODEL_PROP, PROP_HOOK)) overrideBoxParts(MODEL_PROP, PROP_HOOK, *bx, pal, 1.0f, parts_);
    else propParts(PROP_HOOK, pal, parts_);
    for (const ModelPart& q : parts_) r_.boxPitched(pivot, offset + q.c * bs, q.h * bs, yaw, pitch, q.color, sv, q.shadow);
}

void Game::drawHeldWeapon(int i, int w, const Vec3& shoulder, float yaw, float pitch, float bs, const Vec3& accent, float sv) {
    if (w < 0 || w >= WPN_COUNT) w = WPN_KNIFE;
    float sw = (float)(time_ - knifeSlash_[i]) / 0.18f;   // a knife swing: the hand chops down and back up
    if (w == WPN_KNIFE && knifeSlash_[i] > 0 && sw < 1) pitch -= 0.7f * std::sin(sw * 3.14159f);
    drawArm(PROP_ARM, shoulder, yaw, pitch, bs, sv, kNoSlots);
    const Vec3 hand = Vec3(0, 0.02f, -0.5f) * bs;
    const float k = 2.0f * bs;   // the viewmodel's boxes at world size (a held weapon reads at a distance)
    ModelPalette pal;
    pal.c[TINT_METAL] = viewmodelMetal(r_.metalColor);
    pal.c[TINT_BASE] = {0.85f, 0.88f, 0.92f};   // bright steel (a blade has to read from across a room, on any map)
    pal.c[TINT_TEAM] = accent;
    pal.c[TINT_GLOW] = r_.glowColor;
    if (const ModelMesh* ov = modelOverride(MODEL_WEAPON, w)) {   // the owner's model (docs/ASSETS.md)
        Vec3 tint[TINT_COUNT];
        overrideTints(MODEL_WEAPON, w, pal, tint);
        Mat4 sc;
        sc.m[0] = sc.m[5] = sc.m[10] = k;
        r_.mesh(*ov, Mat4::translate(shoulder) * Mat4::rotY(yaw) * Mat4::rotX(pitch) * Mat4::translate(hand) * sc, tint, sv, true);
        return;
    }
    if (const ModelParts* bx = boxOverride(MODEL_WEAPON, w)) overrideBoxParts(MODEL_WEAPON, w, *bx, pal, 1.0f, parts_);
    else viewmodelParts(w, pal, parts_);
    for (const ModelPart& q : parts_) r_.boxPitched(shoulder, hand + q.c * k, q.h * k, yaw, pitch, q.color, sv);
}

void Game::drawBracket() {
    float s = ui_.s(), W = (float)r_.width(), H = (float)r_.height();
    int nr = (int)bracket_.size();
    float colW = std::min(220 * s, (W - 80 * s) / nr), boxH = 26 * s, gap = 14 * s;
    float totalW = nr * colW + (nr - 1) * gap;
    float x0 = (W - totalW) * 0.5f;
    int first = (int)bracket_[0].size();
    float areaH = std::min(H * 0.62f, first * (boxH + 8 * s));
    float y0 = H * 0.5f - areaH * 0.5f;
    ui_.panel(x0 - 24 * s, y0 - 56 * s, totalW + 48 * s, areaH + 80 * s, 0.85f);
    ui_.labelCentered(W * 0.5f, y0 - 44 * s, "TOURNEY BRACKET");
    float ts = std::max(1.0f, std::round(ui_.textScale() * 0.75f));
    const Color fc = rgba(cfg_.colorFriendly);
    for (int r = 0; r < nr; r++) {
        const auto& col = bracket_[r];
        int n = (int)col.size();
        float x = x0 + r * (colW + gap);
        static const char* names[] = {"FINAL", "SEMIFINALS", "QUARTERFINALS", "ROUND OF 16", "ROUND OF 32"};
        int fromEnd = nr - 2 - r;
        std::string head = r == nr - 1 ? "CHAMPION" : fromEnd >= 0 && fromEnd < 5 ? names[fromEnd] : "ROUND " + std::to_string(r + 1);
        r_.text(x, y0 - 22 * s, std::max(1.0f, ts * 0.75f), head, {0.6f, 0.62f, 0.66f, 1});
        for (int i = 0; i < n; i++) {
            float slot = areaH / n;
            float y = y0 + slot * i + (slot - boxH) * 0.5f;
            int id = col[i];
            int next = r + 1 < nr && i / 2 < (int)bracket_[r + 1].size() ? bracket_[r + 1][i / 2] : -2;   // who went through
            bool won = id >= 0 && next == id;
            bool lost = id >= 0 && next >= 0 && next != id;
            bool current = r == bracketRound_ && i / 2 == bracketMatch_ && net_.mode().phase != PHASE_MATCH_END;
            if (current) r_.rect(x - 2 * s, y - 2 * s, colW + 4 * s, boxH + 4 * s, fc);
            r_.rect(x, y, colW, boxH, won || (r == nr - 1 && id >= 0) ? Color{fc.r * 0.35f, fc.g * 0.35f, fc.b * 0.35f, 1} : Color{0.1f, 0.11f, 0.14f, 1});
            static const std::string kBye = "BYE", kNone;
            const std::string& t = id >= 0 ? bracketNames_[id] : id == -1 ? kBye : kNone;   // (no copy per slot per frame)
            Color tc = lost ? Color{0.45f, 0.45f, 0.5f, 1} : id == -1 ? Color{0.4f, 0.4f, 0.45f, 1} : Color{1, 1, 1, 1};
            r_.text(x + 8 * s, y + (boxH - r_.textHeight(ts)) * 0.5f, ts, t, tc);
            if (lost) r_.rect(x + 6 * s, y + boxH * 0.5f, std::min(colW - 12 * s, r_.textWidth(t, ts) + 4 * s), 1.5f * s, {0.6f, 0.3f, 0.3f, 1});
        }
    }
}

// Crosshair from the settings: style, size, thickness, gap, color, outline. A hit flashes it white.
// X5: a copy of the projectile the server will launch for this command, as it launches it (Server::fireWeapon): from
// 0.3 m in front of the eye, at the weapon's speed (a rocket lob: 70% and its drop; a thrown knife: its own).
void Game::spawnPredictedProjectile(int w, bool alt, const UserCmd& c) {
    const MoveState& m = net_.predicted();
    const Vec3 eye = m.pos + Vec3(0, ((m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
    const Vec3 aim = angleForward(c.yaw, c.pitch);
    const WeaponDef& wd = weaponDef(w);
    PredProj* slot = &predProj_[0];   // (a free one, else the oldest)
    for (auto& q : predProj_) { if (!q.live) { slot = &q; break; } if (q.born < slot->born) slot = &q; }
    PredProj q;
    q.type = (uint8_t)w;
    q.live = true;
    q.pos0 = q.pos = eye + aim * 0.3f;
    if (w == WPN_KNIFE) q.vel0 = aim * kKnifeThrowSpeed;
    else {
        const float spread = (wd.scopedSpread >= 0 && (c.buttons & BTN_ALTFIRE)) ? wd.scopedSpread : wd.spread;
        q.vel0 = spreadAim(aim, spread, 0, 1, localBurst_) * wd.projSpeed;
        if (w == WPN_ROCKET && alt) { q.vel0 = q.vel0 * kRocketLobSpeed; q.lob = true; }
    }
    q.born = time_;
    *slot = q;
}

// X4: your hitscan shot against the enemies as you see them: a spark where each pellet / bullet meets one in front of
// the wall (the server's word, the hit sound and number, comes a round trip later). Rays only against the few boxes
// in sight, once a shot (the laser: at most 20 a second). Players are their body and head (rayHitbox), as the server's.
void Game::predictShotGlass(int w, const UserCmd& c) {
    const MoveState& m = net_.predicted();
    const Vec3 eye = m.pos + Vec3(0, ((m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
    const Vec3 aim = angleForward(c.yaw, c.pitch);
    const WeaponDef& wd = weaponDef(w);
    const float spread = (wd.scopedSpread >= 0 && (c.buttons & BTN_ALTFIRE)) ? wd.scopedSpread : wd.spread;
    for (int k = 0; k < wd.pellets; k++) {
        const Vec3 d = spreadAim(aim, spread, k, wd.pellets, localBurst_);
        float bangT, t = net_.map().raycastBullet(eye, d, wd.range, &bangT);
        if (t < 0) continue;
        const int pane = net_.map().glassAt(eye + d * (t + 0.01f));
        if (pane >= 0) net_.predictGlass(pane, GLASS_SHOT, eye + d * t, d);
    }
}

void Game::updateDestruction(const Map& map) {
    // the holes, when they change (and a new map's: none)
    if (map.destructSerial != holeSerial_ || net_.mapRevision() != holeMapRev_) {
        holeSerial_ = map.destructSerial; holeMapRev_ = net_.mapRevision();
        r_.setHoles(map.holes);
    }
    // what broke since the last frame: shards, chunks, a sound
    net_.takeGlassFx(glassFxScratch_);
    for (const NetClient::GlassFx& fx : glassFxScratch_) {
        if (fx.hole && fx.how == 1) {   // a whole wall coming down: chunks all over it, dropping
            uint32_t sd = ((uint32_t)(fx.pane.mn.x * 97.0f) * 2654435761u ^ (uint32_t)(fx.pane.mn.z * 53.0f)) | 1u;
            const Vec3 ext = fx.pane.mx - fx.pane.mn;
            const float area = std::max(ext.x * ext.y, std::max(ext.y * ext.z, ext.x * ext.z));
            const int n = std::clamp((int)(area * 25.0f), 40, 400);
            for (int i = 0; i < n && visShards_.size() < 3000; i++) {
                const Vec3 p = {fx.pane.mn.x + ext.x * glassRandF(sd), fx.pane.mn.y + ext.y * glassRandF(sd), fx.pane.mn.z + ext.z * glassRandF(sd)};
                Vec3 v = p - fx.at;
                const float l = length(v);
                v = (l > 1e-3f ? v / l : Vec3(0, 1, 0)) * (1.0f + 2.0f * glassRandF(sd)) + Vec3(glassRandF(sd) - 0.5f, glassRandF(sd), glassRandF(sd) - 0.5f);
                const float sh = 0.8f + 0.3f * glassRandF(sd);
                visShards_.push_back({p, v, 0.04f + 0.05f * glassRandF(sd), Vec3(0.55f, 0.22f, 0.15f) * sh, false, false, time_});
            }
            audio_.play3D(SND_EXPLOSION, (fx.pane.mn + fx.pane.mx) * 0.5f, 0.6f, 0.6f);
            continue;
        }
        if (fx.hole) {   // a hole: brick-red chunks thrown out of it
            uint32_t sd = ((uint32_t)(fx.at.x * 131.0f) * 2654435761u ^ (uint32_t)(fx.at.z * 71.0f)) | 1u;
            for (int i = 0; i < 40 && visShards_.size() < 3000; i++) {
                const Vec3 v = {glassRandF(sd) * 8 - 4, glassRandF(sd) * 6 - 1, glassRandF(sd) * 8 - 4};
                const float sh = 0.8f + 0.3f * glassRandF(sd);
                visShards_.push_back({fx.at + v * 0.08f, v, 0.03f + 0.04f * glassRandF(sd), Vec3(0.55f, 0.22f, 0.15f) * sh, false, false, time_});
            }
            continue;
        }
        glassShards(fx.pane, fx.how, fx.at, fx.dir, fx.seed, glassShardScratch_);
        uint32_t sd = fx.seed ^ 0x9E3779B9u;
        for (const GlassShard& g : glassShardScratch_) {
            if (visShards_.size() >= 3000) break;
            visShards_.push_back({g.pos, g.vel, g.size, {0.86f, 0.90f, 0.93f}, false, true, time_, glassThinAxis(fx.pane)});
        }
        if (glassCrater(fx.how)) {   // (the crater: a puff of glitter where it was hit, only drawn)
            for (int i = 0; i < 12 && visShards_.size() < 3000; i++) {
                const Vec3 v = fx.dir * (1.0f + 3.0f * glassRandF(sd)) + Vec3(glassRandF(sd) - 0.5f, glassRandF(sd) - 0.3f, glassRandF(sd) - 0.5f) * 3.0f;
                visShards_.push_back({fx.at, v, 0.008f + 0.012f * glassRandF(sd), {0.86f, 0.90f, 0.93f}, false, true, time_});
            }
        }
        audio_.play3D(SND_BOLT_HIT, fx.at, glassCrater(fx.how) ? 0.8f : 1.0f, glassCrater(fx.how) ? 1.8f : 1.5f);
    }
    // the panes still standing: see-through (the bake leaves glass out)
    for (uint32_t i : map.glass) if (!map.brushes[i].gone) r_.translucentBox(map.brushes[i].box, kGlassTint);
    for (uint32_t i : map.windows) r_.translucentBox(map.brushes[i].box, kGlassTint);   // (plain glass: never breaks)
    // shards fly, land and lie; one out of view is gone
    const float dt = shardsAt_ < 0 ? 0.0f : (float)std::min(0.05, time_ - shardsAt_);
    shardsAt_ = time_;
    if (visShards_.empty()) return;
    const float W = (float)r_.width(), H = (float)r_.height();
    size_t keep = 0;
    for (size_t k = 0; k < visShards_.size(); k++) {
        VisShard v = visShards_[k];
        if (time_ - v.born > 30.0) continue;
        if (!v.rest && dt > 0) {
            v.vel.y -= 22.0f * dt;
            const Vec3 step = v.vel * dt;
            const float len = length(step);
            if (len > 1e-6f) {
                Vec3 n;
                const float t = map.raycast(v.pos, step / len, len, &n);
                if (t < 0) v.pos += step;
                else {
                    v.pos += step / len * std::max(0.0f, t - 0.005f);
                    if (n.y > 0.5f) v.rest = true;   // (on a floor: it lies there)
                    else v.vel = (v.vel - n * (2.0f * dot(v.vel, n))) * 0.25f;   // (off a wall, falling on)
                }
            }
        }
        float sx, sy;
        if (time_ - v.born > 0.05 && (!r_.project(v.pos, sx, sy) || sx < -20 || sy < -20 || sx > W + 20 || sy > H + 20)) continue;
        Vec3 h(v.size, v.rest ? v.size * 0.3f : v.size, v.size);
        if (v.thin >= 0) { h = Vec3(v.size, v.size, v.size); h[v.rest ? 1 : v.thin] = 0.015f; }   // (a plate of glass)
        if (v.glass) r_.translucentBox({v.pos - h, v.pos + h}, {v.color.x, v.color.y, v.color.z, 0.55f});
        else r_.box(v.pos, h, (float)k, v.color, 1.0f, false);
        visShards_[keep++] = v;
    }
    visShards_.resize(keep);
}

void Game::localImpactSparks(int w, const UserCmd& c) {
    const MoveState& m = net_.predicted();
    const Vec3 eye = m.pos + Vec3(0, ((m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
    const Vec3 aim = angleForward(c.yaw, c.pitch);
    const WeaponDef& wd = weaponDef(w);
    const float spread = (wd.scopedSpread >= 0 && (c.buttons & BTN_ALTFIRE)) ? wd.scopedSpread : wd.spread;
    const auto& ps = framePlayers();
    const int me = net_.clientId();
    bool any = false;
    for (int k = 0; k < wd.pellets; k++) {
        const Vec3 d = spreadAim(aim, spread, k, wd.pellets, localBurst_);
        float bangT, t = net_.map().raycastBullet(eye, d, wd.range, &bangT);
        if (t < 0) t = wd.range;
        const Vec3 inv = safeInv(d);
        float best = t;
        for (int i = 0; i < kMaxPlayers; i++) {
            if (i == me || !ps[i].valid || !(ps[i].snap.flags & PS_ALIVE) || (ps[i].snap.flags & PS_SPECTATOR) || isFriendly(i)) continue;
            const float tk = rayHitbox(eye, inv, playerBox(ps[i].snap.pos, (ps[i].snap.flags & PS_CROUCHED) != 0, bodyScale()), best);
            if (tk >= 0 && tk < best) best = tk;
        }
        if (best < t) explosions_.push_back({eye + d * best, time_, 0.1f, false, false, true});
        else if (t < wd.range) explosions_.push_back({eye + d * (t - 0.03f), time_, 0.06f, false, false, false, true, 0.2f});   // a wall
        else continue;
        if (bangT >= 0 && bangT < best) explosions_.push_back({eye + d * bangT, time_, 0.05f, false, false, false, true, 0.2f});   // (through a wallbang)
        any = true;
    }
    if (any) nextLocalSpark_ = time_ + 0.05;
}

// Someone else's hitscan shot as you see it: a spark on the first player in its way (not the shooter, their teammates
// or you), else on the wall it ends at. Cosmetic only (the server decides hits). wallT < 0: it hit nothing within range.
bool Game::shotSpark(const Vec3& from, const Vec3& d, float wallT, float range, int shooter) {
    const auto& ps = framePlayers();
    const int me = net_.clientId();
    const PlayerSnap* sh = net_.findPlayer(shooter);
    const bool teams = sh && modeIsTeamBased(net_.mode().mode);
    const Vec3 inv = safeInv(d);
    float best = wallT < 0 ? range : wallT;
    bool player = false;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == me || i == shooter || !ps[i].valid || !(ps[i].snap.flags & PS_ALIVE) || (ps[i].snap.flags & PS_SPECTATOR)) continue;
        if (teams && ps[i].snap.team() == sh->team()) continue;
        const float tk = rayHitbox(from, inv, playerBox(ps[i].snap.pos, (ps[i].snap.flags & PS_CROUCHED) != 0, bodyScale()), best);
        if (tk >= 0 && tk < best) { best = tk; player = true; }
    }
    if (player) explosions_.push_back({from + d * best, time_, 0.1f, false, false, true});
    else if (wallT >= 0) explosions_.push_back({from + d * (wallT - 0.03f), time_, 0.06f, false, false, false, true, 0.2f});
    else return false;
    return true;
}

// One of your projectiles hits on your screen now, not a round trip later: its explosion / pop / bolt hit, as the
// server's would look (that one is then left out: see the vanish check in frame()).
void Game::localBoom(uint16_t id, int type, const Vec3& at) {
    const int pack = net_.playerSoundPack(net_.clientId());
    if (type == WPN_BUBBLE) { explosions_.push_back({at, time_, 0.45f, true}); audio_.play3D(SND_BUBBLE, at, 0.7f, 1.7f, pack); }
    else if (weaponDef(type).splashRadius > 0) { explosions_.push_back({at, time_}); audio_.play3D(SND_EXPLOSION, at, 1.25f, 1.0f, pack); }
    else if (type == WPN_CROSSBOW) {
        audio_.play3D(SND_BOLT_HIT, at, 0.8f, 1.0f, pack);
        if (cfg_.impactSparks) explosions_.push_back({at, time_, 0.08f, false, false, false, true, 0.2f});
    } else return;
    localBooms_.push_back({id, (uint8_t)type, at, time_});
}

// The nearest enemy (alive, as drawn) whose body or head (rayHitbox, as the server's projectiles) the segment a -> b
// passes through: *t its fraction along it.
bool Game::enemyOnSegment(const Vec3& a, const Vec3& b, float* t, int* who, bool* head) {
    const Vec3 d = b - a;
    const float len = length(d);
    if (len < 1e-4f) return false;
    const Vec3 dir = d / len, inv = safeInv(dir);
    const auto& ps = framePlayers();
    float best = len;
    bool any = false;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == net_.clientId() || !ps[i].valid || !(ps[i].snap.flags & PS_ALIVE) || (ps[i].snap.flags & PS_SPECTATOR) || isFriendly(i)) continue;
        bool h = false;
        const float tk = rayHitbox(a, inv, playerBox(ps[i].snap.pos, (ps[i].snap.flags & PS_CROUCHED) != 0, bodyScale()), best, &h);
        if (tk >= 0 && tk < best) { best = tk; any = true; if (who) *who = i; if (head) *head = h; }
    }
    if (any) *t = best / len;
    return any;
}

// ---- your hits, as you saw them (owner, 2026-10-28: "all damage should be client side also, so when you have high
// ping and shoot a projectile it should not magically not do any damage"). The server counts them unless you're dead
// there by the time they arrive. These mirror the server's own hit tests (Server::hitscan, explode, stepProjectile)
// against the players as drawn (framePlayers).

void Game::sendHits(int weapon, bool alt, const std::vector<HitReport>& hits) {
    if (hits.empty() || net_.playback()) return;
    std::string s = "hit " + std::to_string(weapon) + (alt ? " 1 " : " 0 ") + std::to_string(hits.size());
    char buf[160];
    for (const HitReport& h : hits) {
        std::snprintf(buf, sizeof buf, " %d %.2f %d %d %.3f %.2f %.3f %.3f %.3f", h.victim, h.dmg, h.head ? 1 : 0, h.direct ? 1 : 0, h.splash,
                      h.dist, h.knock.x, h.knock.y, h.knock.z);
        s += buf;
    }
    net_.sendCommand(s);
}

void Game::reportShotHits(int w, const UserCmd& c) {
    const MoveState& m = net_.predicted();
    const Vec3 eye = m.pos + Vec3(0, ((m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
    const Vec3 aim = angleForward(c.yaw, c.pitch);
    const WeaponDef& wd = weaponDef(w);
    const WeaponAlt& wa = weaponAlt();
    const WeaponExtra& ex = weaponExtra(w);
    const bool altDown = (c.buttons & BTN_ALTFIRE) != 0;
    const float spread = (wd.scopedSpread >= 0 && altDown) ? wd.scopedSpread : wd.spread;
    const bool pull = wd.beam && altDown, pierce = w == WPN_SNIPER;
    const float dmg = wd.damage * (pull ? wa.pullDamage : 1.0f), headDmg = pull ? 0.0f : ex.headDamage;
    const float grow = w == WPN_KNIFE ? wa.knifeGrow : 0.0f;
    const auto& ps = framePlayers();
    const int me = net_.clientId();
    float dmgPer[kMaxPlayers] = {}, headPer[kMaxPlayers] = {}, distOf[kMaxPlayers] = {};
    bool headHit[kMaxPlayers] = {};
    for (int k = 0; k < wd.pellets; k++) {
        const Vec3 d = spreadAim(aim, spread, k, wd.pellets, localBurst_);
        float bang, wall = net_.map().raycastBullet(eye, d, wd.range, &bang);
        float best = wall >= 0 ? wall : wd.range;
        const Vec3 inv = safeInv(d);
        int hit = -1;
        bool hitHead = false;
        auto add = [&](int i, float t, bool inHead) {
            float dd = dmg;
            const bool head = headDmg > 0 && inHead;
            if (head) { dd = std::max(dd, headDmg); headHit[i] = true; }
            if (ex.falloff) dd *= weaponFalloff(ex, t);
            if (bang >= 0 && t > bang) dd *= ex.wallbang;
            distOf[i] = t;
            dmgPer[i] += dd;
            if (head) headPer[i] += dd;
        };
        for (int i = 0; i < kMaxPlayers; i++) {
            if (i == me || !ps[i].valid || !(ps[i].snap.flags & PS_ALIVE) || (ps[i].snap.flags & PS_SPECTATOR)) continue;
            bool inHead = false;
            const float t = rayHitbox(eye, inv, playerBox(ps[i].snap.pos, (ps[i].snap.flags & PS_CROUCHED) != 0, bodyScale()), best, &inHead, grow);
            if (pierce) { if (t >= 0) add(i, t, inHead); continue; }   // (sniper collateral: everyone up to the wall)
            if (t >= 0 && t < best) { best = t; hit = i; hitHead = inHead; }
        }
        if (hit >= 0) add(hit, best, hitHead);
    }
    if (wd.pellets > 1)   // (a blast is a headshot when half its damage was to the head, as the server's F14)
        for (int i = 0; i < kMaxPlayers; i++) headHit[i] = dmgPer[i] > 0 && headPer[i] * 2 >= dmgPer[i];
    if (wd.beam) {   // the laser: added up, sent every 50 ms (and when it stops or the mode changes)
        if (laserPending_ && laserAlt_ != pull) flushLaserHits(true);
        for (int i = 0; i < kMaxPlayers; i++) {
            if (dmgPer[i] <= 0) continue;
            laserDmg_[i] += dmgPer[i];
            laserKnock_[i] += aim * (ex.knock * wd.refire * (pull ? -wa.pullScale : 1.0f));
            laserDist_[i] = distOf[i];
            laserHead_[i] |= headHit[i];
            if (!laserPending_) { laserPending_ = true; laserSince_ = time_; }
            laserAlt_ = pull;
        }
        flushLaserHits(false);
        return;
    }
    hitScratch_.clear();
    for (int i = 0; i < kMaxPlayers; i++) {
        if (dmgPer[i] <= 0) continue;
        const Vec3 knock = aim * (wd.pellets > 1 ? ex.knock * dmgPer[i] / wd.damage : ex.knock);
        hitScratch_.push_back({i, dmgPer[i], headHit[i], false, 1.0f, distOf[i], knock});
    }
    sendHits(w, altDown, hitScratch_);
}

void Game::flushLaserHits(bool force) {
    if (!laserPending_ || (!force && time_ - laserSince_ < 0.05)) return;
    hitScratch_.clear();
    for (int i = 0; i < kMaxPlayers; i++) {
        if (laserDmg_[i] > 0) hitScratch_.push_back({i, laserDmg_[i], laserHead_[i], false, 1.0f, laserDist_[i], laserKnock_[i]});
        laserDmg_[i] = 0; laserHead_[i] = false; laserKnock_[i] = {};
    }
    laserPending_ = false;
    sendHits(WPN_LASER, laserAlt_, hitScratch_);
}

// Your rocket's blast on the others as you see them (the server's explode; on you it's the server's own, so rocket
// jumps feel as they did). direct: the player it flew into, -1 none.
void Game::reportBlast(int type, bool alt, const Vec3& at, int direct) {
    const WeaponDef& wd = weaponDef(type);
    const WeaponExtra& ex = weaponExtra(type);
    const auto& ps = framePlayers();
    const int me = net_.clientId();
    const MoveState& m = net_.predicted();
    const float ownerDist = length(at - (m.pos + Vec3(0, ((m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0)));
    const Map& map = net_.map();
    hitScratch_.clear();
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == me || !ps[i].valid || !(ps[i].snap.flags & PS_ALIVE) || (ps[i].snap.flags & PS_SPECTATOR)) continue;
        const AABB b = playerBox(ps[i].snap.pos, (ps[i].snap.flags & PS_CROUCHED) != 0, bodyScale());
        const Vec3 closest = hitboxClosest(b, at);
        const float d = length(closest - at);
        if (d > wd.splashRadius) continue;
        const Vec3 c = b.center(), to = c - at;
        const float dist = length(to);
        if (dist > 0.01f) {
            const float w = map.raycast(at, to / dist, dist);
            if (w >= 0 && w < dist - 0.1f) {
                auto clear = [&](const Vec3& pt) {
                    const Vec3 v = pt - at;
                    const float len = length(v);
                    return len < 0.11f || !map.raycastAny(at, v / len, len - 0.1f);
                };
                if (!clear(closest) && !clear(Vec3(c.x, b.mx.y - 0.15f * (b.mx.y - b.mn.y), c.z))) continue;
            }
        }
        const float frac = 1.0f - d / wd.splashRadius;
        const Vec3 push = dist > 0.01f ? to / dist : Vec3(0, 1, 0);
        float dmg = wd.damage * frac;
        if (i == direct && ex.directDamage > 0) dmg = std::max(dmg, ex.directDamage);
        if (ex.falloff) dmg *= weaponFalloff(ex, ownerDist);
        Vec3 knock = push * (ex.knock * frac);
        knock.y *= 2.0f;
        hitScratch_.push_back({i, dmg, false, i == direct, frac, ownerDist, knock});
    }
    sendHits(type, alt, hitScratch_);
}

// Your bolt in someone, as you see it (the server's stepProjectile: direct hits only).
void Game::reportBolt(const Vec3& at, const Vec3& dir, int victim, bool head) {
    if (victim < 0) return;
    const WeaponDef& wd = weaponDef(WPN_CROSSBOW);
    const WeaponExtra& ex = weaponExtra(WPN_CROSSBOW);
    float dmg = (float)wd.damage;
    const float hd = headDamage(WPN_CROSSBOW);
    head = head && hd > 0;
    if (head) dmg = std::max(dmg, hd);
    const MoveState& m = net_.predicted();
    const float dist = length(at - (m.pos + Vec3(0, ((m.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0)));
    if (ex.falloff) dmg *= weaponFalloff(ex, dist);
    hitScratch_.clear();
    hitScratch_.push_back({victim, dmg, head, true, 1.0f, dist, dir * ex.knock});
    sendHits(WPN_CROSSBOW, false, hitScratch_);
}

void Game::reportImpact(int type, bool alt, const Vec3& from, const Vec3& to, int victim, bool head) {
    if (!net_.selfAlive()) return;   // (dead: the server's own hit, as it lands there)
    if (type == WPN_ROCKET) reportBlast(type, alt, to, victim);
    else if (type == WPN_CROSSBOW) {
        const Vec3 d = to - from;
        const float len = length(d);
        reportBolt(to, len > 1e-4f ? d / len : Vec3(0, 0, -1), victim, head);
    }
}

// A muzzle flash: a hot core in a bigger, fainter glow (two see-through boxes), fading over its life (a 0..1).
// The laser's is small and in its beam's colour.
void Game::drawMuzzleFlash(const Vec3& at, int w, const Vec3& beam, float a, float scale) {
    const bool laser = w == WPN_LASER;
    const float c = (laser ? 0.03f : 0.045f) * scale, g = (laser ? 0.05f : 0.1f) * scale;
    const Color core = laser ? rgba(beam, 0.9f * a) : Color{1.0f, 0.95f, 0.75f, 0.9f * a};
    const Color glow = laser ? rgba(beam, 0.35f * a) : Color{1.0f, 0.6f, 0.2f, 0.4f * a};
    r_.translucentBox({at - Vec3(g, g, g), at + Vec3(g, g, g)}, glow);
    r_.translucentBox({at - Vec3(c, c, c), at + Vec3(c, c, c)}, core);
}

// Where the hits you took came from (X3): a red arc round the crosshair toward each attacker (as they stood when it
// hit), for kDmgDirTime, fading. A few dozen small rects while one shows; nothing otherwise.
void Game::drawDamageArcs(float cx, float cy, float scale) {
    const Vec3 me = net_.predicted().pos;
    const Vec3 fwd = {-std::sin(yaw_), 0, -std::cos(yaw_)}, right = {std::cos(yaw_), 0, -std::sin(yaw_)};
    const float rad = 70 * scale, px = std::max(3.0f, std::round(5 * scale));
    for (const DmgDir& d : dmgDirs_) {
        const double age = time_ - d.at;
        if (d.at <= 0 || age > kDmgDirTime) continue;
        const Vec3 to = d.pos - me;
        if (to.x * to.x + to.z * to.z < 0.01f) continue;   // (on top of you: no direction)
        const float th = std::atan2(dot(to, right), dot(to, fwd));   // 0: ahead (up on screen), + to the right
        const float a = (float)(1.0 - age / kDmgDirTime);
        for (int k = -6; k <= 6; k++) {
            const float t = th + k * 0.06f;
            const float w = 1.0f - std::fabs((float)k) / 8.0f;   // (thicker in the middle)
            r_.rect(cx + std::sin(t) * rad - px * 0.5f, cy - std::cos(t) * rad - px * 0.5f, px, px * (0.6f + w), Color{1, 0.12f, 0.08f, a * w});
        }
    }
}

void Game::drawCrosshair(float cx, float cy, float scale, bool hit) {
    float u = std::max(1.0f, std::round(scale));
    float t = std::max(1.0f, std::round(cfg_.chThickness * u)), len = std::round(cfg_.chLength * u), g = std::round(cfg_.chGap * u);
    hit = hit && cfg_.chHitFlash;   // (settings: HIT FLASH off, or its colour)
    Color c = hit ? rgba(cfg_.chHitColor) : rgba(cfg_.chTeamColor ? cfg_.colorFriendly : cfg_.chColor);
    cx = std::round(cx); cy = std::round(cy);
    struct Rects {   // x, y, w, h: at most 25 (the ring), on the stack (this runs every frame)
        std::array<float, 4> r[25]; int n = 0;
        void push_back(const std::array<float, 4>& v) { r[n++] = v; }
        const std::array<float, 4>* begin() const { return r; }
        const std::array<float, 4>* end() const { return r + n; }
    } rs;
    int st = cfg_.chStyle;
    if (st == 0 || st == 1 || st == 4) {
        if (len > 0) {
            if (st != 4) rs.push_back({cx - t * 0.5f, cy - g - len, t, len});        // up (not on T)
            rs.push_back({cx - t * 0.5f, cy + g, t, len});                           // down
            rs.push_back({cx - g - len, cy - t * 0.5f, len, t});                     // left
            rs.push_back({cx + g, cy - t * 0.5f, len, t});                           // right
        }
    }
    if (st == 1 || st == 2) rs.push_back({cx - t * 0.5f, cy - t * 0.5f, t, t});
    if (st == 3) { // ring of small blocks
        float R = g + len * 0.5f + t;
        static const auto ring = [] {   // (the 24 directions, worked out once)
            std::array<std::array<float, 2>, 24> d{};
            for (int i = 0; i < 24; i++) { float a = i * 2 * PI / 24; d[i] = {std::cos(a), std::sin(a)}; }
            return d;
        }();
        for (int i = 0; i < 24; i++) rs.push_back({cx + ring[i][0] * R - t * 0.5f, cy + ring[i][1] * R - t * 0.5f, t, t});
        rs.push_back({cx - t * 0.5f, cy - t * 0.5f, t, t});
    }
    if (cfg_.chOutline)
        for (auto& r : rs) r_.rect(r[0] - 1, r[1] - 1, r[2] + 2, r[3] + 2, {0, 0, 0, c.a * 0.9f});
    for (auto& r : rs) r_.rect(r[0], r[1], r[2], r[3], c);
    if (hit && st != 1 && st != 2) r_.rect(cx - t, cy - t, 2 * t, 2 * t, c);
}

// X6: your kill: a red X round the crosshair, bigger than the hit flash, fading (squares along the diagonals). Drawn
// scoped too.
void Game::drawKillMark(float cx, float cy, float scale) {
    if (time_ >= killMarkUntil_) return;
    float u = std::max(1.0f, std::round(scale));
    float len = std::round(cfg_.chLength * u), g = std::round(cfg_.chGap * u);
    cx = std::round(cx); cy = std::round(cy);
    {
        const float a = (float)std::min(1.0, (killMarkUntil_ - time_) / 0.2);
        const float d = std::max(2.0f, std::round(2 * u)), r0 = g + 3 * u, r1 = g + len + 10 * u;
        for (float r = r0; r <= r1; r += d * 0.75f)
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) r_.rect(cx + sx * r * 0.7071f - d * 0.5f, cy + sy * r * 0.7071f - d * 0.5f, d, d, {1.0f, 0.12f, 0.1f, a});
    }
}

// VISUALS: what's drawn (world look, shot effects, overlays)
// FONT: the UI font (uifonts.h). Built-ins first, then mods/fonts; a click builds that font's atlas once and saves it.
bool Game::loadUiFont(const std::string& id, std::string* err, std::string* name) {
    std::unique_ptr<FontAtlas> fa;
    const double t0 = timeNow();
    if (!uifonts::load(id, Renderer::uiFontLayout(), fa, err, name)) return false;
    if (fa) printf("font: %s built in %.0f ms\n", name ? name->c_str() : id.c_str(), (timeNow() - t0) * 1000);
    r_.setUiFont(std::move(fa));   // (null: the baked default)
    return true;
}

void Game::applyUiFontSetting() {
    fontTried_ = cfg_.uiFont;
    std::string err, name;
    if (loadUiFont(cfg_.uiFont, &err, &name)) return;
    r_.setUiFont(nullptr);   // (Rubik; the setting is kept, so the font is back once its file is)
    notify("FONT " + upper(name) + ": " + upper(err) + " - USING RUBIK", 6);
}

// Footsteps, jumps, landings, dash and grapple for you; footsteps, gunfire and grapples for
// everyone else (positioned in 3D).
void Game::updateSounds(const NetClient::PlayerArray& players) {
    QZONE("updateSounds");
    const int me = net_.clientId();
    bool alive = net_.selfAlive();
    const MoveState& m = net_.predicted();
    const double frameDt = time_ - prevSoundAt_;
    prevSoundAt_ = time_;
    if (alive && prevAlive_) {
        bool now = m.flags & MF_ONGROUND;
        Vec3 d = m.pos - prevMove_.pos;
        float h = std::sqrt(d.x * d.x + d.z * d.z);
        if (now && h < 2.0f) {
            stepDist_ += h;
            if (stepDist_ > 2.4f) { stepDist_ = 0; audio_.play(SND_STEP, 0.55f, 0.9f + (qrand() % 20) * 0.01f); }
        }
    }
    {   // jumps, landings, dashes and the grapple from what each tick's movement did (pmove's ME_*): comparing the state once
        // a frame missed a jump and its landing in one tick (bhops: more of them silent the higher the fps) and a dash
        // with its dash jump in one frame (a silent super or hyper)
        float land = 0;
        const uint8_t ev = net_.takeMoveEvents(&land);
        if (alive && prevAlive_ && ev) {
            if (ev & ME_LAND) audio_.play(SND_LAND, clampf(land / 20.0f, 0.3f, 1.0f));
            if (ev & ME_JUMP) audio_.play(SND_JUMP, 0.5f);
            if (ev & ME_DASH) audio_.play(SND_DASH, 0.8f);
            if (ev & (ME_SUPER | ME_HYPER)) audio_.play((ev & ME_HYPER) ? SND_HYPER : SND_SUPER, 0.9f);
            if (ev & ME_HOOK) {
                audio_.play(SND_GRAPPLE_FIRE, 0.7f);
                audio_.play3D(SND_GRAPPLE_HIT, m.hookPoint, 0.9f);
            }
        }
        if (net_.playback() && alive && prevAlive_ && (m.flags & MF_HOOKED) && !(prevMove_.flags & MF_HOOKED)) {   // (a demo: no ME_HOOK)
            audio_.play(SND_GRAPPLE_FIRE, 0.7f);
            audio_.play3D(SND_GRAPPLE_HIT, m.hookPoint, 0.9f);
        }
    }
    prevMove_ = m;
    prevAlive_ = alive;
    {   // race practice: its checkpoint dings (the server sends a split only in the race itself; modecalls.h)
        const ModeState& ms = net_.mode();
        const PlayerSnap* mine = net_.findPlayer(me);
        const auto& cps = net_.map().checkpoints;
        if (isRaceMode(ms.mode) && ms.phase == PHASE_WARMUP && alive && mine && !cps.empty()) {
            const int next = mine->checkpoint;
            const bool in = next < (int)cps.size() && playerBox(m).overlaps(cps[next]);
            if (int d = practiceDing_.step(next, in, (int)cps.size())) audio_.play(d == 2 ? SND_FINISH : SND_CHECKPOINT, 0.9f);
        } else practiceDing_.reset();
    }

    for (int i = 0; i < kMaxPlayers; i++) {
        RemoteAudio& ra = remoteAudio_[i];
        if (i == me && net_.playback() && players[i].valid && (players[i].snap.flags & PS_ALIVE)) {
            // a demo: the recorded player's shots (theirs came from their input, which a demo hasn't), as your own
            // sound (2D, the same levels) on each firing edge
            const PlayerSnap& p = players[i].snap;
            const bool firing = (p.flags & PS_FIRING) != 0;
            if (firing && p.weapon == WPN_LASER && time_ >= ra.nextLaser) {
                audio_.play(SND_LASER, 0.6f, 0.97f + (qrand() % 7) * 0.01f, net_.playerSoundPack(i));
                ra.nextLaser = time_ + 0.05;
            } else if (firing && !ra.firing && p.weapon != WPN_LASER && p.weapon < WPN_LOCATOR && time_ >= ra.nextShot) {
                ra.nextShot = time_ + weaponDef(p.weapon).refire * 0.6f;
                audio_.play(SND_RIFLE + p.weapon, 0.8f, 0.97f + (qrand() % 7) * 0.01f, net_.playerSoundPack(i));
            }
            ra.firing = firing;
            ra.valid = false;   // (their steps, hurt and grapple: your own sounds' code plays those)
            continue;
        }
        if (i == me || !players[i].valid || !(players[i].snap.flags & PS_ALIVE)) { ra.valid = false; ra.firing = false; continue; }
        const PlayerSnap& p = players[i].snap;
        int sp = net_.playerSoundPack(i); // their sounds in the pack they picked
        if (ra.valid) {
            Vec3 d = p.pos - ra.pos;
            float h = std::sqrt(d.x * d.x + d.z * d.z);
            // walking on the ground: by speed, not per frame (0.05 m a frame let anything slower than 200 m/s up or down
            // count at 4000 fps: jumping, falling and grappling enemies made footsteps in the air; 3 m/s is the old
            // threshold at 60 fps; the 2 m a frame cap stays a teleport guard)
            // On stairs the climb is faster than that: then (rising or falling no steeper than the stairs, 45 deg) a short
            // probe under their feet says whether they're on a step (F23: stairs were silent). The probe only runs then.
            const float vy = std::fabs(d.y), flat = 3.0f * (float)frameDt;
            const bool onFloor = vy < flat ||
                (vy < h * 1.1f + flat && net_.map().sweepAxis(playerBox(p.pos, (p.flags & PS_CROUCHED) != 0, bodyScale()), 1, -0.2f) > -0.199f);
            if (h < 2.0f && onFloor) {
                ra.stepDist += h;
                if (ra.stepDist > 2.4f) { ra.stepDist = 0; audio_.play3D(SND_STEP, p.pos, 0.8f, 0.9f + (qrand() % 20) * 0.01f, sp); }
            }
            if (p.health < ra.health && time_ >= ra.nextHurt) { // their hurt sound, at most 5 a second (beams, drain)
                ra.nextHurt = time_ + 0.2;
                audio_.play3D(SND_HURT, p.pos + Vec3(0, 1.2f, 0), 0.8f, 1.0f, sp);
            }
            if (p.hooked && !ra.hooked) audio_.play3D(SND_GRAPPLE_HIT, p.hookPoint, 0.9f, 1.0f, sp);
            bool firing = (p.flags & PS_FIRING) != 0;
            if (firing && p.weapon == WPN_LASER && time_ >= ra.nextLaser) {
                audio_.play3D(SND_LASER, p.pos + Vec3(0, 1.5f, 0), 0.7f, 1.0f, sp);
                ra.nextLaser = time_ + 0.05;
            } else if (firing && !remoteFiring_[i] && p.weapon != WPN_LASER && p.weapon < WPN_LOCATOR && time_ >= ra.nextShot) {   // (the locator's ping: EV_SOUND)
                ra.nextShot = time_ + weaponDef(p.weapon).refire * 0.6f; // (a flag flicker between snapshots isn't a second shot)
                audio_.play3D(SND_RIFLE + p.weapon, p.pos + Vec3(0, 1.5f, 0), 1.0f, 0.97f + (qrand() % 7) * 0.01f, sp);
            }
        }
        ra.pos = p.pos;
        ra.hooked = p.hooked;
        ra.health = p.health;
        ra.valid = true;
    }
    // everyone else's jumps, landings and dashes (the server's KEY_EV_* bits), each as that player is drawn where it
    // happened: in the air they were silent (holding jump is the best way to move, so movers made no sound at all)
    NetClient::RemoteMoveEvent e;
    while (net_.nextRemoteMoveEvent(net_.renderTime(), &e)) {
        if (e.id >= kMaxPlayers || !players[e.id].valid || !(players[e.id].snap.flags & PS_ALIVE)) continue;
        const uint16_t ev = (uint16_t)(e.ev >> kKeyEvShift);
        const bool self = e.id == me;   // (a demo: the recorded player, whose view this is)
        const Vec3 at = players[e.id].snap.pos;
        const int sp = net_.playerSoundPack(e.id);
        auto play = [&](int snd, float vol) { if (self) audio_.play(snd, vol * 0.6f, 1.0f, sp); else audio_.play3D(snd, at, vol, 1.0f, sp); };
        if (ev & ME_LAND) play(SND_LAND, (e.ev & KEY_EV_LAND_HARD) ? 1.0f : 0.6f);
        if (ev & ME_JUMP) play(SND_JUMP, 0.8f);
        if (ev & ME_DASH) play(SND_DASH, 1.0f);
        if (ev & (ME_SUPER | ME_HYPER)) play((ev & ME_HYPER) ? SND_HYPER : SND_SUPER, 1.0f);
    }
}

// Orbit frame for a backdrop map: centred on the playable floor (pits and shafts reach far below it).
void Game::setBackdropFrame(const Map& m) {
    const AABB& b = m.bounds;
    float floorY = std::max(b.mn.y, -1.0f);
    bgIndoor_ = m.indoor;
    bgCeil_ = m.ceilingY;
    bgHalfX_ = (b.mx.x - b.mn.x) * 0.5f; bgHalfZ_ = (b.mx.z - b.mn.z) * 0.5f;
    bgCenter_ = {(b.mn.x + b.mx.x) * 0.5f, (floorY + b.mx.y) * 0.5f, (b.mn.z + b.mx.z) * 0.5f};
    bgRadius_ = length(Vec3(b.mx.x - b.mn.x, b.mx.y - floorY, b.mx.z - b.mn.z)) * 0.26f;
    bgTop_ = b.mx.y;
}

// --bench SECONDS: frame times while playing (after 3 s to settle), then print avg / 1% low / 0.1% low
// FPS and the worst frame, and quit. The same scene every run, so changes can be compared.
void Game::benchFrame(double dt) {
    if (benchStart_ < 0) { benchStart_ = time_; benchTimes_.reserve(100000); return; }
    if (time_ - benchStart_ < 3.0) return;
    benchTimes_.push_back((float)dt);
    benchCpu_.push_back((float)lastCpu_);
    if (time_ - benchStart_ < 3.0 + opts_.bench) return;
    std::vector<float> scratch;
    const bench::Stats st = bench::compute(benchTimes_.data(), benchTimes_.size(), scratch);   // (BENCHMARK's maths)
    std::vector<float> c = benchCpu_;
    std::sort(c.begin(), c.end());
    double csum = 0;
    for (float f : c) csum += f;
    printf("BENCH %s: %zu frames  avg %.1f fps  1%% low %.1f  0.1%% low %.1f  worst %.2f ms  |  cpu avg %.3f ms  99%% %.3f ms  99.9%% %.3f ms\n",
           net_.map().name.c_str(), st.frames, st.avgFps, st.low1, st.low01, st.worstMs,
           csum / c.size() * 1000.0, c[std::min(c.size() - 1, (size_t)(c.size() * 0.99))] * 1000.0, c[std::min(c.size() - 1, (size_t)(c.size() * 0.999))] * 1000.0);
    qzoneDump();
    fflush(stdout);
    quit_ = true;
}
