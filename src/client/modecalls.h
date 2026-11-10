// The mode calls the client makes itself from the mode state it's sent (X10): text notices plus the announcer's
// existing lines, worked out per player (your side, your place), so the server and the mode logic don't change.
// Header-only and pure so quark_tests can check them (rules modecalls).
#pragma once
#include "../game/gamedefs.h"
#include "../game/protocol.h"
#include <cmath>
#include <cstdio>
#include <vector>

struct ModeCall {
    char text[72] = {};   // a notice ("" = none)
    int voice = -1;       // an announcer line (the client's SND_VOICE_* id; -1 = none)
    int level = 2;        // the announcer setting it needs (1: the countdown's level, 2: the mode's calls)
};

// The calls for the step from pv to ms (the last two mode states seen). myTeam: TEAM_A / TEAM_B, -1 watching.
// voicePointTaken / voicePointLost: the client's ids for the point taken / lost lines (sounds stay in audio.h).
// Returns how many it wrote (at most max).
//  - Bomb, as a round's freeze begins: what your side does (and on a halftime swap, that the sides switched).
//  - Bomb, live: the carrier went down with it (yours: pick it up; theirs: it's lying there), and it was picked up.
//  - Payload: a checkpoint reached, as the point taken / lost line for your side (the server's notice says which).
inline int modeCalls(const ModeState& pv, const ModeState& ms, int myTeam, int voicePointTaken, int voicePointLost, ModeCall* out, int max) {
    int n = 0;
    auto add = [&](const char* text, int voice = -1) {
        if (n >= max) return;
        ModeCall& c = out[n++];
        snprintf(c.text, sizeof c.text, "%s", text);
        c.voice = voice;
    };
    if (pv.mode != ms.mode) return 0;
    const bool attacking = myTeam >= 0 && myTeam == ms.attackers;
    if (ms.mode == MODE_BOMB && ms.phase == PHASE_FREEZE && (pv.phase != PHASE_FREEZE || pv.round != ms.round)) {
        if (ms.round > 1 && pv.round > 0 && pv.attackers != ms.attackers)
            add(myTeam < 0 ? "HALFTIME - SIDES SWITCHED" : attacking ? "HALFTIME - SIDES SWITCHED: YOU ATTACK NOW" : "HALFTIME - SIDES SWITCHED: YOU DEFEND NOW");
        if (myTeam < 0) add(ms.attackers == TEAM_A ? "TEAM A ATTACKS - TEAM B DEFENDS" : "TEAM B ATTACKS - TEAM A DEFENDS");
        else add(attacking ? "ATTACK: CARRY THE BOMB TO A SITE AND PLANT IT" : "DEFEND: KEEP THEM OFF THE SITES, DEFUSE A PLANTED BOMB");
    }
    if (ms.mode == MODE_BOMB && ms.phase == PHASE_LIVE && pv.phase == PHASE_LIVE && pv.round == ms.round) {
        if (pv.bombState == BOMB_CARRIED && ms.bombState == BOMB_DROPPED)
            add(myTeam < 0 ? "THE BOMB IS DOWN" : attacking ? "BOMB DROPPED - PICK IT UP" : "THEY DROPPED THE BOMB");
        else if (pv.bombState == BOMB_DROPPED && ms.bombState == BOMB_CARRIED)
            add(myTeam < 0 ? "THE BOMB IS CARRIED AGAIN" : attacking ? "THE BOMB IS CARRIED AGAIN" : "THEY HAVE THE BOMB AGAIN");
    }
    if (ms.mode == MODE_PAYLOAD && ms.phase == PHASE_LIVE && pv.round == ms.round && ms.point > pv.point && myTeam >= 0)
        add("", attacking ? voicePointTaken : voicePointLost);
    return n;
}

// Free for all: your place (1 = first) among those playing, how many play, and whether you share it. 0 when you
// aren't playing.
inline int ffaPlace(const std::vector<PlayerSnap>& players, int me, int* of, bool* tied) {
    int myScore = 0, n = 0, above = 0, same = 0;
    bool in = false;
    for (auto& p : players) if (p.id == me && !(p.flags & PS_SPECTATOR)) { in = true; myScore = p.score; }
    for (auto& p : players) {
        if (p.flags & PS_SPECTATOR) continue;
        n++;
        if (p.id == me) continue;
        above += p.score > myScore;
        same += p.score == myScore;
    }
    if (of) *of = n;
    if (tied) *tied = same > 0;
    return in ? above + 1 : 0;
}
inline const char* ordinalSuffix(int k) {
    int t = k % 100;
    if (t >= 11 && t <= 13) return "TH";
    return k % 10 == 1 ? "ST" : k % 10 == 2 ? "ND" : k % 10 == 3 ? "RD" : "TH";
}

// Free for all's lead calls, for you only (it played "new leader" to everyone on every lead change): you took a
// clear lead (the new leader line), or lost yours. lead: you lead alone now; wasLead: you did. -1 none, 0 lost, 1 took.
inline int ffaLeadCall(bool wasLead, bool lead) { return lead == wasLead ? -1 : lead ? 1 : 0; }

// ---- mode sounds the client works out itself from the states it's sent (no event for them on the wire)

// Bomb: the planted bomb went off between pv and ms (the round's end comes with it, so the phase isn't checked).
inline bool bombWentOff(const ModeState& pv, const ModeState& ms) {
    return ms.mode == MODE_BOMB && pv.mode == ms.mode && pv.round == ms.round && pv.bombState == BOMB_PLANTED && ms.bombState == BOMB_EXPLODED;
}

// Tag: who is "it" (PS_HASBOMB, never hidden in tag) among the alive players, -1 none.
inline int tagItOf(const std::vector<PlayerSnap>& players) {
    for (auto& p : players) if ((p.flags & PS_ALIVE) && (p.flags & PS_HASBOMB)) return p.id;
    return -1;
}
// Tag: "it" passed by a stab between pv and ms (live, one round): the tagger, the old "it", or -1. prevIt / nowIt:
// tagItOf the two states; ms's players. A new "it" after the old one left isn't a tag (the server draws one at random).
inline int tagPassFrom(const ModeState& pv, const ModeState& ms, int prevIt, int nowIt, const std::vector<PlayerSnap>& players) {
    if (ms.mode != MODE_TAG || pv.mode != ms.mode || pv.round != ms.round || pv.phase != PHASE_LIVE || ms.phase != PHASE_LIVE) return -1;
    if (prevIt < 0 || nowIt < 0 || prevIt == nowIt) return -1;
    for (auto& p : players) if (p.id == prevIt) return prevIt;
    return -1;
}

// Race practice (warmup): the server sends no split for its checkpoints, so the ding is the client's: at once when
// your own predicted box enters the next one, else when the server's count (PlayerSnap::checkpoint) moves on without
// it. One ding per checkpoint either way.
struct PracticeDing {
    int dinged = -1;     // the checkpoint index dinged and not yet passed on the server
    int lastSnap = -1;   // the server's count last seen
    void reset() { dinged = lastSnap = -1; }
    // next: the server's count (the next checkpoint's index); in: your box is in that checkpoint now; total: how many.
    // 0 none, 1 a checkpoint, 2 the finish (the server sends you round again: its count goes back to 0).
    int step(int next, bool in, int total) {
        int r = 0;
        if (total <= 0 || next < 0 || next >= total) { reset(); return 0; }
        if (lastSnap >= 0 && next != lastSnap) {   // the server passed one: dinged already, or say it now
            if (dinged != lastSnap) r = lastSnap + 1 == total ? 2 : 1;
            dinged = -1;
        }
        lastSnap = next;
        if (!r && in && dinged != next) { dinged = next; r = next + 1 == total ? 2 : 1; }
        return r;
    }
};

// ---- the sound audit's cues (q, 2026-10-30: "All of them"): the client's own, from the states it's sent (no event or
// protocol change for any of them)

// Bomb: the planted bomb's tick, as often as its model blinks (every fuse left / 20 s, at least 0.15 s: from 2 s apart
// on a 40 s fuse to 0.15 s over the last 3 s), its pitch rising over the last 5 s (1.0 to 1.3).
inline double bombTickInterval(float timer) { return timer / 20.0 > 0.15 ? timer / 20.0 : 0.15; }
inline float bombTickPitch(float timer) { return timer >= 5.0f ? 1.0f : 1.0f + 0.3f * (1.0f - (timer > 0 ? timer : 0.0f) / 5.0f); }

// Bomb: a plant (1) or a defuse (2) going on, 0 none. The server hides one out of your sight (actionPlayer 255), so
// what you hear is what you could see.
inline int bombActionOf(const ModeState& ms) {
    if (ms.mode != MODE_BOMB || ms.phase != PHASE_LIVE || ms.actionPlayer >= kMaxPlayers) return 0;
    return ms.bombState == BOMB_PLANTED ? 2 : ms.bombState == BOMB_CARRIED ? 1 : 0;
}
constexpr double kBombActionClick = 0.3;   // a plant's / defuse's click this often while it goes on

// King of the hill: both teams on the hill.
inline bool hillContested(const ModeState& ms) { return ms.mode == MODE_KOTH && ms.phase == PHASE_LIVE && (ms.flags & MS_CONTESTED); }
constexpr double kHillContestedGap = 4.0;   // its warning at most this often (the contest flickers as players step on and off)
// Multi point: the enemy filling the point your team holds (the defenders' point: attackers alone on it, hillOwner
// the attackers while they capture; pointStep, gamemodes.cpp). myTeam: TEAM_A / TEAM_B, -1 watching.
inline bool pointUnderCapture(const ModeState& ms, int myTeam) {
    return ms.mode == MODE_POINTS && ms.phase == PHASE_LIVE && myTeam >= 0 && myTeam != ms.attackers && ms.hillOwner == ms.attackers &&
           !(ms.flags & MS_CONTESTED) && ms.point < ms.pointCount;
}
constexpr double kCaptureAlarmEvery = 3.0;   // the alarm as a capture starts, then this often while it goes on

// A cue that plays as its condition begins, then (repeat) every gap while it lasts, or (no repeat) again only once
// it has ended and begun again at least gap after the last one.
struct CueGate {
    double next = -1e9;
    bool was = false;
    void reset() { next = -1e9; was = false; }
    bool step(bool on, double now, double gap, bool repeat) {
        const bool begins = on && !was;
        was = on;
        if (!on || now < next || (!begins && !repeat)) return false;
        next = now + gap;
        return true;
    }
};

// The map's holdfire (the dojo's bow: nobody fires for its first seconds live): a tick as each second of it begins
// (5, 4, 3, 2, 1 on a 5 s one), then the release as it runs out (2). prevLeft / left: the seconds of it left the last
// frame and now (0 or less: none); live: the round is still live (a round ended inside it isn't a release).
inline int holdFireCue(float prevLeft, float left, bool live) {
    if (prevLeft > 0 && left <= 0) return live ? 2 : 0;
    if (left > 0 && (prevLeft <= 0 || std::ceil(left) != std::ceil(prevLeft))) return 1;
    return 0;
}

// Race: a checkpoint the server won't count, entered out of order (one past the next to take: a skipped gate); the
// race's and the practice's rule alike (raceStep / tickRacePractice count only the next one). The one your box just
// passed through counts as taken before the server's count catches up (no buzz at the next gate for a lag).
struct WrongCheckpoint {
    int inside = -1;   // the checkpoint your box was in the last frame (-1 none)
    int took = -1;     // the next one, entered by your box, while the server's count is still on it
    int lastNext = -1;
    void reset() { inside = took = lastNext = -1; }
    // next: the server's count (the next checkpoint's index); in: the checkpoint your box is in (-1 none: the next
    // one first, else the first later one). True: the buzz, once as you enter it.
    bool step(int next, int in) {
        if (next != lastNext) { took = -1; lastNext = next; }
        if (in >= 0 && in == next) took = next;
        const int expect = took >= 0 ? took + 1 : next;
        const bool buzz = in >= 0 && in != inside && in > expect;
        inside = in;
        return buzz;
    }
};
