// Bot brain tests: the .qbrain format (parse / print round trip, converters), and the lockstep test that runs two
// bot brains side by side on the same server state every tick and compares what they do, input and dice included.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "common/net.h"
#include "game/botbrain.h"
#include "game/bots.h"
#include "game/server.h"

namespace {

int g_brainFail = 0;
#define BCHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_brainFail++; } } while (0)

void pumpFor(Server& s, double gameSecs) {
    double end = timeNow() + gameSecs;
    while (timeNow() < end) { s.frame(); sleepMs(1); }
}

// ---- lockstep: two think functions, the same state, every tick
using ThinkFn = UserCmd (*)(Server&, int);
struct Lockstep {
    ThinkFn a = nullptr, b = nullptr;
    long thinks = 0, mismatches = 0;
    std::string first;   // the first mismatch, described
    uint16_t buttons = 0; // every button pressed (coverage)
    long fights = 0;      // thinks with a target
    int kills = 0;
};
Lockstep* g_ls = nullptr;

template <class T> bool same(const T& x, const T& y) { return std::memcmp(&x, &y, sizeof(T)) == 0; }

std::string diffBrains(const BotBrain& x, const BotBrain& y) {
    std::string d;
#define BB(f) if (!same(x.f, y.f)) d += #f " ";
    BB(rng) BB(target) BB(engagedAt) BB(leadFactor) BB(dropFactor) BB(speedJudge) BB(rejudgeAt) BB(velEst)
    BB(aimYaw) BB(aimPitch) BB(yawVel) BB(pitchVel) BB(noiseYaw) BB(noisePitch) BB(flickYaw) BB(flickPitch)
    BB(glanceUntil) BB(nextGlance) BB(glanceYaw) BB(goal) BB(goalUntil) BB(lastPos) BB(stuckCheckAt) BB(strafeDir)
    BB(strafeSwitchAt) BB(unstickUntil) BB(fleeUntil) BB(hookUntil) BB(nextHookTry) BB(hookAnchor) BB(hookChecked)
    BB(fightHookUntil) BB(nextDecision) BB(nearEdge) BB(edgeFar) BB(edgeAway) BB(pathPrev) BB(pathGoalNode) BB(waypoint)
    BB(pathGoal) BB(repathAt) BB(nextSteer) BB(needPath) BB(nextLook)
#undef BB
    if (x.path != y.path) d += "path ";
    for (int i = 0; i < kMaxPlayers; i++) {
        const BotAwareness &p = x.aware[i], &q = y.aware[i];
        if (!same(p.visible, q.visible) || !same(p.reactedAt, q.reactedAt) || !same(p.seenAt, q.seenAt) || !same(p.seenPos, q.seenPos) ||
            !same(p.seenVel, q.seenVel) || !same(p.heardAt, q.heardAt) || !same(p.heardPos, q.heardPos)) { d += "aware[" + std::to_string(i) + "] "; break; }
    }
    return d;
}

UserCmd lockstepThink(Server& s, int id) {
    Lockstep& ls = *g_ls;
    BotBrain saved = s.bots_[id];
    saved.tc.dt = -1;   // (each side works out its own per-tick constants)
    s.bots_[id] = saved;
    UserCmd ca = ls.a(s, id);
    BotBrain ba = s.bots_[id];
    s.bots_[id] = saved;
    UserCmd cb = ls.b(s, id);
    ls.thinks++;
    ls.buttons |= cb.buttons;
    ls.fights += s.bots_[id].target >= 0;
    std::string d = diffBrains(ba, s.bots_[id]);
    if (ca.forward != cb.forward) d += "cmd.forward ";
    if (ca.side != cb.side) d += "cmd.side ";
    if (ca.buttons != cb.buttons) d += "cmd.buttons ";
    if (ca.weapon != cb.weapon) d += "cmd.weapon ";
    if (!same(ca.yaw, cb.yaw)) d += "cmd.yaw ";
    if (!same(ca.pitch, cb.pitch)) d += "cmd.pitch ";
    if (!d.empty()) {
        if (!ls.mismatches) {
            char buf[160];
            snprintf(buf, sizeof buf, "bot %d (%s) at %.3f s, tick %llu: ", id, botSkillName(s.bots_[id].skill), s.time(), (unsigned long long)s.tick_);
            ls.first = buf + d;
        }
        ls.mismatches++;
    }
    return cb;
}

struct LockMatch { const char* map; int mode; int bots; };

// One match: `bots` bots of every level (both teams), seeded, `secs` of game time with every bot's every tick run
// through both think functions.
void lockstepMatch(uint16_t port, const LockMatch& lm, int rate, double secs, uint32_t seed, Lockstep& ls) {
    Server s;
    ServerConfig cfg;
    cfg.map = lm.map; cfg.mode = lm.mode; cfg.port = port; cfg.maxTickRate = rate; cfg.maxPlayers = 16;
    cfg.autoStart = true; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    std::string err;
    if (!s.start(cfg, &err)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, err.c_str()); g_brainFail++; return; }
    BCHECK(s.tickRate() == rate);
    for (int k = 0; k < lm.bots; k++) s.addBot(k % 2 ? TEAM_B : TEAM_A, (k / 2 + k) % BOT_SKILLS);
    for (int k = 0; k < 200 && !s.nav_.ready(s.map_); k++) pumpFor(s, 0.05);
    BCHECK(s.nav_.ready(s.map_));
    for (int i = 0; i < kMaxPlayers; i++)
        if (s.players_[i].connected && s.players_[i].isBot) s.bots_[i].rng = (seed * 2654435761u) ^ (uint32_t)(i * 40503u + 1u);
    s.command("forcestart");
    g_ls = &ls;
    g_botThinkHook = lockstepThink;
    pumpFor(s, secs);
    for (auto& p : s.players_) ls.kills += p.connected ? p.kills : 0;
    g_botThinkHook = nullptr;
    g_ls = nullptr;
    s.stop();
}

const LockMatch kLockMatches[] = {
    {"clockwork", MODE_FFA, 6}, {"summit", MODE_KOTH, 8}, {"depot", MODE_BOMB, 8}, {"railyard", MODE_PAYLOAD, 8},
    {"canal", MODE_NPAYLOAD, 8}, {"midtown", MODE_POINTS, 8}, {"foundry", MODE_TAG, 6}, {"hamlet", MODE_HIDE, 6},
    {"viaduct", MODE_RACE, 4}, {"bastion", MODE_VS, 8}, {"furnace", MODE_ARENA, 2},
};

void lockstepAll(uint16_t& port, ThinkFn a, ThinkFn b, const char* what, double secs) {
    const int rates[] = {1000, 250, 125};
    long thinks = 0, fights = 0;
    int kills = 0;
    uint16_t buttons = 0;
    for (const LockMatch& lm : kLockMatches)
        for (int r = 0; r < 3; r++) {
            Lockstep ls;
            ls.a = a; ls.b = b;
            lockstepMatch(port, lm, rates[r], secs, 0xB4A1u + 977u * r + (uint32_t)lm.mode, ls);
            thinks += ls.thinks; fights += ls.fights; kills += ls.kills; buttons |= ls.buttons;
            if (ls.mismatches) printf("  FAIL %s: %s %s at %d Hz: %ld of %ld thinks differ; first: %s\n", what, lm.map, modeName(lm.mode), rates[r], ls.mismatches, ls.thinks, ls.first.c_str());
            BCHECK(ls.mismatches == 0 && ls.thinks > 0);
        }
    printf("  %s: %ld bot thinks compared (%ld fighting, %d kills, buttons %#x)\n", what, thinks, fights, kills, buttons);
    BCHECK(fights > thinks / 20 && kills > 20);
}

} // namespace

#ifdef QUARK_BOTS_REF
UserCmd botThinkRef(Server& s, int id);   // (src/game/bots_ref.cpp: the 1.18.4 bot, temporary)
#endif

int brainTests(uint16_t port) {
    g_brainFail = 0;
    printf("bot brains\n");
#ifdef QUARK_BOTS_REF
    lockstepAll(port, botThinkRef, botThinkDirect, "1.18.4 bot vs native brain", 12.0);
#endif
    (void)port;
    return g_brainFail;
}
