// The client application: menus, hosting, connection, input, world/HUD rendering.
#pragma once
#include <cstring>
#include <map>
#include <set>
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include "hudwidgets.h"
#include "layout.h"
#include "theme.h"
#include "weaponpredict.h"
#include "../game/netclient.h"
#include "../game/glass.h"
#include "../game/server.h"
#include "../game/ranked.h"
#include "audio.h"
#include "clientfeel.h"
#include "modecalls.h"
#include "rawmouse.h"
#include "gpuprefs.h"
#include "binds.h"
#include "config.h"
#include "renderer.h"
#include "uifonts.h"
#include "ui.h"

struct GLFWwindow;

// The redesign's coordinates (720p, a 1280 frame) on this screen: x centred on W / 2, everything times s, y from oy
// (Game::menuLayout: the menus' pages, narrowed on a squarer screen with their top kept under the title)
struct SetLayout {
    float s = 1, cx = 640, oy = 0;
    float X(float px) const { return cx + (px - 640) * s; }
    float Y(float py) const { return oy + py * s; }
    float L(float px) const { return px * s; }
};

struct LaunchOptions {
    std::string connect;        // --connect addr
    int hostMode = -1;          // --host mode
    int vsBotsMode = -1;        // --vsbots mode (uses the saved bot settings)
    std::string hostMap;        // --map name
    std::string screenshot;     // --screenshot path (taken after --shot-delay seconds, then quit)
    float shotDelay = 3.0f;
    float spawnYaw = -1000;     // --yaw deg (debug camera direction for screenshots)
    float spawnPitch = 0;       // --pitch deg
    int debugWeapon = -1;       // --weapon N (1-5), for screenshots
    bool debugScope = false, debugFire = false; // --scope, --fire
    bool debugHook = false;     // --hook: the grapple held (screenshots; with --models: the lineup hangs on ropes instead)
    bool debugPulse = false;    // --pulse: --fire / --scope let go every other 0.3 s (a switch or a throw needs a release), and
                                // a step right before the --screenshot (what was fired is seen from the side)
    bool debugPerf = false;     // --perf: F3 overlay on, with a synthetic input event every frame
    bool textHeavy = false;     // --textheavy: scoreboard + F3 + chat lines always on, to measure text cost (with --bench)
    bool feedShot = false;      // --feedshot: made-up kill feed and chat lines that stay (HUD screenshots and benches)
    bool debugCrosshair = false;
    int debugPicker = 0;        // --picker N: open that host-menu list (1 maps, 2 modes, 7 rulesets)
    bool debugPause = false, debugVote = false; // --pause / --votemenu
    std::string backdrop;       // --backdrop MAP: menu backdrop of that map, no menu drawn (site screenshots)
    float backdropAngle = -1;   // --orbit DEG: fixed orbit position for --backdrop   // --picker: open the host menu's map list
    bool debugWidgets = false;
    bool debugHudLayout = false; // --hudlayout: open HUD layout settings in game
    bool debugModels = false;
    int debugRps = 0;           // --rpsshot: the hide and seek tiebreak panel with made-up players (1 picking, 2 reveal, 3 three-way reveal)
    bool camSet = false;         // --cam: fixed backdrop camera (map analysis shots); in a match, the view's camera
    Vec3 camPos;
    float camYaw = 0, camPitch = 0;
    bool timeTrial = false;      // --timetrial: start a time trial (on --map if that is a ranked race map, else the first)
    bool debugRaceStats = false; // --racestats: the leaderboard's race stats page    // --models: every player model lined up in front of you  // --widgets: open the HUD widget editor in game
    bool debugSettings = false; // --settings: open the settings screen in game
    bool debugControls = false; // --controls
    std::string settingsTab;    // --settingstab PAGE: with --settings / --settingsmenu, that page (gameplay, controls/movement, hud/elements...)
    std::string menuPage;       // --menupage PATH: that menu page (Game::uiScreenPath's name: host/advanced, custom/dash, info/controls+2...)
    std::string editMap;        // --editor MAP: open the map editor on that map
    bool editSession = false;   // --editsession MAP: and host it as an edit session
    int editSel = -1;           // --edsel N: select box N (screenshots)
    std::string editScript;     // --edscript "click x y;type t;..." : drive the editor's UI headless (tests, screenshots)
    bool debugLeaderboard = false; // --leaderboard: open the leaderboard screen
    std::string demo;              // --demo <file>: watch it
    int debugCustom = 0;           // --customshot <tab>: open the custom mode editor on that tab (stored +1; 0 = off)
    int debugInfo = 0;             // --info <tab>: open the info screen on that tab (stored +1; 0 = off)
    std::string chatShot;       // --chatshot TEXT: in game, chat open with TEXT typed and the caret in the middle
    std::string consoleShot;    // --consoleshot TEXT: in game, the console open with TEXT typed, then Tab pressed
    bool edFocus = false;       // --edfocus: the editor's map title field focused (caret screenshot)
    int debugFonts = 0;         // --fonts: open Settings > FONT in the main menu (1); --settingsmenu: the settings page (2)
    bool menuShot = false;      // --menushot: screenshot whatever menu is up (main / settings / controls)
    double bench = 0;           // --bench SECONDS: measure frame times in a vs-bots game, print, quit
    bool benchmark = false;     // --benchmark [DEMO] [SECONDS]: the main menu's BENCHMARK (benchmark.cpp), printed, then quit
    std::string benchmarkDemo;  // ...on this demo instead of the shipped one
    double benchmarkSeconds = 0;   // ...measuring at most this long (0: to the demo's end)
    bool uiTest = false;        // --uitest [report.json]: click every widget and check it does what it says (uitest.cpp)
    std::string uiTestReport;   // where the report goes (default uitest-report.json)
    std::string uiTestOnly;     // --uitest-only a,b: just these scenarios (development)
    std::string uiTestPath;     // --uitest-path p: just the screens under p (development)
};

class UiTest;
// --uitest: before Game::init (an empty data folder, the network off, the test's settings), then the whole run.
void uiTestPrepare(LaunchOptions& opts);
int runUiTest(class Game& game, GLFWwindow* win, const LaunchOptions& opts);

class Game {
public:
    bool init(GLFWwindow* win, const LaunchOptions& opts, std::string* err);
    void shutdown();
    void frame(double dt);
    bool quitRequested() const { return quit_; }
    int maxFps() const {   // (VIDEO > MORE: WHEN UNFOCUSED caps it while the window is in the background or minimized)
        int cap = cfg_.maxFps;
        if (cfg_.vrr && vrrHz_ > 3) cap = cap > 0 ? std::min(cap, vrrHz_ - 3) : vrrHz_ - 3;   // (VRR: under the refresh, never at it)
        if (drawPaused()) return cfg_.bgFps > 0 ? cfg_.bgFps : 60;   // (nothing drawn: the loop only runs the game and network)
        if (bgCap_ && cfg_.bgFps > 0) return cap > 0 ? std::min(cap, cfg_.bgFps) : cfg_.bgFps;
        return cap;
    }
    // alt-tabbed or minimized with BACKGROUND DRAWING off: no frame is drawn or swapped; everything else runs
    // (never in automated runs: a headless window is never focused, and screenshots, benches and the UI test must draw)
    bool drawPaused() const { return bgCap_ && !cfg_.bgDraw && opts_.screenshot.empty() && !opts_.benchmark && opts_.bench <= 0 && !opts_.menuShot && opts_.backdrop.empty(); }

    // GLFW callbacks
    void onChar(unsigned cp);
    void onKey(int key, int action, int mods);
    void onMouseButton(int button, int action);
    void onFocus(bool focused);
    void onIconify(bool iconified);
    void onWindowPos(int x, int y);    // windowed: remembered for the next launch (VIDEO > MORE)
    void onWindowSize(int w, int h);   // (the click that brings the window back is the window's, not a shot)
    void onCursor(double x, double y);
    void onResize(int w, int h);
    void onScroll(double dy);

    // Frame pacing hooks called by the main loop around the buffer swap.
    void beforeInput();   // frame limiter sleep happens before this, so input is sampled as late as possible
    void preInput();      // after beforeInput, before the input is read: the network (snapshots, reconcile)
    void beforeSwap();    // latent sync: wait for the GPU to finish the frame, then for the vertical blank
    void afterSwap();     // low-latency fence waits + F3 input-latency measurement

private:
    // ---- the UI test (quark --uitest, uitest.cpp). In normal play none of this runs: uiTest_ is null, Ui::probe is
    // null, testHeld_ is 0, and the widgets' UiExp descriptors are a few pointers each.
    friend class UiTest;
    UiTest* uiTest_ = nullptr;
    uint64_t testHeld_ = 0;                 // actions the UI test holds down (bit per Action), read by isDown
    int edOpens_ = 0;                       // openEditor() calls (NEW / OPEN reopened the editor)
    std::string uiScreenPath() const;       // where the UI is: "main", "host/picker:maps", "ingame/pause/settings/hud"...
    std::string uiContext() const;          // what else tells this screen's widgets apart (the editor's selection)
    std::string uiObserve(const std::string& what);   // a STATE observable's current value
    void uiTestActions();                   // this screen's keys / clicks that aren't widgets (the 3D view, hotkeys)
    void uiTestHandles();                   // the editor selection's handles, while the 3D view's camera is set
    const char* edPath() const { return edOnline_ ? "editor-online" : "editor"; }
    const char* edExportAskPath() const { return edOnline_ ? "editor-online/export-ask" : "editor/export-ask"; }
    const char* edMatsPath() const { return edOnline_ ? "editor-online/materials" : "editor/materials"; }
    const char* edGroupsPath() const { return edOnline_ ? "editor-online/groups" : "editor/groups"; }
    const char* edKeepPath() const { return edOnline_ ? "editor-online/keep-edits" : "editor/keep-edits"; }
    bool edGroupTakesEveryBox() const;      // GROUP would leave the map without a box of its own (refused)
    enum class Screen { Main, Join, Host, Settings, Connecting, InGame, VsBots, Editor, Leaderboard, CustomMode, Info, Demos, Benchmarks, BenchmarkResult };
    void setScreen(Screen s);
    void startHost(bool vsBots = false);
    void stopHost();
    void connectTo(const std::string& addr);
    void leaveGame(const std::string& error = "");
    void loadMapIfNeeded();
    // PERF-3: a joined map's lighting bakes on a worker thread from a private copy of the map, while frame() keeps
    // polling the network and window events and draws a loading bar; the world is swapped in (uploaded) when it's done.
    struct BakeJob {
        std::thread th;
        Map map;                           // (the worker's own copy: the connection's map can change meanwhile)
        Renderer::PreparedWorld out;
        std::atomic<float> progress{0};
        std::atomic<bool> done{false}, cancel{false};
        uint32_t gen = 0;                  // net_.mapGeneration() it was started for
    };
    std::unique_ptr<BakeJob> bake_;
    void stopBake();                       // cancel a running bake and wait for its worker (leaving, shutdown)
    void drawBakeScreen(const std::string& name, float p);   // the loading bar (no flush / swap)
    void updateInput(double dt);
    UserCmd buildCmd();
    void handleEvents();
    void drawWorld(float alpha);
    void drawStuckProps(const Vec3& eye, float alpha);   // knives / bolts / syringes stuck in walls and players (EV_PROP)
    // net_.interpolatedPlayers() for this frame, worked out once (PERF-12: world, HUD, spectating and the editor each
    // asked for it): valid from the first use after the frame's ticks until the next frame or tick loop
    NetClient::PlayerArray& framePlayers() {   // (--models adds its lineup to it)
        if (!framePlayersOk_) { net_.interpolatedPlayers(framePlayers_); framePlayersOk_ = true; }
        return framePlayers_;
    }
    NetClient::PlayerArray framePlayers_{};
    bool framePlayersOk_ = false;
    void drawHud();
    void drawRps(float W, float H, float s, float top);   // the hide and seek tiebreak (rock paper scissors) panel, from y top
    void drawRpsShape(float x, float y, float size, int shape, Color c);
    bool rpsPicking() const;                   // we're in the tiebreak and the throw is open
    void drawTaggerMarker(float W, float H, float s);   // tag: where the tagger is, through walls (at the screen edge when off it)
    void drawNotices(float y, float ts, float s); // notify() lines, centred from y down (HUD and menus)
    void drawScoreboard();
    void drawScoreboardAt();   // (drawScoreboard: this, moved / sized by HUD > SCOREBOARD)
    // where the scoreboard goes this frame (its panel; under the notice list) and its rows' text scale / height
    struct ScoreboardBox { float x, y, w, h, ts, rowH; };
    ScoreboardBox scoreboardBox(size_t players) const;
    // the in-game list under the score bar (objective, banners, ranked notes, a running vote, host info): where it
    // ended this frame, in screen px (the scoreboard and the pause menu's panels go under it)
    float noticeEnd_ = 0;
    void drawMenu();
    // the menus' pages (the redesign's 720p coordinates through a SetLayout: game.cpp "Menus")
    SetLayout menuLayout(float designW) const;
    // in game: the pause menu's pages between the notice list and the weapon bar (y 148..638 at 720p)
    SetLayout ingameLayout(float designW) const;
    float ingameTop(const SetLayout& L, float kH) const;   // where content kH design px tall starts there (its panel centred)
    void drawMainMenu(const SetLayout& L);
    void drawJoin(const SetLayout& L);
    void drawHost(const SetLayout& L);
    void drawHostAdvanced(const SetLayout& L);
    void drawVsBots(const SetLayout& L);
    void drawConnecting(const SetLayout& L);
    void debugMenuPage();     // --menupage PATH: that menu page open (screenshots)
    bool hostAdvanced_ = false;   // HOST > ADVANCED (UDP port, port forward, server log, bot brain) open
    std::vector<std::string> hostBrains_;   // ADVANCED's BOT BRAIN choices after BUILT-IN: bots/*.qbrain, listed when HOST opens
    std::string hostBrainNote_;             // the chosen brain's problem, short ("" none): it doesn't compile
    void refreshHostBrains();               // list the brains again (a file gone: back to the built-in one) and check the choice
    void checkHostBrain();                  // compile the chosen brain (loadBrain: cached while the file is unchanged)
    void drawPauseMenu(float cx, float y);
    // Settings: one shell, six tabs (drawSettings: the menus and the pause menu; game.cpp "Settings")
    enum { TAB_PLAYER, TAB_GAMEPLAY, TAB_CONTROLS, TAB_VIDEO, TAB_CROSSHAIR, TAB_HUD, TAB_SEARCH };   // (SEARCH: every setting in one list)
    int setTab_ = TAB_PLAYER;
    int controlsSub_ = 0;    // CONTROLS: 0 general + interface keys, 1 move + weapon keys
    void drawSettings();
    void openSettingsTab(int tab);
    void leaveSettings();
    void debugSettingsTab();   // --settingstab
    void drawPlayerSettings(const SetLayout& L);
    void drawGameplaySettings(const SetLayout& L);
    void drawVideoSettings(const SetLayout& L);
    void drawImportDialog(const SetLayout& L);
    void captureMouse(bool on);
    Vec3 teamColor(int playerId) const;
    Vec3 outlineColor(int playerId) const; // the player outline (settings: friendly / enemy presets)
    bool isFriendly(int playerId) const;
    bool nameColorOf(int playerId, Color& out) const;   // the player's chosen name colour (lifted to stay readable); false: none
    void notify(const std::string& s, float seconds = 4.0f);
    void updateAnnouncer();
    ModeState annPrev_;
    bool annPrevValid_ = false;
    int annCount_ = 0;
    int annLeader_ = -2;   // FFA: you lead alone (1) or not (0, -1: not counted yet); -2 not counting (X10)

    GLFWwindow* win_ = nullptr;
    Settings cfg_;
    LaunchOptions opts_;
    Renderer r_;
    // models (models.h): the box list being drawn (reused every frame), and the owner's glTF overrides (null: none)
    ModelParts parts_;
    ModelOverrides modelOv_;
    bool fairModels_ = false;   // this frame: ranked / time trial (rankedPlayer), only the shipped overrides are drawn
    // the override drawn for a model: a mesh (drawn as triangles), or a box model (its boxes go through the box path
    // in place of the built-in ones: overrideBoxParts); null: none of that kind
    const ModelMesh* modelOverride(int kind, int i) const { const ModelMesh* m = modelOv_.get(kind, i, fairModels_); return m && m->boxes.empty() ? m : nullptr; }
    const ModelParts* boxOverride(int kind, int i) const { const ModelMesh* m = modelOv_.get(kind, i, fairModels_); return m && !m->boxes.empty() ? &m->boxes : nullptr; }
    void overrideTints(int kind, int idx, const ModelPalette& pal, Vec3* tint) const;   // tint[TINT_COUNT]
    // a prop (models.h) at `origin`: its override turned by yaw, or its boxes at origin + right * x + y + back * z
    // (model space), each box turned by boxYaw
    void drawProp(int prop, const ModelPalette& pal, const Vec3& origin, float yaw, const Vec3& right, const Vec3& back, float boxYaw, float sv);
    // an arm prop (GUN_ARM / ARM / HOOK_ARM) from the shoulder, along the aim, at body scale bs (pal: its slots' colours)
    void drawArm(int prop, const Vec3& shoulder, float yaw, float pitch, float bs, float sv, const ModelPalette& pal);
    // the grapple's claw (PROP_HOOK) at pivot + (yaw, pitch turned) offset, prongs along the turned -Z, at scale bs
    void drawHookClaw(const Vec3& pivot, const Vec3& offset, float yaw, float pitch, float bs, float sv);
    void loadAssetOverrides();
    Ui ui_;
    UiInput uiIn_;
    NetClient net_;
    ServerBrowser browser_;
    Screen screen_ = Screen::Main;
    bool quit_ = false;
    double benchStart_ = -1;
    std::vector<float> benchTimes_, benchCpu_;
    std::vector<float> perfScratch_ = std::vector<float>(2048); // F3 percentile workspace
    double lastCpu_ = 0;
    double modelSentAt_ = -10;
    int socdLastF_ = 0, socdLastS_ = 0;     // control scripts: SOCD press order, alt-fire toggle state
    bool socdPrev_[4] = {}, altPrev_ = false, altToggled_ = false;
    int altWeapon_ = -1;   // the weapon alt-fire was pressed with (-1 up; -2 held over a switch: ignored till let go)
    void benchFrame(double dt);
    // ---- BENCHMARK (main menu; --benchmark): a fixed bot match on Hamlet watched from a fixed camera path, then its
    // frame times (benchmark.cpp; the match and the maths: src/game/benchmark.h)
    struct BenchRun {
        enum Phase { OFF, STARTING, WARMUP, MEASURING } phase = OFF;
        std::string demo;            // the demo it plays
        double seconds = 0;          // measured at most (0: to the demo's end)
        double startedAt = 0;        // (time_) the demo's world first drawn: the warm-up's start
        double measureAt = 0;        // (time_) measuring from
        std::vector<float> times;    // the measured frames' times (s): allocated at the start, never grown during the run
        size_t n = 0;
    } bench_;
    struct BenchFile {               // a saved run (benchmarks/<date>.qbench): its "# key,value" header and its frames
        std::string path, name, label, when, map, demo;
        double avgFps = 0, low1 = 0, low01 = 0, minFps = 0, stdevMs = 0, avgMs = 0, worstMs = 0, seconds = 0;
        size_t frames = 0;
        int uploaded = 0;            // to the ranking server: 0 not yet (retried), 1 done, 2 refused there (not retried)
        std::vector<std::pair<std::string, std::string>> setup;   // RESOLUTION, RENDER SCALE, ... GPU, DRIVER, VERSION
        std::vector<float> times;                                 // (s; only when loaded with its frames)
    };
    struct BenchResult {             // the run the result page shows, and the one saved before it (to compare)
        BenchFile run, prev;
        bool havePrev = false, nameDirty = false;
        float topMs = 20;
        std::vector<float> colMax, colMean;   // the graph (bench::graphColumns)
    } benchRes_;
    std::vector<BenchFile> benchList_;   // the BENCHMARKS page's runs, newest first (headers only)
    double benchListAt_ = 0;             // (rescanned every 2 s while it's open, as the demos list)
    int benchScroll_ = 0;
    std::set<std::string> benchSending_;  // runs whose upload is on its way (RankService::request key "bench:<path>")
    void benchUpload(const BenchFile& f);   // a finished run's numbers to the ranking server (POST /bench; no key: none)
    void benchUploads();                    // each frame while any is on its way: the answers into the run files
    void benchRetryUploads();               // the runs not uploaded yet (at the start, and when BENCHMARKS opens)
    std::string benchDemoFound_;         // (benchDemoPath, looked for again with the list)
    std::string benchShot_;              // EXPORT PNG: the result page into this file at the end of the frame
    void benchStart();               // host the benchmark's match and join it as the camera
    void benchFrameTime(double dt);  // each frame: the warm-up, then the frame's time into the buffer
    std::string benchDemoPath() const;   // the demo a run plays ("" if it's missing)
    void benchFinish();              // the run saved, then its result page (or, with --benchmark, printed and quit)
    void benchCancel();              // Esc: back to the main menu, nothing saved
    std::string benchSave(const float* t, size_t n);   // a run's file from its frame times (s): its path ("" if not written)
    bool benchWrite(const BenchFile& f);               // (the whole file: a new run, or a rename)
    bool benchOpen(const std::string& path);           // the result page for a saved run
    static bool benchLoad(const std::string& path, BenchFile& f, bool frames);   // frames: read them too, else the header only
    void benchDelete();
    void benchExportCsv();
    void drawBenchOverlay(float W, float H, float s);  // BENCHMARK and the time left (the HUD is off)
    void drawBenchList(const SetLayout& L);
    void drawBenchResults(const SetLayout& L);
    bool paused_ = false, chatting_ = false, showScores_ = false, settingsInGame_ = false;
    bool unratedServer_ = false;   // the server said its ranked isn't rated (kUnratedNotice: a testing server)
    std::string chatText_, joinAddr_, error_;
    size_t chatCursor_ = 0;      // caret in chatText_ (chat / console input)
    std::string chatSeen_;       // chatText_ as last edited: any other change (clear, history, completion) puts the caret at the end
    double time_ = 0, tickAcc_ = 0;
    uint32_t loadedMapGen_ = 0;

    // hosting
    std::unique_ptr<Server> host_;
    std::thread hostThread_;
    std::atomic<bool> hostQuit_{false};
    std::string hostPortStr_;
    int hostMapIdx_ = 0, hostModeIdx_ = 0;
    uint16_t localPrevButtons_ = 0;   // the last command's buttons (a held burst: the spread sequence, NET-9)
    uint32_t localBurst_ = 0;          // this shot's place in the held burst (mirrors the server's ServerPlayer::burstShot)
    int localBurstWeapon_ = -1;
    // custom modes: the one being edited, its tab, and the rules arriving from a server in pieces
    CustomRules editRules_;
    std::string editOrigName_;
    int customTab_ = 0;
    std::vector<std::string> rulesParts_;
    void drawCustomModeEditor(const SetLayout& L);
    void drawInfo(const SetLayout& L);
    void drawDemos(const SetLayout& L);     // the demos folder: newest first, click to watch
    void startRecording();
    bool playDemo(const std::string& path);
    std::vector<std::string> demoFiles_;
    std::vector<std::string> demoNotes_;   // per demo: "" (plays), or which version recorded it / converting
    std::vector<uintmax_t> demoSizes_;     // ...its size in bytes
    std::string demoDelArmed_; double demoDelUntil_ = 0;   // DELETE: the demo armed by a first click (a second deletes it)
    std::string demoRename_, demoRenameText_;   // RENAME: the demo being renamed, and the name typed so far
    void applySettings();                  // after many settings changed at once (console, IMPORT, RESET ALL): the ones not read every frame
    std::string allMsg_; double allMsgUntil_ = 0, resetAllArmed_ = 0;   // GAMEPLAY > MORE: ALL SETTINGS' answer; RESET ALL armed
    void pruneDemos();                     // KEEP: delete all but the newest cfg_.demoKeep
    double demoListAt_ = -1;
    int infoTab_ = 0;
    int infoScroll_ = 0;                    // the info page's first line shown (mouse wheel: 3 lines a notch)
    int demoScroll_ = 0, joinScroll_ = 0;   // first row shown in the demo list / server list (mouse wheel)
    std::vector<std::string> maps_;
    struct MapInfo { std::string title; bool legacy = false; int preferredMode = -1; uint32_t modes = 0; uint16_t ranked = 0; std::vector<uint8_t> preview; }; // ranked: ladders it's a ranked map for // modes: bit per supported mode // preview: the picker's image (kPreviewW x kPreviewH RGBA, rows bottom-up)
    static constexpr int kPreviewW = 192, kPreviewH = 108;
    int pickScroll_ = 0;        // the map picker's first row shown (the wheel)
    std::string previewSig_;    // (the maps the renderer's previews are of: names, file times and sizes)
    std::vector<MapInfo> mapInfo_;
    enum { PICK_NONE = 0, PICK_HOST_MAP, PICK_HOST_MODE, PICK_BOT_MAP, PICK_BOT_MODE, PICK_LIVE_MAP, PICK_LIVE_MODE, PICK_VARIANT, PICK_TMAPS, PICK_TMODES, PICK_RANKED };
    int variantCtx_ = 0, liveVariant_ = 0; // ruleset step: 0 host, 1 vs bots, 2 in game
    std::vector<bool> multiSel_;
    int liveMode_ = 0, liveMap_ = 0;  // in-game map/mode change (host) or vote (anyone)
    bool livePickVote_ = false;
    void openLivePicker(int kind, bool vote);
    bool voteMenu_ = false;
    bool specPick_ = false;     // the vote menu's player list: who to move to spectator (callvote spec)
    void drawVoteMenu(float cx, float y);
    int picker_ = PICK_NONE;    // full list of maps / modes open over the host or vs-bots menu
    void drawPicker(float cx, float y);
    // Menu backdrop: a map loaded at startup, orbited slowly behind the menus.
    Map menuMap_;
    Vec3 bgCenter_;
    float bgRadius_ = 30;
    float bgTop_ = 0;
    void setBackdropFrame(const Map& m);
    bool bgIndoor_ = false;
    float bgCeil_ = 0, bgHalfX_ = 0, bgHalfZ_ = 0;
    void loadMenuBackdrop();
    void rescanMaps();          // re-list maps/ so new or edited files show up without restarting
    // rescanMaps' per-file cache (PERF-11): a map is only loaded again when its file's path, size or mtime changed
    struct MapInfoCached { long long mtime = 0; uintmax_t size = 0; MapInfo info; };
    std::map<std::string, MapInfoCached> mapInfoCache_;
    std::string testMap_;       // set while play-testing a map from the editor
    int testMode_ = 0;

    // ranked: this computer's key, reports + rating lookups (background), the rating before the last report
    RankKey rankKey_;
    RankService rank_;
    double nextRankFetch_ = 0;
    int rankBefore_ = -1, rankGamesBefore_ = -1;
    bool helloPending_ = false;   // POST /hello in flight (retried in the background) until it took: then recorded in the config
    void updateRanked();
    double nextBoardFetch_ = 0;
    void drawLeaderboard(const SetLayout& L);
    std::vector<std::string> rankIdsInGame() const;
    // map editor (editor.cpp). Every change is an edit op (applyMapEdit): applied locally when editing
    // alone, sent through the server in an edit session so everyone's copy stays the same.
    enum { ED_BRUSH, ED_SPAWN, ED_LIGHT, ED_SITE, ED_HILL, ED_CHECK, ED_PATH, ED_LADDER, ED_GROUP };
    struct EdSel { int kind = -1, idx = -1; bool valid() const { return kind >= 0; } };
    Map ed_;
    std::string edName_ = "mymap", edMsg_, edTitle_;
    double edResetArmed_ = 0, edMsgUntil_ = 0, edLastPush_ = -1e9, edLeaveArmed_ = 0, edNextCam_ = 0, edTickAcc_ = 0;
    EdSel edSel_;
    std::vector<EdSel> edMulti_; // shift+click: more selected boxes / groups (for GROUP)
    int edGridIdx_ = 2, edLoadIdx_ = 0, edMat_ = 0; // edMat_: the material new boxes / rooms / stairs get (the last one picked)
    // the material picker (owner, 2026-10-29: "a pick list with images ... a seperate list for materials already in the
    // map, and the last 10 used surfaces"): 0 closed, 1 for new boxes (edMat_), 2 for what's selected
    int edMatMenu_ = 0;
    std::vector<int> edRecentMats_;   // (newest first, at most 10: edUseMaterial)
    int edMatTab_ = 2, edMatScroll_ = 0;   // the picker's tab (BASIC first) and its first row shown (the wheel)
    std::string edMatSearch_;             // the picker's SEARCH
    void drawEditorMatMenu();
    void edUseMaterial(int m);   // a material picked or cycled: the recent list
    void edSetMaterial(int m);   // ...on everything selected (boxes, rooms / stairs), one undo step
    bool edGroupMenu_ = false, edExportAsk_ = false; // IMPORT GROUP's list of saved groups; EXPORT's "make it a group first?"
    std::vector<std::string> edGroupFiles_;
    float edSpeed_ = 12;
    Vec3 edTestCam_;                // TEST PLAY: START AT CAMERA: where the camera was
    float edTestYaw_ = 0, edTestPitch_ = 0;
    float edGrid() const;
    std::string edLastLight_;       // the light values last set this session (cfg's edLight on close)           // the grid step (the CUSTOM GRID after the built-in steps)
    Vec3 edCam_;
    bool edLook_ = false, edDirty_ = false, edBaked_ = false;
    bool edMeshDirty_ = true;      // the map's boxes changed: rebuild the renderer's static batch (PERF-12)
    float edMeshOutline_ = -1;     // the outline width that batch was built with
    int edFullW_ = 1280;   // the window width (the editor's 3D view is narrower: the panel is on the right)
    bool edOnline_ = false;     // in an edit session (connected to a server in edit mode)
    bool edSession_ = false;    // this connection has been an edit session (leaving offers to save a copy)
    bool testEdit_ = false;     // hosting an edit session started from the editor
    uint32_t edRev_ = 0;        // net_.mapRevision() that ed_ was copied from
    std::vector<std::string> edUndo_; // inverse ops, newest last (edUndoFwd_: each one's op)
    // dragging a handle of the selection: a face (resize) or, with Shift / on points, the whole thing (move)
    struct EdHandle { Vec3 pos; int axis, sign; bool move; };
    std::vector<EdHandle> edHandles() const;
    int edDragAxis_ = -1, edDragSign_ = 0;
    bool edDragMove_ = false, edDragPushed_ = false;
    Vec3 edDragP0_;
    float edDragT0_ = 0;
    Map edDragBase_;
    std::string edDragLast_;
    float edDragDelta_ = 0, edDragGrid_ = 0;   // the drag's step and grid last worked out (the same again: nothing to do)
    EdSel edDragSel_;                          // the object edDragBaseOp_ is of
    std::string edDragBaseOp_;                 // its "set" op on edDragBase_ (what undo puts back)
    std::vector<EdHandle> edHandleBuf_;        // (editorFrame: the selection's handles, kept from frame to frame)
    std::vector<std::pair<Vec3, std::string>> edOthers_;   // (editorFrame: the other editors' labels)
    uint32_t edGen_ = 0, edCacheGen_ = ~0u;    // edGen_ moves on with every change to ed_; the next four were worked out at edCacheGen_
    std::vector<AABB> edClipBoxes_;            // (the clip boxes, drawn see-through)
    int edClipCount_ = 0;                      // (the editable ones: FIND CLIP)
    uint32_t edCanModes_ = 0;                  // (the modes the map has what it needs for)
    std::string edModesText_;                  // (PLAYABLE AS)
    // The save question (OWNER_DECISIONS_2 B3): "keep your edits as a new map?" when you save or leave a built-in map
    // you edited (its file is never written: yes saves your edits under a new name in your maps folder, no drops
    // them), and "save your copy?" after leaving someone else's edit session. Nothing is ever saved without asking.
    bool edPrompt_ = false;
    Map edPromptMap_;           // what yes saves
    std::string edPromptName_;  // under this name (a new one when the map is built in)
    enum { ED_THEN_NONE, ED_THEN_SAVE, ED_THEN_LEAVE, ED_THEN_TEST, ED_THEN_HOST, ED_THEN_OPEN };
    int edPromptThen_ = ED_THEN_NONE;   // what the editor does after the answer (NONE: asked after a session)
    std::string edPromptOpen_;          // ED_THEN_OPEN: the map to open next ("" = a new one)
    bool edPromptOfficial_ = false;     // the edits are to a built-in map
    bool edOfficial() const { return isOfficialMapName(edName_) || isOfficialMapName(ed_.name); }   // editing a built-in map
    void edAskKeep(int then, const std::string& open = std::string());
    void edPromptAnswer(int answer);    // 1 yes, 0 no, -1 cancel
    std::string edFreshName(const std::string& base) const;   // a name for your copy that's free and not a built-in map's
    void openEditor(std::string map); // (by value: callers pass maps_[i], and opening rescans maps_)
    std::vector<std::string> edScript_; // --edscript steps still to run (one per frame)
    void edScriptStep();
    void enterOnlineEditor();
    void closeEditor();
    void editorFrame(double dt);
    void editorKey(int key, int mods);
    bool edDo(const std::string& op, const std::string& inverse, bool coalesce = false, bool undoable = true);
    void edUndoLast();
    void edRedoLast();   // Ctrl+Y / Ctrl+Shift+Z: the last undone step again
    std::vector<std::string> edUndoFwd_;   // each undo step's own op (what redo does again), beside edUndo_
    std::vector<std::pair<std::string, std::string>> edRedo_;   // undone steps (op, inverse), newest last; a new edit clears it
    bool edRedoing_ = false;
    double edAutoAt_ = 0;    // EDITOR AUTOSAVE: the next one (a changed map every 60 s, to editor-autosave.qmap)
    bool edHasAutosave_ = false;   // ...one is there to RESTORE (from a crash, or leaving without saving)
    void edAutosave();
    void edRestoreAutosave();
    void edChanged();
    void edNotify(const std::string& s);
    bool edSelValid() const;
    AABB edBounds(const Map& m, EdSel sel) const;
    void edModify(const std::function<void(Map&)>& f, bool coalesce);
    void edDelete();
    void edDuplicate();
    void edFix(bool gaps);   // FIX MAP (merge redundant boxes, drop hidden ones) / FIX GAPS (close gaps under 1/8 m): one undo step
    void edCoat();   // K: a 1/8 m layer on the side of the selected box(es) you look at
    static constexpr float kCoatThick = 0.125f;
    enum { ED_ROT_Y, ED_ROT_X, ED_ROT_Z, ED_MIR_X, ED_MIR_Y, ED_MIR_Z };
    void edTransform(int op);
    bool edTransformed(int op, Map& tmp, std::vector<EdSel>& sel) const;   // the map with the selection turned (false: nothing to turn)
    UiExp edTransformExp(int op);   // ROTATE / MIRROR's expectation: the map changes, or (it's symmetric that way) just the notice
    enum { ED_SPLIT_X, ED_SPLIT_Y, ED_SPLIT_Z, ED_COMBINE, ED_SUBTRACT };
    bool edBoxOpPlan(int op, std::vector<int>& gone, std::vector<Brush>& made, std::string& why) const;   // SPLIT / COMBINE / SUBTRACT on the selected boxes
    UiExp edBoxOpExp(int op) const;
    int edSplitOp() const;
    void edBoxOp(int op);
    void edAdd(int kind, int variant);
    void edGroupSelection();            // GROUP: the selected boxes / groups become one custom group
    void edUngroup();                   // UNGROUP: the selected group's boxes become plain boxes
    bool edPlaceGroup(const std::string& text); // a "quark-group 1" text, added at the screen center
    void edImportGroup();               // clipboard first (saved to groups/), else the groups menu
    void edExportGroup();               // the selected group to groups/<new file> + clipboard, then open the folder
    void drawEditorGroupMenu();         // IMPORT's list / EXPORT's question: the editor's dialog template (drawEditorSavePrompt)
    std::vector<EdSel> edPickAll(const Vec3& o, const Vec3& dir) const;   // everything under the ray, nearest first
    float edLastClickX_ = -1e9f, edLastClickY_ = -1e9f, edLastClickYaw_ = 0, edLastClickPitch_ = 0;   // (clicking the same spot again cycles)
    Vec3 edLastClickCam_;
    int edClipFind_ = -1;   // FIND CLIP: the clip box shown last
    Vec3 edMouseRay() const;
    bool edSave(int then = ED_THEN_SAVE);   // false: not saved (or the question is up: `then` follows the answer)
    bool edSaveCopy(const Map& m, const std::string& name);
    void edTest();
    void edHostSession();
    void drawEditorSavePrompt();
    void drawBakeProgress(const std::string& name, float p);

    // view / input
    float yaw_ = 0, pitch_ = 0;
    double lastMx_ = 0, lastMy_ = 0;
    bool mouseCaptured_ = false, firstMouse_ = true;
    bool fireLatched_ = false;
    double dashHoldUntil_ = -1;   // a dash press holds BTN_DASH till then, or till its dash starts (pmove dashes while it's held)
    static constexpr double kDashPressBuffer = 0.1;
    uint16_t moveLatched_ = 0;   // BTN_JUMP / DASH / GRAPPLE pressed since the last command (a tap shorter than a tick still counts)
    bool localFireHeld_ = false, scopeOverlay_ = false, localFireLocked_ = false;
    double localFireQueued_ = -1;
    int localTapWeapon_ = -1;   // the weapon localFireQueued_'s click was made with (a switch drops it)
    double nextLaserSound_ = 0;
    double knifeSlash_[kMaxPlayers] = {}; // when each player last swung the knife (slash trail)
    void drawKnifeSlash(const Vec3& eye, float yaw, float pitch, float t, const Vec3& color, Vec3 from);   // from: the knife (zero: a spot by the eye)
    // a player's weapon as others see it (hide and seek / tag: the seeker's / tagger's knife or locator): an arm from
    // the shoulder, the weapon's own model in the hand, aimed where they look
    void drawHeldWeapon(int player, int weapon, const Vec3& shoulder, float yaw, float pitch, float bodyScale, const Vec3& accent, float sunVis);
    double wheelAccum_ = 0;         // fractional scroll (trackpads, high-res wheels) until a full notch
    double wheelHeldUntil_[2] = {}; // a scroll step counts as "held" briefly, so hold actions (jump, fire...) work on the wheel
    int specTarget_ = -1;
    int specSent_ = -2;         // the spectate target last told to the server (-2: none since alive)
    double killCamUntil_ = 0;   // team modes: the killer can be watched until then (kKillCamTime), then teammates only
    // the death screen (X1): who killed you with what, their health then (-1: not shown, ranked or an old server), and
    // when you respawn (-1: not known / not this round)
    int deathKiller_ = -1, deathWeapon_ = -1, deathKillerHp_ = -1;
    double deathRespawnAt_ = -1;
    bool deathValid_ = false;

    // effects + feeds
    struct Span { size_t at, len; Color color; };   // part of a feed line in its own colour (a player's chosen name colour)
    struct Feed { std::string text; Color color; double until; int crown = 0; bool player = false; std::vector<Span> spans{};   // crown: the sender's leaderboard place (chat); player: a player's chat line (hidden while chat is off)
                  mutable float w = 0, wts = -1; mutable int wgen = -1; };   // (its text's width at text size wts: measured once, not every frame)
    float feedWidth(const Feed& f, float ts) const {
        if (f.wts != ts || f.wgen != r_.fontGeneration()) { f.w = r_.textWidth(f.text, ts); f.wts = ts; f.wgen = r_.fontGeneration(); }
        return f.w;
    }
    // a short HUD text's width, measured again only when the text, its size or the font changes (longer: every time)
    struct HudWidth { char t[32] = {}; float ts = -1, w = 0; int gen = -1; };
    float hudWidth(HudWidth& c, const char* t, float ts) const {
        if (c.ts == ts && c.gen == r_.fontGeneration() && std::strcmp(c.t, t) == 0) return c.w;
        const float w = r_.textWidth(t, ts);
        if (std::strlen(t) < sizeof c.t) { std::strcpy(c.t, t); c.ts = ts; c.gen = r_.fontGeneration(); c.w = w; }
        return w;
    }
    std::deque<Feed> killFeed_, chatFeed_, notices_;
    void drawFeedText(float x, float y, float ts, const Feed& f);
    std::deque<Feed> chatLog_;   // the chat history shown while typing (Enter)
    void addChat(const Feed& f) { chatFeed_.push_back(f); chatLog_.push_back(f); while ((int)chatFeed_.size() > cfg_.chatLines) chatFeed_.pop_front(); while (chatLog_.size() > 30) chatLog_.pop_front(); }
    std::string centerText_;
    double killMarkUntil_ = 0;   // your kill: a bigger red X on the crosshair until then (X6)
    // batch 3 (GAMEPLAY > MORE 2): KILL TEXT; HEAL NUMBERS / LOW HEALTH / auto-hide health (trackHealth)
    double killTextUntil_ = 0, healUntil_ = 0, hpChangedAt_ = -10, slotChangedAt_ = -10;
    std::string killTextName_;
    int healAmt_ = 0, prevHp_ = -1, prevSlotWeapon_ = -1;
    void trackHealth();
    void trackAlerts();
    bool jumpUsed_ = false, jumpGround_ = false;   // AUTO-HOP off
    bool fireSwapped() const;
    bool altKey() const;
    int altToggleMask() const;
    bool altToggleOn() const;
    void applyRawInput();
    void consoleKeyPress(int key);
    void applyMenuTheme();
    bool queuesRestored_ = false;
    void saveQueues();
    bool quipBlocked(const std::string& name) const;
    bool muted_ = false;
    float modeSens_ = 0, modeFov_ = 0, modeZoom_[2] = {0, 0};   // a mode's controls profile's own aim (0: yours)
    float baseFov() const { return hfov(); }   // the MUTE key (the master volume at 0 until pressed again or the volume changes)
    bool deathBody_ = false;
    bool joinHandled_ = false;
    int drainBeep_ = -1;
    bool kickPick_ = false;   // the vote menu's player list: KICK (else MOVE TO SPECTATOR)   // STALEMATE WARNING: the second last beeped   // ON JOIN done for this connection   // DEATH CAMERA: YOUR BODY (till you respawn or pick someone to watch)
    int alertPhase_ = -1, alertPlayers_ = 0;
    bool alertVote_ = false;
    std::string chatStamp() const;
    bool mentionsMe(const std::string& text) const;
    std::string filterChat(std::string t) const;
    bool sysLineShown(const std::string& t) const;
    mutable int clockRound_ = -1;
    mutable float clockLen_ = 0;
    mutable int raceRunRound_ = -1; mutable double raceRunStart_ = 0;   // race: when this round went live here (its run stopwatch)   // (TIME PLAYED: the longest time left seen this round = its length)
    std::string shownName(int id) const;   // (STREAMER MODE: other players as "PLAYER <n>")
    bool hitHead_ = false;   // (the last hit a headshot: the crosshair flashes HEADSHOT's colour)
    double centerUntil_ = 0, hitUntil_ = 0, damageUntil_ = 0, recoil_ = 0, localNextFire_ = 0;
    // the first-person grapple arm: how far it has turned toward the hook (camera space yaw / pitch, radians) and raised
    // (0..1), eased each frame; and each player's launcher muzzle as drawn this frame (their rope starts there)
    float hookAimYaw_ = -0.06f, hookAimPitch_ = 0, hookRaise_ = 0;
    Vec3 hookMuzzle_[kMaxPlayers];
    uint32_t hookMuzzleFrame_[kMaxPlayers] = {};
    uint32_t drawFrame_ = 0;
    PracticeDing practiceDing_;   // race practice's checkpoint dings (modecalls.h)
    int tagIt_ = -1;   // tag: who was "it" in the last mode state seen (a pass on: its sounds, updateAnnouncer)
    HurtSoundGate hurtGate_;   // your own hurt sound: one per 0.15 s, louder for more damage (clientfeel.h)
    HitSoundGate hitGate_;   // the hitsound: one per moment, the heaviest (audio.h)
    double localKnifeBack_ = 0, localSyringeBack_ = 0;   // predicted: a thrown knife / syringe is away (disabled) until then
    // seconds until a thrown knife / syringe is back (0 = have it): the prediction, or the server's word (snapshot)
    double weaponAwayLeft(int w) const {
        double local = w == WPN_KNIFE ? localKnifeBack_ : w == WPN_SYRINGE ? localSyringeBack_ : 0;
        int snap = !net_.hasSnapshot() ? 0 : w == WPN_KNIFE ? net_.latest().knifeAway : w == WPN_SYRINGE ? net_.latest().syringeAway : 0;
        return std::max(local - time_, snap * 0.1);
    }
    int lastHealth_ = 0;
    bool fireAlive_ = false;
    float stepCam_ = 0, stepFeetY_ = 0;   // X7: the camera's lag behind a stair step (m), and last frame's feet height
    bool stepGround_ = false;
    float localRefire_ = 0;    // the refire localNextFire_ counts down (the last shot's, alt-fire's own; the spawn lock's)
    float tickYaw_ = 0;        // the yaw the last tick sent (F18: the next frame's turn spreads from it)   // alive as of the last frame's ticks (a spawn starts the mirrored spawn fire lock)
    struct Tracer { Vec3 a, b; double until; Vec3 col = {1.0f, 0.95f, 0.7f}; };   // (col: TRACER COLOR, tracerColorFor)
    std::vector<Tracer> tracers_;
    struct Explosion { Vec3 at; double start; float size = 1; bool bubble = false; bool kill = false; bool spark = false; bool wall = false; float life = 0.35f; };   // kill: a red burst where someone died (X6); spark: a hitscan hit on a player (X4); wall: on a wall
    void localImpactSparks(int w, const UserCmd& c);
    bool shotSpark(const Vec3& from, const Vec3& d, float wallT, float range, int shooter);   // a remote hitscan ray's impact spark
    void drawMuzzleFlash(const Vec3& at, int w, const Vec3& beam, float a, float scale);
    double nextLocalSpark_ = 0;
    std::vector<Explosion> explosions_;
    // Glass shards and brick / cobblestone chunks (glass.h): drawn falling, then lying where they land, until out of view
    // (owner, 2026-10-28: "disappear from your screen once its no longer in your fov")
    struct VisShard { Vec3 pos, vel; float size; Vec3 color; bool rest, glass; double born; int thin = -1; bool dust = false; };   // thin: a glass plate's thin axis (flat on the floor once it lies); dust: glitter (no collision, gone in 1.5 s)
    std::vector<VisShard> visShards_;
    std::vector<NetClient::GlassFx> glassFxScratch_;
    std::vector<GlassShard> glassShardScratch_;
    double shardsAt_ = -1;
    uint32_t holeSerial_ = ~0u, holeMapRev_ = ~0u;
    void predictShotGlass(int w, const UserCmd& c);   // your own shot / knife breaks a pane at once (the server confirms)
    // Your hits, as your screen has them, are the ones that count (owner, 2026-10-28: "all damage should be client
    // side"): each shot's / blast's go to the server as one "hit" command (Server::clientHitReport). The laser's
    // 10 ms shots are added up and sent every 50 ms.
    struct HitReport { int victim; float dmg; bool head, direct; float splash, dist; Vec3 knock; };
    std::vector<HitReport> hitScratch_;
    float laserDmg_[kMaxPlayers] = {}, laserDist_[kMaxPlayers] = {};
    Vec3 laserKnock_[kMaxPlayers];
    bool laserHead_[kMaxPlayers] = {}, laserAlt_ = false, laserPending_ = false;
    double laserSince_ = 0;
    void sendHits(int weapon, bool alt, const std::vector<HitReport>& hits);
    void reportShotHits(int w, const UserCmd& c);
    void flushLaserHits(bool force);
    void reportBlast(int type, bool alt, const Vec3& at, int direct);
    void reportBolt(const Vec3& at, const Vec3& dir, int victim, bool head);
    void reportImpact(int type, bool alt, const Vec3& from, const Vec3& to, int victim, bool head);   // a rocket or bolt landed
    void updateDestruction(const Map& map);           // breaks to show, holes to send, shards to fly and draw
    std::vector<ProjectileSnap> lastProjectiles_;
    std::vector<ProjectileSnap> projScratch_;   // (frame: this frame's projectiles, built here then swapped in)
    std::vector<GameEvent> evScratch_;          // (handleEvents)
    // your own projectiles start drawn at your weapon model's muzzle and blend onto their real path
    struct ProjStart { uint16_t id; double seen; Vec3 offset; float lead = 0; bool boomed = false; };   // boomed: exploded on your screen already (localBoom)   // lead: X5, drawn this far ahead (s)
    // X5: your own rockets, bolts and thrown knives, flying from the click until the server's copy arrives (a round trip
    // later), which then takes over drawn as far ahead as the copy was (lead), so nothing jumps back
    struct PredProj { uint8_t type = 0; bool live = false, lob = false, started = false, atWall = false, boomed = false; Vec3 pos0, vel0, pos, offset; double born = 0; };
    PredProj predProj_[8];
    void spawnPredictedProjectile(int w, bool alt, const UserCmd& c);
    std::vector<ProjStart> projStarts_, projStartsKeep_;
    // Your projectiles' impacts shown at once, on your screen (rockets / bolts on a wall or an enemy, stickies on your
    // detonate): the server's own explosion of that one (its id; 0xFFFF: a predicted copy's, not handed over yet) is then
    // left out when it comes, if it's near. Damage and deaths stay the server's.
    struct LocalBoom { uint16_t id; uint8_t type; Vec3 at; double t; };
    std::vector<LocalBoom> localBooms_;
    void localBoom(uint16_t id, int type, const Vec3& at);
    bool enemyOnSegment(const Vec3& a, const Vec3& b, float* t, int* who = nullptr, bool* head = nullptr);   // the first enemy box (as you see them) on a -> b
    std::string feedPart_;   // (drawFeedText)
    double frameDt_ = 0;     // this frame's dt (frame)
    bool polledEarly_ = false;   // preInput read the network this frame
    struct InfoRow { std::string text; bool head = false; float y = 0; };   // y: design px from the page's top
    std::vector<InfoRow> infoRows_;          // (drawInfo: the page wrapped to its column, for this tab, size and font)
    int infoRowsTab_ = -1;
    float infoRowsW_ = 0, infoRowsTs_ = 0;
    int infoRowsFont_ = -1;
    std::string perfTick_;
    std::string perfPrio_;   // F3: PRIORITIZE GAME as Windows reports it (gamePriorityStatus; empty off Windows)   // (drawPerf: the version / server line, made with the numbers)
    struct JoiningThread { std::thread t; ~JoiningThread() { if (t.joinable()) t.join(); } };
    JoiningThread raceWriter_;  // (onRaceSplit: appends a finished run to race_times.log; joined however the game ends)
    // the race ghost: this run's path (20 a second, from its start) and the best run's on this map (ghosts/<map>.txt)
    struct GhostPt { float t; Vec3 pos; };
    std::vector<GhostPt> ghostRec_, ghostBest_;
    std::string ghostMap_;               // (the map ghostBest_ is for)
    double ghostRunStart_ = -1, ghostNext_ = 0;
    JoiningThread ghostWriter_;
    void drawTeamPanel(float H, float s, float ts);   // TEAM PANEL
    int matchShots_ = 0, matchHits_ = 0, matchBestStreak_ = 0; float matchDmg_ = 0;   // MATCH SUMMARY: this match's, from when it began
    void updateGhost();                  // record this run; draw the best one
    Vec3 viewAnchor(const Settings::HudElem& e, const Vec3& base) const;   // a first-person model's pivot (camera space)
    Mat4 weaponFrame(float kick, int weapon = -1) const;
    bool vmHidden(int weapon) const;     // the WEAPON MODEL element hidden, or that weapon's own model (wpn_vm_<n>)    // the weapon model: its space (x its size) -> camera space (HUD layout)
    Vec3 muzzlePoint(int weapon, const Vec3& eye) const;   // world point of the weapon model's barrel tip (HUD layout)
    double shotAt_ = -1;
    bool scoped_ = false;
    double laserUntil_ = 0;            // local laser beam visible until
    bool remoteFiring_[kMaxPlayers] = {};
    double muzzleUntil_ = 0;           // your muzzle flash showing until (VISUALS > MUZZLE FLASH)
    double remoteMuzzleUntil_[kMaxPlayers] = {}, remoteSparkAt_[kMaxPlayers] = {}; // others' flashes; their laser's next spark
    float viewFov() const;
    void sceneSettings();   // the renderer's world look from cfg_
    static const Settings& defaultSettings() { static const Settings d; return d; }   // (sliders' right-click: back to these)
    float edPanelX_ = 1e9f;   // the editor's side panel starts here (a right-click there is the UI's, not mouse look)
    float edBarTop_ = 0;      // the top of the editor's key bar (the 3D view's status line + key help): its dialogs centre over it

    // F3 performance overlay: frame-time percentiles (lows matter, averages hide stutter)
    // F3 cycles (owner 2026-10-29): 1 the FPS line, 2 + ping / 3D size / server, 3 everything with the pipeline graph, 0 off.
    // Only 3 runs the graph's and input-to-frame measuring (GPU timestamps): 1 and 2 cost just their text.
    int perfStage_ = 0;
    bool hudBuilt_ = false;            // drawWorld built the HUD (and F3 / F4) before its late read this frame
    bool perfMini_ = false;            // F4: only the mouse to photon number (drawPerfMini)
    bool perfOn() const { return perfStage_ == 3 || perfMini_; }   // (the pipeline graph's measuring runs)
    std::string perfMiniText_;
    std::vector<float> frameTimes_ = std::vector<float>(2048, 0.0f);
    size_t frameIdx_ = 0, frameCount_ = 0;
    double perfNextUpdate_ = 0;
    std::string perfText_, perfText2_;   // F3's first two lines: the frame rates, then the worst frame and the ping
    void drawPerf();

    // display / latency
    void applyDisplayMode();
    void applyPresentMode();
    void markInput() { if (pendingInput_ < 0) pendingInput_ = timeNow(); }
    void turn(double dx, double dy);   // mouse counts into yaw_ / pitch_
    RawMouse rawMouse_;                // Windows: raw mouse reports on their own thread (rawmouse.h)
    RasterClock raster_;               // latent sync: the monitor's scanline (gpuprefs.h)
    GLFWmonitor* latentMon_ = nullptr; // (the monitor raster_ was opened on)
    double latentFlip_ = 0;            // (this frame's planned flip: the vertical blank it's for)
    // (the lead: the 3rd slowest of the last 256 frames' input read to finished on the GPU, about a second at 240 Hz:
    // the single slowest of 3 s, as it was, held every frame's read milliseconds early for seconds after one hitch)
    float latentTook_[256] = {};
    int latentTookN_ = 0;
    double latentLead_ = 0.002, latentHeld_ = 0;   // (+ how long this frame was held for its blank: not CPU time)
    double latentLastFlip_ = 0;                    // (the last planned flip: one frame a refresh)
    double latentOpenAt_ = -1;                     // (when raster_ was last opened: a failed one is retried a second on)
    int latentH_ = 0, latentHz_ = 0;               // (the mode it was opened for: reopened when it changes)
    // The game's monitor's mode, cached: glfwGetVideoMode asks the display driver every call (EnumDisplaySettings, tens
    // of us), and Latent Sync, F3 and the settings page wanted it every frame. Read again on a display mode change, a
    // resize or another monitor.
    GLFWmonitor* vmMon_ = nullptr;
    int vmW_ = 0, vmH_ = 0, vmHz_ = 0;
    bool vmStale_ = true;
    GLFWmonitor* gameMonitor();   // (and the cache current)
    void latentWait();
    bool rasterBlank(double* at, double* refresh);   // the next vertical blank on the game's monitor (Windows)
    bool ultraPaced() const { return cfg_.lowLatency == 2 || cfg_.presentMode == 3; }   // (fences, learnt CPU / GPU times)
    bool mouseHeld_ = false;           // rawMouse_.hold is on (the cursor clipped to the window's centre)
    bool focused_ = true, iconified_ = false, bgCap_ = false;   // (the window in the background / minimized: WHEN UNFOCUSED)
    void applyBackground();            // bgCap_ and the background mute from focused_ / iconified_
    bool scoresOn_ = false;            // the scoreboard shown (CONTROLS > MORE: SCOREBOARD TOGGLE)
    double raceResetAt_ = -1;          // (RACE RESET pressed and not yet acted on: let go, the checkpoint; held 250 ms, the start)
    int moreOpen_ = 0;                 // settings: the tab's MORE page open (1, 2: which; 1.19.4, the settings that don't fit the tab)
    int morePages() const { return setTab_ == TAB_VIDEO ? 8 : setTab_ == TAB_GAMEPLAY ? 4 : setTab_ == TAB_CONTROLS ? 2 : setTab_ == TAB_PLAYER || setTab_ == TAB_HUD ? 1 : 0; }
    // batch 3 (1.19.4): GAMEPLAY > MORE 2 (feedback), MORE 3 (HUD info), MORE 4 (social, spectating); CONTROLS > MORE 2; PLAYER > MORE
    void drawGameplayMore2(const SetLayout& L);
    void drawGameplayMore3(const SetLayout& L);
    void drawGameplayMore4(const SetLayout& L);
    void drawControlsMore2(const SetLayout& L);
    void drawPlayerMore(const SetLayout& L);
    // PLAYER: your model in your team colour, turning (drag to turn it): its boxes projected into the 2D batch, only
    // while the tab is open (a few hundred quads)
    void drawModelPreview(float x, float y, float w, float h);
    void drawCompassRadar(float W, float H, float s);   // HUD > MORE: COMPASS, RADAR
    std::vector<std::pair<Vec3, Color>> radarObj_;     // (the objectives' markers this frame, for the RADAR)
    void drawHudMore(const SetLayout& L);               // HUD > MORE (batch 4b)
    float previewYaw_ = 0.6f;
    bool previewDrag_ = false;
    float previewLastX_ = 0;
    ModelParts previewParts_;
    struct PreviewFace { float xy[8]; float z; Color c; };
    std::vector<PreviewFace> previewFaces_;
    std::string chCodeMsg_; double chCodeUntil_ = 0;   // (CROSSHAIR > COPY / PASTE CODE's answer)
    void drawMoreSettings(const SetLayout& L);
    const char* moreNav(int n) const;
    // the page strip at the foot of a tab with MORE pages ("<  1 2 3 ... N  >", 1.19.4+): page n (0: the tab itself),
    // clamped to the tab's pages; the Left / Right arrow keys step it while nothing else has the keyboard
    void settingsPage(int n);
    bool settingsPageKeys() const;
    void drawSettingsPages(const SetLayout& L, float footY);
    // (UI test only) the lowest pixel the settings page's content drew on this frame, and the footer row's top: the
    // test fails a page whose content runs into its footer (-1: no settings page drawn this frame)
    float setInkBottom_ = -1, setFootTop_ = 0;
    bool hudHidden_ = false;           // the HIDE HUD key
    // 1.19.4 settings, batch 3 (VIDEO + AUDIO > MORE 2 / 4 / 5 / 6, CROSSHAIR)
    float hfov() const;                       // FOV MODE: the horizontal degrees the camera takes
    Vec3 tracerColorFor(int shooter) const;   // TRACER COLOR
    int soundPackOf(int id) const { return cfg_.forcePack ? -1 : net_.playerSoundPack(id); }   // FORCE MY SOUND PACK
    float masterGain() const;                 // VOLUME as a gain (PERCEPTUAL: squared; unfocused: x BACKGROUND VOLUME)
    void applyLive();                         // the batch's settings into the renderer / mixer / net (each frame: a few compares)
    void drawNetGraph(float W, float H, float s);
    void drawSoundViz(float cx, float cy, float s);
    void pruneMapDownloads();                 // MAP CACHE: the oldest downloaded maps past the limit deleted
    void applyCursor();                       // MENU CURSOR: the system's, or ours at a size and colour
    void applyCompositor();                   // BYPASS COMPOSITOR (Linux, X11)
    bool useOn_ = false;                      // USE: TOGGLE's state
    float netPing_[120] = {}; int netPingN_ = 0; double netSampleAt_ = 0;   // NET GRAPH: ping, 10 a second
    float netLoss_[120] = {};
    struct HeardMark { int id; Vec3 at; double until; };
    std::vector<HeardMark> heardMarks_;       // VISUALIZE SOUNDS
    void* cursor_ = nullptr; int cursorKey_ = 0;   // (a GLFWcursor; the size / colour it was made for)
    int compositorSet_ = -1;
    bool dlWas_ = false;                      // (a map download was running last frame)
    std::string lookMsg_; double lookMsgUntil_ = 0;   // (MORE 4 / 6 buttons' answers)
    // batch 4
    std::string shotPending_;                 // SCREENSHOT: HIDE HUD: saved after the 3D view, before the HUD
    std::string shotDir() const;
    int onTopSet_ = 0;
    bool captionQuip_ = false;                // (the caption showing is a quip's: QUIP CAPTIONS)
    Color hbLowC() const { return length(cfg_.hbLowColor - Vec3(1, 0.35f, 0.3f)) < 0.01f ? kRedNow() : rgba(cfg_.hbLowColor); }
    static Color kRedNow();
    double reconnectAt_ = 0; int reconnectTries_ = 0;   // AUTO RECONNECT: the next try, and how many so far
    // a key bound here that another action had: who lost it (UNDO puts it back there, and this slot as it was)
    int bindTakenA_ = -1, bindTakenK_ = 0, bindTakenCode_ = 0, bindTakenTo_ = -1, bindTakenSlot_ = 0, bindTakenWas_ = 0; double bindTakenUntil_ = 0;
    int chatScroll_ = 0;               // typing: chat history scrolled up this many lines (the wheel / Page Up)
    bool teamChat_ = false;            // the chat line being typed goes to the team (TEAM CHAT key)
    bool grappleOn_ = false;           // GRAPPLE TOGGLE: hooked by the last press
    int lastWeapon_ = -1, lastWeaponFrom_ = -1;   // LAST WEAPON: the weapon held before the current one
    std::string keyOf(int action) const;   // its key's name, for hints ("UNBOUND")
    void cycleSpec(int dir);           // the next (1) / previous (-1) player to watch
    double cornerAt_ = 0; int cornerFrames_ = 0, cornerFps_ = 0;   // SHOW FPS: frames counted over half a second
    float hpMax_ = 100; bool hpAlive_ = false;   // HEALTH BARS: MODE'S MAX: the most health seen this life (from its spawn)
    std::string captionText_; double captionUntil_ = 0;   // ANNOUNCER CAPTIONS: the last line, as text
    double bombBeepAt_ = 0;            // the planted bomb's next beep (OBJECTIVE volume)
    void announce(int snd);            // an announcer line (played, and captioned when ANNOUNCER CAPTIONS is on)
    int vrrHz_ = 0;                    // G-SYNC / FREESYNC: the monitor's refresh (the cap goes 3 under it)
    std::string confirmWhat_; double confirmUntil_ = 0;   // CONFIRM LEAVE: the button armed by a first click
    bool confirmed(const std::string& what);   // (true: do it; false: armed, click again)
    bool confirmArmed(const std::string& what) const { return cfg_.confirmLeave && confirmWhat_ == what && time_ < confirmUntil_; }
    int settingsStartTab() const { return cfg_.rememberTab ? cfg_.lastTab : TAB_PLAYER; }
    int prioLevel(bool focused) const { return cfg_.prioritize && focused ? cfg_.cpuPriority : 0; }
    // a display change waiting for KEEP (VIDEO > MORE: RESOLUTION, VIDEO: DISPLAY): reverted at keepUntil_ unless kept
    double keepUntil_ = 0;
    int keepMode_ = 0, keepW_ = 0, keepH_ = 0;
    void askKeepDisplay(int oldMode, int oldW, int oldH);
    bool focusGained_ = false;         // (focus came back in this event poll: its mouse presses are swallowed)
    uint16_t swallowedBtn_ = 0;        // (mouse buttons pressed by that click: up to their release, not down)
    bool mouseFocused_ = false;        // (the window had the focus at the frame's first read: the late reads use it)
    uint8_t barRay_[kMaxPlayers] = {};   // (health bars: the sight ray that last found a player hidden, tried first)
    double mouseHeldAt_ = 0;
public:
    void readMouse(bool late = false); // the main loop, before glfwPollEvents: raw mouse reports in one read (Windows)
private:
public:
    // F3 pipeline graph: when each part of a frame starts and ends (the main loop and frame() mark them), GPU timestamps
    // around the frame's GL work, averaged twice a second and drawn as a waterfall from the mouse to the photons.
    enum PipeMark { PM_FRAME, PM_CAP, PM_FENCE, PM_NET, PM_POLL, PM_TICKS, PM_SCENE0, PM_SCENE1, PM_HUD, PM_SWAP0, PM_SWAP1, PM_AFTER, PM_LATCH, PM_COUNT };
    void pipeMark(int m);
private:
    double pipeNow_[PM_COUNT] = {};
    struct PipeSlot { unsigned q[2] = {0, 0}; double marks[PM_COUNT] = {}; bool used = false; };
    PipeSlot pipeQ_[4];
    int pipeNext_ = 0;
    bool pipeGpuStarted_ = false, pipeFrameDone_ = false;
    double pipeSum_[PM_COUNT + 3] = {}, pipeAvg_[PM_COUNT + 3] = {};   // offsets from the input read (+ GPU start, end, flip)
    double pipeBlankAt_ = 0, pipeLastFlip_ = 0, pipePrevFrame_ = 0, pipeFrameDt_ = 0;   // (+ the frame time, smoothed)   // (vsync: a vertical blank's start from the scanline; the last frame's flip)
    bool pipeFlipOk_ = false;                     // (the flips are worked out from the monitor's blanks, not estimated)
    int pipeN_ = 0;
    bool pipeHave_ = false;
    int mouseEventsPoll_ = 0;                  // raw mouse events in this poll (rate: events over the time they span)
    double mouseLastEventPoll_ = -1, mouseBurstEvents_ = 0, mouseBurstTime_ = 0, mouseHz_ = 0;
    void pipeGpuBegin();
    void pipeGpuEnd();
    void pipeCollect();
    void pipeReset();   // (F3 / F4 turned on: nothing from the last time they were on)
    void drawPipeline(float x, float y, float ts, double* totalOnly = nullptr);
    void pipeAverages();
    void drawPerfMini();
    double pendingInput_ = -1, frameInput_ = -1;
    void* fence_[2] = {nullptr, nullptr};
    int fenceIdx_ = 0;
    void* ultraFence_ = nullptr;               // ULTRA: the last frame's fence (beforeInput paces the next frame by it)
    double ultraSwapAt_ = 0, ultraReadAt_ = 0;
    double ultraGpu_ = 0.0005, ultraCpu_ = 0.0003;   // learnt: swap to GPU done, input read to swap (seconds)
    struct LatQuery { unsigned q; double input; bool used; };
    LatQuery latQ_[16] = {};
    int latNext_ = 0;
    double gpuClockOffset_ = 0, gpuCalibAt_ = -10;
    std::vector<float> latSamples_;      // ms, last second
    std::string latText_;
    double latNextUpdate_ = 0;
    // your weapon, predicted (weaponpredict.h): the key's choice until the server has run it, and what you hold now
    // (view model, HUD, scope, local shots) without waiting a round trip for the snapshot to say so
    WeaponPredictor wp_;
    int heldWeapon_ = 0;   // updated every tick and frame (heldWeapon()); the snapshot's selfWeapon when dead / a demo
    WeaponView weaponView() const;
    int heldWeapon() const { return heldWeapon_; }
    // sound
    Audio audio_;
    void updateSounds(const NetClient::PlayerArray& players);
    MoveState prevMove_;
    double prevSoundAt_ = 0;   // (updateSounds: when it last ran, for the frame time)
    bool prevAlive_ = false;
    float stepDist_ = 0;
    struct RemoteAudio { Vec3 pos; bool valid = false; float stepDist = 0; bool hooked = false; bool firing = false; double nextLaser = 0, nextShot = 0, nextHurt = 0; int health = 0; };   // (firing: a demo's recorded player only)
    RemoteAudio remoteAudio_[kMaxPlayers];
    // rebinding
    bool isDown(int action) const;
    struct HudRect { float x = 0, y = 0, w = 0, h = 0; };
    HudRect drawKeyOverlay(float W, float H, float s, bool sample = false, bool draw = true);   // (sample: no key lit; draw false: only where it is)
    // community (community.cpp): Discord link, queues shared with the Discord bot, leaderboard crowns
    struct QueueDef { const char* key; const char* title; int size; };
    static const QueueDef kQueues[];
    static const int kQueueCount;
    static const char* queueTitle(const std::string& key);
    void updateCommunity();
    void linkDiscord();
    void queueAction(const std::string& mode, const char* action);
    void drawQueueMenu(float cx, float y);
    int crownOf(int playerId) const;              // 1 = #1 on a ranked board, 2-10 top 10, 0 none (or crowns off)
    const char* crownLetter(int playerId) const;  // the board of that best place: Q vanilla, LG laser vampire, I instagib, R race
    int specKeysOf() const;
    HudRect drawChatBox(float W, float H, float s, float ts, bool sample = false, bool draw = true);   // (the HUD's chat element)
    void drawMenuChat(float W, float H, float s, float ts);
    bool chatOverMenu_ = false;   // (drawChatBox over the settings menu: dark boxes)
    void toggleChat();                     // TOGGLE CHAT key: other players' chat off / on (the DISABLE CHAT setting), with a notice
    float drawVotePanel(float cx, float s, float ts, float y = -1);   // a running vote, its box from y (-1: 122 at 720p); returns where it ends
    void drawCrown(float x, float y, float w, int place, const char* letter = "");
    struct Crown { int place; const char* letter; };
    std::map<std::string, Crown> crowns_;
    // lifetime kills / deaths (every session, version and mode; the master adds up what each client reports)
    std::map<std::string, std::pair<int, int>> lifetime_;   // ranked id -> kills, deaths
    int lifeSeenK_ = -1, lifeSeenD_ = -1, lifePendK_ = 0, lifePendD_ = 0;
    double nextLifeFetch_ = 0;
    int lifePhase_ = -1;
    std::string lifeAsked_;
    void updateLifetime();         // ranked id -> best place on any board (and which board)
    std::set<std::string> queued_;
    std::map<std::string, int> queueCounts_;
    double nextCrownFetch_ = 0, nextQueuePoll_ = 0, nextFilledPoll_ = 0;
    long long lastFilled_ = -1;
    std::string pendingFilled_;
    std::string discordCode_;   // settings: the one-time code from the bot's /link (or "unlink"), sent on BACK
    bool queueMenu_ = false;
    bool practiceMenu_ = false;          // pause > HOST > PRACTICE (the host alone with bots)
    void drawPracticeMenu();
    bool playersMenu_ = false;           // pause > HOST > PLAYERS: kick, move to spectators, bots
    void drawPlayersMenu();
    // Settings > SEARCH (owner, 2026-10-30): every settings page's widgets in one 3-wide list, what's typed in its field
    // narrowing it (drawSearchTab; how a row is matched: the relocator in game.cpp)
    std::string setSearch_;              // the field's text
    int searchScroll_ = 0;               // the list's first row shown (the wheel, the scrollbar)
    bool searchDrag_ = false;            // the scrollbar's thumb held
    float searchDragOff_ = 0;            // (where on the thumb it was taken)
    int searchRows_ = 0;                 // the list's rows last frame (the wheel's and the scrollbar's range)
    std::vector<std::string> searchMatched_;   // (UI test only) every matching widget's label this frame, shown or not
    static constexpr int kSearchField = 62;
    void drawSearchTab(const SetLayout& L);
    void openSearch();                   // Ctrl+F / "/": the SEARCH tab, its field focused
    bool settingsShown() const { return screen_ == Screen::Settings || (screen_ == Screen::InGame && paused_ && settingsInGame_); }
    void onKillFeedback(int killer, int victim, int weapon, bool head, bool airborne);
    int streak_[kMaxPlayers] = {};
    double typingUntil_[kMaxPlayers] = {}, nextTypingSend_ = 0;
    double locatorReady_ = 0, locateUntil_ = 0;
    // hide and seek tiebreak (EV_RPS): the server's latest state, and what we chose this throw
    struct RpsView {
        struct P { int id = 0, shape = 0, wins = 0; bool out = false; };
        int stage = RPS_PICK, throwNo = 0, winner = -1;
        bool bestOf3 = false;
        std::vector<P> players;
        std::string result;
        double at = -1e9;   // when it arrived
    } rps_;
    int rpsMine_ = RPS_NONE;
    double liveSeenAt_ = -1e9;   // when this round went live (here): the map's holdfire counts from it
    float holdFireLeft() const { return net_.hasSnapshot() && net_.mode().phase == PHASE_LIVE && net_.map().holdFire > 0 ? net_.map().holdFire - (float)(time_ - liveSeenAt_) : 0; }   // tag / hide and seek locator
    Vec3 locatePos_;
    int locateId_ = 0;   // kills since their last death (killstreak calls)
    bool console_ = false;               // the settings console (`)
    std::deque<std::string> consoleLog_;
    void consolePrint(const std::string& t);
    std::vector<std::string> consoleWords() const;
    std::vector<std::string> consoleWords_;   // drawConsole's cache of consoleWords() (cleared: listed again)
    void consoleComplete();
    std::vector<std::string> consoleCandidates(const std::vector<std::string>& words) const;
    void chatSync();                       // caret to the end if chatText_ was replaced
    // The key press that opened the chat / console (a bind: T, Y, `...) arrives as a character right after the key
    // event (same event poll) and again on each key repeat: neither may be typed into the line it opened.
    int textOpenKey_ = -1;                 // the key that opened it, until released (its repeats are swallowed)
    bool swallowChar_ = false;             // drop the next character (this poll only: cleared each frame)
    void openedTextBy(int key) { textOpenKey_ = key; swallowChar_ = true; uiIn_.chars.clear(); }
    bool chatCaretKey(int key);            // Left / Right / Home / End / Delete in the chat or console input
    void chatEdit(size_t maxLen, bool console); // typed characters + backspace at the caret
    void drawCaret(float x, float y, float ts, bool mono, const std::string& before);
    std::vector<std::string> consoleHistory_;
    int consoleHistoryAt_ = -1;
    void runConsole(const std::string& line);
    void runCommands(const std::string& cmds, int depth = 0);   // ';'-separated console lines (binds, aliases, exec files)
    int consoleDepth_ = 0;                 // (aliases / exec nesting: a loop stops at 8)
    void execFile(const std::string& name, bool quiet = false);   // exec: <data folder>/<name>[.cfg], a console line each
    void drawConsole(float W, float H, float s, float ts);
    int bestMapFor(int mode) const;
    int bestRankedMap(int ladder) const; // the ladder's own ranked map, else any ranked map for it
    bool pressInput(int code);
    void drawControls(const SetLayout& L);
    void drawHudSettings(const SetLayout& L);
    void drawCrosshairSettings(const SetLayout& L);
    void drawKillMark(float cx, float cy, float scale);
    void drawCrosshair(float cx, float cy, float scale, bool hit, int weapon = -1);   // (weapon: its own STYLE if set)
    // FONT (settings/fonts, from PLAYER): the built-in UI fonts, then the player's mods/fonts files (uifonts.h). The
    // list is looked up when the page opens and on REFRESH; a font's atlas is built once when it's picked.
    bool fontsOpen_ = false;
    void drawFontSettings(const SetLayout& L);
    std::vector<uifonts::Entry> fontList_;
    bool fontListed_ = false;      // (fontList_ is current: cleared on opening the page and by REFRESH)
    int fontScans_ = 0, fontScroll_ = 0;
    std::string fontTried_;        // the ui_font value last applied (frame() applies cfg_.uiFont when it differs)
    bool loadUiFont(const std::string& id, std::string* err, std::string* name);   // built and on the GPU, or false
    void applyUiFontSetting();     // cfg_.uiFont, or Rubik with a notice when it can't be loaded
    // DEVICES (settings/video/devices, from VIDEO + AUDIO): the sound output, the microphone, the monitor and exclusive
    // fullscreen's refresh rate (devices.h). The OS is asked for the lists when the page opens and on REFRESH (and a
    // monitor plugged in or out), never per frame; a pick is saved and applied at once.
    bool devicesOpen_ = false;
    void drawDeviceSettings(const SetLayout& L);
    void listDevices();            // the page's lists, from the OS
    void listRates();              // the chosen monitor's refresh rates at its desktop size (highest first)
    bool devListed_ = false;       // (the lists are current: cleared on opening the page, by REFRESH, by a monitor change)
    int devScans_ = 0;             // (how many times they were listed: the UI test's "devscan")
    std::vector<DeviceEntry> devOut_, devIn_, devMons_;   // (names as shown: unique, upper case; monitors with their mode)
    std::vector<int> devRates_;
    // the rows' text, made when the lists are (listDevices / listRates), not per frame: each row's UI test id "<PREFIX>: <NAME>"
    // and its label in use "> NAME <" (an id is a pointer the UI test keeps past the frame: it must not move while the lists
    // stay the same), and REFRESH RATE's names (made again only when the rates, the desktop rate or the saved rate change)
    void devRowText();
    std::vector<std::string> devRowId_[3], devRowOn_[3];
    std::vector<std::string> devRateNames_;
    int devRateKey_[2] = {-1, -1};   // (the desktop rate and cfg_.fullscreenHz devRateNames_ were made for; -1: make them again)
    int devScroll_[3] = {};        // the output / input / monitor lists' first row shown (the wheel over a list scrolls it)
    float devListBox_[3][4] = {};  // (where each list was drawn: the wheel's target)
    // the monitor: cfg_.monitor's, else the primary (gone: noticed once). Resolved on a change only (not per frame).
    GLFWmonitor* chosenMonitor();
    GLFWmonitor* chosenMon_ = nullptr;
    std::string chosenMonId_, monGoneNoted_;
    bool chosenGone_ = false;
    GLFWmonitor* appliedMon_ = nullptr;   // (the monitor applyDisplayMode last put the window on)
    // a monitor's desktop mode (its mode while the game isn't exclusive on it: then glfwGetVideoMode gives the game's)
    struct DeskMode { int w = 0, h = 0, hz = 0; };
    DeskMode desktopMode(GLFWmonitor* mon);
    GLFWmonitor* deskMon_ = nullptr;
    DeskMode desk_;
    GLFWmonitor* dmCacheMon_ = nullptr;   // (desktopMode: the last windowed answer, kept a second)
    DeskMode dmCache_;
    double dmCacheUntil_ = 0;
    void monitorsChanged();        // (GLFW's monitor callback: one plugged in or out)
    // scriptable HUD: hud.cfg widgets, the verbose info block, and the in-game widget editor
    HudWidgets widgets_;
    HudVals hudVals_;
    bool widgetEdit_ = false, widgetDrag_ = false, valueDrop_ = false;
    int widgetSel_ = -1;
    float dragDX_ = 0, dragDY_ = 0;
    std::string widgetMsg_;
    double widgetMsgUntil_ = 0;
    void computeHudVals();
    // race: splits of the current run, every finished run (race_times.log), time trial starts / submits
    struct RaceRun { long long when = 0; std::string map; int ms = 0; bool trial = false; std::vector<int> splits; };
    std::vector<RaceRun> raceRuns_;
    bool raceRunsLoaded_ = false, timeTrialStart_ = false, raceStatsView_ = false;
    std::vector<int> raceSplits_;
    std::string splitText_;
    Color splitColor_ = {1, 1, 1, 1};
    double splitUntil_ = 0;
    int raceStatsMap_ = 0;   // MY RACE STATS: which raced map
    int timeTrialMap_ = 0;   // the leaderboard's TRIAL MAP: which ranked race map (rankedRaceMaps) PLAY TIME TRIAL starts
    std::string timeTrialMapName_;   // (set by startTimeTrial for startHost)
    void loadRaceRuns();
    const RaceRun* bestRun(const std::string& map, bool trial) const;
    void onRaceSplit(int checkpoint, int ms);
    void startTimeTrial(const std::string& map = std::string());   // (empty: the TRIAL MAP)
    void drawRaceStats(const SetLayout& L);
    // profiles: per-mode rules swap the HUD layout / controls while that mode is running
    Binds modeBinds_;
    bool modeCtlActive_ = false, modeAlt_ = false;
    int modeSocd_ = 0, appliedMode_ = -2;
    std::string modeHudPath_;               // "" = hud.cfg
    int ruleMode_ = MODE_FFA;               // mode shown in the rule rows
    int filePick_ = 0;                      // import file list open: 1 huds, 2 controls
    // ranked and time trials: default model, HUD, controls and 10 ms interp for everyone
    // duel "double size" lives: models, hitboxes and eye heights scale together (matches Server::bodyScale)
    float bodyScale() const {
        if (!net_.hasSnapshot()) return 1.0f;
        const ModeState& m = net_.latest().ms;
        return m.mode == MODE_DUEL && m.phase != PHASE_WARMUP ? duelModDef(m.duelMod).size : 1.0f;
    }
    // ranked visuals (no crowns, health bars, typing, custom models or HUD extras) apply to the players only:
    // spectators of a ranked match see everything
    bool rankedPlayer() const {
        if (!fairRules()) return false;
        const PlayerSnap* me = net_.findPlayer(net_.clientId());
        return !me || me->team() != TEAM_SPECTATOR;
    }
    bool fairRules() const { return net_.hasSnapshot() && (net_.mode().flags & (MS_RANKED | MS_TIMETRIAL)); }
    const Binds& binds() const { return modeCtlActive_ ? modeBinds_ : cfg_.binds; }
    std::string activeHudPath() const;
    void applyModeRules(int mode);
    bool drawFileList(const char* sub, float x, float y, float w, std::string& picked);
    void drawVerboseHud(float W, float s, float& y);
    void drawWidgetEditor();
    int hudSel_ = 0;         // HUD settings: the element (its ELEMENT dropdown)
    // The HUD's elements, each drawn from its values (the live HUD and the HUD tab's preview share them); each returns
    // where it is on screen when draw is false (nothing drawn: the preview's outline; drawn, it may return an empty rect)
    struct HudBarVals { float wRem = 0, wTot = 1; int dashN = 2, dashHave = 2; bool dashReady = true; float hookRem = 0, hookTot = 1; };
    HudRect drawHudHealth(const Settings::HudElem& e, float W, float H, float s, int hp, Color num, Color bar, bool draw = true);
    HudRect drawHudWeaponName(const Settings::HudElem& e, float W, float H, float s, const std::string& name, Color c, bool draw = true);
    HudRect drawHudBars(const Settings::HudElem& e, float W, float H, float s, const HudBarVals& v, Color full, bool draw = true);
    HudRect drawHudSpeed(const Settings::HudElem& e, float W, float H, float s, float v, bool draw = true);
    HudRect drawHudSlots(const Settings::HudElem& e, float W, float H, float s, int mode, uint32_t mask, int held, Color heldC, bool live, bool draw = true);
    struct ScoreText { std::string title, phase, scoreA, scoreB; bool ranked = false, unrated = false; };
    struct ScoreWidths { std::string title, phase, scoreA, scoreB; float ts = -1, wTitle = 0, wPhase = 0, wA = 0, wB = 0; int gen = -1; };
    ScoreWidths hudScoreW_;                           // (the score box's text widths: measured again only when a line changes)
    ScoreText hudScoreText_;                          // (reused: its strings keep their buffers from frame to frame)
    void scoreText(ScoreText& t, bool sample) const;  // the score box's lines (sample: a made-up free for all)
    mutable int voteKeyCode_ = -2;                    // (the vote-start key's name, for the warmup line: looked up when the bind changes)
    mutable std::string voteKeyName_;
    HudRect drawHudScore(const ScoreText& t, float W, float H, float s, bool draw = true);
    HudRect drawHudKillFeed(float W, float H, float s, float top, bool sample, bool draw = true);
    float hudFeedTop_ = 12;                           // (where the kill feed starts, 720p units: under the verbose block)
    // ---- the HUD's elements on the layout engine (layout.h), styled by theme.cfg (theme.h): drawHudElement and friends
    HudTheme theme_;
    std::string themePath_;                           // (theme.cfg's path, built once)
    double themePolledAt_ = -1e9;                     // (time_ when theme.cfg was last looked at: once a second)
    std::string themeErrShown_;                       // (the theme's error last shown as a notice)
    void pollTheme();
    // one per element, each re-solving only when its own content changes (held here, not on the heap: a frame's HUD
    // touches as few cache lines as it can; capacities: the weapon list's slots, the kill feed's and chat's lines)
    lay::Layout hudLay_[Settings::HUD_COUNT] = {lay::Layout(4), lay::Layout(4), lay::Layout(4), lay::Layout(4), lay::Layout(24), lay::Layout(4),
                                                lay::Layout(4), lay::Layout(4), lay::Layout(8), lay::Layout(56), lay::Layout(64),
                                                lay::Layout(1), lay::Layout(1), lay::Layout(1)};   // (the last three: drawn by fixed code, moved by hudMove)
    HudWidth hudCapW_[8];                             // (captions: the bars' WEAPON / DASH / HOOK, the speed meter's, the weapon name)
    struct SlotLabels { char t[WPN_COUNT][32]; float w[WPN_COUNT]; int away[WPN_COUNT]; float ts = -1; int gen = -1; };   // (away: the seconds in t; INT_MIN: not made)
    SlotLabels hudSlotW_ = {};                        // (the weapon list's labels and widths, measured when one changes)
    lay::Rect hudRect_[Settings::HUD_COUNT];          // each element's box as last laid out (the editor: outlines, hits, snapping)
    static constexpr float kRankedMinOpacity = 0.3f;  // ranked: a theme's opacity can't take an element below this
    // ---- the on-screen HUD editor (Settings > HUD > EDIT ON SCREEN; hudeditor.cpp): nothing of it runs while closed
    bool hudEdit_ = false;
    int hudEdSel_ = Settings::HUD_HEALTH;
    int hudEdDrag_ = 0;                               // 0 none, 1 moving the selection, 2 sizing it (a corner)
    bool hudEdLeft_ = false;                          // (the inspector on the left: the selection is under its usual place)
    float hudEdX0_ = 0, hudEdY0_ = 0;                 // (where the drag started)
    Settings::HudElem hudEdOrig_{};
    lay::Rect hudEdRect0_;
    double hudEdWheel_ = 0;                           // (wheel notches since the last frame: sizes what's under the mouse)
    struct HudSnap { Settings::HudElem hud[Settings::HUD_COUNT]; std::string theme; };   // an undo step: the elements and theme.cfg
    std::vector<HudSnap> hudUndo_, hudRedo_;
    HudSnap hudEdLast_;                               // (the state the last step ended in)
    uint32_t hudEdLastVer_ = 0;
    void hudEditOpen();
    void hudEditClose();
    void drawHudEditor();
    bool hudEditKey(int key, int action, int mods);
    void hudEdCommit(bool force);
    void hudEdApply(const HudSnap& sn);
    bool hudEdUndo(bool redo);
    const HudStyle& hudStyle(int el) const;
    float hudOpacity(int el) const;
    // The objective line, the notices and the scoreboard: drawn where they always were, then moved and sized as a group
    // by their HUD element (offset, size; the theme's margin and opacity). Nothing at the defaults.
    struct HudMove { size_t mark = 0; float px = 0, py = 0, k = 1, dx = 0, dy = 0, oldAlpha = 1; bool on = false; };
    HudMove hudMoveBegin(int el, float px, float py);
    void hudMoveEnd(const HudMove& m);
    Color hudTint(int el, Color c) const;             // the element's colour: its HUD COLOR, else the theme's, else c
    float hudFont(int el) const;                      // its text size: SIZE x the theme's font-size
    bool hudSame(int el, float W, float H, const float* v, int n);
    lay::Rect hudSimple(int el, float W, float H, float ax, float ay, float refX, float refY, float k, float cw, float ch, lay::Rect& c);
    // opens element el's layout and its root box (anchored at (ax, ay) of the screen, its anchor point by default at
    // refX, refY); defPad: its built-in padding (px), themePad false: the theme's padding styles its lines instead
    lay::Layout& hudOpen(int el, float W, float H, float ax, float ay, float refX, float refY, float k, lay::Decl d,
                         const float* defPad = nullptr, bool themePad = true);
    lay::Rect hudClose(int el);                       // closes the root, solves (cached), keeps and returns its rect
    void hudBoxDraw(int el, const lay::Rect& r, float k, Color def);   // its background (def: the built-in one) and border
    // Settings > HUD's live preview: the tab is showing (not the widget editor, FONT or DEVICES)
    bool hudPreviewOn() const { return setTab_ == TAB_HUD && !moreOpen_ && !widgetEdit_ && !hudEdit_ && !fontsOpen_ && !devicesOpen_; }
    void drawHudLive(const ModeState& ms, const PlayerSnap* mep, float W, float H, float s, bool preview);   // (alive)
    HudRect drawHudElement(int el, float W, float H, float s, bool sample, bool draw = true);   // one element (the overlay)
    void drawHudSample(float W, float H, float s);   // every element with made-up values (the main menu's settings)
    void drawHudPreviewTop();                        // the selected element over the panel while it changes, and its outline
    Settings::HudElem hudPrevElem_{};                // (what the selected element was last frame: a change restarts the overlay)
    int hudPrevSel_ = -1;
    double hudChangedAt_ = -10;
    int captureAction_ = -1, captureSlot_ = 0;
    std::string voteText_;
    // tourney bracket (EV_BRACKET): rounds of player ids (-1 bye, -2 undecided), current match
    std::vector<std::vector<int>> bracket_;
    std::string bracketNames_[kMaxPlayers];
    int bracketRound_ = 0, bracketMatch_ = 0;
    void drawBracket();
    struct DmgNum { int victim; float amount; double start; Vec3 pos; bool head = false; };   // head: a headshot (drawn red)
    std::vector<DmgNum> dmgNums_;
    struct DmgDir { Vec3 pos; double at = 0; int from = -1; };   // a hit you took: from where (X3, drawDamageArcs)
    DmgDir dmgDirs_[6];
    static constexpr double kDmgDirTime = 1.0;
    void drawDamageArcs(float cx, float cy, float scale);
    int botMapIdx_ = 0;
};
