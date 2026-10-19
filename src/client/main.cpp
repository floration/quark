// quark game client entry point.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "game.h"
#include "gl.h"

static Game* g_game = nullptr;

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    LaunchOptions opts;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--connect") opts.connect = next();
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
        else if (a == "--pitch") opts.spawnPitch = (float)atof(next().c_str());
        else if (a == "-h" || a == "--help") {
            printf("quark [--connect ip:port] [--host mode --map name]\n");
            return 0;
        }
    }

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
    std::string err;
    if (!game.init(win, opts, &err)) {
        fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }
    g_game = &game;
    glfwSetCharCallback(win, [](GLFWwindow*, unsigned cp) { g_game->onChar(cp); });
    glfwSetKeyCallback(win, [](GLFWwindow*, int k, int, int a, int m) { g_game->onKey(k, a, m); });
    glfwSetMouseButtonCallback(win, [](GLFWwindow*, int b, int a, int) { g_game->onMouseButton(b, a); });
    glfwSetCursorPosCallback(win, [](GLFWwindow*, double x, double y) { g_game->onCursor(x, y); });
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int fw, int fh) { g_game->onResize(fw, fh); });
    glfwSetScrollCallback(win, [](GLFWwindow*, double, double dy) { g_game->onScroll(dy); });

    double last = timeNow();
    double frameStart = timeNow();
    while (!glfwWindowShouldClose(win) && !game.quitRequested()) {
        // Frame cap: wait *before* reading input, so the frame shows the freshest input possible.
        if (game.maxFps() > 0) {
            double target = frameStart + 1.0 / game.maxFps();
            while (timeNow() < target - 0.0015) sleepMs(1);
            while (timeNow() < target) {}
        }
        frameStart = timeNow();
        game.beforeInput();
        glfwPollEvents();
        double now = timeNow();
        double dt = now - last;
        last = now;
        game.frame(dt);
        glfwSwapBuffers(win);
        game.afterSwap();
    }
    game.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    netShutdown();
    return 0;
}
