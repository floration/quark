#include "models.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

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
void playerModelParts(int model, float hs, const ModelPalette& pal, ModelParts& out) {
    out.clear();
    Emit e{out};
    const Vec3 col = pal.c[TINT_TEAM];
    const Vec3 grey = col * 0.6f + Vec3(0.06f, 0.06f, 0.07f), dark = {0.05f, 0.05f, 0.06f};
    const uint8_t T = TINT_TEAM, N = TINT_NONE;
    // b(x, y, z, hx, hy, hz): a body box (y and hy follow the crouch); h(...): a head box (its centre follows, not its size)
    auto b = [&](float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        e(Vec3(x, y * hs, z), Vec3(hx, hy * hs, hz), c, s, sh, vis); };
    auto h = [&](float x, float y, float z, float hx, float hy, float hz, const Vec3& c, uint8_t s, bool sh = true, bool vis = false) {
        e(Vec3(x, y * hs, z), Vec3(hx, hy, hz), c, s, sh, vis); };
    const Vec3 skin = {0.72f, 0.52f, 0.4f}, rot = {0.36f, 0.5f, 0.3f}, eyeW = {1.6f, 1.6f, 1.5f};
    switch (model) {
        case 1: // HEAVY: wide armoured legs and boots, a big chest plate with a belt, shoulder pads, a small helmet
            for (float sd : {-1.0f, 1.0f}) {
                b(sd * 0.15f, 0.42f, 0, 0.14f, 0.42f, 0.2f, col * 0.45f, T);
                b(sd * 0.15f, 0.08f, -0.03f, 0.16f, 0.08f, 0.24f, dark, N);
                b(sd * 0.15f, 0.5f, -0.2f, 0.11f, 0.08f, 0.02f, grey, T, false, true);
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
        case 2: // SIMPLE: just the hitbox: the body box and the head box, nothing else
            b(0, 0.72f, 0, kPlayerHalfWidth, 0.72f, kPlayerHalfWidth, col, T);
            h(0, 1.62f, 0, kPlayerHalfWidth * kHeadWidthFraction, 0.18f, kPlayerHalfWidth * kHeadWidthFraction, col * 1.25f, T);
            break;
        case 3: // ROBOT: grey frame on piston legs, team chest with a glowing core, cube head, eye bar, antenna
            for (float sd : {-1.0f, 1.0f}) {
                b(sd * 0.14f, 0.5f, 0, 0.1f, 0.3f, 0.12f, grey * 0.8f, T);
                b(sd * 0.14f, 0.12f, -0.03f, 0.13f, 0.12f, 0.19f, grey * 0.6f, T);
                b(sd * 0.14f, 0.5f, -0.12f, 0.04f, 0.2f, 0.01f, dark, N, false, true);
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
                b(sd * 0.14f, 0.44f, 0, 0.12f, 0.44f, 0.16f, grey * 0.85f, T);
                b(sd * 0.14f, 0.06f, -0.04f, 0.13f, 0.06f, 0.2f, grey * 0.6f, T);
                b(sd * 0.14f, 0.52f, -0.16f, 0.09f, 0.06f, 0.02f, grey * 1.1f, T, false, true);
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
        case 5: // WIZARD: a long team robe flaring at the hem, a sash, a beard, a tall pointed hat with a band and a star
            b(0, 0.18f, 0, 0.32f, 0.18f, 0.28f, col * 0.55f, T);
            b(0, 0.6f, 0, 0.27f, 0.26f, 0.23f, col * 0.65f, T);
            b(0, 1.1f, 0, 0.3f, 0.26f, 0.22f, col * 0.8f, T);
            b(0, 0.6f, -0.235f, 0.04f, 0.42f, 0.01f, col * 1.4f + Vec3(0.2f, 0.2f, 0.2f), T, false, true);
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
            b(-0.14f, 0.42f, 0, 0.12f, 0.42f, 0.15f, col * 0.4f, T);
            b(0.14f, 0.4f, 0.03f, 0.12f, 0.4f, 0.15f, col * 0.35f, T);
            b(0.14f, 0.2f, -0.13f, 0.08f, 0.1f, 0.02f, rot, N, false, true);
            for (float sd : {-1.0f, 1.0f}) b(sd * 0.14f, 0.05f, -0.03f, 0.12f, 0.05f, 0.18f, Vec3(0.18f, 0.14f, 0.1f), N);
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
            for (float sd : {-1.0f, 1.0f}) {
                b(sd * 0.14f, 0.3f, 0, 0.1f, 0.3f, 0.12f, skin, N);
                b(sd * 0.14f, 0.04f, -0.03f, 0.11f, 0.04f, 0.16f, Vec3(0.9f, 0.9f, 0.9f), N);
                b(sd * 0.14f, 0.12f, 0, 0.11f, 0.06f, 0.13f, Vec3(0.85f, 0.85f, 0.82f), N);
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
                b(sd * 0.14f, 0.44f, 0, 0.12f, 0.44f, 0.15f, Vec3(0.85f, 0.85f, 0.8f), N);
                b(sd * 0.14f, 0.2f, -0.01f, 0.13f, 0.2f, 0.17f, Vec3(0.12f, 0.08f, 0.05f), N);
                b(sd * 0.14f, 0.55f, -0.155f, 0.12f, 0.03f, 0.01f, col * 0.6f, T, false, true);
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
                b(sd * 0.13f, 0.44f, 0, 0.11f, 0.44f, 0.14f, dark * 2.0f, N);
                b(sd * 0.13f, 0.05f, -0.03f, 0.115f, 0.05f, 0.17f, dark, N);
                b(sd * 0.13f, 0.3f, 0, 0.115f, 0.03f, 0.145f, dark * 3.0f, N);
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
                b(sd * 0.14f, 0.44f, 0, 0.12f, 0.44f, 0.15f, Vec3(0.3f, 0.22f, 0.15f), N);
                b(sd * 0.14f, 0.12f, -0.02f, 0.13f, 0.12f, 0.17f, Vec3(0.18f, 0.12f, 0.07f), N);
                b(sd * 0.14f, 0.5f, -0.155f, 0.1f, 0.3f, 0.01f, Vec3(0.4f, 0.3f, 0.2f), N, false, true);
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
                b(sd * 0.14f, 0.44f, 0, 0.12f, 0.44f, 0.16f, col * 0.55f, T);
                b(sd * 0.14f, 0.06f, -0.03f, 0.13f, 0.06f, 0.19f, dark, N);
                b(sd * 0.14f, 0.5f, -0.16f, 0.09f, 0.07f, 0.02f, col * 0.35f, T, false, true);
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
            e({0, 0, 0.36f}, {0.05f * fl, 0.05f * fl, 0.08f * fl}, Vec3(2.0f, 1.3f, 0.5f), N);
            e({0, 0, 0.33f}, {0.025f * fl, 0.025f * fl, 0.05f * fl}, Vec3(2.4f, 2.2f, 1.6f), N);
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

void meshToModel(const MeshData& mesh, ModelMesh& out) {
    out.opaque.clear(); out.translucent.clear();
    Vec3 mn, mx;
    if (!mesh.bounds(mn, mx)) { out.center = {}; out.radius = 0; return; }
    out.center = (mn + mx) * 0.5f;
    out.radius = length(mx - mn) * 0.5f;
    for (const MeshPrim& p : mesh.prims) {
        auto& dst = p.alpha < 0.999f ? out.translucent : out.opaque;
        size_t nv = p.pos.size() / 3;
        for (uint32_t i : p.idx) {
            if (i >= nv) continue;
            ModelMesh::V v;
            for (int k = 0; k < 3; k++) {
                v.pos[k] = p.pos[i * 3 + k];
                v.nrm[k] = i * 3 + k < p.nrm.size() ? p.nrm[i * 3 + k] : (k == 1 ? 1.0f : 0.0f);
                v.col[k] = i * 3 + k < p.col.size() ? p.col[i * 3 + k] : 1.0f;
            }
            v.col[3] = p.alpha;
            v.slot = p.slot;
            dst.push_back(v);
        }
        while (dst.size() % 3) dst.pop_back();   // (whole triangles only)
    }
}
