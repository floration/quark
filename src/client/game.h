// The client application: menus, hosting, connection, input, world/HUD rendering.
#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "../game/netclient.h"
#include "../game/server.h"
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
    bool debugPicker = false;
    bool debugPause = false, debugVote = false; // --pause / --votemenu   // --picker: open the host menu's map list
    bool debugSettings = false; // --settings: open the settings screen in game
    bool debugControls = false; // --controls
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
    enum class Screen { Main, Join, Host, Settings, Connecting, InGame, VsBots };
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
    std::vector<std::string> maps_;
    struct MapInfo { std::string title; int preferredMode = -1; uint32_t modes = 0; }; // modes: bit per supported mode
    std::vector<MapInfo> mapInfo_;
    enum { PICK_NONE = 0, PICK_HOST_MAP, PICK_HOST_MODE, PICK_BOT_MAP, PICK_BOT_MODE, PICK_LIVE_MAP, PICK_LIVE_MODE };
    int liveMode_ = 0, liveMap_ = 0;  // host's in-game map/mode change
    bool voteMenu_ = false;
    void drawVoteMenu(float cx, float y);
    int picker_ = PICK_NONE;    // full list of maps / modes open over the host or vs-bots menu
    void drawPicker(float cx, float y);

    // view / input
    float yaw_ = 0, pitch_ = 0;
    double lastMx_ = 0, lastMy_ = 0;
    bool mouseCaptured_ = false, firstMouse_ = true;
    bool fireLatched_ = false;
    bool localFireHeld_ = false, scopeOverlay_ = false;
    double localFireQueued_ = -1;
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
    struct Explosion { Vec3 at; double start; };
    std::vector<Explosion> explosions_;
    std::vector<ProjectileSnap> lastProjectiles_;
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
    struct RemoteAudio { Vec3 pos; bool valid = false; float stepDist = 0; bool hooked = false; double nextLaser = 0; };
    RemoteAudio remoteAudio_[kMaxPlayers];
    // rebinding
    bool isDown(int action) const;
    int bestMapFor(int mode) const;
    bool pressInput(int code);
    void drawControls(float x, float y, float w);
    void drawCrosshairSettings(float x, float y, float w);
    void drawCrosshair(float cx, float cy, float scale, bool hit);
    bool controlsOpen_ = false;
    bool crosshairOpen_ = false;
    int captureAction_ = -1, captureSlot_ = 0;
    std::string voteText_;
    struct DmgNum { int victim; float amount; double start; Vec3 pos; };
    std::vector<DmgNum> dmgNums_;
    int botMapIdx_ = 0;
};
