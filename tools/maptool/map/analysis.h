// What check / metrics / route / custom work on: the map as the game loads it, plus the walk grid
// (tests/walkgrid.h: the player's hull against the real collision), the bots' nav graph and the map report's
// measurements, each built the first time something asks for it.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "json.h"
#include "game/bots.h"
#include "game/map.h"
#include "walkgrid.h"

struct Analysis {
    Map m;                          // as loaded: clip included (players collide with it)
    explicit Analysis(Map map) : m(std::move(map)) {}
    // without clip (material 9): what the map report measures sight, cover and the nav graph on
    const Map& open();
    // the bots' nav graph: on the map as loaded (what the game's bots use, and what quark_tests' route checks walk)
    const NavGraph& nav();
    // the map report's model of the map (NavKind::Surfaces: every top whole) on the map without clip: its walking
    // distances and objective timings
    const NavGraph& navOpen();
    const NavGraph& navSurfaces();  // (the same on the map as loaded: the stairs check's walkable tops, as quark_tests')
    WalkGrid& walk();               // built, with its components
    const std::vector<char>& reach();   // walk grid spots a player gets to from some spawn (the report's walkReachable)
    Json timings() const;           // ms per part built so far
    // time to points for a mode (metrics.cpp): the objectives' arrivals; every spawn's route appended to `routes`
    Json objectives(int mode, Json& routes, bool withPaths);
private:
    std::unique_ptr<Map> open_;
    std::unique_ptr<NavGraph> nav_, navOpen_, navSurf_;
    std::unique_ptr<WalkGrid> walk_;
    std::vector<char> reach_;
    bool reachDone_ = false;
    std::vector<std::pair<std::string, double>> ms_;
    void lap(const char* what, double t0);
};

// The map report's per-map measurement (mapreport.cpp's measure(), copied: measure_impl.h), as JSON: every number it
// measures, the per-map scores (decor, layout, movement, mode fit, balance: the report's formulas; creativity and
// originality compare maps with each other, so only their inputs are given), the movement envelope.
Json measureJson(Analysis& a, Json* timings = nullptr);
// The movement envelope (what a good player's jumps reach at each carried speed), as JSON.
Json envelopeJson();
// Out of bounds (the map report's check): floor only movement tech reaches from the first spawn, and the part of it
// outside the playable floor's footprint (+1 m). spots: those outside it.
struct OutOfBounds { float techExtra = 0, outside = 0; std::vector<Vec3> spots; bool measured = false; };
OutOfBounds outOfBounds(Analysis& a);

// the map checks (checks.cpp): req's "only" / "outOfBounds" pick them; {map, humanMade, checks, pass, failed}
Json checksJson(Analysis& a, const Json& req);
// check ids, in the order they run
extern const char* const kCheckIds[];

// nav graph routes (the report's walking distances, m), with the path walked
float navRoute(const NavGraph& g, const Vec3& from, const Vec3& to, std::vector<Vec3>* path);
float navRouteToBox(const NavGraph& g, const Vec3& from, const AABB& box, std::vector<Vec3>* path);
// walk grid routes (the player's hull, no movement tech): metres along it, -1 unreachable; path: its corners.
// From spot `from` to the nearest of the target spots (reached: which one)...
float walkRouteNodes(WalkGrid& w, int from, const std::vector<int>& targets, std::vector<Vec3>* path, int* reached = nullptr);
// ...or between two points (each to the standing spot within maxRadius of it)
float walkRoute(WalkGrid& w, const Vec3& from, const Vec3& to, std::vector<Vec3>* path, float maxRadius = 0.6f);
