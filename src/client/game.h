// The client application: menus, hosting, connection, input, world/HUD rendering.
#pragma once
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
#include "../game/netclient.h"
#include "../game/server.h"
#include "../game/ranked.h"
#include "audio.h"
#include "rawmouse.h"
#include "gpuprefs.h"
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
    bool debugPulse = false;    // --pulse: --fire / --scope let go every other 0.3 s (a switch or a throw needs a release), and
                                // a step right before the --screenshot (what was fired is seen from the side)
    bool debugPerf = false;     // --perf: F3 overlay on, with a synthetic input event every frame
    bool textHeavy = false;     // --textheavy: scoreboard + F3 + chat lines always on, to measure text cost (with --bench)
    bool debugCrosshair = false;
    int debugPicker = 0;        // --picker N: open that host-menu list (1 maps, 2 modes, 7 rulesets)
    bool debugPause = false, debugVote = false; // --pause / --votemenu
    std::string backdrop;       // --backdrop MAP: menu backdrop of that map, no menu drawn (site screenshots)
    float backdropAngle = -1;   // --orbit DEG: fixed orbit position for --backdrop   // --picker: open the host menu's map list
    bool debugWidgets = false;
    bool debugHudLayout = false; // --hudlayout: open HUD layout settings in game
    bool debugModels = false;
    int debugRps = 0;           // --rpsshot: the hide and seek tiebreak panel with made-up players (1 picking, 2 reveal, 3 three-way reveal)
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
    std::string editScript;     // --edscript "click x y;type t;..." : drive the editor's UI headless (tests, screenshots)
    bool debugLeaderboard = false; // --leaderboard: open the leaderboard screen
    std::string demo;              // --demo <file>: watch it
    int debugCustom = 0;           // --customshot <tab>: open the custom mode editor on that tab (stored +1; 0 = off)
    int debugInfo = 0;             // --info <tab>: open the info screen on that tab (stored +1; 0 = off)
    std::string chatShot;       // --chatshot TEXT: in game, chat open with TEXT typed and the caret in the middle
    std::string consoleShot;    // --consoleshot TEXT: in game, the console open with TEXT typed, then Tab pressed
    bool edFocus = false;       // --edfocus: the editor's map title field focused (caret screenshot)
    bool menuShot = false;      // --menushot: screenshot whatever menu is up (main / settings / controls)
    double bench = 0;           // --bench SECONDS: measure frame times in a vs-bots game, print, quit
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
    int maxFps() const { return cfg_.maxFps; }

    // GLFW callbacks
    void onChar(unsigned cp);
    void onKey(int key, int action, int mods);
    void onMouseButton(int button, int action);
    void onFocus(bool focused);   // (the click that brings the window back is the window's, not a shot)
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
    uint32_t testHeld_ = 0;                 // actions the UI test holds down (bit per Action), read by isDown
    int edOpens_ = 0;                       // openEditor() calls (NEW / OPEN reopened the editor)
    std::string uiScreenPath() const;       // where the UI is: "main", "host/picker:maps", "ingame/pause/settings/hud"...
    std::string uiContext() const;          // what else tells this screen's widgets apart (the editor's selection)
    std::string uiObserve(const std::string& what);   // a STATE observable's current value
    void uiTestActions();                   // this screen's keys / clicks that aren't widgets (the 3D view, hotkeys)
    void uiTestHandles();                   // the editor selection's handles, while the 3D view's camera is set
    const char* edPath() const { return edOnline_ ? "editor-online" : "editor"; }
    const char* edExportAskPath() const { return edOnline_ ? "editor-online/export-ask" : "editor/export-ask"; }
    const char* edGroupsPath() const { return edOnline_ ? "editor-online/groups" : "editor/groups"; }
    const char* edKeepPath() const { return edOnline_ ? "editor-online/keep-edits" : "editor/keep-edits"; }
    bool edGroupTakesEveryBox() const;      // GROUP would leave the map without a box of its own (refused)
    enum class Screen { Main, Join, Host, Settings, Connecting, InGame, VsBots, Editor, Leaderboard, CustomMode, Info, Demos };
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
    void drawRps(float W, float H, float s);   // the hide and seek tiebreak (rock paper scissors) panel
    void drawRpsShape(float x, float y, float size, int shape, Color c);
    bool rpsPicking() const;                   // we're in the tiebreak and the throw is open
    void drawTaggerMarker(float W, float H, float s);   // tag: where the tagger is, through walls (at the screen edge when off it)
    void drawNotices(float y, float ts, float s); // notify() lines, centred from y down (HUD and menus)
    void drawScoreboard();
    void drawMenu();
    void drawPauseMenu(float cx, float y);
    void drawSettings(float x, float y, float w);
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
    int annLeader_ = -2;   // FFA: who leads (-1 nobody yet, -2 not counting)

    GLFWwindow* win_ = nullptr;
    Settings cfg_;
    LaunchOptions opts_;
    Renderer r_;
    // models (models.h): the box list being drawn (reused every frame), and the owner's glTF overrides (null: none)
    ModelParts parts_;
    std::unique_ptr<ModelMesh> modelOv_[MODEL_KINDS][kProjectileModels];
    const ModelMesh* modelOverride(int kind, int i) const { return i >= 0 && i < kProjectileModels ? modelOv_[kind][i].get() : nullptr; }
    void overrideTints(int kind, const ModelPalette& pal, Vec3* tint) const;   // tint[TINT_COUNT]
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
    void benchFrame(double dt);
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
    uint8_t lastCmdWeapon_ = 255;   // the weapon the last command asked for (switching with fire held locks fire)
    // custom modes: the one being edited, its tab, and the rules arriving from a server in pieces
    CustomRules editRules_;
    std::string editOrigName_;
    int customTab_ = 0;
    std::vector<std::string> rulesParts_;
    void drawCustomModeEditor(float cx, float y);
    void drawInfo(float cx, float y);
    void drawDemos(float cx, float y);      // the demos folder: newest first, click to watch
    void startRecording();
    bool playDemo(const std::string& path);
    std::vector<std::string> demoFiles_;
    std::vector<std::string> demoNotes_;   // per demo: "" (plays), or which version recorded it / converting
    double demoListAt_ = -1;
    int infoTab_ = 0;
    float infoScroll_ = 0;
    int demoScroll_ = 0, joinScroll_ = 0;   // first row shown in the demo list / server list (mouse wheel)
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
    void drawLeaderboard(float cx, float y);
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
    bool edGroupMenu_ = false, edExportAsk_ = false; // IMPORT GROUP's list of saved groups; EXPORT's "make it a group first?"
    std::vector<std::string> edGroupFiles_;
    float edSpeed_ = 12;
    Vec3 edCam_;
    bool edLook_ = false, edDirty_ = false, edBaked_ = false;
    bool edMeshDirty_ = true;      // the map's boxes changed: rebuild the renderer's static batch (PERF-12)
    float edMeshOutline_ = -1;     // the outline width that batch was built with
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
    void edDo(const std::string& op, const std::string& inverse, bool coalesce = false, bool undoable = true);
    void edUndoLast();
    void edChanged();
    void edNotify(const std::string& s);
    bool edSelValid() const;
    AABB edBounds(const Map& m, EdSel sel) const;
    void edModify(const std::function<void(Map&)>& f, bool coalesce);
    void edDelete();
    void edDuplicate();
    enum { ED_ROT_Y, ED_ROT_X, ED_ROT_Z, ED_MIR_X, ED_MIR_Y, ED_MIR_Z };
    void edTransform(int op);
    bool edTransformed(int op, Map& tmp, std::vector<EdSel>& sel) const;   // the map with the selection turned (false: nothing to turn)
    UiExp edTransformExp(int op);   // ROTATE / MIRROR's expectation: the map changes, or (it's symmetric that way) just the notice
    void edAdd(int kind, int variant);
    void edGroupSelection();            // GROUP: the selected boxes / groups become one custom group
    void edUngroup();                   // UNGROUP: the selected group's boxes become plain boxes
    bool edPlaceGroup(const std::string& text); // a "quark-group 1" text, added at the screen center
    void edImportGroup();               // clipboard first (saved to groups/), else the groups menu
    void edExportGroup();               // the selected group to groups/<new file> + clipboard, then open the folder
    void drawEditorGroupMenu(float x, float y, float w);
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
    bool localFireHeld_ = false, scopeOverlay_ = false, localFireLocked_ = false;
    double localFireQueued_ = -1;
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

    // effects + feeds
    struct Span { size_t at, len; Color color; };   // part of a feed line in its own colour (a player's chosen name colour)
    struct Feed { std::string text; Color color; double until; int crown = 0; bool player = false; std::vector<Span> spans{}; };   // crown: the sender's leaderboard place (chat); player: a player's chat line (hidden while chat is off)
    std::deque<Feed> killFeed_, chatFeed_, notices_;
    void drawFeedText(float x, float y, float ts, const Feed& f);
    std::deque<Feed> chatLog_;   // the chat history shown while typing (Enter)
    void addChat(const Feed& f) { chatFeed_.push_back(f); chatLog_.push_back(f); while (chatFeed_.size() > 8) chatFeed_.pop_front(); while (chatLog_.size() > 30) chatLog_.pop_front(); }
    std::string centerText_;
    double centerUntil_ = 0, hitUntil_ = 0, damageUntil_ = 0, recoil_ = 0, localNextFire_ = 0;
    HitSoundGate hitGate_;   // the hitsound: one per moment, the heaviest (audio.h)
    double localKnifeBack_ = 0, localSyringeBack_ = 0;   // predicted: a thrown knife / syringe is away (disabled) until then
    // seconds until a thrown knife / syringe is back (0 = have it): the prediction, or the server's word (snapshot)
    double weaponAwayLeft(int w) const {
        double local = w == WPN_KNIFE ? localKnifeBack_ : w == WPN_SYRINGE ? localSyringeBack_ : 0;
        int snap = !net_.hasSnapshot() ? 0 : w == WPN_KNIFE ? net_.latest().knifeAway : w == WPN_SYRINGE ? net_.latest().syringeAway : 0;
        return std::max(local - time_, snap * 0.1);
    }
    int lastHealth_ = 0;
    struct Tracer { Vec3 a, b; double until; };
    std::vector<Tracer> tracers_;
    struct Explosion { Vec3 at; double start; float size = 1; bool bubble = false; };
    std::vector<Explosion> explosions_;
    std::vector<ProjectileSnap> lastProjectiles_;
    std::vector<ProjectileSnap> projScratch_;   // (frame: this frame's projectiles, built here then swapped in)
    std::vector<GameEvent> evScratch_;          // (handleEvents)
    // your own projectiles start drawn at your weapon model's muzzle and blend onto their real path
    struct ProjStart { uint16_t id; double seen; Vec3 offset; };
    std::vector<ProjStart> projStarts_, projStartsKeep_;
    std::string feedPart_;   // (drawFeedText)
    double frameDt_ = 0;     // this frame's dt (frame)
    bool polledEarly_ = false;   // preInput read the network this frame
    std::vector<std::pair<std::string, bool>> infoRows_;   // (drawInfo: the page wrapped to the panel, for this tab and size)
    int infoRowsTab_ = -1;
    float infoRowsW_ = 0, infoRowsTs_ = 0;
    std::string perfTick_;   // (drawPerf: the version / server line, made with the numbers)
    struct JoiningThread { std::thread t; ~JoiningThread() { if (t.joinable()) t.join(); } };
    JoiningThread raceWriter_;  // (onRaceSplit: appends a finished run to race_times.log; joined however the game ends)
    Vec3 muzzlePoint(int weapon, const Vec3& eye) const;   // world point of the weapon model's barrel tip (HUD layout)
    double shotAt_ = -1;
    bool scoped_ = false;
    double laserUntil_ = 0;            // local laser beam visible until
    bool remoteFiring_[kMaxPlayers] = {};
    float viewFov() const;
    void sceneSettings();   // the renderer's world look from cfg_
    static const Settings& defaultSettings() { static const Settings d; return d; }   // (sliders' right-click: back to these)
    float edPanelX_ = 1e9f;   // the editor's side panel starts here (a right-click there is the UI's, not mouse look)

    // F3 performance overlay: frame-time percentiles (lows matter, averages hide stutter)
    bool showPerf_ = false;
    bool hudBuilt_ = false;            // drawWorld built the HUD (and F3 / F4) before its late read this frame
    bool perfMini_ = false;            // F4: only the mouse to photon number (drawPerfMini)
    bool perfOn() const { return showPerf_ || perfMini_; }   // (the pipeline graph's measuring runs)
    std::string perfMiniText_;
    std::vector<float> frameTimes_ = std::vector<float>(2048, 0.0f);
    size_t frameIdx_ = 0, frameCount_ = 0;
    double perfNextUpdate_ = 0;
    std::string perfText_;
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
    int desiredWeapon_ = -1;
    // sound
    Audio audio_;
    void updateSounds(const NetClient::PlayerArray& players);
    MoveState prevMove_;
    double prevSoundAt_ = 0;   // (updateSounds: when it last ran, for the frame time)
    bool prevAlive_ = false;
    float stepDist_ = 0;
    struct RemoteAudio { Vec3 pos; bool valid = false; float stepDist = 0; bool hooked = false; double nextLaser = 0, nextShot = 0, nextHurt = 0; int health = 0; };
    RemoteAudio remoteAudio_[kMaxPlayers];
    // rebinding
    bool isDown(int action) const;
    void drawKeyOverlay(float W, float H, float s);
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
    void drawChatBox(float W, float H, float s, float ts);
    void toggleChat();                     // TOGGLE CHAT key: other players' chat off / on (the DISABLE CHAT setting), with a notice
    void drawVotePanel(float cx, float s, float ts);
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
    void drawConsole(float W, float H, float s, float ts);
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
    bool visualsOpen_ = false;
    void drawVisualSettings(float x0, float y0, float wTotal);
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
    struct DmgNum { int victim; float amount; double start; Vec3 pos; bool head = false; };   // head: a headshot (drawn red)
    std::vector<DmgNum> dmgNums_;
    int botMapIdx_ = 0;
};
