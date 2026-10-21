// The client application: menus, hosting, connection, input, world/HUD rendering.
#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "hudwidgets.h"
#include "../game/netclient.h"
#include "../game/server.h"
#include "../game/ranked.h"
#include "audio.h"
#include "binds.h"
#include "config.h"
#include "renderer.h"
#include "ui.h"

struct GLFWwindow;

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
    bool debugPerf = false;     // --perf: F3 overlay on, with a synthetic input event every frame
    bool debugCrosshair = false;
    int debugPicker = 0;        // --picker N: open that host-menu list (1 maps, 2 modes, 7 rulesets)
    bool debugPause = false, debugVote = false; // --pause / --votemenu
    std::string backdrop;       // --backdrop MAP: menu backdrop of that map, no menu drawn (site screenshots)
    float backdropAngle = -1;   // --orbit DEG: fixed orbit position for --backdrop   // --picker: open the host menu's map list
    bool debugWidgets = false;
    bool debugHudLayout = false; // --hudlayout: open HUD layout settings in game
    bool debugModels = false;
    bool camSet = false;         // --cam: fixed backdrop camera (map analysis shots)
    Vec3 camPos;
    float camYaw = 0, camPitch = 0;
    bool timeTrial = false;      // --timetrial: start a Viaduct time trial
    bool debugRaceStats = false; // --racestats: the leaderboard's race stats page    // --models: every player model lined up in front of you  // --widgets: open the HUD widget editor in game
    bool debugSettings = false; // --settings: open the settings screen in game
    bool debugControls = false; // --controls
    std::string editMap;        // --editor MAP: open the map editor on that map
    bool editSession = false;   // --editsession MAP: and host it as an edit session
    int editSel = -1;           // --edsel N: select box N (screenshots)
    bool debugLeaderboard = false; // --leaderboard: open the leaderboard screen
    bool menuShot = false;      // --menushot: screenshot whatever menu is up (main / settings / controls)
    double bench = 0;           // --bench SECONDS: measure frame times in a vs-bots game, print, quit
};

class Game {
public:
    bool init(GLFWwindow* win, const LaunchOptions& opts, std::string* err);
    void shutdown();
    void frame(double dt);
    bool quitRequested() const { return quit_; }
    int maxFps() const { return cfg_.maxFps; }

    // GLFW callbacks
    void onChar(unsigned cp);
    void onKey(int key, int action, int mods);
    void onMouseButton(int button, int action);
    void onCursor(double x, double y);
    void onResize(int w, int h);
    void onScroll(double dy);

    // Frame pacing hooks called by the main loop around the buffer swap.
    void beforeInput();   // frame limiter sleep happens before this, so input is sampled as late as possible
    void afterSwap();     // low-latency fence waits + F3 input-latency measurement

private:
    enum class Screen { Main, Join, Host, Settings, Connecting, InGame, VsBots, Editor, Leaderboard, CustomMode };
    void setScreen(Screen s);
    void startHost(bool vsBots = false);
    void stopHost();
    void connectTo(const std::string& addr);
    void leaveGame(const std::string& error = "");
    void loadMapIfNeeded();
    void updateInput(double dt);
    UserCmd buildCmd();
    void handleEvents();
    void drawWorld(float alpha);
    void drawHud();
    void drawScoreboard();
    void drawMenu();
    void drawPauseMenu(float cx, float y);
    void drawSettings(float x, float y, float w);
    void captureMouse(bool on);
    Vec3 teamColor(int playerId) const;
    bool isFriendly(int playerId) const;
    void notify(const std::string& s, float seconds = 4.0f);
    void updateAnnouncer();
    ModeState annPrev_;
    bool annPrevValid_ = false;
    int annCount_ = 0;

    GLFWwindow* win_ = nullptr;
    Settings cfg_;
    LaunchOptions opts_;
    Renderer r_;
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
    void benchFrame(double dt);
    bool paused_ = false, chatting_ = false, showScores_ = false, settingsInGame_ = false;
    std::string chatText_, joinAddr_, error_;
    double time_ = 0, tickAcc_ = 0;
    uint32_t loadedMapGen_ = 0;

    // hosting
    std::unique_ptr<Server> host_;
    std::thread hostThread_;
    std::atomic<bool> hostQuit_{false};
    std::string hostPortStr_;
    int hostMapIdx_ = 0, hostModeIdx_ = 0;
    uint8_t lastCmdWeapon_ = 255;   // the weapon the last command asked for (switching with fire held locks fire)
    // custom modes: the one being edited, its tab, and the rules arriving from a server in pieces
    CustomRules editRules_;
    std::string editOrigName_;
    int customTab_ = 0;
    std::vector<std::string> rulesParts_;
    void drawCustomModeEditor(float cx, float y);
    std::vector<std::string> maps_;
    struct MapInfo { std::string title; int preferredMode = -1; uint32_t modes = 0; uint16_t ranked = 0; }; // ranked: ladders it's a ranked map for // modes: bit per supported mode
    std::vector<MapInfo> mapInfo_;
    enum { PICK_NONE = 0, PICK_HOST_MAP, PICK_HOST_MODE, PICK_BOT_MAP, PICK_BOT_MODE, PICK_LIVE_MAP, PICK_LIVE_MODE, PICK_VARIANT, PICK_TMAPS, PICK_TMODES, PICK_RANKED };
    int variantCtx_ = 0, liveVariant_ = 0; // ruleset step: 0 host, 1 vs bots, 2 in game
    std::vector<bool> multiSel_;
    int liveMode_ = 0, liveMap_ = 0;  // in-game map/mode change (host) or vote (anyone)
    bool livePickVote_ = false;
    void openLivePicker(int kind, bool vote);
    bool voteMenu_ = false;
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
    std::string testMap_;       // set while play-testing a map from the editor
    int testMode_ = 0;

    // ranked: this computer's key, reports + rating lookups (background), the rating before the last report
    RankKey rankKey_;
    RankService rank_;
    double nextRankFetch_ = 0;
    int rankBefore_ = -1, rankGamesBefore_ = -1;
    void updateRanked();
    double nextBoardFetch_ = 0;
    void drawLeaderboard(float cx, float y);
    std::vector<std::string> rankIdsInGame() const;
    // map editor (editor.cpp). Every change is an edit op (applyMapEdit): applied locally when editing
    // alone, sent through the server in an edit session so everyone's copy stays the same.
    enum { ED_BRUSH, ED_SPAWN, ED_LIGHT, ED_SITE, ED_HILL, ED_CHECK, ED_PATH, ED_LADDER };
    struct EdSel { int kind = -1, idx = -1; bool valid() const { return kind >= 0; } };
    Map ed_;
    std::string edName_ = "mymap", edMsg_, edTitle_;
    double edResetArmed_ = 0, edMsgUntil_ = 0, edLastPush_ = 0, edLeaveArmed_ = 0, edNextCam_ = 0, edTickAcc_ = 0;
    EdSel edSel_;
    int edGridIdx_ = 1, edLoadIdx_ = 0;
    float edSpeed_ = 12;
    Vec3 edCam_;
    bool edLook_ = false, edDirty_ = false, edBaked_ = false;
    int edFullW_ = 1280;   // the window width (the editor's 3D view is narrower: the panel is on the right)
    bool edOnline_ = false;     // in an edit session (connected to a server in edit mode)
    bool edSession_ = false;    // this connection has been an edit session (leaving offers to save a copy)
    bool testEdit_ = false;     // hosting an edit session started from the editor
    uint32_t edRev_ = 0;        // net_.mapRevision() that ed_ was copied from
    std::vector<std::string> edUndo_; // inverse ops, newest last
    // dragging a handle of the selection: a face (resize) or, with Shift / on points, the whole thing (move)
    struct EdHandle { Vec3 pos; int axis, sign; bool move; };
    std::vector<EdHandle> edHandles() const;
    int edDragAxis_ = -1, edDragSign_ = 0;
    bool edDragMove_ = false, edDragPushed_ = false;
    Vec3 edDragP0_;
    float edDragT0_ = 0;
    Map edDragBase_;
    std::string edDragLast_;
    bool edPrompt_ = false;     // "save your copy?" after leaving someone else's edit session
    Map edPromptMap_;
    std::string edPromptName_;
    void openEditor(const std::string& map);
    void enterOnlineEditor();
    void closeEditor();
    void editorFrame(double dt);
    void editorKey(int key, int mods);
    void edDo(const std::string& op, const std::string& inverse, bool coalesce = false, bool undoable = true);
    void edUndoLast();
    void edChanged();
    void edNotify(const std::string& s);
    bool edSelValid() const;
    AABB edBounds(const Map& m, EdSel sel) const;
    void edModify(const std::function<void(Map&)>& f, bool coalesce);
    void edDelete();
    void edDuplicate();
    void edAdd(int kind, int variant);
    EdSel edPick(const Vec3& o, const Vec3& dir) const;
    Vec3 edMouseRay() const;
    bool edSave();
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
    bool localFireHeld_ = false, scopeOverlay_ = false, localFireLocked_ = false;
    double localFireQueued_ = -1;
    double nextLaserSound_ = 0;
    double knifeSlash_[kMaxPlayers] = {}; // when each player last swung the knife (slash trail)
    void drawKnifeSlash(const Vec3& eye, float yaw, float pitch, float t, const Vec3& color);
    double wheelAccum_ = 0;         // fractional scroll (trackpads, high-res wheels) until a full notch
    double wheelHeldUntil_[2] = {}; // a scroll step counts as "held" briefly, so hold actions (jump, fire...) work on the wheel
    int specTarget_ = -1;

    // effects + feeds
    struct Feed { std::string text; Color color; double until; };
    std::deque<Feed> killFeed_, chatFeed_, notices_;
    std::string centerText_;
    double centerUntil_ = 0, hitUntil_ = 0, damageUntil_ = 0, recoil_ = 0, localNextFire_ = 0;
    int lastHealth_ = 0;
    struct Tracer { Vec3 a, b; double until; };
    std::vector<Tracer> tracers_;
    struct Explosion { Vec3 at; double start; float size = 1; bool bubble = false; };
    std::vector<Explosion> explosions_;
    std::vector<ProjectileSnap> lastProjectiles_;
    // your own projectiles start drawn at your weapon model's muzzle and blend onto their real path
    struct ProjStart { uint16_t id; double seen; Vec3 offset; };
    std::vector<ProjStart> projStarts_;
    Vec3 muzzlePoint(int weapon, const Vec3& eye) const;   // world point of the weapon model's barrel tip (HUD layout)
    double shotAt_ = -1;
    bool scoped_ = false;
    double laserUntil_ = 0;            // local laser beam visible until
    bool remoteFiring_[kMaxPlayers] = {};
    float viewFov() const;

    // F3 performance overlay: frame-time percentiles (lows matter, averages hide stutter)
    bool showPerf_ = false;
    std::vector<float> frameTimes_ = std::vector<float>(2048, 0.0f);
    size_t frameIdx_ = 0, frameCount_ = 0;
    double perfNextUpdate_ = 0;
    std::string perfText_;
    void drawPerf();

    // display / latency
    void applyDisplayMode();
    void applyPresentMode();
    void markInput() { if (pendingInput_ < 0) pendingInput_ = timeNow(); }
    double pendingInput_ = -1, frameInput_ = -1;
    void* fence_[2] = {nullptr, nullptr};
    int fenceIdx_ = 0;
    struct LatQuery { unsigned q; double input; bool used; };
    LatQuery latQ_[16] = {};
    int latNext_ = 0;
    double gpuClockOffset_ = 0, gpuCalibAt_ = -10;
    std::vector<float> latSamples_;      // ms, last second
    std::string latText_;
    double latNextUpdate_ = 0;
    int desiredWeapon_ = -1;
    // sound
    Audio audio_;
    void updateSounds(const NetClient::PlayerArray& players);
    MoveState prevMove_;
    bool prevAlive_ = false;
    float stepDist_ = 0;
    struct RemoteAudio { Vec3 pos; bool valid = false; float stepDist = 0; bool hooked = false; double nextLaser = 0, nextHurt = 0; int health = 0; };
    RemoteAudio remoteAudio_[kMaxPlayers];
    // rebinding
    bool isDown(int action) const;
    int bestMapFor(int mode) const;
    int bestRankedMap(int ladder) const; // the ladder's own ranked map, else any ranked map for it
    bool pressInput(int code);
    void drawControls(float x, float y, float w);
    void drawHudSettings(float x0, float y0, float wTotal);
    void drawCrosshairSettings(float x, float y, float w);
    void drawCrosshair(float cx, float cy, float scale, bool hit);
    bool controlsOpen_ = false;
    bool crosshairOpen_ = false;
    bool hudOpen_ = false;
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
    void loadRaceRuns();
    const RaceRun* bestRun(const std::string& map, bool trial) const;
    void onRaceSplit(int checkpoint, int ms);
    void startTimeTrial();
    void drawRaceStats(float cx, float y);
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
    bool fairRules() const { return net_.hasSnapshot() && (net_.mode().flags & (MS_RANKED | MS_TIMETRIAL)); }
    const Binds& binds() const { return modeCtlActive_ ? modeBinds_ : cfg_.binds; }
    std::string activeHudPath() const;
    void applyModeRules(int mode);
    bool drawFileList(const char* sub, float x, float y, float w, std::string& picked);
    void ruleRow(float x, float y, float w, float h, bool hud);
    void drawVerboseHud(float W, float s, float& y);
    void drawWidgetEditor();
    int hudSel_ = 0;
    bool hudDrop_ = false;   // HUD settings: the element dropdown is open
    int captureAction_ = -1, captureSlot_ = 0;
    std::string voteText_;
    // tourney bracket (EV_BRACKET): rounds of player ids (-1 bye, -2 undecided), current match
    std::vector<std::vector<int>> bracket_;
    std::string bracketNames_[kMaxPlayers];
    int bracketRound_ = 0, bracketMatch_ = 0;
    void drawBracket();
    struct DmgNum { int victim; float amount; double start; Vec3 pos; };
    std::vector<DmgNum> dmgNums_;
    int botMapIdx_ = 0;
};
