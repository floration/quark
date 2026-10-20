// Server-side AI players. A bot produces one UserCmd per server tick, which then goes through
// exactly the same movement and weapon code as a human's input.
#pragma once
#include <cstdint>
#include <vector>
#include "map.h"
#include "../common/qmath.h"
#include "pmove.h"

class Server;

enum BotSkill { BOT_BEGINNER = 0, BOT_INTERMEDIATE, BOT_ADVANCED, BOT_EXPERT, BOT_SKILLS };
const char* botSkillName(int skill);

struct BotBrain {
    int skill = BOT_INTERMEDIATE;
    int target = -1;           // player id being fought
    float seenAt = -1;         // when the current target was first seen (reaction time)
    float aimYaw = 0, aimPitch = 0;
    float yawVel = 0, pitchVel = 0;      // current turn speed (rad/s): limited speed and acceleration
    float errYaw = 0, errPitch = 0;      // aim error (rad), drifting toward errTo*
    float errToYaw = 0, errToPitch = 0;
    float errorRetargetAt = 0;
    float lapseUntil = 0;                // a moment of bad aim (lost track, panic)
    float nextLapseRoll = 0;
    Vec3 goal;
    float goalUntil = 0;
    Vec3 lastPos;
    float stuckCheckAt = 0;
    float strafeDir = 1, strafeSwitchAt = 0;
    float nextTargetScan = 0;
    Vec3 waypoint;             // next point on the nav path
    Vec3 pathGoal;
    float repathAt = 0;
    uint32_t rng = 1;
    int forceWeapon = -1;      // tests: always use this weapon
    float fleeUntil = 0;       // hide and seek / tag: committed to the current escape route until then
};

// Walkable surfaces (tops of blocks) and which ones a player can jump between.
// Built from the map geometry, so every map gets bot navigation for free.
struct NavGraph {
    struct Node { float x0, z0, x1, z1, y; };
    std::vector<Node> nodes;
    std::vector<std::vector<std::pair<int, float>>> links; // (node, cost)
    std::vector<std::pair<std::pair<int, int>, Vec3>> ladderLinks; // (from node, to node) climbed at this point
    uint32_t mapCrc = 0;
    void build(const Map& m);
    int nodeAt(const Vec3& p) const;                       // surface the point stands on (or -1)
    // Next point to walk toward on the way from `from` to `to`.
    Vec3 nextWaypoint(const Vec3& from, const Vec3& to) const;
};

// Produces this tick's input for bot `id`.
UserCmd botThink(Server& s, int id);
