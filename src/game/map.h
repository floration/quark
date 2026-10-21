// Block-based maps: a set of axis-aligned solid boxes plus gameplay markers.
#pragma once
#include <string>
#include <vector>
#include "../common/qmath.h"

enum SpawnTeam { SPAWN_A = 0, SPAWN_B = 1, SPAWN_ANY = 2, SPAWN_RACE = 3, SPAWN_STAGED = 4 };
// Multi point spawns ("spawn att2 ..." / "spawn def0 ..."): team = SPAWN_STAGED + 2 * stage + (defenders ? 1 : 0).
// A stage's spawns are used once that many points have fallen (the highest stage reached so far wins).
constexpr int kMaxSpawnStage = 9;
inline int stagedSpawn(int stage, bool defenders) { return SPAWN_STAGED + 2 * stage + (defenders ? 1 : 0); }
inline bool isStagedSpawn(int team) { return team >= SPAWN_STAGED; }
inline int spawnStage(int team) { return (team - SPAWN_STAGED) / 2; }
inline bool spawnDefends(int team) { return ((team - SPAWN_STAGED) & 1) != 0; }

struct Brush {
    AABB box;
    int material = 0; // 0 = base, 1 = accent, 2 = wallbang (dark grey: solid, but hitscan goes through it), 3 = metal, 4 = glow (emissive trim), 5 = far (distant scenery outside the walls: flat-shaded, no lightmap), 6 = water, 7 = lava (unlit surfaces of deadly pits: not solid, you fall through and die), 8 = trim (accent-coloured decoration: lit, but not solid), 9 = clip (invisible: blocks players only, not shots, hooks or sight; tops off the outer walls so nobody leaves the map)
    bool generated = false; // made from another object (an editor ladder's rails and rungs): not saved, not editable
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
    uint16_t rankedLadders = 0; // "ranked vanilla vampire instagib": the ranked ladders (1v1 rulesets) this map is built for
    uint32_t allowedModes = 0; // "modes ffa duel ...": only these (bit per mode); 0 = every mode the map has the pieces for
    std::vector<Brush> brushes;
    std::vector<SpawnPoint> spawns;
    std::vector<AABB> bombSites;
    bool hasHill = false;
    AABB hill;
    std::vector<AABB> ladders;        // "ladder x0 y0 z0 x1 y1 z1 [auto]": climbable volumes (stand in it and move)
    std::vector<uint8_t> ladderAuto;  // per ladder: draws its own rails and rungs (one editor object; else separate boxes do)
    void genLadderBrushes();          // (re)makes the generated rails / rungs of the auto ladders, at the end of brushes
    size_t editableBrushes() const { size_t n = 0; for (auto& b : brushes) n += !b.generated; return n; }
    std::vector<AABB> checkpoints;    // race; the last one is the finish line
    std::vector<Vec3> payloadPath;    // ordered waypoints
    std::vector<float> cartChecks;    // payload checkpoints, as fractions of the track ("cartcheck 0.33"), ascending
    std::vector<AABB> points;         // multi point: the capture zones, in the order they're attacked ("point x0 y0 z0 x1 y1 z1")
    Vec3 sunDir = normalize(Vec3(0.4f, 1.0f, 0.25f)); // points toward the sun
    // "palette base r g b accent r g b metal r g b glow r g b sky r g b" (linear 0..1): per-map colours
    // "void <y>": falling below this height kills (pits, water, the drop off a bridge); default 20 m below the map
    bool hasVoid = false;
    float holdFire = 0;        // "holdfire <s>": nobody can fire for this long after a round goes live (the dojo's bow)
    bool indoor = false;   // "indoor <y>": a roof over everything at y (cameras that orbit the map stay below it)
    float ceilingY = 0;
    float voidY = 0;
    float killY() const { return hasVoid ? voidY : bounds.mn.y - 20.0f; }
    bool hasPalette = false;
    Vec3 palBase, palAccent, palMetal, palGlow, palSky;
    struct Light { Vec3 pos; float radius = 10, intensity = 1; };
    std::vector<Light> lights;        // baked point lights ("light x y z radius [intensity]"): lit rooms under roofs
    AABB bounds;
    uint32_t crc = 0;
    std::string source;               // the .qmap text this was parsed from (sent to clients that lack it)

    bool load(const std::string& name, std::string* err = nullptr);
    bool onLadder(const AABB& player) const { for (auto& l : ladders) if (player.overlaps(l)) return true; return false; }
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

    bool supportsMode(int mode) const;  // allowed and has what the mode needs
    bool canSupportMode(int mode) const; // has what the mode needs (ignores allowedModes)

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
//                                         intensity), z bomb site (box), c checkpoint (box), p path point (x y z), h hill (has + box),
//                                         o multi point capture zone (box)
//   t <title>    f <preferred mode, -1 any>    m <allowed modes bitmask, 0 = all>
// applyMapEdit validates (indices, sizes, finite numbers, at least one box and spawn) and rebuilds the grid.
bool applyMapEdit(Map& m, const std::string& op);
// The op that sets (or inserts) object `idx` of `kind` to its current state in m: used to build edits and their undo.
std::string describeMapObject(const Map& m, char kind, const char* verb, int idx);

std::string findDataPath(const std::string& rel); // resolves maps/... next to the executable or cwd
void setExecutableDir(const char* argv0);
const std::string& executableDir();       // empty if unknown
std::vector<std::string> listMaps();
// Writes every shipped map back to its original text (and drops their light caches); returns how many.
int resetOfficialMaps();
std::string mapsFolder(); // absolute path of the maps folder
// Maps fetched from servers live apart from your own, keyed by content: maps/downloads/<name>-<crc>.qmap
std::string downloadedMapPath(const std::string& name, uint32_t crc);
bool saveMapFile(const std::string& path, const std::string& text);
uint32_t crc32(const void* data, size_t len);
