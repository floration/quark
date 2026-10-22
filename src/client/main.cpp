// quark game client entry point.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../common/profile.h"
#include <cstdio>
#include <cstring>
#include <string>
#include "game.h"
#include "gl.h"
#include "../launcher/selfupdate.h"

static Game* g_game = nullptr;

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    LaunchOptions opts;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data-dir") setUserDataDir(next()); // settings, key, demos, your maps (default: per-user folder)
        else if (a == "--connect") opts.connect = next();
        else if (a == "--host") opts.hostMode = modeFromName(next().c_str());
        else if (a == "--vsbots") opts.vsBotsMode = modeFromName(next().c_str());
        else if (a == "--map") opts.hostMap = next();
        else if (a == "--screenshot") opts.screenshot = next();
        else if (a == "--shot-delay") opts.shotDelay = (float)atof(next().c_str());
        else if (a == "--yaw") opts.spawnYaw = (float)atof(next().c_str());
        else if (a == "--weapon") opts.debugWeapon = atoi(next().c_str());
        else if (a == "--scope") opts.debugScope = true;
        else if (a == "--fire") opts.debugFire = true;
        else if (a == "--perf") opts.debugPerf = true;
        else if (a == "--settings") opts.debugSettings = true;
        else if (a == "--backdrop") opts.backdrop = next();
        else if (a == "--cam") { // --cam x y z yawDeg pitchDeg: fixed camera over a --backdrop map (map shots)
            opts.camSet = true;
            for (int k = 0; k < 3; k++) opts.camPos[k] = (float)atof(next().c_str());
            opts.camYaw = (float)atof(next().c_str()); opts.camPitch = (float)atof(next().c_str());
        }
        else if (a == "--orbit") opts.backdropAngle = (float)atof(next().c_str());
        else if (a == "--pause") opts.debugPause = true;
        else if (a == "--votemenu") { opts.debugPause = true; opts.debugVote = true; }
        else if (a == "--picker") opts.debugPicker = atoi(next().c_str());
        else if (a == "--crosshair") { opts.debugSettings = true; opts.debugCrosshair = true; }
        else if (a == "--hudlayout") { opts.debugSettings = true; opts.debugHudLayout = true; }
        else if (a == "--models") opts.debugModels = true;
        else if (a == "--timetrial") opts.timeTrial = true;
        else if (a == "--racestats") { opts.debugLeaderboard = true; opts.debugRaceStats = true; }
        else if (a == "--widgets") { opts.debugSettings = true; opts.debugWidgets = true; }
        else if (a == "--controls") { opts.debugSettings = true; opts.debugControls = true; }
        else if (a == "--editor") opts.editMap = next();
        else if (a == "--leaderboard") opts.debugLeaderboard = true;
        else if (a == "--demo") opts.demo = next();   // watch a demo file
        else if (a == "--customshot") opts.debugCustom = atoi(next().c_str()) + 1;   // the custom mode editor on that tab (screenshots)
        else if (a == "--info") opts.debugInfo = atoi(next().c_str()) + 1;   // the info screen on that tab (screenshots)
        else if (a == "--menushot") opts.menuShot = true;
        else if (a == "--edfocus") opts.edFocus = true;
        else if (a == "--chatshot") opts.chatShot = next();
        else if (a == "--consoleshot") opts.consoleShot = next();
        else if (a == "--bench") opts.bench = atof(next().c_str());
        else if (a == "--textheavy") opts.textHeavy = true;
        else if (a == "--edsel") opts.editSel = atoi(next().c_str());
        else if (a == "--edscript") opts.editScript = next();
        else if (a == "--editsession") { opts.editMap = next(); opts.editSession = true; }
        else if (a == "--pitch") opts.spawnPitch = (float)atof(next().c_str());
        else if (a == "-h" || a == "--help") {
            printf("quark [--connect ip:port] [--host mode --map name] [--data-dir DIR]\n");
            return 0;
        }
    }

    // started from a launcher install: bring every older launcher (and its shortcuts) up to the one in this package.
    // Not in automated runs (screenshots, benchmarks, scripted editor sessions): they start and quit in seconds and
    // must not touch the player's launchers. Whatever way main returns, the thread is stopped between steps (never
    // inside a swap) and joined, waiting at most 2 s (selfupdate::finishFromGame).
    bool automated = !opts.screenshot.empty() || opts.bench > 0 || !opts.editScript.empty() || opts.textHeavy ||
                     opts.menuShot || !opts.chatShot.empty() || !opts.consoleShot.empty() || opts.debugPerf;
    if (!automated) selfupdate::startFromGame(argv[0]);
    struct LauncherUpdateJoin { ~LauncherUpdateJoin() { selfupdate::finishFromGame(2.0); } } launcherUpdateJoin;
    netInit();
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE); // required on macOS
    glfwWindowHint(GLFW_SAMPLES, 0);

    Settings pre;
    pre.load();
    // Created windowed; Game::init switches to the configured display mode.
    GLFWwindow* win = glfwCreateWindow(pre.width, pre.height, "quark", nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "could not create an OpenGL 3.3 window - update your graphics driver\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    if (const char* missing = qglLoad((QGLGetProc)glfwGetProcAddress)) {
        fprintf(stderr, "OpenGL function missing: %s\n", missing);
        return 1;
    }
    printf("GL %s / %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

    Game game;
    g_game = &game; // callbacks are live before init, so the initial fullscreen resize isn't missed
    glfwSetCharCallback(win, [](GLFWwindow*, unsigned cp) { g_game->onChar(cp); });
    glfwSetKeyCallback(win, [](GLFWwindow*, int k, int, int a, int m) { g_game->onKey(k, a, m); });
    glfwSetMouseButtonCallback(win, [](GLFWwindow*, int b, int a, int) { g_game->onMouseButton(b, a); });
    glfwSetCursorPosCallback(win, [](GLFWwindow*, double x, double y) { g_game->onCursor(x, y); });
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int fw, int fh) { g_game->onResize(fw, fh); });
    glfwSetScrollCallback(win, [](GLFWwindow*, double, double dy) { g_game->onScroll(dy); });

    std::string err;
    if (!game.init(win, opts, &err)) {
        fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }
    double last = timeNow();
    double frameStart = timeNow();
    while (!glfwWindowShouldClose(win) && !game.quitRequested()) {
        // Frame cap: wait *before* reading input, so the frame shows the freshest input possible.
        if (game.maxFps() > 0) {
            double period = 1.0 / game.maxFps();
            double target = frameStart + period;
            while (timeNow() < target - 0.0015) sleepMs(1);
            while (timeNow() < target) {}
            frameStart = std::max(target, timeNow() - period); // keep the cadence (no slow drift below the cap), but don't try to catch up after a hitch
        } else {
            frameStart = timeNow();
        }
        game.beforeInput();
        { QZONE("poll events"); glfwPollEvents(); }
        double now = timeNow();
        double dt = now - last;
        last = now;
        game.frame(dt);
        { QZONE("swap"); glfwSwapBuffers(win); }
        { QZONE("after swap (low latency wait)"); game.afterSwap(); }
        QFRAME();
    }
    game.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    netShutdown();
    return 0;
}
