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

struct DynVertex { float pos[3]; float normal[3]; float color[4]; float edge[4]; }; // color.a = sun visibility
struct FlatVertex { float pos[3]; float color[4]; };
struct Vertex2D { float pos[2]; float uv[2]; float color[4]; };

struct Color { float r, g, b, a; };
inline Color rgba(const Vec3& c, float a = 1) { return {c.x, c.y, c.z, a}; }

class Renderer {
public:
    bool init(int shadowRes, std::string* err);
    void resize(int w, int h) { width_ = w; height_ = h; }
    int width() const { return width_; }
    int height() const { return height_; }
    void loadWorld(const Map& map, const std::function<void(float)>& progress);
    bool worldLoaded() const { return worldVerts_ > 0; }

    void setCamera(const Vec3& eye, float yaw, float pitch, float hfovDeg);
    const Vec3& cameraPos() const { return camPos_; }
    // Projects a world point to screen pixels; returns false if behind the camera.
    bool project(const Vec3& p, float& sx, float& sy) const;

    // Dynamic geometry (cleared every frame)
    void box(const Vec3& center, const Vec3& half, float yaw, const Vec3& color, float sunVis, bool castShadow = true, bool visible = true);
    void boxPitched(const Vec3& pivot, const Vec3& offset, const Vec3& half, float yaw, float pitch, const Vec3& color, float sunVis);
    void viewBox(const Vec3& offset, const Vec3& half, const Vec3& color); // camera-relative (viewmodel)
    void translucentBox(const AABB& b, const Color& c);
    void line(const Vec3& a, const Vec3& b, float width, const Color& c);

    void drawScene(bool shadows);

    // 2D (pixels, origin top-left)
    void rect(float x, float y, float w, float h, const Color& c);
    void text(float x, float y, float scale, const std::string& s, const Color& c);
    float textWidth(const std::string& s, float scale) const { return s.size() * 6.0f * scale; }
    float textHeight(float scale) const { return 8.0f * scale; }
    void flush2D();

    void setShadowsAvailable(bool b) { shadowsOn_ = b; }
    bool screenshot(const std::string& path);

    // palette
    Vec3 skyColor = {0.62f, 0.66f, 0.72f};
    Vec3 baseColor = {0.62f, 0.62f, 0.64f};
    Vec3 accentColor = {0.40f, 0.40f, 0.43f};

private:
    unsigned compile(const char* vs, const char* fs, std::string* err);
    void addQuad(std::vector<DynVertex>& out, const Vec3* c, const Vec3& n, const Vec3& col, float sv);
    void emitBox(std::vector<DynVertex>& out, const Mat4& xf, const Vec3& half, const Vec3& color, float sunVis);

    int width_ = 1280, height_ = 720;
    unsigned worldProg_ = 0, dynProg_ = 0, flatProg_ = 0, shadowProg_ = 0, uiProg_ = 0;
    unsigned worldVao_ = 0, worldVbo_ = 0, lightmapTex_ = 0;
    int worldVerts_ = 0;
    unsigned dynVao_ = 0, dynVbo_ = 0, flatVao_ = 0, flatVbo_ = 0, uiVao_ = 0, uiVbo_ = 0, shadowVao_ = 0;
    unsigned shadowFbo_ = 0, shadowTex_ = 0;
    int shadowRes_ = 2048;
    bool shadowsOn_ = true;
    unsigned fontTex_ = 0;
    size_t dynCap_ = 0, flatCap_ = 0, uiCap_ = 0;

    std::vector<DynVertex> dyn_, dynShadowOnly_, view_;
    std::vector<FlatVertex> flat_;
    std::vector<Vertex2D> ui_;

    Vec3 camPos_, sunDir_ = {0, 1, 0};
    float camYaw_ = 0, camPitch_ = 0;
    Mat4 view_m_, proj_, viewProj_;
};
