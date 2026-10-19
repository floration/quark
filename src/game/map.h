// Block-based maps: a set of axis-aligned solid boxes plus gameplay markers.
#pragma once
#include <string>
#include <vector>
#include "../common/qmath.h"

enum SpawnTeam { SPAWN_A = 0, SPAWN_B = 1, SPAWN_ANY = 2, SPAWN_RACE = 3 };

struct Brush {
    AABB box;
    int material = 0; // 0 = base, 1 = accent, 2 = wallbang (dark grey: solid, but hitscan goes through it)
};

struct SpawnPoint {
    Vec3 pos;
    float yaw = 0;
    int team = SPAWN_ANY;
};

struct Map {
    std::string name;
    std::string title;
    int preferredMode = -1;    // "for <mode>": the mode this map was designed for
    std::vector<Brush> brushes;
    std::vector<SpawnPoint> spawns;
    std::vector<AABB> bombSites;
    bool hasHill = false;
    AABB hill;
    std::vector<AABB> checkpoints;    // race; the last one is the finish line
    std::vector<Vec3> payloadPath;    // ordered waypoints
    Vec3 sunDir = normalize(Vec3(0.4f, 1.0f, 0.25f)); // points toward the sun
    struct Light { Vec3 pos; float radius = 10, intensity = 1; };
    std::vector<Light> lights;        // baked point lights ("light x y z radius [intensity]"): lit rooms under roofs
    AABB bounds;
    uint32_t crc = 0;
    std::string source;               // the .qmap text this was parsed from (sent to clients that lack it)

    bool load(const std::string& name, std::string* err = nullptr);
    bool parse(const std::string& text, std::string* err = nullptr);
    std::string serialize() const;    // back to .qmap text (what the editor saves)
    void rebuild();                   // after editing brushes in place: bounds, wallbang flag, grid

    // Movement collision: returns how far along `axis` the box may move (clamped delta).
    float sweepAxis(const AABB& box, int axis, float delta) const;
    bool boxSolid(const AABB& box) const;
    // Returns hit distance or -1. normal receives the face normal on hit.
    float raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3* normal = nullptr) const;
    bool raycastAny(const Vec3& origin, const Vec3& dir, float maxDist) const; // shadow-ray style early out
    // Hitscan bullets: like raycast, but passes through wallbang brushes; *through is set if it went
    // through at least one before the returned hit (or maxDist); *bangDist = distance to the first one.
    float raycastBullet(const Vec3& origin, const Vec3& dir, float maxDist, float* bangDist) const;
    bool hasWallbang = false;

    bool supportsMode(int mode) const;

    // Uniform XZ grid over brushes: queries only touch cells they pass through.
    static constexpr float kCell = 4.0f;
    int gridW = 0, gridH = 0;
    float gridX0 = 0, gridZ0 = 0;
    std::vector<uint32_t> cellStart;   // gridW*gridH+1 offsets into cellBrushes
    std::vector<uint16_t> cellBrushes;
    void buildGrid();
    template <class F> void forBox(const AABB& b, F f) const; // calls f(brush) for brushes in overlapped cells (may repeat)

    // Moving solids that aren't part of the static geometry (the payload cart). Collision and
    // raycasts include them; kept tiny so checking them all is free.
    std::vector<AABB> dynSolids;
    AABB payloadCartBox(float dist) const;
    float payloadLength() const;
    Vec3 payloadPoint(float dist) const;
};

// Map edit ops (the editor's undo steps and what multiplayer editing sends through the server), one line each:
//   <kind> ins|set|del <index> [fields]   kind: b brush (material + box), s spawn (team x y z yawDeg), l light (x y z radius
//                                         intensity), z bomb site (box), c checkpoint (box), p path point (x y z), h hill (has + box)
//   t <title>    f <preferred mode, -1 any>
// applyMapEdit validates (indices, sizes, finite numbers, at least one box and spawn) and rebuilds the grid.
bool applyMapEdit(Map& m, const std::string& op);
// The op that sets (or inserts) object `idx` of `kind` to its current state in m: used to build edits and their undo.
std::string describeMapObject(const Map& m, char kind, const char* verb, int idx);

std::string findDataPath(const std::string& rel); // resolves maps/... next to the executable or cwd
void setExecutableDir(const char* argv0);
const std::string& executableDir();       // empty if unknown
std::vector<std::string> listMaps();
// Maps fetched from servers live apart from your own, keyed by content: maps/downloads/<name>-<crc>.qmap
std::string downloadedMapPath(const std::string& name, uint32_t crc);
bool saveMapFile(const std::string& path, const std::string& text);
uint32_t crc32(const void* data, size_t len);
