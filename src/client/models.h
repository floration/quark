// The game's built-in models (players, weapon viewmodels, projectiles, the leaderboard crown, the props: arms, the
// bomb, the payload cart and its checkpoint gates) as lists of boxes in each model's own space. drawWorld draws these boxes; the asset export (quark_tests exportassets) turns the same
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
//               it is pushed; GATE: origin on the track at the checkpoint, -Z the cart's way, the posts at +-X
// Pure C++ (no GL): the game, the exporter and the tests share it.
#pragma once
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
};
using ModelParts = std::vector<ModelPart>;

// The colour of each slot (index TintSlot; TINT_NONE unused).
struct ModelPalette { Vec3 c[TINT_COUNT] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}}; };

enum ModelKind { MODEL_PLAYER, MODEL_WEAPON, MODEL_PROJECTILE, MODEL_CROWN, MODEL_PROP, MODEL_KINDS };
constexpr int kProjectileModels = WPN_COUNT + 1;  // per weapon type (only some fly as a model) + the plain orb
constexpr int MODEL_PROJ_ORB = WPN_COUNT;
// The props: everything else drawWorld draws as a model (not the map, explosions, tracers or zone volumes)
enum PropModel { PROP_GUN_ARM, PROP_ARM, PROP_BOMB_PACK, PROP_BOMB, PROP_CART, PROP_GATE, PROP_COUNT };
int modelCount(int kind);                         // 5 players, 11 weapons, 12 projectile slots, 1 crown, 6 props
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
// colour once the cart has reached it; the rest have no colour slots
void propParts(int prop, const ModelPalette& pal, ModelParts& out);

// A triangle mesh: what a glTF file holds for one model (one primitive per material).
struct MeshPrim {
    std::string material;          // "body" / "team" / "glow" / "metal" / "base" (or any name: tint by prefix)
    uint8_t slot = TINT_NONE;
    float alpha = 1;
    std::vector<float> pos, nrm, col; // xyz, xyz, rgb per vertex
    std::vector<uint32_t> idx;
};
struct MeshData {
    std::vector<MeshPrim> prims;
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
};
void meshToModel(const MeshData& mesh, ModelMesh& out);
constexpr size_t kMaxOverrideTriangles = 20000;   // per model: more is refused (it streams every frame)

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
};
