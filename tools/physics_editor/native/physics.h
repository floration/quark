// The physics editor's commands in quark_maptool (README.md here, ../INTERFACE.md): physics sets (a custom mode's
// movement settings), the built-in test room, and the scenario runner that drives the game's own pmove().
// Everything is in namespace phys so these files never collide with the map / bot editors' (one executable).
#pragma once
#include <string>
#include <vector>
#include "json.h"
#include "game/customrules.h"
#include "game/map.h"
#include "game/pmove.h"

namespace phys {

// ---- physics sets (physics.cpp) ----------------------------------------------------------------------------------
// A physics set from the request: "standard", a .qmode text, or {gravity, speed, jump, dash, grapple, move:{key:x},
// qmode?, name?}. Every value goes through CustomRules::fromText, so it is clamped exactly as the game clamps a
// custom mode file; `clamped` lists the keys whose value changed. false: not a physics set (err says why).
bool physicsFromJson(const Json& j, CustomRules& out, std::vector<std::string>& clamped, std::string& err);
// The request's main set: "physics" (any form above), else top-level "qmode", else the standard rules.
bool requestPhysics(const Json& req, CustomRules& out, std::vector<std::string>& clamped, std::string& err);
Json physicsToJson(const CustomRules& r);   // {gravity, speed, jump, dash, grapple, move:{every key}, changed:[keys]}
bool isStandardMovement(const CustomRules& r);

// ---- the test room (testroom.cpp) --------------------------------------------------------------------------------
struct RoomPart {
    const char* id;
    const char* label;
    const char* desc;
    AABB box;          // the part's floor area (for drawing and for "which part is this start in")
    Vec3 start;        // where its presets start
    float yawDeg;      // facing (degrees; -90 = +x)
};
const std::vector<RoomPart>& roomParts();
const std::string& roomText();                 // the .qmap text (the game's map format, version kMapFormatVersion)
const Map& roomMap();                          // parsed once with Map::parse
struct Gap { float x0, x1; };                  // the gap course: the open stretches (x), in order, narrowest first
const std::vector<Gap>& roomGaps();
const RoomPart* roomPartAt(const Vec3& p);     // the part whose area holds p (nullptr: none)

// A request's map: absent / "room" / "room:<part>" = the test room; another string = a map by name; an object =
// {text|file|name|map} (loadRequestMap). `isRoom` says which. `holder` keeps a loaded map alive.
bool requestMap(const Json& j, const Map*& out, Map& holder, bool& isRoom, std::string& label, std::string& err);

// ---- the scenario runner (sim.cpp) --------------------------------------------------------------------------------
// angles are degrees in requests and answers; yaw 0 faces -z, -90 faces +x (the game's: forward = (-sin, 0, -cos))
struct Step {
    int atMs = 0;
    int forward = 0, side = 0;
    bool hasYaw = false, hasPitch = false;
    float yaw = 0, pitch = 0, yawRate = 0, pitchRate = 0;   // deg, deg/s
    bool jump = false, crouch = false, dash = false, hook = false;
    int strafeBot = 0;        // 0 off, 1 the optimal angle (computed), 2 search (tries angles through pmove itself)
    int strafeSwitch = 0;     // strafe bot: 1 weave (swap A <-> D once the velocity swings strafeSwing degrees past the
                              // heading the step started facing), 2 swap at every landing, 0 never (it circles)
    float strafeSwing = 25;
    bool jumpAtEdge = false;  // press jump on the last ms the floor still holds you (a running jump off an edge)
};
struct Scenario {
    std::string name;
    Vec3 pos, vel;
    float yaw = -90, pitch = 0;
    int onGround = -1;        // -1: found by probing the floor under the start
    int dashCharges = -1;     // -1: a full set
    int durationMs = 2000;
    std::vector<Step> steps;  // sorted by atMs
};
bool scenarioFromJson(const Json& j, Scenario& out, std::string& err);
Json scenarioToJson(const Scenario& s);

struct RunResult { Json trace, events, metrics; };
// Runs the scenario under the CALLING THREAD's active rules (setActiveRules). sampleMs: the trace's spacing.
RunResult runScenario(const Scenario& sc, const Map& map, bool isRoom, int sampleMs);

// (events: sim.cpp's Watch compares each 1 ms state with the one before: jump, land, dash, bounce, hook, ladder...)
float hspeed(const Vec3& v);
float roundTo(float v, double q);
Json vec(const Vec3& v, double q);

// The strafe bot's yaw (degrees) for this ms: the wish direction at the angle to the velocity that gains the most.
float strafeYaw(const MoveState& s, int forward, int side, float curYawDeg, int mode, const Map& map, uint16_t buttons, float pitchDeg);

} // namespace phys
