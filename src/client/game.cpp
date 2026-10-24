#include <fstream>
#include "../common/u8file.h"
#include "../common/rng.h"
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <sstream>
#include "../common/profile.h"
#include <array>
#include "game.h"
#include "complete.h"
#include "openurl.h"
#include "profiles.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include "../game/demo.h"
#include "../game/gamemodes.h"
#include "../common/version.h"
#include "gltf.h"
#include "overrides.h"
#include "hudclock.h"

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

const char* weaponName(int w) {
    if (w == kDamageGrapple) return "grapple";
    if (w == 252) return "fell";
    if (w == 253) return "bomb";
    if (w == 254) return "drained";
    if (w == 255) return "suicide";
    return weaponDef(w).name;
}
}

// The owner's asset overrides (docs/ASSETS.md) the game reads itself: glTF models in place of the box models, and the
// window icon. (Sounds: Audio; the font: Renderer; maps: the build.) Nothing is read when there are none.
void Game::loadAssetOverrides() {
    if (!overrides::any()) return;
    for (int kind = 0; kind < MODEL_KINDS; kind++)
        for (int i = 0; i < modelCount(kind); i++) {
            if (!modelExists(kind, i)) continue;
            std::vector<uint8_t> file;
            std::string matched, gerr;
            if (!overrides::find(modelPath(kind, i), {".glb", ".gltf"}, file, &matched)) continue;
            std::string dir = matched.substr(0, matched.rfind('/') + 1);
            MeshData mesh;
            auto resolve = [&](const std::string& uri, std::vector<uint8_t>& out) { return uri.find("..") == std::string::npos && overrides::readRelative(dir + uri, out); };
            if (!loadGltf(file.data(), file.size(), resolve, mesh, &gerr)) { printf("override %s: %s\n", matched.c_str(), gerr.c_str()); continue; }
            if (mesh.triangleCount() > kMaxOverrideTriangles) { printf("override %s: %zu triangles (at most %zu)\n", matched.c_str(), mesh.triangleCount(), kMaxOverrideTriangles); continue; }
            auto m = std::make_unique<ModelMesh>();
            meshToModel(mesh, *m);
            modelOv_[kind][i] = std::move(m);
            printf("override: %s\n", matched.c_str());
        }
    std::vector<uint8_t> icon;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (overrides::find("images/icon", {".png", ".jpg", ".jpeg"}, icon) && decodeImage(icon.data(), icon.size(), w, h, rgba) && w > 0 && h > 0) {
        GLFWimage img{w, h, rgba.data()};
        glfwSetWindowIcon(win_, 1, &img);
    }
}

void Game::overrideTints(int kind, const ModelPalette& pal, Vec3* tint) const {
    const ModelPalette& ref = referencePalette(kind);
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
    ui_.accent = rgba(cfg_.colorFriendly);
    applyDisplayMode();
    applyPresentMode();
    for (auto& q : latQ_) glGenQueries(1, &q.q);
    audio_.setAnnouncer(cfg_.announcerVoice);
    audio_.setExclusive(cfg_.exclusiveAudio);
    if (!opts_.uiTest) rawMouse_.start();   // (the UI test drives the mouse through GLFW)
    if (!audio_.start()) printf("audio: no output device - running without sound\n");
    audio_.setMasterVolume(cfg_.volume);

    botMapIdx_ = bestMapFor(cfg_.botMode);
    if (opts_.timeTrial) startTimeTrial();
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
    if (settingsInGame_) cfg_.save();
    settingsInGame_ = voteMenu_ = specPick_ = queueMenu_ = controlsOpen_ = crosshairOpen_ = hudOpen_ = visualsOpen_ = widgetEdit_ = hudDrop_ = false;
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
    if (s == Screen::Host || s == Screen::VsBots) rescanMaps();
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
    if (b.has(A_SCREENSHOT, code)) {
        static int n = 0;
        r_.screenshot("screenshot_" + std::to_string(n++) + ".png");
        notify("screenshot saved");
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
            if (filePick_) { filePick_ = 0; return; }
            if (widgetEdit_) { widgetEdit_ = false; widgets_.save(activeHudPath()); return; }
            if (controlsOpen_ || crosshairOpen_ || hudOpen_ || visualsOpen_) { controlsOpen_ = crosshairOpen_ = hudOpen_ = visualsOpen_ = false; captureAction_ = -1; cfg_.save(); return; }
            if (settingsInGame_) { settingsInGame_ = false; cfg_.save(); return; }
            if (voteMenu_ || queueMenu_ || picker_ != PICK_NONE) { voteMenu_ = queueMenu_ = specPick_ = false; picker_ = PICK_NONE; return; }
            paused_ = !paused_;
            captureMouse(!paused_);
            return;
        }
    } else if (key == GLFW_KEY_ESCAPE) {
        uiIn_.escape = true;
        if (edPrompt_ && screen_ == Screen::Main) { edPrompt_ = false; return; }   // "save your copy?": Esc = DON'T SAVE (it did nothing - UI test)
        if (filePick_) { filePick_ = 0; return; }
        if (widgetEdit_) { widgetEdit_ = false; widgets_.save(activeHudPath()); return; }
        if (controlsOpen_ || crosshairOpen_ || hudOpen_ || visualsOpen_) { controlsOpen_ = crosshairOpen_ = hudOpen_ = visualsOpen_ = false; captureAction_ = -1; cfg_.save(); return; }
        if (picker_ != PICK_NONE) { picker_ = PICK_NONE; return; }
        if (screen_ == Screen::Leaderboard && raceStatsView_) { raceStatsView_ = false; return; } // race stats: back to the board
        if (screen_ == Screen::CustomMode) { setScreen(Screen::Host); return; }                  // same as its BACK (unsaved changes dropped)
        if (screen_ == Screen::Join || screen_ == Screen::Host || screen_ == Screen::Settings || screen_ == Screen::VsBots || screen_ == Screen::Leaderboard || screen_ == Screen::Demos || screen_ == Screen::Info) {
            cfg_.save();
            setScreen(Screen::Main);
        } else if (screen_ == Screen::Connecting) {
            leaveGame();
        }
    }
}

void Game::onFocus(bool focused) { if (focused) focusGained_ = true; else swallowedBtn_ = 0; }   // (let go while away: no release comes)

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
    if (screen_ == Screen::Info) { infoScroll_ -= (float)dy * 60.0f; return; }
    if (screen_ == Screen::Demos && picker_ == PICK_NONE) { demoScroll_ = std::max(0, demoScroll_ - (dy > 0 ? 1 : -1)); return; }   // clamped when drawn
    if (screen_ == Screen::Join) { joinScroll_ = std::max(0, joinScroll_ - (dy > 0 ? 1 : -1)); return; }
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
            if (ms.mode == MODE_FFA) {   // free for all: someone new has the most kills (a clear lead, not a tie)
                int lead = -1, best = 0; bool tie = false;
                for (auto& p : net_.latest().players) {
                    if (p.flags & PS_SPECTATOR) continue;
                    if (p.score > best) { best = p.score; lead = p.id; tie = false; } else if (p.score == best && best > 0) tie = true;
                }
                if (tie) lead = -1;
                if (lead >= 0 && lead != annLeader_ && annLeader_ != -2) say(SND_VOICE_NEW_LEADER, 2);
                if (lead >= 0) annLeader_ = lead;
            }
        }
    }
    if (ms.phase != PHASE_LIVE) annLeader_ = -2;   // (a new match: the first lead isn't "new")
    else if (annLeader_ == -2) annLeader_ = -1;
    // Killstreaks are per match and per player: a new match (back to warmup, or another mode) starts them again, and a
    // slot's new player doesn't inherit the last one's (they only reset on leaving the server, so a spree carried over)
    if (annPrevValid_ && (ms.mode != annPrev_.mode || (annPrev_.phase != PHASE_WARMUP && ms.phase == PHASE_WARMUP)))
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
                if (e.b == net_.clientId() && e.a < kMaxPlayers && e.a != e.b) specTarget_ = e.a;
                if (e.a == net_.clientId() && e.b != e.a && cfg_.hitsounds) audio_.play(SND_KILL, 0.9f);
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
            case EV_QUIP: {   // someone's quip: a voice line from the pack (only lines that are recorded), from where they are
                std::vector<int> lines;
                for (int id = SND_VOICE_THREE; id < SND_COUNT; id++) if (audio_.hasSound(id)) lines.push_back(id);
                if (lines.empty() || e.a >= kMaxPlayers) break;
                int line = lines[e.b % lines.size()];
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
                if (r.fromText(all)) { setActiveRules(r); notify("CUSTOM MODE: " + upper(r.name), 4); }
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
                }
                centerText_ = e.text;
                centerUntil_ = time_ + (e.b ? 8.0 : 3.5);
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
            overrideTints(MODEL_PROJECTILE, ppal, tint);
            r_.mesh(*ov, Mat4::translate(pos) * Mat4::rotY(yaw) * Mat4::rotX(pitch), tint, 1.0f);
            continue;
        }
        projectileParts(mt, ppal, 1, 1, 0, parts_);
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
    if (opts_.debugPerf) { showPerf_ = true; if (pendingInput_ < 0) pendingInput_ = timeNow(); }
    if (opts_.debugPause && screen_ == Screen::InGame && !paused_) { paused_ = true; voteMenu_ = opts_.debugVote; opts_.debugPause = false; }
    if (opts_.debugSettings && screen_ == Screen::InGame && !settingsInGame_) { paused_ = true; settingsInGame_ = true; controlsOpen_ = opts_.debugControls; crosshairOpen_ = opts_.debugCrosshair; widgetEdit_ = opts_.debugWidgets; hudOpen_ = opts_.debugHudLayout; widgetSel_ = opts_.debugWidgets ? 0 : -1; opts_.debugSettings = false; }
    r_.setMsaa(cfg_.msaa);
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
    net_.setInterpFixed(fairRules() ? 0.010f : cfg_.interpMs > 0 ? cfg_.interpMs * 0.001f : cfg_.interpMs < 0 ? 0.0f : -1.0f);
    frameInput_ = pendingInput_; // the oldest input this frame will show
    pendingInput_ = -1;
    frameDt_ = dt;
    if (opts_.bench > 0 && screen_ == Screen::InGame && !bake_) benchFrame(dt);
    frameTimes_[frameIdx_] = (float)dt;
    frameIdx_ = (frameIdx_ + 1) % frameTimes_.size();
    frameCount_ = std::min(frameCount_ + 1, frameTimes_.size());
    if (!polledEarly_) {   // (normally done by preInput, before this frame's input was read)
        if (bake_ && net_.playback()) net_.holdPlaybackClock();   // a demo waits for its map's bake
        QZONE("net poll"); net_.poll();
    }
    polledEarly_ = false;
    updateCommunity();
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
        QZONE("input + prediction ticks");
        while (tickAcc_ >= period) {
            if (bake_) {   // the world isn't there yet: keep the connection alive, nothing else
                fireLatched_ = false;   // (no stray shot when the new map is ready)
                UserCmd k; k.yaw = yaw_; k.pitch = pitch_;
                net_.tick(k);
                tickAcc_ -= period;
                continue;
            }
            UserCmd c = buildCmd();
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
            if (weaponDef(hw).semiAuto) { // mirror the server: one shot per click, short buffer
                if (wantFire && !localFireHeld_) localFireQueued_ = time_ + kFireBuffer;
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
                localNextFire_ = (cadence ? localNextFire_ : time_) + shotRefire(w, alt);
                if (isThrow(w, alt)) (w == WPN_KNIFE ? localKnifeBack_ : localSyringeBack_) = time_ + (w == WPN_KNIFE ? kKnifeAway : kSyringeAway);
                if (w != WPN_LASER || time_ >= nextLaserSound_) { // laser fires every 10 ms: sound at 20 Hz
                    audio_.play(SND_RIFLE + w, w == WPN_LASER ? 0.6f : 0.8f, 0.97f + (qrand() % 7) * 0.01f);
                    nextLaserSound_ = time_ + 0.05;
                }
                if (w == WPN_KNIFE && !alt) knifeSlash_[net_.clientId() % kMaxPlayers] = time_;
                if (isThrow(w, alt)) { localFireLocked_ = true; heldWeapon_ = wp_.thrown(weaponView()); }   // (and out comes the next weapon)
                recoil_ = w == WPN_LASER ? 0.15 : w == WPN_SNIPER ? 1.6 : 1.0;
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
                            for (int b = 0; b <= kShotgunBounces && range > 0.01f; b++) {
                                Vec3 n;
                                float t = net_.map().raycast(o, d, range, &n);
                                Vec3 to = o + d * (t < 0 ? range : t);
                                tracers_.push_back({b ? o : from, to, time_ + 0.3});
                                if (t < 0) break;
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
            net_.tick(c);
            tickAcc_ -= period;
        }
        if (net_.playback()) wp_.reset();   // (a demo: what the recorded player held)
        heldWeapon_ = wp_.current(weaponView());   // (no tick this frame: the last one's, or the snapshot's when dead)
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

        int hp = net_.selfHealth();
        if (hp < lastHealth_ && net_.selfAlive()) { damageUntil_ = time_ + 0.25; audio_.play(SND_HURT, 0.7f); }
        lastHealth_ = hp;
        if (!net_.selfAlive() && uiIn_.pressed && !paused_) {
            // cycle spectate target
            const auto& ps = framePlayers();
            for (int k = 1; k <= kMaxPlayers; k++) {
                int id = (specTarget_ + k + kMaxPlayers) % kMaxPlayers;
                if (ps[id].valid && (ps[id].snap.flags & PS_ALIVE) && id != net_.clientId()) { specTarget_ = id; break; }
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
    { QZONE("flush2D"); r_.flush2D(); }
    pipeMark(PM_HUD);
    pipeGpuEnd();

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

// Where your shots come out: the muzzle of the weapon model wherever the HUD settings put it (x / y / depth /
// size), so beams, tracers and your projectiles leave the gun you see. Hidden model: a spot low and right.
Vec3 Game::muzzlePoint(int weapon, const Vec3& eye) const {
    const auto& vm = cfg_.hud[Settings::HUD_VIEWMODEL];
    Vec3 d = angleForward(yaw_, pitch_), right = normalize(cross(d, {0, 1, 0}));
    if (weapon < 0 || weapon >= WPN_COUNT) return eye + d * 0.5f + right * 0.15f - Vec3(0, 0.12f, 0); // (hidden model: where it would be)
    Vec3 o = Vec3(0.2f + vm.x * 0.0005f, -0.17f - vm.y * 0.0005f, -0.55f - vm.z * 0.001f + (float)recoil_ * 0.06f) + kMuzzle[weapon] * vm.scale;
    float cp = std::cos(pitch_), sp = std::sin(pitch_), cy = std::cos(yaw_), sy = std::sin(yaw_);
    Vec3 p = {o.x, cp * o.y - sp * o.z, sp * o.y + cp * o.z};           // the viewmodel's own transform (rotX, then rotY)
    return eye + Vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
}

void Game::drawWorld(float alpha) {
    QZONE("drawWorld");
    r_.shadowCasters = cfg_.shadows;   // (shadows off: nothing off screen is built just to be thrown away)
    const Map& map = net_.map();
    const ModeState& ms = net_.mode();
    auto& players = framePlayers();
    int me = net_.clientId();
    bool alive = net_.selfAlive();
    // demo playback: through the recorder's eyes (their recorded view, interpolated like everyone else)
    const bool demoView = net_.playback() && me >= 0 && me < kMaxPlayers && players[me].valid;
    const bool lateLatch = alive && !demoView && mouseCaptured_;   // (your own view: the aim is re-read before drawing)
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
    } else {
        if (specTarget_ < 0 || !players[specTarget_].valid || !(players[specTarget_].snap.flags & PS_ALIVE)) {
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
    r_.setCamera(eye, camYaw, camPitch, alive ? viewFov() : cfg_.fov);
    audio_.setListener(eye, camYaw);
    updateSounds(players);

    if (opts_.debugModels && alive) { // model lineup for screenshots: the last slots, 5 m ahead, facing you
        Vec3 f = {-std::sin(yaw_), 0, -std::cos(yaw_)}, rt = {std::cos(yaw_), 0, -std::sin(yaw_)};
        for (int k = 0; k < kPlayerModels; k++) {
            auto& ip = players[kMaxPlayers - kPlayerModels + k];
            ip.valid = true;
            ip.snap = PlayerSnap{};
            ip.snap.id = (uint8_t)(kMaxPlayers - kPlayerModels + k);
            ip.snap.flags = PS_ALIVE | (k % 2 ? PS_TEAM_B : 0);
            ip.snap.pos = net_.smoothedPos(alpha) + f * 5.0f + rt * ((k - 2) * 1.5f);
            ip.snap.yaw = yaw_ + 3.14159265f;
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
            overrideTints(MODEL_PLAYER, pal, tint);
            Mat4 sc;
            sc.m[0] = bs; sc.m[5] = bs * hs; sc.m[10] = bs;
            r_.mesh(*ov, Mat4::translate(pos) * Mat4::rotY(yaw) * sc, tint, sv, true, vis);
        } else {
            playerModelParts(pm, hs, pal, parts_);
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
            r_.boxPitched(shoulder, Vec3(0, 0, -0.35f) * bs, Vec3(0.06f, 0.07f, 0.35f) * bs, yaw, pitch, {0.12f, 0.12f, 0.14f}, sv);
            if ((p.flags & PS_HASBOMB) && !isItMode(net_.mode().mode))
                r_.box(at(1.15f) + fwd * -0.32f, {0.2f, 0.22f, 0.1f}, yaw, {0.1f, 0.1f, 0.1f}, sv);
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
                overrideTints(MODEL_CROWN, cpal, tint);
                Mat4 sc;
                sc.m[0] = sc.m[5] = sc.m[10] = bs;
                r_.mesh(*ov, Mat4::translate(top) * Mat4::rotY(yaw) * sc, tint, sv, false);
            } else {
                crownParts(cpal, parts_);                                   // a band and four points
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
    auto ropeEnd = [&](int target, const Vec3& anchor) {
        if (target < 0 || target >= kMaxPlayers || !players[target].valid || players[target].snap.hidden || !(players[target].snap.flags & PS_ALIVE)) return anchor;
        Vec3 at = target == me && alive ? net_.smoothedPos(alpha) : players[target].snap.pos;
        return at + Vec3(0, 1.0f * bs, 0);
    };
    if (alive && (net_.predicted().flags & MF_HOOKED)) {
        Vec3 d = angleForward(yaw_, pitch_);
        Vec3 right = normalize(cross(d, {0, 1, 0}));
        const MoveState& pm = net_.predicted();
        r_.line(eye - Vec3(0, 0.25f, 0) - right * 0.15f, ropeEnd((int)pm.hookTarget - 1, pm.hookPoint), 0.03f, {0.1f, 0.1f, 0.1f, 1});
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == me || !players[i].valid || !players[i].snap.hooked) continue;
        r_.line(players[i].snap.pos + Vec3(0, 1.2f, 0), ropeEnd((int)players[i].snap.hooked - 2, players[i].snap.hookPoint), 0.04f, {0.1f, 0.1f, 0.1f, 1});
    }

    // other players' shots: laser beams while firing, a tracer when a sniper fires
    for (int i = 0; i < kMaxPlayers; i++) {
        bool firing = players[i].valid && (players[i].snap.flags & PS_FIRING) && (players[i].snap.flags & PS_ALIVE) && i != me;
        if (firing) {
            const PlayerSnap& p = players[i].snap;
            int w = p.weapon;
            if (w == WPN_KNIFE && !remoteFiring_[i]) knifeSlash_[i] = time_;
            if (w == WPN_SHOTGUN && !remoteFiring_[i] && cfg_.enemyShots) { // pellet tracers in the server's fixed pattern
                const PlayerSnap& q = players[i].snap;
                Vec3 eyeP = q.pos + Vec3(0, ((q.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);   // (giant / double size: their eye, not a body below it)
                Vec3 aim = angleForward(q.yaw, q.pitch);
                const WeaponDef& wd = weaponDef(w);
                for (int k = 0; k < wd.pellets; k++) {
                    Vec3 d = spreadAim(aim, wd.spread, k, wd.pellets, 0);
                    float bangT, t = map.raycastBullet(eyeP, d, wd.range, &bangT);
                    tracers_.push_back({eyeP + d * 0.6f, eyeP + d * (t < 0 ? wd.range : t), time_ + 0.2});
                }
            }
            if (cfg_.enemyShots && (w == WPN_LASER || (w == WPN_SNIPER && !remoteFiring_[i]))) {
                Vec3 eyeP = p.pos + Vec3(0, ((p.flags & PS_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(), 0);
                Vec3 d = angleForward(p.yaw, p.pitch);
                float range = weaponDef(w).range;
                float bangT, t = map.raycastBullet(eyeP, d, range, &bangT);
                Vec3 end = eyeP + d * (t < 0 ? range : t);
                Vec3 start = eyeP + d * 0.6f;
                // (watching through their eyes: a line from the eye along the view is edge on, invisible; from the
                // gun's place, low and to the right, like your own)
                if (i == viewing && !alive) start += Vec3(std::cos(p.yaw), 0, -std::sin(p.yaw)) * 0.2f + Vec3(0, -0.17f, 0);
                if (w == WPN_LASER) r_.line(start, end, 0.06f, rgba(teamColor(i), 0.85f));
                else tracers_.push_back({start, end, time_ + 0.9});
            }
        }
        remoteFiring_[i] = firing;
    }
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

    // projectiles (yours: from your weapon model's muzzle, blending onto the real path over 0.2 s)
    {
        std::vector<ProjStart>& keep = projStartsKeep_;   // (the two trade memory each frame)
        keep.clear();
        for (auto& pr : lastProjectiles_) {
            if (pr.owner != me) continue;
            ProjStart st{pr.id, time_, {}};
            bool found = false;
            for (auto& o : projStarts_) if (o.id == pr.id) { st = o; found = true; }
            if (!found && alive) {
                // where the weapon that fired it points out of the screen (thrown knife: from the knife hand)
                int w = pr.type < WPN_COUNT ? pr.type : heldWeapon_;
                Vec3 muz = muzzlePoint(w, eye);
                if (length(pr.pos - eye) < 3.0f) st.offset = muz - pr.pos;   // only shots just fired
            }
            keep.push_back(st);
        }
        projStarts_.swap(keep);
    }
    for (auto pr : lastProjectiles_) {
        // (on you: where you are this frame; on anyone else, interpolatedProjectiles already put it on them)
        if ((pr.flags & PROJ_ON_PLAYER) && pr.onPlayer == me) net_.attachedPos(pr, alpha, &pr.pos);
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
            overrideTints(MODEL_PROJECTILE, ppal, tint);
            r_.mesh(*ov, Mat4::translate(pr.pos) * Mat4::rotY(my) * Mat4::rotX(mp), tint, 1.0f);
        } else {
            projectileParts(mt, ppal, fl, core, wob, parts_);
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
    }
    drawStuckProps(eye, alpha);
    // explosions
    for (size_t i = 0; i < explosions_.size();) {
        float t = (float)(time_ - explosions_[i].start) / 0.35f;
        if (t >= 1) { explosions_.erase(explosions_.begin() + i); continue; }
        float r = (0.5f + t * 3.0f) * explosions_[i].size;
        Vec3 c = explosions_[i].at;
        Color ec = explosions_[i].bubble ? rgba(r_.glowColor, 0.5f * (1 - t)) : Color{1.0f, 0.85f, 0.5f, 0.55f * (1 - t)};
        r_.translucentBox({c - Vec3(r, r, r), c + Vec3(r, r, r)}, ec);
        i++;
    }
    for (size_t i = 0; i < tracers_.size();) {
        if (time_ > tracers_[i].until) { tracers_.erase(tracers_.begin() + i); continue; }
        float life = (float)std::min(1.0, (tracers_[i].until - time_) / 0.25); // fade out over the last quarter second
        r_.line(tracers_[i].a, tracers_[i].b, 0.03f, {1.0f, 0.95f, 0.7f, 0.8f * life});
        i++;
    }

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
        if (ms.bombState == BOMB_DROPPED || ms.bombState == BOMB_PLANTED) {
            bool blink = ms.bombState == BOMB_PLANTED && std::fmod(time_, std::max(0.15, ms.bombTimer / 20.0)) < 0.08;
            Vec3 c = blink ? Vec3(1, 0.2f, 0.2f) : Vec3(0.15f, 0.15f, 0.15f);
            r_.box(ms.bombPos + Vec3(0, 0.15f, 0), {0.25f, 0.15f, 0.18f}, 0, c, 1.0f);
            labelAt(ms.bombPos + Vec3(0, 0.8f, 0), ms.bombState == BOMB_PLANTED ? "BOMB" : "BOMB (DROPPED)", rgba(teamCol(ms.attackers)));
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
            Vec3 side = normalize(Vec3(-ah.z, 0, ah.x)) * 1.8f;
            bool reached = (int)k < ms.point;
            Vec3 gc = reached ? teamCol(ms.attackers) : Vec3(0.8f, 0.8f, 0.8f);
            r_.box(at + side + Vec3(0, 1.4f, 0), {0.12f, 1.4f, 0.12f}, 0, gc, 1.0f);
            r_.box(at - side + Vec3(0, 1.4f, 0), {0.12f, 1.4f, 0.12f}, 0, gc, 1.0f);
            labelAt(at + Vec3(0, 3.2f, 0), "CHECKPOINT " + std::to_string(k + 1), rgba(gc));
        }
        Vec3 cart = map.cartPoint(ms.mode, ms.attackers, ms.payloadDist);
        Vec3 ahead = map.cartPoint(ms.mode, ms.attackers, ms.payloadDist + 0.5f);
        Vec3 d = ahead - cart;
        float yaw = (length(d) > 1e-3f) ? std::atan2(-d.x, -d.z) : 0;
        float sv = sunVisibility(map, cart + Vec3(0, 1, 0));
        // an armoured ore cart in the map's palette: metal chassis on four wheels, team-coloured band, a
        // glowing core in a cradle on top
        auto lin = [](const Vec3& c) { return Vec3(std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f)); };
        Vec3 metal = lin(r_.metalColor) * 1.6f + Vec3(0.04f, 0.04f, 0.04f), accent = lin(r_.accentColor) * 1.4f;
        float cy = std::cos(yaw), syw = std::sin(yaw);
        auto at = [&](float x, float y, float z) { return cart + Vec3(x * cy + z * syw, y, -x * syw + z * cy); };
        for (int k = 0; k < 4; k++) r_.box(at(k & 1 ? 0.85f : -0.85f, 0.32f, k & 2 ? 0.75f : -0.75f), {0.12f, 0.3f, 0.3f}, yaw, metal * 0.45f, sv);
        r_.box(at(0, 0.75f, 0), {0.8f, 0.4f, 1.25f}, yaw, metal, sv);                       // chassis
        r_.box(at(0, 0.8f, 0), {0.83f, 0.12f, 1.28f}, yaw, cartCol * 0.9f, sv);            // team band
        r_.box(at(0, 1.2f, 0), {0.85f, 0.06f, 1.3f}, yaw, accent, sv);                     // rim
        r_.box(at(0, 1.45f, 0), {0.45f, 0.2f, 0.6f}, yaw, metal * 0.7f, sv);                // cradle
        r_.box(at(0, 1.75f, 0), {0.3f, 0.22f, 0.3f}, yaw, r_.glowColor * 1.3f + cartCol * 0.3f, sv);  // core
        r_.box(at(0, 0.75f, 1.3f), {0.5f, 0.15f, 0.06f}, yaw, r_.glowColor, sv);            // lamps front
        r_.box(at(0, 0.75f, -1.3f), {0.5f, 0.15f, 0.06f}, yaw, r_.glowColor, sv);           // and back
    } else if (ms.mode == MODE_RACE) {
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
    const bool specGun = !alive && viewing != me && viewing >= 0 && players[viewing].valid;
    const PlayerSnap* holder = specGun ? &players[viewing].snap : mep;
    bool unarmed = isItMode(net_.mode().mode) && !(holder && (holder->flags & PS_HASBOMB)) && !(opts_.debugWeapon > 0 && !specGun);   // (--weapon: shown anyway, for screenshots)
    const auto& vm = cfg_.hud[Settings::HUD_VIEWMODEL];
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
        // placed from the HUD settings: x / y in 720p units (+y down the screen), depth in mm, scale
        Vec3 at = {0.2f + vm.x * 0.0005f, -0.17f - vm.y * 0.0005f, -0.55f - vm.z * 0.001f + kick};
        const float k = vm.scale;
        if (const ModelMesh* ov = modelOverride(MODEL_WEAPON, w)) {   // the owner's model (docs/ASSETS.md)
            Vec3 tint[TINT_COUNT];
            overrideTints(MODEL_WEAPON, vpal, tint);
            r_.viewMesh(*ov, at, k, tint);
        } else {
            viewmodelParts(w, vpal, parts_);
            // the locator's screen: dim while it recharges, pulsing when it's ready
            float glow = w != WPN_LOCATOR || specGun ? 1.0f : time_ >= locatorReady_ ? 0.85f + 0.35f * (float)std::sin(time_ * 6.0) : 0.3f;
            for (const ModelPart& q : parts_) r_.viewBox(at + q.c * k, q.h * k, q.slot == TINT_GLOW ? q.color * glow : q.color);
        }
    }
    sceneSettings();
    // The HUD, F3 / F4 and their upload, before the late read: off the mouse to photon path (it used to sit between the
    // read and the swap, ~87 us at 4000 fps). What it places in the 3D view (damage numbers, the locator box) uses the
    // aim from before the read, a sliver of a turn behind, like the name tags above (owner: the UI may lag the aim).
    drawHud();
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
// out; our three choices while the throw is open.
void Game::drawRps(float W, float H, float s) {
    const float ts = ui_.textScale(), cx = W * 0.5f;
    const int me = net_.clientId();
    const int n = (int)rps_.players.size();
    const float card = 120 * s, gap = 12 * s;
    const Color gold = {1, 0.85f, 0.3f, 1};
    bool in = false;   // (we're still in it)
    for (auto& p : rps_.players) in |= p.id == me && !p.out;
    float pw = std::min(W - 32 * s, std::max(560 * s, n * (card + gap) + 40 * s));
    float ph = (rps_.stage == RPS_PICK ? (in ? 306 : 262) : rps_.winner >= 0 ? 292 : 262) * s;
    float px = cx - pw * 0.5f, py = H * 0.5f - 120 * s;
    r_.rect(px, py, pw, ph, {0.03f, 0.035f, 0.05f, 0.92f});
    r_.rect(px, py, pw, 3 * s, gold);
    float big = std::max(1.0f, std::round(ts * 1.4f)), mid = std::max(1.0f, std::round(ts * 0.9f)), small = std::max(1.0f, std::round(ts * 0.7f));
    auto centered = [&](const std::string& t, float y, float k, Color c) { r_.text(cx - r_.textWidth(t, k) * 0.5f, y, k, t, c); };
    centered("TIEBREAK - ROCK PAPER SCISSORS", py + 12 * s, big, gold);
    std::string sub = rps_.bestOf3 ? "BEST OF 3 - FIRST TO 2 WINS THE MATCH" : "EVERYONE THROWS - THE LOSING SHAPE IS OUT";
    sub += "   THROW " + std::to_string(std::max(1, rps_.throwNo));
    centered(sub, py + 14 * s + r_.textHeight(big) + 6 * s, small, kGrey);
    // the players, a card each (squeezed together when there are many)
    float step = n > 1 ? std::min(card + gap, (pw - 20 * s - card) / (n - 1)) : 0;
    float x0 = cx - (card + step * std::max(0, n - 1)) * 0.5f, y0 = py + 70 * s;
    const bool reveal = rps_.stage != RPS_PICK;
    for (int k = 0; k < n; k++) {
        const auto& p = rps_.players[k];
        float x = x0 + k * step;
        bool lost = p.out, won = rps_.winner == p.id;
        Color frame = won ? kGreenOk : lost ? Color{0.45f, 0.2f, 0.2f, 1} : p.id == me ? rgba(cfg_.colorFriendly) : Color{0.3f, 0.32f, 0.36f, 1};
        r_.rect(x, y0, card, 150 * s, frame);
        r_.rect(x + 2 * s, y0 + 2 * s, card - 4 * s, 150 * s - 4 * s, {0.07f, 0.08f, 0.1f, 1});
        std::string nm = upper(net_.playerName(p.id));
        if (nm.empty()) nm = "P" + std::to_string(p.id);
        while (nm.size() > 3 && r_.textWidth(nm, small) > card - 12 * s) nm.pop_back();
        r_.text(x + (card - r_.textWidth(nm, small)) * 0.5f, y0 + 8 * s, small, nm, p.id == me ? rgba(cfg_.colorFriendly) : kWhite);
        int shape = p.shape;
        if (!reveal && p.id == me && rpsMine_ != RPS_NONE) shape = rpsMine_;   // (our own pick, before the reveal)
        Color sc = lost ? Color{0.5f, 0.3f, 0.3f, 1} : won ? kGreenOk : Color{0.92f, 0.9f, 0.85f, 1};
        float icon = 64 * s;
        drawRpsShape(x + (card - icon) * 0.5f, y0 + 30 * s, icon, shape, sc);
        std::string st = lost ? "OUT" : reveal || (p.id == me && shape != RPS_NONE) ? rpsName(shape) : shape == RPS_NONE ? "CHOOSING..." : "READY";
        r_.text(x + (card - r_.textWidth(st, small)) * 0.5f, y0 + 100 * s, small, st, lost ? kRed : reveal ? kWhite : shape == RPS_NONE ? kGrey : kGreenOk);
        if (rps_.bestOf3 && !lost)   // best of 3: a pip per throw won
            for (int w = 0; w < 2; w++) {
                float bx = x + card * 0.5f - 15 * s + w * 18 * s;
                r_.rect(bx, y0 + 126 * s, 12 * s, 12 * s, {0.25f, 0.27f, 0.3f, 1});
                if (w < p.wins) r_.rect(bx + 2 * s, y0 + 128 * s, 8 * s, 8 * s, gold);
            }
    }
    float y = y0 + 162 * s;
    if (rps_.stage == RPS_PICK) {
        float left = opts_.debugRps ? kRpsPickTime : net_.mode().timeLeft;
        centered((in ? "CHOOSE - " : "THEY'RE CHOOSING - ") + std::to_string((int)std::ceil(left)), y, mid, kWhite);
        if (in) {   // our three choices, on the weapon slot keys
            float bw = 150 * s, bx = cx - (3 * bw + 2 * gap) * 0.5f, by = y + r_.textHeight(mid) + 8 * s;
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

void Game::drawHud() {
    QZONE("drawHud");
    const float W = (float)r_.width(), H = (float)r_.height();
    float s = H / 720.0f;
    ui_.begin(&r_, uiIn_, s);
    float ts = ui_.textScale();
    const ModeState& ms = net_.mode();
    int me = net_.clientId();
    const PlayerSnap* mep = net_.findPlayer(me);
    int myTeam = mep ? (mep->team() == TEAM_SPECTATOR ? TEAM_A : mep->team()) : TEAM_A;
    Color fc = rgba(cfg_.colorFriendly), ec = rgba(cfg_.colorEnemy);
    bool teamMode = modeIsTeamBased(ms.mode);

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
    if (isItMode(ms.mode) && ms.phase != PHASE_WARMUP && mep && mep->team() != TEAM_SPECTATOR) {
        bool it = (mep->flags & PS_HASBOMB) != 0;
        float bts = std::max(1.0f, std::round(ts * 1.2f));
        auto banner = [&](const std::string& t, Color c, float y) { r_.text(cx - r_.textWidth(t, bts) * 0.5f, y, bts, t, c); };
        if (ms.mode == MODE_HIDE && ms.phase == PHASE_FREEZE && it) { // the seeker faces the wall while everyone hides
            r_.rect(0, 0, W, H, {0.02f, 0.02f, 0.03f, 1});
            float big = std::max(2.0f, std::round(ts * 3));
            std::string n = std::to_string((int)std::ceil(ms.timeLeft));
            r_.text(cx - r_.textWidth(n, big) * 0.5f, cy - 40 * s, big, n, kWhite);
            banner("YOU'RE SEEKING - FACE THE WALL AND COUNT", kWhite, cy + 30 * s);
        } else {
            int left = 0;
            for (auto& p : net_.latest().players) if ((p.flags & PS_ALIVE) && !(p.flags & PS_HASBOMB) && p.team() != TEAM_SPECTATOR) left++;
            std::string t = ms.mode == MODE_HIDE ? (it ? "SEEKING - " + std::to_string(left) + " STILL HIDDEN" : ms.phase == PHASE_FREEZE ? "HIDE! THE SEEKER IS COUNTING" : "STAY HIDDEN - " + std::to_string(left) + " LEFT")
                                                 : (it ? "YOU'RE THE TAGGER - TAG SOMEONE BEFORE TIME RUNS OUT" : "RUN - DON'T BE THE TAGGER WHEN TIME RUNS OUT");
            if (!(mep->flags & PS_ALIVE) && ms.mode == MODE_HIDE) t = "FOUND - WATCH THE REST OF THE ROUND";
            banner(t, it ? kRed : kWhite, 92 * s);
        }
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

    // top: mode, timer, scores
    std::string title = modeTitle(ms.mode);
    if (ms.mode == MODE_ARENA || (ms.mode == MODE_TOURNEY && ms.phase != PHASE_WARMUP))
        title += std::string(" - ") + arenaVariant(ms.arenaVariant).title;
    std::string phase;
    switch (ms.phase) {
        case PHASE_WARMUP:
            phase = ms.timeLeft > 0 ? "MATCH STARTS IN " + std::to_string((int)std::ceil(ms.timeLeft))
                                    : ms.mode == MODE_RACE ? "PRACTICE (UNTIMED) - " + inputName(binds().code[A_VOTESTART][0]) + " TO START THE RACE"
                                    : "WARMUP - " + inputName(binds().code[A_VOTESTART][0]) + " TO VOTE START";
            break;
        case PHASE_FREEZE: // free for all is one long round: just the countdown
            phase = (ms.mode == MODE_FFA ? std::string("STARTING IN ") : "ROUND " + std::to_string(ms.round) + " - STARTS IN ") + std::to_string((int)std::ceil(ms.timeLeft));
            break;
        case PHASE_LIVE: {   // the round clock in every timed mode (hudclock.h; short enough to stay in the string's own buffer)
            char line[32];
            hudLiveLine(ms, line, sizeof line);
            phase = line;
            break;
        }
        case PHASE_ROUND_END: phase = "ROUND OVER"; break;
        case PHASE_MATCH_END: phase = "MATCH OVER"; break;
    }
    bool rankedGame = ms.flags & MS_RANKED;
    if (rankedGame) title = std::string(rankedKind(ms.arenaVariant)) + " - " + arenaVariant(ms.arenaVariant).title;
    if (ms.flags & MS_TIMETRIAL) title = "TIME TRIAL - VIADUCT";
    // score box sized from the real text sizes (HUD text is rounded to whole steps, so it's relatively
    // bigger at small heights): the scores sit outside the widest centre line, the phase under the title
    std::string scoreA, scoreB;
    if (teamMode) {
        scoreA = std::to_string(myTeam == TEAM_A ? ms.scoreA : ms.scoreB);
        scoreB = std::to_string(myTeam == TEAM_A ? ms.scoreB : ms.scoreA);
    } else if (mep) {
        int best = -9999;
        for (auto& p : net_.latest().players) if (p.id != me && !(p.flags & PS_SPECTATOR)) best = std::max<int>(best, p.score);
        scoreA = std::to_string(mep->score);
        scoreB = best == -9999 ? "-" : std::to_string(best);
    }
    const float sts = ts * 1.5f, th = r_.textHeight(ts);
    const float scoreW = std::max(r_.textWidth(scoreA, sts), r_.textWidth(scoreB, sts));
    const float midW = std::max(r_.textWidth(title, ts), r_.textWidth(phase, ts));
    const float half = std::min(W * 0.5f - 8 * s, std::max(220 * s, midW * 0.5f + scoreW + 32 * s));
    const float phaseY = 16 * s + std::max(22 * s, th + 4 * s);
    const float boxH = std::max(64 * s, phaseY + th);
    const float scoreY = 8 * s + (boxH - r_.textHeight(sts)) * 0.5f;
    ui_.panel(cx - half, 8 * s, half * 2, boxH, 0.55f);
    if (rankedGame) { // always visible on a ranked server
        const Color gold = {1.0f, 0.78f, 0.25f, 1};
        r_.rect(cx - half, 8 * s, half * 2, 3 * s, gold);
        float tts = std::max(1.0f, std::round(ts * 0.5f));
        const char* tag = unratedServer_ ? "RANKED - NOT RATED" : "RANKED";
        float tw = r_.textWidth(tag, tts) + 12 * s; // tag on the panel's right edge, clear of the objective line
        r_.rect(cx + half + 4 * s, 8 * s, tw, r_.textHeight(tts) + 8 * s, gold);
        r_.text(cx + half + 10 * s, 12 * s, tts, tag, {0.1f, 0.08f, 0.02f, 1});
        if (ms.phase == PHASE_WARMUP || ms.phase == PHASE_FREEZE)
            ui_.labelCentered(cx, 104 * s, "RANKED: DEFAULT MODEL, HUD, CONTROLS AND 10 MS INTERP FOR FAIRNESS", gold, 0.5f); // under the ruleset line
        if (unratedServer_) ui_.labelCentered(cx, 118 * s, std::string(kUnratedNotice) + ": RESULTS DON'T COUNT", gold, 0.5f);
    }
    ui_.labelCentered(cx, 16 * s, title, kWhite);
    ui_.labelCentered(cx, phaseY, phase, kGrey);
    if (!scoreA.empty()) {
        r_.text(cx - half + 20 * s, scoreY, sts, scoreA, fc);
        r_.text(cx + half - 20 * s - r_.textWidth(scoreB, sts), scoreY, sts, scoreB, ec);
    }

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
    } else if (ms.mode == MODE_RACE && mep) {
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
    const bool spectator = mep && mep->team() == TEAM_SPECTATOR;
    if (spectator) {
        obj.clear(); progress = -1;
        float bts = std::max(2.0f, std::round(ts * 1.5f));
        const char* t = "SPECTATOR";
        float bw = r_.textWidth(t, bts) + 48 * s, bh = r_.textHeight(bts) + 16 * s, by = 8 * s + boxH + 8 * s;
        r_.rect(cx - bw * 0.5f, by, bw, bh, {0.05f, 0.06f, 0.08f, 0.8f});
        r_.rect(cx - bw * 0.5f, by, bw, 3 * s, rgba(cfg_.colorFriendly));
        r_.text(cx - r_.textWidth(t, bts) * 0.5f, by + 8 * s, bts, t, kWhite);
        ui_.labelCentered(cx, by + bh + 6 * s, "JOIN A TEAM FROM THE MENU (ESC)", kGrey, 0.5f);
    }
    if (!obj.empty()) ui_.labelCentered(cx, 80 * s, obj, kWhite);
    if (progress >= 0) {
        float bw = 300 * s;
        r_.rect(cx - bw * 0.5f, 104 * s, bw, 8 * s, {0, 0, 0, 0.5f});
        r_.rect(cx - bw * 0.5f, 104 * s, bw * clampf(progress, 0, 1), 8 * s, (ms.mode == MODE_PAYLOAD || ms.mode == MODE_POINTS) ? rgba(myTeam == ms.attackers ? cfg_.colorFriendly : cfg_.colorEnemy) : kWhite);
    }
    if (ms.mode == MODE_PAYLOAD && progress >= 0)   // the checkpoints on the bar
        for (float f : net_.map().cartChecks) r_.rect(cx - 150 * s + 300 * s * f - 1, 100 * s, 2, 16 * s, kWhite);
    if (ms.mode == MODE_NPAYLOAD && ms.payloadLen > 0) {
        // tug-of-war bar: your side's end on the left
        float bw = 300 * s, f = ms.payloadDist / ms.payloadLen;
        if (myTeam == TEAM_A) f = 1 - f; // team A pushes toward the end of the track
        r_.rect(cx - bw * 0.5f, 104 * s, bw, 8 * s, rgba(cfg_.colorEnemy, 0.8f));
        r_.rect(cx - bw * 0.5f, 104 * s, bw * clampf(1 - f, 0, 1), 8 * s, rgba(cfg_.colorFriendly, 0.9f));
        r_.rect(cx - 1, 100 * s, 2, 16 * s, kWhite);
    }
    if (ms.mode == MODE_BOMB && ms.bombState == BOMB_PLANTED && ms.phase == PHASE_LIVE)
        ui_.labelCentered(cx, 118 * s, fmtTime(ms.bombTimer), kRed, 1.5f);

    // health / weapon (each element moved / sized / recolored by the HUD settings); spectating: their health
    const PlayerSnap* specP = !net_.selfAlive() && specTarget_ >= 0 ? net_.findPlayer(specTarget_) : nullptr;
    if (specP && (specP->flags & PS_ALIVE) && !cfg_.hud[Settings::HUD_HEALTH].hidden) {
        const auto& e = cfg_.hud[Settings::HUD_HEALTH];
        float k = s * e.scale, ox = 24 * s + e.x * s, oy = H + e.y * s;
        int hp = specP->health;
        float hts = std::max(1.0f, std::round(ts * 2 * e.scale));
        r_.text(ox, oy - 70 * k, hts, std::to_string(hp), hp > 30 ? kWhite : kRed);
        r_.rect(ox, oy - 28 * k, 200 * k, 8 * k, {0, 0, 0, 0.5f});
        r_.rect(ox, oy - 28 * k, 200 * k * clampf(hp / 100.0f, 0, 1), 8 * k, hp > 30 ? rgba(teamColor(specTarget_)) : kRed);
    }
    if (net_.selfAlive()) {
        auto he = [&](int i) { return cfg_.hud[i]; };
        auto tint = [&](int i, Color c) { const auto& e = cfg_.hud[i]; return e.custom ? Color{e.color.x, e.color.y, e.color.z, c.a} : c; };
        if (!he(Settings::HUD_HEALTH).hidden) {   // health: number + bar, bottom left
            auto e = he(Settings::HUD_HEALTH);
            float k = s * e.scale, ox = 24 * s + e.x * s, oy = H + e.y * s;
            int hp = net_.selfHealth();
            float hts = std::max(1.0f, std::round(ts * 2 * e.scale));
            r_.text(ox, oy - 70 * k, hts, std::to_string(hp), hp > 30 ? tint(Settings::HUD_HEALTH, kWhite) : kRed);
            r_.rect(ox, oy - 28 * k, 200 * k, 8 * k, {0, 0, 0, 0.5f});
            r_.rect(ox, oy - 28 * k, 200 * k * clampf(hp / 100.0f, 0, 1), 8 * k, hp > 30 ? tint(Settings::HUD_HEALTH, fc) : kRed);
        }
        bool unarmed = isItMode(ms.mode) && !(mep && (mep->flags & PS_HASBOMB));
        if (!unarmed && !he(Settings::HUD_WEAPON).hidden) {   // current weapon name, bottom right
            auto e = he(Settings::HUD_WEAPON);
            float wts = std::max(1.0f, std::round(ts * 1.5f * e.scale));
            std::string wn = weaponDef(heldWeapon_).name;
            r_.text(W - 24 * s + e.x * s - r_.textWidth(wn, wts), H - 50 * s * e.scale + e.y * s, wts, wn, tint(Settings::HUD_WEAPON, kWhite));
        }
        const MoveState& mv = net_.predicted();
        if (!paused_ && !(mv.mods & MOVE_RUN_ONLY) && !he(Settings::HUD_BARS).hidden) {   // dash + grapple cooldown bars under the crosshair (full = ready); hidden behind menus / without abilities
            auto e = he(Settings::HUD_BARS);
            float k = s * e.scale, bcx = cx + e.x * s, bcy = cy + e.y * s;
            Color full = tint(Settings::HUD_BARS, fc);
            auto bar = [&](float y, const char* label, float remaining, float total) {
                float bw = 90 * k, bh = 5 * k, bx = bcx - bw * 0.5f;
                float f = clampf(1.0f - remaining / total, 0, 1);
                r_.rect(bx - 1, y - 1, bw + 2, bh + 2, {0, 0, 0, 0.8f});
                r_.rect(bx, y, bw * f, bh, f >= 1 ? full : Color{0.6f, 0.62f, 0.66f, 0.9f});
                float lts = std::max(1.0f, std::round(ts * 0.5f * e.scale));
                r_.text(bx - r_.textWidth(label, lts) - 6 * k, y - 2 * k, lts, label, f >= 1 ? kWhite : kGrey);
            };
            int cw = heldWeapon_;
            bar(bcy + 40 * k, "WEAPON", (float)std::max(0.0, (cw == WPN_LOCATOR ? locatorReady_ : localNextFire_) - time_), weaponDef(cw).refire);   // (the locator: its recharge)
            {   // dash: a pip per charge (lit = there to spend; the ground refills them, a bhop landing one); while the
                // cooldown since the last dash runs, the charged pips fill grey like the old cooldown bar, lit when it's done
                int n = dashMaxCharges(mv.mods), have = std::min<int>(mv.dashCharges, n);
                const int cdTotal = dashCooldownMs(mv.mods);
                const float f = mv.dashCooldownMs > 0 && cdTotal > 0 ? clampf(1.0f - (float)mv.dashCooldownMs / cdTotal, 0, 1) : 1.0f;
                float bw = 90 * k, bh = 5 * k, bx = bcx - bw * 0.5f, y = bcy + 52 * k, gap = 4 * k, pw = (bw - gap * (n - 1)) / n;
                for (int i = 0; i < n; i++) {
                    float px = bx + i * (pw + gap);
                    r_.rect(px - 1, y - 1, pw + 2, bh + 2, {0, 0, 0, 0.8f});
                    if (i < have) r_.rect(px, y, pw * f, bh, f >= 1 ? full : Color{0.6f, 0.62f, 0.66f, 0.9f});
                }
                float lts = std::max(1.0f, std::round(ts * 0.5f * e.scale));
                r_.text(bx - r_.textWidth("DASH", lts) - 6 * k, y - 2 * k, lts, "DASH", have > 0 && f >= 1 ? kWhite : kGrey);
            }
            bar(bcy + 64 * k, "HOOK", mv.hookCooldown, kHookCooldown * moveTuning().m[MV_HOOK_COOLDOWN]);
        }
        if (!paused_ && !he(Settings::HUD_SPEED).hidden) {   // speed meter: speed is unbounded, so the bar fills on a saturating exponential curve
            // (1 - e^(-v/20)): half full at ~14 m/s, 90% at ~46 m/s, always room to grow. Number shows the real value.
            auto e = he(Settings::HUD_SPEED);
            float k = s * e.scale, bcx = cx + e.x * s, y = cy + 80 * k + e.y * s;
            float v = std::sqrt(mv.vel.x * mv.vel.x + mv.vel.z * mv.vel.z);
            float f = 1.0f - std::exp(-v / 20.0f);
            float bw = 90 * k, bh = 5 * k, bx = bcx - bw * 0.5f;
            Color c = tint(Settings::HUD_SPEED, Color{1.0f, 0.85f - 0.6f * f, 0.3f - 0.2f * f, 0.95f});
            r_.rect(bx - 1, y - 1, bw + 2, bh + 2, {0, 0, 0, 0.8f});
            r_.rect(bx, y, bw * f, bh, c);
            for (float m : {5.0f, 10.0f, 20.0f, 40.0f, 80.0f}) // tick marks at doubling speeds
                r_.rect(bx + bw * (1.0f - std::exp(-m / 20.0f)), y, 1, bh, {0, 0, 0, 0.6f});
            float lts = std::max(1.0f, std::round(ts * 0.5f * e.scale));
            char buf[16]; snprintf(buf, sizeof buf, "%.1f", v);
            r_.text(bx - r_.textWidth("SPEED", lts) - 6 * k, y - 2 * k, lts, "SPEED", kWhite);
            r_.text(bx + bw + 6 * k, y - 2 * k, lts, buf, kWhite);
        }
        if (cfg_.stickyCounter && !unarmed) {   // stickies out (the 7th removes the oldest)
            int out = 0;
            for (auto& pr : lastProjectiles_) out += pr.type == WPN_STICKY && pr.owner == net_.clientId();   // (this frame's)
            if (out > 0 || heldWeapon_ == WPN_STICKY) {
                auto e = he(Settings::HUD_WEAPON);
                float k = std::max(1.0f, std::round(ts * 0.9f * e.scale));
                std::string t = "STICKIES " + std::to_string(out) + "/" + std::to_string(kMaxStickies);
                r_.text(W - 24 * s - r_.textWidth(t, k) + e.x * s, H - 128 * s * e.scale + e.y * s, k, t, out >= kMaxStickies ? Color{1, 0.6f, 0.2f, 1} : tint(Settings::HUD_WEAPON, kWhite));
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
        if (!unarmed && !he(Settings::HUD_SLOTS).hidden) {   // weapon slots in kWeaponSlots order: 1 rocket, 2 rifle, 3 laser, ...
            auto e = he(Settings::HUD_SLOTS);
            float bx = W - 24 * s + e.x * s;
            float sts = std::max(1.0f, std::round(ts * 0.75f * e.scale));
            for (int k = WPN_COUNT - 1; k >= 0; k--) {
                int wi = kWeaponSlots[k];
                bool has = !ms.weaponMask || (ms.weaponMask >> wi & 1);
                if (!weaponInMode(wi, ms.mode) || (isItMode(ms.mode) && !has)) continue;   // (hide and seek / tag: just the knife and the locator)
                std::string t = std::to_string(weaponSlotKey(k)) + " " + weaponDef(wi).name;
                double awayLeft = weaponAwayLeft(wi);   // a thrown knife / syringe: disabled, greyed with its timer
                if (awayLeft > 0) t += " " + std::to_string((int)std::ceil(awayLeft - 1e-6)); // (seconds until it's back)
                float tw = r_.textWidth(t, sts);
                bx -= tw + 14 * s * e.scale;
                bool inPool = (!ms.weaponMask || (ms.weaponMask >> wi & 1)) && awayLeft <= 0;
                r_.text(bx, H - 74 * s * e.scale + e.y * s, sts, t, wi == heldWeapon_ && awayLeft <= 0 ? tint(Settings::HUD_SLOTS, fc) : inPool ? Color{0.85f, 0.87f, 0.9f, 0.9f} : Color{0.5f, 0.52f, 0.55f, 0.35f});
            }
        }
    } else if (ms.phase != PHASE_ROUND_END && ms.phase != PHASE_MATCH_END) {
        std::string t = mep && (mep->flags & PS_SPECTATOR) ? "SPECTATING" : "DEAD";
        if (specTarget_ >= 0) t += " - WATCHING " + net_.playerName(specTarget_) + " (CLICK TO CYCLE)";
        ui_.labelCentered(cx, H - 60 * s, t, kGrey);
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
    for (auto& k : killFeed_) {
        float tw = r_.textWidth(k.text, ts);
        r_.rect(W - tw - 20 * s, y - 3 * s, tw + 12 * s, r_.textHeight(ts) + 6 * s, {0, 0, 0, 0.4f});
        drawFeedText(W - tw - 14 * s, y, ts, k);
        y += r_.textHeight(ts) + 8 * s;
    }
    // chat (bottom left)
    drawChatBox(W, H, s, ts);
    // notices
    drawNotices(H * 0.3f, ts, s);
    if (time_ < centerUntil_) ui_.labelCentered(cx, H * 0.36f, centerText_, kWhite, 1.5f);
    drawVotePanel(cx, s, ts);

    // host info
    // the tall in-game submenus (settings, map / mode lists) start higher up, over where the host info sits
    bool tallMenu = paused_ && (settingsInGame_ || picker_ != PICK_NONE);
    if (host_ && paused_ && !tallMenu) {
        // under the top panel, clear of the pause menu
        std::string st = "HOSTING ON UDP PORT " + std::to_string(host_->port());
        ui_.labelCentered(cx, 84 * s, st, kGrey, 0.6f);
        if (host_->config().upnp && host_->upnpStatus().size())
            ui_.labelCentered(cx, 96 * s, upper(host_->upnpStatus()), !host_->upnpDone() ? kGrey : host_->upnpOk() ? kGreenOk : kRed, 0.6f);
        std::string ls = upper(host_->listStatus());
        if (!ls.empty()) ui_.labelCentered(cx, 108 * s, ls, kGrey, 0.6f);
        Server::Reach rc = host_->reachability();
        if (rc != Server::REACH_OFF) {   // can friends actually get in? (a public server sent a packet to our port)
            ui_.labelCentered(cx, 121 * s, upper(host_->reachStatus()), rc == Server::REACH_OK ? kGreenOk : rc == Server::REACH_BLOCKED ? kRed : kGrey, 0.6f);
            if (rc != Server::REACH_CHECKING && ui_.button(cx - 50 * s, 131 * s, 100 * s, 16 * s, "CHECK AGAIN", UiExp::state("reach"))) host_->recheckReachability();
        }
        // C9: a ranked player dialled an address this server can't see as its own (a forwarded port, a container)
        std::string own = host_->ownAddrHint();
        if (!own.empty()) {
            float oy = rc != Server::REACH_OFF ? 152 * s : 121 * s;
            ui_.labelCentered(cx, oy, "A PLAYER JOINED VIA " + own + ", WHICH THIS PC CAN'T CONFIRM IS ITSELF: NO RANKED ID", kRed, 0.55f);
            ui_.labelCentered(cx, oy + 11 * s, "IF THAT ADDRESS REACHES YOU, HOST A DEDICATED SERVER WITH --own-addr " + own, kGrey, 0.55f);
        }
    }

    if (rpsOn) drawRps(W, H, s);   // the hide and seek tiebreak (under the scoreboard and menus)
    showScores_ = (isDown(A_SCORES) && !chatting_) || opts_.textHeavy || ms.phase == PHASE_MATCH_END;
    // tourney: the bracket between matches / rounds and on TAB, so everyone sees who's still in
    bool showBracket = ms.mode == MODE_TOURNEY && !bracket_.empty() &&
                       (showScores_ || ms.phase == PHASE_ROUND_END || (ms.phase == PHASE_FREEZE && ms.round <= 1));
    if (showBracket && !paused_) drawBracket();
    else if (showScores_ && !paused_) drawScoreboard();
    if (paused_) {
        // dim the game behind the menu so HUD text doesn't show through its panels; not on the pages
        // that preview the HUD / crosshair over the game
        if (!(settingsInGame_ && (hudOpen_ || widgetEdit_ || crosshairOpen_))) r_.rect(0, 0, W, H, {0.03f, 0.035f, 0.05f, 0.55f});
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

// "In <mode> use <profile>" for HUD layouts (hud) or control profiles.
void Game::ruleRow(float x, float y, float w, float h, bool hud) {
    float gap = 5 * ui_.s(), mw = std::max(w * 0.35f, std::min(w * 0.5f, 210 * ui_.s()));   // (narrow: "IN NPAYLOAD" was cut off)
    static std::vector<std::string> mn;
    if (mn.empty()) for (int m = 0; m < MODE_COUNT; m++) mn.push_back(std::string("IN ") + upper(modeName(m)));
    ui_.cycle(x, y, mw, h, "MODE", ruleMode_, MODE_COUNT, mn, UiExp::local());
    const auto& files = profilesCached(hud ? "huds" : "controls", time_);
    std::string& rule = hud ? cfg_.modeHud[ruleMode_] : cfg_.modeCtl[ruleMode_];
    std::vector<std::string> fn = {hud ? "MY HUD" : "MY CONTROLS"};
    int cur = 0;
    for (auto& f : files) { fn.push_back(upper(f)); if (f == rule) cur = (int)fn.size() - 1; }
    if (!rule.empty() && cur == 0) { fn.push_back(upper(rule) + " (MISSING)"); cur = (int)fn.size() - 1; }
    if (ui_.cycle(x + mw + gap, y, w - mw - gap, h, "PROFILE", cur, (int)fn.size(), fn,
                  (fn.size() < 2 ? UiExp::inert() : UiExp::cfg(hud ? "mode_hud_*" : "mode_controls_*")).clipOk())) {   // (file names: may be long)
        rule = cur == 0 || cur > (int)files.size() ? "" : files[cur - 1];
        cfg_.save();
        appliedMode_ = -2; // re-apply now
    }
    ui_.label(x, y + h + 3 * ui_.s(), "MODE RULES NEVER APPLY IN RANKED (DEFAULT HUD + CONTROLS)", {0.7f, 0.72f, 0.75f, 1}, 0.5f);
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
    set(HV_DASH, (float)mv.dashCharges, (float)dashMaxCharges(mv.mods));   // dash charges left (1.17: was % of the cooldown)
    set(HV_HOOK, 100.0f * (1.0f - clampf(mv.hookCooldown / (kHookCooldown * moveTuning().m[MV_HOOK_COOLDOWN]), 0, 1)), 100);
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
    if (k == "voice") audio_.setAnnouncer(cfg_.announcerVoice);   // (it resamples every line: only when it's the one set)
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

void Game::drawChatBox(float W, float H, float s, float ts) {
    float y;
    while (!chatFeed_.empty() && chatFeed_.front().until < time_ && !chatting_) chatFeed_.pop_front();
    {
    const auto& lines = chatting_ ? chatLog_ : chatFeed_;   // typing: the whole recent history
    size_t shown = 0;   // chat off: players' lines are hidden (server messages stay)
    for (auto& c : lines) shown += !(c.player && cfg_.chatOff);
    y = H - 110 * s - shown * (r_.textHeight(ts) + 4 * s);
    if (chatting_ && shown) r_.rect(18 * s, y - 4 * s, 520 * s, shown * (r_.textHeight(ts) + 4 * s) + 4 * s, {0, 0, 0, 0.4f});
    for (auto& c : lines) {
        if (c.player && cfg_.chatOff) continue;
        float cw = c.crown ? r_.textHeight(ts) * 1.2f : 0;
        if (c.crown) drawCrown(24 * s, y + r_.textHeight(ts) * 0.1f, cw, c.crown);
        drawFeedText(24 * s + (cw ? cw + 4 * s : 0), y, ts, c);
        y += r_.textHeight(ts) + 4 * s;
    }
    }
    if (chatting_ && console_) drawConsole(W, H, s, ts);
    else if (chatting_) {
        if ((!uiIn_.chars.empty() || uiIn_.backspace) && time_ >= nextTypingSend_) { net_.sendCommand("typing"); nextTypingSend_ = time_ + 0.25; }
        chatEdit(120, false);
        float lts = std::max(1.0f, std::round(ui_.textScale()));
        ui_.label(24 * s, H - 100 * s, "SAY: " + chatText_, kWhite);
        drawCaret(24 * s, H - 100 * s, lts, false, "SAY: " + chatText_.substr(0, chatCursor_));
    }
}

// A running vote: what it is, the count, F1 / F2.
void Game::drawVotePanel(float cx, float s, float ts) {
    const ModeState& ms = net_.mode();
    if (ms.voteActive) {
        char vb[200];
        snprintf(vb, sizeof vb, "VOTE: %s   YES %d  NO %d  OF %d   F1 YES / F2 NO   %ds", voteText_.c_str(), ms.voteYes, ms.voteNo,
                 ms.voteVoters, (int)std::ceil(ms.voteTimeLeft));
        float vw = r_.textWidth(vb, ts) + 24 * s;
        r_.rect(cx - vw * 0.5f, 122 * s, vw, r_.textHeight(ts) + 12 * s, {0, 0, 0, 0.6f});
        ui_.labelCentered(cx, 128 * s, vb, kWhite);
    }
}

void Game::drawKeyOverlay(float W, float H, float s) {
    const auto& e = cfg_.hud[Settings::HUD_KEYS];
    float k = s * e.scale, u = 30 * k, g = 4 * k;
    float ox = W * 0.5f - (4 * u + 3 * g) * 0.5f + e.x * s, oy = H - 230 * s + e.y * s;
    Color lit = e.custom ? Color{e.color.x, e.color.y, e.color.z, 0.9f} : rgba(cfg_.colorFriendly, 0.9f);
    float ts = std::max(1.0f, std::round(ui_.textScale() * 0.5f * e.scale));
    // alive: our own keys; spectating: what the watched player is holding (from the snapshot)
    const int spec = net_.selfAlive() ? -1 : specKeysOf();
    const uint16_t held = spec >= 0 ? net_.findPlayer(spec)->keys : 0;
    auto key = [&](float col, float row, float wUnits, const char* label, int action, uint16_t bit) {
        float x = ox + col * (u + g), y = oy + row * (u + g), w = wUnits * u + (wUnits - 1) * g;
        bool on = spec >= 0 ? (held & bit) != 0 : isDown(action);
        r_.rect(x, y, w, u, on ? lit : Color{0.05f, 0.06f, 0.08f, 0.55f});
        r_.text(x + (w - r_.textWidth(label, ts)) * 0.5f, y + (u - r_.textHeight(ts)) * 0.5f, ts, label, on ? Color{0, 0, 0, 1} : Color{0.85f, 0.87f, 0.9f, 0.9f});
    };
    key(1, 0, 1, "W", A_FORWARD, KEY_FWD);
    key(0, 1, 1, "A", A_LEFT, KEY_LEFT); key(1, 1, 1, "S", A_BACK, KEY_BACK); key(2, 1, 1, "D", A_RIGHT, KEY_RIGHT);
    key(3, 0, 1, "LMB", A_FIRE, KEY_FIRE); key(3, 1, 1, "RMB", A_ALTFIRE, KEY_ALT);
    key(0, 2, 2, "JUMP", A_JUMP, KEY_JUMP); key(2, 2, 2, "CROUCH", A_CROUCH, KEY_CROUCH);
    key(0, 3, 2, "DASH", A_DASH, KEY_DASH); key(2, 3, 2, "HOOK", A_GRAPPLE, KEY_HOOK);
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
    float pw = 380 * s, px = W - pw - 16 * s, py = 16 * s, h = 24 * s, gap = 5 * s;
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
    int rows = widgetSel_ >= 0 ? 17 : 5;
    ui_.panel(px - 12 * s, py - 12 * s, pw + 24 * s, rows * (h + gap) + 40 * s);
    ui_.label(px, py, "CLICK A WIDGET TO EDIT, DRAG TO MOVE. IMPORT/EXPORT = CLIPBOARD", kWhite, 0.5f);
    float y = py + 18 * s, hw = pw * 0.5f - gap * 0.5f;
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
    if (time_ < widgetMsgUntil_) ui_.label(px, y, widgetMsg_, ui_.accent, 0.5f);
    else if (!widgets_.error().empty()) ui_.label(px, y, widgets_.error(), kRed, 0.5f);
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
    static const std::vector<std::string> stn = {"STYLE: NUMBER", "STYLE: BAR", "STYLE: COLOUR-GRADED BAR", "STYLE: ICON (OPACITY)", "STYLE: ICON + BAR"};
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
        ui_.label(px, y + 4 * s, name, kWhite, 0.5f);
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
    rgba4("COLOUR (FULL)", w.color, dw.color);
    ui_.toggle(px, y, pw, h, "GRADE FROM LOW COLOUR", w.grade, UiExp::state("widgets"));
    y += h + gap;
    if (w.grade || w.style == HudWidget::GRADBAR) rgba4("LOW COLOUR (EMPTY)", w.low, dw.low);
    ui_.label(px, y + 4 * s, "LABEL", kWhite, 0.5f);
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

// Your own race history from race_times.log: best, recent runs, average, best possible from your best splits.
void Game::drawRaceStats(float cx, float y) {
    if (!raceRunsLoaded_) loadRaceRuns();
    float s = ui_.s(), w = 720 * s, x = cx - w * 0.5f, rowH = 17 * s;
    float ts = std::max(1.0f, std::round(ui_.textScale() * 0.6f));
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 26 * rowH + 60 * s, 0.85f);
    ui_.labelCentered(cx, y, "MY VIADUCT RUNS");
    y += 34 * s;
    std::vector<const RaceRun*> runs;
    for (auto& r : raceRuns_) if (r.map == "viaduct") runs.push_back(&r);
    if (runs.empty()) { r_.text(x, y, ts, "NO RUNS YET - FINISH VIADUCT IN A TIME TRIAL OR A RACE", kGrey); return; }
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
    auto row = [&](float cx0, float yy, const std::string& a, const std::string& b, Color c = kWhite) {
        r_.text(cx0, yy, ts, a, kGrey);
        r_.text(cx0 + 230 * s - r_.textWidth(b, ts), yy, ts, b, c);
    };
    float lx = x, rx = x + 260 * s, yy = y;
    row(lx, yy, "RUNS", std::to_string(runs.size())); yy += rowH;
    row(lx, yy, "TIME TRIAL RUNS", std::to_string(std::count_if(runs.begin(), runs.end(), [](const RaceRun* r) { return r->trial; }))); yy += rowH;
    row(lx, yy, "BEST", fmtRace(best->ms), Color{1.0f, 0.8f, 0.3f, 1}); yy += rowH;
    row(lx, yy, "BEST POSSIBLE (SEGMENTS)", fmtRace((int)possible), ui_.accent); yy += rowH;
    row(lx, yy, "AVERAGE", fmtRace((int)(sum / runs.size()))); yy += rowH;
    row(lx, yy, "AVERAGE, LAST " + std::to_string(recentN), fmtRace((int)(recent / recentN))); yy += rowH;
    row(lx, yy, "LAST RUN", fmtRace(runs.back()->ms)); yy += rowH * 2;
    // best run's splits vs your best segment at each checkpoint
    r_.text(lx, yy, ts, "CHECKPOINT   BEST RUN   BEST SEGMENT", kGrey); yy += rowH;
    for (int i = 0; i < total && i < 16; i++) {
        char b[96];
        int seg = (int)best->splits.size() == total ? best->splits[i] - (i ? best->splits[i - 1] : 0) : 0;
        snprintf(b, sizeof b, "%-12s %9s  %s", i == total - 1 ? "FINISH" : ("CP " + std::to_string(i + 1)).c_str(),
                 (int)best->splits.size() == total ? fmtRace(best->splits[i]).c_str() : "-",
                 bestSeg[i] == INT32_MAX ? "-" : (fmtRace(bestSeg[i]) + (seg > bestSeg[i] ? "  (LOSES " + fmtRace(seg - bestSeg[i]) + ")" : "")).c_str());
        r_.text(lx, yy, ts, b, kWhite);
        yy += rowH;
    }
    // recent runs, newest first, with a bar against your best
    yy = y;
    r_.text(rx + 120 * s, yy, ts, "RECENT RUNS", kGrey); yy += rowH;
    for (int i = 0; i < 22 && i < (int)runs.size(); i++) {
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
        r_.text(rx + 120 * s, yy, ts, std::string(d) + (r->trial ? "  TT  " : "      ") + fmtRace(r->ms), c);
        float f = clampf((float)best->ms / r->ms, 0, 1);
        r_.rect(rx + 120 * s + 210 * s, yy + 2 * s, 100 * s * f, rowH - 6 * s, {c.r, c.g, c.b, 0.5f});
        yy += rowH;
    }
}

void Game::drawLeaderboard(float cx, float y) {
    static const int ladders[3] = {0, 1, 5};
    float s = ui_.s(), gap = 14 * s;
    // four columns of 236 at 16:9; narrower ones (names cut shorter) when the screen is squarer, e.g. 4:3
    float colW = std::min(236 * s, ((float)r_.width() - 48 * s - 3 * gap) / 4), w = 4 * colW + 3 * gap, x0 = cx - w * 0.5f;
    auto head = [&](float x, float yy, const std::string& t) { // column title, a whole step smaller if it won't fit
        float hs = std::max(1.0f, std::round(ui_.textScale() * 0.8f));
        while (hs > 1 && r_.textWidth(t, hs) > colW - 8 * s) hs -= 1;
        r_.text(x + (colW - r_.textWidth(t, hs)) * 0.5f, yy + 4 * s, hs, t, kWhite);
    };
    if (raceStatsView_) {
        drawRaceStats(cx, y);
        float by = y + 26 * 17 * s + 54 * s;
        if (ui_.button(cx - 210 * s, by, 200 * s, 36 * s, "LEADERBOARD", UiExp::nav("leaderboard"))) raceStatsView_ = false;
        if (ui_.button(cx + 10 * s, by, 200 * s, 36 * s, "PLAY TIME TRIAL", UiExp::state("hosting", "1"))) startTimeTrial();
        return;
    }
    if (time_ >= nextBoardFetch_) {
        rank_.fetchRaceBoard("viaduct");
        for (int v : ladders) rank_.fetchLeaderboard(arenaVariant(v).name);
        if (!rankKey_.id.empty()) for (int v : ladders) rank_.fetch(arenaVariant(v).name, {rankKey_.id});
        nextBoardFetch_ = time_ + 30;
    }
    float rowH = 17 * s, ts = std::max(1.0f, std::round(ui_.textScale() * 0.6f));
    ui_.panel(x0 - 16 * s, y - 16 * s, w + 32 * s, 20 * rowH + 130 * s, 0.85f);
    ui_.labelCentered(cx, y, "RANKED LEADERBOARD");
    y += 34 * s;
    for (int c = 0; c < 3; c++) {
        float x = x0 + c * (colW + gap), yy = y;
        std::string ladder = arenaVariant(ladders[c]).name;
        r_.rect(x, yy, colW, 26 * s, {ui_.accent.r * 0.45f, ui_.accent.g * 0.45f, ui_.accent.b * 0.45f, 0.95f});
        head(x, yy, arenaVariant(ladders[c]).title);
        yy += 32 * s;
        std::vector<RankService::LeaderRow> rows;
        if (!rank_.leaderboard(ladder, rows)) { r_.text(x + 6 * s, yy, ts, "LOADING...", kGrey); continue; }
        if (rows.empty()) r_.text(x + 6 * s, yy, ts, "NOBODY RANKED YET (3 GAMES)", kGrey);
        if (!rows.empty() && rows[0].rating < 0) { r_.text(x + 6 * s, yy, ts, upper(rows[0].name), kGrey); continue; }
        for (size_t i = 0; i < rows.size() && i < 20; i++) {
            std::string rt = std::to_string(rows[i].rating);
            std::string n = r_.textClip(std::to_string(i + 1) + ". " + rows[i].name, ts, colW - 18 * s - r_.textWidth(rt, ts));
            Color c2 = i == 0 ? Color{1.0f, 0.8f, 0.3f, 1} : i < 3 ? kWhite : Color{0.8f, 0.82f, 0.85f, 1};
            r_.text(x + 6 * s, yy, ts, n, c2);
            r_.text(x + colW - 6 * s - r_.textWidth(rt, ts), yy, ts, rt, c2);
            yy += rowH;
        }
        Rating mine;
        if (rank_.rating(ladder, rankKey_.id, mine))
            r_.text(x + 6 * s, y + 32 * s + 20 * rowH + 6 * s, ts, "YOU: " + std::to_string(mine.display()) + (mine.games < kPlacementGames ? "? " : " ") + "(" + std::to_string(mine.games) + " GAMES)", ui_.accent);
    }
    {   // race: fastest Viaduct time trials
        float x = x0 + 3 * (colW + gap), yy = y;
        r_.rect(x, yy, colW, 26 * s, {1.0f * 0.45f, 0.78f * 0.45f, 0.25f * 0.45f, 0.95f});
        head(x, yy, "VIADUCT TIME TRIAL");
        yy += 32 * s;
        std::vector<RankService::LeaderRow> rows;
        if (!rank_.raceBoard("viaduct", rows)) r_.text(x + 6 * s, yy, ts, "LOADING...", kGrey);
        else if (!rows.empty() && rows[0].rating < 0) r_.text(x + 6 * s, yy, ts, upper(rows[0].name), kGrey);
        else {
            if (rows.empty()) r_.text(x + 6 * s, yy, ts, "NO TIMES YET", kGrey);
            for (size_t i = 0; i < rows.size() && i < 20; i++) {
                std::string t = fmtRace(rows[i].rating);
                std::string n = r_.textClip(std::to_string(i + 1) + ". " + rows[i].name, ts, colW - 18 * s - r_.textWidth(t, ts));
                Color c2 = i == 0 ? Color{1.0f, 0.8f, 0.3f, 1} : i < 3 ? kWhite : Color{0.8f, 0.82f, 0.85f, 1};
                r_.text(x + 6 * s, yy, ts, n, c2);
                r_.text(x + colW - 6 * s - r_.textWidth(t, ts), yy, ts, t, c2);
                yy += rowH;
            }
        }
        if (!raceRunsLoaded_) loadRaceRuns();
        if (const RaceRun* b = bestRun("viaduct", true))
            r_.text(x + 6 * s, y + 32 * s + 20 * rowH + 6 * s, ts, "YOU: " + fmtRace(b->ms), ui_.accent);
    }
    float by = y + 32 * s + 20 * rowH + 30 * s, bw3 = 200 * s;
    if (ui_.button(cx - bw3 * 1.5f - 10 * s, by, bw3, 36 * s, "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
    if (ui_.button(cx - bw3 * 0.5f, by, bw3, 36 * s, "MY RACE STATS", UiExp::nav("leaderboard/race-stats"))) raceStatsView_ = true;
    if (ui_.button(cx + bw3 * 0.5f + 10 * s, by, bw3, 36 * s, "PLAY TIME TRIAL", UiExp::state("hosting", "1"))) startTimeTrial();
}

void Game::drawScoreboard() {
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f;
    float ts = ui_.textScale();
    float w = std::min(900 * s, W - 32 * s), x = (W - w) * 0.5f, y = 140 * s;
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
    if (ps.size() > 12) { // big servers (up to 32): smaller rows so everyone fits
        ts = std::max(1.0f, std::round(ts * 0.75f));
        y = 90 * s;
    }
    float rowH = std::min(r_.textHeight(ts) + 10 * s, (H - y - 40 * s) / (ps.size() + 2));
    ui_.panel(x, y, w, (ps.size() + 2) * rowH + 10 * s, 0.8f);
    float cy = y + 10 * s;
    r_.text(x + 12 * s, cy, ts, "PLAYER", kGrey);
    std::string ladder = arenaVariant(ms.arenaVariant).name;
    if (ranked) r_.text(x + w * 0.31f, cy, ts, "RATING", kGrey);
    r_.text(x + w * 0.79f, cy, ts, "TOTAL K / D", kGrey);   // lifetime kills / deaths (every game, live)
    if (gib) {
        r_.text(x + w * 0.53f, cy, ts, "GIBS", kGrey);
        r_.text(x + w * 0.65f, cy, ts, "ACC", kGrey);
    } else {
        r_.text(x + w * 0.40f, cy, ts, ms.mode == MODE_RACE ? "WINS" : "SCORE", kGrey);
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
        {   // lifetime totals so far + this game's kills / deaths (sent when the game ends), so it counts up live
            auto lt = lifetime_.find(p.id == net_.clientId() ? rankKey_.id : net_.rankId(p.id));
            if (lt != lifetime_.end()) r_.text(x + w * 0.79f, cy, ts, std::to_string(lt->second.first + p.kills) + " / " + std::to_string(lt->second.second + p.deaths), kWhite);
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

// Everything players can call a vote on (majority of human players decides).
void Game::drawVoteMenu(float cx, float y) {
    float s = ui_.s();
    float w = 300 * s, h = 36 * s, gap = 8 * s, x = cx - w * 0.5f, hw = (w - gap) * 0.5f;
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
    if (specPick_ && canSpec) {
        const int n = (int)specable.size(), cols = (n + 9) / 10, rowsN = (n + cols - 1) / cols;   // (up to 10 a column: a full server fits)
        const float tw = cols * w + (cols - 1) * gap, tx = cx - tw * 0.5f;
        ui_.panel(tx - 16 * s, y - 16 * s, tw + 32 * s, (rowsN + 1) * (h + gap) + 40 * s);
        ui_.labelCentered(cx, y, "VOTE: MOVE TO SPECTATOR (UNTIL THE NEXT MAP)");
        y += 30 * s;
        for (int k = 0; k < n; k++) {
            if (ui_.button(tx + (k / rowsN) * (w + gap), y + (k % rowsN) * (h + gap), w, h, upper(net_.playerName(specable[k])), UiExp::cmd("callvote spec").withId("PLAYER"))) {
                net_.sendCommand("callvote spec " + std::to_string(specable[k]));   // (by id: names can be alike)
                specPick_ = voteMenu_ = paused_ = false;
            }
        }
        y += rowsN * (h + gap);
        if (ui_.button(x, y, w, h, "BACK", UiExp::nav("ingame/pause/vote"))) specPick_ = false;
        return;
    }
    specPick_ = false;
    int rows = 3 + (warmup ? 1 : 0) + (warmup && tourney ? 1 : 0) + (ranked ? 0 : 4) + (canSpec ? 1 : 0);
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, rows * (h + gap) + 40 * s);
    ui_.labelCentered(cx, y, "CALL A VOTE");
    y += 30 * s;
    auto voteBtn = [&](float bx, float bw, const std::string& label, const char* cmd) {   // cmd: "callvote ..."
        if (ui_.button(bx, y, bw, h, label, UiExp::cmd(cmd))) { net_.sendCommand(cmd); voteMenu_ = false; paused_ = false; }
    };
    if (warmup) { voteBtn(x, w, "START THE MATCH", "callvote start"); y += h + gap; }
    if (warmup && tourney) {
        if (ui_.button(x, y, hw, h, "TOURNEY MAPS", UiExp::nav("ingame/pause/picker:tmaps"))) { multiSel_.clear(); picker_ = PICK_TMAPS; }
        if (ui_.button(x + hw + gap, y, hw, h, "TOURNEY RULES", UiExp::nav("ingame/pause/picker:tmodes"))) { multiSel_.clear(); picker_ = PICK_TMODES; }
        y += h + gap;
    }
    // one vote for both: pick the mode, then a map for it
    if (ui_.button(x, y, w, h, "CHANGE MAP / MODE", UiExp::nav("ingame/pause/picker:livemode")) && !maps_.empty()) openLivePicker(PICK_LIVE_MODE, true);
    y += h + gap;
    // ranked: on with a ladder / switch ladder / off (no bots while it's on)
    if (ui_.button(x, y, ranked ? hw : w, h, ranked ? "LADDER" : "RANKED", UiExp::nav("ingame/pause/picker:ladders"))) { variantCtx_ = 2; livePickVote_ = true; picker_ = PICK_RANKED; }
    if (ranked) voteBtn(x + hw + gap, hw, "RANKED OFF", "callvote ranked off");
    y += h + gap;
    if (!ranked) { // edit the map together; on a normal server the changes last until the map changes
        voteBtn(x, w, "EDIT THE MAP TOGETHER", "callvote edit on");
        y += h + gap;
        if (canSpec) {
            if (ui_.button(x, y, w, h, "MOVE A PLAYER TO SPECTATOR", UiExp::nav("ingame/pause/vote/spec"))) specPick_ = true;
            y += h + gap;
        }
        voteBtn(x, hw, "BOTS ON", "callvote bots on");
        voteBtn(x + hw + gap, hw, "BOTS OFF", "callvote bots off");
        y += h + gap;
        static const char* skills[4] = {"BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"};
        float qw = (w - gap) * 0.5f;
        for (int k = 0; k < 4; k++) {
            if (k == 2) y += h + gap;
            float bx = x + (k % 2) * (qw + gap);
            static const char* shortName[4] = {"EASY", "MEDIUM", "HARD", "EXPERT"};
            if (ui_.button(bx, y, qw, h, std::string(shortName[k]) + " BOTS", UiExp::cmd("callvote botskill"))) {
                net_.sendCommand(std::string("callvote botskill ") + skills[k]); voteMenu_ = false; paused_ = false;
            }
        }
        y += h + gap;
    }
    if (ui_.button(x, y, w, h, "BACK", UiExp::nav("ingame/pause"))) voteMenu_ = false;
}

void Game::drawPauseMenu(float cx, float y) {
    float s = ui_.s();
    float w = 300 * s, h = 36 * s, gap = 8 * s, x = cx - w * 0.5f;
    if (settingsInGame_) {
        drawSettings(x - 60 * s, y, w + 120 * s);
        return;
    }
    if (picker_ != PICK_NONE) { drawPicker(cx, y); return; }
    if (voteMenu_) { drawVoteMenu(cx, y); return; }
    if (queueMenu_) { drawQueueMenu(cx, y); return; }
    if (net_.playback()) { // watching a demo
        ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 4 * (h + gap) + 24 * s);
        if (ui_.button(x, y, w, h, "RESUME", UiExp::nav("ingame"))) { paused_ = false; }
        y += h + gap;
        if (ui_.button(x, y, w, h, "STOP DEMO", UiExp::nav("demos"))) { leaveGame(); setScreen(Screen::Demos); return; }
        y += h + gap;
        if (ui_.button(x, y, w, h, "SETTINGS", UiExp::nav("ingame/pause/settings"))) settingsInGame_ = true;
        y += h + gap;
        if (ui_.button(x, y, w, h, "QUIT", UiExp::quit())) quit_ = true;
        return;
    }
    bool teamMode = modeIsTeamBased(net_.mode().mode);
    int rows = 8 + (host_ ? 2 : 0) + (host_ && !(net_.mode().flags & MS_RANKED) ? 1 : 0);
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, rows * (h + gap) + 24 * s);
    if (ui_.button(x, y, w, h, "RESUME", UiExp::nav("ingame"))) { paused_ = false; }
    y += h + gap;
    if (teamMode) {
        float hw = (w - gap) * 0.5f;
        if (ui_.button(x, y, hw, h, "JOIN TEAM A", UiExp::cmd("team a"))) { net_.sendCommand("team a"); paused_ = false; }
        if (ui_.button(x + hw + gap, y, hw, h, "JOIN TEAM B", UiExp::cmd("team b"))) { net_.sendCommand("team b"); paused_ = false; }
        y += h + gap;
    } else {
        if (ui_.button(x, y, w, h, "PLAY", UiExp::cmd("team a"))) { net_.sendCommand("team a"); paused_ = false; }
        y += h + gap;
    }
    if (ui_.button(x, y, w, h, "SPECTATE", UiExp::cmd("team spec"))) { net_.sendCommand("team spec"); paused_ = false; }
    y += h + gap;
    {
        float hw = (w - gap) * 0.5f;
        if (ui_.button(x, y, hw, h, "VOTE", UiExp::nav("ingame/pause/vote"))) voteMenu_ = true;
        if (ui_.button(x + hw + gap, y, hw, h, queued_.empty() ? "QUEUE" : "QUEUE (" + std::to_string(queued_.size()) + ")", UiExp::nav("ingame/pause/queue").withId("QUEUE"))) queueMenu_ = true;
    }
    y += h + gap;
    if (host_) {
        // Host controls: change mode/map live.
        float hw = (w - gap) * 0.5f;
        if (ui_.button(x, y, w, h, "CHANGE MAP / MODE", UiExp::nav("ingame/pause/picker:livemode")) && !maps_.empty()) openLivePicker(PICK_LIVE_MODE, false);
        y += h + gap;
        if (ui_.button(x, y, hw, h, "FORCE START", UiExp::cmd("forcestart"))) { net_.sendCommand("forcestart"); paused_ = false; }
        if (ui_.button(x + hw + gap, y, hw, h, "RELOAD MAP", UiExp::cmd("reload"))) { net_.sendCommand("reload"); paused_ = false; } // picks up edits to the map file
        y += h + gap;
        if (!(net_.mode().flags & MS_RANKED) && ui_.button(x, y, w, h, "EDIT THE MAP TOGETHER", UiExp::cmd("editmode on"))) { net_.sendCommand("editmode on"); paused_ = false; } // edit together
        if (!(net_.mode().flags & MS_RANKED)) y += h + gap;
    }
    if (net_.recording()) {
        if (ui_.button(x, y, w, h, "STOP RECORDING (" + fmtTime((float)net_.recordingTime()) + ")", UiExp::state("recording", "0").withId("STOP RECORDING"))) { net_.stopRecording(); notify("DEMO SAVED - MAIN MENU > DEMOS", 4); }
    } else if (ui_.button(x, y, w, h, "RECORD DEMO", UiExp::state("recording", "1"))) startRecording();
    y += h + gap;
    if (ui_.button(x, y, w, h, "SETTINGS", UiExp::nav("ingame/pause/settings"))) settingsInGame_ = true;
    y += h + gap;
    if (ui_.button(x, y, w, h, !testMap_.empty() ? "BACK TO EDITOR" : host_ ? "STOP HOSTING" : "DISCONNECT", UiExp::nav(!testMap_.empty() ? "editor" : "main"))) { leaveGame(); return; }
    y += h + gap;
    if (ui_.button(x, y, w, h, "QUIT", UiExp::quit())) quit_ = true;
}

// ---------------------------------------------------------------------------
// menus

void Game::drawSettings(float x0, float y0, float wTotal) {
    if (controlsOpen_) { drawControls(x0, y0, wTotal); return; }
    if (crosshairOpen_) { drawCrosshairSettings(x0, y0, wTotal); return; }
    if (widgetEdit_) { drawWidgetEditor(); return; }
    if (hudOpen_) { drawHudSettings(x0, y0, wTotal); return; }
    if (visualsOpen_) { drawVisualSettings(x0, y0, wTotal); return; }
    float s = ui_.s();
    float h = 32 * s, gap = 7 * s, colGap = 24 * s;
    float w = std::max(370 * s, wTotal * 0.5f);   // (320: HITSOUND / PLAYER SHADOWS / VOICE were cut off at 720p - UI test)
    float x = x0 + wTotal * 0.5f - w - colGap * 0.5f; // two columns centred on the old area
    float xr = x + w + colGap;
    float ph = 14 * (h + gap) + 150 * s;
    y0 = std::max(24 * s, std::min(y0, (float)r_.height() - ph + 8 * s)); // panel bottom stays on screen
    ui_.panel(x - 16 * s, y0 - 16 * s, w * 2 + colGap + 32 * s, ph);

    // --- left column: player, mouse, colors
    float y = y0;
    ui_.label(x, y, "NAME");
    // Discord: run /link in the Discord server, type the 6-digit code the bot gives you here ("unlink" to remove it)
    // (linking makes the Discord name public next to your game name and ranked id: crowns and queue fills)
    {   // (fits its half column: shrunk, then a long linked name clipped, keeping "(PUBLIC)")
        float lw = w * 0.5f - gap * 0.5f;
        std::string dl = cfg_.discordName.empty() ? "DISCORD CODE (/link) - PUBLIC"
            : "DISCORD: " + r_.textClip(cfg_.discordName, Renderer::kMinTextScale, lw - r_.textWidth("DISCORD:  (PUBLIC)", Renderer::kMinTextScale)) + " (PUBLIC)";
        ui_.labelFit(x + w * 0.5f + gap * 0.5f, y, lw, dl);
    }
    y += 22 * s;
    ui_.textField(1, x, y, w * 0.5f - gap * 0.5f, h, cfg_.name, 20, UiExp::cfg("name").withId("NAME"));
    ui_.textField(43, x + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, discordCode_, 8, UiExp::local().withId("DISCORD CODE"));
    y += h + gap;
    {   // sensitivity, or the same thing as cm per 360 at your mouse DPI (drag either)
        ui_.slider(x, y, w, h, "SENSITIVITY", cfg_.sensitivity, 0.1f, 10.0f, "%.2f", UiExp::cfg("sensitivity"), defaultSettings().sensitivity);
        cfg_.sensitivity = clampf(cfg_.sensitivity, 0.01f, 50.0f);   // (typed: quark.cfg's range)
        y += h + gap;
        float cw = w * 0.6f, dw = w - cw - gap;
        // counts per 360 = 360 / (sens * 0.022); cm = counts / dpi * 2.54
        auto cmOf = [&](float sens) { return 360.0f / (sens * 0.022f) / cfg_.dpi * 2.54f; };
        float cm = cmOf(cfg_.sensitivity);
        if (ui_.slider(x, y, cw, h, "CM/360", cm, 5, 120, "%.1f", UiExp::cfg("sensitivity"), cmOf(defaultSettings().sensitivity)))
            cfg_.sensitivity = clampf(360.0f / (cm / 2.54f * cfg_.dpi) / 0.022f, 0.01f, 50.0f);
        static std::string dpiText;   // click and type any DPI
        if (!ui_.typing()) dpiText = std::to_string(cfg_.dpi);
        if (ui_.textField(42, x + cw + gap, y, dw, h, dpiText, 6, UiExp::cfg("dpi").withId("DPI"))) {
            dpiText.erase(std::remove_if(dpiText.begin(), dpiText.end(), [](char ch) { return ch < '0' || ch > '9'; }), dpiText.end());
            int v = atoi(dpiText.c_str());
            if (v >= 50 && v <= 100000) cfg_.dpi = v;
        }
        ui_.label(x + cw + gap + dw - r_.textWidth("DPI", ui_.textScale()) - 6 * s, y + (h - r_.textHeight(ui_.textScale())) * 0.5f, "DPI", {0.6f, 0.62f, 0.66f, 1});
    }
    y += h + gap;
    ui_.slider(x, y, w, h, "FOV", cfg_.fov, 70, 130, "%.0f", UiExp::cfg("fov"), defaultSettings().fov);
    cfg_.fov = clampf(cfg_.fov, 60, 140);   // (typed: quark.cfg's range)
    y += h + gap;
    auto colorRow = [&](const char* label, Vec3& c, const char* key, const Vec3& def) {
        ui_.label(x, y + 4 * s, label);
        r_.rect(x + w - 60 * s, y, 60 * s, 22 * s, rgba(c));
        y += 26 * s;
        float sw = (w - 2 * gap) / 3;
        const char* ch[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            float v = c[i] * 255.0f;
            if (ui_.slider(x + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f", UiExp::cfg(key), std::round(def[i] * 255))) c[i] = clampf(std::round(v), 0, 255) / 255.0f;
        }
        y += h + gap;
    };
    colorRow("TEAM COLOR", cfg_.colorFriendly, "color_friendly", defaultSettings().colorFriendly);
    colorRow("ENEMY COLOR", cfg_.colorEnemy, "color_enemy", defaultSettings().colorEnemy);
    std::vector<std::string> pn;
    int pi = -1;
    for (int i = 0; i < kPaletteCount; i++) {
        pn.push_back(kPalettes[i].name);
        if (length(kPalettes[i].friendly - cfg_.colorFriendly) < 0.01f && length(kPalettes[i].enemy - cfg_.colorEnemy) < 0.01f) pi = i;
    }
    // own colours: CUSTOM is one more value, so ">" goes to the first preset and "<" to the last (">" skipped the first)
    if (pi < 0) { pn.push_back("CUSTOM"); pi = kPaletteCount; }
    if (ui_.cycle(x, y, w, h, "PRESET", pi, (int)pn.size(), pn, UiExp::cfg("color_*")) && pi < kPaletteCount) {
        cfg_.colorFriendly = kPalettes[pi].friendly;
        cfg_.colorEnemy = kPalettes[pi].enemy;
    }
    ui_.accent = rgba(cfg_.colorFriendly);
    y += h + gap;
    {   // player outlines
        static std::vector<std::string> ot, oe;
        if (ot.empty()) for (int i = 0; i < kOutlineColors; i++) { ot.push_back(std::string("TEAM OUTLINE: ") + kOutlineNames[i]); oe.push_back(std::string("ENEMY OUTLINE: ") + kOutlineNames[i]); }
        // full-width rows: at half width the names shrank to an unreadable size
        ui_.cycle(x, y, w, h, "TEAM OUTLINE", cfg_.outlineFriendly, kOutlineColors, ot, UiExp::cfg("outline_friendly"));
        y += h + gap;
        ui_.cycle(x, y, w, h, "ENEMY OUTLINE", cfg_.outlineEnemy, kOutlineColors, oe, UiExp::cfg("outline_enemy"));
        y += h + gap;
        // outline widths (pixels): players' models and the map's edges; 0 = none
        float hw = (w - gap) * 0.5f;
        // (dragged: quarter pixels; typed: as typed, up to 6)
        if (ui_.slider(x, y, hw, h, "PLAYER OUTLINE", cfg_.outlinePlayer, 0, 4, "%.2f", UiExp::cfg("outline_player_width"), defaultSettings().outlinePlayer) && !ui_.typed())
            cfg_.outlinePlayer = std::round(cfg_.outlinePlayer * 4) / 4;
        if (ui_.slider(x + hw + gap, y, hw, h, "WORLD OUTLINE", cfg_.outlineWorld, 0, 4, "%.2f", UiExp::cfg("outline_world_width"), defaultSettings().outlineWorld) && !ui_.typed())
            cfg_.outlineWorld = std::round(cfg_.outlineWorld * 4) / 4;
        cfg_.outlinePlayer = clampf(cfg_.outlinePlayer, 0, 6);
        cfg_.outlineWorld = clampf(cfg_.outlineWorld, 0, 6);
        y += h + gap;
    }
    if (ui_.slider(x, y, w * 0.6f, h, "VOLUME", cfg_.volume, 0, 1, "%.2f", UiExp::cfg("volume"), defaultSettings().volume)) audio_.setMasterVolume(cfg_.volume = clampf(cfg_.volume, 0, 1));
    ui_.toggle(x + w * 0.6f + gap, y, w * 0.4f - gap, h, "HITSOUND", cfg_.hitsounds, UiExp::cfg("hitsounds"));
    y += h + gap;
    {
        static const std::vector<std::string> an = {"ANNOUNCER: OFF", "ANNOUNCER: COUNTDOWN", "ANNOUNCER: ALL CALLS"};
        static std::vector<std::string> av;
        if (av.empty()) for (int v = 0; v < kAnnouncerVoices; v++) av.push_back(std::string("VOICE: ") + announcerVoiceName(v));
        // (a row each: VOICE beside it was cut off at 720p - UI test)
        if (ui_.cycle(x, y, w, h, "ANNOUNCER", cfg_.announcer, 3, an, UiExp::cfg("announcer")) && cfg_.announcer > 0) audio_.play(SND_VOICE_GO, 1.0f);
        y += h + gap;
        if (ui_.cycle(x, y, w, h, "VOICE", cfg_.announcerVoice, kAnnouncerVoices, av, UiExp::cfg("voice"))) { audio_.setAnnouncer(cfg_.announcerVoice); audio_.play(SND_VOICE_GODLIKE, 1.0f); }
        y += h + gap;
    }
    {   // cosmetics: player model (everyone sees it) and sound pack
        static std::vector<std::string> mn;
        if (mn.empty()) for (int i = 0; i < kPlayerModels; i++) mn.push_back(std::string("PLAYER MODEL: ") + playerModelName(i));
        static const std::vector<std::string> sn = {"SOUND PACK: DEFAULT", "SOUND PACK: HEAVY METAL", "SOUND PACK: MAGICAL", "SOUND PACK: RETRO", "SOUND PACK: MOUTH"};
        ui_.cycle(x, y, w, h, "MODEL", cfg_.model, kPlayerModels, mn, UiExp::cfg("model"));
        y += h + gap;
        ui_.cycle(x, y, w, h, "SOUNDS", cfg_.soundPack, 5, sn, UiExp::cfg("soundpack"));
        y += h + gap;
    }

    // --- right column: display + latency
    y = y0;
    static const std::vector<std::string> dispNames = {"WINDOWED", "BORDERLESS FULLSCREEN", "EXCLUSIVE FULLSCREEN"};
    if (ui_.cycle(xr, y, w, h, "DISPLAY", cfg_.displayMode, 3, dispNames, UiExp::cfg("display_mode"))) applyDisplayMode();
    y += h + gap;
#ifdef _WIN32
    static const std::vector<std::string> presNames = {"NO VSYNC", "VSYNC", "ADAPTIVE VSYNC", "LATENT SYNC"};
    const int nPres = 4;
#else
    static const std::vector<std::string> presNames = {"NO VSYNC", "VSYNC", "ADAPTIVE VSYNC"};
    const int nPres = 3;
#endif
    if (ui_.cycle(xr, y, w, h, "PRESENT", cfg_.presentMode, nPres, presNames, UiExp::cfg("present_mode"))) applyPresentMode();
    y += h + gap;
    if (cfg_.presentMode == 3) {   // latent sync: where the tear line goes (0: the top of the blank)
        float v = (float)cfg_.latentOffsetUs;
        ui_.slider(xr, y, w, h, "TEAR LINE OFFSET (US)", v, -2000, 2000, "%.0f", UiExp::cfg("latent_offset_us"), 0);
        cfg_.latentOffsetUs = (int)std::lround(clampf(v, -4000, 4000));
        y += h + gap;
    }
    static const std::vector<std::string> llNames = {"LOW LATENCY OFF", "LOW LATENCY ON", "LOW LATENCY ULTRA"};
    ui_.cycle(xr, y, w, h, "LOW LATENCY", cfg_.lowLatency, 3, llNames, UiExp::cfg("low_latency"));
    y += h + gap;
#ifdef _WIN32
    // NVIDIA: "Prefer maximum performance" in quark's driver profile (the driver reads it at the next start)
    // (beside it, the latency half of SOUNDS, which ran off the left column's bottom: the sound device to the game
    // alone, about 10 ms sooner, and Discord / music go silent while the game runs)
    const float hw2 = (w - gap) * 0.5f;
    if (ui_.toggle(xr, y, hw2, h, "GPU MAX CLOCKS", cfg_.gpuMaxClocks, UiExp::cfg("gpu_max_clocks"))) {
        std::string msg = "GPU MAX CLOCKS: NOT CHANGED IN THE UI TEST";
        if (!uiTest_ && !nvMaxClocks(cfg_.gpuMaxClocks, &msg)) cfg_.gpuMaxClocks = false;
        notify(msg);
    }
    if (ui_.toggle(xr + hw2 + gap, y, hw2, h, !cfg_.exclusiveAudio || audio_.exclusiveActive() ? "EXCLUSIVE AUDIO" : audio_.exclusiveState() == Audio::kExclUnsupported ? "EXCLUSIVE AUDIO (N/A)" : "EXCLUSIVE AUDIO (BUSY)",   // (N/A: the device takes none of its formats)
                   cfg_.exclusiveAudio, UiExp::cfg("exclusive_audio")))
        audio_.setExclusive(cfg_.exclusiveAudio);
    y += h + gap;
#endif
    {   // any cap, typed or dragged (0: unlimited); right-click: the screen's refresh rate (G-Sync / FreeSync: a few under)
        gameMonitor();
        float v = (float)cfg_.maxFps;
        ui_.slider(xr, y, w, h, "MAX FPS", v, 0, 1000, cfg_.maxFps <= 0 ? "UNLIMITED" : "%.0f", UiExp::cfg("maxfps"),
                   (float)(vmHz_ > 0 ? vmHz_ : 60));
        cfg_.maxFps = std::min(std::max(0, (int)std::lround(v)), 100000);
    }
    y += h + gap;
    // (a row each: "PLAYER SHADOWS: ON" in half a row was cut off at 720p - UI test)
    ui_.toggle(xr, y, w, h, "PLAYER SHADOWS", cfg_.shadows, UiExp::cfg("shadows"));
    y += h + gap;
    ui_.slider(xr, y, w, h, "BRIGHTNESS", cfg_.brightness, 0.6f, 1.8f, "%.2f", UiExp::cfg("brightness"), defaultSettings().brightness);
    cfg_.brightness = clampf(cfg_.brightness, 0.6f, 1.8f);   // (typed: quark.cfg's range)
    y += h + gap;
    static const int aaModes[] = {0, 1, 2, 4};
    static const std::vector<std::string> aaNames = {"ANTI-ALIASING OFF", "SMOOTH EDGES (CHEAP)", "MSAA 2X", "MSAA 4X"};
    int ai = 0;
    for (int i = 0; i < 4; i++) if (aaModes[i] == cfg_.msaa) ai = i;
    if (ui_.cycle(xr, y, w, h, "ANTI-ALIASING", ai, 4, aaNames, UiExp::cfg("msaa"))) cfg_.msaa = aaModes[ai];
    y += h + gap;
    {   // other players' interpolation delay (ranked: always 10 ms)
        // default (adaptive), disabled (no delay: newest snapshot), or a custom delay set on a slider
        static const std::vector<std::string> imn = {"INTERP: DEFAULT (ADAPTIVE)", "INTERP: CUSTOM", "INTERP: DISABLED"};
        int ii = cfg_.interpMs == 0 ? 0 : cfg_.interpMs < 0 ? 2 : 1;
        constexpr int kCustomInterpMs = 10;   // (CUSTOM starts here; right-click on its slider: back to it)
        if (ui_.cycle(xr, y, w, h, "INTERP", ii, 3, imn, UiExp::cfg("interp_ms"))) cfg_.interpMs = ii == 0 ? 0 : ii == 2 ? -1 : kCustomInterpMs;
        if (cfg_.interpMs > 0) {
            y += h + gap;
            float v = (float)cfg_.interpMs;
            ui_.slider(xr, y, w, h, "INTERP DELAY (MS)", v, 1, (float)kMaxInterpMs, "%.0f", UiExp::cfg("interp_ms"), (float)kCustomInterpMs);
            cfg_.interpMs = std::min(std::max(1, (int)std::lround(v)), kMaxInterpMs);
        }
        y += h + gap;
    }
    {
        // your name's colour as everyone sees it (scoreboard, chat, kill feed): DEFAULT, or R / G / B (moving one picks it)
        bool custom = cfg_.nameColor >= 0;
        float cwid = w * 0.46f, sw = (w - cwid - 3 * gap) / 3;
        constexpr int kNameColorOn = 0xFFD040;   // (turned on, and where a right-click on R / G / B goes back to)
        if (ui_.toggle(xr, y, cwid, h, "NAME COLOR", custom, UiExp::cfg("name_color"))) cfg_.nameColor = custom ? kNameColorOn : -1;
        int rgb = cfg_.nameColor >= 0 ? cfg_.nameColor : 0xFFFFFF;
        const char* ch[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            int sh = 16 - 8 * i;
            float v = (float)((rgb >> sh) & 255);
            if (ui_.slider(xr + cwid + gap + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f", UiExp::cfg("name_color"), (float)((kNameColorOn >> sh) & 255))) {
                rgb = (rgb & ~(255 << sh)) | ((int)std::lround(clampf(v, 0, 255)) << sh);
                cfg_.nameColor = rgb;
            }
        }
        if (cfg_.nameColor >= 0)   // (a swatch of the colour as others see it)
            r_.rect(xr + 4 * s, y + 4 * s, 6 * s, h - 8 * s,
                    rgba(Vec3(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f)));
        y += h + gap;
    }
    const char* help[] = {
        "NO VSYNC = LOWEST LATENCY, MAY TEAR",
        "EXCLUSIVE / BORDERLESS SKIP THE DESKTOP",
        "COMPOSITOR ON MOST DRIVERS",
        "LOW LATENCY ON: <= 1 FRAME QUEUED",
        "ULTRA: 0 QUEUED + LATE INPUT SAMPLING",
        "F3 SHOWS INPUT->FRAME LATENCY",
    };
    for (const char* t : help) { ui_.label(xr, y, t, {0.7f, 0.72f, 0.75f, 1}, 0.5f); y += 14 * s; }
    y += gap;
    if (ui_.button(xr, y, w * 0.5f - gap * 0.5f, h, "CONTROLS", UiExp::nav("~settings/controls"))) controlsOpen_ = true;
    if (ui_.button(xr + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "CROSSHAIR", UiExp::nav("~settings/crosshair"))) crosshairOpen_ = true;
    y += h + gap;
    if (ui_.button(xr, y, w * 0.5f - gap * 0.5f, h, "HUD LAYOUT", UiExp::nav("~settings/hud"))) hudOpen_ = true;
    if (ui_.button(xr + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "VISUALS", UiExp::nav("~settings/visuals"))) visualsOpen_ = true;
    y += h + gap;
    if (ui_.button(xr, y, w, h, "BACK", UiExp::nav(settingsInGame_ ? "ingame/pause" : "main"))) {
        cfg_.save();
        linkDiscord();
        if (net_.state() == ConnState::Connected) net_.sendCommand("name " + cfg_.name);
        if (settingsInGame_) settingsInGame_ = false;
        else setScreen(Screen::Main);
    }
}

void Game::drawNotices(float y, float ts, float s) {
    while (!notices_.empty() && notices_.front().until < time_) notices_.pop_front();
    for (auto& n : notices_) { ui_.labelCentered(r_.width() * 0.5f, y, n.text, n.color); y += r_.textHeight(ts) + 6 * s; }
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
    float bw = 340 * s, bh = 40 * s, gap = 10 * s;
    float x = cx - bw * 0.5f;
    float y = H * 0.12f;
    ui_.labelCentered(cx, y, "QUARK", rgba(cfg_.colorFriendly), 4.0f);
    r_.text(8 * s, H - 20 * s, std::max(1.0f, std::round(s)), std::string("V") + QUARK_VERSION, kGrey);
    y += 80 * s;

    if (picker_ != PICK_NONE && (screen_ == Screen::Host || screen_ == Screen::VsBots)) { drawPicker(cx, y); return; }
    if (edPrompt_ && screen_ == Screen::Main) { drawEditorSavePrompt(); return; }
    switch (screen_) {
        case Screen::Main: {
            ui_.label(x, y, "YOUR NAME (SHOWN IN CHAT AND SCOREBOARD)", kGrey, 0.5f);
            y += 16 * s;
            if (ui_.textField(9, x, y, bw, bh, cfg_.name, 20, UiExp::cfg("name").withId("NAME"))) cfg_.save();
            y += bh + gap * 2;
            if (ui_.button(x, y, bw * 0.5f - gap * 0.5f, bh, "PLAY VS BOTS", UiExp::nav("vsbots"))) setScreen(Screen::VsBots);
            if (ui_.button(x + bw * 0.5f + gap * 0.5f, y, bw * 0.5f - gap * 0.5f, bh, "TIME TRIAL", UiExp::state("hosting", "1"))) startTimeTrial();
            y += bh + gap;
            if (ui_.button(x, y, bw, bh, "JOIN GAME", UiExp::nav("join"))) setScreen(Screen::Join);
            y += bh + gap;
            if (ui_.button(x, y, bw, bh, "HOST GAME", UiExp::nav("host"))) setScreen(Screen::Host);
            y += bh + gap;
            if (ui_.button(x, y, bw * 0.5f - gap * 0.5f, bh, "MAP EDITOR", UiExp::nav("editor"))) openEditor("");
            if (ui_.button(x + bw * 0.5f + gap * 0.5f, y, bw * 0.5f - gap * 0.5f, bh, "LEADERBOARD", UiExp::nav("leaderboard"))) { nextBoardFetch_ = 0; raceStatsView_ = false; setScreen(Screen::Leaderboard); }
            y += bh + gap;
            if (ui_.button(x, y, bw * 0.5f - gap * 0.5f, bh, "SETTINGS", UiExp::nav("settings"))) setScreen(Screen::Settings);
            if (ui_.button(x + bw * 0.5f + gap * 0.5f, y, bw * 0.5f - gap * 0.5f, bh, "INFO", UiExp::nav("info/*"))) setScreen(Screen::Info);   // how everything works
            y += bh + gap;
            {
                float tw = (bw - 2 * gap) / 3;
                if (ui_.button(x, y, tw, bh, "DISCORD", UiExp::url())) openUrl(kDiscordUrl);
                if (ui_.button(x + tw + gap, y, tw, bh, "DEMOS", UiExp::nav("demos"))) setScreen(Screen::Demos);
                if (ui_.button(x + 2 * (tw + gap), y, tw, bh, "QUIT", UiExp::quit())) quit_ = true;
            }
            y += bh + gap * 2;
            if (!error_.empty()) ui_.labelCentered(cx, y, error_, kRed);
            break;
        }
        case Screen::Join: {
            float lw = 560 * s, lx = cx - lw * 0.5f;
            ui_.label(lx, y, "ADDRESS (IP:PORT)");
            y += 22 * s;
            ui_.textField(2, lx, y, lw - 140 * s, bh, joinAddr_, 64, UiExp::local().withId("ADDRESS"));
            if (ui_.button(lx + lw - 130 * s, y, 130 * s, bh, "CONNECT", UiExp::nav("connecting")) || (uiIn_.enter && !joinAddr_.empty())) connectTo(joinAddr_);
            y += bh + gap * 2;
            ui_.label(lx, y, "SERVERS (LAN + INTERNET)");
            ui_.label(lx, y + 22 * s, browser_.internetStatus(), kGrey, 0.5f);
            y += 12 * s;
            if (ui_.button(lx + lw - 130 * s, y - 6 * s, 130 * s, 30 * s, "REFRESH", UiExp::state("browser"))) browser_.refresh();
            y += 30 * s;
            ui_.panel(lx, y, lw, 220 * s, 0.6f);
            float ry = y + 8 * s;
            const int kJoinRows = 6;   // rows that fit the list panel; the wheel scrolls the rest
            int nsv = (int)browser_.servers().size();
            joinScroll_ = std::min(joinScroll_, std::max(0, nsv - kJoinRows));
            for (int si = joinScroll_; si < nsv && si < joinScroll_ + kJoinRows; si++) {
                auto& sv = browser_.servers()[si];
                // name on the left (a long one scrolls back and forth so all of it can be read), details on the right
                char info[96];
                if (sv.noReply) snprintf(info, sizeof info, "NO RESPONSE - CLICK TO TRY");
                else snprintf(info, sizeof info, "%s/%s  %d/%d  %dms", sv.map.c_str(), modeName(sv.mode), sv.players, sv.maxPlayers, (int)sv.ping);
                std::string infoU = upper(info);
                float bx = lx + 6 * s, bwRow = lw - 12 * s, bhRow = 30 * s, ts = ui_.textScale();
                if (ui_.button(bx, ry, bwRow, bhRow, "", UiExp::nav("connecting").withId("SERVER").optional())) {
                    joinAddr_ = sv.addr.toString();
                    connectTo(joinAddr_);
                }
                float ty = ry + (bhRow - r_.textHeight(ts)) * 0.5f, iw = r_.textWidth(infoU, ts);
                r_.text(bx + bwRow - iw - 8 * s, ty, ts, infoU, kWhite);
                std::string name = upper(sv.name);
                float avail = std::max(0.0f, bwRow - iw - 28 * s);
                int extra = 0;   // leading characters to drop before the rest fits (measured: W is wider than M)
                while (extra < (int)name.size() && r_.textWidth(name.substr(extra), ts) > avail) extra++;
                if (extra > 0) { // ping-pong: pause 1.5 s at each end, scroll 6 characters a second
                    double cycle = 2 * (extra / 6.0 + 1.5), t = std::fmod(time_, cycle);
                    double pos = t < 1.5 ? 0 : t < 1.5 + extra / 6.0 ? (t - 1.5) * 6 : t < 3 + extra / 6.0 ? extra : extra - (t - 3 - extra / 6.0) * 6;
                    name = name.substr((size_t)std::clamp((int)pos, 0, extra));
                    name.resize(r_.textFit(name, ts, avail));
                }
                r_.text(bx + 8 * s, ty, ts, name, kWhite);
                ry += 34 * s;
            }
            if (browser_.servers().empty()) ui_.label(lx + 10 * s, ry, "NO GAMES FOUND", kGrey);
            else if (nsv > kJoinRows) {
                std::string more = std::to_string(joinScroll_ + 1) + "-" + std::to_string(std::min(nsv, joinScroll_ + kJoinRows)) + " OF " + std::to_string(nsv) + " - WHEEL TO SCROLL";
                ui_.label(lx + lw - r_.textWidth(more, std::max(1.0f, std::round(ui_.textScale() * 0.5f))), y + 222 * s, more, kGrey, 0.5f);
            }
            y += 230 * s;
            if (ui_.button(x, y, bw, bh, "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
            if (!error_.empty()) ui_.labelCentered(cx, y + bh + gap, error_, kRed);
            break;
        }
        case Screen::Host: {
            float lw = 460 * s, lx = cx - lw * 0.5f;
            ui_.panel(lx - 16 * s, y - 16 * s, lw + 32 * s, 528 * s + bh + gap);
            ui_.label(lx, y, "SERVER NAME");
            y += 22 * s;
            ui_.textField(3, lx, y, lw, bh, cfg_.hostName, 32, UiExp::cfg("host_name").withId("SERVER NAME"));
            y += bh + gap;
            if (!maps_.empty()) {
                if (ui_.button(lx, y, lw, bh, "MAP: " + upper(maps_[hostMapIdx_ % maps_.size()]) + "  >", UiExp::nav("host/picker:maps").withId("MAP"))) picker_ = PICK_HOST_MAP;
                y += bh + gap;
                {   // custom mode: click to go through your saved ones (or none); EDIT / NEW open the builder
                    float ew = 74 * s, cw = lw - 2 * (ew + 6 * s);
                    // (re-listed twice a second at most: not a folder scan every frame; none saved yet: nothing to cycle through)
                    static std::vector<std::string> saved;
                    static double savedAt = -1;
                    if (time_ >= savedAt || time_ < savedAt - 1) { saved = listCustomModes(); savedAt = time_ + 0.5; }
                    if (cfg_.hostRanked) ui_.button(lx, y, cw, bh, "CUSTOM MODE: OFF (RANKED)", UiExp::inert().withId("CUSTOM MODE"), false);
                    else if (ui_.button(lx, y, cw, bh, "CUSTOM MODE: " + (cfg_.hostCustom.empty() ? std::string("NONE") : upper(cfg_.hostCustom)) + "  >",
                                        UiExp::cfg("host_custom").withId("CUSTOM MODE"), !saved.empty() || !cfg_.hostCustom.empty())) {
                        auto list = listCustomModes();
                        auto it = std::find(list.begin(), list.end(), cfg_.hostCustom);
                        cfg_.hostCustom = list.empty() ? "" : it == list.end() ? list[0] : it + 1 == list.end() ? "" : *(it + 1);
                    }
                    if (ui_.button(lx + cw + 6 * s, y, ew, bh, "EDIT", UiExp::nav("custom/general"), !cfg_.hostCustom.empty())) {
                        editRules_ = CustomRules();
                        if (loadCustomMode(cfg_.hostCustom, editRules_)) { editOrigName_ = editRules_.name; customTab_ = 0; setScreen(Screen::CustomMode); }
                    }
                    if (ui_.button(lx + cw + ew + 12 * s, y, ew, bh, "NEW", UiExp::nav("custom/general"))) {
                        editRules_ = CustomRules(); editRules_.name = "my mode"; editRules_.baseMode = MODE_FFA;
                        editOrigName_.clear(); customTab_ = 0; setScreen(Screen::CustomMode);
                    }
                    // the chosen custom mode, re-read twice a second at most (not a file read every frame)
                    static CustomRules cur;
                    static std::string curName;
                    static double curAt = -1;
                    static bool curOk = false;
                    if (curName != cfg_.hostCustom || time_ >= curAt) {
                        curName = cfg_.hostCustom; curAt = time_ + 0.5;
                        cur = CustomRules();
                        curOk = !curName.empty() && loadCustomMode(curName, cur);
                    }
                    if (!cfg_.hostRanked && !cfg_.hostCustom.empty() && curOk) {
                        hostModeIdx_ = cur.baseMode;   // the mode comes from the custom mode
                        if (!cur.maps.empty() && std::find(cur.maps.begin(), cur.maps.end(), maps_[hostMapIdx_ % maps_.size()]) == cur.maps.end())
                            for (size_t k = 0; k < maps_.size(); k++) if (maps_[k] == cur.maps[0]) hostMapIdx_ = (int)k;
                    }
                    y += bh + gap;
                }
                if (cfg_.hostRanked) { // ranked is always 1v1 on a ladder: the mode button picks the ladder instead
                    if (!arenaVariantRanked(cfg_.hostArenaVariant)) cfg_.hostArenaVariant = 0;
                    hostModeIdx_ = rankedMode(cfg_.hostArenaVariant);
                    if (hostMapIdx_ >= (int)mapInfo_.size() || !(mapInfo_[hostMapIdx_].ranked >> cfg_.hostArenaVariant & 1))
                        hostMapIdx_ = bestRankedMap(cfg_.hostArenaVariant);
                }
                bool built = hostMapIdx_ < (int)mapInfo_.size() && (mapInfo_[hostMapIdx_].modes >> hostModeIdx_ & 1); // (no map file read every frame)
                if (cfg_.hostRanked) {
                    static const int ladders[3] = {0, 1, 5};
                    int k = cfg_.hostArenaVariant == 1 ? 1 : cfg_.hostArenaVariant == 5 ? 2 : 0;
                    if (ui_.button(lx, y, lw, bh, std::string("MODE: ") + rankedKind(ladders[k]) + " - " + arenaVariant(ladders[k]).title + "  >", UiExp::nav("host/picker:modes").withId("MODE"))) picker_ = PICK_HOST_MODE;
                } else if (ui_.button(lx, y, lw, bh, std::string("MODE: ") + modeTitle(hostModeIdx_) + (hostModeIdx_ == MODE_ARENA || hostModeIdx_ == MODE_TOURNEY ? std::string(" - ") + arenaVariant(cfg_.hostArenaVariant).title : "") + "  >",
                                      UiExp::nav("host/picker:modes").withId("MODE"))) picker_ = PICK_HOST_MODE;
                y += bh + gap;
                if (cfg_.hostRanked) {
                    if (testingBuild())   // (a testing build hosts ranked unrated: Server::rated)
                        ui_.label(lx, y, "TESTING BUILD - RANKED NOT RATED (PLAYED, NEVER COUNTED)", {1.0f, 0.78f, 0.25f, 1}, 0.5f);
                    else ui_.label(lx, y, "RANKED MAPS ONLY - EACH LADDER HAS ITS OWN RATING", kGrey, 0.5f);
                    ui_.label(lx, y + 12 * s, "FAIRNESS: DEFAULT MODEL, HUD, CONTROLS, 10 MS INTERP", kGrey, 0.5f);
                }
                else if (!built) ui_.label(lx, y, "NOT BUILT FOR THIS MODE - IT MAY NOT WORK", kRed);
                else if (hostModeIdx_ == MODE_DUEL && ui_.button(lx, y - 2 * s, lw, 24 * s, cfg_.hostDuelSize == 2 ? "TEAMS: 2V2 (CLICK FOR 1V1)" : "TEAMS: 1V1 (CLICK FOR 2V2)", UiExp::cfg("host_duel_size").withId("TEAMS")))
                    cfg_.hostDuelSize = cfg_.hostDuelSize == 2 ? 1 : 2;
                y += 24 * s;
            }
            ui_.label(lx, y, "UDP PORT");
            y += 22 * s;
            ui_.textField(4, lx, y, lw * 0.4f, bh, hostPortStr_, 5, UiExp::local().withId("UDP PORT"));
            float mp = (float)cfg_.hostMaxPlayers;
            if (ui_.slider(lx + lw * 0.45f, y, lw * 0.55f, bh, "MAX PLAYERS", mp, 2, (float)kMaxPlayers, "%.0f", UiExp::cfg("host_max"), (float)defaultSettings().hostMaxPlayers))
                cfg_.hostMaxPlayers = std::min(std::max((int)std::round(mp), 2), kMaxPlayers);
            y += bh + gap;
            ui_.toggle(lx, y, lw, bh, "AUTO PORT FORWARD (UPNP)", cfg_.hostUpnp, UiExp::cfg("host_upnp"));
            y += bh + gap;
            if (cfg_.hostRanked) ui_.button(lx, y, lw * 0.5f - 4 * s, bh, "BOTS: OFF (RANKED)", UiExp::inert().withId("BALANCE BOTS"), false);
            else ui_.toggle(lx, y, lw * 0.5f - 4 * s, bh, "BALANCE BOTS", cfg_.hostBalanceBots, UiExp::cfg("host_balance_bots"));
            ui_.toggle(lx + lw * 0.5f + 4 * s, y, lw * 0.5f - 4 * s, bh, "PUBLIC", cfg_.hostPublic, UiExp::cfg("host_public"));
            y += bh + gap;
            static const std::vector<std::string> logNames = {"SERVER LOG OFF", "SIMPLE LOG", "VERBOSE LOG (125 HZ)", "EXTREME LOG (EVERY TICK)"};
            ui_.cycle(lx, y, lw, bh, "SERVER LOG", cfg_.hostLog, 4, logNames, UiExp::cfg("host_log"));
            y += bh + gap;
            ui_.label(lx, y, "FRIENDS JOIN WITH YOUR PUBLIC IP:" + hostPortStr_, kGrey, 0.5f);
            y += 14 * s;
            ui_.label(lx, y, "IN GAME, ESC SHOWS IF THE PORT IS REACHABLE (GREEN) OR NOT (RED)", kGrey, 0.5f);
            y += 26 * s;
            if (ui_.button(lx, y, lw * 0.5f - 5 * s, bh, "START", UiExp::state("hosting", "1"))) startHost();
            if (ui_.button(lx + lw * 0.5f + 5 * s, y, lw * 0.5f - 5 * s, bh, "BACK", UiExp::nav("main"))) { cfg_.save(); setScreen(Screen::Main); }
            y += bh + gap;
            if (!error_.empty()) ui_.labelCentered(cx, y + 20 * s, error_, kRed);
            break;
        }
        case Screen::Settings:
            drawSettings(cx - 230 * s, y, 460 * s);
            break;
        case Screen::VsBots: {
            float lw = 460 * s, lx = cx - lw * 0.5f;
            ui_.panel(lx - 16 * s, y - 16 * s, lw + 32 * s, 360 * s);
            if (ui_.button(lx, y, lw, bh, std::string("MODE: ") + modeTitle(cfg_.botMode) + (cfg_.botMode == MODE_ARENA ? std::string(" - ") + arenaVariant(cfg_.botArenaVariant).title : "") + "  >",
                           UiExp::nav("vsbots/picker:modes").withId("MODE"))) picker_ = PICK_BOT_MODE;
            y += bh + gap;
            if (!maps_.empty()) {
                // only offer maps that support the chosen mode
                for (size_t k = 0; k < maps_.size(); k++) {   // (from the scan, not a map file read every frame)
                    size_t i = botMapIdx_ % maps_.size();
                    if (i < mapInfo_.size() && (mapInfo_[i].modes >> cfg_.botMode & 1)) break;
                    botMapIdx_ = (botMapIdx_ + 1) % (int)maps_.size();
                }
                if (ui_.button(lx, y, lw, bh, "MAP: " + upper(maps_[botMapIdx_ % maps_.size()]) + "  >", UiExp::nav("vsbots/picker:maps").withId("MAP"))) picker_ = PICK_BOT_MAP;
                y += bh + gap;
            }
            static const std::vector<std::string> skills = {"BEGINNER BOTS", "INTERMEDIATE BOTS", "ADVANCED BOTS", "EXPERT BOTS"};
            ui_.cycle(lx, y, lw, bh, "DIFFICULTY", cfg_.botSkill, 4, skills, UiExp::cfg("bot_skill"));
            y += bh + gap;
            bool teams = modeIsTeamBased(cfg_.botMode);
            float ne = (float)cfg_.botEnemies, na = (float)cfg_.botAllies;
            if (ui_.slider(lx, y, lw, bh, teams ? "ENEMY BOTS" : "BOTS", ne, 1, 7, "%.0f", UiExp::cfg("bot_enemies"), (float)defaultSettings().botEnemies)) cfg_.botEnemies = std::min(std::max((int)std::round(ne), 1), 7);
            y += bh + gap;
            if (teams && ui_.slider(lx, y, lw, bh, "TEAMMATE BOTS", na, 0, 3, "%.0f", UiExp::cfg("bot_allies"), (float)defaultSettings().botAllies)) cfg_.botAllies = std::min(std::max((int)std::round(na), 0), 3);
            y += bh + gap * 2;
            if (ui_.button(lx, y, lw * 0.5f - 5 * s, bh, "START", UiExp::state("hosting", "1"))) startHost(true);
            if (ui_.button(lx + lw * 0.5f + 5 * s, y, lw * 0.5f - 5 * s, bh, "BACK", UiExp::nav("main"))) { cfg_.save(); setScreen(Screen::Main); }
            if (!error_.empty()) ui_.labelCentered(cx, y + bh + gap + 10 * s, error_, kRed);
            break;
        }
        case Screen::Leaderboard: drawLeaderboard(cx, y); break;
        case Screen::Info: drawInfo(cx, y - 8 * s); break;
        case Screen::Demos: drawDemos(cx, y); break;
        case Screen::CustomMode: drawCustomModeEditor(cx, y); break;
        case Screen::Connecting: {
            float dlp = net_.downloadProgress();
            if (dlp >= 0) ui_.labelCentered(cx, y, "DOWNLOADING MAP " + upper(net_.downloadName()) + " - " + std::to_string((int)(dlp * 100)) + "%", kWhite);
            else ui_.labelCentered(cx, y, "CONNECTING TO " + net_.address() + "...", kWhite);
            y += 60 * s;
            if (ui_.button(x, y, bw, bh, "CANCEL", UiExp::nav("main"))) leaveGame();
            break;
        }
        default: break;
    }
}

// "NAME 2.2 -> 4.4 M/S": a custom mode's setting as the standard value and what this mode makes it
static std::string stdTo(const char* name, float base, float mul, const char* unit = "") {
    auto num = [](float v) { char b[32]; snprintf(b, sizeof b, v >= 100 ? "%.0f" : v >= 10 ? "%.1f" : "%.2f", v); return std::string(b); };
    std::string u = *unit ? std::string(" ") + upper(unit) : "";
    return std::string(name) + " " + num(base) + (std::fabs(mul - 1) > 1e-3f ? " -> " + num(base * mul) : "") + u;
}

void Game::drawCustomModeEditor(float cx, float y) {
    float s = ui_.s(), w = 640 * s, x = cx - w * 0.5f, bh = 30 * s, gap = 6 * s, hw = (w - gap) * 0.5f;
    // WEAPONS and MOVEMENT are grids of "NAME standard -> this mode: x" sliders: they get the screen's width (in 640 the
    // labels were cut off, the value with them, at 720p - UI test)
    const float gw = customTab_ == 1 || customTab_ == 3 ? std::max(w, std::min((float)r_.width() - 64 * s, 1100 * s)) : w, gx = cx - gw * 0.5f;
    CustomRules& r = editRules_;
    ui_.panel(gx - 16 * s, y - 16 * s, gw + 32 * s, std::min(640 * s, r_.height() - (y - 16 * s) - 8 * s), 0.85f);
    ui_.labelCentered(cx, y, "CUSTOM MODE");
    y += 34 * s;
    ui_.label(x, y + 7 * s, "NAME");
    ui_.textField(901, x + 90 * s, y, w - 90 * s, bh, r.name, 24, UiExp::state("custom").withId("NAME"));
    y += bh + gap * 2;
    static const char* tabs[4] = {"GENERAL", "WEAPONS", "MAPS", "MOVEMENT"};
    static const char* tabPaths[4] = {"custom/general", "custom/weapons", "custom/maps", "custom/movement"};
    float tw = (w - 3 * gap) / 4;
    for (int t = 0; t < 4; t++)
        if (ui_.button(x + t * (tw + gap), y, tw, bh, customTab_ == t ? std::string("> ") + tabs[t] + " <" : tabs[t], UiExp::nav(tabPaths[t]).withId(tabs[t]))) customTab_ = t;
    y += bh + gap * 2;
    if (customTab_ == 0) {
        static std::vector<std::string> modes;   // (built once: 13 strings every frame)
        if (modes.empty()) for (int m = 0; m < MODE_COUNT; m++) modes.push_back(std::string("PLAYS AS: ") + upper(modeName(m)));
        const int before = r.baseMode;
        ui_.cycle(x, y, w, bh, "PLAYS AS", r.baseMode, MODE_COUNT, modes, UiExp::state("custom"));
        // tourney and ranked arena keep their own rules: stepped over, onwards in the direction it went (going ">" it
        // was put back to FFA, so TOURNEY's neighbours on, hide and seek, tag and points, couldn't be reached that way)
        const int step = r.baseMode == (before + 1) % MODE_COUNT ? 1 : MODE_COUNT - 1;
        for (int k = 0; k < MODE_COUNT && (r.baseMode == MODE_TOURNEY || r.baseMode == MODE_ARENA); k++) r.baseMode = (r.baseMode + step) % MODE_COUNT;
        y += bh + gap;
        float hp = (float)r.health;
        const UiExp ce = UiExp::state("custom");
        // (a typed value is kept as typed, within the custom mode file's ranges - CustomRules::fromText)
        static const CustomRules kStd;   // (right-click on a slider: the standard value)
        ui_.slider(x, y, hw, bh, "HEALTH", hp, 1, 500, "%.0f", ce, (float)kStd.health); r.health = std::min(std::max((int)std::lround(hp), 1), 1000);
        ui_.slider(x + hw + gap, y, hw, bh, stdTo("GRAVITY", moveBaseGravity(), r.gravity, "m/s2"), r.gravity, 0.1f, 3, "%.2fx", ce.withId("GRAVITY"), kStd.gravity); y += bh + gap;
        ui_.slider(x, y, hw, bh, stdTo("RUN SPEED", moveBaseSpeed(), r.moveSpeed, "m/s"), r.moveSpeed, 0.25f, 3, "%.2fx", ce.withId("RUN SPEED"), kStd.moveSpeed);
        ui_.slider(x + hw + gap, y, hw, bh, stdTo("JUMP", moveBaseJump(), r.jump, "m/s"), r.jump, 0.25f, 3, "%.2fx", ce.withId("JUMP"), kStd.jump); y += bh + gap;
        ui_.slider(x, y, hw, bh, "KNOCKBACK", r.knockback, 0, 5, "%.2fx", ce, kStd.knockback); y += bh + gap;
        r.gravity = clampf(r.gravity, 0.1f, 3); r.moveSpeed = clampf(r.moveSpeed, 0.25f, 3); r.jump = clampf(r.jump, 0.25f, 3); r.knockback = clampf(r.knockback, 0, 5);
        ui_.toggle(x, y, hw, bh, "DASH", r.dash, ce);
        ui_.toggle(x + hw + gap, y, hw, bh, "GRAPPLE", r.grapple, ce); y += bh + gap;
        ui_.toggle(x, y, hw, bh, "SELF DAMAGE", r.selfDamage, ce);
        ui_.toggle(x + hw + gap, y, hw, bh, "INSTAGIB", r.instagib, ce); y += bh + gap;
        ui_.toggle(x, y, hw, bh, "VAMPIRE", r.vampire, ce);
        ui_.toggle(x + hw + gap, y, hw, bh, "REGEN", r.regen, ce); y += bh + gap;
        float tl = (float)r.timeLimit, sl = (float)r.scoreLimit, rw = (float)r.roundsToWin;
        // (dragged: half minutes; typed: any whole second)
        if (ui_.slider(x, y, hw, bh, "TIME (S, 0=DEFAULT)", tl, 0, 3600, "%.0f", ce, (float)kStd.timeLimit)) r.timeLimit = std::min(std::max((int)std::lround(ui_.typed() ? tl : std::round(tl / 30) * 30), 0), 3600);
        ui_.slider(x + hw + gap, y, hw, bh, "SCORE (0=DEFAULT)", sl, 0, 200, "%.0f", ce, (float)kStd.scoreLimit); r.scoreLimit = std::min(std::max((int)std::lround(sl), 0), 10000); y += bh + gap;
        ui_.slider(x, y, hw, bh, "ROUNDS (0=DEFAULT)", rw, 0, 15, "%.0f", ce, (float)kStd.roundsToWin); r.roundsToWin = std::min(std::max((int)std::lround(rw), 0), 50); y += bh + gap;
    } else if (customTab_ == 3) {   // every movement constant, x its normal value (gravity / speed / jump are on GENERAL)
        ui_.label(x, y, "EVERY MOVEMENT SETTING: STANDARD -> THIS MODE (THE SLIDER IS THE MULTIPLIER, 1.00X = STANDARD)", kGrey, 0.5f);
        y += 16 * s;
        // three columns when the longest label fits one, else two; rows as tall as fit above RESET / SAVE / BACK
        const float avail = r_.height() - y - 2 * (bh + gap) - 60 * s;
        float widest = 0;
        for (int k = 0; k < MV_COUNT; k++) {
            char v[16];
            snprintf(v, sizeof v, ": %.2fx", r.move[k]);
            widest = std::max(widest, r_.textWidth(stdTo(moveParamLabel(k), moveParamBase(k), r.move[k], moveParamUnit(k)) + v, Renderer::kMinTextScale));
        }
        int cols = widest + 8 * s <= (gw - 2 * gap) / 3 ? 3 : 2, rows = (MV_COUNT + cols - 1) / cols;
        const float rh = std::max(18 * s, std::min(bh, avail / rows - gap));
        float cw = (gw - (cols - 1) * gap) / cols;
        for (int k = 0; k < MV_COUNT; k++) {
            float cx2 = gx + (k % cols) * (cw + gap), cy2 = y + (k / cols) * (rh + gap);
            if (ui_.slider(cx2, cy2, cw, rh, stdTo(moveParamLabel(k), moveParamBase(k), r.move[k], moveParamUnit(k)), r.move[k], 0.1f, 5, "%.2fx",
                           UiExp::state("custom").withId(moveParamLabel(k)), CustomRules().move[k]))   // (dragged: 0.05 steps; typed / reset: as is)
                r.move[k] = clampf(ui_.typed() ? r.move[k] : std::round(r.move[k] * 20) / 20, 0.1f, 5);
        }
        y += rows * (rh + gap);
        bool standard = std::all_of(std::begin(r.move), std::end(r.move), [](float v) { return v == 1; });
        if (ui_.button(x, y, w, bh, "RESET MOVEMENT TO STANDARD", UiExp::state("custom"), !standard)) for (float& v : r.move) v = 1;
        y += bh + gap;
    } else if (customTab_ == 1) {
        static const CustomRules kStdW;   // (right-click on a slider: the standard value)
        float sw = (gw - 130 * s - 3 * gap) / 3;
        for (int i = 0; i < WPN_COUNT; i++) {
            int wp = kWeaponSlots[i];
            if (wp == WPN_LOCATOR) continue;   // (hide and seek / tag only, always "it"'s: not a pick)
            bool on = r.weapons == 0 || (r.weapons >> wp & 1);
            bool onlyOne = r.weapons == (1u << wp);   // (the last one on stays on)
            if (ui_.toggle(gx, y, 124 * s, bh, upper(baseWeaponDef(wp).name), on, onlyOne ? UiExp::inert() : UiExp::state("custom"))) {
                uint16_t all = (uint16_t)((1u << WPN_COUNT) - 1), m = r.weapons ? r.weapons : all;
                m = on ? (uint16_t)(m | 1u << wp) : (uint16_t)(m & ~(1u << wp));
                if (m == 0) m = (uint16_t)(1u << wp);   // keep at least one
                r.weapons = m == all ? 0 : m;
            }
            float fx = gx + 130 * s;
            const WeaponDef& bd = baseWeaponDef(wp);   // standard -> this mode
            ui_.slider(fx, y, sw, bh, stdTo("SHOTS/S", bd.refire > 0 ? 1.0f / bd.refire : 0, r.fireRate[wp]), r.fireRate[wp], 0.25f, 4, "%.2fx", UiExp::state("custom").withId("SHOTS/S"), kStdW.fireRate[wp]);
            ui_.slider(fx + sw + gap, y, sw, bh, stdTo("DMG", bd.damage, r.damage[wp]), r.damage[wp], 0.1f, 5, "%.2fx", UiExp::state("custom").withId("DMG"), kStdW.damage[wp]);
            ui_.slider(fx + 2 * (sw + gap), y, sw, bh, (bd.projSpeed > 0 ? stdTo("SPEED", bd.projSpeed, r.projSpeed[wp], "m/s") : std::string("SPEED: HITSCAN")), r.projSpeed[wp], 0.25f, 4, "%.2fx",
                       UiExp::state("custom").withId("SPEED"), kStdW.projSpeed[wp]);
            r.fireRate[wp] = clampf(r.fireRate[wp], 0.1f, 10); r.damage[wp] = clampf(r.damage[wp], 0, 10); r.projSpeed[wp] = clampf(r.projSpeed[wp], 0.1f, 5);
            y += bh + gap;
        }
    } else {
        ui_.label(x, y, "MAP POOL (ROTATES EACH MATCH; NONE = HOST'S MAP)", Color{0.7f, 0.7f, 0.7f, 1}, 0.8f);
        y += 24 * s;
        int n = 0;
        float widest = 0;
        for (size_t i = 0; i < maps_.size() && i < mapInfo_.size(); i++)
            if (mapInfo_[i].modes >> r.baseMode & 1) { n++; widest = std::max(widest, r_.textWidth(upper(maps_[i]) + ": OFF", Renderer::kMinTextScale)); }
        // as many columns as fit above SAVE / BACK, but never so narrow a name is cut off (with many of your own maps:
        // lower rows instead, never off the bottom)
        const float avail = r_.height() - y - 80 * s;
        int maxCols = std::max(2, std::min(6, (int)((w + gap) / (widest + 8 * s + gap))));
        int cols = Ui::fitColumns(n, avail, bh + gap, 2, maxCols), rows = std::max(1, (n + cols - 1) / cols);
        const float rh = std::max(18 * s, std::min(bh, avail / rows - gap));
        float cw = (w - (cols - 1) * gap) / cols;
        int k = 0;
        for (size_t i = 0; i < maps_.size() && i < mapInfo_.size(); i++) {
            if (!(mapInfo_[i].modes >> r.baseMode & 1)) continue;
            auto it = std::find(r.maps.begin(), r.maps.end(), maps_[i]);
            bool on = it != r.maps.end();
            if (ui_.toggle(x + (k % cols) * (cw + gap), y + (k / cols) * (rh + gap), cw, rh, upper(maps_[i]), on, UiExp::state("custom"))) {
                if (on) r.maps.push_back(maps_[i]); else r.maps.erase(it);
            }
            k++;
        }
        y += rows * (rh + gap) - gap;
        // drop maps that no longer support the mode
        r.maps.erase(std::remove_if(r.maps.begin(), r.maps.end(), [&](const std::string& n) {
            for (size_t i = 0; i < maps_.size() && i < mapInfo_.size(); i++) if (maps_[i] == n) return !(mapInfo_[i].modes >> r.baseMode & 1);
            return true; }), r.maps.end());
    }
    float by = y + gap * 2;
    float bw = (w - 2 * gap) / 3;
    bool named = !r.name.empty();
    if (ui_.button(x, by, bw, 36 * s, "SAVE", UiExp::nav("host"), named)) {
        if (!editOrigName_.empty() && editOrigName_ != r.name) deleteCustomMode(editOrigName_);
        if (saveCustomMode(r)) { editOrigName_ = r.name; cfg_.hostCustom = r.name; cfg_.save(); notify("SAVED " + upper(r.name), 2); setScreen(Screen::Host); }
        else notify("COULD NOT SAVE", 3);
    }
    if (ui_.button(x + bw + gap, by, bw, 36 * s, "DELETE", UiExp::nav("host"), !editOrigName_.empty())) {
        deleteCustomMode(editOrigName_);
        if (cfg_.hostCustom == editOrigName_) cfg_.hostCustom.clear();
        setScreen(Screen::Host);
    }
    if (ui_.button(x + 2 * (bw + gap), by, bw, 36 * s, "BACK", UiExp::nav("host"))) setScreen(Screen::Host);
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
        "LOCATOR (hide and seek and tag: the seeker's / tagger's second weapon, key 0) - fire it to see one random hider / runner through walls for a second; it recharges in 5 s.",
        "# CUSTOM MODES",
        "Host Game > CUSTOM MODE > NEW: any base mode with your own health, gravity, speed, jump, knockback, weapons (fire rate, damage, projectile speed) and map pool. Players who join get the rules automatically.",
        "# MAPS AND MODES",
        "Each map is built for one mode, plus free for all; Lodestone is built for every mode. You can still start any map in any mode: the menus warn when it isn't built for it (it may be missing spawns or objectives).",
    },
    {   // CONTROLS
        "# DEFAULT KEYS (Settings > CONTROLS to change any of them, two keys per action)",
        "W A S D - move.   SPACE - jump (hold it as you land to bounce: bunny hop).",
        "SHIFT - dash: a burst of speed in the direction you're moving, tilted by where you look. You hold 2 dashes (landing refills both, a bunny hop one); the next is ready 1.2 s after the last one ends.",
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
        "Super dash: look down and dash into the floor while holding jump for even more speed and a hop that gets higher the further down you look (it leaves you 1 dash).",
        "Dash into a wall head-on (under 45 degrees) and you bounce off it at full dash speed (it leaves you 1 dash).",
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
        "WEAPON MODEL (its own button): move the gun model left / right / up / down, change its size and depth, give it its own accent colour, or hide it. Your shots come out of the model wherever you put it.",
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
        "M cycles the material (or a spawn's team), CTRL+D duplicates, DEL deletes, CTRL+Z undoes, CTRL+S saves. [ and ] change the grid.",
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

void Game::drawDemos(float cx, float y) {
    float s = ui_.s(), w = 560 * s, h = 32 * s, gap = 6 * s, x = cx - w * 0.5f;
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
    // as many rows as fit above the rate row and BACK; the wheel scrolls the rest
    int fitRows = std::max(3, (int)(((float)r_.height() - y - 4 * (h + gap) - 60 * s) / (h + gap)));
    int nd = (int)demoFiles_.size();
    demoScroll_ = std::min(demoScroll_, std::max(0, nd - fitRows));
    int rows = std::min(nd - demoScroll_, fitRows);
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, (rows + 4) * (h + gap) + (nd > rows ? 52 : 40) * s);
    ui_.labelCentered(cx, y, "DEMOS", kWhite, 1.4f);
    y += h + gap;
    if (demoFiles_.empty()) {
        ui_.labelCentered(cx, y + 6 * s, "NONE YET - IN A GAME, ESC > RECORD DEMO", kGrey, 0.6f);
        y += h + gap;
    }
    for (int k = 0; k < rows; k++, y += h + gap) {
        int i = demoScroll_ + k;
        std::string name = std::filesystem::path(demoFiles_[i]).stem().string();
        for (auto& c : name) c = c == '_' ? ' ' : (char)toupper((unsigned char)c);
        if (i < (int)demoNotes_.size() && !demoNotes_[i].empty()) name += " (" + demoNotes_[i] + ")";
        if (ui_.button(x, y, w, h, name, UiExp::state("demo", "1").withId("DEMO"))) { playDemo(demoFiles_[i]); return; }
    }
    if (nd > rows) {
        std::string more = std::to_string(demoScroll_ + 1) + "-" + std::to_string(demoScroll_ + rows) + " OF " + std::to_string(nd) + " - WHEEL TO SCROLL";
        ui_.labelCentered(cx, y - gap + 2 * s, more, kGrey, 0.5f);
        y += 12 * s;
    }
    float tw = (w - 2 * gap) / 3;
    static const std::vector<std::string> rates = {"RECORD AT 62.5 HZ", "RECORD AT FULL RATE"};
    int rate = cfg_.demoFullRate ? 1 : 0;
    if (ui_.cycle(x, y, tw * 2 + gap, h, "RATE", rate, 2, rates, UiExp::cfg("demo_full_rate"))) { cfg_.demoFullRate = rate == 1; cfg_.save(); }
    if (ui_.button(x + 2 * (tw + gap), y, tw, h, "OPEN FOLDER", UiExp::url())) {
        std::error_code ec;
        std::filesystem::create_directories(demoDir(), ec);
        openUrl(std::filesystem::absolute(demoDir(), ec).lexically_normal().string());
    }
    y += h + gap;
    ui_.label(x, y, "FULL RATE: EVERY SNAPSHOT (UP TO 1000 HZ) - SMOOTHER, MUCH BIGGER FILES", kGrey, 0.5f);
    y += 16 * s;
    if (ui_.button(x, y, w, h, "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
}

void Game::drawInfo(float cx, float y) {
    float s = ui_.s(), w = std::min(1100 * s, (float)r_.width() - 40 * s), x = cx - w * 0.5f;
    float bh = 34 * s, gap = 6 * s, H = (float)r_.height();
    int nt = (int)(sizeof kInfoTabs / sizeof kInfoTabs[0]);
    float tw = (w - gap * (nt - 1)) / nt;
    for (int t = 0; t < nt; t++)
        if (ui_.button(x + t * (tw + gap), y, tw, bh, infoTab_ == t ? std::string("> ") + kInfoTabs[t] + " <" : kInfoTabs[t], UiExp::nav(kInfoPaths[t]).withId(kInfoTabs[t]))) { infoTab_ = t; infoScroll_ = 0; }
    y += bh + gap * 2;
    float top = y, bottom = H - 70 * s;
    ui_.panel(x - 12 * s, top - 8 * s, w + 24 * s, bottom - top + 16 * s, 0.85f);
    float ts = std::max(1.0f, std::round(ui_.textScale() * 0.62f)), lh = r_.textHeight(ts) + 5 * s;
    // wrap every line to the panel (once per page and size: it measured the page word by word every frame), then draw
    // the part that's scrolled into view
    std::vector<std::pair<std::string, bool>>& rows = infoRows_;
    if (infoRowsTab_ != infoTab_ || infoRowsW_ != w || infoRowsTs_ != ts) {
    infoRowsTab_ = infoTab_; infoRowsW_ = w; infoRowsTs_ = ts;
    rows.clear();
    for (const char* line : kInfoPages[infoTab_]) {
        if (!line) break;
        bool head = line[0] == '#';
        std::string rest = head ? std::string(line + 2) : std::string(line);
        if (head && !rows.empty()) rows.push_back({"", false});
        while (!rest.empty()) {
            size_t cut = rest.size();
            while (r_.textWidth(rest.substr(0, cut), ts) > w && cut > 0) { size_t sp = rest.rfind(' ', cut - 1); cut = sp == std::string::npos ? cut - 1 : sp; }
            rows.push_back({rest.substr(0, cut), head});
            rest = cut < rest.size() ? rest.substr(cut + 1) : "";
        }
    }
    }
    float total = rows.size() * lh, view = bottom - top;
    infoScroll_ = clampf(infoScroll_, 0, std::max(0.0f, total - view));
    for (size_t i = 0; i < rows.size(); i++) {
        float ry = top + i * lh - infoScroll_;
        if (ry < top - 1 || ry + lh > bottom + 1) continue;
        r_.text(x, ry, ts, rows[i].first, rows[i].second ? ui_.accent : Color{0.88f, 0.9f, 0.93f, 1});
    }
    if (total > view) ui_.label(x + w - 150 * s, bottom + 2 * s, "WHEEL TO SCROLL", kGrey, 0.5f);
    if (ui_.button(cx - 100 * s, H - 52 * s, 200 * s, 36 * s, "BACK", UiExp::nav("main"))) setScreen(Screen::Main);
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
        std::vector<float>& t = perfScratch_;
        t.assign(frameTimes_.begin(), frameTimes_.begin() + frameCount_);
        double sum = 0;
        for (float f : t) sum += f;
        // the 99th, then the 99.9th within what's above it, then the worst within what's above that
        size_t k1 = std::min(t.size() - 1, (size_t)(t.size() * 0.99)), k2 = std::min(t.size() - 1, (size_t)(t.size() * 0.999));
        std::nth_element(t.begin(), t.begin() + k1, t.end());
        float p99 = t[k1];
        std::nth_element(t.begin() + k1, t.begin() + k2, t.end());
        float p999 = t[k2], worst = *std::max_element(t.begin() + k2, t.end());
        char b[200];
        snprintf(b, sizeof b, "AVG %.0f  1%% LOW %.0f  0.1%% LOW %.0f  WORST %.1fMS  PING %d",
                 t.size() / sum, 1.0 / p99, 1.0 / p999, worst * 1000.0,
                 net_.findPlayer(net_.clientId()) ? net_.findPlayer(net_.clientId())->ping : 0);
        perfText_ = b;
        pipeAverages();
        perfTick_ = std::string("QUARK V") + QUARK_VERSION;   // (with the numbers, not every frame)
        if (net_.hasSnapshot()) perfTick_ += "  SERVER " + std::to_string(net_.tickRate()) + " HZ  LOSS " + std::to_string(net_.snapshotLoss()) + "%";
    }
    float ts = std::max(1.0f, std::round(r_.height() / 720.0f * 2.0f));
    const std::string& tick = perfTick_;
    float lh = r_.textHeight(ts) + 6;
    float w = std::max(std::max(r_.textWidth(perfText_, ts), r_.textWidth(latText_, ts)), r_.textWidth(tick, ts));
    r_.rect(4, 4, w + 8, lh * 3 + 4, {0, 0, 0, 0.6f});
    r_.text(8, 8, ts, perfText_, {1, 1, 0.4f, 1});
    r_.text(8, 8 + lh, ts, latText_, {1, 1, 0.4f, 1});
    r_.text(8, 8 + lh * 2, ts, tick, {1, 1, 0.4f, 1});
    if (screen_ == Screen::InGame) drawPipeline(4, 4 + lh * 3 + 12, std::max(1.0f, ts * 0.85f));
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
    const float ts = std::max(1.0f, std::round(r_.height() / 720.0f * 2.0f));
    r_.rect(4, 4, r_.textWidth(perfMiniText_, ts) + 8, r_.textHeight(ts) + 8, {0, 0, 0, 0.6f});
    r_.text(8, 8, ts, perfMiniText_, {1, 1, 0.4f, 1});
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
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    switch (cfg_.displayMode) {
        case 2: // exclusive fullscreen: owns the display, bypasses the desktop compositor
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(win_, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
            break;
        case 1: { // borderless fullscreen window covering the monitor
            int mx = 0, my = 0;
            glfwGetMonitorPos(mon, &mx, &my);
            glfwSetWindowMonitor(win_, nullptr, mx, my, vm->width, vm->height, 0);
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowPos(win_, mx, my);
            glfwSetWindowSize(win_, vm->width, vm->height);
            break;
        }
        default:
            glfwSetWindowMonitor(win_, nullptr, 100, 100, cfg_.width, cfg_.height, 0);
            glfwSetWindowAttrib(win_, GLFW_DECORATED, GLFW_TRUE);
            break;
    }
    applyPresentMode(); // some drivers reset the swap interval on mode changes
    // Render at the new size right away (don't wait for a resize event that may arrive late).
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(win_, &fw, &fh);
    if (cfg_.displayMode != 0) { fw = std::max(fw, vm->width); fh = std::max(fh, vm->height); }
    if (fw > 0 && fh > 0) r_.resize(fw, fh);
}

// Latent Sync (PRESENT: LATENT SYNC, like Special K's): one frame a refresh. It's started late enough that its input is
// fresh but early enough that even the slowest recent frame is finished in time; beforeSwap then holds the finished
// frame and flips it (no vsync) just as the refresh's vertical blank begins, so the tear line falls in the blank, off
// screen, however long the frame took. The lead adapts by itself: the slowest frame (input read to finished on the GPU)
// of the last few seconds, plus a margin. The offset moves the flip (+ later) if a driver's flip lands off the blank.
GLFWmonitor* Game::gameMonitor() {
    GLFWmonitor* mon = glfwGetWindowMonitor(win_);   // (both cheap: GLFW's own fields)
    if (!mon) mon = glfwGetPrimaryMonitor();
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

// Every action, two bind slots each. Click a slot, then press a key / mouse button / scroll.
// Esc cancels, Backspace or Delete clears the slot.
void Game::drawControls(float x0, float y0, float wTotal) {
    float s = ui_.s();
    if (filePick_ == 2) {
        std::string f;
        float fw = std::max(330 * s, wTotal * 0.5f);
        if (drawFileList("controls", x0 + wTotal * 0.5f - fw * 0.5f, y0, fw, f)) {
            bool ok = controlsFromText(readTextFile(profileDir("controls") + "/" + f), cfg_.binds, cfg_.socd, cfg_.altToggle);
            if (ok) cfg_.save();
            widgetMsg_ = ok ? "IMPORTED " + upper(f) : upper(f) + " HAS NO BINDS";
            widgetMsgUntil_ = time_ + 3;
        }
        return; // the list is modal: nothing underneath takes clicks
    }
    float rowH = 22 * s, gap = 3 * s, colGap = 24 * s;
    float colW = std::max(330 * s, wTotal * 0.5f);
    float x = x0 + wTotal * 0.5f - colW - colGap * 0.5f;
    static std::string idText[A_COUNT][3];   // the UI test's ids: "<ACTION> KEY 1", "<ACTION> KEY 2", "<ACTION> UNBIND"
    static const char* ids[A_COUNT][3];
    if (!ids[0][0])
        for (int a = 0; a < A_COUNT; a++)
            for (int k = 0; k < 3; k++) { idText[a][k] = std::string(actionName(a)) + (k == 2 ? " UNBIND" : k ? " KEY 2" : " KEY 1"); ids[a][k] = idText[a][k].c_str(); }
    std::vector<int> actions; // every rebindable action (crouch was removed from the game)
    for (int a = 0; a < A_COUNT; a++) if (a != A_CROUCH) actions.push_back(a);
    int perCol = ((int)actions.size() + 1) / 2;
    float h = perCol * (rowH + gap) + 266 * s;
    float y = std::max(24 * s, std::min(y0 - 40 * s, (float)r_.height() - h + 8 * s)); // keep the BACK row on screen
    ui_.panel(x - 16 * s, y - 16 * s, colW * 2 + colGap + 32 * s, h);
    float lts = std::max(1.0f, std::round(ui_.textScale() * 0.75f));
    for (int ai = 0; ai < (int)actions.size(); ai++) {
        int a = actions[ai];
        float cx = x + (ai / perCol) * (colW + colGap), cy = y + (ai % perCol) * (rowH + gap);
        float bw = colW * 0.24f, xw = rowH;
        float nts = lts;   // a long name drops a size rather than run under its key buttons (small windows)
        if (nts > 1 && r_.textWidth(actionName(a), nts) > colW - xw - 2 * (bw + 4 * s) - 10 * s) nts -= 1;
        r_.text(cx, cy + 5 * s, nts, actionName(a), {0.85f, 0.87f, 0.9f, 1});
        if (ui_.button(cx + colW - xw, cy, xw, rowH, "X", UiExp::cfg("bind_*").withId(ids[a][2]), cfg_.binds.code[a][0] || cfg_.binds.code[a][1])) { // unbind both slots
            cfg_.binds.code[a][0] = cfg_.binds.code[a][1] = 0;
            if (captureAction_ == a) captureAction_ = -1;
            cfg_.save();
        }
        for (int k = 0; k < 2; k++) {
            float bx = cx + colW - xw - 4 * s - (2 - k) * (bw + 4 * s);
            bool cap = captureAction_ == a && captureSlot_ == k;
            std::string label = cap ? "PRESS..." : inputName(cfg_.binds.code[a][k]);
            if (ui_.button(bx, cy, bw, rowH, label, UiExp::bind().withId(ids[a][k]))) { captureAction_ = a; captureSlot_ = k; }
        }
    }
    y += perCol * (rowH + gap) + 10 * s;
    ui_.label(x, y, captureAction_ >= 0 ? "PRESS A KEY, MOUSE BUTTON OR SCROLL - ESC CANCELS, BACKSPACE CLEARS"
                                        : "CLICK A SLOT TO REBIND IT - X UNBINDS THE ACTION", {0.7f, 0.72f, 0.75f, 1}, 0.5f);
    y += 20 * s;
    {   // control scripts (ignored in ranked)
        static const std::vector<std::string> sn = {"OPPOSITE KEYS: CANCEL", "OPPOSITE KEYS: LAST WINS (SNAP TAP)", "OPPOSITE KEYS: FIRST WINS"};
        static const std::vector<std::string> an = {"ALT-FIRE: HOLD", "ALT-FIRE: TOGGLE"};
        if (ui_.cycle(x, y, colW, 32 * s, "SOCD", cfg_.socd, 3, sn, UiExp::cfg("socd"))) cfg_.save();
        int at = cfg_.altToggle;
        if (ui_.cycle(x + colW + colGap, y, colW, 32 * s, "ALT", at, 2, an, UiExp::cfg("alt_toggle"))) { cfg_.altToggle = at; cfg_.save(); }
        y += 32 * s + 6 * s;
        ui_.label(x, y, "CONTROL SCRIPTS ARE OFF IN RANKED", {0.7f, 0.72f, 0.75f, 1}, 0.5f);
        y += 20 * s;
        float tw = (colW - 2 * 6 * s) / 3;
        if (ui_.button(x, y, tw, 32 * s, "IMPORT", UiExp::state("ctlimport"))) { // controls on the clipboard win, else pick a file from controls/
            const char* clip = glfwGetClipboardString(win_);
            if (clip && controlsFromText(clip, cfg_.binds, cfg_.socd, cfg_.altToggle)) { cfg_.save(); widgetMsg_ = "IMPORTED CONTROLS FROM CLIPBOARD"; widgetMsgUntil_ = time_ + 3; }
            else filePick_ = 2;
        }
        if (ui_.button(x + tw + 6 * s, y, tw, 32 * s, "EXPORT", UiExp::state("files:controls"))) {
            std::string text = controlsToText(cfg_.binds, cfg_.socd, cfg_.altToggle), name = stampedName("controls");
            glfwSetClipboardString(win_, text.c_str());
            bool ok = writeTextFile(profileDir("controls") + "/" + name, text);
            g_profilesStale = true;
            widgetMsg_ = ok ? "COPIED + SAVED CONTROLS/" + upper(name) : "COPIED (COULDN'T WRITE FILE)";
            widgetMsgUntil_ = time_ + 4;
        }
        if (ui_.button(x + 2 * (tw + 6 * s), y, tw, 32 * s, "FOLDER", UiExp::url())) openUrl(profileDir("controls"));
        y += 32 * s + 6 * s;
        ruleRow(x, y, colW * 2 + colGap, 32 * s, false);
        y += 32 * s + 22 * s; // room for the rule row's note
        if (time_ < widgetMsgUntil_) ui_.label(x, y, widgetMsg_, ui_.accent, 0.5f);
        y += 16 * s;
    }
    float bw = colW * 0.6f;
    static const Binds defaults = [] { Binds b; b.setDefaults(); return b; }();
    bool isDefault = std::memcmp(defaults.code, cfg_.binds.code, sizeof defaults.code) == 0;
    if (ui_.button(x, y, bw, 32 * s, "RESET TO DEFAULTS", UiExp::cfg("bind_*"), !isDefault)) { cfg_.binds.setDefaults(); cfg_.save(); }
    if (ui_.button(x + colW * 2 + colGap - bw, y, bw, 32 * s, "BACK", UiExp::nav("~settings"))) { controlsOpen_ = false; captureAction_ = -1; filePick_ = 0; cfg_.save(); }
}

// Every map or mode as a grid of buttons. Maps that can't run the chosen mode are greyed out, and
// each map says which mode it was built for.
void Game::drawPicker(float cx, float y) {
    float s = ui_.s(), bh = 40 * s, gap = 8 * s;
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
    int cols = 3;
    float bw = 250 * s;
    float rowH = bh + (variants ? 30 : 18) * s + gap;
    {   // a long list (your own maps too): more, narrower columns so BACK stays on screen
        int fitRows = std::max(1, (int)(((float)r_.height() - y - 34 * s - bh - 40 * s) / rowH));
        if ((n + cols - 1) / cols > fitRows) {
            cols = std::min(8, (n + fitRows - 1) / fitRows);
            bw = std::min(bw, ((float)r_.width() - 64 * s - (cols - 1) * gap) / cols);
        }
    }
    float w = cols * bw + (cols - 1) * gap;
    float x = cx - w * 0.5f;
    int rows = (n + cols - 1) / cols;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 40 * s + rows * rowH + bh + 30 * s);
    std::string title = picker_ == PICK_TMAPS ? "TOURNEY MAPS (PICK ANY, THEY ROTATE)" : picker_ == PICK_TMODES ? "TOURNEY RULESETS (PICK ANY, THEY ROTATE)"
                      : ladders ? "RANKED - CHOOSE A LADDER"
                      : variants ? std::string(modeTitle(mode)) + " - CHOOSE A RULESET" : modes ? "CHOOSE A MODE"
                      : std::string("CHOOSE A MAP FOR ") + (rankedNow ? "RANKED" : modeTitle(mode));
    if ((live && livePickVote_) || multi) title = "VOTE: " + title;
    ui_.labelCentered(cx, y, title);
    y += 34 * s;
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
    for (int i = 0; i < n; i++) {
        float bx = x + (i % cols) * (bw + gap), by = y + (i / cols) * rowH;
        std::string label, sub;
        bool ok = true, cur, warnRow = false;
        if (ladders) {
            label = arenaVariant(kLadders[i]).title;
            sub = std::string(arenaVariant(kLadders[i]).desc) + " - OWN RATING";
            cur = rankedNow && kLadders[i] == (live ? net_.mode().arenaVariant : variant);
        } else if (variants) {
            label = arenaVariant(i).title;
            sub = arenaVariant(i).desc;
            cur = multi ? multiSel_[i] : i == variant;
        } else if (modes && i == MODE_COUNT) {
            label = "RANKED";
            sub = "LADDERS (1V1, INSTAGIB FFA) - DEFAULT MODEL, HUD, CONTROLS >";
            cur = rankedNow;
        } else if (modes) {
            label = modeTitle(i);
            cur = i == mode && !rankedNow;
            for (auto& mi : mapInfo_) if (mi.preferredMode == i) sub = "MAP: " + upper(mi.title);
            if (sub.empty() && i == MODE_TAG) for (auto& mi : mapInfo_) if (mi.preferredMode == MODE_HIDE) sub = "MAP: " + upper(mi.title);
            if (i == MODE_ARENA) sub = "9 RULESETS >";
            if (i == MODE_TOURNEY) sub = "BRACKET OF 1V1S - RULESETS >";
        } else {
            const MapInfo& mi = mapInfo_[i];
            label = upper(maps_[i]);
            cur = multi ? multiSel_[i] : i == mapIdx;
            bool built = (mi.modes >> (multi ? MODE_TOURNEY : mode)) & 1;
            int ladderNow = live ? net_.mode().arenaVariant : cfg_.hostArenaVariant;
            bool rankedOnly = rankedNow && !(mi.ranked >> ladderNow & 1);
            // any map can be picked for any mode; one not built for it says so (it may be missing what the mode needs)
            sub = rankedOnly ? "NOT A RANKED MAP FOR THIS LADDER" : !built ? "WARNING: NOT BUILT FOR THIS MODE - MAY NOT WORK" : mi.preferredMode >= 0 ? std::string("BUILT FOR ") + modeTitle(mi.preferredMode) : "ALL MODES";
            ok = !rankedOnly;
            warnRow = !built && !rankedOnly;
        }
        if (cur) r_.rect(bx - 2 * s, by - 2 * s, bw + 4 * s, bh + 4 * s, ui_.accent);
        UiExp pe = multi ? UiExp::state("multisel")
                 : ladders && live ? UiExp::cmd(livePickVote_ ? "callvote ranked on" : "ranked on")
                 : ladders ? UiExp::nav("host")
                 : variants ? (live ? UiExp::nav("ingame/pause/picker:livemap") : UiExp::nav(done))
                 : modes && i == MODE_COUNT ? UiExp::nav(live ? "ingame/pause/picker:ladders" : "host/picker:ladders")
                 : modes ? (i == MODE_ARENA || i == MODE_TOURNEY ? UiExp::nav(live ? "ingame/pause/picker:variants" : host ? "host/picker:variants" : "vsbots/picker:variants")
                            : live ? UiExp::nav("ingame/pause/picker:livemap") : UiExp::nav(done))
                 : live ? UiExp::cmd(livePickVote_ ? "callvote map" : "map") : UiExp::nav(done);
        if (ui_.button(bx, by, bw, bh, label, pe, ok)) {
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
        }
        // wrap long descriptions onto two small lines
        float sts = std::max(1.0f, std::round(ui_.textScale() * 0.5f));
        std::string line1 = sub, line2;
        while (r_.textWidth(line1, sts) > bw && line1.find(' ') != std::string::npos) {
            size_t sp = line1.rfind(' ');
            line2 = line1.substr(sp + 1) + (line2.empty() ? "" : " " + line2);
            line1 = line1.substr(0, sp);
        }
        Color sc = warnRow ? Color{1.0f, 0.55f, 0.3f, 1} : ok ? Color{0.7f, 0.72f, 0.75f, 1} : Color{0.5f, 0.3f, 0.3f, 1};
        r_.text(bx + 2 * s, by + bh + 3 * s, sts, line1, sc);
        if (!line2.empty()) r_.text(bx + 2 * s, by + bh + 3 * s + r_.textHeight(sts) + 2 * s, sts, line2, sc);
    }
    y += rows * rowH + 10 * s;
    if (multi) {
        int count = 0;
        std::string list;
        for (int i = 0; i < n; i++)
            if (multiSel_[i]) { list += (count++ ? "," : "") + std::string(variants ? arenaVariant(i).name : maps_[i]); }
        if (ui_.button(cx - 210 * s, y, 200 * s, bh, "CALL VOTE", UiExp::cmd(picker_ == PICK_TMAPS ? "callvote tmaps" : "callvote tmodes"), count > 0)) {
            net_.sendCommand(std::string("callvote ") + (picker_ == PICK_TMAPS ? "tmaps " : "tmodes ") + list);
            picker_ = PICK_NONE; voteMenu_ = false; paused_ = false;
        }
        if (ui_.button(cx + 10 * s, y, 200 * s, bh, "BACK", UiExp::nav(back))) picker_ = PICK_NONE;
    } else if (ui_.button(cx - 100 * s, y, 200 * s, bh, "BACK", UiExp::nav(back))) picker_ = PICK_NONE;
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

void Game::drawHeldWeapon(int i, int w, const Vec3& shoulder, float yaw, float pitch, float bs, const Vec3& accent, float sv) {
    if (w < 0 || w >= WPN_COUNT) w = WPN_KNIFE;
    float sw = (float)(time_ - knifeSlash_[i]) / 0.18f;   // a knife swing: the hand chops down and back up
    if (w == WPN_KNIFE && knifeSlash_[i] > 0 && sw < 1) pitch -= 0.7f * std::sin(sw * 3.14159f);
    r_.boxPitched(shoulder, Vec3(0, 0, -0.22f) * bs, Vec3(0.055f, 0.06f, 0.22f) * bs, yaw, pitch, {0.12f, 0.12f, 0.14f}, sv);   // the arm
    const Vec3 hand = Vec3(0, 0.02f, -0.5f) * bs;
    const float k = 2.0f * bs;   // the viewmodel's boxes at world size (a held weapon reads at a distance)
    ModelPalette pal;
    pal.c[TINT_METAL] = viewmodelMetal(r_.metalColor);
    pal.c[TINT_BASE] = {0.85f, 0.88f, 0.92f};   // bright steel (a blade has to read from across a room, on any map)
    pal.c[TINT_TEAM] = accent;
    pal.c[TINT_GLOW] = r_.glowColor;
    if (const ModelMesh* ov = modelOverride(MODEL_WEAPON, w)) {   // the owner's model (docs/ASSETS.md)
        Vec3 tint[TINT_COUNT];
        overrideTints(MODEL_WEAPON, pal, tint);
        Mat4 sc;
        sc.m[0] = sc.m[5] = sc.m[10] = k;
        r_.mesh(*ov, Mat4::translate(shoulder) * Mat4::rotY(yaw) * Mat4::rotX(pitch) * Mat4::translate(hand) * sc, tint, sv, true);
        return;
    }
    viewmodelParts(w, pal, parts_);
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

// VISUALS: what's drawn (world look, shot effects, overlays)
void Game::drawVisualSettings(float x0, float y0, float wTotal) {
    float s = ui_.s();
    float h = 32 * s, gap = 7 * s;
    float w = std::max(360 * s, wTotal * 0.5f);
    const float ph = (cfg_.fullbright ? 16 : 15) * (h + gap) + 48 * s;
    float x = x0 + wTotal * 0.5f - w * 0.5f, y = std::max(24 * s, std::min(y0, (float)r_.height() - ph + 8 * s));   // (bottom on screen)
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, ph);
    ui_.toggle(x, y, w, h, "FULLBRIGHT (FLAT MAP, NO LIGHTING)", cfg_.fullbright, UiExp::cfg("fullbright")); y += h + gap;
    if (cfg_.fullbright) {   // its colour: R G B (like the team colours), a swatch, RESET to white
        float rw = 74 * s, sww = 40 * s, sw = (w - rw - sww - 4 * gap) / 3;
        const char* ch[3] = {"MAP R", "MAP G", "MAP B"};
        Vec3& c = cfg_.fullbrightColor;
        for (int i = 0; i < 3; i++) {
            float v = c[i] * 255.0f;
            if (ui_.slider(x + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f", UiExp::cfg("fullbright_color"), std::round(defaultSettings().fullbrightColor[i] * 255))) c[i] = clampf(std::round(v), 0, 255) / 255.0f;
        }
        r_.rect(x + 3 * (sw + gap), y, sww, h, rgba(c));
        bool white = c.x == defaultSettings().fullbrightColor.x && c.y == defaultSettings().fullbrightColor.y && c.z == defaultSettings().fullbrightColor.z;
        if (ui_.button(x + w - rw, y, rw, h, "RESET", UiExp::cfg("fullbright_color").withId("RESET MAP COLOR"), !white)) c = defaultSettings().fullbrightColor;
        y += h + gap;
    }
    static const std::vector<std::string> own = {"YOUR SHOTS: WITH THE WEAPON MODEL", "YOUR SHOTS: ALWAYS", "YOUR SHOTS: NEVER"};
    ui_.cycle(x, y, w, h, "YOUR SHOTS", cfg_.ownShots, 3, own, UiExp::cfg("own_shots")); y += h + gap;
    ui_.toggle(x, y, w, h, "OTHER PLAYERS' TRACERS / BEAMS", cfg_.enemyShots, UiExp::cfg("enemy_shots")); y += h + gap;
    ui_.toggle(x, y, w, h, "HEALTH BARS OVER PLAYERS", cfg_.healthBars, UiExp::cfg("health_bars")); y += h + gap;
    {
        static const std::vector<std::string> hbp = {"HEALTH BAR POSITION: TOP (OVER THE HEAD)", "HEALTH BAR POSITION: BOTTOM (AT THE FEET)"};
        int hi = cfg_.healthBarsBottom ? 1 : 0;
        if (ui_.cycle(x, y, w, h, "HEALTH BAR POSITION", hi, 2, hbp, UiExp::cfg("health_bars_bottom"))) cfg_.healthBarsBottom = hi == 1;
        y += h + gap;
    }
    ui_.slider(x, y, w, h, "SCOPE OVERLAY", cfg_.scopeOpacity, 0, 1, "%.2f", UiExp::cfg("scope_opacity"), defaultSettings().scopeOpacity); y += h + gap;
    cfg_.scopeOpacity = clampf(cfg_.scopeOpacity, 0, 1);
    {   // the sniper's and rifle's alt-fire zoom (degrees, as FOV; the standard zoom in the label). Client-side only.
        float hw = (w - gap) * 0.5f;
        static const std::string sl = "SNIPER ZOOM FOV (STD " + std::to_string((int)baseWeaponDef(WPN_SNIPER).zoomFov) + ")",
                                 rl = "RIFLE ZOOM FOV (STD " + std::to_string((int)baseWeaponDef(WPN_RIFLE).zoomFov) + ")";
        ui_.slider(x, y, hw, h, sl, cfg_.zoomFovSniper, 10, 90, "%.0f", UiExp::cfg("zoom_fov_sniper"), defaultSettings().zoomFovSniper);
        ui_.slider(x + hw + gap, y, hw, h, rl, cfg_.zoomFovRifle, 10, 90, "%.0f", UiExp::cfg("zoom_fov_rifle"), defaultSettings().zoomFovRifle);
        cfg_.zoomFovSniper = clampf(cfg_.zoomFovSniper, 10, 140);   // (typed: quark.cfg's range; used up to the normal FOV)
        cfg_.zoomFovRifle = clampf(cfg_.zoomFovRifle, 10, 140);
        y += h + gap;
    }
    ui_.toggle(x, y, w, h, "STICKY COUNTER", cfg_.stickyCounter, UiExp::cfg("sticky_counter")); y += h + gap;
    ui_.toggle(x, y, w, h, "WINNERS CROWN", cfg_.crowns, UiExp::cfg("crowns")); y += h + gap;
    if (ui_.toggle(x, y, w, h, "DISABLE CHAT (HIDE PLAYERS' MESSAGES)", cfg_.chatOff, UiExp::cfg("chat_off")) && cfg_.chatOff) for (auto& t : typingUntil_) t = 0;
    y += h + gap;
    {   // damage numbers: on / off, and their size, with a sample of the number at that size
        float tw = w * 0.42f, sw = w - tw - gap;
        ui_.toggle(x, y, tw, h, "DAMAGE NUMBERS", cfg_.dmgNums, UiExp::cfg("dmg_nums"));
        if (ui_.slider(x + tw + gap, y, sw - 56 * s, h, "SIZE", cfg_.dmgNumScale, 0.5f, 3.0f, "%.2f", UiExp::cfg("dmg_num_scale"), defaultSettings().dmgNumScale) && !ui_.typed())
            cfg_.dmgNumScale = std::round(cfg_.dmgNumScale * 20) / 20;   // (dragged: 0.05 steps; typed: as typed)
        cfg_.dmgNumScale = clampf(cfg_.dmgNumScale, 0.5f, 3.0f);
        float k = std::max(1.0f, std::round(ui_.textScale() * cfg_.dmgNumScale));
        r_.rect(x + w - 50 * s, y, 50 * s, h, {0.42f, 0.43f, 0.45f, 1});
        if (cfg_.dmgNums) r_.text(x + w - 25 * s - r_.textWidth("42", k) * 0.5f, y + (h - r_.textHeight(k)) * 0.5f, k, "42", kWhite);
        y += h + gap;
    }
    // the flash round the screen's edges when you're hit (0 = off)
    ui_.slider(x, y, w, h, "DAMAGE FLASH (SCREEN EDGES WHEN HIT, 0 = OFF)", cfg_.damageFlash, 0, 1, "%.2f", UiExp::cfg("damage_flash"), defaultSettings().damageFlash);
    cfg_.damageFlash = clampf(cfg_.damageFlash, 0, 1);
    y += h + gap;
    ui_.label(x, y, "YOUR SHOTS: TRACERS AND THE LASER BEAM COME FROM WHERE THE GUN IS (OR WOULD BE)", {0.7f, 0.72f, 0.75f, 1}, 0.5f);
    y += 14 * s;
    ui_.label(x, y, "RANKED: NO HEALTH BARS", {0.7f, 0.72f, 0.75f, 1}, 0.5f);
    y += 14 * s + gap * 2;
    if (ui_.button(x, y, w, h, "BACK", UiExp::nav("~settings"))) { visualsOpen_ = false; cfg_.save(); }
}

void Game::drawHudSettings(float x0, float y0, float wTotal) {
    float s = ui_.s();
    float h = 32 * s, gap = 7 * s;
    float w = std::max(360 * s, wTotal * 0.5f);
    float ph = 14 * (h + gap) + 3 * gap + 48 * s;
    float x = x0 + wTotal * 0.5f - w * 0.5f, y = std::max(24 * s, std::min(y0, (float)r_.height() - ph + 8 * s)); // panel bottom stays on screen
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, ph);
    static const char* names[] = {"HEALTH", "WEAPON NAME", "COOLDOWN BARS", "SPEED METER", "WEAPON LIST", "WEAPON MODEL", "KEY OVERLAY (VERBOSE)"};
    static const int listed[] = {Settings::HUD_HEALTH, Settings::HUD_WEAPON, Settings::HUD_BARS, Settings::HUD_SPEED, Settings::HUD_SLOTS, Settings::HUD_KEYS};
    const int nListed = (int)(sizeof listed / sizeof listed[0]);
    // HUD elements in a dropdown; the weapon model (a 3D model, not a HUD element) has its own button
    bool vmSel = hudSel_ == Settings::HUD_VIEWMODEL;
    float dw = w * 0.62f, bw2 = w - dw - gap;
    if (ui_.button(x, y, dw, h, std::string(vmSel ? "HUD ELEMENT" : names[hudSel_]) + (hudDrop_ ? "  ^" : "  V"),
                   UiExp::nav(hudDrop_ ? "~settings/hud" : "~settings/hud/elements").withId("HUD ELEMENT"))) hudDrop_ = !hudDrop_;
    if (ui_.button(x + dw + gap, y, bw2, h, vmSel ? "> WEAPON MODEL <" : "WEAPON MODEL",
                   (vmSel && !hudDrop_ ? UiExp::inert() : hudDrop_ ? UiExp::nav("~settings/hud") : UiExp::state("hudsel")).withId("WEAPON MODEL"))) { hudSel_ = Settings::HUD_VIEWMODEL; hudDrop_ = false; }
    y += h + gap;
    if (hudDrop_) { // modal until one is picked
        ui_.panel(x - 4 * s, y - 4 * s, dw + 8 * s, nListed * (h + gap) + 4 * s, 0.97f);
        for (int k = 0; k < nListed; k++, y += h + gap) {
            int i = listed[k];
            if (ui_.button(x, y, dw, h, (i == hudSel_ ? "> " : "") + std::string(names[i]), (i == hudSel_ ? UiExp::nav("~settings/hud") : UiExp::state("hudsel")).withId(names[i]))) { hudSel_ = i; hudDrop_ = false; }
        }
        return;
    }
    auto& e = cfg_.hud[hudSel_];
    bool shown = !e.hidden;
    const UiExp he = UiExp::cfg("hud_*");
    ui_.toggle(x, y, w, h, "SHOW", shown, he); e.hidden = !shown;
    y += h + gap;
    // (typed numbers: within quark.cfg's ranges)
    const auto& de = defaultSettings().hud[hudSel_];   // (right-click on a slider: the element's default)
    ui_.slider(x, y, w, h, "X OFFSET", e.x, -640, 640, "%.0f", he, de.x); e.x = clampf(std::round(e.x), -1280, 1280);
    y += h + gap;
    ui_.slider(x, y, w, h, "Y OFFSET", e.y, -360, 360, "%.0f", he, de.y); e.y = clampf(std::round(e.y), -720, 720);
    y += h + gap;
    ui_.slider(x, y, w, h, "SIZE", e.scale, 0.5f, 3, "%.2f", he, de.scale); e.scale = clampf(e.scale, 0.5f, 3);
    y += h + gap;
    if (hudSel_ == Settings::HUD_VIEWMODEL) { ui_.slider(x, y, w, h, "DEPTH", e.z, -300, 300, "%.0f", he, de.z); e.z = clampf(std::round(e.z), -300, 300); }
    y += h + gap;
    ui_.toggle(x, y, w, h, hudSel_ == Settings::HUD_VIEWMODEL ? "CUSTOM ACCENT COLOR" : "CUSTOM COLOR", e.custom, he);
    y += h + gap;
    if (e.custom) {
        float sw = (w - 2 * gap) / 3;
        const char* ch[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            float v = e.color[i] * 255.0f;
            if (ui_.slider(x + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f", he, std::round(de.color[i] * 255))) e.color[i] = clampf(std::round(v), 0, 255) / 255.0f;
        }
    }
    y += h + gap * 3;
    // (a RESET with nothing to reset is greyed out)
    auto isDefault = [](const Settings::HudElem& el) {
        Settings::HudElem d;
        return el.x == d.x && el.y == d.y && el.scale == d.scale && el.custom == d.custom && el.hidden == d.hidden && el.z == d.z &&
               el.color.x == d.color.x && el.color.y == d.color.y && el.color.z == d.color.z;
    };
    if (ui_.button(x, y, w * 0.5f - gap * 0.5f, h, "RESET", he, !isDefault(e))) e = Settings::HudElem{};
    if (ui_.button(x + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "BACK", UiExp::nav("~settings"))) { hudOpen_ = false; cfg_.save(); }
    y += h + gap;
    if (ui_.button(x, y, w, h, "RESET ALL", he, !std::all_of(std::begin(cfg_.hud), std::end(cfg_.hud), isDefault))) for (auto& el : cfg_.hud) el = Settings::HudElem{};
    y += h + gap;
    ui_.toggle(x, y, w * 0.5f - gap * 0.5f, h, "VERBOSE INFO", cfg_.hudVerbose, UiExp::cfg("hud_verbose"));
    ui_.toggle(x + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "CUSTOM WIDGETS", cfg_.hudWidgets, UiExp::cfg("hud_widgets"));
    y += h + gap;
    if (ui_.button(x, y, w, h, "WIDGET EDITOR (DRAG, ADD, IMPORT / EXPORT)", UiExp::nav("~settings/hud/widgets"))) { widgetEdit_ = true; widgetSel_ = -1; }
    y += h + gap;
    ruleRow(x, y, w, h, true);
}

void Game::drawCrosshairSettings(float x0, float y0, float wTotal) {
    float s = ui_.s();
    float h = 32 * s, gap = 7 * s;
    float w = std::max(360 * s, wTotal * 0.5f);
    float x = x0 + wTotal * 0.5f - w * 0.5f;
    float y = y0;
    ui_.panel(x - 16 * s, y - 16 * s, w + 32 * s, 110 * s + 9 * (h + gap) + 2 * gap + 32 * s);
    // preview on a mid-grey square, like the world behind it: as it is, and as a hit flashes it
    float ph = 110 * s;
    r_.rect(x, y, w, ph, {0.42f, 0.43f, 0.45f, 1});
    drawCrosshair(x + w * 0.3f, y + ph * 0.5f, s, false);
    drawCrosshair(x + w * 0.7f, y + ph * 0.5f, s, true);
    ui_.labelCentered(x + w * 0.3f, y + ph - 18 * s, "NORMAL", {0.85f, 0.86f, 0.88f, 1}, 0.5f);
    ui_.labelCentered(x + w * 0.7f, y + ph - 18 * s, "ON HIT", {0.85f, 0.86f, 0.88f, 1}, 0.5f);
    y += ph + gap;
    static const std::vector<std::string> styles = {"CROSS", "CROSS + DOT", "DOT", "CIRCLE", "T"};
    ui_.cycle(x, y, w, h, "STYLE", cfg_.chStyle, 5, styles, UiExp::cfg("ch_style"));
    y += h + gap;
    ui_.slider(x, y, w, h, "LENGTH", cfg_.chLength, 0, 30, "%.0f", UiExp::cfg("ch_length"), defaultSettings().chLength);
    cfg_.chLength = clampf(std::round(cfg_.chLength), 0, 30);
    y += h + gap;
    ui_.slider(x, y, w, h, "THICKNESS", cfg_.chThickness, 1, 8, "%.0f", UiExp::cfg("ch_thickness"), defaultSettings().chThickness);
    cfg_.chThickness = clampf(std::round(cfg_.chThickness), 1, 8);
    y += h + gap;
    ui_.slider(x, y, w, h, "GAP", cfg_.chGap, 0, 20, "%.0f", UiExp::cfg("ch_gap"), defaultSettings().chGap);
    cfg_.chGap = clampf(std::round(cfg_.chGap), 0, 20);
    y += h + gap;
    ui_.toggle(x, y, w * 0.5f - gap * 0.5f, h, "OUTLINE", cfg_.chOutline, UiExp::cfg("ch_outline"));
    ui_.toggle(x + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "TEAM COLOR", cfg_.chTeamColor, UiExp::cfg("ch_team_color"));
    y += h + gap;
    if (!cfg_.chTeamColor) {
        float sw = (w - 2 * gap) / 3;
        const char* ch[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            float v = cfg_.chColor[i] * 255.0f;
            if (ui_.slider(x + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f", UiExp::cfg("ch_color"), std::round(defaultSettings().chColor[i] * 255))) cfg_.chColor[i] = clampf(std::round(v), 0, 255) / 255.0f;
        }
    }
    y += h + gap;
    // HIT FLASH: off, or the colour a hit flashes the crosshair (white by default)
    ui_.toggle(x, y, w, h, "HIT FLASH", cfg_.chHitFlash, UiExp::cfg("ch_hit_flash"));
    y += h + gap;
    if (cfg_.chHitFlash) {
        float sw = (w - 2 * gap) / 3;
        const char* ch[3] = {"HIT R", "HIT G", "HIT B"};
        for (int i = 0; i < 3; i++) {
            float v = cfg_.chHitColor[i] * 255.0f;
            if (ui_.slider(x + i * (sw + gap), y, sw, h, ch[i], v, 0, 255, "%.0f", UiExp::cfg("ch_hit_color"), std::round(defaultSettings().chHitColor[i] * 255))) cfg_.chHitColor[i] = clampf(std::round(v), 0, 255) / 255.0f;
        }
    }
    y += h + gap * 3;
    static const Settings dch;   // (RESET is greyed out while the crosshair is the default one)
    bool chDefault = cfg_.chStyle == dch.chStyle && cfg_.chLength == dch.chLength && cfg_.chThickness == dch.chThickness && cfg_.chGap == dch.chGap &&
                     cfg_.chOutline == dch.chOutline && cfg_.chTeamColor == dch.chTeamColor && length(cfg_.chColor - dch.chColor) == 0 &&
                     cfg_.chHitFlash == dch.chHitFlash && length(cfg_.chHitColor - dch.chHitColor) == 0;
    if (ui_.button(x, y, w * 0.5f - gap * 0.5f, h, "RESET", UiExp::cfg("ch_*"), !chDefault)) {
        const Settings& d = dch;
        cfg_.chStyle = d.chStyle; cfg_.chLength = d.chLength; cfg_.chThickness = d.chThickness; cfg_.chGap = d.chGap;
        cfg_.chOutline = d.chOutline; cfg_.chTeamColor = d.chTeamColor; cfg_.chColor = d.chColor;
        cfg_.chHitFlash = d.chHitFlash; cfg_.chHitColor = d.chHitColor;
    }
    if (ui_.button(x + w * 0.5f + gap * 0.5f, y, w * 0.5f - gap * 0.5f, h, "BACK", UiExp::nav("~settings"))) { crosshairOpen_ = false; cfg_.save(); }
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
        bool was = prevMove_.flags & MF_ONGROUND, now = m.flags & MF_ONGROUND;
        Vec3 d = m.pos - prevMove_.pos;
        float h = std::sqrt(d.x * d.x + d.z * d.z);
        if (now && h < 2.0f) {
            stepDist_ += h;
            if (stepDist_ > 2.4f) { stepDist_ = 0; audio_.play(SND_STEP, 0.55f, 0.9f + (qrand() % 20) * 0.01f); }
        }
        if (was && !now && m.vel.y > 3) audio_.play(SND_JUMP, 0.5f);
        if (!was && now && prevMove_.vel.y < -6) audio_.play(SND_LAND, clampf(-prevMove_.vel.y / 20.0f, 0.3f, 1.0f));
        if (m.dashTimer > 0 && prevMove_.dashTimer <= 0) audio_.play(SND_DASH, 0.8f);
        // a dash cut short by a jump: a super (dash jump), or a hyper (super dash: the dash was aimed down into the
        // floor). (A dash running out on its own has under a frame left; an upward dash ending isn't a jump.)
        if (prevMove_.dashTimer > (float)frameDt + 0.01f && m.dashTimer <= 0 && m.vel.y > 1.0f && !(m.flags & MF_ONGROUND))
            audio_.play(prevMove_.dashUp < 0 && m.dashCharges == 1 ? SND_HYPER : SND_SUPER, 0.9f);   // (a hyper leaves one charge, a super all)
        if ((m.flags & MF_HOOKED) && !(prevMove_.flags & MF_HOOKED)) {
            audio_.play(SND_GRAPPLE_FIRE, 0.7f);
            audio_.play3D(SND_GRAPPLE_HIT, m.hookPoint, 0.9f);
        }
    }
    prevMove_ = m;
    prevAlive_ = alive;

    for (int i = 0; i < kMaxPlayers; i++) {
        RemoteAudio& ra = remoteAudio_[i];
        if (i == me || !players[i].valid || !(players[i].snap.flags & PS_ALIVE)) { ra.valid = false; continue; }
        const PlayerSnap& p = players[i].snap;
        int sp = net_.playerSoundPack(i); // their sounds in the pack they picked
        if (ra.valid) {
            Vec3 d = p.pos - ra.pos;
            float h = std::sqrt(d.x * d.x + d.z * d.z);
            // walking on the ground: by speed, not per frame (0.05 m a frame let anything slower than 200 m/s up or down
            // count at 4000 fps: jumping, falling and grappling enemies made footsteps in the air; 3 m/s is the old
            // threshold at 60 fps; the 2 m a frame cap stays a teleport guard)
            if (h < 2.0f && std::fabs(d.y) < 3.0f * (float)frameDt) {
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
            } else if (firing && !remoteFiring_[i] && p.weapon != WPN_LASER && p.weapon < WPN_LOCATOR && time_ >= ra.nextShot) {
                ra.nextShot = time_ + weaponDef(p.weapon).refire * 0.6f; // (a flag flicker between snapshots isn't a second shot)
                audio_.play3D(SND_RIFLE + p.weapon, p.pos + Vec3(0, 1.5f, 0), 1.0f, 0.97f + (qrand() % 7) * 0.01f, sp);
            }
        }
        ra.pos = p.pos;
        ra.hooked = p.hooked;
        ra.health = p.health;
        ra.valid = true;
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
    std::vector<float> t = benchTimes_;
    std::sort(t.begin(), t.end());
    double sum = 0;
    for (float f : t) sum += f;
    auto pct = [&](double p) { return t[std::min(t.size() - 1, (size_t)(t.size() * p))]; };
    std::vector<float> c = benchCpu_;
    std::sort(c.begin(), c.end());
    double csum = 0;
    for (float f : c) csum += f;
    printf("BENCH %s: %zu frames  avg %.1f fps  1%% low %.1f  0.1%% low %.1f  worst %.2f ms  |  cpu avg %.3f ms  99%% %.3f ms  99.9%% %.3f ms\n",
           net_.map().name.c_str(), t.size(), t.size() / sum, 1.0 / pct(0.99), 1.0 / pct(0.999), t.back() * 1000.0,
           csum / c.size() * 1000.0, c[std::min(c.size() - 1, (size_t)(c.size() * 0.99))] * 1000.0, c[std::min(c.size() - 1, (size_t)(c.size() * 0.999))] * 1000.0);
    qzoneDump();
    fflush(stdout);
    quit_ = true;
}
