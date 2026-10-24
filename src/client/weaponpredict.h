// The weapon you hold, predicted: a weapon key switches the view model, HUD, scope and local fire feedback on the
// next tick instead of when a snapshot confirms the switch (one round trip later). The server's switch rules
// (Server::runPlayerCmd) run here on what the client knows; the server stays authoritative and its word wins once
// it has run the command that asked for the switch (the snapshot's ackCmdSeq). Header-only so quark_tests can
// check it against a real server.
#pragma once
#include "../game/gamedefs.h"
#include "../game/pmove.h"
#include <cstdint>

// What the client knows about its own weapon state, as of now.
struct WeaponView {
    int serverWeapon = 0;    // the latest snapshot's selfWeapon (after the server ran ackSeq)
    uint32_t ackSeq = 0;     // the latest snapshot's ackCmdSeq
    bool alive = false;
    uint16_t mask = 0;       // the mode's weapon pool (ModeState::weaponMask = Server::weaponMask; 0 = all)
    uint16_t away = 0;       // bit w: weapon w is away (a thrown knife / syringe, predicted or the server's word)
};

// Server::runPlayerCmd's switch, for one command asking for `want` (-1 / kNoWeaponChange: keep) while holding `held`.
// Dead: no switch (the server returns before it). Frozen: switches (the server only stops the shots).
inline int switchedWeapon(int held, int want, bool alive, uint16_t mask, uint16_t away) {
    if (!alive) return held;
    auto allowed = [&](int w) { return !mask || (mask >> w & 1); };       // Server::weaponAllowed
    auto isAway = [&](int w) { return (away >> w & 1) != 0; };
    int other = -1;                                                       // otherWeapon(): first in slot order
    for (int k = 0; k < WPN_COUNT && other < 0; k++) { int w = kWeaponSlots[k]; if (!isAway(w) && allowed(w)) other = w; }
    int w = held;
    if (want >= 0 && want < WPN_COUNT && allowed(want) && !(isAway(want) && other >= 0)) w = want;
    if (w >= 0 && w < WPN_COUNT && isAway(w) && other >= 0) w = other;   // a thrown knife / syringe: the next weapon
    return w;
}

// The weapon key's choice, sent in every command until the server has run one of them, and the weapon each command
// predicts. One per client; no allocation, a few compares per tick.
struct WeaponPredictor {
    int desired = -1;        // what the keys asked for (-1: nothing pending)
    bool sent = false;       // a command carrying `desired` has gone out...
    uint32_t firstSeq = 0;   // ...this one, the first
    int held = -1;           // the weapon the last command predicts (-1: none yet / dead: the snapshot's)

    void want(int w) { if (w != desired) { desired = w; sent = false; } }
    void reset() { desired = -1; sent = false; held = -1; }
    // The server has run a command asking for `desired` (or the connection started over: seqs from 1 again), so its
    // snapshot says what you hold now: accepted (it matches) or refused (it doesn't, and the snapshot wins).
    bool resolved(uint32_t ackSeq, uint32_t nextSeq) const {
        return sent && ((int32_t)(ackSeq - firstSeq) >= 0 || (int32_t)(nextSeq - firstSeq) <= 0);
    }
    // Before a command goes out (it gets seq `nextSeq`): the pending choice goes into it.
    void fill(UserCmd& c, uint32_t ackSeq, uint32_t nextSeq) {
        if (desired >= 0 && resolved(ackSeq, nextSeq)) desired = -1;
        if (desired < 0) return;
        c.weapon = (uint8_t)desired;
        if (!sent) { sent = true; firstSeq = nextSeq; }
    }
    // The command as it goes out (buttons final): the weapon the server will hold after running it. *lockFire: it
    // switches with fire / alt-fire held, which the server answers by locking fire until both are let go.
    int run(const UserCmd& c, const WeaponView& v, bool* lockFire = nullptr) {
        if (lockFire) *lockFire = false;
        if (!v.alive) { held = -1; return v.serverWeapon; }
        int want = c.weapon < WPN_COUNT ? (int)c.weapon : -1;
        // Base: the server's word plus this command's ask. Commands still in flight all carry the same ask (fill),
        // so the newest one's result is the prediction; once resolved there's no ask and the snapshot stands.
        int w = switchedWeapon(v.serverWeapon, want, true, v.mask, v.away);
        int before = held >= 0 ? held : v.serverWeapon;
        if (lockFire && want >= 0 && w == want && w != before && (c.buttons & (BTN_FIRE | BTN_ALTFIRE))) *lockFire = true;
        held = w;
        return w;
    }
    // A throw this command predicts (knife / syringe away from now: `v.away` has it): the server switches to the next
    // weapon in the same command, so this command's prediction does too.
    int thrown(const WeaponView& v) { if (v.alive && held >= 0) held = switchedWeapon(held, -1, true, v.mask, v.away); return current(v); }
    // Between ticks (drawing): the last command's prediction, or the server's word.
    int current(const WeaponView& v) const { return v.alive && held >= 0 ? held : v.serverWeapon; }
};
