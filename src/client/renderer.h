// Renderer: baked-lightmap world, shadow-mapped dynamic objects (players, projectiles),
// translucent markers, first-person viewmodel and a 2D layer for HUD/menus.
// Everything dynamic is streamed into one vertex buffer per layer each frame (few draw calls).
#pragma once
#include <string>
#include <vector>
#include "../common/qmath.h"
#include "../game/map.h"
#include "gl.h"
#include "lightmap.h"
#include "models.h"

struct DynVertex { float pos[3]; float normal[3]; float color[4]; float edge[4]; float outline[4]; }; // color.a = sun visibility; outline = edge colour (black unless set) + width (pixels)
struct FlatVertex { float pos[3]; float color[4]; };
// 16 bytes: screen pixels, font atlas texel coords (u16 fixed point: (texel + 1) * 32), color (RGBA8), and for text
// the text scale (integer part + 1/256ths; 0 = plain rect), which sets the distance-field edge and outline width
// in the shader, so each character is one quad.
struct Vertex2D { int16_t pos[2]; uint16_t uv[2]; uint8_t color[4]; uint8_t scaleInt, scaleFrac, pad[2]; };

struct Color { float r, g, b, a; };
inline Color rgba(const Vec3& c, float a = 1) { return {c.x, c.y, c.z, a}; }

class Renderer {
public:
    bool init(int shadowRes, std::string* err);
    void resize(int w, int h) { width_ = w; height_ = h; }
    int width() const { return width_; }
    int height() const { return height_; }
    // World geometry grouped into spatial chunks (12 m columns) so the world pass only draws what's in
    // the view frustum: one glMultiDrawArrays over the visible ranges.
    struct WorldChunk { Vec3 mn, mx; int first, count; };
    // A map's world, built and lit but not on the GPU yet: prepareWorld needs no GL context (any thread, on its
    // own copy of the map), uploadWorld puts it on the GPU (the GL thread). loadWorld = both, here and now.
    struct PreparedWorld { BakedWorld bw; std::vector<WorldChunk> chunks; };
    static void prepareWorld(const Map& map, PreparedWorld& out, const std::function<void(float)>& progress,
                             const std::atomic<bool>* cancel = nullptr);
    void uploadWorld(const Map& map, PreparedWorld& w);
    void loadWorld(const Map& map, const std::function<void(float)>& progress);
    void warmup(const Map& map); // compile every shader / state now, not on the first match frame
    bool worldLoaded() const { return worldVerts_ > 0; }
    void unloadWorld(const Vec3& sunDir) { worldVerts_ = 0; sunDir_ = sunDir; } // editor: draw only dynamic boxes

    void setCamera(const Vec3& eye, float yaw, float pitch, float hfovDeg);
    const Vec3& cameraPos() const { return camPos_; }
    float cameraYaw() const { return camYaw_; }
    float cameraPitch() const { return camPitch_; }
    // Projects a world point to screen pixels; returns false if behind the camera.
    bool project(const Vec3& p, float& sx, float& sy) const;

    // Dynamic geometry (cleared every frame)
    void box(const Vec3& center, const Vec3& half, float yaw, const Vec3& color, float sunVis, bool castShadow = true, bool visible = true);
    void boxPitched(const Vec3& pivot, const Vec3& offset, const Vec3& half, float yaw, float pitch, const Vec3& color, float sunVis);
    void viewBox(const Vec3& offset, const Vec3& half, const Vec3& color); // camera-relative (viewmodel)
    // an override model (models.h, docs/ASSETS.md) in place of a model's boxes: model space through xf, each vertex's
    // colour times tint[its slot]; viewMesh: camera-relative like viewBox (offset, then uniform scale)
    void mesh(const ModelMesh& m, const Mat4& xf, const Vec3* tint, float sunVis, bool castShadow = true, bool visible = true);
    void viewMesh(const ModelMesh& m, const Vec3& offset, float scale, const Vec3* tint);
    void overlayBox(const Vec3& center, const Vec3& half, const Vec3& color); // world space, drawn over everything (editor handles)
    Vec3 viewPoint(const Vec3& offset) const;  // where a camera-relative point (as viewBox takes) is in the world
    void translucentBox(const AABB& b, const Color& c);
    // A static batch of boxes (the editor's unbaked map, PERF-12): box() calls made inside emit go to a buffer that's
    // uploaded once and drawn every frame useStaticBoxes() is called, culled per box, instead of being streamed again
    // each frame (~21 MB a frame on Midtown). Rebuild it only when the boxes change.
    void setStaticBoxes(const std::function<void()>& emit);
    void useStaticBoxes() { staticOn_ = staticCount_ > 0; }   // draw it this frame
    void clearStaticBoxes() { staticCount_ = 0; staticItems_.clear(); staticItems_.shrink_to_fit(); }
    void line(const Vec3& a, const Vec3& b, float width, const Color& c);

    void drawScene(bool shadows);

    // 2D (pixels, origin top-left)
    void rect(float x, float y, float w, float h, const Color& c);
    void text(float x, float y, float scale, const std::string& s, const Color& c);
    // proportional UI font (Work Sans Bold): widths come from per-glyph advances and kerning
    float textWidth(const std::string& s, float scale) const;
    void textMono(float x, float y, float scale, const std::string& s, const Color& c); // fixed advance (console)
    float textWidthMono(const std::string& s, float scale) const;
    size_t textFit(const std::string& s, float scale, float maxW) const; // how many leading chars fit in maxW
    std::string textClip(const std::string& s, float scale, float maxW) const; // fit, with "..." if cut
    // smallest drawn text scale: the proportional font's caps are ~5 units tall, so scale 1 would be unreadable
    static constexpr float kMinTextScale = 1.3f;
    float textHeight(float scale) const { return 8.0f * (scale < kMinTextScale ? kMinTextScale : scale); }
    void flush2D();

    void setShadowsAvailable(bool b) { shadowsOn_ = b; }
    void setMsaa(int samples) { msaa_ = samples; }     // 0 = off, 1 = edges (FXAA-style), 2 / 4 / 8 MSAA (clamped to what the driver supports)
    bool screenshot(const std::string& path);

    // palette
    Vec3 skyColor = {0.62f, 0.66f, 0.72f};
    Vec3 baseColor = {0.62f, 0.62f, 0.64f};
    Vec3 accentColor = {0.40f, 0.40f, 0.43f};
    Vec3 metalColor = {0.30f, 0.33f, 0.37f};
    Vec3 glowColor = {1.00f, 0.55f, 0.15f};
    bool fullbright = false;   // VISUALS > FULLBRIGHT: flat white world with black outlines
    Vec3 fullbrightColor = {1, 1, 1};   // ...that colour instead of white (VISUALS > FULLBRIGHT COLOUR)
    float brightness = 1.0f;   // the player's brightness setting (scales the lit scene before tonemapping)
    float worldOutline = 1.0f; // the baked world's outline width in pixels (settings); 0 = none

private:
    unsigned compile(const char* vs, const char* fs, std::string* err);
    void addQuad(std::vector<DynVertex>& out, const Vec3* c, const Vec3& n, const Vec3& col, float sv);
    void emitBox(std::vector<DynVertex>& out, const Mat4& xf, const Vec3& half, const Vec3& color, float sunVis);
    void emitMesh(std::vector<DynVertex>& out, const ModelMesh& m, const Mat4& xf, const Vec3* tint, float sunVis, bool translucent);
public:
    Vec3 outlineColor;   // the edge colour for the boxes drawn while it's set (players' outline setting); black by default
    float outlineWidth = 1.0f;   // ...and the edge width in pixels (players' / the editor's boxes: settings); 0 = none
private:

    int width_ = 1280, height_ = 720;
    unsigned worldProg_ = 0, dynProg_ = 0, flatProg_ = 0, shadowProg_ = 0, uiProg_ = 0, skyProg_ = 0;
    Vec3 camFwd_, camRight_, camUp_;
    float tanX_ = 1, tanY_ = 1;
    unsigned worldVao_ = 0, worldVbo_ = 0, lightmapTex_ = 0;
    int worldVerts_ = 0;
    std::vector<WorldChunk> chunks_;
    std::vector<int> drawFirst_, drawCount_;
    void drawWorldChunks();
    float frustum_[6][5] = {};
    // anti-aliasing: the 3D scene renders into a multisampled framebuffer that is resolved into the window
    // before the UI is drawn (so the HUD and menus don't pay for it). 0 / 1 = off.
    // aa: 0 off, 1 edges (the scene goes to a texture, then one cheap FXAA-style pass smooths the high-contrast
    // edges, i.e. the outlines), 2 / 4 / 8 MSAA
    int msaa_ = 0, msaaW_ = 0, msaaH_ = 0, msaaActive_ = 0;
    unsigned msaaFbo_ = 0, msaaColor_ = 0, msaaDepth_ = 0, edgeTex_ = 0, edgeProg_ = 0;
    void ensureMsaaTarget();
public:
    bool inView(const Vec3& center, float radius) const; // sphere vs the current view frustum
private:
    unsigned dynVao_ = 0, dynVbo_ = 0, flatVao_ = 0, flatVbo_ = 0, uiVao_ = 0, uiVbo_ = 0, shadowVao_ = 0;
    unsigned shadowFbo_ = 0, shadowTex_ = 0;
    int shadowRes_ = 2048;
    bool shadowsOn_ = true;
    unsigned fontTex_ = 0;
    size_t dynCap_ = 0, flatCap_ = 0, uiCap_ = 0;

    std::vector<DynVertex> dyn_, dynShadowOnly_, view_;
    struct StaticItem { Vec3 c; float r; int first, count; };
    std::vector<StaticItem> staticItems_;
    std::vector<DynVertex> staticVerts_;          // (only while setStaticBoxes builds it)
    std::vector<int> staticFirst_, staticCnt_;
    unsigned staticVao_ = 0, staticVbo_ = 0;
    int staticCount_ = 0;
    bool buildingStatic_ = false, staticOn_ = false;
    void setupDynVao(unsigned vao, unsigned vbo);
    std::vector<FlatVertex> flat_;
    std::vector<Vertex2D> ui_;
    float casterRect_[4] = {0, 0, 1, 1}; // shadow-map rect covered by dynamic casters (this frame)

    Vec3 camPos_, sunDir_ = {0, 1, 0};
    float camYaw_ = 0, camPitch_ = 0;
    Mat4 view_m_, proj_, viewProj_;
};
