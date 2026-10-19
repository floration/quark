// Block-based maps: a set of axis-aligned solid boxes plus gameplay markers.
#pragma once
#include <string>
#include <vector>
#include "../common/qmath.h"

enum SpawnTeam { SPAWN_A = 0, SPAWN_B = 1, SPAWN_ANY = 2, SPAWN_RACE = 3 };

struct Brush {
    AABB box;
    int material = 0; // 0 = base, 1 = accent
};

struct SpawnPoint {
    Vec3 pos;
    float yaw = 0;
    int team = SPAWN_ANY;
};

struct Map {
    std::string name;
    std::string title;
    std::vector<Brush> brushes;
    std::vector<SpawnPoint> spawns;
    std::vector<AABB> bombSites;
    bool hasHill = false;
    AABB hill;
    std::vector<AABB> checkpoints;    // race; the last one is the finish line
    std::vector<Vec3> payloadPath;    // ordered waypoints
    Vec3 sunDir = normalize(Vec3(0.4f, 1.0f, 0.25f)); // points toward the sun
    AABB bounds;
    uint32_t crc = 0;

    bool load(const std::string& name, std::string* err = nullptr);
    bool parse(const std::string& text, std::string* err = nullptr);

    // Movement collision: returns how far along `axis` the box may move (clamped delta).
    float sweepAxis(const AABB& box, int axis, float delta) const;
    bool boxSolid(const AABB& box) const;
    // Returns hit distance or -1. normal receives the face normal on hit.
    float raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3* normal = nullptr) const;

    bool supportsMode(int mode) const;
    float payloadLength() const;
    Vec3 payloadPoint(float dist) const;
};

std::string findDataPath(const std::string& rel); // resolves maps/... next to the executable or cwd
void setExecutableDir(const char* argv0);
std::vector<std::string> listMaps();
uint32_t crc32(const void* data, size_t len);
