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
        else if (a == "--map") opts.hostMap = next();
        else if (a == "--screenshot") opts.screenshot = next();
        else if (a == "--shot-delay") opts.shotDelay = (float)atof(next().c_str());
        else if (a == "--yaw") opts.spawnYaw = (float)atof(next().c_str());
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
    GLFWmonitor* mon = pre.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    int w = pre.width, h = pre.height;
    if (mon) { const GLFWvidmode* vm = glfwGetVideoMode(mon); w = vm->width; h = vm->height; }
    GLFWwindow* win = glfwCreateWindow(w, h, "quark", mon, nullptr);
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

    double last = timeNow();
    while (!glfwWindowShouldClose(win) && !game.quitRequested()) {
        glfwPollEvents();
        double now = timeNow();
        double dt = now - last;
        last = now;
        game.frame(dt);
        glfwSwapBuffers(win);
        // optional frame cap (sleep most of the remainder, spin the rest for accuracy)
        if (game.maxFps() > 0) {
            double target = last + 1.0 / game.maxFps();
            while (timeNow() < target - 0.002) sleepMs(1);
            while (timeNow() < target) {}
        }
    }
    game.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    netShutdown();
    return 0;
}
