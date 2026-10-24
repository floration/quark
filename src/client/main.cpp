// quark game client entry point.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../common/profile.h"
#include <cstdio>
#include <cstring>
#include <string>
#include "game.h"
#include "../game/server.h"
#include "gl.h"
#include "lightmap.h"
#include "../launcher/selfupdate.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <vector>
#endif
#include "../game/demo.h"

static Game* g_game = nullptr;

int main(int argc, char** argv) {
#ifdef _WIN32
    // argv is in the ANSI code page on Windows: take the command line as UTF-16 and keep UTF-8 strings, like every
    // other path in the game (a demo / map / data dir under a non-ASCII folder), and argv[0] = this exe's real path
    // (setExecutableDir finds the shipped data next to it; the install sits under the user's own folder) (COR-5)
    static std::vector<std::string> u8args;
    static std::vector<char*> u8argv;
    if (int wn = 0; LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &wn)) {
        for (int i = 0; i < wn; i++) u8args.push_back(std::filesystem::path(w[i]).u8string());
        LocalFree(w);
        static wchar_t exe[32768];
        DWORD n = GetModuleFileNameW(nullptr, exe, 32768);
        if (n > 0 && n < 32768 && !u8args.empty()) u8args[0] = std::filesystem::path(std::wstring(exe, n)).u8string();
        for (auto& a : u8args) u8argv.push_back(&a[0]);
        u8argv.push_back(nullptr);
        if (!u8args.empty()) { argc = (int)u8args.size(); argv = u8argv.data(); }
    }
#endif
    setExecutableDir(argv[0]);
    LaunchOptions opts;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data-dir") setUserDataDir(next()); // settings, key, demos, your maps (default: per-user folder)
        else if (a == "--bake-maps") return bakeOfficialMaps(next());   // the official maps' light into DIR (packages: maps/lightcache); no window
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
        else if (a == "--pulse") opts.debugPulse = true;   // (with --fire: stuck bolt / knife screenshots)
        else if (a == "--perf") opts.debugPerf = true;
        else if (a == "--settings") opts.debugSettings = true;
        else if (a == "--backdrop") opts.backdrop = next();
        else if (a == "--cam") { // --cam x y z yawDeg pitchDeg: fixed camera over a --backdrop map (map shots), or in a match
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
        else if (a == "--rpsshot") opts.debugRps = atoi(next().c_str());   // the tiebreak panel (screenshots)
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
        else if (a == "--fonts") opts.debugFonts = 1;   // Settings > FONT open from the start (screenshots: with --menushot)
        else if (a == "--settingsmenu") opts.debugFonts = 2;   // the settings page from the main menu (screenshots)
        else if (a == "--edfocus") opts.edFocus = true;
        else if (a == "--chatshot") opts.chatShot = next();
        else if (a == "--consoleshot") opts.consoleShot = next();
        else if (a == "--bench") opts.bench = atof(next().c_str());
        else if (a == "--textheavy") opts.textHeavy = true;
        else if (a == "--edsel") opts.editSel = atoi(next().c_str());
        else if (a == "--edscript") opts.editScript = next();
        else if (a == "--editsession") { opts.editMap = next(); opts.editSession = true; }
        else if (a == "--pitch") opts.spawnPitch = (float)atof(next().c_str());
        else if (a == "--uitest") {   // the UI test (docs/UI_TEST.md): [report.json]
            opts.uiTest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') opts.uiTestReport = next();
        }
        else if (a == "--uitest-only") { opts.uiTest = true; opts.uiTestOnly = next(); }   // scenarios a,b,c
        else if (a == "--uitest-path") { opts.uiTest = true; opts.uiTestPath = next(); }   // screens under this path
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
                     opts.menuShot || !opts.chatShot.empty() || !opts.consoleShot.empty() || opts.debugPerf || opts.uiTest;
    if (opts.uiTest) uiTestPrepare(opts);   // (its own data folder and settings: before anything reads them)
    if (!automated) selfupdate::startFromGame(argv[0]);
    struct LauncherUpdateJoin { ~LauncherUpdateJoin() { selfupdate::finishFromGame(2.0); } } launcherUpdateJoin;
    // demos recorded by an older version: converted to this one in the background (originals in demos/old/<version>/)
    if (!automated) demo::startUpgrade(userDataPath("demos"), userDataPath("versions.txt"));
    if (!automated)   // your maps written in an older map format: converted (originals in maps/old/v<N>/)
        for (auto& l : upgradeMapDir(userMapsDir()).log) fprintf(stderr, "maps: %s\n", l.c_str());
    struct DemoUpgradeJoin { ~DemoUpgradeJoin() { demo::finishUpgrade(5.0); } } demoUpgradeJoin;
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
    glfwSetWindowFocusCallback(win, [](GLFWwindow*, int f) { g_game->onFocus(f != 0); });
    glfwSetCursorPosCallback(win, [](GLFWwindow*, double x, double y) { g_game->onCursor(x, y); });
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int fw, int fh) { g_game->onResize(fw, fh); });
    glfwSetScrollCallback(win, [](GLFWwindow*, double, double dy) { g_game->onScroll(dy); });

    std::string err;
    if (!game.init(win, opts, &err)) {
        fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }
    if (opts.uiTest) {   // the UI test drives the frames itself
        int rc = runUiTest(game, win, opts);
        game.shutdown();
        glfwDestroyWindow(win);
        glfwTerminate();
        Server::waitBackground(3.0);
        netShutdown();
        return rc;
    }
    double last = timeNow();
    double frameStart = timeNow();
    while (!glfwWindowShouldClose(win) && !game.quitRequested()) {
        game.pipeMark(Game::PM_FRAME);   // (F3's pipeline graph: the frame's parts)
        // Frame cap: wait *before* reading input, so the frame shows the freshest input possible.
        if (game.maxFps() > 0) {
            double period = 1.0 / game.maxFps();
            double target = frameStart + period;
            waitUntil(target);
            frameStart = std::max(target, timeNow() - period); // keep the cadence (no slow drift below the cap), but don't try to catch up after a hitch
        } else {
            frameStart = timeNow();
        }
        game.pipeMark(Game::PM_CAP);
        game.beforeInput();
        game.pipeMark(Game::PM_FENCE);
        game.preInput();   // (the network: before the mouse is read)
        game.pipeMark(Game::PM_NET);
        game.readMouse();   // (raw mouse reports in one read; GLFW's poll gets the rest)
        { QZONE("poll events"); glfwPollEvents(); }
        game.pipeMark(Game::PM_POLL);
        double now = timeNow();
        double dt = now - last;
        last = now;
        game.frame(dt);
        game.beforeSwap();   // (latent sync: the finished frame held until the vertical blank)
        game.pipeMark(Game::PM_SWAP0);
        { QZONE("swap"); glfwSwapBuffers(win); }
        game.pipeMark(Game::PM_SWAP1);
        { QZONE("after swap (low latency wait)"); game.afterSwap(); }
        QFRAME();
    }
    game.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    Server::waitBackground(3.0);   // (window gone: a hosted game's UPnP / list removal may finish, FINAL_REVIEW 2.10)
    netShutdown();
    return 0;
}
