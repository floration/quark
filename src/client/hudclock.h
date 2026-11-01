// The HUD's live-phase line (under the mode title): the round clock, or what stands in for it in a mode without
// one. Header-only so quark_tests can check it against what a real server sends, mode by mode.
#pragma once
#include "../game/gamedefs.h"
#include "../game/protocol.h"
#include <cmath>
#include <cstddef>
#include <cstdio>

// Writes the line into out (a stack buffer, no allocation; 32 bytes is plenty; empty = nothing to show).
//  - VS / duel / 1v1 / tourney rounds have no round clock: their timeLeft is the stalemate drain countdown.
//  - KOTH has no clock either (first to the target; the server sends timeLeft 0): the target.
//  - Race and surf count the run time up: they have no clock (owner, 2026-10-31).
//  - Overtime: its countdown (FFA's overtime is sudden death: next kill wins, no clock).
//  - Bomb, once planted: the fuse is the clock (the round clock no longer ends the round).
//  - Every other mode is timed (FFA, tag's fuse, hide, bomb, payload, both-ways payload, points, race): its clock.
//    A live round with timeLeft 0 has no clock running (phaseEnd 0 on the server), so it shows nothing.
// raceRun: seconds since this race round went live (the client's own stopwatch; race has no clock), < 0 unknown.
inline void hudLiveLine(const ModeState& ms, char* out, size_t n, float raceRun = -1.0f) {
    auto clock = [&](const char* pre, float sec) {
        int t = (int)std::ceil(sec > 0 ? sec : 0.0f);
        snprintf(out, n, "%s%d:%02d", pre, t / 60, t % 60);
    };
    out[0] = 0;
    if (ms.mode == MODE_VS || ms.mode == MODE_DUEL || isOneVsOneMode(ms.mode) || ms.mode == MODE_TOURNEY) {
        if (ms.flags & MS_DRAIN) snprintf(out, n, "STALEMATE - HEALTH DRAINING");
        else clock("DRAIN IN ", ms.timeLeft);
    } else if (ms.mode == MODE_KOTH) snprintf(out, n, "FIRST TO %d", (int)ms.target);
    else if ((ms.mode == MODE_RACE || ms.mode == MODE_SURF) && raceRun >= 0) {   // run time so far, to the hundredth
        int run = (int)std::lround(raceRun * 1000);
        if (run < 0) run = 0;
        snprintf(out, n, "%d:%02d.%02d", run / 60000, run / 1000 % 60, run % 1000 / 10);
    } else if (ms.mode == MODE_BOMB && ms.bombState == BOMB_PLANTED) clock("BOMB ", ms.bombTimer);
    else if ((ms.flags & MS_OVERTIME) && ms.mode == MODE_FFA) snprintf(out, n, "OVERTIME - NEXT KILL WINS");
    else if (ms.flags & MS_OVERTIME) clock("OVERTIME ", ms.timeLeft);   // (ends this long after the objective was last touched)
    else if (ms.timeLeft > 0) clock("", ms.timeLeft);
}
