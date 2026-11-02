// AIM TRAINER (owner, 2026-11-01; 1.19.5): 30 s scenarios in three categories (clicking with the rifle, autofire off;
// switching and tracking with the laser), each in an easy / medium / hard version, in two modes: 3D STATIC (you can't
// move: look and shoot) and 2D (the view never moves: the mouse moves a cursor over a flat wall). This is the
// simulation alone (no drawing, no input): the targets, their motion, hits, the score. The client (src/client/
// trainer.cpp) draws it and feeds it the aim; quark_tests runs it headless (group "trainer").
//
// The world: your eye at the origin, looking down -z; the target wall faces you 24 m away (z = -24), its centre at eye
// height. Every distance is metres, every speed m/s.
#pragma once
#include "../common/qmath.h"
#include <cstdint>
#include <string>
#include <vector>

namespace trainer {

enum Mode { TR_3D = 0, TR_2D, TR_MODES };
enum Cat { CAT_CLICK = 0, CAT_SWITCH, CAT_TRACK, CAT_COUNT };
constexpr int kDiffs = 3;                  // easy, medium, hard
constexpr float kRunTime = 30.0f;          // every scenario (owner)
constexpr float kCountdown = 3.0f;         // 3, 2, 1 before the clock starts (shots before it don't count)
constexpr float kWallZ = -24.0f;           // the target wall, 24 m ahead (owner: "24m away")
constexpr float kLaserRange = 80.0f;       // the laser's 24 m reach made longer here (owner: "increase laser length")
constexpr float kLaserDps = 40.0f;         // the game's laser (0.04 every 1 ms)
constexpr float kRifleRefire = 0.75f * 2.0f / 9.9f;   // the game's rifle (weaponDef(WPN_RIFLE).refire)
constexpr float kClickBuffer = 0.08f;      // a click this soon before the rifle is ready fires when it is

enum Motion {
    MO_WALL,       // still, at a random spot on the wall (wall ball, 2 ball, frenzy)
    MO_AIR,        // still, anywhere in a 24 m cube room at least 10 m from you (2D: anywhere on a wider wall)
    MO_REACTOR,    // still, at a random spot on the wall; back 1 s after a kill
    MO_REACTOR9,   // still, at one of 9 preset spots on the wall; back 1 s after a kill
    MO_BOUNCY,     // around a 24 m circle around you, bouncing between 6 and up to 24 m high
    MO_CROSS,      // straight across a 48 x 24 wall; gone 0.5 s after reaching the far edge
    MO_CRAZY,      // a new diagonal every 0.3 s, staying on the wall, bouncing off its edges
    MO_SIDE_WALL,  // side to side on the wall, turning only at its edges
    MO_SIDE_AIR,   // side to side in the 24 m room, turning only at its walls
    MO_WANDER,     // random turns on the wall
    MO_RING,       // around you on a circle at one height, speeding up, slowing down, turning back
    MO_SHELL,      // anywhere around you (360), 12 to 24 m away
    MO_GRID,       // along the lines of a 3 x 3 grid, picking the next line at random at each crossing
    MO_DASH,       // bursts over a 180 degree wall: 1 m/s accelerating to a random top speed, then a new direction
    MO_RECT,       // 2D: bouncing inside the wall's rectangle (the bouncy balls, flattened)
    MO_STRAFE,     // 2D: left and right at one height, changing speed (the ring, flattened)
    MO_PULSE,      // 2D: wandering while it swells and shrinks (the 360 one's depth, flattened)
};

struct Scenario {
    const char* id;      // stable: scores are saved under it
    const char* name;    // the menu card's title
    const char* desc;    // its line under the title
    int mode, cat, motion;
    int count[kDiffs];   // targets at once
    float width[kDiffs]; // ball diameter
    float hp;            // 0: a hit kills; > 0: laser damage to kill; < 0: never dies (tracking)
    float speedMin[kDiffs], speedMax[kDiffs];
    bool laser;          // the laser (held), else the rifle (one shot a click)
};
const std::vector<Scenario>& scenarios();
const Scenario* findScenario(const std::string& id);
std::vector<const Scenario*> list(int mode, int cat);   // one mode and category, in menu order
const char* modeName(int mode);
const char* catName(int cat);
const char* diffName(int diff);

struct Target {
    Vec3 pos, vel;
    float r = 0.5f;       // radius (drawn, and hit)
    float baseR = 0.5f;   // (MO_PULSE: the size it swells around)
    float hp = 0;
    float hide = 0;       // > 0: gone this long yet (reactor, crossing guard)
    float timer = 0;      // until the next turn
    float speed = 0, goal = 0, angle = 0, phase = 0;
    int slot = -1;        // reactor 9: its preset spot; grid: the node it's heading to
    int from = -1;        // grid: the node it left
};

struct Stats {
    int shots = 0, hits = 0, kills = 0;
    double fireTime = 0, onTime = 0, damage = 0;   // the laser: held, on a target, dealt
};

// What one step did, for the client's sounds and effects
struct StepEvents {
    bool shot = false, shotHit = false;   // the rifle fired; it hit
    Vec3 shotEnd;                         // where its shot ended (a hit, or kLaserRange out)
    int kills = 0;
    bool laserOn = false;                 // the laser is on a target this step
    Vec3 laserEnd;                        // where the beam ends
    float damage = 0;                     // laser damage dealt this step
};

// The room drawn around a scenario (its inside): the client's walls, floor and ceiling
struct Room { Vec3 mn, mx; bool pillar = true; };

class Run {
public:
    void start(const Scenario& s, int diff, uint32_t seed);
    // dt seconds; aim: the shot's direction (unit) from the eye; fire: the button held; click: pressed since the last step
    StepEvents step(float dt, const Vec3& aim, bool fire, bool click);
    // the nearest live target along a ray from the eye (-1: none); its distance
    int raycast(const Vec3& dir, float range, float* dist) const;
    const Scenario* scenario() const { return sc_; }
    int diff() const { return diff_; }
    float countdown() const { return countdown_; }   // > 0: still counting down
    float timeLeft() const { return timeLeft_; }
    bool finished() const { return finished_; }
    const Stats& stats() const { return stats_; }
    float accuracy() const;   // 0..1: rifle hits / shots, laser time on target / held
    int score() const;        // clicking / switching: kills x 100 x accuracy; tracking: damage x 10 x accuracy
    const std::vector<Target>& targets() const { return targets_; }
    Room room() const;
    bool flat() const { return sc_ && sc_->mode == TR_2D; }

private:
    const Scenario* sc_ = nullptr;
    int diff_ = 0;
    uint32_t rng_ = 1;
    float countdown_ = 0, timeLeft_ = 0, refire_ = 0, clickQueued_ = -1;
    bool finished_ = false;
    Stats stats_;
    std::vector<Target> targets_;
    float rnd();                         // 0..1
    float rnd(float a, float b) { return a + (b - a) * rnd(); }
    float speedRoll() { return rnd(sc_->speedMin[diff_], sc_->speedMax[diff_]); }
    void spawn(int i, bool first);
    void move(Target& t, float dt);
    bool overlaps(int i, const Vec3& p, float r) const;
    void kill(int i);
};

// The wall's half sizes for each kind of spot (the 2D view shows about +-28 m across and +-16 m up at 24 m)
constexpr float kWallHX = 16, kWallHY = 8;       // wall ball, reactor, crazy, wander, grid
constexpr float kCrossHX = 24, kCrossHY = 12;    // crossing guard: 48 x 24 (owner)
constexpr float kAir2HX = 22, kAir2HY = 11;      // 2D air ball: a wider wall
constexpr float kRoomHalf = 12;                  // the air room: 24 x 24 x 24, you at the centre of its back wall
constexpr float kArenaR = 24;                    // the bouncy balls' circle
constexpr float kArenaFloor = -12;               // the arena's floor (heights 6..24 m above it)

} // namespace trainer
