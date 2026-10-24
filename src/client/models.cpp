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
            static const char* p[PROP_COUNT] = {"GUN_ARM", "ARM", "BOMB_PACK", "BOMB", "CART", "GATE"};
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
    return p;
}
const ModelPalette& referencePalette(int kind, int idx) {
    static const ModelPalette pals[MODEL_KINDS] = {makeReference(0), makeReference(1), makeReference(2), makeReference(3), makeReference(4)};
    static const ModelPalette props[PROP_COUNT] = {makeReference(MODEL_PROP, 0), makeReference(MODEL_PROP, 1), makeReference(MODEL_PROP, 2),
                                                   makeReference(MODEL_PROP, 3), makeReference(MODEL_PROP, 4), makeReference(MODEL_PROP, 5)};
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

// (the same boxes drawWorld drew before they moved here: see git history of src/client/game.cpp)
void playerModelParts(int model, float hs, const ModelPalette& pal, ModelParts& out) {
    out.clear();
    Emit e{out};
    const Vec3 col = pal.c[TINT_TEAM];
    auto at = [&](float h) { return Vec3(0, h * hs, 0); };
    const Vec3 fwd = {0, 0, -1}, rgt = {1, 0, 0};
    const Vec3 grey = col * 0.6f + Vec3(0.06f, 0.06f, 0.07f), dark = {0.05f, 0.05f, 0.06f};
    const uint8_t T = TINT_TEAM, N = TINT_NONE;
    switch (model) {
        case 1: // HEAVY: wide armoured body, shoulder pads, small helmet
            e(at(0.45f), Vec3(0.32f, 0.45f * hs, 0.24f), col * 0.45f, T);
            e(at(1.15f), Vec3(0.44f, 0.3f * hs, 0.28f), col, T);
            for (float sd : {-1.0f, 1.0f}) e(at(1.38f) + rgt * (sd * 0.42f), Vec3(0.14f, 0.1f, 0.22f), grey, T);
            e(at(1.6f), Vec3(0.18f, 0.16f, 0.18f), col * 1.1f, T);
            e(at(1.6f) + fwd * 0.18f, Vec3(0.15f, 0.04f, 0.02f), dark, N, false, true);
            break;
        case 2: // SCOUT: slim, tall head with a long glowing visor
            e(at(0.47f), Vec3(0.22f, 0.47f * hs, 0.16f), col * 0.6f, T);
            e(at(1.17f), Vec3(0.3f, 0.26f * hs, 0.18f), col, T);
            e(at(1.63f), Vec3(0.17f, 0.2f, 0.17f), col * 1.2f, T);
            e(at(1.66f) + fwd * 0.17f, Vec3(0.17f, 0.035f, 0.02f), col * 1.8f + Vec3(0.3f, 0.3f, 0.3f), T, false, true);
            break;
        case 3: // ROBOT: grey frame, team-coloured chest, cube head, glowing eye, antenna
            e(at(0.45f), Vec3(0.26f, 0.45f * hs, 0.2f), grey * 0.8f, T);
            e(at(1.15f), Vec3(0.38f, 0.3f * hs, 0.26f), grey, T);
            e(at(1.18f) + fwd * 0.26f, Vec3(0.26f, 0.18f, 0.02f), col, T, false, true);
            e(at(1.62f), Vec3(0.23f, 0.19f, 0.23f), grey * 1.15f, T);
            e(at(1.64f) + fwd * 0.23f, Vec3(0.12f, 0.035f, 0.02f), col * 2.0f + Vec3(0.4f, 0.4f, 0.4f), T, false, true);
            e(at(1.9f), Vec3(0.02f, 0.1f, 0.02f), grey, T, false);
            e(at(2.0f), Vec3(0.045f, 0.045f, 0.045f), col * 1.8f, T, false);
            break;
        case 4: // KNIGHT: plate armour, team tabard, crested helmet with a visor slit
            e(at(0.45f), Vec3(0.27f, 0.45f * hs, 0.2f), grey * 0.85f, T);
            e(at(1.15f), Vec3(0.4f, 0.3f * hs, 0.25f), grey, T);
            e(at(1.05f) + fwd * 0.25f, Vec3(0.2f, 0.36f, 0.02f), col, T, false, true);
            e(at(1.6f), Vec3(0.21f, 0.2f, 0.21f), grey * 1.1f, T);
            e(at(1.86f), Vec3(0.03f, 0.08f, 0.19f), col * 1.1f, T, false);
            e(at(1.62f) + fwd * 0.21f, Vec3(0.15f, 0.025f, 0.02f), dark, N, false, true);
            break;
        default: // STANDARD
            e(at(0.45f), Vec3(0.28f, 0.45f * hs, 0.2f), col * 0.55f, T);
            e(at(1.15f), Vec3(0.38f, 0.28f * hs, 0.24f), col, T);
            e(at(1.6f), Vec3(0.2f, 0.18f, 0.2f), col * 1.15f, T);
            e(at(1.62f) + fwd * 0.2f, Vec3(0.16f, 0.05f, 0.02f), dark, N, false, true);
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
        case WPN_RIFLE:      // receiver, long barrel with a front sight, magazine, stock, accent rail
            part(0, 0, 0, 0.026f, 0.032f, 0.12f, dark, M);
            part(0, 0.008f, -0.2f, 0.009f, 0.009f, 0.09f, mid, M);
            part(0, 0.022f, -0.27f, 0.004f, 0.012f, 0.006f, mid, M);
            part(0, -0.06f, -0.03f, 0.016f, 0.035f, 0.022f, mid, M);
            part(0, -0.012f, 0.15f, 0.02f, 0.03f, 0.05f, dark, M);
            part(0, 0.038f, -0.01f, 0.01f, 0.005f, 0.09f, acc, A);
            break;
        case WPN_ROCKET:     // fat tube with flared muzzle and exhaust rings, side sight, grip
            part(0, 0, -0.02f, 0.045f, 0.045f, 0.2f, mid, M);
            part(0, 0, -0.23f, 0.055f, 0.055f, 0.02f, dark, M);
            part(0, 0, 0.19f, 0.052f, 0.052f, 0.015f, dark, M);
            part(0, 0, -0.14f, 0.0475f, 0.0475f, 0.005f, glow, G);
            part(0, 0, 0.05f, 0.047f, 0.047f, 0.006f, acc, A);
            part(-0.058f, 0.025f, -0.06f, 0.012f, 0.02f, 0.03f, dark, M);
            part(0, -0.07f, 0.04f, 0.014f, 0.03f, 0.02f, dark, M);
            break;
        case WPN_KNIFE:      // grip, cross guard, long bright blade, accent pommel
            part(0, -0.01f, 0.04f, 0.012f, 0.016f, 0.05f, dark, M);
            part(0, -0.01f, -0.015f, 0.035f, 0.008f, 0.008f, mid, M);
            part(0, -0.006f, -0.11f, 0.004f, 0.018f, 0.09f, light, B);
            part(0, 0.006f, -0.2f, 0.003f, 0.008f, 0.012f, light, B);
            part(0, -0.01f, 0.095f, 0.014f, 0.014f, 0.008f, acc, A);
            break;
        case WPN_SNIPER:     // long thin barrel with muzzle brake, receiver, scope on rings, bolt, stock
            part(0, 0, 0, 0.022f, 0.028f, 0.13f, dark, M);
            part(0, 0.006f, -0.27f, 0.007f, 0.007f, 0.15f, mid, M);
            part(0, 0.006f, -0.43f, 0.012f, 0.01f, 0.018f, dark, M);
            part(0, 0.058f, -0.02f, 0.017f, 0.017f, 0.09f, dark, M);
            part(0, 0.058f, -0.115f, 0.02f, 0.02f, 0.008f, acc, A);
            part(0, 0.058f, -0.124f, 0.012f, 0.012f, 0.002f, glow, G);
            part(0, 0.036f, -0.06f, 0.006f, 0.01f, 0.006f, mid, M); part(0, 0.036f, 0.03f, 0.006f, 0.01f, 0.006f, mid, M);
            part(0.03f, 0.01f, 0.05f, 0.012f, 0.006f, 0.006f, light, B);
            part(0, -0.015f, 0.17f, 0.018f, 0.035f, 0.06f, dark, M);
            break;
        case WPN_LASER:      // chunky body, coil segments on top, glowing emitter rings at the front
            part(0, 0, 0, 0.036f, 0.04f, 0.15f, mid, M);
            for (int k = 0; k < 4; k++) part(0, 0.046f, -0.09f + k * 0.05f, 0.022f, 0.008f, 0.012f, k % 2 ? dark : glow, k % 2 ? M : G);
            part(0, 0, -0.165f, 0.03f, 0.03f, 0.012f, dark, M);
            part(0, 0, -0.185f, 0.022f, 0.022f, 0.008f, glow * 1.4f, G);
            part(0, -0.07f, 0.03f, 0.014f, 0.03f, 0.02f, dark, M);
            break;
        case WPN_SHOTGUN:    // twin barrels, pump under them, heavy receiver and stock
            part(-0.012f, 0.012f, -0.17f, 0.011f, 0.011f, 0.13f, mid, M);
            part(0.012f, 0.012f, -0.17f, 0.011f, 0.011f, 0.13f, mid, M);
            part(0, -0.018f, -0.15f, 0.022f, 0.016f, 0.06f, dark, M);
            part(0, 0, 0.02f, 0.03f, 0.035f, 0.08f, dark, M);
            part(0, -0.015f, 0.16f, 0.024f, 0.035f, 0.06f, mid, M);
            part(0, 0.038f, 0.0f, 0.008f, 0.004f, 0.06f, acc, A);
            break;
        case WPN_STICKY:     // drum magazine, stubby wide barrel, canister on top
            part(0, 0, -0.06f, 0.03f, 0.03f, 0.13f, mid, M);
            part(0, -0.02f, 0.04f, 0.055f, 0.055f, 0.04f, dark, M);
            part(0, -0.02f, 0.04f, 0.057f, 0.012f, 0.03f, acc, A);
            part(0, 0.045f, -0.02f, 0.014f, 0.014f, 0.05f, dark, M);
            part(0, 0.06f, -0.02f, 0.008f, 0.003f, 0.04f, glow, G);
            part(0, 0, -0.2f, 0.036f, 0.036f, 0.012f, dark, M);
            break;
        case WPN_BUBBLE:     // a round soap tank on top, a fat body, a flared wand nozzle with a bubble on it
            part(0, 0, 0, 0.03f, 0.035f, 0.11f, mid, M);
            part(0, 0.065f, 0.02f, 0.04f, 0.035f, 0.045f, light, B);
            part(0, 0.065f, 0.02f, 0.03f, 0.04f, 0.035f, glow, G);
            part(0, 0.004f, -0.15f, 0.014f, 0.014f, 0.05f, dark, M);
            part(0, 0.004f, -0.205f, 0.03f, 0.03f, 0.006f, acc, A);
            part(0, 0.004f, -0.235f, 0.022f, 0.022f, 0.022f, glow, G);
            part(0, -0.06f, 0.03f, 0.014f, 0.035f, 0.02f, dark, M);
            break;
        case WPN_SYRINGE:    // barrel full of glowing serum, plunger and finger grips behind, steel needle in front
            part(0, 0, 0, 0.022f, 0.022f, 0.09f, light, B);
            part(0, 0, -0.01f, 0.016f, 0.016f, 0.07f, glow, G);
            part(0, 0, 0.1f, 0.006f, 0.006f, 0.03f, mid, M); part(0, 0, 0.135f, 0.02f, 0.02f, 0.006f, dark, M);
            part(0, 0, 0.085f, 0.045f, 0.008f, 0.006f, acc, A);
            part(0, 0, -0.1f, 0.01f, 0.01f, 0.01f, dark, M); part(0, 0, -0.15f, 0.0025f, 0.0025f, 0.05f, mid, M);
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
        case WPN_CROSSBOW:   // stock, bow limbs with a string, a bolt on top
            part(0, 0, 0.02f, 0.02f, 0.025f, 0.2f, dark, M);
            part(0, 0.012f, -0.17f, 0.13f, 0.008f, 0.012f, mid, M);
            part(-0.115f, 0.012f, -0.15f, 0.012f, 0.01f, 0.03f, mid, M); part(0.115f, 0.012f, -0.15f, 0.012f, 0.01f, 0.03f, mid, M);
            part(0, 0.014f, -0.08f, 0.12f, 0.002f, 0.002f, light, B);
            part(0, 0.032f, -0.12f, 0.004f, 0.004f, 0.11f, acc, A);
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
        case WPN_STICKY: // a spiked mine: dark shell, team-coloured core, spikes (the game spins it)
            e({0, 0, 0}, {0.11f, 0.11f, 0.11f}, metalC, N);
            e({0, 0, 0}, {0.075f, 0.135f, 0.075f}, col * 1.3f * core, T);
            e({0, 0, 0}, {0.135f, 0.075f, 0.075f}, metalC * 0.7f, N);
            e({0, 0, 0}, {0.075f, 0.075f, 0.135f}, metalC * 0.7f, N);
            break;
        case WPN_ROCKET: // a rocket: nose cone, body with a team band, four fins and a flickering flame
            e({0, 0, 0.05f}, {0.07f, 0.07f, 0.22f}, metalC * 1.6f, N);
            e({0, 0, -0.22f}, {0.045f, 0.045f, 0.06f}, col * 1.2f, T);
            e({0, 0, 0}, {0.075f, 0.075f, 0.03f}, col, T);
            e({0, 0, 0.24f}, {0.14f, 0.012f, 0.05f}, metalC, N);
            e({0, 0, 0.24f}, {0.012f, 0.14f, 0.05f}, metalC, N);
            e({0, 0, 0.34f}, {0.05f * fl, 0.05f * fl, 0.08f * fl}, Vec3(2.0f, 1.3f, 0.5f), N);
            break;
        case WPN_SYRINGE: // thrown syringe: serum barrel, needle first
            e({0, 0, 0}, {0.03f, 0.03f, 0.12f}, Vec3(0.8f, 0.85f, 0.9f), N);
            e({0, 0, 0}, {0.022f, 0.022f, 0.09f}, glow * 1.5f, TINT_GLOW);
            e({0, 0, -0.17f}, {0.005f, 0.005f, 0.06f}, metalC * 2.0f, N);
            break;
        case WPN_KNIFE: // thrown knife (the game spins it end over end)
            e({0, 0, 0}, {0.012f, 0.03f, 0.18f}, {0.75f, 0.77f, 0.8f}, N);
            break;
        case WPN_BUBBLE: { // a wobbling bubble: a bright core in a see-through shell
            float r = 0.17f + wob;
            e({0, 0, 0}, {r, r - wob, r}, col, T, false, false, 0.35f);
            e({0.05f, 0.06f, 0}, {0.035f, 0.035f, 0.035f}, Vec3(1, 1, 1) * 1.5f, N);
        } break;
        case WPN_CROSSBOW: // a bolt: dark shaft, steel broadhead, team-coloured fletching
            e({0, 0, 0}, {0.02f, 0.02f, 0.33f}, {0.12f, 0.09f, 0.06f}, N);
            e({0, 0, -0.36f}, {0.045f, 0.012f, 0.05f}, Vec3(0.8f, 0.82f, 0.86f), N);
            e({0, 0, 0.28f}, {0.06f, 0.006f, 0.06f}, col * 1.2f, T);
            e({0, 0, 0.28f}, {0.006f, 0.06f, 0.06f}, col * 1.2f, T);
            break;
        default: // an orb
            e({0, 0, 0}, {0.12f, 0.12f, 0.12f}, col * 1.3f, T);
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
