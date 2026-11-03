// Out of bounds (tests/mapbounds.cpp): with every move a good player has, from the first spawn, is any floor outside
// the playable floor's footprint (the floor you can walk to from a spawn) reachable? One implementation, used by
// quark_tests (`bounds`, `mapbounds`, `mapbounds2`) and by quark_maptool's `out_of_bounds` check, so the editors
// and the tests always give the same answer.
#pragma once
#include <vector>
#include "game/map.h"
#include "walkgrid.h"

struct OutOfBoundsMeasure {
    bool measured = false;          // false: no spawn, or no floor a spawn walks to (the other map checks fail those)
    float techExtra = 0;            // m2 of floor only movement tech reaches (roofs, wall tops)
    float outside = 0;              // m2 of it outside the playable floor's footprint (+1 m): out of bounds
    std::vector<Vec3> spots;        // the samples out of bounds
    float x0 = 0, z0 = 0, x1 = 0, z1 = 0;   // the playable floor's footprint
};

// m: the map as loaded (clip included). walk: its walk grid (tests/walkgrid.h, built on m with its components) if
// the caller has one already; else it's built here.
OutOfBoundsMeasure measureOutOfBounds(const Map& m, const WalkGrid* walk = nullptr);
