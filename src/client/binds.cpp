#include "binds.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cctype>

void Binds::setDefaults() {
    for (auto& c : code) c[0] = c[1] = 0;
    auto set = [&](int a, int k0, int k1 = 0) { code[a][0] = k0; code[a][1] = k1; };
    set(A_FORWARD, GLFW_KEY_W);
    set(A_BACK, GLFW_KEY_S);
    set(A_LEFT, GLFW_KEY_A);
    set(A_RIGHT, GLFW_KEY_D);
    set(A_JUMP, GLFW_KEY_SPACE);
    set(A_FIRE, kMouseCode + GLFW_MOUSE_BUTTON_LEFT);
    set(A_ALTFIRE, kMouseCode + GLFW_MOUSE_BUTTON_RIGHT);
    set(A_USE, GLFW_KEY_E);
    set(A_DASH, GLFW_KEY_LEFT_SHIFT);
    set(A_GRAPPLE, GLFW_KEY_Q);
    for (int w = 0; w < 8; w++) set(A_WEAPON1 + w, GLFW_KEY_1 + w);
    set(A_WEAPON9, GLFW_KEY_9);
    set(A_WEAPON10, GLFW_KEY_0);
    set(A_NEXTWEAPON, kWheelDown);
    set(A_PREVWEAPON, kWheelUp);
    set(A_SCORES, GLFW_KEY_TAB);
    set(A_CHAT, GLFW_KEY_ENTER, GLFW_KEY_KP_ENTER);
    set(A_VOTEYES, GLFW_KEY_F1);
    set(A_VOTENO, GLFW_KEY_F2);
    set(A_PERF, GLFW_KEY_F3);
    set(A_SCREENSHOT, GLFW_KEY_F12);
    set(A_VOTESTART, GLFW_KEY_F5);
    set(A_PERFMINI, GLFW_KEY_F4);
    set(A_QUIP, GLFW_KEY_G);
}

const char* actionName(int a) {
    static const char* n[A_COUNT] = {
        "FORWARD", "BACK", "LEFT", "RIGHT", "JUMP", "CROUCH", "FIRE", "ALT FIRE", "USE", "DASH", "GRAPPLE",
        "ROCKET", "RIFLE", "LASER", "SHOTGUN", "SNIPER", "STICKY", "CROSSBOW", "BUBBLE", // weapon slots 1-8
        "NEXT WEAP", "PREV WEAP", "SCOREBOARD", "CHAT", "VOTE YES", "VOTE NO", "PERF STATS", "SCREENSHOT", "VOTE START", "KNIFE", "SYRINGE", "QUIP", "TOGGLE CHAT", "LATENCY",
    };
    return a >= 0 && a < A_COUNT ? n[a] : "?";
}

std::string inputName(int c) {
    if (c == 0) return "-";
    if (c == kWheelUp) return "WHEEL UP";
    if (c == kWheelDown) return "WHEEL DN";
    if (c >= kMouseCode && c < kMouseCode + 16) {
        int b = c - kMouseCode;
        if (b == 0) return "MOUSE 1";
        if (b == 1) return "MOUSE 2";
        if (b == 2) return "MOUSE 3";
        return "MOUSE " + std::to_string(b + 1);
    }
    switch (c) {
        case GLFW_KEY_SPACE: return "SPACE";
        case GLFW_KEY_LEFT_SHIFT: return "L SHIFT";
        case GLFW_KEY_RIGHT_SHIFT: return "R SHIFT";
        case GLFW_KEY_LEFT_CONTROL: return "L CTRL";
        case GLFW_KEY_RIGHT_CONTROL: return "R CTRL";
        case GLFW_KEY_LEFT_ALT: return "L ALT";
        case GLFW_KEY_RIGHT_ALT: return "R ALT";
        case GLFW_KEY_TAB: return "TAB";
        case GLFW_KEY_ENTER: return "ENTER";
        case GLFW_KEY_KP_ENTER: return "KP ENTER";
        case GLFW_KEY_BACKSPACE: return "BACKSPACE";
        case GLFW_KEY_CAPS_LOCK: return "CAPS LOCK";
        case GLFW_KEY_UP: return "UP";
        case GLFW_KEY_DOWN: return "DOWN";
        case GLFW_KEY_LEFT: return "LEFT";
        case GLFW_KEY_RIGHT: return "RIGHT";
        case GLFW_KEY_INSERT: return "INSERT";
        case GLFW_KEY_DELETE: return "DELETE";
        case GLFW_KEY_HOME: return "HOME";
        case GLFW_KEY_END: return "END";
        case GLFW_KEY_PAGE_UP: return "PAGE UP";
        case GLFW_KEY_PAGE_DOWN: return "PAGE DOWN";
        case GLFW_KEY_ESCAPE: return "ESC";
    }
    if (c >= GLFW_KEY_F1 && c <= GLFW_KEY_F25) return "F" + std::to_string(c - GLFW_KEY_F1 + 1);
    if (c >= GLFW_KEY_KP_0 && c <= GLFW_KEY_KP_9) return "NUM " + std::to_string(c - GLFW_KEY_KP_0);
    if (const char* n = glfwGetKeyName(c, 0)) { // layout-aware name for printable keys
        std::string s(n);
        for (auto& ch : s) ch = (char)toupper((unsigned char)ch);
        return s;
    }
    if (c >= 32 && c < 127) return std::string(1, (char)c);
    return "KEY " + std::to_string(c);
}
