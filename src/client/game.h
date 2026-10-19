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
#include "config.h"
#include "renderer.h"
#include "ui.h"

struct GLFWwindow;

struct LaunchOptions {
    std::string connect;        // --connect addr
    int hostMode = -1;          // --host mode
    std::string hostMap;        // --map name
    std::string screenshot;     // --screenshot path (taken after --shot-delay seconds, then quit)
    float shotDelay = 3.0f;
    float spawnYaw = -1000;     // --yaw deg (debug camera direction for screenshots)
    float spawnPitch = 0;       // --pitch deg
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

private:
    enum class Screen { Main, Join, Host, Settings, Connecting, InGame };
    void setScreen(Screen s);
    void startHost();
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

    // view / input
    float yaw_ = 0, pitch_ = 0;
    double lastMx_ = 0, lastMy_ = 0;
    bool mouseCaptured_ = false, firstMouse_ = true;
    bool fireLatched_ = false;
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
};
