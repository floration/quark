#include "models.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

const char* tintSlotName(int s) {
    static const char* n[TINT_COUNT] = {"body", "team", "glow", "metal", "base", "accent"};
    return n[s < 0 || s >= TINT_COUNT ? 0 : s];
}

int tintSlotFromName(const std::string& name) {
    std::string l;
    for (char c : name) l += (char)std::tolower((unsigned char)c);
    for (int s = 1; s < TINT_COUNT; s++)
        if (l.compare(0, strlen(tintSlotName(s)), tintSlotName(s)) == 0) return s;
    return TINT_NONE;
}

static Vec3 lin(const Vec3& c) { return Vec3(std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f)); }
Vec3 viewmodelMetal(const Vec3& m) { return lin(m) * 1.6f + Vec3(0.05f, 0.05f, 0.05f); }
Vec3 viewmodelBase(const Vec3& b) { return lin(b) * 1.1f; }
Vec3 cartMetal(const Vec3& m) { return lin(m) * 1.6f + Vec3(0.04f, 0.04f, 0.04f); }
Vec3 cartAccent(const Vec3& a) { return lin(a) * 1.4f; }
Vec3 crownColor(int place) { return place == 1 ? Vec3{1.0f, 0.78f, 0.15f} : Vec3{0.82f, 0.85f, 0.9f}; }

int modelCount(int kind) {
    switch (kind) {
        case MODEL_PLAYER: return kPlayerModels;
        case MODEL_WEAPON: return WPN_COUNT;
        case MODEL_PROJECTILE: return kProjectileModels;
        case MODEL_CROWN: return 1;
        case MODEL_PROP: return PROP_COUNT;
    }
    return 0;
}
bool modelExists(int kind, int i) {
    if (i < 0 || i >= modelCount(kind)) return false;
    if (kind != MODEL_PROJECTILE) return true;
    return i == WPN_ROCKET || i == WPN_KNIFE || i == WPN_STICKY || i == WPN_CROSSBOW || i == WPN_BUBBLE || i == WPN_SYRINGE || i == MODEL_PROJ_ORB;
}
const char* modelKindName(int kind) {
    static const char* n[MODEL_KINDS] = {"player", "weapon", "projectile", "crown", "prop"};
    return kind >= 0 && kind < MODEL_KINDS ? n[kind] : "model";
}
const char* modelName(int kind, int i) {
    static const char* w[WPN_COUNT] = {"RIFLE", "ROCKET", "KNIFE", "SNIPER", "LASER", "SHOTGUN", "STICKY", "CROSSBOW", "BUBBLE", "SYRINGE", "LOCATOR"};
    switch (kind) {
        case MODEL_PLAYER: return playerModelName(i);
        case MODEL_WEAPON: return w[i < 0 || i >= WPN_COUNT ? 0 : i];
        case MODEL_PROJECTILE: return i >= 0 && i < WPN_COUNT ? w[i] : "ORB";
        case MODEL_CROWN: return "CROWN";
        case MODEL_PROP: {
            static const char* p[PROP_COUNT] = {"GUN_ARM", "ARM", "BOMB_PACK", "BOMB", "CART", "GATE", "HOOK_ARM", "HOOK"};
            return p[i < 0 || i >= PROP_COUNT ? 0 : i];
        }
    }
    return "";
}
std::string modelPath(int kind, int i) { return std::string("models/") + modelKindName(kind) + "/" + modelName(kind, i); }

static_assert(PROP_COUNT <= kMaxModelsPerKind, "ModelOverrides holds kMaxModelsPerKind models of a kind");

static ModelPalette makeReference(int kind, int idx = 0) {
    // team colours are white (a "team" material drawn white is exactly the team colour); the map colours are the
    // stock palette (the Renderer's defaults when a map sets none); the crown is the #1 gold; the props as they are
    // seen most: the cart's own metal and rim, the bomb dark (not blinking), a gate grey (not reached yet)
    ModelPalette p;
    p.c[TINT_GLOW] = {1.00f, 0.55f, 0.15f};
    p.c[TINT_METAL] = viewmodelMetal({0.30f, 0.33f, 0.37f});
    p.c[TINT_BASE] = viewmodelBase({0.62f, 0.62f, 0.64f});
    p.c[TINT_ACCENT] = cartAccent({0.40f, 0.40f, 0.43f});
    if (kind == MODEL_CROWN) p.c[TINT_TEAM] = crownColor(1);
    if (kind == MODEL_PROP && idx == PROP_CART) p.c[TINT_METAL] = cartMetal({0.30f, 0.33f, 0.37f});
    if (kind == MODEL_PROP && idx == PROP_BOMB) p.c[TINT_GLOW] = {0.15f, 0.15f, 0.15f};
    if (kind == MODEL_PROP && idx == PROP_GATE) p.c[TINT_TEAM] = {0.8f, 0.8f, 0.8f};
    // the grapple launcher and its claw: one fixed steel, not the map's metal (they must read on every map)
    if (kind == MODEL_PROP && (idx == PROP_HOOK_ARM || idx == PROP_HOOK)) p.c[TINT_METAL] = {0.5f, 0.53f, 0.58f};
    return p;
}
const ModelPalette& referencePalette(int kind, int idx) {
    static const ModelPalette pals[MODEL_KINDS] = {makeReference(0), makeReference(1), makeReference(2), makeReference(3), makeReference(4)};
    static const ModelPalette props[PROP_COUNT] = {makeReference(MODEL_PROP, 0), makeReference(MODEL_PROP, 1), makeReference(MODEL_PROP, 2),
                                                   makeReference(MODEL_PROP, 3), makeReference(MODEL_PROP, 4), makeReference(MODEL_PROP, 5),
                                                   makeReference(MODEL_PROP, 6), makeReference(MODEL_PROP, 7)};
    static_assert(PROP_COUNT == 8, "a reference palette per prop");
    if (kind == MODEL_PROP && idx >= 0 && idx < PROP_COUNT) return props[idx];
    return pals[kind < 0 || kind >= MODEL_KINDS ? 0 : kind];
}

namespace {
struct Emit {
    ModelParts& out;
    void operator()(const Vec3& c, const Vec3& h, const Vec3& col, uint8_t slot, bool shadow = true, bool visOnly = false, float alpha = 1) {
        ModelPart p; p.c = c; p.h = h; p.color = col; p.slot = slot; p.shadow = shadow; p.visOnly = visOnly; p.alpha = alpha;
        out.push_back(p);
    }
};
}

// Every model stands in the same hitbox (kPlayerHalfWidth x kPlayerHeight, the head its top kHeadFraction): models are
// cosmetic. Heights are given standing; a crouch (hs < 1) squashes the legs and body, the head keeps its size.
// Each box hangs from a joint (PlayerBone): leg() splits a leg into a thigh and a shin at the knee, shin guards hang
// from the shin, boots and shoes from the foot, the head's boxes from the head, everything else from the torso (SIMPLE: all still).
void playerModelParts(int model, float hs, const ModelPalette& pal, ModelParts& out) {
    out.clear();
    Emit e{out};
    const Vec3 col = pal.c[TINT_TEAM];
    const Vec3 grey = col * 0.6f + Vec3(0.06f, 0.06f, 0.07f), dark = {0.05f, 0.05f, 0.06f};
    const uint8_t T = TINT_TEAM, N = TINT_NONE;
    uint8_t cb = BONE_TORSO;   // the bone b() hangs its boxes from
    // b(x, y, z, hx, hy, hz): a body box (y and hy follow the crouch); h(...): a head box (its centre follows, not its size)
    auto b = [&](float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        e(Vec3(x, y * hs, z), Vec3(hx, hy * hs, hz), c, s, sh, vis); out.back().bone = cb; };
    auto h = [&](float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        e(Vec3(x, y * hs, z), Vec3(hx, hy, hz), c, s, sh, vis); out.back().bone = BONE_HEAD; };
    // the side's bones (sd -1: the left leg, +X is the right)
    auto thigh = [](float sd) { return (uint8_t)(sd < 0 ? BONE_THIGH_L : BONE_THIGH_R); };
    auto shin = [](float sd) { return (uint8_t)(sd < 0 ? BONE_SHIN_L : BONE_SHIN_R); };
    // a box down the leg from y0 to y1, cut at the knee kn: the thigh part reaches 2 cm under the knee (a bent knee
    // shows no gap), the shin part is 5 mm slimmer (no flicker where they overlap); y0 inside the boot (the ankle)
    auto leg = [&](float sd, float x, float z, float y0, float y1, float kn, float hx, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        const float ov = 0.02f;
        cb = thigh(sd); b(x, (kn - ov + y1) * 0.5f, z, hx, (y1 - kn + ov) * 0.5f, hz, c, s, sh, vis);
        cb = shin(sd);  b(x, (y0 + kn) * 0.5f, z, hx - 0.005f, (kn - y0) * 0.5f, hz - 0.005f, c, s, sh, vis);
        cb = BONE_TORSO;
    };
    auto foot = [](float sd) { return (uint8_t)(sd < 0 ? BONE_FOOT_L : BONE_FOOT_R); };
    auto onFoot = [&](float sd, float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        cb = foot(sd); b(x, y, z, hx, hy, hz, c, s, sh, vis); cb = BONE_TORSO; };
    auto onShin = [&](float sd, float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        cb = shin(sd); b(x, y, z, hx, hy, hz, c, s, sh, vis); cb = BONE_TORSO; };
    auto onThigh = [&](float sd, float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        cb = thigh(sd); b(x, y, z, hx, hy, hz, c, s, sh, vis); cb = BONE_TORSO; };
    const Vec3 skin = {0.72f, 0.52f, 0.4f}, rot = {0.36f, 0.5f, 0.3f}, eyeW = {1.6f, 1.6f, 1.5f};
    switch (model) {
        case 1: // HEAVY: wide armoured legs and boots, a big chest plate with a belt, shoulder pads, a small helmet
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.15f, 0, 0.12f, 0.84f, 0.42f, 0.14f, 0.2f, col * 0.45f, T);
                onFoot(sd, sd * 0.15f, 0.08f, -0.03f, 0.16f, 0.08f, 0.24f, dark, N);
                onThigh(sd, sd * 0.15f, 0.5f, -0.2f, 0.11f, 0.08f, 0.02f, grey, T, false, true);
                b(sd * 0.42f, 1.38f, 0, 0.14f, 0.1f, 0.22f, grey, T);
                b(sd * 0.42f, 1.24f, 0, 0.1f, 0.06f, 0.16f, col * 0.45f, T);
            }
            b(0, 1.13f, 0, 0.4f, 0.3f, 0.27f, col, T);
            b(0, 1.15f, -0.27f, 0.24f, 0.18f, 0.02f, grey, T, false, true);
            b(0, 0.86f, 0, 0.36f, 0.04f, 0.25f, dark, N);
            b(0, 0.86f, -0.25f, 0.06f, 0.035f, 0.01f, col * 1.5f, T, false, true);
            b(0, 1.12f, 0.29f, 0.22f, 0.22f, 0.05f, grey * 0.8f, T);
            h(0, 1.6f, 0, 0.18f, 0.16f, 0.18f, col * 1.1f, T);
            h(0, 1.6f, -0.18f, 0.15f, 0.04f, 0.02f, dark, N, false, true);
            h(0, 1.77f, 0, 0.12f, 0.02f, 0.14f, grey, T, false);
            break;
        case 2: // SIMPLE: just the hitbox: the body box and the head box, nothing else (still: no joints)
            b(0, 0.72f, 0, kPlayerHalfWidth, 0.72f, kPlayerHalfWidth, col, T);
            h(0, 1.62f, 0, kPlayerHalfWidth * kHeadWidthFraction, 0.18f, kPlayerHalfWidth * kHeadWidthFraction, col * 1.25f, T);
            for (ModelPart& q : out) q.bone = BONE_ROOT;
            break;
        case 3: // ROBOT: grey frame on piston legs, team chest with a glowing core, cube head, eye bar, antenna
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.14f, 0, 0.2f, 0.8f, 0.5f, 0.1f, 0.12f, grey * 0.8f, T);
                onFoot(sd, sd * 0.14f, 0.12f, -0.03f, 0.13f, 0.12f, 0.19f, grey * 0.6f, T);
                leg(sd, sd * 0.14f, -0.12f, 0.3f, 0.7f, 0.5f, 0.04f, 0.01f, dark, N, false, true);
                b(sd * 0.41f, 1.33f, 0, 0.08f, 0.12f, 0.16f, dark, N);
            }
            b(0, 0.83f, 0, 0.3f, 0.05f, 0.2f, dark, N);
            b(0, 1.14f, 0, 0.36f, 0.27f, 0.26f, grey, T);
            b(0, 1.18f, -0.26f, 0.24f, 0.16f, 0.02f, col, T, false, true);
            b(0, 1.18f, -0.28f, 0.07f, 0.07f, 0.01f, col * 2.0f + Vec3(0.4f, 0.4f, 0.4f), T, false, true);
            b(0, 1.12f, 0.27f, 0.18f, 0.18f, 0.02f, dark, N);
            h(0, 1.45f, 0, 0.08f, 0.04f, 0.08f, dark, N);
            h(0, 1.63f, 0, 0.18f, 0.15f, 0.18f, grey * 1.15f, T);
            h(0, 1.65f, -0.18f, 0.13f, 0.03f, 0.02f, col * 2.0f + Vec3(0.4f, 0.4f, 0.4f), T, false, true);
            for (float sd : {-1.0f, 1.0f}) h(sd * 0.19f, 1.63f, 0, 0.02f, 0.06f, 0.06f, dark, N);
            h(0.08f, 1.84f, 0.05f, 0.012f, 0.06f, 0.012f, grey, T, false);
            h(0.08f, 1.92f, 0.05f, 0.03f, 0.03f, 0.03f, col * 1.8f, T, false);
            break;
        case 4: // KNIGHT: plate legs and sabatons, breastplate, team tabard, pauldrons, belt, helmet with a visor and crest
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.14f, 0, 0.08f, 0.88f, 0.44f, 0.12f, 0.16f, grey * 0.85f, T);
                onFoot(sd, sd * 0.14f, 0.06f, -0.04f, 0.13f, 0.06f, 0.2f, grey * 0.6f, T);
                onThigh(sd, sd * 0.14f, 0.52f, -0.16f, 0.09f, 0.06f, 0.02f, grey * 1.1f, T, false, true);
                b(sd * 0.38f, 1.38f, 0, 0.13f, 0.09f, 0.2f, grey * 1.1f, T);
                b(sd * 0.38f, 1.27f, 0, 0.11f, 0.05f, 0.18f, grey * 0.8f, T);
            }
            b(0, 1.13f, 0, 0.32f, 0.29f, 0.23f, grey, T);
            b(0, 0.98f, -0.23f, 0.18f, 0.38f, 0.02f, col, T, false, true);
            b(0, 1.12f, -0.25f, 0.06f, 0.12f, 0.01f, grey * 1.3f, T, false, true);
            b(0, 0.87f, 0, 0.33f, 0.04f, 0.24f, dark, N);
            b(0, 1.0f, 0.24f, 0.2f, 0.32f, 0.02f, col * 0.7f, T);
            h(0, 1.6f, 0, 0.18f, 0.18f, 0.18f, grey * 1.1f, T);
            h(0, 1.62f, -0.18f, 0.14f, 0.022f, 0.02f, dark, N, false, true);
            h(0, 1.52f, -0.19f, 0.1f, 0.05f, 0.01f, grey * 1.3f, T, false, true);
            h(0, 1.84f, 0, 0.025f, 0.07f, 0.18f, col * 1.1f, T, false);
            break;
        case 5: // WIZARD: a long team robe flaring at the hem, a sash, a beard, a tall pointed hat with a band and a star;
                // under the robe, legs (hidden, no shadow) whose shoes peek out at the front as they step
            for (float sd : {-1.0f, 1.0f}) {
                onThigh(sd, sd * 0.12f, 0.61f, 0, 0.07f, 0.25f, 0.08f, col * 0.45f, T, false);
                onShin(sd, sd * 0.12f, 0.22f, 0, 0.06f, 0.16f, 0.07f, col * 0.45f, T, false);
                onFoot(sd, sd * 0.12f, 0.04f, -0.12f, 0.07f, 0.04f, 0.12f, Vec3(0.12f, 0.08f, 0.05f), N);
            }
            cb = BONE_ROOT;     // (the hem stays on the floor as the body bobs; the robe above it turns with the legs)
            b(0, 0.18f, 0, 0.32f, 0.18f, 0.28f, col * 0.55f, T);
            cb = BONE_PELVIS;
            b(0, 0.6f, 0, 0.27f, 0.26f, 0.23f, col * 0.65f, T);
            cb = BONE_TORSO;
            b(0, 1.1f, 0, 0.3f, 0.26f, 0.22f, col * 0.8f, T);
            cb = BONE_PELVIS;
            b(0, 0.6f, -0.235f, 0.04f, 0.42f, 0.01f, col * 1.4f + Vec3(0.2f, 0.2f, 0.2f), T, false, true);
            cb = BONE_TORSO;
            b(0, 0.88f, 0, 0.31f, 0.035f, 0.23f, Vec3(0.75f, 0.6f, 0.2f), N);
            for (float sd : {-1.0f, 1.0f}) {
                b(sd * 0.36f, 1.15f, -0.02f, 0.1f, 0.2f, 0.12f, col * 0.75f, T);
                b(sd * 0.36f, 0.9f, -0.04f, 0.12f, 0.06f, 0.14f, col * 0.55f, T);
            }
            b(0, 1.38f, 0, 0.32f, 0.04f, 0.24f, col * 0.6f, T);
            h(0, 1.56f, 0, 0.15f, 0.14f, 0.15f, skin, N);
            h(0, 1.47f, -0.12f, 0.12f, 0.12f, 0.05f, Vec3(0.85f, 0.85f, 0.88f), N, false, true);
            for (float sd : {-1.0f, 1.0f}) h(sd * 0.06f, 1.6f, -0.155f, 0.025f, 0.015f, 0.01f, dark, N, false, true);
            h(0, 1.72f, 0, 0.26f, 0.02f, 0.26f, col * 0.7f, T, false);
            h(0, 1.8f, 0, 0.15f, 0.06f, 0.15f, col * 0.8f, T, false);
            h(0, 1.76f, 0, 0.155f, 0.02f, 0.155f, Vec3(0.75f, 0.6f, 0.2f), N, false);
            h(0, 1.92f, 0.03f, 0.09f, 0.07f, 0.09f, col * 0.85f, T, false);
            h(0, 2.03f, 0.08f, 0.04f, 0.05f, 0.04f, col * 0.9f, T, false);
            h(0, 1.81f, -0.155f, 0.03f, 0.03f, 0.005f, Vec3(1.6f, 1.4f, 0.5f), N, false, true);
            break;
        case 6: // ZOMBIE: torn team clothes over green skin, uneven legs, a hunched head with glowing eyes, arms reaching out
            leg(-1, -0.14f, 0, 0.06f, 0.84f, 0.42f, 0.12f, 0.15f, col * 0.4f, T);
            leg(1, 0.14f, 0.03f, 0.06f, 0.8f, 0.4f, 0.12f, 0.15f, col * 0.35f, T);
            onShin(1, 0.14f, 0.2f, -0.13f, 0.08f, 0.1f, 0.02f, rot, N, false, true);
            for (float sd : {-1.0f, 1.0f}) onFoot(sd, sd * 0.14f, 0.05f, -0.03f, 0.12f, 0.05f, 0.18f, Vec3(0.18f, 0.14f, 0.1f), N);
            b(0, 1.1f, 0.03f, 0.33f, 0.28f, 0.22f, col * 0.7f, T);
            b(0.12f, 0.95f, -0.2f, 0.12f, 0.12f, 0.02f, rot, N, false, true);
            b(-0.2f, 1.25f, -0.2f, 0.06f, 0.05f, 0.01f, Vec3(0.4f, 0.08f, 0.06f), N, false, true);
            for (float sd : {-1.0f, 1.0f}) {
                b(sd * 0.4f, 1.3f, -0.14f, 0.08f, 0.08f, 0.2f, col * 0.6f, T);
                b(sd * 0.4f, 1.3f, -0.4f, 0.07f, 0.07f, 0.08f, rot, N, true, true);
            }
            h(0, 1.42f, -0.06f, 0.07f, 0.05f, 0.07f, rot * 0.8f, N);
            h(0.02f, 1.56f, -0.1f, 0.17f, 0.16f, 0.17f, rot, N);
            for (float sd : {-1.0f, 1.0f}) h(sd * 0.07f, 1.6f, -0.27f, 0.03f, 0.025f, 0.01f, Vec3(1.8f, 1.6f, 0.4f), N, false, true);
            h(0.02f, 1.48f, -0.27f, 0.09f, 0.02f, 0.01f, Vec3(0.15f, 0.05f, 0.05f), N, false, true);
            h(-0.08f, 1.73f, -0.1f, 0.08f, 0.02f, 0.1f, Vec3(0.2f, 0.25f, 0.15f), N, false);
            break;
        case 7: // FIGHTER: shorts and wrapped legs, bare arms and a team belt, a vest, gloves up in a guard, a headband
                // (the legs reach up inside the shorts to the hips)
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.14f, 0, 0.05f, 0.8f, 0.34f, 0.1f, 0.12f, skin, N);
                onFoot(sd, sd * 0.14f, 0.04f, -0.03f, 0.11f, 0.04f, 0.16f, Vec3(0.9f, 0.9f, 0.9f), N);
                onShin(sd, sd * 0.14f, 0.12f, 0, 0.11f, 0.06f, 0.13f, Vec3(0.85f, 0.85f, 0.82f), N);
                b(sd * 0.36f, 1.22f, -0.12f, 0.07f, 0.07f, 0.16f, skin, N);
                b(sd * 0.3f, 1.4f, -0.3f, 0.09f, 0.09f, 0.09f, col * 1.2f, T, true, true);
            }
            b(0, 0.72f, 0, 0.27f, 0.14f, 0.18f, col * 0.5f, T);
            b(0, 0.86f, 0, 0.29f, 0.04f, 0.19f, col * 1.2f, T);
            b(0, 0.86f, -0.19f, 0.05f, 0.03f, 0.01f, Vec3(1.2f, 1.0f, 0.3f), N, false, true);
            b(0, 1.15f, 0, 0.32f, 0.25f, 0.2f, skin, N);
            b(0, 1.15f, 0, 0.33f, 0.24f, 0.17f, col * 0.8f, T);
            b(0, 1.15f, -0.2f, 0.1f, 0.22f, 0.01f, skin, N, false, true);
            h(0, 1.45f, 0, 0.08f, 0.05f, 0.08f, skin, N);
            h(0, 1.6f, 0, 0.16f, 0.16f, 0.16f, skin, N);
            h(0, 1.7f, 0, 0.165f, 0.025f, 0.165f, col * 1.2f, T, false);
            h(0.05f, 1.68f, 0.2f, 0.02f, 0.02f, 0.05f, col * 1.2f, T, false);
            h(0, 1.77f, 0.02f, 0.165f, 0.03f, 0.15f, Vec3(0.1f, 0.07f, 0.05f), N, false);
            for (float sd : {-1.0f, 1.0f}) h(sd * 0.06f, 1.62f, -0.165f, 0.025f, 0.015f, 0.01f, dark, N, false, true);
            break;
        case 8: // PIRATE: boots and striped trousers, a team coat with a sash, a belt buckle, a beard, an eye patch,
                // a tricorn hat with a skull badge
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.14f, 0, 0.06f, 0.88f, 0.44f, 0.12f, 0.15f, Vec3(0.85f, 0.85f, 0.8f), N);
                onShin(sd, sd * 0.14f, 0.25f, -0.01f, 0.13f, 0.15f, 0.17f, Vec3(0.12f, 0.08f, 0.05f), N);   // (the boot's shaft, its foot)
                onFoot(sd, sd * 0.14f, 0.05f, -0.01f, 0.13f, 0.05f, 0.17f, Vec3(0.12f, 0.08f, 0.05f), N);
                onThigh(sd, sd * 0.14f, 0.55f, -0.155f, 0.12f, 0.03f, 0.01f, col * 0.6f, T, false, true);
                b(sd * 0.35f, 1.3f, 0, 0.1f, 0.12f, 0.18f, col * 0.8f, T);
            }
            b(0, 1.08f, 0, 0.32f, 0.3f, 0.22f, col, T);
            b(0, 0.82f, 0, 0.33f, 0.08f, 0.23f, col * 0.75f, T);
            b(0, 0.92f, 0, 0.33f, 0.04f, 0.23f, Vec3(0.12f, 0.08f, 0.05f), N);
            b(0, 0.92f, -0.235f, 0.05f, 0.035f, 0.01f, Vec3(0.9f, 0.75f, 0.25f), N, false, true);
            b(0, 1.15f, -0.225f, 0.12f, 0.2f, 0.01f, Vec3(0.9f, 0.9f, 0.85f), N, false, true);
            h(0, 1.57f, 0, 0.16f, 0.15f, 0.16f, skin, N);
            h(0, 1.47f, -0.13f, 0.13f, 0.08f, 0.04f, Vec3(0.2f, 0.12f, 0.06f), N, false, true);
            h(-0.06f, 1.6f, -0.165f, 0.04f, 0.03f, 0.01f, dark, N, false, true);
            h(0.06f, 1.6f, -0.165f, 0.022f, 0.015f, 0.01f, dark, N, false, true);
            h(0, 1.73f, 0, 0.26f, 0.03f, 0.2f, dark, N, false);
            h(0, 1.81f, 0, 0.17f, 0.06f, 0.15f, dark, N, false);
            h(0, 1.77f, -0.21f, 0.03f, 0.03f, 0.005f, Vec3(1.2f, 1.2f, 1.1f), N, false, true);
            break;
        case 9: // NINJA: dark wrapped suit, tabi boots, a team sash and belt knot, arm guards, a hood with an eye slit and
                // the headband tails flying behind
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.13f, 0, 0.06f, 0.88f, 0.44f, 0.11f, 0.14f, dark * 2.0f, N);
                onFoot(sd, sd * 0.13f, 0.05f, -0.03f, 0.115f, 0.05f, 0.17f, dark, N);
                onShin(sd, sd * 0.13f, 0.3f, 0, 0.115f, 0.03f, 0.145f, dark * 3.0f, N);
                b(sd * 0.35f, 1.28f, 0, 0.09f, 0.11f, 0.15f, dark * 2.4f, N);
                b(sd * 0.35f, 1.12f, 0, 0.095f, 0.04f, 0.155f, col * 0.8f, T);
            }
            b(0, 1.1f, 0, 0.3f, 0.28f, 0.2f, dark * 2.0f, N);
            b(0, 0.88f, 0, 0.31f, 0.045f, 0.21f, col, T);
            b(0.12f, 0.8f, -0.21f, 0.04f, 0.08f, 0.01f, col, T, false, true);
            b(0, 1.12f, 0.21f, 0.04f, 0.26f, 0.02f, Vec3(0.3f, 0.3f, 0.32f), N);
            h(0, 1.58f, 0, 0.16f, 0.16f, 0.16f, dark * 2.4f, N);
            h(0, 1.61f, -0.165f, 0.12f, 0.025f, 0.01f, skin, N, false, true);
            for (float sd : {-1.0f, 1.0f}) h(sd * 0.05f, 1.61f, -0.172f, 0.02f, 0.012f, 0.005f, dark, N, false, true);
            h(0, 1.69f, 0, 0.165f, 0.025f, 0.165f, col, T, false);
            h(0.05f, 1.67f, 0.24f, 0.02f, 0.015f, 0.08f, col, T, false);
            h(-0.03f, 1.64f, 0.26f, 0.02f, 0.015f, 0.09f, col, T, false);
            break;
        case 10: // BANDIT: boots and chaps, a team shirt under a dark vest, a gun belt with a holster, a bandana over the
                 // face, a wide-brimmed hat with a team band
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.14f, 0, 0.18f, 0.88f, 0.44f, 0.12f, 0.15f, Vec3(0.3f, 0.22f, 0.15f), N);
                onFoot(sd, sd * 0.14f, 0.12f, -0.02f, 0.13f, 0.12f, 0.17f, Vec3(0.18f, 0.12f, 0.07f), N);
                leg(sd, sd * 0.14f, -0.155f, 0.2f, 0.8f, 0.44f, 0.1f, 0.01f, Vec3(0.4f, 0.3f, 0.2f), N, false, true);
                b(sd * 0.35f, 1.3f, 0, 0.09f, 0.11f, 0.16f, col * 0.85f, T);
            }
            b(0, 1.1f, 0, 0.31f, 0.28f, 0.21f, col * 0.85f, T);
            b(0, 1.1f, 0, 0.32f, 0.27f, 0.19f, Vec3(0.15f, 0.1f, 0.07f), N);
            b(0, 1.12f, -0.2f, 0.1f, 0.24f, 0.01f, col * 0.85f, T, false, true);
            b(0, 0.86f, 0, 0.33f, 0.045f, 0.22f, Vec3(0.2f, 0.13f, 0.07f), N);
            b(0.33f, 0.76f, 0, 0.04f, 0.1f, 0.06f, Vec3(0.2f, 0.13f, 0.07f), N);
            h(0, 1.57f, 0, 0.16f, 0.15f, 0.16f, skin, N);
            h(0, 1.51f, -0.04f, 0.17f, 0.08f, 0.13f, Vec3(0.6f, 0.12f, 0.1f), N, false);
            for (float sd : {-1.0f, 1.0f}) h(sd * 0.06f, 1.62f, -0.165f, 0.025f, 0.015f, 0.01f, dark, N, false, true);
            h(0, 1.72f, 0, 0.3f, 0.015f, 0.3f, Vec3(0.3f, 0.2f, 0.12f), N, false);
            h(0, 1.79f, 0, 0.17f, 0.06f, 0.17f, Vec3(0.3f, 0.2f, 0.12f), N, false);
            h(0, 1.745f, 0, 0.175f, 0.015f, 0.175f, col, T, false);
            break;
        default: // STANDARD: two legs with boots and knee pads, a jacket with a team chest, a belt, shoulders, a visor
            for (float sd : {-1.0f, 1.0f}) {
                leg(sd, sd * 0.14f, 0, 0.08f, 0.88f, 0.44f, 0.12f, 0.16f, col * 0.55f, T);
                onFoot(sd, sd * 0.14f, 0.06f, -0.03f, 0.13f, 0.06f, 0.19f, dark, N);
                onThigh(sd, sd * 0.14f, 0.5f, -0.16f, 0.09f, 0.07f, 0.02f, col * 0.35f, T, false, true);
                b(sd * 0.36f, 1.33f, 0, 0.1f, 0.1f, 0.2f, col * 0.8f, T);
            }
            b(0, 0.87f, 0, 0.3f, 0.04f, 0.2f, dark, N);
            b(0, 1.13f, 0, 0.34f, 0.24f, 0.22f, col, T);
            b(0, 1.15f, -0.22f, 0.2f, 0.14f, 0.02f, col * 1.3f, T, false, true);
            b(0, 1.15f, 0.24f, 0.16f, 0.18f, 0.04f, col * 0.55f, T);
            h(0, 1.42f, 0, 0.08f, 0.04f, 0.08f, col * 0.55f, T);
            h(0, 1.6f, 0, 0.18f, 0.16f, 0.18f, col * 1.15f, T);
            h(0, 1.62f, -0.18f, 0.16f, 0.05f, 0.02f, dark, N, false, true);
            h(0.12f, 1.62f, -0.2f, 0.03f, 0.03f, 0.01f, col * 1.6f, T, false, true);
            break;
    }
}


// ---- player animation -----------------------------------------------------------------------------------------------
// Quake Live-style legs: a gait cycle driven by the ground covered (the feet don't slide at walking speeds), the legs
// turned toward the move (back-pedalling plays the cycle backwards with the legs forward), a tucked pose in the air, the
// torso and head on the aim. Visual only: the hitbox, the netcode and the movement don't know about any of it.
const AnimTuning kAnim = {
    /*walkStride*/ 1.7f, /*runStride*/ 3.4f,
    /*walkSpeed*/ 2.5f, /*runSpeed*/ 7.0f,          // (the ground speed is 9.5 m/s, pmove.cpp kGroundSpeed; crouch 4)
    /*maxCycleRate*/ 2.2f,                           // (9.5 m/s would be 2.8 a second: strafe-hopping 15+ blurs the legs)
    /*startSpeed*/ 0.6f,
    /*walkSwing*/ 0.38f, /*runSwing*/ 0.52f,         // (22 / 30 degrees)
    /*walkKnee*/ 0.55f, /*runKnee*/ 1.25f,
    /*stanceKnee*/ 0.08f,
    /*runLean*/ 0.12f,
    /*legTurnMax*/ 1.05f,                            // (60 degrees)
    /*legTurnRate*/ 12.0f,
    /*idleTurnStart*/ 0.9f, /*idleTurnStop*/ 0.06f,
    /*backEnter*/ 1.92f, /*backLeave*/ 1.48f,        // (110 / 85 degrees: a pure strafe stays forward-facing)
    /*airThighL*/ 0.75f, /*airThighR*/ 0.35f, /*airKneeL*/ 1.45f, /*airKneeR*/ 0.95f, /*airLean*/ 0.1f,
    /*airRate*/ 12.0f,
    /*headPitch*/ 0.3f, /*torsoPitch*/ 0.1f, /*pitchMax*/ 0.35f,
    /*velSmooth*/ 0.05f,
    /*teleport*/ 3.0f,
};

const char* boneName(int b) {
    static const char* n[BONE_COUNT] = {"root", "pelvis", "torso", "head", "thigh_l", "shin_l", "thigh_r", "shin_r", "foot_l", "foot_r"};
    return b >= 0 && b < BONE_COUNT ? n[b] : "root";
}
int boneFromName(const std::string& s) {
    for (int b = 0; b < BONE_COUNT; b++) if (s == boneName(b)) return b;
    return -1;
}

void playerRig(const ModelParts& parts, PlayerRig& r) {
    r = PlayerRig{};
    float thighTop = -1e9f, thighBot = 1e9f, headBot = 1e9f, hx = 0;
    int nThigh = 0;
    unsigned have = 0;
    for (const ModelPart& q : parts) {
        if (q.bone >= BONE_COUNT) continue;
        have |= 1u << q.bone;
        const float top = q.c.y + q.h.y, bot = q.c.y - q.h.y;
        if (q.bone == BONE_THIGH_L || q.bone == BONE_THIGH_R) {
            thighTop = std::max(thighTop, top);
            thighBot = std::min(thighBot, bot);
            hx += std::fabs(q.c.x);
            nThigh++;
        } else if (q.bone == BONE_HEAD) headBot = std::min(headBot, bot);
    }
    const unsigned legs = 1u << BONE_THIGH_L | 1u << BONE_SHIN_L | 1u << BONE_THIGH_R | 1u << BONE_SHIN_R;
    r.animated = (have & legs) == legs && nThigh > 0 && thighTop > thighBot;
    if (!r.animated) return;
    r.hipY = r.waistY = thighTop;
    r.kneeY = thighBot;   // (the thigh's bottom: inside the overlap with the shin, so a bend shows no gap)
    r.hipX = hx / (float)nThigh;
    if (headBot < 1e8f) r.neckY = headBot;
    r.footHalf = r.ankleY = 0;
    for (const ModelPart& q : parts) {
        if ((q.bone == BONE_SHIN_L || q.bone == BONE_SHIN_R) && q.c.y - q.h.y < 0.05f) r.footHalf = std::max(r.footHalf, std::fabs(q.c.z) + q.h.z);
        if (q.bone == BONE_FOOT_L || q.bone == BONE_FOOT_R) r.ankleY = std::max(r.ankleY, q.c.y + q.h.y);
    }
    r.ankleY = std::min(r.ankleY, r.kneeY);
}

namespace {
// a turn about the X axis through (y, z = 0) by the angle whose cosine and sine are c, s: positive takes a part below
// the pivot forward (-Z)
inline Mat4 pivotX(float y, float c, float s) {
    Mat4 m;
    m.m[5] = c; m.m[6] = s; m.m[9] = -s; m.m[10] = c;
    m.m[13] = y - c * y;
    m.m[14] = -s * y;
    return m;
}
inline float wrapPi(float a) {
    if (!(std::fabs(a) < 1e4f)) return 0;
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}
}

void playerPose(const AnimState& s, const PlayerRig& rig, PlayerPose& out) {
    for (Mat4& m : out.bone) m = Mat4();
    if (!rig.animated) return;
    const AnimTuning& k = kAnim;
    const float sp = std::isfinite(s.speed) ? std::max(0.0f, s.speed) : 0.0f;
    const float air = std::isfinite(s.air) ? clampf(s.air, 0, 1) : 0.0f, gnd = 1 - air;
    const float run = clampf((sp - k.walkSpeed) / (k.runSpeed - k.walkSpeed), 0, 1);
    const float amt = clampf(sp / k.walkSpeed, 0, 1);   // (slower than walking: smaller steps)
    const float ph = (std::isfinite(s.phase) ? s.phase : 0.0f) * 6.28318531f;
    const float sw = lerpf(k.walkSwing, k.runSwing, run) * amt, kn = lerpf(k.walkKnee, k.runKnee, run) * amt;
    const float sn = std::sin(ph), cs = std::cos(ph);
    // the gait: the left thigh forward at phase 0.25, the right at 0.75; a knee bends most as its leg swings through
    float thL = sw * sn, thR = -sw * sn;
    float knL = kn * std::max(0.0f, cs) + k.stanceKnee * amt, knR = kn * std::max(0.0f, -cs) + k.stanceKnee * amt;
    // the air: tucked (one knee higher)
    thL = lerpf(thL, k.airThighL, air); thR = lerpf(thR, k.airThighR, air);
    knL = lerpf(knL, k.airKneeL, air); knR = lerpf(knR, k.airKneeR, air);
    // each joint's turn once (its cosine and sine serve the matrix and the feet's height)
    const float cTL = std::cos(thL), sTL = std::sin(thL), cTR = std::cos(thR), sTR = std::sin(thR);
    const float cKL = std::cos(knL), sKL = std::sin(knL), cKR = std::cos(knR), sKR = std::sin(knR);
    const float cSL = cTL * cKL + sTL * sKL, sSL = sTL * cKL - cTL * sKL;   // (the shins' angle to the floor: thigh - knee)
    const float cSR = cTR * cKR + sTR * sKR, sSR = sTR * cKR - cTR * sKR;
    // on the ground the lower foot stays on it: the body drops by as much as the legs' spread raises the feet (the
    // lowest point of a foot: the ankle's height straight down from the shin's end, less the toe or heel of a shin box
    // that reaches the floor as it tilts)
    const float lt = rig.hipY - rig.kneeY, ls = rig.kneeY - rig.ankleY;
    const float lowL = rig.hipY - lt * cTL - ls * cSL - rig.ankleY - rig.footHalf * std::fabs(sSL);
    const float lowR = rig.hipY - lt * cTR - ls * cSR - rig.ankleY - rig.footHalf * std::fabs(sSR);
    const float drop = std::max(std::min(lowL, lowR), -0.15f) * gnd;   // (a heel that would dip under the floor lifts the body)
    const float lean = k.runLean * run * amt * gnd + k.airLean * air;
    const float pitch = std::isfinite(s.pitch) ? s.pitch : 0.0f;
    const float tp = clampf(pitch * k.torsoPitch, -k.pitchMax, k.pitchMax);
    const float hp = clampf(pitch * k.headPitch, -k.pitchMax, k.pitchMax) - tp * 0.5f;
    const float ly = std::isfinite(s.legYaw) ? clampf(s.legYaw, -k.legTurnMax, k.legTurnMax) : 0.0f;
    Mat4 pelvis = Mat4::rotY(ly);
    pelvis.m[13] = -drop;
    out.bone[BONE_PELVIS] = pelvis;
    out.bone[BONE_THIGH_L] = mulAffine(pelvis, pivotX(rig.hipY, cTL, sTL));
    out.bone[BONE_THIGH_R] = mulAffine(pelvis, pivotX(rig.hipY, cTR, sTR));
    out.bone[BONE_SHIN_L] = mulAffine(out.bone[BONE_THIGH_L], pivotX(rig.kneeY, cKL, -sKL));
    out.bone[BONE_SHIN_R] = mulAffine(out.bone[BONE_THIGH_R], pivotX(rig.kneeY, cKR, -sKR));
    out.bone[BONE_FOOT_L] = mulAffine(out.bone[BONE_SHIN_L], pivotX(rig.ankleY, cSL, -sSL));   // (back level with the floor)
    out.bone[BONE_FOOT_R] = mulAffine(out.bone[BONE_SHIN_R], pivotX(rig.ankleY, cSR, -sSR));
    out.bone[BONE_TORSO] = pivotX(rig.waistY, std::cos(tp - lean), std::sin(tp - lean));   // (+ tips a part above the pivot back)
    out.bone[BONE_TORSO].m[13] -= drop;
    out.bone[BONE_HEAD] = mulAffine(out.bone[BONE_TORSO], pivotX(rig.neckY, std::cos(hp), std::sin(hp)));
}

void animStep(AnimTrack& t, const Vec3& pos, float aimYaw, float aimPitch, bool ground, float dt) {
    const AnimTuning& k = kAnim;
    t.s.pitch = std::isfinite(aimPitch) ? aimPitch : 0.0f;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) || !std::isfinite(aimYaw)) return;
    const Vec3 d = pos - t.lastPos;
    if (!t.init || length(d) > k.teleport) {   // first sight, a respawn or a teleport: start standing where they are
        t.lastPos = pos;
        t.vel = {0, 0, 0};
        t.legWorldYaw = aimYaw;
        t.back = t.turning = false;
        t.s.phase = 0; t.s.speed = 0; t.s.legYaw = 0;
        t.s.air = ground ? 0.0f : 1.0f;
        t.init = true;
        return;
    }
    if (!(dt > 0)) return;
    dt = std::min(dt, 0.1f);
    t.lastPos = pos;
    const float a = 1 - std::exp(-dt / k.velSmooth);
    t.vel = t.vel + (d * (1.0f / dt) - t.vel) * a;
    const float hs = std::sqrt(t.vel.x * t.vel.x + t.vel.z * t.vel.z);
    t.s.speed = hs;
    // the legs' facing
    float target = t.legWorldYaw;
    if (hs > k.startSpeed) {
        float rel = wrapPi(std::atan2(-t.vel.x, -t.vel.z) - aimYaw);
        if (std::fabs(rel) > k.backEnter) t.back = true;
        else if (std::fabs(rel) < k.backLeave) t.back = false;
        if (t.back) rel = wrapPi(rel + 3.14159265f);
        target = aimYaw + clampf(rel, -k.legTurnMax, k.legTurnMax);
        t.turning = false;
    } else {   // standing: the legs stay put until the aim is far enough round, then follow it
        const float off = wrapPi(t.legWorldYaw - aimYaw);
        if (std::fabs(off) > k.idleTurnStart) t.turning = true;
        if (t.turning) { target = aimYaw; if (std::fabs(off) < k.idleTurnStop) t.turning = false; }
        t.back = false;
    }
    t.legWorldYaw = wrapPi(t.legWorldYaw + wrapPi(target - t.legWorldYaw) * (1 - std::exp(-dt * k.legTurnRate)));
    float off = wrapPi(t.legWorldYaw - aimYaw);
    if (std::fabs(off) > k.legTurnMax) { off = off > 0 ? k.legTurnMax : -k.legTurnMax; t.legWorldYaw = wrapPi(aimYaw + off); }
    t.s.legYaw = off;
    // the cycle: by the ground covered (capped), frozen in the air; standing, the feet come together
    if (hs > k.startSpeed && t.s.air < 0.5f) {
        const float run = clampf((hs - k.walkSpeed) / (k.runSpeed - k.walkSpeed), 0, 1);
        const float rate = std::min(hs / lerpf(k.walkStride, k.runStride, run), k.maxCycleRate);
        t.s.phase += (t.back ? -1.0f : 1.0f) * rate * dt;
    } else if (hs <= k.startSpeed) {
        const float near = std::round(t.s.phase * 2) * 0.5f;
        t.s.phase += (near - t.s.phase) * (1 - std::exp(-dt * 8));
    }
    t.s.phase -= std::floor(t.s.phase);
    if (!std::isfinite(t.s.phase)) t.s.phase = 0;
    t.s.air += ((ground ? 0.0f : 1.0f) - t.s.air) * (1 - std::exp(-dt * k.airRate));
}

void viewmodelParts(int w, const ModelPalette& pal, ModelParts& out) {
    out.clear();
    Emit e{out};
    const Vec3 mid = pal.c[TINT_METAL], dark = mid * 0.55f, light = pal.c[TINT_BASE], acc = pal.c[TINT_TEAM], glow = pal.c[TINT_GLOW];
    const uint8_t M = TINT_METAL, B = TINT_BASE, A = TINT_TEAM, G = TINT_GLOW;
    auto part = [&](float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t slot) { e(Vec3(x, y, z), Vec3(hx, hy, hz), c, slot); };
    switch (w) {
        case WPN_RIFLE:      // receiver with an ejection port, handguard with vents, barrel, muzzle brake, front and rear
                             // sights, top rail, curved magazine, pistol grip, trigger guard, stock with a butt pad (17)
            part(0, 0, 0, 0.026f, 0.032f, 0.11f, dark, M);
            part(0.027f, 0.01f, -0.01f, 0.002f, 0.009f, 0.025f, mid, M);
            part(0, 0.004f, -0.15f, 0.022f, 0.024f, 0.06f, mid, M);
            for (int k = 0; k < 3; k++) part(0, 0.029f, -0.12f - k * 0.03f, 0.016f, 0.003f, 0.008f, dark, M);
            part(0, 0.008f, -0.26f, 0.008f, 0.008f, 0.06f, mid, M);
            part(0, 0.008f, -0.325f, 0.012f, 0.012f, 0.012f, dark, M);
            part(0, 0.03f, -0.29f, 0.003f, 0.014f, 0.004f, mid, M);
            part(0, 0.044f, 0.06f, 0.008f, 0.01f, 0.006f, mid, M);
            part(0, 0.036f, -0.03f, 0.01f, 0.004f, 0.08f, acc, A);
            part(0, -0.055f, -0.04f, 0.016f, 0.03f, 0.018f, mid, M);
            part(0, -0.09f, -0.03f, 0.016f, 0.012f, 0.018f, mid, M);
            part(0, -0.065f, 0.06f, 0.014f, 0.035f, 0.016f, dark, M);
            part(0, -0.04f, 0.01f, 0.004f, 0.004f, 0.03f, mid, M);
            part(0, -0.01f, 0.17f, 0.02f, 0.03f, 0.06f, dark, M);
            part(0, -0.01f, 0.235f, 0.022f, 0.036f, 0.006f, acc, A);
            break;
        case WPN_ROCKET:     // fat tube, flared muzzle and exhaust, glowing warhead band, accent stripes, heat shield,
                             // side scope, front grip, pistol grip, shoulder rest, a rocket's nose in the muzzle (18)
            part(0, 0, -0.02f, 0.045f, 0.045f, 0.2f, mid, M);
            part(0, 0, -0.23f, 0.055f, 0.055f, 0.02f, dark, M);
            part(0, 0, -0.245f, 0.035f, 0.035f, 0.004f, glow * 0.5f, G);
            part(0, 0, 0.19f, 0.052f, 0.052f, 0.015f, dark, M);
            part(0, 0, 0.21f, 0.04f, 0.04f, 0.006f, dark, M);
            part(0, 0, -0.14f, 0.0475f, 0.0475f, 0.005f, glow, G);
            part(0, 0, 0.05f, 0.047f, 0.047f, 0.006f, acc, A);
            part(0, 0, 0.11f, 0.047f, 0.047f, 0.006f, acc, A);
            part(0, 0.05f, -0.02f, 0.02f, 0.006f, 0.08f, dark, M);
            part(-0.058f, 0.025f, -0.06f, 0.012f, 0.02f, 0.03f, dark, M);
            part(-0.058f, 0.025f, -0.092f, 0.009f, 0.009f, 0.002f, glow, G);
            part(-0.058f, 0.025f, -0.025f, 0.007f, 0.007f, 0.005f, mid, M);
            part(0, -0.065f, -0.1f, 0.012f, 0.025f, 0.014f, dark, M);
            part(0, -0.07f, 0.04f, 0.014f, 0.03f, 0.02f, dark, M);
            part(0, -0.045f, 0.065f, 0.004f, 0.004f, 0.02f, mid, M);
            part(0, -0.05f, 0.14f, 0.03f, 0.008f, 0.04f, dark, M);
            part(0, 0, -0.215f, 0.025f, 0.025f, 0.012f, acc, A);
            part(0.047f, 0, 0.02f, 0.002f, 0.03f, 0.03f, light, B);
            break;
        case WPN_KNIFE:      // wrapped grip, cross guard with tips, blade with a spine, edge and point, a glowing fuller,
                             // accent pommel and lanyard ring (13)
            part(0, -0.01f, 0.04f, 0.012f, 0.016f, 0.05f, dark, M);
            for (int k = 0; k < 3; k++) part(0, -0.01f, 0.01f + k * 0.03f, 0.013f, 0.017f, 0.004f, mid, M);
            part(0, -0.01f, -0.015f, 0.035f, 0.008f, 0.008f, mid, M);
            part(-0.037f, -0.004f, -0.015f, 0.004f, 0.012f, 0.007f, dark, M); part(0.037f, -0.004f, -0.015f, 0.004f, 0.012f, 0.007f, dark, M);
            part(0, 0.004f, -0.11f, 0.005f, 0.006f, 0.09f, mid, M);
            part(0, -0.012f, -0.11f, 0.003f, 0.013f, 0.09f, light, B);
            part(0, -0.006f, -0.1f, 0.0055f, 0.003f, 0.06f, glow * 0.6f, G);
            part(0, -0.002f, -0.21f, 0.003f, 0.012f, 0.012f, light, B);
            part(0, -0.01f, 0.095f, 0.014f, 0.014f, 0.008f, acc, A);
            part(0, -0.01f, 0.11f, 0.005f, 0.008f, 0.006f, mid, M);
            break;
        case WPN_SNIPER:     // receiver, fluted barrel, muzzle brake with ports, scope with lens, caps and turrets on
                             // rings, bolt, magazine, bipod legs folded, grip, cheek rest and stock (20)
            part(0, 0, 0, 0.022f, 0.028f, 0.13f, dark, M);
            part(0, 0.006f, -0.27f, 0.007f, 0.007f, 0.15f, mid, M);
            part(0, 0.014f, -0.22f, 0.002f, 0.002f, 0.08f, dark, M);
            part(0, 0.006f, -0.43f, 0.012f, 0.01f, 0.018f, dark, M);
            part(0, 0.006f, -0.43f, 0.0125f, 0.004f, 0.004f, mid, M);
            part(0, 0.058f, -0.02f, 0.017f, 0.017f, 0.09f, dark, M);
            part(0, 0.058f, -0.115f, 0.02f, 0.02f, 0.008f, acc, A);
            part(0, 0.058f, -0.124f, 0.012f, 0.012f, 0.002f, glow, G);
            part(0, 0.058f, 0.075f, 0.019f, 0.019f, 0.006f, acc, A);
            part(0, 0.08f, -0.02f, 0.006f, 0.006f, 0.008f, mid, M); part(0.022f, 0.058f, -0.02f, 0.006f, 0.006f, 0.008f, mid, M);
            part(0, 0.036f, -0.06f, 0.006f, 0.01f, 0.006f, mid, M); part(0, 0.036f, 0.03f, 0.006f, 0.01f, 0.006f, mid, M);
            part(0.03f, 0.01f, 0.05f, 0.012f, 0.006f, 0.006f, light, B);
            part(0, -0.045f, -0.02f, 0.012f, 0.02f, 0.025f, mid, M);
            part(-0.008f, -0.02f, -0.24f, 0.003f, 0.003f, 0.06f, dark, M); part(0.008f, -0.02f, -0.24f, 0.003f, 0.003f, 0.06f, dark, M);
            part(0, -0.06f, 0.08f, 0.013f, 0.033f, 0.015f, dark, M);
            part(0, 0.02f, 0.16f, 0.016f, 0.01f, 0.05f, acc, A);
            part(0, -0.015f, 0.17f, 0.018f, 0.035f, 0.06f, dark, M);
            break;
        case WPN_LASER:      // chunky body with side fins, coil segments on top, capacitor glow down the side, emitter
                             // with three glowing rings, sight, grip and a power cell behind (19)
            part(0, 0, 0, 0.036f, 0.04f, 0.15f, mid, M);
            for (int k = 0; k < 4; k++) part(0, 0.046f, -0.09f + k * 0.05f, 0.022f, 0.008f, 0.012f, k % 2 ? dark : glow, k % 2 ? M : G);
            part(-0.04f, 0, -0.02f, 0.006f, 0.03f, 0.08f, dark, M); part(0.04f, 0, -0.02f, 0.006f, 0.03f, 0.08f, dark, M);
            part(0.047f, 0, -0.02f, 0.002f, 0.006f, 0.06f, glow, G);
            part(0, 0, -0.165f, 0.03f, 0.03f, 0.012f, dark, M);
            for (int k = 0; k < 3; k++) part(0, 0, -0.185f - k * 0.02f, 0.024f - k * 0.004f, 0.024f - k * 0.004f, 0.004f, glow * (1.0f + 0.25f * k), G);
            part(0, 0, -0.205f, 0.01f, 0.01f, 0.03f, dark, M);
            part(0, 0.06f, 0.06f, 0.006f, 0.008f, 0.006f, acc, A);
            part(0, -0.07f, 0.03f, 0.014f, 0.03f, 0.02f, dark, M);
            part(0, -0.035f, 0.12f, 0.022f, 0.02f, 0.03f, dark, M);
            part(0, -0.035f, 0.152f, 0.016f, 0.012f, 0.002f, acc, A);
            part(0, 0, 0.155f, 0.03f, 0.034f, 0.006f, dark, M);
            break;
        case WPN_SHOTGUN:    // twin barrels with a rib and bead, muzzle band, ribbed pump, shell carrier on the side,
                             // heavy receiver, trigger guard, grip and stock with a pad (16)
            part(-0.012f, 0.012f, -0.17f, 0.011f, 0.011f, 0.13f, mid, M);
            part(0.012f, 0.012f, -0.17f, 0.011f, 0.011f, 0.13f, mid, M);
            part(0, 0.025f, -0.17f, 0.003f, 0.002f, 0.13f, dark, M);
            part(0, 0.03f, -0.29f, 0.003f, 0.003f, 0.003f, light, B);
            part(0, 0.012f, -0.29f, 0.026f, 0.013f, 0.008f, dark, M);
            part(0, -0.018f, -0.15f, 0.022f, 0.016f, 0.06f, dark, M);
            for (int k = 0; k < 3; k++) part(0, -0.018f, -0.19f + k * 0.04f, 0.024f, 0.018f, 0.005f, mid, M);
            part(0, 0, 0.02f, 0.03f, 0.035f, 0.08f, dark, M);
            for (int k = 0; k < 3; k++) part(0.033f, 0.004f, -0.02f + k * 0.022f, 0.004f, 0.012f, 0.008f, acc, A);
            part(0, -0.045f, 0.05f, 0.005f, 0.008f, 0.025f, mid, M);
            part(0, -0.015f, 0.16f, 0.024f, 0.035f, 0.06f, mid, M);
            part(0, 0.038f, 0.0f, 0.008f, 0.004f, 0.06f, acc, A);
            break;
        case WPN_STICKY:     // drum magazine with accent bands and glowing windows, stubby wide barrel with a flared
                             // mouth, a canister on top with a glow strip, grip and stock (16)
            part(0, 0, -0.06f, 0.03f, 0.03f, 0.13f, mid, M);
            part(0, -0.02f, 0.04f, 0.055f, 0.055f, 0.04f, dark, M);
            part(0, -0.02f, 0.04f, 0.057f, 0.012f, 0.03f, acc, A);
            part(0, -0.02f, 0.04f, 0.012f, 0.057f, 0.03f, acc, A);
            part(-0.056f, 0.015f, 0.04f, 0.002f, 0.008f, 0.008f, glow, G); part(0.056f, 0.015f, 0.04f, 0.002f, 0.008f, 0.008f, glow, G);
            part(0, 0.045f, -0.02f, 0.014f, 0.014f, 0.05f, dark, M);
            part(0, 0.06f, -0.02f, 0.008f, 0.003f, 0.04f, glow, G);
            part(0, 0.045f, 0.035f, 0.01f, 0.01f, 0.006f, mid, M);
            part(0, 0, -0.2f, 0.036f, 0.036f, 0.012f, dark, M);
            part(0, 0, -0.218f, 0.042f, 0.042f, 0.006f, mid, M);
            part(0, 0, -0.222f, 0.022f, 0.022f, 0.003f, glow * 0.4f, G);
            part(0, -0.012f, -0.13f, 0.032f, 0.006f, 0.04f, dark, M);
            part(0, -0.075f, 0.1f, 0.014f, 0.03f, 0.016f, dark, M);
            part(0, -0.03f, 0.14f, 0.02f, 0.025f, 0.05f, mid, M);
            part(0, -0.03f, 0.192f, 0.022f, 0.028f, 0.004f, acc, A);
            break;
        case WPN_BUBBLE:     // a round soap tank on top (with a cap and a level window), a fat body with side bubbles,
                             // a flared wand nozzle with a ring and a bubble on it, grip (16)
            part(0, 0, 0, 0.03f, 0.035f, 0.11f, mid, M);
            part(0, 0.065f, 0.02f, 0.04f, 0.035f, 0.045f, light, B);
            part(0, 0.065f, 0.02f, 0.03f, 0.04f, 0.035f, glow, G);
            part(0, 0.108f, 0.02f, 0.014f, 0.006f, 0.014f, acc, A);
            part(0.041f, 0.065f, 0.02f, 0.002f, 0.025f, 0.006f, glow * 1.4f, G);
            part(0, 0.03f, 0.02f, 0.008f, 0.01f, 0.008f, dark, M);
            part(-0.033f, 0.01f, -0.04f, 0.006f, 0.006f, 0.006f, glow, G); part(0.033f, -0.01f, 0.05f, 0.008f, 0.008f, 0.008f, glow, G);
            part(0, 0.004f, -0.15f, 0.014f, 0.014f, 0.05f, dark, M);
            part(0, 0.004f, -0.12f, 0.018f, 0.018f, 0.005f, acc, A);
            part(0, 0.004f, -0.205f, 0.03f, 0.03f, 0.006f, acc, A);
            part(0, 0.004f, -0.235f, 0.022f, 0.022f, 0.022f, glow, G);
            part(0.02f, 0.03f, -0.27f, 0.008f, 0.008f, 0.008f, glow, G);
            part(0, -0.06f, 0.03f, 0.014f, 0.035f, 0.02f, dark, M);
            part(0, -0.035f, -0.005f, 0.004f, 0.004f, 0.02f, mid, M);
            part(0, 0, 0.115f, 0.028f, 0.03f, 0.008f, dark, M);
            break;
        case WPN_SYRINGE:    // glass barrel with dose marks, glowing serum, plunger rod, thumb pad and finger flange,
                             // hub, needle and a bright tip (14)
            part(0, 0, 0, 0.022f, 0.022f, 0.09f, light, B);
            part(0, 0, -0.01f, 0.016f, 0.016f, 0.07f, glow, G);
            for (int k = 0; k < 3; k++) part(0, 0.023f, -0.05f + k * 0.035f, 0.008f, 0.001f, 0.002f, dark, M);
            part(0, 0, 0.065f, 0.017f, 0.017f, 0.006f, dark, M);
            part(0, 0, 0.1f, 0.006f, 0.006f, 0.03f, mid, M); part(0, 0, 0.135f, 0.02f, 0.02f, 0.006f, dark, M);
            part(0, 0, 0.143f, 0.016f, 0.016f, 0.002f, acc, A);
            part(0, 0, 0.085f, 0.045f, 0.008f, 0.006f, acc, A);
            part(0, 0, -0.095f, 0.018f, 0.018f, 0.005f, dark, M);
            part(0, 0, -0.1f, 0.01f, 0.01f, 0.01f, dark, M); part(0, 0, -0.15f, 0.0025f, 0.0025f, 0.05f, mid, M);
            part(0, 0, -0.201f, 0.0015f, 0.0015f, 0.002f, glow * 1.5f, G);
            break;
        case WPN_LOCATOR:    // a handheld tracker: pistol grip, a slab body with a glowing radar screen on top, an emitter
                             // in front, a whip antenna with an accent tip (hide and seek / tag: the seeker's / tagger's)
            part(0, -0.055f, 0.045f, 0.015f, 0.04f, 0.02f, dark, M);
            part(0, -0.004f, -0.015f, 0.042f, 0.02f, 0.085f, mid, M);
            part(0, 0.018f, 0.004f, 0.036f, 0.004f, 0.058f, dark, M);
            part(0, 0.022f, 0.004f, 0.03f, 0.002f, 0.05f, glow * 0.55f, G);
            part(0, 0.0245f, -0.012f, 0.0025f, 0.001f, 0.026f, glow * 1.4f, G); part(0, 0.0245f, 0.004f, 0.022f, 0.001f, 0.0025f, glow * 1.4f, G);
            part(0.012f, 0.025f, -0.02f, 0.005f, 0.0015f, 0.005f, acc, A);
            part(0, -0.004f, -0.106f, 0.03f, 0.016f, 0.008f, dark, M); part(0, -0.004f, -0.116f, 0.018f, 0.009f, 0.003f, glow * 1.3f, G);
            part(0.032f, 0.05f, -0.07f, 0.0035f, 0.05f, 0.0035f, mid, M); part(0.032f, 0.104f, -0.07f, 0.007f, 0.007f, 0.007f, acc, A);
            part(-0.043f, -0.004f, -0.015f, 0.002f, 0.007f, 0.06f, acc, A);
            break;
        case WPN_CROSSBOW:   // stock with a cheek rest, rail, bow riser, limbs angled back with tips, a string to the
                             // latch, a bolt with a glowing head, a scope, foot stirrup, grip (19)
            part(0, 0, 0.02f, 0.02f, 0.025f, 0.2f, dark, M);
            part(0, 0.03f, 0.14f, 0.016f, 0.008f, 0.05f, acc, A);
            part(0, 0.027f, -0.06f, 0.008f, 0.002f, 0.12f, mid, M);
            part(0, 0.012f, -0.17f, 0.04f, 0.014f, 0.016f, dark, M);
            part(-0.075f, 0.012f, -0.165f, 0.04f, 0.008f, 0.01f, mid, M); part(0.075f, 0.012f, -0.165f, 0.04f, 0.008f, 0.01f, mid, M);
            part(-0.122f, 0.012f, -0.15f, 0.012f, 0.009f, 0.02f, mid, M); part(0.122f, 0.012f, -0.15f, 0.012f, 0.009f, 0.02f, mid, M);
            part(-0.134f, 0.012f, -0.135f, 0.004f, 0.012f, 0.006f, acc, A); part(0.134f, 0.012f, -0.135f, 0.004f, 0.012f, 0.006f, acc, A);
            part(0, 0.014f, -0.08f, 0.12f, 0.002f, 0.002f, light, B);
            part(0, 0.03f, -0.08f, 0.012f, 0.008f, 0.01f, dark, M);
            part(0, 0.032f, -0.12f, 0.004f, 0.004f, 0.11f, acc, A);
            part(0, 0.032f, -0.235f, 0.006f, 0.006f, 0.008f, glow, G);
            part(0, 0.06f, -0.02f, 0.012f, 0.012f, 0.05f, dark, M); part(0, 0.06f, -0.071f, 0.008f, 0.008f, 0.002f, glow, G);
            part(0, -0.01f, -0.215f, 0.02f, 0.025f, 0.004f, mid, M);
            part(0, -0.06f, 0.07f, 0.013f, 0.033f, 0.015f, dark, M);
            part(0, -0.03f, 0.03f, 0.004f, 0.006f, 0.02f, mid, M);
            break;
    }
}

// the rocket's flickering flame and its hot core, behind the nozzle (z 0.27): the box rocket's and a mesh rocket's
static std::array<ModelPart, 2> rocketFlame(float fl) {
    std::array<ModelPart, 2> f{};
    f[0].c = {0, 0, 0.36f}; f[0].h = {0.05f * fl, 0.05f * fl, 0.08f * fl}; f[0].color = {2.0f, 1.3f, 0.5f};
    f[1].c = {0, 0, 0.33f}; f[1].h = {0.025f * fl, 0.025f * fl, 0.05f * fl}; f[1].color = {2.4f, 2.2f, 1.6f};
    return f;
}

void rocketFlameParts(float fl, ModelParts& out) {
    out.clear();
    for (const ModelPart& q : rocketFlame(fl)) out.push_back(q);
}

void projectileParts(int type, const ModelPalette& pal, float fl, float core, float wob, ModelParts& out) {
    out.clear();
    Emit e{out};
    const Vec3 col = pal.c[TINT_TEAM], glow = pal.c[TINT_GLOW];
    const Vec3 metalC = {0.22f, 0.23f, 0.26f};
    const uint8_t T = TINT_TEAM, N = TINT_NONE;
    switch (type) {
        case WPN_STICKY: // a spiked mine: dark shell, a team-coloured core showing through slots, six spikes with tips,
                         // a blinking light (the game spins it)
            e({0, 0, 0}, {0.1f, 0.1f, 0.1f}, metalC, N);
            e({0, 0, 0}, {0.075f, 0.12f, 0.075f}, col * 1.3f * core, T);
            e({0, 0, 0}, {0.12f, 0.075f, 0.075f}, col * 1.1f * core, T);
            e({0, 0, 0}, {0.075f, 0.075f, 0.12f}, col * 1.1f * core, T);
            for (int k = 0; k < 3; k++) for (float sd : {-1.0f, 1.0f}) {
                Vec3 d = k == 0 ? Vec3(sd, 0, 0) : k == 1 ? Vec3(0, sd, 0) : Vec3(0, 0, sd);
                e(d * 0.15f, Vec3(0.022f, 0.022f, 0.022f) + Vec3(std::fabs(d.x), std::fabs(d.y), std::fabs(d.z)) * 0.012f, metalC * 0.7f, N);
            }
            e({0.07f, 0.07f, 0.07f}, {0.02f, 0.02f, 0.02f}, glow * (0.6f + core), TINT_GLOW);
            break;
        case WPN_ROCKET: // a rocket: pointed nose, team band, body with panel lines, a glowing seeker eye, four fins, a nozzle
                         // and a flickering flame with a hot core
            e({0, 0, 0.05f}, {0.065f, 0.065f, 0.2f}, metalC * 1.6f, N);
            e({0, 0, -0.19f}, {0.05f, 0.05f, 0.05f}, col * 1.2f, T);
            e({0, 0, -0.26f}, {0.03f, 0.03f, 0.03f}, col * 1.4f, T);
            e({0, 0, -0.295f}, {0.012f, 0.012f, 0.008f}, glow * 1.5f, TINT_GLOW);
            e({0, 0, -0.08f}, {0.07f, 0.07f, 0.025f}, col, T);
            e({0, 0, 0.1f}, {0.067f, 0.067f, 0.006f}, metalC, N);
            e({0, 0, 0.24f}, {0.14f, 0.01f, 0.05f}, metalC, N);
            e({0, 0, 0.24f}, {0.01f, 0.14f, 0.05f}, metalC, N);
            e({0, 0, 0.27f}, {0.045f, 0.045f, 0.025f}, metalC * 0.6f, N);
            for (const ModelPart& q : rocketFlame(fl)) out.push_back(q);
            break;
        case WPN_SYRINGE: // thrown syringe: glass barrel, glowing serum, flange, plunger, hub and needle first
            e({0, 0, 0}, {0.03f, 0.03f, 0.12f}, Vec3(0.8f, 0.85f, 0.9f), N);
            e({0, 0, -0.01f}, {0.022f, 0.022f, 0.09f}, glow * 1.5f, TINT_GLOW);
            e({0, 0, 0.12f}, {0.06f, 0.012f, 0.008f}, col, T);
            e({0, 0, 0.16f}, {0.008f, 0.008f, 0.04f}, metalC * 2.0f, N);
            e({0, 0, 0.2f}, {0.026f, 0.026f, 0.006f}, metalC, N);
            e({0, 0, -0.13f}, {0.012f, 0.012f, 0.012f}, metalC, N);
            e({0, 0, -0.19f}, {0.005f, 0.005f, 0.06f}, metalC * 2.0f, N);
            break;
        case WPN_KNIFE: // thrown knife (the game spins it end over end): blade with a spine and point, guard, grip, pommel
            e({0, 0, -0.06f}, {0.008f, 0.03f, 0.12f}, {0.75f, 0.77f, 0.8f}, N);
            e({0, 0.022f, -0.06f}, {0.01f, 0.008f, 0.12f}, {0.5f, 0.52f, 0.55f}, N);
            e({0, 0, -0.2f}, {0.006f, 0.018f, 0.02f}, {0.8f, 0.82f, 0.85f}, N);
            e({0, 0, 0.065f}, {0.012f, 0.05f, 0.008f}, metalC * 1.5f, N);
            e({0, 0, 0.12f}, {0.014f, 0.018f, 0.05f}, metalC, N);
            e({0, 0, 0.175f}, {0.018f, 0.02f, 0.008f}, col, T);
            break;
        case WPN_BUBBLE: { // a wobbling bubble: a see-through shell, a bright core and two highlights
            float r = 0.17f + wob;
            e({0, 0, 0}, {r, r - wob, r}, col, T, false, false, 0.35f);
            e({0, 0, 0}, {r * 0.55f, (r - wob) * 0.55f, r * 0.55f}, col * 1.4f, T, false, false, 0.25f);
            e({0.05f, 0.06f, 0}, {0.035f, 0.035f, 0.035f}, Vec3(1, 1, 1) * 1.5f, N);
            e({-0.07f, -0.04f, 0.04f}, {0.015f, 0.015f, 0.015f}, Vec3(1, 1, 1) * 1.2f, N);
        } break;
        case WPN_CROSSBOW: // a bolt: dark shaft with a nock, a steel broadhead with a tip, team-coloured fletching
            e({0, 0, 0}, {0.018f, 0.018f, 0.33f}, {0.12f, 0.09f, 0.06f}, N);
            e({0, 0, -0.35f}, {0.04f, 0.012f, 0.04f}, Vec3(0.8f, 0.82f, 0.86f), N);
            e({0, 0, -0.4f}, {0.012f, 0.008f, 0.02f}, Vec3(0.9f, 0.92f, 0.95f), N);
            e({0, 0, -0.31f}, {0.024f, 0.024f, 0.012f}, metalC, N);
            e({0, 0, 0.26f}, {0.06f, 0.006f, 0.06f}, col * 1.2f, T);
            e({0, 0, 0.26f}, {0.006f, 0.06f, 0.06f}, col * 1.2f, T);
            e({0, 0, 0.34f}, {0.024f, 0.024f, 0.01f}, col * 0.6f, T);
            break;
        default: // an orb: a bright core in a team shell with a ring around it
            e({0, 0, 0}, {0.1f, 0.1f, 0.1f}, col * 1.3f, T);
            e({0, 0, 0}, {0.06f, 0.06f, 0.06f}, col * 2.0f + Vec3(0.3f, 0.3f, 0.3f), T);
            e({0, 0, 0}, {0.13f, 0.02f, 0.13f}, col * 0.9f, T);
            break;
    }
}


void crownParts(const ModelPalette& pal, ModelParts& out) {
    out.clear();
    Emit e{out};
    const Vec3 gold = pal.c[TINT_TEAM];
    e({0, 0.06f, 0}, {0.17f, 0.06f, 0.17f}, gold, TINT_TEAM, false);         // band
    const Vec3 fwd = {0, 0, -1}, rgt = {1, 0, 0};
    for (int k = 0; k < 4; k++) {                                              // four points
        Vec3 d = (k & 1 ? rgt : fwd) * ((k & 2) ? -0.14f : 0.14f);
        e(d + Vec3(0, 0.17f, 0), {0.035f, 0.06f, 0.035f}, gold, TINT_TEAM, false);
    }
}

// (the same boxes drawWorld / drawHeldWeapon drew before they moved here: see git history of src/client/game.cpp)
void propParts(int prop, const ModelPalette& pal, ModelParts& out) {
    out.clear();
    Emit e{out};
    const Vec3 arm = {0.12f, 0.12f, 0.14f};
    const uint8_t N = TINT_NONE;
    switch (prop) {
        case PROP_GUN_ARM:   // everyone else's arm and gun: one dark block from the shoulder, along the aim
            e({0, 0, -0.35f}, {0.06f, 0.07f, 0.35f}, arm, N);
            break;
        case PROP_ARM:       // the arm a weapon model is held out on (hide and seek / tag: "it"; the hand at z -0.5)
            e({0, 0, -0.22f}, {0.055f, 0.06f, 0.22f}, arm, N);
            break;
        case PROP_BOMB_PACK: // the bomb on its carrier's back
            e({0, 1.15f, 0.32f}, {0.2f, 0.22f, 0.1f}, {0.1f, 0.1f, 0.1f}, N);
            break;
        case PROP_BOMB:      // the bomb on the ground, dropped or planted
            e({0, 0.15f, 0}, {0.25f, 0.15f, 0.18f}, pal.c[TINT_GLOW], TINT_GLOW);
            break;
        case PROP_CART: {    // an armoured ore cart: metal chassis on four wheels, team-coloured band, accent rim, a
                             // glowing core in a cradle on top, lamps front and back
            const Vec3 metal = pal.c[TINT_METAL], team = pal.c[TINT_TEAM], glow = pal.c[TINT_GLOW];
            const uint8_t M = TINT_METAL, G = TINT_GLOW;
            for (int k = 0; k < 4; k++) e({k & 1 ? 0.85f : -0.85f, 0.32f, k & 2 ? 0.75f : -0.75f}, {0.12f, 0.3f, 0.3f}, metal * 0.45f, M);
            e({0, 0.75f, 0}, {0.8f, 0.4f, 1.25f}, metal, M);                        // chassis
            e({0, 0.8f, 0}, {0.83f, 0.12f, 1.28f}, team * 0.9f, TINT_TEAM);         // team band
            e({0, 1.2f, 0}, {0.85f, 0.06f, 1.3f}, pal.c[TINT_ACCENT], TINT_ACCENT); // rim
            e({0, 1.45f, 0}, {0.45f, 0.2f, 0.6f}, metal * 0.7f, M);                  // cradle
            e({0, 1.75f, 0}, {0.3f, 0.22f, 0.3f}, glow * 1.3f + team * 0.3f, G);     // core
            e({0, 0.75f, 1.3f}, {0.5f, 0.15f, 0.06f}, glow, G);                      // lamps front
            e({0, 0.75f, -1.3f}, {0.5f, 0.15f, 0.06f}, glow, G);                     // and back
        } break;
        case PROP_GATE:      // a checkpoint on the payload track: a post each side
            e({1.8f, 1.4f, 0}, {0.12f, 1.4f, 0.12f}, pal.c[TINT_TEAM], TINT_TEAM);
            e({-1.8f, 1.4f, 0}, {0.12f, 1.4f, 0.12f}, pal.c[TINT_TEAM], TINT_TEAM);
            break;
        case PROP_HOOK_ARM: { // the left arm with the grapple launcher strapped on the forearm: the arm, a gunmetal housing
                              // with a team stripe on top and the cable drum on the outside (-X), the barrel on top at the
                              // front (above the housing: seen from behind in first person), its mouth at kHookMuzzle. The
                              // claw (PROP_HOOK) is drawn on its own: in the barrel, or out on the rope.
            const Vec3 metal = pal.c[TINT_METAL];
            const uint8_t M = TINT_METAL;
            e({0, -0.01f, -0.27f}, {0.055f, 0.06f, 0.27f}, arm, N);                             // forearm
            e({0, 0.015f, -0.42f}, {0.07f, 0.055f, 0.12f}, metal * 0.3f, M);                    // housing
            e({0, 0.074f, -0.38f}, {0.025f, 0.004f, 0.07f}, pal.c[TINT_TEAM], TINT_TEAM, false); // team stripe
            e({-0.082f, 0.015f, -0.38f}, {0.012f, 0.045f, 0.045f}, metal * 0.75f, M, false);     // cable drum
            e({0, kHookMuzzle.y, kHookMuzzle.z + 0.07f}, {0.042f, 0.042f, 0.07f}, metal * 0.55f, M); // barrel
        } break;
        case PROP_HOOK: {     // the grapple's claw: a shank (the rope ties on at the origin), a head, three prongs
            const Vec3 steel = pal.c[TINT_METAL] * 1.6f;
            const uint8_t M = TINT_METAL;
            e({0, 0, -0.03f}, {0.014f, 0.014f, 0.035f}, steel * 0.7f, M, false);
            e({0, 0, -0.075f}, {0.036f, 0.036f, 0.016f}, steel, M, false);
            for (int k = 0; k < 3; k++) {   // at 90, 210 and 330 degrees round the shank, splayed out
                const float a = 1.5707963f + k * 2.0943951f;
                e({std::cos(a) * 0.042f, std::sin(a) * 0.042f, -0.115f}, {0.012f, 0.012f, 0.035f}, steel, M, false);
            }
        } break;
    }
}

// ---- box model files ----------------------------------------------------------------------------------------------
unsigned drivenSlots(int kind, int idx) {
    const unsigned T = 1u << TINT_TEAM, G = 1u << TINT_GLOW, M = 1u << TINT_METAL, B = 1u << TINT_BASE, A = 1u << TINT_ACCENT;
    switch (kind) {
        case MODEL_PLAYER: case MODEL_CROWN: return T;
        case MODEL_PROJECTILE: return T | G;
        case MODEL_WEAPON: return T | G | M | B;
        case MODEL_PROP: return idx == PROP_BOMB ? G : idx == PROP_CART ? T | M | A | G : idx == PROP_GATE || idx == PROP_HOOK_ARM ? T : 0;
    }
    return 0;
}

ModelPalette drivenPalette(int kind, int idx, const ModelPalette& pal) {
    ModelPalette p = referencePalette(kind, idx);
    const unsigned d = drivenSlots(kind, idx);
    for (int s = 1; s < TINT_COUNT; s++) if (d >> s & 1) p.c[s] = pal.c[s];
    return p;
}

static void buildParts(int kind, int i, const ModelPalette& pal, float hs, ModelParts& out) {
    if (kind == MODEL_PLAYER) playerModelParts(i, hs, pal, out);
    else if (kind == MODEL_WEAPON) viewmodelParts(i, pal, out);
    else if (kind == MODEL_PROJECTILE) projectileParts(i, pal, 1.0f, 1.0f, 0.0f, out);
    else if (kind == MODEL_PROP) propParts(i, pal, out);
    else crownParts(pal, out);
}

void exportModelParts(int kind, int idx, ModelParts& out) {
    const ModelPalette& ref = referencePalette(kind, idx);
    buildParts(kind, idx, ref, 1.0f, out);
    // each part's colour is affine in its slot's colour and the team colour (the builders' only arithmetic): built with
    // both black it is `add`; that slot white instead gives `mul` (minus add), the team white `teamMul` (other slots
    // stay at the reference)
    ModelParts lo, hi, tm;
    for (int s = 1; s < TINT_COUNT; s++) {
        ModelPalette p0 = ref, p1 = ref, pt = ref;
        p0.c[s] = p0.c[TINT_TEAM] = {0, 0, 0};
        p1.c[TINT_TEAM] = {0, 0, 0};
        p1.c[s] = {1, 1, 1};
        pt.c[s] = {0, 0, 0};
        pt.c[TINT_TEAM] = {1, 1, 1};
        buildParts(kind, idx, p0, 1.0f, lo);
        buildParts(kind, idx, p1, 1.0f, hi);
        buildParts(kind, idx, pt, 1.0f, tm);
        for (size_t k = 0; k < out.size() && k < lo.size() && k < hi.size() && k < tm.size(); k++)
            if (out[k].slot == s) {
                out[k].add = lo[k].color;
                out[k].mul = hi[k].color - lo[k].color;
                out[k].teamMul = s == TINT_TEAM ? Vec3(0, 0, 0) : tm[k].color - lo[k].color;
            }
    }
    if (kind == MODEL_PLAYER) {   // which half heights a crouch squashes
        buildParts(kind, idx, ref, 0.5f, lo);
        for (size_t k = 0; k < out.size() && k < lo.size(); k++) out[k].crouch = lo[k].h.y != out[k].h.y;
    }
}

void finishBoxModel(int kind, int idx, ModelParts& boxes) {
    const ModelPalette& ref = referencePalette(kind, idx);
    auto ratio = [](float a, float b) { return b > 1e-3f ? a / b : a; };
    for (ModelPart& q : boxes)
        if (q.slot != TINT_NONE && q.slot < TINT_COUNT && q.mul.x < 0) {
            const Vec3& r = ref.c[q.slot];
            q.mul = {ratio(q.color.x, r.x), ratio(q.color.y, r.y), ratio(q.color.z, r.z)};
            q.add = q.teamMul = {0, 0, 0};
        }
}

void overrideBoxParts(int kind, int idx, const ModelParts& boxes, const ModelPalette& pal, float hs, ModelParts& out) {
    out.clear();
    const ModelPalette p = drivenPalette(kind, idx, pal);
    for (const ModelPart& b : boxes) {
        out.push_back(b);
        ModelPart& q = out.back();
        if (q.slot != TINT_NONE && q.slot < TINT_COUNT) {
            const Vec3 &s = p.c[q.slot], &t = p.c[TINT_TEAM];
            q.color = Vec3(s.x * q.mul.x + t.x * q.teamMul.x + q.add.x, s.y * q.mul.y + t.y * q.teamMul.y + q.add.y, s.z * q.mul.z + t.z * q.teamMul.z + q.add.z);
        }
        if (hs != 1.0f) {
            q.c.y = q.c.y * hs;
            if (q.crouch) q.h.y = q.h.y * hs;
        }
    }
}

bool MeshData::bounds(Vec3& mn, Vec3& mx) const {
    mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (auto& p : prims)
        for (size_t i = 0; i + 2 < p.pos.size(); i += 3) {
            Vec3 v(p.pos[i], p.pos[i + 1], p.pos[i + 2]);
            mn = vmin(mn, v); mx = vmax(mx, v); any = true;
        }
    return any;
}

size_t MeshData::triangleCount() const { size_t n = 0; for (auto& p : prims) n += p.idx.size() / 3; return n; }

void partsToMesh(const ModelParts& parts, MeshData& out) {
    out.prims.clear();
    // the faces in the box renderer's order and shading (Renderer::emitBox)
    struct Face { Vec3 n; float shade; Vec3 c[4]; };
    static const Face faces[6] = {
        {{1, 0, 0}, 1.0f, {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}},
        {{-1, 0, 0}, 0.92f, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}},
        {{0, 1, 0}, 1.0f, {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}},
        {{0, -1, 0}, 0.8f, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}},
        {{0, 0, 1}, 0.96f, {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}},
        {{0, 0, -1}, 0.96f, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}},
    };
    for (const ModelPart& p : parts) {
        MeshPrim* prim = nullptr;
        for (auto& q : out.prims) if (q.slot == p.slot && q.alpha == p.alpha) prim = &q;
        if (!prim) {
            out.prims.push_back({});
            prim = &out.prims.back();
            prim->slot = p.slot; prim->alpha = p.alpha;
            prim->material = std::string(tintSlotName(p.slot)) + (p.alpha < 1 ? "_translucent" : "");
        }
        for (const Face& f : faces) {
            uint32_t base = (uint32_t)(prim->pos.size() / 3);
            for (const Vec3& k : f.c) {
                Vec3 v = p.c + Vec3(k.x * p.h.x, k.y * p.h.y, k.z * p.h.z);
                prim->pos.insert(prim->pos.end(), {v.x, v.y, v.z});
                prim->nrm.insert(prim->nrm.end(), {f.n.x, f.n.y, f.n.z});
                prim->col.insert(prim->col.end(), {p.color.x * f.shade, p.color.y * f.shade, p.color.z * f.shade});
            }
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) prim->idx.push_back(base + i);
        }
    }
}

// the triangles of the prims in anim node `anim` (-1: the body), less `off`, into out (bounds from them too)
static bool isInk(const std::string& material) {
    return material.size() >= 3 && std::tolower((unsigned char)material[0]) == 'i' && std::tolower((unsigned char)material[1]) == 'n' &&
           std::tolower((unsigned char)material[2]) == 'k';
}

// Each hull vertex's growth direction (ModelMesh::inkDir): the faces that meet at its position (the hull is drawn
// inside out, so its winding normals point in; a mesh whose hull points the other way is told by which side of the
// hull's middle its faces face), the mean of their distinct normals, mitred so each of them moves 1 m.
static void inkDirections(ModelMesh& m) {
    m.inkDir.clear();
    if (m.inkIdx.empty()) return;
    std::vector<uint32_t> keep;
    for (uint32_t i : m.inkIdx) if (i % 3 == 0 && i + 2 < m.opaque.size()) { keep.push_back(i); keep.push_back(i + 1); keep.push_back(i + 2); }
    m.inkIdx.swap(keep);
    auto P = [&](uint32_t i) { return Vec3(m.opaque[i].pos[0], m.opaque[i].pos[1], m.opaque[i].pos[2]); };
    Vec3 mid{0, 0, 0};
    for (uint32_t i : m.inkIdx) mid = mid + P(i);
    mid = mid * (1.0f / (float)std::max<size_t>(1, m.inkIdx.size()));
    std::vector<Vec3> fn(m.inkIdx.size() / 3);
    float side = 0;
    for (size_t t = 0; t < fn.size(); t++) {
        const Vec3 a = P(m.inkIdx[t * 3]), b = P(m.inkIdx[t * 3 + 1]), c = P(m.inkIdx[t * 3 + 2]);
        const Vec3 n = cross(b - a, c - a);
        const float l = length(n);
        fn[t] = l > 1e-12f ? n * (1.0f / l) : Vec3(0, 0, 0);
        side += dot(fn[t], (a + b + c) * (1.0f / 3.0f) - mid) * l;
    }
    const float out = side > 0 ? 1.0f : -1.0f;   // (inside out, as the low-poly builder writes it: -1)
    // the faces at each position (exact: the hull's corners are shared bit for bit)
    std::unordered_map<uint64_t, std::vector<uint32_t>> at;
    auto key = [&](uint32_t i) {
        const float* p = m.opaque[i].pos;
        uint64_t h = 1469598103934665603ull;
        for (int k = 0; k < 3; k++) { uint32_t u; memcpy(&u, &p[k], 4); h = (h ^ u) * 1099511628211ull; }
        return h;
    };
    for (size_t t = 0; t < fn.size(); t++)
        for (int k = 0; k < 3; k++) at[key(m.inkIdx[t * 3 + k])].push_back((uint32_t)t);
    m.inkDir.resize(m.inkIdx.size() * 3);
    std::unordered_map<uint64_t, Vec3> done;
    for (size_t j = 0; j < m.inkIdx.size(); j++) {
        const uint64_t h = key(m.inkIdx[j]);
        auto it = done.find(h);
        if (it == done.end()) {
            Vec3 ns[16]; int nn = 0;
            for (uint32_t t : at[h]) {
                const Vec3 n = fn[t] * out;
                if (dot(n, n) < 0.5f) continue;
                bool dup = false;
                for (int q = 0; q < nn && !dup; q++) dup = dot(ns[q], n) > 0.999f;
                if (!dup && nn < 16) ns[nn++] = n;
            }
            Vec3 d{0, 0, 0};
            for (int q = 0; q < nn; q++) d = d + ns[q];
            const float l = length(d);
            if (l > 1e-6f) {
                d = d * (1.0f / l);
                float lo = 1.0f;
                for (int q = 0; q < nn; q++) lo = std::min(lo, dot(d, ns[q]));
                d = d * (1.0f / std::max(lo, 0.25f));   // (mitred; a needle-sharp corner at most 4x)
            }
            it = done.emplace(h, d).first;
        }
        m.inkDir[j * 3] = it->second.x; m.inkDir[j * 3 + 1] = it->second.y; m.inkDir[j * 3 + 2] = it->second.z;
    }
}

static void meshTriangles(const MeshData& mesh, int anim, const Vec3& off, ModelMesh& out) {
    out.opaque.clear(); out.translucent.clear(); out.inkIdx.clear(); out.inkDir.clear();
    Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    bool any = false;
    for (const MeshPrim& p : mesh.prims) {
        if (p.anim != anim) continue;
        auto& dst = p.alpha < 0.999f ? out.translucent : out.opaque;
        const size_t first = dst.size();
        size_t nv = p.pos.size() / 3;
        for (size_t i = 0; i < nv; i++) { Vec3 v(p.pos[i * 3], p.pos[i * 3 + 1], p.pos[i * 3 + 2]); v = v - off; mn = vmin(mn, v); mx = vmax(mx, v); any = true; }
        for (uint32_t i : p.idx) {
            if (i >= nv) continue;
            ModelMesh::V v;
            for (int k = 0; k < 3; k++) {
                v.pos[k] = p.pos[i * 3 + k] - (k == 0 ? off.x : k == 1 ? off.y : off.z);
                v.nrm[k] = i * 3 + k < p.nrm.size() ? p.nrm[i * 3 + k] : (k == 1 ? 1.0f : 0.0f);
                v.col[k] = i * 3 + k < p.col.size() ? p.col[i * 3 + k] : 1.0f;
            }
            v.col[3] = p.alpha;
            v.slot = p.slot;
            dst.push_back(v);
        }
        while (dst.size() % 3) dst.pop_back();   // (whole triangles only)
        if (&dst == &out.opaque && isInk(p.material))
            for (size_t k = first; k < dst.size(); k++) out.inkIdx.push_back((uint32_t)k);
    }
    inkDirections(out);
    if (any) { out.center = (mn + mx) * 0.5f; out.radius = length(mx - mn) * 0.5f; }
    else { out.center = {}; out.radius = 0; }
}

void meshToModel(const MeshData& mesh, ModelMesh& out) {
    out.parts.clear();
    out.rig = PlayerRig{};
    meshTriangles(mesh, -1, {0, 0, 0}, out);   // (no anim nodes: every prim, exactly as before)
    Vec3 mn, mx;
    if (mesh.bounds(mn, mx)) { out.allCenter = (mn + mx) * 0.5f; out.allRadius = length(mx - mn) * 0.5f; }
    else { out.allCenter = {}; out.allRadius = 0; }
    for (size_t a = 0; a < mesh.anims.size(); a++) {
        ModelMesh part;
        meshTriangles(mesh, (int)a, mesh.anims[a].pivot, part);
        if (part.opaque.empty() && part.translucent.empty()) continue;   // (an anim node with no triangles)
        part.name = mesh.anims[a].name;
        part.pivot = mesh.anims[a].pivot;
        out.parts.push_back(std::move(part));
    }
}

void playerMeshRig(ModelMesh& m) {
    m.rig = PlayerRig{};
    Vec3 piv[BONE_COUNT];
    unsigned has = 0;
    for (ModelMesh& p : m.parts) {
        const int b = boneFromName(p.name);
        p.tag = (uint8_t)(b > 0 && b < BONE_COUNT ? b : BONE_ROOT);
        if (p.tag != BONE_ROOT && !(has >> p.tag & 1u)) { piv[p.tag] = p.pivot; has |= 1u << p.tag; }
    }
    auto got = [&](int b) { return (has >> b & 1u) != 0; };
    if (!got(BONE_THIGH_L) || !got(BONE_THIGH_R) || !got(BONE_SHIN_L) || !got(BONE_SHIN_R)) return;
    PlayerRig& r = m.rig;
    const Vec3 &tl = piv[BONE_THIGH_L], &tr = piv[BONE_THIGH_R], &sl = piv[BONE_SHIN_L], &sr = piv[BONE_SHIN_R];
    r.hipY = (tl.y + tr.y) * 0.5f;
    r.kneeY = (sl.y + sr.y) * 0.5f;
    r.hipX = (std::fabs(tl.x) + std::fabs(tr.x)) * 0.5f;
    if (!(r.hipY > r.kneeY) || !(r.kneeY > 0) || !std::isfinite(r.hipY + r.kneeY + r.hipX)) { r = PlayerRig{}; return; }
    r.waistY = got(BONE_TORSO) ? piv[BONE_TORSO].y : r.hipY;
    if (got(BONE_HEAD)) r.neckY = piv[BONE_HEAD].y;
    float ankle = 0;
    if (got(BONE_FOOT_L)) ankle = std::max(ankle, piv[BONE_FOOT_L].y);
    if (got(BONE_FOOT_R)) ankle = std::max(ankle, piv[BONE_FOOT_R].y);
    r.ankleY = std::min(std::max(ankle, 0.0f), r.kneeY);
    r.footHalf = 0;
    r.animated = true;
}

// ---- first-person weapon part motions ------------------------------------------------------------------------------

static const char* const kWeaponPartNames[WPART_COUNT] = {"", "bolt", "mag", "round", "spin", "pump", "drum", "string", "lever", "plunger", "sweep", "antenna"};
const char* weaponPartName(int part) { return part > 0 && part < WPART_COUNT ? kWeaponPartNames[part] : ""; }
int weaponPartFromName(const std::string& name) {
    for (int k = 1; k < WPART_COUNT; k++) if (name == kWeaponPartNames[k]) return k;
    return WPART_NONE;
}

namespace {
float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float smooth(float x) { x = clamp01(x); return x * x * (3 - 2 * x); }
// 0 -> 1 -> 0 across [a, b] (as a fraction u): out over the first `split` of it, back over the rest, eased
float outBack(float u, float a, float b, float split) {
    if (u <= a || u >= b) return 0;
    float t = (u - a) / (b - a);
    return t < split ? smooth(t / split) : 1 - smooth((t - split) / (1 - split));
}
Mat4 slideZ(float z) { return Mat4::translate({0, 0, z}); }
Mat4 rotZ(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
constexpr float kDeg = 3.14159265f / 180.0f;
}

// Every weapon's parts (the table in docs/ASSETS.md "Animated parts"): u is the time since the shot over its refire.
// The tuning is here: distances in metres (model space, before the HUD's viewmodel size), angles in degrees.
bool weaponPartMotion(int weapon, int part, const WeaponAnimState& s, Mat4& out) {
    out = Mat4();
    const float t = s.since, r = s.refire > 1e-4f ? s.refire : 1e-4f, u = t / r;
    switch (weapon) {
        case WPN_RIFLE:
            if (part == WPART_BOLT) out = slideZ(0.025f * outBack(t, 0, std::min(0.055f, r * 0.9f), 0.35f));   // the charging handle, each shot
            return true;
        case WPN_ROCKET:
            if (part == WPART_ROUND) {   // fired: gone, then the next one slides up the tube from behind
                if (u < 0.3f) return false;
                out = slideZ(0.15f * (1 - smooth((u - 0.3f) / 0.6f)));
            }
            return true;
        case WPN_SNIPER:
            if (part == WPART_BOLT) {   // up, back, forward, down: across the refire, done before the next shot (the
                                        // handle on the left wall, its pivot at the stem's root: up is -Z rotation)
                const float a = 0.12f, b = 0.8f, q = (u - a) / (b - a);
                if (q > 0 && q < 1) {
                    float lift = q < 0.2f ? smooth(q / 0.2f) : q > 0.8f ? 1 - smooth((q - 0.8f) / 0.2f) : 1;
                    float back = q < 0.2f || q > 0.8f ? 0 : q < 0.5f ? smooth((q - 0.2f) / 0.3f) : 1 - smooth((q - 0.5f) / 0.3f);
                    out = mulAffine(slideZ(0.015f * back), rotZ(-70 * kDeg * lift));   // (1.5 cm: the slot's length)
                }
            }
            return true;
        case WPN_LASER:
            if (part == WPART_SPIN) out = rotZ(s.spin);
            return true;
        case WPN_SHOTGUN:
            if (part == WPART_PUMP) out = slideZ(0.06f * outBack(u, 0.1f, 0.7f, 0.5f));   // racks after each shot
            return true;
        case WPN_STICKY:
            if (part == WPART_DRUM && s.shots) {   // one chamber (60 degrees) a shot, turned just after it; the angle
                                                   // adds up (shots x 60, mod 360), never turning back
                float step = smooth((t - 0.04f) / 0.16f);
                out = rotZ(60 * kDeg * ((float)((s.shots - 1) % 6) + step));
            }
            return true;
        case WPN_CROSSBOW:
            if (part == WPART_STRING) {   // snaps forward on the shot, drawn back over the reload
                float fwd = t < 0.03f ? smooth(t / 0.03f) : u < 0.2f ? 1 : 1 - smooth((u - 0.2f) / 0.4f);
                if (u < 1) out = slideZ(-0.01f * fwd);   // (1 cm: further, its ends pass the cams)
            } else if (part == WPART_BOLT) {   // the loaded bolt: gone, then slides in once the string is back
                if (u < 0.6f) return false;
                out = slideZ(0.15f * (1 - smooth((u - 0.6f) / 0.3f)));
            }
            return true;
        case WPN_BUBBLE:
            if (part == WPART_LEVER) out = Mat4::rotX(-25 * kDeg * outBack(t, 0, std::min(0.16f, r), 0.3f));
            return true;
        case WPN_SYRINGE:
            if (part == WPART_PLUNGER) {   // pushed in on the shot, pulled back over the cooldown
                float in = t < 0.06f ? smooth(t / 0.06f) : 1 - smooth((u - 0.06f / r) / 0.8f);
                if (u < 1) out = slideZ(-0.03f * in);
            }
            return true;
        case WPN_LOCATOR:
            if (part == WPART_SWEEP) out = Mat4::rotY(-s.clock * 3.14159265f);   // one turn every 2 s (clockwise from above)
            else if (part == WPART_ANTENNA) out = Mat4::rotX(s.wobble);
            return true;
        default: return true;
    }
}
