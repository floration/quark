// The mode calls the client makes itself from the mode state it's sent (X10): text notices plus the announcer's
// existing lines, worked out per player (your side, your place), so the server and the mode logic don't change.
// Header-only and pure so quark_tests can check them (rules modecalls).
#pragma once
#include "../game/gamedefs.h"
#include "../game/protocol.h"
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
