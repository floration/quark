// Rebindable controls. Every action has two slots; a slot holds a key, a mouse button or a
// scroll direction.
#pragma once
#include <string>

enum Action {
    A_FORWARD = 0, A_BACK, A_LEFT, A_RIGHT, A_JUMP, A_CROUCH, A_FIRE, A_ALTFIRE, A_USE, A_DASH, A_GRAPPLE,
    A_WEAPON1, A_WEAPON2, A_WEAPON3, A_WEAPON4, A_WEAPON5, A_WEAPON6, A_WEAPON7, A_WEAPON8,
    A_NEXTWEAPON, A_PREVWEAPON, A_SCORES, A_CHAT, A_VOTEYES, A_VOTENO, A_PERF, A_SCREENSHOT, A_VOTESTART,
    A_WEAPON9,  // slot 9 (the knife)
    A_WEAPON10, // slot 10, key 0 (the syringe)
    A_QUIP,     // say a random line from the voice pack, out loud where you stand (G)
    A_TOGGLECHAT, // hide / show other players' chat (Settings::chatOff); unbound by default
    A_PERFMINI, // F4: only the F3 graph's mouse to photon number (to see what F3's text and graph cost)
    A_COUNT
};

// Input codes: GLFW key codes as-is, mouse buttons from kMouseCode, scroll from kWheelUp/kWheelDown.
constexpr int kMouseCode = 1000;
constexpr int kWheelUp = 2000;
constexpr int kWheelDown = 2001;

struct Binds {
    int code[A_COUNT][2] = {};
    void setDefaults();
    bool has(int action, int code) const { return code != 0 && (this->code[action][0] == code || this->code[action][1] == code); }
};

const char* actionName(int action);
inline int slotAction(int slot) { return slot < 8 ? A_WEAPON1 + slot : slot == 8 ? A_WEAPON9 : A_WEAPON10; } // weapon slot -> its key action
std::string inputName(int code);
