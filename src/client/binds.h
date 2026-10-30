// Rebindable controls. Every action has three slots (two until 1.19.4); a slot holds a key, a mouse button or a
// scroll direction.
#pragma once
#include <string>

enum Action {
    A_FORWARD = 0, A_BACK, A_LEFT, A_RIGHT, A_JUMP, A_CROUCH, A_FIRE, A_ALTFIRE, A_USE, A_DASH, A_GRAPPLE,
    A_WEAPON1, A_WEAPON2, A_WEAPON3, A_WEAPON4, A_WEAPON5, A_WEAPON6, A_WEAPON7, A_WEAPON8,
    A_NEXTWEAPON, A_PREVWEAPON, A_SCORES, A_CHAT, A_VOTEYES, A_VOTENO, A_PERF, A_SCREENSHOT, A_VOTESTART,
    A_WEAPON9,  // slot 9 (the knife)
    A_WEAPON10, // slot 10, key 0 (the syringe)
    A_QUIP,     // say a random quip (never a call-out), out loud where you stand (G); Settings QUIPS / QUIPS VOLUME
    A_TOGGLECHAT, // hide / show other players' chat (Settings::chatOff); unbound by default
    A_PERFMINI, // F4: only the F3 graph's mouse to photon number (to see what F3's text and graph cost)
    A_RACERESET, // race / surf (R): back to your last checkpoint; twice quickly: the start (1.19.4, owner)
    A_TEAMCHAT,  // chat to your team only (unbound by default)
    A_LASTWEAPON, // back to the weapon held before this one (unbound by default)
    A_SPECNEXT, A_SPECPREV,   // spectating / dead: the next / previous player to watch (a click still does next)
    A_SAVEPOS, A_LOADPOS,   // practice (the host alone with bots): save your spot / go back to it (unbound by default)
    A_HIDEHUD,   // hide / show the HUD (a clean screen; the crosshair kept unless HIDE HUD KEEPS CROSSHAIR is off); unbound
    A_VOLUP, A_VOLDOWN, A_MUTE,           // master volume up / down 5%, mute / unmute (1.19.4 batch 3; unbound)
    A_RECORD, A_JOINTEAM, A_SPECTATE,     // start / stop a demo, play (team modes: the other team), spectate (unbound)
    A_COUNT
};
static_assert(A_COUNT <= 64, "Game::testHeld_ holds a bit per action");

// Input codes: GLFW key codes as-is, mouse buttons from kMouseCode, scroll from kWheelUp/kWheelDown.
constexpr int kMouseCode = 1000;
constexpr int kWheelUp = 2000;
constexpr int kWheelDown = 2001;

constexpr int kBindSlots = 3;

struct Binds {
    int code[A_COUNT][kBindSlots] = {};
    void setDefaults();
    bool has(int action, int code) const {
        if (code == 0) return false;
        for (int k = 0; k < kBindSlots; k++) if (this->code[action][k] == code) return true;
        return false;
    }
};

const char* actionName(int action);
inline int slotAction(int slot) { return slot < 8 ? A_WEAPON1 + slot : slot == 8 ? A_WEAPON9 : A_WEAPON10; } // weapon slot -> its key action
std::string inputName(int code);
