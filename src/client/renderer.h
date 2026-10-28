// Renderer: baked-lightmap world, shadow-mapped dynamic objects (players, projectiles),
// translucent markers, first-person viewmodel and a 2D layer for HUD/menus.
// Everything dynamic is streamed into one vertex buffer per layer each frame (few draw calls).
#pragma once
#include <memory>
#include "fontatlas.h"
#include "textfit.h"
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
// in the shader, so each character is one quad. pad[0]: the title's split Q; pad[1]: a rounded rect (roundRect: uv its
// own pixel position * 4, scaleInt its corner radius, scaleFrac its ring width * 4).
struct Vertex2D { int16_t pos[2]; uint16_t uv[2]; uint8_t color[4]; uint8_t scaleInt, scaleFrac, pad[2]; };
// The 2D batch's allocator: growing the batch leaves the new vertices unset (every writer sets all of a vertex's
// fields), so text and rects don't zero a vertex just before writing it.
template <class T> struct NoInitAlloc : std::allocator<T> {
    template <class U> struct rebind { using other = NoInitAlloc<U>; };
    NoInitAlloc() = default;
    template <class U> NoInitAlloc(const NoInitAlloc<U>&) noexcept {}
    template <class U> void construct(U* p) noexcept { ::new ((void*)p) U; }
    template <class U, class... A> void construct(U* p, A&&... a) { ::new ((void*)p) U(std::forward<A>(a)...); }
};
using Vertex2DList = std::vector<Vertex2D, NoInitAlloc<Vertex2D>>;

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
    // Late latch: a newer aim just before drawScene (the world and players are in world space, so only the view moves).
    // The view model was already built for the old aim: drawScene carries it over (it never lags the world).
    void relatch(float yaw, float pitch);
    // Extra culling margin while the aim may still move before drawing (relatch): a sphere counts as on screen this
    // many radians (roughly) outside the frustum, so nothing at the edge pops in a frame late on a flick.
    float cullPad = 0;
    const Vec3& cameraPos() const { return camPos_; }
    float cameraYaw() const { return camYaw_; }
    float tanX() const { return tanX_; }   // tan(half the field of view): horizontal / vertical (the last setCamera)
    float tanY() const { return tanY_; }
    float cameraPitch() const { return camPitch_; }
    // Projects a world point to screen pixels; returns false if behind the camera.
    bool project(const Vec3& p, float& sx, float& sy) const;

    // Dynamic geometry (cleared every frame)
    void box(const Vec3& center, const Vec3& half, float yaw, const Vec3& color, float sunVis, bool castShadow = true, bool visible = true);
    void boxPitched(const Vec3& pivot, const Vec3& offset, const Vec3& half, float yaw, float pitch, const Vec3& color, float sunVis, bool castShadow = true);
    void viewBox(const Vec3& offset, const Vec3& half, const Vec3& color); // camera-relative (viewmodel)
    void viewBoxXf(const Mat4& local, const Vec3& half, const Vec3& color);  // camera-relative, turned (local: camera space)
    // an override model (models.h, docs/ASSETS.md) in place of a model's boxes: model space through xf, each vertex's
    // colour times tint[its slot]; viewMesh: camera-relative like viewBox (offset, then uniform scale)
    void mesh(const ModelMesh& m, const Mat4& xf, const Vec3* tint, float sunVis, bool castShadow = true, bool visible = true);
    void viewMesh(const ModelMesh& m, const Vec3& offset, float scale, const Vec3* tint);
    void viewMeshXf(const ModelMesh& m, const Mat4& local, const Vec3* tint);   // (local: model -> camera space)
    void overlayBox(const Vec3& center, const Vec3& half, const Vec3& color); // world space, drawn over everything (editor handles)
    Vec3 viewPoint(const Vec3& offset) const;  // where a camera-relative point (as viewBox takes) is in the world
    void translucentBox(const AABB& b, const Color& c);
    // A static batch of boxes (the editor's unbaked map, PERF-12): box() calls made inside emit go to a buffer that's
    // uploaded once and drawn every frame useStaticBoxes() is called, culled per box, instead of being streamed again
    // each frame (~21 MB a frame on Midtown). Rebuild it only when the boxes change.
    void setStaticBoxes(const std::function<void()>& emit);
    void useStaticBoxes() { staticOn_ = staticCount_ > 0; }   // draw it this frame
    void clearStaticBoxes() { staticCount_ = 0; staticItems_.clear(); staticItems_.shrink_to_fit(); std::vector<DynVertex>().swap(staticVerts_); }
    void line(const Vec3& a, const Vec3& b, float width, const Color& c);

    void drawScene(bool shadows);
    // The parts of drawScene that don't depend on the aim, done early (before the late latch): the frame's dynamic
    // geometry uploaded and the shadow pass drawn. drawScene then only draws the view (it does these itself otherwise).
    void prepareScene(bool shadows);

    // 2D (pixels, origin top-left)
    void rect(float x, float y, float w, float h, const Color& c);
    // a rect whose corners have their own colours (blended across it): colour pickers' hue and value ramps
    void rectGradient(float x, float y, float w, float h, const Color& tl, const Color& tr, const Color& br, const Color& bl);
    // A rect with rounded corners (radius px, at most half its shorter side), or with ring > 0 only a band that wide
    // inside its edge (a border): one quad in the same 2D batch, the corners cut by a distance test in the UI shader
    // (HUD themes, theme.h). Radius 0 and no ring is rect().
    void roundRect(float x, float y, float w, float h, float radius, const Color& c, float ring = 0);
    // Every 2D colour's alpha times a (a HUD element's theme opacity, set around its drawing); 1: as given.
    void setAlpha(float a) { alpha_ = a; }
    float alpha() const { return alpha_; }
    // The QUARK title (the menus'): centred on cx, its line top at y (as text() takes it), at text scale ts (fractional
    // is fine); the Q split along a slanted line with a black seam: its left and UARK in `friendly`, its right in
    // `enemy` (the player's team colours); friendly's alpha fades all of it. In Rubik: the baked logo
    // (tools/make_logo.py, logo_data.h), one mipmapped quad; in another UI font: the word in that font, the same size,
    // its Q split by the text shader. Either way in the 2D batch: no extra pass or draw call.
    void logo(float cx, float y, float ts, const Color& friendly, const Color& enemy);
    // level: how far its letters are closed up (font units, each pair as far as its ink allows: textfit.h; Ui's fitting)
    void text(float x, float y, float scale, const std::string& s, const Color& c, float level = 0);
    // proportional UI font (Rubik Bold, or the one picked in Settings > FONT): widths come from per-glyph advances
    // and kerning. A font whose ink would leave the 8-unit line box (with its outline) is drawn a little smaller, the
    // same everywhere (textfit.h lineFit; 1 for Rubik and every built-in that fits), and measured so.
    float textWidth(const std::string& s, float scale, float level = 0) const;
    // s fitted into maxW at scale: closed up (the letters' fills never touch), then smaller, then cut ("..."): textfit.h
    TextFit fitText(const std::string& s, float scale, float maxW) const;
    int fontGeneration() const { return fontGen_; }   // (changes when the UI font does: Ui's fit cache)
    void textMono(float x, float y, float scale, const std::string& s, const Color& c); // fixed advance (console)
    float textWidthMono(const std::string& s, float scale) const;
    size_t textFit(const std::string& s, float scale, float maxW) const; // how many leading chars fit in maxW
    std::string textClip(const std::string& s, float scale, float maxW) const; // fit, with "..." if cut
    // smallest drawn text scale: the proportional fonts' caps are ~5 units tall, so scale 1 would be unreadable; a pixel
    // font (7 units, 1 a pixel) goes down to 1 (FontAtlas::minScale): minTextScale() is the font in use's
    static constexpr float kMinTextScale = 1.3f;
    float minTextScale() const { return minScale_; }
    float textHeight(float scale) const { return 8.0f * (scale < minScale_ ? minScale_ : scale); }
    // The UI font (Settings > FONT, uifonts.h): its atlas replaces the texture's texels in place (same layout) and its
    // metrics become the ones every text call uses. null: the baked default (font_ui.h). Once per pick, never per frame.
    void setUiFont(std::unique_ptr<FontAtlas> font);
    static FontAtlasLayout uiFontLayout();   // font_ui.h's layout, sized to Rubik's capitals
    void flush2D();
    // The 2D batch so far uploaded now (the HUD before the late latch): flush2D then only draws it, unless more was added.
    void prepare2D();

    void setShadowsAvailable(bool b) { shadowsOn_ = b; }
    // this frame's drawScene will run the shadow pass (false: off-screen casters aren't even built). Set before drawing.
    bool shadowCasters = true;
    void setMsaa(int samples) { msaa_ = samples; }     // 0 = off, 1 = edges (FXAA-style), 2 / 4 / 8 MSAA (clamped to what the driver supports)
    // RENDER SCALE (VIDEO): the 3D scene drawn at the window's size times pct / 100 (50 - 200), then stretched to the
    // window, filtered; the 2D layer (HUD, text, menus) is drawn after at the window's own size, so it stays sharp.
    // sharpen: below 100%, a contrast-adaptive sharpen in the stretch (not with EDGES, which smooths instead).
    void setRenderScale(int pct, bool sharpen) { scalePct_ = std::max(50, std::min(200, pct)); sharpen_ = sharpen; }
    // the size the scene was last drawn at (the window's, at 100% or with no world drawn yet): F3
    int sceneWidth() const { return sceneW_; }
    int sceneHeight() const { return sceneH_; }
    bool screenshot(const std::string& path);

    // palette
    Vec3 skyColor = {0.62f, 0.66f, 0.72f};
    Vec3 baseColor = {0.62f, 0.62f, 0.64f};
    Vec3 accentColor = {0.40f, 0.40f, 0.43f};
    Vec3 metalColor = {0.30f, 0.33f, 0.37f};
    Vec3 glowColor = {1.00f, 0.55f, 0.15f};
    float viewOpacity = 1.0f;  // GAMEPLAY > WEAPON MODEL OPACITY: the first-person gun and grapple launcher (0 = not drawn); one frame's, drawScene resets it
    bool fullbright = false;   // VISUALS > FULLBRIGHT: flat white world with black outlines
    Vec3 fullbrightColor = {1, 1, 1};   // ...that colour instead of white (VISUALS > FULLBRIGHT COLOUR)
    float brightness = 1.0f;   // the player's brightness setting (scales the lit scene before tonemapping)
    float worldOutline = 1.0f; // the baked world's outline width in pixels (settings); 0 = none

private:
    unsigned compile(const char* vs, const char* fs, std::string* err);
    void aim(float yaw, float pitch);   // (setCamera's view half: the view, its vectors, viewProj_ and the frustum planes)
    void addQuad(std::vector<DynVertex>& out, const Vec3* c, const Vec3& n, const Vec3& col, float sv);
    void emitBox(std::vector<DynVertex>& out, const Mat4& xf, const Vec3& half, const Vec3& color, float sunVis);
    void emitMesh(std::vector<DynVertex>& out, const ModelMesh& m, const Mat4& xf, const Vec3* tint, float sunVis, std::vector<FlatVertex>* flatOut);
public:
    Vec3 outlineColor;   // the edge colour for the boxes drawn while it's set (players' outline setting); black by default
    float outlineWidth = 1.0f;   // ...and the edge width in pixels (players' / the editor's boxes: settings); 0 = none
private:

    int width_ = 1280, height_ = 720;
    unsigned worldProg_ = 0, dynProg_ = 0, flatProg_ = 0, shadowProg_ = 0, uiProg_ = 0, skyProg_ = 0;
    // Uniform locations, looked up once after linking (init): no lookups in the frame loop. Values that never change are
    // set there too, the per-frame ones that don't depend on the aim in prepareScene, the rest in drawScene (GL keeps
    // each program's values).
    struct { GLint viewProj, shadowMat, shadowOn, casterRect, camPos, fogColor, bright, baseL, accentL, wallbangL, metalL, floorL, metal, glow, full, fullColor, outlineW, time; } uWorld_ = {};
    struct { GLint viewProj, shadowMat, shadowOn, casterRect, camPos, fogColor, bright, receive, sunDir; } uDyn_ = {};
    struct { GLint fwd, right, up, tan, sky, sunDir, bright; } uSky_ = {};
    GLint uDynFade_ = -1, uFlatFade_ = -1;   // (the view pass's see-through amount: WEAPON MODEL OPACITY)
    GLint uFlatViewProj_ = -1, uShadowViewProj_ = -1, uUiScreen_ = -1, uEdgeRcp_ = -1, uSharpRcp_ = -1, uWorldPx_ = -1, uDynPx_ = -1;
    GLint uLogoA_ = -1, uLogoB_ = -1, uSplitP_ = -1, uSplitN_ = -1, uSplitSeam_ = -1;   // (the title: Renderer::logo)
    float uiScreen_[2] = {}, edgeRcp_[2] = {}, sharpRcp_[2] = {};   // (the values last set: uScreen / uRcp change only with the size)
    float pxScale_ = 1;                          // (uPxScale's value: scene pixels per window pixel)
    float clear_[3] = {};                        // (the sky colour in display space: prepareScene works it out)
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
    // RENDER SCALE: the target is sceneW_ x sceneH_ (the window's size times scalePct_ / 100), and is made even with AA
    // off (a texture, as edges); with MSAA, resolveFbo_ / resolveTex_ take the resolved samples at that size, since a
    // multisampled blit can't stretch. At 100% with AA off there's no target at all (the scene draws to the window).
    int msaa_ = 0, msaaActive_ = 0, scalePct_ = 100, sceneW_ = 1280, sceneH_ = 720;
    int triedAa_ = -1, triedW_ = 0, triedH_ = 0;   // (the target last asked for: ensureMsaaTarget)
    bool sharpen_ = true;
    unsigned msaaFbo_ = 0, msaaColor_ = 0, msaaDepth_ = 0, edgeTex_ = 0, edgeProg_ = 0;
    unsigned resolveFbo_ = 0, resolveTex_ = 0, sharpProg_ = 0, fsVao_ = 0;
    void ensureMsaaTarget();
public:
    bool inView(const Vec3& center, float radius) const; // sphere vs the current view frustum
private:
    unsigned dynVao_ = 0, dynVbo_ = 0, flatVao_ = 0, flatVbo_ = 0, uiVao_ = 0, uiVbo_ = 0;
    unsigned shadowFbo_ = 0, shadowTex_ = 0;
    int shadowRes_ = 2048;
    bool shadowsOn_ = true;
    unsigned fontTex_ = 0;
    unsigned logoTex_ = 0;     // (logo_data.h, RGBA, mipmapped; bound to unit 1 by a flush that draws it)
    bool logoDrawn_ = false;   // (this frame's 2D batch has the logo in it)
    bool splitDrawn_ = false;  // (...or the title in another font, its Q split: splitP_ / N_ / Seam_)
    Color logoA_ = {1, 1, 1, 1}, logoB_ = {1, 1, 1, 1};
    float splitP_[2] = {}, splitN_[2] = {1, 0}, splitSeam_ = 0;
    float minScale_ = kMinTextScale;   // (the UI font's smallest scale: setUiFont)
    int fontGen_ = 0;
    size_t dynCap_ = 0, flatCap_ = 0, uiCap_ = 0;
    float alpha_ = 1;         // (setAlpha)
    void uiIndices(size_t quads);   // (the 2D batch's index buffer covers at least this many quads)
    unsigned uiEbo_ = 0;      // the 2D batch's indices: 4 vertices a quad, drawn as 0 1 2 / 0 3 2 (fixed; grown when needed)
    size_t uiIdxQuads_ = 0;   // (quads the index buffer covers)
    size_t uiUploaded_ = 0;   // (prepare2D: this many 2D vertices are in uiVbo_ already; 0: none)

    std::vector<DynVertex> dyn_, dynShadowOnly_, view_;
    std::vector<DynVertex> dynNoCast_;   // drawn with dyn_, but no shadow (and not in the shadow pass's caster rect)
    struct StaticItem { Vec3 c; float r; int first, count; };
    std::vector<StaticItem> staticItems_;
    std::vector<DynVertex> staticVerts_;          // (only while setStaticBoxes builds it; its memory kept until clearStaticBoxes)
    std::vector<int> staticFirst_, staticCnt_;
    unsigned staticVao_ = 0, staticVbo_ = 0;
    int staticCount_ = 0;
    bool buildingStatic_ = false, staticOn_ = false;
    void setupDynVao(unsigned vao, unsigned vbo);
    std::vector<FlatVertex> flat_;
    std::vector<FlatVertex> viewFlat_;   // the view model's translucent parts (drawn in its pass; empty for most models)
    Vertex2DList ui_;
    float casterRect_[4] = {0, 0, 1, 1}; // shadow-map rect covered by dynamic casters (this frame)
    bool prep_ = false, prepShadows_ = false;   // (prepareScene ran this frame; with the shadow pass)
    size_t prepN_[6] = {};                      // (its uploads: shadow only, lit casting, lit not casting, view model; flat, view flat)
    Mat4 shadowMat_;

    Vec3 camPos_, sunDir_ = {0, 1, 0};
    float camYaw_ = 0, camPitch_ = 0, camFov_ = 90, camAspect_ = 0;
    Mat4 viewFix_;   // (relatch: the old view to the new one, for the view model built for the old)
    Mat4 viewProjFix_;   // (viewProj_ * viewFix_: the view model's pass)
    Mat4 view_m_, proj_, viewProj_;
};
