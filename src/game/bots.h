// Server-side AI players. A bot produces one UserCmd per server tick, which then goes through
// exactly the same movement and weapon code as a human's input.
//
// Bots play fair: they only know what a player in their place could know. They see through a view cone from
// the eye with a line of sight (no 360 degree scan, no wallhacks), hear gunfire and footsteps within range
// (with an approximate direction), and remember where they last saw someone for a few seconds. They react
// after a human reaction time, aim with a hand that has a top turn speed and acceleration, flick with
// overshoot and settle, track with an error that grows with how fast the target crosses their view, and lead
// from the velocity they've perceived (late on jukes, misjudged on projectiles). Every timing is in game
// milliseconds, so a bot plays the same at any server tick rate.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "map.h"
#include "../common/qmath.h"
#include "pmove.h"

class Server;

enum BotSkill { BOT_BEGINNER = 0, BOT_INTERMEDIATE, BOT_ADVANCED, BOT_EXPERT, BOT_SKILLS };
const char* botSkillName(int skill);

// What a bot knows about one other player.
struct BotAwareness {
    bool visible = false;      // in the view cone with a line of sight at the last look
    double reactedAt = -1;     // came into view: the reaction to it is over at this time
    double seenAt = -1e9;      // last time it was seen
    Vec3 seenPos, seenVel;     // where it was then, and how it seemed to move
    double heardAt = -1e9;     // last time it was heard (gunfire, footsteps)
    Vec3 heardPos;             // roughly where (the error grows with distance)
};

struct BotBrain {
    int skill = BOT_INTERMEDIATE;
    uint32_t rng = 1;
    int forceWeapon = -1;      // tests: always use this weapon
    int onlyEnemy = -1;        // tests: the only player treated as an enemy
    std::function<UserCmd(Server&, int)> script; // tests: this bot's input comes from here instead (a scripted player)
    // perception
    BotAwareness aware[32];    // (kMaxPlayers)
    double nextLook = 0;       // perception runs at a fixed game-time rate, staggered between bots
    int target = -1;           // player id being fought (visible) or hunted (remembered)
    double engagedAt = -1;     // aim started moving onto the current target (after the reaction time)
    float leadFactor = 1, dropFactor = 1, speedJudge = 1; // per engagement: how this bot misjudges leads
    double rejudgeAt = 0;
    Vec3 velEst;               // perceived velocity of the target (lags behind direction changes)
    // aim: a hand on a mouse
    float aimYaw = 0, aimPitch = 0;
    float yawVel = 0, pitchVel = 0;      // current turn speed (rad/s)
    float noiseYaw = 0, noisePitch = 0;  // tracking error (rad), a random process in game time
    float flickYaw = 0, flickPitch = 0;  // flick landing error (rad), corrected over the settle time
    double glanceUntil = 0, nextGlance = 0; float glanceYaw = 0; // looking around while moving
    // movement
    Vec3 goal;
    double goalUntil = 0;
    MapId goalMapId; uint32_t goalNavGen = 0; // the map / nav graph the goal was picked on (another one: picked again)
    Vec3 lastPos;
    double stuckCheckAt = 0;
    float strafeDir = 1;
    double strafeSwitchAt = 0;
    double unstickUntil = 0;
    double fleeUntil = 0;       // hide and seek / tag: committed to the current escape route until then
    double hookUntil = 0, nextHookTry = 0; // grapple: holding toward hookAnchor until then
    Vec3 hookAnchor;
    bool hookChecked = false;   // the grapple's catch was checked again (safeHook) from where it caught
    double fightHookUntil = 0;  // fighting: hooking the target (held while the aim is on them) until then
    double nextDecision = 0;    // fight movement choices (jump, dash) are rolled at a fixed game-time rate
    bool nearEdge = false;      // a drop into the void / lava within 3 m (looked for at 20 Hz)
    bool edgeFar = false;       // ...or only within 5 m
    Vec3 edgeAway;              // ...and the way away from it (xz, unit)
    // navigation: the route is kept, so reaching a waypoint costs no search
    std::vector<int> path;      // surfaces still to walk across, in order
    int pathPrev = -1;          // the surface the route continues from
    int pathGoalNode = -1;
    uint32_t pathGen = 0;       // nav graph generation the route belongs to
    Vec3 waypoint, pathGoal;
    double repathAt = 0, nextSteer = 0;
    bool needPath = true;
    // botThink's per-tick constants: functions of the tick length and the skill level only (exp, lround), worked out
    // again whenever either changes (bots.cpp tickConsts: the same expressions, so bit-identical to inline)
    struct TickConsts {
        float dt = -1; int rate = -1, skill = -1;   // what they were worked out for
        float velK = 0;                    // perceived velocity catch-up per tick: 1 - exp(-dt / velTau)
        float noiseA = 0, noiseKK = 0;     // tracking error: exp(-dt / noiseTau), sqrt(max(0, 1 - a^2))
        float settle = 0;                  // flick error left after a tick: exp(-dt / settleTau)
        float pJump = 0, pDash = 0, pRunDash = 0, pPulledDash = 0; // chance thresholds 1 - exp(-rate * dt)
        int seenTicks = 0, olderTicks = 0; // seenAgo's history offsets (visDelayMs, visDelayMs + 16)
        int turnSteps = 1; float turnH = 0, turnDv = 0; // the hand's 1 ms steps: count, length, speed change
    } tc;
};

struct NavJob;

// Walkable surfaces (tops of blocks) and which ones a player can jump between.
// Built from the map geometry, so every map gets bot navigation for free.
struct NavGraph {
    struct Node { float x0, z0, x1, z1, y; };
    std::vector<Node> nodes;
    std::vector<std::vector<std::pair<int, float>>> links; // (node, cost)
    std::vector<std::pair<std::pair<int, int>, Vec3>> ladderLinks; // (from node, to node) climbed at this point
    MapId mapId;               // the map it was built for
    uint32_t generation = 0;   // bumped by every build (bots drop routes from an older graph)
    void build(const Map& m);  // O(N log N)-ish: a grid finds the neighbours (same graph as checking every pair)
    int nodeAt(const Vec3& p) const;                       // surface the point stands on (or -1)
    // A* from surface a to b: the surfaces after a, up to and including b. Reuses its buffers (no allocations
    // once warm). False if there's no route.
    bool findPath(int a, int b, std::vector<int>& out) const;
    // Where to walk now along a route (the portal into the next surface, or the ladder to climb).
    Vec3 steer(const Vec3& from, int prevNode, const std::vector<int>& path, const Vec3& to) const;

    // Map load: build in the background (REVIEW PERF-10); the tick thread only picks up the result.
    void buildAsync(Map m);
    bool ready(const Map& m);  // adopts a finished background build; true once the graph matches the map
    std::shared_ptr<NavJob> job;

    // the grid behind nodeAt and build (4 m cells)
    float gx0 = 0, gz0 = 0;
    int gw = 0, gh = 0;
    std::vector<uint32_t> cellStart;
    std::vector<int> cellNodes;
    float hScale = 0;          // A* heuristic: straight-line distance times this never overestimates a route
    int cellOf(float x, float z) const;

    // findPath's scratch, reused between searches. The graph's, not thread_local: only its server's thread searches
    // it, and it's freed with the server. (A thread_local with a destructor runs it after MinGW's emutls has freed
    // its storage: when the hosted server's thread ended, STOP HOSTING, the vectors freed garbage pointers and the
    // heap corruption closed the game on Windows. No TLS lookups per search either.)
    struct PathScratch {
        std::vector<float> g;
        std::vector<int> prev;
        std::vector<uint32_t> stamp;
        std::vector<std::pair<float, int>> heap;
        uint32_t cur = 0;
    };
    mutable PathScratch scratch_;
};

// Produces this tick's input for bot `id`.
UserCmd botThink(Server& s, int id);
// Tests: when set, botThink calls this instead (the lockstep tests run two brains side by side through it, each
// calling botThinkDirect, which is botThink without the hook).
extern UserCmd (*g_botThinkHook)(Server& s, int id);
UserCmd botThinkDirect(Server& s, int id);
