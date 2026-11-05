// The game's built-in models (players, weapon viewmodels, projectiles, the leaderboard crown, the props: arms, the
// bomb, the payload cart and its checkpoint gates, the grapple launcher arm and its hook) as lists of boxes in each model's own space. drawWorld draws these boxes; the asset export (quark_tests exportassets) turns the same
// lists into glTF files, so an export always matches the game. The owner's glTF overrides (docs/ASSETS.md) are drawn
// in their place: loaded once at startup into a ModelMesh, nothing at all when there are none.
//
// Model spaces (metres, +Y up, -Z forward, +X right):
//   player      origin on the ground between the feet, 2 m tall (kPlayerHeight), facing -Z
//   weapon      first-person viewmodel: origin at the weapon's pivot (where the HUD's viewmodel x / y / depth put it),
//               barrel along -Z; the game scales it by the HUD's viewmodel size
//   projectile  origin at the projectile, flying toward -Z
//   crown       origin at the top of the head (centred), front at -Z
//   prop        GUN_ARM / ARM: origin at the shoulder, reaching along -Z (the game turns it with the aim, yaw and
//               pitch, and scales it with the body); BOMB_PACK: the player's space (worn on the back, +Z);
//               BOMB: origin on the ground under it; CART: origin on the track under its middle, front (-Z) the way
//               it is pushed; GATE: origin on the track at the checkpoint, -Z the cart's way, the posts at +-X;
//               HOOK_ARM: the left arm with the grapple launcher, origin at the left shoulder like GUN_ARM, the
//               launcher's muzzle at kHookMuzzle; HOOK: the grapple's claw, origin where the rope ties on, prongs
//               toward -Z (sits in the launcher's barrel at kHookMuzzle + kHookSeat, flies out to the hook point)
// Pure C++ (no GL): the game, the exporter and the tests share it.
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../common/qmath.h"
#include "../game/gamedefs.h"

// Colour slots: a part (and an override's material, by name) follows a colour that changes at run time. An override
// mesh's colour in a slot is multiplied by (current colour / the slot's reference colour, ModelPalette below), so a
// model drawn in the exported colours looks like the default in game.
enum TintSlot : uint8_t { TINT_NONE = 0, TINT_TEAM, TINT_GLOW, TINT_METAL, TINT_BASE, TINT_ACCENT, TINT_COUNT };
const char* tintSlotName(int slot);                    // material names: "body", "team", "glow", "metal", "base", "accent"
int tintSlotFromName(const std::string& materialName); // "team", "Team_armour", "glow.001" ... -> slot (prefix, any case)

struct ModelPart {
    Vec3 c, h, color;          // centre, half extents, linear colour (may exceed 1: bright / emissive)
    uint8_t slot = TINT_NONE;
    bool shadow = true;        // casts a shadow
    bool visOnly = false;      // drawn only when the model is seen from outside (face plates: not on your own body)
    float alpha = 1;           // < 1: translucent (the bubble's shell)
    // A box model's file (exportModelParts, an override's extras.quark.boxes): how the colour follows its slot, drawn
    // colour = slot colour * mul + team colour * teamMul + add (per channel; the default HEAVY's grey is team * 0.6 +
    // (0.06, 0.06, 0.07); teamMul: a part of another slot that also takes some team colour, the cart's core), and
    // whether a player model's crouch also squashes the half height (every centre's height follows it). The built-in
    // builders below don't fill these: they compute `color` themselves.
    Vec3 mul = {1, 1, 1}, add = {0, 0, 0}, teamMul = {0, 0, 0};
    bool crouch = false;
    // players: the joint the box hangs from (PlayerBone below; its `c` is where it sits in the standing pose). Every
    // other model, and a box model file without "bone", is all BONE_ROOT: drawn still.
    uint8_t bone = 0;
    // weapons: the moving part the box belongs to (WeaponPart below, "part" in the file; 0: the still body) and the
    // point it turns about or slides from ("pivot", model space). The first-person weapon draws it at pivot * its
    // motion (weaponPartMotion), like a mesh override's anim_ nodes.
    uint8_t wpart = 0;
    Vec3 pivot = {0, 0, 0};
};
using ModelParts = std::vector<ModelPart>;
constexpr size_t kMaxDefaultParts = 32;   // the most boxes in one default model (the tests check): the game reserves it

// ---- player animation (pure, no GL: the game, the preview and the tests share it) ----------------------------------
// A player model's boxes hang from these joints. Thighs turn at the hip (the top of the thigh boxes), shins at the
// knee (the bottom of the thigh boxes), feet at the ankle (the top of the foot boxes: kept level with the floor), the
// pelvis carries both legs (turned toward where you move), the torso and head follow the aim (a little of its pitch).
// ROOT: never moves.
enum PlayerBone : uint8_t { BONE_ROOT = 0, BONE_PELVIS, BONE_TORSO, BONE_HEAD, BONE_THIGH_L, BONE_SHIN_L, BONE_THIGH_R, BONE_SHIN_R, BONE_FOOT_L, BONE_FOOT_R, BONE_COUNT };
const char* boneName(int bone);                   // "root", "pelvis", "torso", "head", "thigh_l", "shin_l", "thigh_r", "shin_r", "foot_l", "foot_r"
int boneFromName(const std::string& name);        // -1: unknown
// Where a model's joints are, measured from its boxes (in model space, standing): the hips at the top of the thighs,
// the knees at the top of the shins, the neck at the bottom of the head. A model without leg bones: animated = false.
struct PlayerRig {
    float hipX = 0.14f, hipY = 0.88f, kneeY = 0.46f, ankleY = 0, neckY = 1.44f, waistY = 0.88f;
    float footHalf = 0;   // a shin box down on the floor (no foot bone): how far it reaches forward / back (its toe / heel)
    bool animated = false;
};
void playerRig(const ModelParts& parts, PlayerRig& out);
// What a pose is made from (the game keeps one a player: AnimTrack): the gait's phase (cycles: 0 and 0.5 have the legs
// together, the left leg forward at 0.25), how fast they move over the ground (m/s: walk -> run), the legs' turn from
// the aim (radians, + = toward the left like a yaw), how far into the jump pose (0 ground, 1 air) and the aim's pitch.
struct AnimState { float phase = 0, speed = 0, legYaw = 0, air = 0, pitch = 0; };
// One matrix a bone, model space to model space (identity: the standing pose)
struct PlayerPose { Mat4 bone[BONE_COUNT]; };
void playerPose(const AnimState& s, const PlayerRig& rig, PlayerPose& out);
// The client's per-player tracking (one a player slot, fixed: no allocation). animStep moves it on by dt from where the
// player is drawn now: velocity from the drawn movement, the phase by distance over the stride, the legs' turn, the air.
struct AnimTrack {
    Vec3 lastPos = {0, 0, 0}, vel = {0, 0, 0};
    float legWorldYaw = 0;   // the legs' facing in the world (radians)
    bool init = false, back = false, turning = false;
    AnimState s;
};
// ground: on the floor (own player: the prediction's flag; others: a short ray down from the feet)
void animStep(AnimTrack& t, const Vec3& pos, float aimYaw, float aimPitch, bool ground, float dt);
// The tuning, in one place (retime the animations here): metres, radians, seconds
struct AnimTuning {
    float walkStride, runStride;          // ground covered by one full cycle (two steps) walking / running
    float walkSpeed, runSpeed;            // m/s: the blend from the walk to the run cycle
    float maxCycleRate;                   // cycles a second at most (strafe-hopping speeds: the stride grows instead)
    float startSpeed;                     // m/s: slower than this is standing (the legs ease to still)
    float walkSwing, runSwing;            // thigh swing either way of straight down
    float walkKnee, runKnee;              // knee bend at mid-swing
    float stanceKnee;                     // the planted leg's slight bend
    float runLean;                        // torso lean forward at run speed
    float legTurnMax;                     // the legs' turn from the aim at most (either way)
    float legTurnRate;                    // how quickly the legs turn (1 / s, exponential)
    float idleTurnStart, idleTurnStop;    // standing: the legs follow the aim once it is this far off, until this close
    float backEnter, backLeave;           // back-pedal once the move is this far from forward, until it's this close
    float airThighL, airThighR, airKneeL, airKneeR, airLean;   // the tucked jump pose
    float airRate;                        // blend in / out of the air pose (1 / s)
    float headPitch, torsoPitch, pitchMax;   // how much of the aim's pitch the head / torso take, and at most
    float velSmooth;                      // the measured velocity's time constant (s)
    float teleport;                       // a move this far (m) in one frame: a respawn or teleport (reset)
};
extern const AnimTuning kAnim;
// a * b for matrices with no projection (last row 0 0 0 1): 36 multiplies, not 64 (posing runs per player a frame)
inline Mat4 mulAffine(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) {
        const float x = b.m[c * 4], y = b.m[c * 4 + 1], z = b.m[c * 4 + 2];
        for (int k = 0; k < 3; k++) r.m[c * 4 + k] = a.m[k] * x + a.m[4 + k] * y + a.m[8 + k] * z + (c == 3 ? a.m[12 + k] : 0.0f);
    }
    return r;
}
// A posed box's matrix: its bone's (from playerPose) with the box's centre c (model space, standing) carried through it
inline Mat4 posedPart(const PlayerPose& pose, uint8_t bone, const Vec3& c) {
    const Mat4& b = pose.bone[bone < BONE_COUNT ? bone : 0];
    Mat4 r = b;
    r.m[12] = b.m[0] * c.x + b.m[4] * c.y + b.m[8] * c.z + b.m[12];
    r.m[13] = b.m[1] * c.x + b.m[5] * c.y + b.m[9] * c.z + b.m[13];
    r.m[14] = b.m[2] * c.x + b.m[6] * c.y + b.m[10] * c.z + b.m[14];
    return r;
}

// The colour of each slot (index TintSlot; TINT_NONE unused).
struct ModelPalette { Vec3 c[TINT_COUNT] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}}; };

enum ModelKind { MODEL_PLAYER, MODEL_WEAPON, MODEL_PROJECTILE, MODEL_CROWN, MODEL_PROP, MODEL_KINDS };
constexpr int kProjectileModels = WPN_COUNT + 1;  // per weapon type (only some fly as a model) + the plain orb
constexpr int MODEL_PROJ_ORB = WPN_COUNT;
// The props: everything else drawWorld draws as a model (not the map, explosions, tracers or zone volumes)
// (new props go at the end: an override's and the export's paths are by name, but older indexes stay put)
enum PropModel { PROP_GUN_ARM, PROP_ARM, PROP_BOMB_PACK, PROP_BOMB, PROP_CART, PROP_GATE, PROP_HOOK_ARM, PROP_HOOK, PROP_COUNT };
// The grapple launcher on the left arm (PROP_HOOK_ARM space): where the rope leaves it (the barrel's mouth), and where
// the claw (PROP_HOOK) sits in the barrel while the hook is in: kHookMuzzle + kHookSeat (the shank inside, prongs out).
// The game starts the rope here (first and third person), so the model and the rope agree.
constexpr Vec3 kHookMuzzle = {0, 0.07f, -0.66f};
constexpr Vec3 kHookSeat = {0, 0, 0.07f};
int modelCount(int kind);                         // 5 players, 11 weapons, 12 projectile slots, 1 crown, 8 props
bool modelExists(int kind, int idx);              // projectiles: ROCKET, KNIFE, STICKY, CROSSBOW, BUBBLE, SYRINGE, ORB
const char* modelKindName(int kind);              // "player", "weapon", "projectile", "crown", "prop"
const char* modelName(int kind, int idx);         // "HEAVY", "RIFLE", "ORB", "CROWN", "CART"
std::string modelPath(int kind, int idx);         // "models/player/HEAVY" (the override / export path, no extension)
// the colours the export uses and override tints are relative to (props: each its own)
const ModelPalette& referencePalette(int kind, int idx = 0);

// The viewmodel's colours from the map's palette (display-space colours, as the Renderer holds them)
Vec3 viewmodelMetal(const Vec3& mapMetal);
Vec3 viewmodelBase(const Vec3& mapBase);
// The payload cart's metal and accent (its rim) from the map's palette
Vec3 cartMetal(const Vec3& mapMetal);
Vec3 cartAccent(const Vec3& mapAccent);

// Box lists. `out` is cleared first (its capacity is kept: the game reuses one list every frame).
// player: hs = crouch height scale (1 standing), pal team = the team colour
void playerModelParts(int model, float hs, const ModelPalette& pal, ModelParts& out);
// weapon: pal team = the accent colour, metal = viewmodelMetal(map metal), base = viewmodelBase(map base), glow
void viewmodelParts(int weapon, const ModelPalette& pal, ModelParts& out);
// projectile (WPN_* or MODEL_PROJ_ORB): flicker = rocket flame size (1 = still), core = sticky core brightness
// (1 = lit), wobble = the bubble's squash (0 = round)
void projectileParts(int type, const ModelPalette& pal, float flicker, float core, float wobble, ModelParts& out);
// crown: pal team = gold for #1, silver for 2-10 (crownColor)
void crownParts(const ModelPalette& pal, ModelParts& out);
Vec3 crownColor(int place);
// prop (PROP_*): BOMB: glow = its colour (dark; red while a planted one blinks); CART: team = the cart's colour (team
// or neutral grey), metal = cartMetal, accent = cartAccent, glow = the map's glow; GATE: team = grey, or the attackers'
// colour once the cart has reached it; HOOK_ARM: team = the player's team colour (a stripe on the launcher), metal = a
// fixed gunmetal (referencePalette: reads on any map); HOOK: metal = the same fixed steel; the rest have no colour slots
void propParts(int prop, const ModelPalette& pal, ModelParts& out);

// A default model's boxes as the export writes them (the reference colours, standing, still) with mul / add / crouch
// filled in, so a box model file of it draws exactly like the built-in model (overrideBoxParts).
void exportModelParts(int kind, int idx, ModelParts& out);
// The slots a model's drawing gives colours to (bit per TintSlot): a slot it doesn't drive stays at the reference
// colour, so an override's "glow" on a player is drawn as authored (tint 1), not at white / reference.
unsigned drivenSlots(int kind, int idx);
// pal with every slot the model doesn't drive set to its reference colour
ModelPalette drivenPalette(int kind, int idx, const ModelPalette& pal);
// A box model override's parts as drawn now: colours from pal (drivenPalette'd), the crouch (hs: players' height
// scale, 1 standing) applied. out is cleared first and reused (no allocation once it has the room).
// a box model file's parts once read: a slot part written without `mul` gets colour / the reference colour (how a
// mesh's colours are tinted), with add 0
void finishBoxModel(int kind, int idx, ModelParts& boxes);
void overrideBoxParts(int kind, int idx, const ModelParts& boxes, const ModelPalette& pal, float hs, ModelParts& out);

// A triangle mesh: what a glTF file holds for one model (one primitive per material).
struct MeshPrim {
    std::string material;          // "body" / "team" / "glow" / "metal" / "base" (or any name: tint by prefix)
    uint8_t slot = TINT_NONE;
    float alpha = 1;
    std::vector<float> pos, nrm, col; // xyz, xyz, rgb per vertex
    std::vector<uint32_t> idx;
    int anim = -1;                 // the anim node it belongs to (MeshData::anims), -1: the static body
};
// A glTF node named anim_<part> (docs/ASSETS.md "Animated parts"): a part the game moves about its pivot (the node's
// translation, model space). Its primitives (MeshPrim::anim) are in model space like the rest, assembled at the pivot,
// so anything that ignores anims (bounds, the export, the editors) sees the whole model at rest.
constexpr size_t kMaxAnimParts = 12;   // per model: anim nodes past these stay in the static body
struct MeshAnim {
    std::string name;   // the part ("pump" for anim_pump; lower case as written)
    Vec3 pivot;
};
struct MeshData {
    std::vector<MeshPrim> prims;
    std::vector<MeshAnim> anims;   // (empty: a model with no anim nodes, loaded exactly as before)
    bool bounds(Vec3& mn, Vec3& mx) const;
    size_t triangleCount() const;
};
// Boxes -> triangles, one primitive per (slot, alpha), with the box renderer's per-face shading baked into the colours
void partsToMesh(const ModelParts& parts, MeshData& out);

// An override ready to draw: flat triangle lists (opaque and translucent), each vertex with its slot.
struct ModelMesh {
    struct V { float pos[3], nrm[3], col[4]; uint8_t slot; };
    std::vector<V> opaque, translucent;   // 3 vertices per triangle
    Vec3 center;                          // bounding sphere (model space)
    float radius = 0;
    // a box model (its file's extras.quark.boxes, docs/ASSETS.md): drawn by the box path like a built-in model
    // (outlines, crouch per part, visOnly, shadows), the triangles above unused. Empty: a mesh.
    ModelParts boxes;
    // Animated parts (the file's anim_<part> nodes, at most kMaxAnimParts): each a mesh of its own whose triangles are
    // relative to its pivot (model space). The triangles above are the static body without them. Renderer::mesh /
    // viewMesh / viewMeshXf draw every part still at its pivot, so any model kind draws whole as it is; a draw path
    // that animates them (the first-person weapon: weaponPartMotion) draws the body with parts off and each part with
    // pivot * its motion. Empty (no anim nodes): nothing changes, nothing costs.
    std::vector<ModelMesh> parts;
    std::string name;                     // a part's name ("pump"); its pivot, and what the game moves it as
    Vec3 pivot = {0, 0, 0};
    uint8_t tag = 0;                      // (a part: a weapon's WeaponPart / a player's PlayerBone, set by loadModelOverrides; 0: none / root)
    // the whole model's bounding sphere, body and parts at rest (model space): what a posed player is culled by
    Vec3 allCenter = {0, 0, 0};
    float allRadius = 0;
    // a player mesh whose parts are joints (anim_<bone> nodes, docs/ASSETS.md "Player mesh parts"): the joints from
    // their pivots (playerMeshRig). rig.animated false: drawn still, as before.
    PlayerRig rig;
};
void meshToModel(const MeshData& mesh, ModelMesh& out);
// A player mesh: tags each part with its PlayerBone (anim_pelvis -> BONE_PELVIS; an unknown name: BONE_ROOT, still) and
// measures the rig from the pivots: hips at the thighs' (x: the legs' spacing), knees at the shins', ankles at the
// feet's, the neck at the head's, the waist at the torso's (else the hips). Needs both thighs and shins, else still.
void playerMeshRig(ModelMesh& m);

// ---- first-person weapon part motions (pure, no GL: the game and the tests share it) ------------------------------
// The parts a weapon model may name (anim_<name>, docs/ASSETS.md); each weapon moves its own ones from your predicted
// shots (WeaponAnimState). Others (and a part on a weapon that doesn't move it) stay still at their pivot.
enum WeaponPart : uint8_t { WPART_NONE = 0, WPART_BOLT, WPART_MAG, WPART_ROUND, WPART_SPIN, WPART_PUMP, WPART_DRUM, WPART_STRING, WPART_LEVER, WPART_PLUNGER, WPART_SWEEP, WPART_ANTENNA, WPART_COUNT };
const char* weaponPartName(int part);            // "bolt", "mag", ... ("" for NONE)
int weaponPartFromName(const std::string& name);  // "pump" -> WPART_PUMP; unknown: WPART_NONE
// What the motions are made from: the game keeps it for the weapon in your hands (your own shots, predicted)
struct WeaponAnimState {
    float since = 1e9f;    // seconds since your last shot with this weapon (primary or alt), 1e9: none yet
    float refire = 1;      // that shot's refire (shotRefire)
    unsigned shots = 0;    // shots fired with it (the sticky's drum: one chamber each)
    float spin = 0;        // laser: the coil's turn (radians, the game winds it up while you fire, down after)
    float clock = 0;       // seconds, wrapped (the locator's sweep)
    float wobble = 0;      // locator: the antenna's spring angle (radians, about X)
};
// The part's motion about its pivot (model space; drawn as pivot * this). false: hidden this frame (a round or bolt
// being reloaded). Identity for a part the weapon doesn't move.
bool weaponPartMotion(int weapon, int part, const WeaponAnimState& s, Mat4& out);
constexpr size_t kMaxOverrideTriangles = 20000;   // per model: more is refused (it streams every frame)
constexpr size_t kMaxOverrideBoxes = 256;         // per box model: more is refused (each is a box with an outline)

// The owner's glTF overrides of every model (loadModelOverrides, gltf.h), loaded once at startup: the shipped ones
// (assets/overrides/, every player has them) and yours (the per-user overrides folder). Ranked and time trials draw
// the shipped ones only: your own could make enemies bigger or brighter for you alone. Nothing loaded: all null.
constexpr int kMaxModelsPerKind = kProjectileModels;   // (the most models of one kind)
struct ModelOverrides {
    std::unique_ptr<ModelMesh> shipped[MODEL_KINDS][kMaxModelsPerKind], user[MODEL_KINDS][kMaxModelsPerKind];
    // what to draw for a model (null: its box model); shippedOnly: ranked (fairRules)
    const ModelMesh* get(int kind, int i, bool shippedOnly) const {
        if (kind < 0 || kind >= MODEL_KINDS || i < 0 || i >= kMaxModelsPerKind) return nullptr;
        const ModelMesh* m = shippedOnly ? nullptr : user[kind][i].get();
        return m ? m : shipped[kind][i].get();
    }
    size_t maxBoxes() const {   // (the most boxes of one box model: the game reserves its box list for it)
        size_t n = 0;
        for (int k = 0; k < MODEL_KINDS; k++)
            for (int i = 0; i < kMaxModelsPerKind; i++) {
                if (shipped[k][i]) n = std::max(n, shipped[k][i]->boxes.size());
                if (user[k][i]) n = std::max(n, user[k][i]->boxes.size());
            }
        return n;
    }
};
