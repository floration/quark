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
// Brush materials are 0..kMapMaterials-1 everywhere: a map's boxes, groups and group parts, in files, edits and the editor.
constexpr int kMapMaterials = 10;
inline bool validMapMaterial(int m) { return m >= 0 && m < kMapMaterials; }
inline int nextMapMaterial(int m) { return (m + 1) % kMapMaterials; }   // the editor's M key: base .. lava, trim, clip, base

// What any map may hold, however it arrives (file, download, demo, edits): Map::validate checks all of it, after
// groups and ladders are expanded, before the collision grid is built. Shipped maps use a small fraction of this.
constexpr float kMapExtent = 4096.0f;        // every coordinate, |x| <= this
constexpr size_t kMaxMapBrushes = 60000;     // generated ones included, on every path (file, download, edit)
static_assert(kMaxMapBrushes <= 0xFFFF, "the collision grid's cells index brushes with 16 bits (Map::cellBrushes)");
constexpr size_t kMaxMapGridRefs = 16u << 20; // brush references in the collision grid (32 MB; one box over the whole range is 4.2M)
constexpr size_t kMaxMapSpawns = 256, kMaxMapLights = 512, kMaxMapLadders = 256, kMaxMapGroups = 256, kMaxGroupParts = 1024;
constexpr size_t kMaxMapSites = 16, kMaxMapCheckpoints = 128, kMaxMapPath = 512, kMaxMapPoints = 16, kMaxMapCartChecks = 64;
constexpr float kMaxObjectHeight = 64.0f;    // ladders and groups (a ladder's rungs are made every 0.4 m)
constexpr float kMinLightRadius = 0.5f, kMaxLightRadius = 100.0f, kMaxLightIntensity = 10.0f;   // intensity > 0 (0 lights nothing)
inline bool validMapLight(float radius, float intensity) {
    return radius >= kMinLightRadius && radius <= kMaxLightRadius && intensity > 0 && intensity <= kMaxLightIntensity;   // (NaN fails)
}

// A map's title, from a file or an edit (one rule for both): the spaces around it dropped, then at most
// kMaxMapTitle printable ASCII characters and no '#' (the comment mark). false: not a valid title.
constexpr size_t kMaxMapTitle = 40;
bool cleanMapTitle(std::string& title);

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
    void genLadderBrushes();          // (re)makes the generated brushes (auto ladders' rails / rungs, groups' parts), at the end of brushes
    // "group custom|room|stairs <material> <dir> x0 y0 z0 x1 y1 z1" (+ "part <material> fx0 fy0 fz0 fx1 fy1 fz1" lines for custom):
    // one editor object whose boxes are made from its box, so moving / stretching it moves / stretches them all.
    // room: floor, walls with a player-sized doorway each, ceiling. stairs: steps rising toward dir (0 +x, 1 -x, 2 +z,
    // 3 -z), each no taller than pmove's step-up. custom: parts kept as 0..1 fractions of the box.
    enum { GROUP_CUSTOM, GROUP_ROOM, GROUP_STAIRS };
    struct Group { int type = GROUP_CUSTOM, material = 0, dir = 0; AABB box; std::vector<Brush> parts; };
    std::vector<Group> groups;
    static void groupBoxes(const Group& g, std::vector<Brush>& out); // its parts in world space
    size_t editableBrushes() const { // (the generated brushes are always the last ones)
        size_t n = brushes.size();
        while (n && brushes[n - 1].generated) n--;
        return n;
    }
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
    // The map's identity: the CRC of the text it was parsed from, then in an edit session each edit op continues it
    // (mapEditCrc), so nobody re-serializes the map to name it.
    uint32_t crc = 0;
    // The .qmap text (sent to clients that lack the map, saved, hashed for ranked): the parsed text, or after edits
    // serialize()d the first time it's asked for. Read it with sourceText().
    mutable std::string source;
    mutable bool sourceStale = false;
    mutable uint32_t sourceCrc = 0;          // crc32 of source, once known
    mutable bool sourceCrcKnown = false;
    const std::string& sourceText() const { if (sourceStale) { source = serialize(); sourceStale = false; sourceCrcKnown = false; } return source; }
    uint32_t sourceTextCrc() const;          // crc32 of sourceText() (what a download checks; the same as crc until an edit)

    bool load(const std::string& name, std::string* err = nullptr);
    bool onLadder(const AABB& player) const { for (auto& l : ladders) if (player.overlaps(l)) return true; return false; }
    // Never throws: a bad map is an error. Every field is reset first (only name is kept), so nothing of an earlier
    // map or of a failed parse is left behind.
    bool parse(const std::string& text, std::string* err = nullptr);
    bool parseText(const std::string& text, std::string* err);          // (parse's body)
    bool validate(std::string* err = nullptr) const;   // the limits above (counts, finite, in range)
    // Back to .qmap text (what the editor saves). Every number is written with as many digits as it needs to parse
    // back to the same float (spawn yaws to the same radians), so a saved map is exactly the map its edits made.
    std::string serialize() const;
    bool rebuild();                   // after editing brushes in place: bounds, wallbang flag, grid (false: over the limits)
    // One brush replaced, inserted or removed, with bounds, wallbang flag, lava list and grid left exactly as rebuild()
    // would leave them, but updated in place (the edit path: Midtown's full rebuild is ~0.3 ms, this a few us). The
    // generated brushes aren't remade (the caller keeps them right). false: over the grid's limit, nothing changed.
    bool setBrush(size_t i, const Brush& b) { return changeBrush(0, i, b); }
    bool insertBrush(size_t i, const Brush& b) { return changeBrush(1, i, b); }
    bool eraseBrush(size_t i) { return changeBrush(2, i, Brush()); }
    bool changeBrush(int op, size_t i, const Brush& b);

    // Movement collision: returns how far along `axis` the box may move (clamped delta).
    float sweepAxis(const AABB& box, int axis, float delta) const;
    bool boxSolid(const AABB& box) const;
    bool inLava(const AABB& box) const { for (auto& l : lava) if (box.mn.x < l.mx.x && box.mx.x > l.mn.x && box.mn.y < l.mx.y && box.mx.y > l.mn.y && box.mn.z < l.mx.z && box.mx.z > l.mn.z) return true; return false; }
    std::vector<AABB> lava;   // lava brushes (touch one and you die), from rebuild()
    // Returns hit distance or -1. normal receives the face normal on hit.
    float raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3* normal = nullptr) const;
    // Shadow-ray style early out: light, sight and line of fire, so clip (material 9) doesn't block it unless
    // hitClip (movement probes: clip blocks players).
    bool raycastAny(const Vec3& origin, const Vec3& dir, float maxDist, bool hitClip = false) const;
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
    // Cell c's brushes (indices, ascending) are cellBrushes[start, end); the slots up to the next cell's start are spare
    // room, so an edit can add a brush to a cell without rebuilding the grid.
    struct GridCell { uint32_t start = 0, end = 0; };
    std::vector<GridCell> cells;       // gridW*gridH
    std::vector<uint16_t> cellBrushes;
    size_t gridRefs = 0;               // brush references in use (spare room not counted)
    bool buildGrid();                  // false: more grid references than kMaxMapGridRefs
    static bool gridSolid(const Brush& b) { return b.material < 5 || b.material == 9; } // in the grid: far scenery, water, lava aren't
    template <class F> void forBox(const AABB& b, F f) const; // calls f(brush) for brushes in overlapped cells (may repeat)

    // Moving solids that aren't part of the static geometry (the payload cart). Collision and
    // raycasts include them; kept tiny so checking them all is free.
    std::vector<AABB> dynSolids;
    AABB payloadCartBox(float dist) const;
    float payloadLength() const;
    Vec3 payloadPoint(float dist) const;
    // Payload: where on the track the cart is after `progress` metres pushed. With unstaged spawns each team spawns
    // at its own end, so in the rounds team B attacks the cart starts at B's end and runs the track the other way.
    float cartTrack(int mode, int attackers, float progress) const;
    Vec3 cartPoint(int mode, int attackers, float progress) const { return payloadPoint(cartTrack(mode, attackers, progress)); }
};

// Map edit ops (the editor's undo steps and what multiplayer editing sends through the server), one line each:
//   <kind> ins|set|del <index> [fields]   kind: b brush (material + box), s spawn (team x y z yawDeg), l light (x y z radius
//                                         intensity), z bomb site (box), c checkpoint (box), p path point (x y z), h hill (has + box),
//                                         o multi point capture zone (box), d ladder (auto + box),
//                                         g group (type material dir box; ins makes it empty, set keeps its parts)
//   g part <index> <material> <6 fractions of the box>: adds a part to custom group <index>
//   (materials: 0..kMapMaterials-1 for boxes, groups and parts alike, trim 8 and clip 9 included)
//   t <title>    f <preferred mode, -1 any>    m <allowed modes bitmask, 0 = all>
// applyMapEdit validates (indices, sizes, finite numbers, at least one box and spawn) and updates the grid (in place
// for brushes and custom group parts). It doesn't serialize: m.source goes stale until m.sourceText() is asked for.
bool applyMapEdit(Map& m, const std::string& op);
// The op that sets (or inserts) object `idx` of `kind` to its current state in m: used to build edits and their undo.
std::string describeMapObject(const Map& m, char kind, const char* verb, int idx);

std::string findDataPath(const std::string& rel); // resolves shipped, read-only data (maps/..., mods/...) in the install
// Per-user data (settings, ranked key, demos, your maps, downloaded maps, light caches, HUD/controls profiles, custom
// modes, logs): never inside the install, so updates and uninstalls can't touch it.
//   Windows %APPDATA%\quark   macOS ~/Library/Application Support/quark   Linux $XDG_DATA_HOME/quark (~/.local/share/quark)
// --data-dir / QUARK_DATA_DIR override it. The first use of the default folder copies files left in the old
// install-folder locations over (originals are kept; a .migrated marker stops it running again).
void setUserDataDir(const std::string& dir, bool migrate = false); // call before the first userDataPath()
std::string userDataPath(const std::string& rel = ""); // <root>/<rel>; the root is created on first use
std::string userMapsDir();                              // your own / edited maps (searched before the shipped ones)
int migrateUserData(const std::string& oldRoot, const std::string& newRoot); // files copied (exposed for tests)
void setExecutableDir(const char* argv0);
const std::string& executableDir();       // empty if unknown
std::vector<std::string> listMaps();
// The file Map::load reads for a map name (old names mapped to the new ones): your maps folder first, then the
// shipped one. Empty for a name that isn't valid.
std::string mapFilePath(const std::string& name);
// A map name is a name, never a path: 1-40 letters, digits, - and _ (the editor's rule), and not a Windows device
// name (con, nul, com1, ...: opening "maps/con.qmap" opens the device). Names from servers and demos become file
// names (map, download, light cache, demo), so every one is checked with this.
bool validMapName(const std::string& name);
// A plain file name inside one folder (a HUD or controls profile): printable, no folder separators or drive, doesn't
// start with '.', at most 128 characters.
bool plainFileName(const std::string& name);
// Writes every shipped map back to its original text (and drops their light caches and your edited copies) and removes
// retired ones; returns how many. shippedDir: the install's maps folder (findDataPath("maps")) unless given (tests).
int resetOfficialMaps(const std::string& shippedDir = std::string());
std::string mapsFolder(); // absolute path of your maps folder (userMapsDir)
std::string typedMapName(const std::string& typed); // what the editor's name field keeps of what you type: validMapName's characters (space -> _), at most 40
std::string safeMapName(const std::string& name); // for building file names: anything else becomes '_', capped
// Maps fetched from servers live apart from your own, keyed by content: <user>/maps/downloads/<name>-<crc>.qmap
std::string downloadedMapPath(const std::string& name, uint32_t crc);
bool saveMapFile(const std::string& path, const std::string& text);
// Writes a file whole or not at all: a temp file next to it, then a rename over it, so a crash or a full disk
// leaves the old file (settings, profiles, the ranked key, custom modes, maps). privateFile: only this user may
// read it (POSIX 0600: the ranked key).
bool writeFileAtomic(const std::string& path, const std::string& data, bool privateFile = false);
uint32_t crc32(const void* data, size_t len, uint32_t prev = 0); // zlib's CRC-32; prev continues an earlier one
// A map's identity after applying edit op to the map whose identity was before (both ends of an edit session compute
// it; see Map::crc).
inline uint32_t mapEditCrc(uint32_t before, const std::string& op) { return crc32(op.data(), op.size(), before); }
