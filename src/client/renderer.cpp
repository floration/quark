#include "renderer.h"
#include "../common/u8file.h"
#include "../common/profile.h"
#include "../common/net.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include "font_ui.h"
#include "logo_data.h"
#include "overrides.h"
#include "fontatlas.h"
#include "textfit.h"
#include <memory>

namespace {

// UI font: Rubik Bold as a signed distance field (font_ui.h, made by tools/make_font_atlas.py), or the one picked in
// Settings > FONT (Renderer::setUiFont). Font units: a line is 8 tall; advances and kerning are proportional.
inline int glyphIndex(char ch) { unsigned char uc = (unsigned char)ch; return (uc < 32 || uc > 126) ? '?' - 32 : uc - 32; }
// The metrics in use: font_ui.h's, or the picked font's (g_uiFont, built when it was picked)
const float* g_uiAdv = kUiAdv;
const signed char* g_uiKern = kUiKern;
std::unique_ptr<FontAtlas> g_uiFont;
// ...and what's measured from it (textfit.h: every glyph's ink, the scale that keeps it in the line box), once per font
const LineBox& defaultLineBox() {   // (Rubik's own ink defines the box)
    static const LineBox box = [] {
        static GlyphInk ink[95];
        measureInk(kUiAtlas, {kUiTexW, kUiTexH, kUiCell, kUiTpu, kUiPad, kUiOrigin, kUiSpread}, ink);
        return lineBoxOf(ink);
    }();
    return box;
}
TextMetrics& uiMetrics() {
    static TextMetrics m = [] {
        TextMetrics t;
        t.adv = kUiAdv; t.kern = kUiKern;
        measureInk(kUiAtlas, {kUiTexW, kUiTexH, kUiCell, kUiTpu, kUiPad, kUiOrigin, kUiSpread}, t.ink);
        LineFit lf = lineFit(t.ink, defaultLineBox());
        t.fitScale = lf.scale; t.fitShift = lf.shift;
        t.minScale = Renderer::kMinTextScale;
        return t;
    }();
    return m;
}

const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in float aMat;
layout(location=4) in vec4 aEdge;
uniform mat4 uViewProj;
uniform mat4 uShadowMat;
out vec3 vPos; out vec3 vN; out vec2 vUV; out float vMat; out vec4 vShadow; out vec4 vEdge;
void main() {
    vPos = aPos; vN = aNormal; vUV = aUV; vMat = aMat; vEdge = aEdge;
    vShadow = uShadowMat * vec4(aPos + aNormal * 0.04, 1.0);
    gl_Position = uViewProj * vec4(aPos, 1.0);
})";

const char* kShadowFn = R"(

// 1 at a face's middle, 0 on its edges: the outline. w = its width in pixels (settings; 1 = the default look,
// smoothstep(1, 2)); 0 = no outline.
float outline(vec4 e, float w) {
    if (w <= 0.0) return 1.0;
    vec2 d2 = min(e.xy, e.zw - e.xy);
    float d = min(d2.x, d2.y);
    float px = fwidth(d);
    return smoothstep(w, w + 1.0, d / max(px, 1e-5));
}
uniform sampler2DShadow uShadow;
uniform int uShadowOn;
uniform float uShadowTexel;
uniform vec4 uCasterRect; // shadow-map area the dynamic casters cover: outside it nothing can be shadowed
float shadowAt(vec4 sc) {
    if (uShadowOn == 0) return 1.0;
    vec3 p = sc.xyz / sc.w * 0.5 + 0.5;
    if (p.x < uCasterRect.x || p.y < uCasterRect.y || p.x > uCasterRect.z || p.y > uCasterRect.w || p.z >= 1.0) return 1.0;
    float s = 0.0;
    s += texture(uShadow, vec3(p.xy + vec2(-0.5, -0.5) * uShadowTexel, p.z - 0.0015));
    s += texture(uShadow, vec3(p.xy + vec2( 0.5, -0.5) * uShadowTexel, p.z - 0.0015));
    s += texture(uShadow, vec3(p.xy + vec2(-0.5,  0.5) * uShadowTexel, p.z - 0.0015));
    s += texture(uShadow, vec3(p.xy + vec2( 0.5,  0.5) * uShadowTexel, p.z - 0.0015));
    return s * 0.25;
}
)";

const char* kWorldFS = R"(
in vec3 vPos; in vec3 vN; in vec2 vUV; in float vMat; in vec4 vShadow; in vec4 vEdge;
uniform sampler2D uLightmap;
uniform vec3 uSunColor, uSkyColor, uMetal, uGlow, uFogColor, uCamPos;
// palette in display space, linearised and deepened on the CPU (pow(c, 2.2) * 1.5) so colours read under
// bright sun: base, accent, wallbang (dark grey: base * 0.42), metal, and the floor tint (pow(metal, 2.2) * 2)
uniform vec3 uBaseL, uAccentL, uWallbangL, uMetalL, uFloorL;
uniform float uFogDensity;
uniform float uBright;   // the brightness setting (exposure, before the filmic curve)
uniform float uFull;     // fullbright: flat white, black outlines, no light or fog
uniform vec3 uFullColor; // ...and the flat colour (white by default)
uniform float uOutlineW; // the world's outline width (pixels; settings)
out vec4 fragColor;
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec3 finish(vec3 c) {
    float d = length(vPos - uCamPos);
    c = mix(c, uFogColor, 1.0 - exp(-d * uFogDensity)) * uBright;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0) * 1.08; // ACES-style filmic curve
    return pow(c, vec3(1.0 / 2.2));
}
void main() {
    int mat = int(vMat + 0.5);
    if (uFull > 0.5) {
        float g = mat == 2 ? 0.35 : mat == 5 ? 0.6 : mat == 6 ? 0.5 : mat == 7 ? 0.9 : abs(vN.y) > 0.5 ? 0.8 : 0.95;
        fragColor = vec4(uFullColor * g * (mat >= 4 && mat < 8 ? 1.0 : outline(vEdge, uOutlineW)), 1.0);
        return;
    }
    if (mat >= 4 && mat < 8) { // glow trim / far scenery / water / lava: unlit, skip everything below
        vec3 c = mat == 4 ? uGlow * 0.9
               : mat == 6 ? mix(vec3(0.02, 0.10, 0.20), vec3(0.10, 0.32, 0.50), 1.0 - abs(vN.y) * 0.6)   // water: deep blue, lighter at grazing faces
               : mat == 7 ? vec3(1.6, 0.42, 0.08)                                                         // lava: glowing
               : mix(uMetal * (0.55 + 0.25 * vN.y + 0.15 * abs(vN.x)), uFogColor, 0.45); // far: hazy silhouette
        fragColor = vec4(finish(c), 1.0);
        return;
    }
    vec2 lm = texture(uLightmap, vUV).rg;
    vec3 albedo = mat == 0 ? uBaseL : (mat == 1 || mat == 8) ? uAccentL : mat == 2 ? uWallbangL : uMetalL; // 8: trim (decor, not solid)
    // surface detail: 2 m panels (4 m on floors) with a faint per-panel tint and thin seams, world-space so it
    // tiles across brushes; floor contact darkening on walls
    vec3 an = abs(vN);
    vec2 pp = an.y > 0.5 ? vPos.xz * 0.25 : an.x > 0.5 ? vPos.zy * 0.5 : vPos.xy * 0.5;
    if (mat == 3) pp = pp * vec2(2.0, 0.5);
    vec2 cell = floor(pp), f = fract(pp), w = fwidth(pp) * 1.5;
    float seam = min(smoothstep(0.0, w.x + 0.012, min(f.x, 1.0 - f.x)), smoothstep(0.0, w.y + 0.012, min(f.y, 1.0 - f.y)));
    albedo *= (0.93 + 0.1 * hash2(cell + floor(vPos.y * 0.01))) * mix(0.82, 1.0, seam);
    if (mat == 0 && vN.y > 0.5) albedo = mix(albedo, uFloorL, 0.55); // floors / tops: darker, cooler than walls
    if (an.y < 0.5) albedo *= mix(0.72, 1.0, smoothstep(0.0, 0.6, fract(vPos.y + 1000.0) + step(1.0, vPos.y)));
    vec3 light = uSkyColor * lm.g + uSunColor * lm.r * shadowAt(vShadow);
    vec3 c = albedo * light * 0.62 * outline(vEdge, uOutlineW);
    fragColor = vec4(finish(c), 1.0);
})";

// Edge smoothing ("EDGES" anti-aliasing): FXAA-style. Low-contrast pixels (almost everything) pass straight
// through after 5 taps; on high-contrast edges (the outlines) it blends along the edge direction.
const char* kEdgeVS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
const char* kEdgeFS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform vec2 uRcp;
out vec4 fragColor;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec3 m = texture(uScene, vUV).rgb;
    float lNW = luma(texture(uScene, vUV + vec2(-1.0, -1.0) * uRcp).rgb), lNE = luma(texture(uScene, vUV + vec2(1.0, -1.0) * uRcp).rgb);
    float lSW = luma(texture(uScene, vUV + vec2(-1.0, 1.0) * uRcp).rgb), lSE = luma(texture(uScene, vUV + vec2(1.0, 1.0) * uRcp).rgb);
    float lM = luma(m);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE))), lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.05, lMax * 0.125)) { fragColor = vec4(m, 1.0); return; }
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0, 8.0) * uRcp;
    vec3 a = 0.5 * (texture(uScene, vUV + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uScene, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uScene, vUV - dir * 0.5).rgb + texture(uScene, vUV + dir * 0.5).rgb);
    float lB = luma(b);
    fragColor = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
})";

// sky: one full-screen triangle at the far plane, drawn after the world so only uncovered pixels shade
const char* kSkyVS = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p; gl_Position = vec4(p, 1.0, 1.0);
})";
const char* kSkyFS = R"(#version 330 core
in vec2 vNdc;
uniform vec3 uFwd, uRight, uUp, uSky, uSunDir;
uniform vec2 uTan;
uniform float uBright;
out vec4 fragColor;
void main() {
    vec3 d = normalize(uFwd + vNdc.x * uTan.x * uRight + vNdc.y * uTan.y * uUp);
    float up = clamp(d.y, 0.0, 1.0);
    vec3 zenith = uSky * vec3(0.55, 0.62, 0.8);
    vec3 c = mix(uSky, zenith, pow(up, 0.6));
    float s = max(dot(d, uSunDir), 0.0);
    c += vec3(1.0, 0.9, 0.7) * (pow(s, 600.0) * 4.0 + pow(s, 12.0) * 0.25);
    // a dark sky (space) gets a starfield: one hash per pixel on a 3D grid of directions, no geometry
    float dark = 1.0 - smoothstep(0.03, 0.08, dot(uSky, vec3(0.3, 0.59, 0.11)));
    if (dark > 0.0) {
        vec3 g = d * 260.0, cell = floor(g);
        float h = fract(sin(dot(cell, vec3(127.1, 311.7, 74.7))) * 43758.5453);
        if (h > 0.985) {
            vec3 j = fract(vec3(h * 17.0, h * 31.0, h * 47.0)) * 0.5 + 0.25;
            float r = length(g - cell - j);
            float b = (h - 0.985) / 0.015;
            c += mix(vec3(0.7, 0.8, 1.0), vec3(1.0, 0.9, 0.75), fract(h * 91.0)) * smoothstep(0.22, 0.0, r) * (0.6 + 2.4 * b * b) * dark;
        }
    }
    c *= uBright;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0) * 1.08;
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
})";

const char* kDynVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=3) in vec4 aEdge;
layout(location=4) in vec4 aOutline; // rgb: the edge colour, a: its width (pixels)
uniform mat4 uViewProj;
uniform mat4 uShadowMat;
out vec3 vPos; out vec3 vN; out vec4 vColor; out vec4 vShadow; out vec4 vEdge; out vec3 vOutline; flat out float vOutlineW;
void main() {
    vPos = aPos; vN = aNormal; vColor = aColor; vEdge = aEdge; vOutline = aOutline.rgb; vOutlineW = aOutline.a;
    vShadow = uShadowMat * vec4(aPos + aNormal * 0.03, 1.0);
    gl_Position = uViewProj * vec4(aPos, 1.0);
})";

const char* kDynFS = R"(
in vec3 vPos; in vec3 vN; in vec4 vColor; in vec4 vShadow; in vec4 vEdge; in vec3 vOutline; flat in float vOutlineW;
uniform vec3 uSunColor, uSkyColor, uSunDir, uFogColor, uCamPos;
uniform float uFogDensity;
uniform int uReceiveShadow;
uniform float uBright;
uniform float uFade;   // the view model's see-through amount (1 - WEAPON MODEL OPACITY); 0 (unset) everywhere else
out vec4 fragColor;
void main() {
    float sh = uReceiveShadow != 0 ? shadowAt(vShadow) : 1.0;
    float ndl = max(dot(normalize(vN), uSunDir), 0.0);
    vec3 light = uSkyColor * (0.6 + 0.25 * vN.y) + uSunColor * ndl * vColor.a * sh;
    vec3 c = mix(vOutline, vColor.rgb * (light * 0.7 + 0.3), outline(vEdge, vOutlineW)); // partly emissive so team colors stay readable; edges in the outline colour
    float d = length(vPos - uCamPos);
    c = mix(c, uFogColor, 1.0 - exp(-d * uFogDensity)) * uBright;
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0 - uFade);
})";

const char* kFlatVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPos, 1.0); })";
const char* kFlatFS = R"(#version 330 core
in vec4 vColor;
uniform float uFade;   // (as the dynamic shader's: the view model's translucent parts)
out vec4 fragColor;
void main() { fragColor = vec4(pow(vColor.rgb, vec3(1.0 / 2.2)), vColor.a * (1.0 - uFade)); })";

const char* kShadowVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); })";
const char* kShadowFS = R"(#version 330 core
out vec4 fragColor;
void main() { fragColor = vec4(1.0); })";

const char* kUiVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
layout(location=3) in vec4 aText; // text scale: x integer part, y 1/256ths (0 = plain rect); z 1: the title's split Q
uniform vec2 uScreen;
uniform vec2 uFontSize;
uniform vec2 uLogoSize;
out vec2 vUV; out vec4 vColor; flat out float vScale; flat out float vSplit;
void main() {
    vScale = aText.x + aText.y / 256.0;   // (255 + 255/256: a logo texel, Renderer::logo)
    vSplit = aText.z;
    vUV = (aUV / 32.0 - 1.0) / (vScale > 255.5 ? uLogoSize : uFontSize); vColor = aColor;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";
// Text: one distance-field sample per pixel. The glyph edge (0.5) is smoothed over one screen pixel; a band
// outside it (1 px, wider on big text) is the black outline, coverage-blended under the fill.
const char* kUiFS = R"(#version 330 core
in vec2 vUV; in vec4 vColor; flat in float vScale; flat in float vSplit;
uniform sampler2D uFont;
uniform sampler2D uLogo;
uniform float uK; // distance-field units per screen pixel at scale 1 (texels per font unit / (2 * spread))
// the title (Renderer::logo): its two team colours; the split Q's line (a window point, its normal towards the second
// colour) and half the seam's width (px)
uniform vec3 uLogoA, uLogoB;
uniform vec2 uSplitP, uSplitN;
uniform float uSplitSeam;
out vec4 fragColor;
void main() {
    if (vScale > 255.5) {   // the baked title: masks, R the friendly half, G the enemy half, A the black outline
        vec4 t = texture(uLogo, vUV);
        float a = max(t.a, max(t.r, t.g));
        vec3 c = uLogoA * t.r + uLogoB * t.g;   // (over black: premultiplied)
        fragColor = vec4(a > 0.0 ? c / a : c, a * vColor.a);
        return;
    }
    float d = texture(uFont, vUV).r;
    if (vScale <= 0.0) { fragColor = vec4(vColor.rgb, vColor.a * d); return; }
    float px = uK / vScale;                       // one screen pixel in distance-field units
    float w = 0.5 * px;
    float od = 0.5 - max(1.0, vScale * 0.15) * px; // outline edge
    float o = smoothstep(od - w, od + w, d);
    if (o <= 0.004) discard;
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    vec3 col = vColor.rgb;
    if (vSplit > 0.5) {   // the title's Q in another font: the second colour past the slanted line, a black seam on it
        float s = dot(gl_FragCoord.xy - uSplitP, uSplitN);
        col = mix(col, uLogoB, clamp(s + 0.5, 0.0, 1.0)) * (1.0 - clamp(uSplitSeam + 0.5 - abs(s), 0.0, 1.0));
    }
    fragColor = vec4(col * fill, vColor.a * o);
})";

// Uniforms by location (Renderer::init looks each one up once: no string or hash lookups in the frame loop).
GLint U(unsigned prog, const char* name) { return glGetUniformLocation(prog, name); }
void setVec3(GLint loc, const Vec3& v) { glUniform3f(loc, v.x, v.y, v.z); }
void setMat(GLint loc, const Mat4& m) { glUniformMatrix4fv(loc, 1, GL_FALSE, m.m); }

// Upload a vector into a growable stream buffer.
#ifdef QUARK_TRACY
static size_t g_uploadBytes = 0, g_uploads = 0, g_dynVerts = 0, g_shadowVerts = 0; // per-frame counters (profiling builds)
#endif
template <class T>
void upload(unsigned vbo, size_t& cap, const std::vector<T>& v) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    size_t bytes = v.size() * sizeof(T);
#ifdef QUARK_TRACY
    g_uploadBytes += bytes; g_uploads++;
#endif
    if (bytes > cap) {
        cap = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, cap, nullptr, GL_STREAM_DRAW);
    } else {
        glBufferData(GL_ARRAY_BUFFER, cap, nullptr, GL_STREAM_DRAW); // orphan to avoid stalls
    }
    if (bytes) glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, v.data());
}

// --- tiny PNG writer (stored deflate blocks) for screenshots ---
uint32_t crcPng(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c;
}
void be32(std::vector<uint8_t>& o, uint32_t v) { o.push_back(v >> 24); o.push_back(v >> 16); o.push_back(v >> 8); o.push_back(v); }
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crcPng(td.data(), td.size()) ^ 0xFFFFFFFFu);
}
} // namespace

unsigned Renderer::compile(const char* vs, const char* fs, std::string* err) {
    auto sh = [&](GLenum type, const char* src) -> unsigned {
        unsigned s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            if (err) *err += std::string("shader: ") + log;
            return 0;
        }
        return s;
    };
    unsigned v = sh(GL_VERTEX_SHADER, vs), f = sh(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        if (err) *err += std::string("link: ") + log;
        return 0;
    }
    return p;
}

bool Renderer::init(int shadowRes, std::string* err) {
    {   // the lightmap atlas is packed to fit the GPU's largest texture (PERF-13)
        GLint64 maxTex = 0;
        glGetInteger64v(GL_MAX_TEXTURE_SIZE, &maxTex);
        if (maxTex > 0) setLightmapMaxTextureSize((int)std::min<GLint64>(maxTex, 1 << 15));
    }
    std::string worldFS = std::string("#version 330 core\n") + kShadowFn + kWorldFS;
    std::string dynFS = std::string("#version 330 core\n") + kShadowFn + kDynFS;
    worldProg_ = compile(kWorldVS, worldFS.c_str(), err);
    dynProg_ = compile(kDynVS, dynFS.c_str(), err);
    flatProg_ = compile(kFlatVS, kFlatFS, err);
    shadowProg_ = compile(kShadowVS, kShadowFS, err);
    uiProg_ = compile(kUiVS, kUiFS, err);
    skyProg_ = compile(kSkyVS, kSkyFS, err);
    edgeProg_ = compile(kEdgeVS, kEdgeFS, err);
    if (!skyProg_ || !worldProg_ || !dynProg_ || !flatProg_ || !shadowProg_ || !uiProg_) return false;

    // VAOs
    glGenVertexArrays(1, &dynVao_);
    glGenBuffers(1, &dynVbo_);
    setupDynVao(dynVao_, dynVbo_);

    glGenVertexArrays(1, &flatVao_);
    glGenBuffers(1, &flatVbo_);
    glBindVertexArray(flatVao_);
    glBindBuffer(GL_ARRAY_BUFFER, flatVbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), (void*)12);

    glGenVertexArrays(1, &uiVao_);
    glGenBuffers(1, &uiVbo_);
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    // room for a text-heavy frame up front (512 KB): the first scoreboard or console mid-match doesn't grow it
    ui_.reserve(1 << 15);
    glBufferData(GL_ARRAY_BUFFER, uiCap_ = (size_t)(1 << 15) * sizeof(Vertex2D), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, sizeof(Vertex2D), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(Vertex2D), (void*)4);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex2D), (void*)8);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vertex2D), (void*)12);   // (scale, frac, split Q flag, 0)
    glBindVertexArray(0);

    // Font texture: the UI font's distance field (linear filtering: one atlas serves every text size)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    // (the default; Game::init then applies the ui_font setting, which may replace it: setUiFont)
    const unsigned char* atlas = g_uiFont ? g_uiFont->tex.data() : kUiAtlas;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kUiTexW, kUiTexH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    {   // the title logo: baked at its 2160p size, mipmapped so it's crisp smaller (tools/make_logo.py)
        int lw = 0, lh = 0;
        std::vector<uint8_t> px;
        if (decodeImage(kLogoPng, sizeof kLogoPng, lw, lh, px)) {
            glGenTextures(1, &logoTex_);
            glBindTexture(GL_TEXTURE_2D, logoTex_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        }
    }

    // Shadow map (depth only)
    shadowRes_ = shadowRes < 256 ? 256 : shadowRes;
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowRes_, shadowRes_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) shadowsOn_ = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Uniform locations, once; the values that never change set now (each program keeps its own)
    const Vec3 sunColor = {0.78f, 0.75f, 0.70f};
    const Vec3 skyLight = {0.42f, 0.45f, 0.52f};
    const float fog = 0.0035f;
    glUseProgram(worldProg_);
    uWorld_ = {U(worldProg_, "uViewProj"), U(worldProg_, "uShadowMat"), U(worldProg_, "uShadowOn"), U(worldProg_, "uCasterRect"),
               U(worldProg_, "uCamPos"), U(worldProg_, "uFogColor"), U(worldProg_, "uBright"), U(worldProg_, "uBaseL"),
               U(worldProg_, "uAccentL"), U(worldProg_, "uWallbangL"), U(worldProg_, "uMetalL"), U(worldProg_, "uFloorL"),
               U(worldProg_, "uMetal"), U(worldProg_, "uGlow"), U(worldProg_, "uFull"), U(worldProg_, "uFullColor"), U(worldProg_, "uOutlineW")};
    glUniform1i(U(worldProg_, "uLightmap"), 0);
    glUniform1i(U(worldProg_, "uShadow"), 1);
    glUniform1f(U(worldProg_, "uShadowTexel"), 1.0f / shadowRes_);
    setVec3(U(worldProg_, "uSunColor"), sunColor);
    setVec3(U(worldProg_, "uSkyColor"), skyLight);
    glUniform1f(U(worldProg_, "uFogDensity"), fog);
    glUseProgram(dynProg_);
    uDyn_ = {U(dynProg_, "uViewProj"), U(dynProg_, "uShadowMat"), U(dynProg_, "uShadowOn"), U(dynProg_, "uCasterRect"),
             U(dynProg_, "uCamPos"), U(dynProg_, "uFogColor"), U(dynProg_, "uBright"), U(dynProg_, "uReceiveShadow"), U(dynProg_, "uSunDir")};
    glUniform1i(U(dynProg_, "uShadow"), 1);
    glUniform1f(U(dynProg_, "uShadowTexel"), 1.0f / shadowRes_);
    setVec3(U(dynProg_, "uSunColor"), sunColor);
    setVec3(U(dynProg_, "uSkyColor"), skyLight);
    glUniform1f(U(dynProg_, "uFogDensity"), fog);
    uSky_ = {U(skyProg_, "uFwd"), U(skyProg_, "uRight"), U(skyProg_, "uUp"), U(skyProg_, "uTan"), U(skyProg_, "uSky"),
             U(skyProg_, "uSunDir"), U(skyProg_, "uBright")};
    uFlatViewProj_ = U(flatProg_, "uViewProj");
    uDynFade_ = U(dynProg_, "uFade"); uFlatFade_ = U(flatProg_, "uFade");
    uShadowViewProj_ = U(shadowProg_, "uViewProj");
    glUseProgram(uiProg_);
    uUiScreen_ = U(uiProg_, "uScreen");
    glUniform1i(U(uiProg_, "uFont"), 0);
    glUniform2f(U(uiProg_, "uFontSize"), (float)kUiTexW, (float)kUiTexH);
    glUniform1f(U(uiProg_, "uK"), kUiTpu / (2.0f * kUiSpread));
    glUniform1i(U(uiProg_, "uLogo"), 1);
    glUniform2f(U(uiProg_, "uLogoSize"), (float)kLogoW, (float)kLogoH);
    uLogoA_ = U(uiProg_, "uLogoA"); uLogoB_ = U(uiProg_, "uLogoB");
    uSplitP_ = U(uiProg_, "uSplitP"); uSplitN_ = U(uiProg_, "uSplitN"); uSplitSeam_ = U(uiProg_, "uSplitSeam");
    if (edgeProg_) {
        glUseProgram(edgeProg_);
        uEdgeRcp_ = U(edgeProg_, "uRcp");
        glUniform1i(U(edgeProg_, "uScene"), 0);
    }
    glUseProgram(0);
    uiScreen_[0] = uiScreen_[1] = edgeRcp_[0] = edgeRcp_[1] = 0;   // (set on first use)

    // the frame's vertex lists sized for a busy frame up front, so one never grows them mid-frame
    dyn_.reserve(16384); dynShadowOnly_.reserve(16384);
    dynNoCast_.reserve(2048); view_.reserve(2048);
    flat_.reserve(4096); viewFlat_.reserve(4096);
    return true;
}

void Renderer::loadWorld(const Map& map, const std::function<void(float)>& progress) {
    PreparedWorld w;
    prepareWorld(map, w, progress);
    uploadWorld(map, w);
}

void Renderer::prepareWorld(const Map& map, PreparedWorld& w, const std::function<void(float)>& progress, const std::atomic<bool>* cancel) {
    BakedWorld& bw = w.bw;
    buildWorld(map, bw, progress, cancel);
    if (bw.cancelled) return;
    std::vector<WorldChunk>& chunks_ = w.chunks;
    {   // sort the faces (6 vertices each) into 12 m columns and remember each column's bounds
        QZONE("world chunks");
        const float kChunk = 12.0f;
        size_t nf = bw.verts.size() / 6;
        std::vector<std::pair<uint64_t, uint32_t>> key(nf);
        for (size_t f = 0; f < nf; f++) {
            const WorldVertex* v = &bw.verts[f * 6];
            float cx = 0, cz = 0;
            for (int k = 0; k < 6; k++) { cx += v[k].pos[0]; cz += v[k].pos[2]; }
            int64_t gx = (int64_t)std::floor(cx / 6 / kChunk) + 100000, gz = (int64_t)std::floor(cz / 6 / kChunk) + 100000;
            bool far = v[0].material > 4.5f && v[0].material < 5.5f; // distant scenery: its own chunks (huge, rarely all in view)
            key[f] = {((uint64_t)far << 40) | ((uint64_t)gx << 20) | (uint64_t)gz, (uint32_t)f};
        }
        std::sort(key.begin(), key.end());
        std::vector<WorldVertex> sorted;
        sorted.reserve(bw.verts.size());
        chunks_.clear();
        for (size_t i = 0; i < nf; i++) {
            if (i == 0 || key[i].first != key[i - 1].first)
                chunks_.push_back({{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}, (int)sorted.size(), 0});
            WorldChunk& c = chunks_.back();
            for (int k = 0; k < 6; k++) {
                const WorldVertex& v = bw.verts[key[i].second * 6 + k];
                sorted.push_back(v);
                c.mn = vmin(c.mn, Vec3(v.pos[0], v.pos[1], v.pos[2]));
                c.mx = vmax(c.mx, Vec3(v.pos[0], v.pos[1], v.pos[2]));
            }
            c.count += 6;
        }
        bw.verts.swap(sorted);
    }
}

void Renderer::uploadWorld(const Map& map, PreparedWorld& w) {
    QZONE("world upload");
    BakedWorld& bw = w.bw;
    chunks_.swap(w.chunks);
    drawFirst_.reserve(chunks_.size());
    drawCount_.reserve(chunks_.size());
    sunDir_ = map.sunDir;
    {   // per-map palette (falls back to the stock greys)
        skyColor = map.hasPalette ? map.palSky : Vec3{0.62f, 0.66f, 0.72f};
        baseColor = map.hasPalette ? map.palBase : Vec3{0.62f, 0.62f, 0.64f};
        accentColor = map.hasPalette ? map.palAccent : Vec3{0.40f, 0.40f, 0.43f};
        metalColor = map.hasPalette ? map.palMetal : Vec3{0.30f, 0.33f, 0.37f};
        glowColor = map.hasPalette ? map.palGlow : Vec3{1.00f, 0.55f, 0.15f};
    }
    if (!worldVao_) { glGenVertexArrays(1, &worldVao_); glGenBuffers(1, &worldVbo_); glGenTextures(1, &lightmapTex_); }
    glBindVertexArray(worldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferData(GL_ARRAY_BUFFER, bw.verts.size() * sizeof(WorldVertex), bw.verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)32);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)36);
    glBindVertexArray(0);
    worldVerts_ = (int)bw.verts.size();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, lightmapTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, bw.atlasW, bw.atlasH, 0, GL_RG, GL_UNSIGNED_BYTE, bw.texels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    warmup(map);
}

// Drivers compile shader variants and pipeline states lazily, on the first draw that uses them, which
// is a 30-60 ms hitch on the first frame of a match (and on the first explosion, beam, ...). Draw one
// throwaway frame that touches every program and state while the loading screen is up, and wait for it.
void Renderer::warmup(const Map& map) {
    QZONE("renderer warmup");
    Vec3 c = map.bounds.center();
    setCamera(c + Vec3(0, 2, 0), 0, 0, 90);
    box(c, {0.3f, 0.3f, 0.3f}, 0, {0.5f, 0.5f, 0.5f}, 1.0f, true, true);   // dynamic + shadow caster
    box(c, {0.3f, 0.3f, 0.3f}, 0, {0.5f, 0.5f, 0.5f}, 1.0f, true, false);  // shadow-only caster
    translucentBox({c, c + Vec3(0.5f, 0.5f, 0.5f)}, {1, 1, 1, 0.3f});       // blended pass
    line(c, c + Vec3(0, 1, 0), 0.05f, {1, 1, 1, 0.5f});
    viewBox({0.2f, -0.2f, -0.5f}, {0.02f, 0.02f, 0.1f}, {0.5f, 0.5f, 0.5f}); // first-person pass
    drawScene(true);
    rect(0, 0, 4, 4, {1, 1, 1, 0.5f});
    text(0, 0, 2, "A", {1, 1, 1, 1});
    flush2D();
    glFinish();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::relatch(float yaw, float pitch) {
    const Vec3 p = camPos_;
    const float y0 = camYaw_, p0 = camPitch_;
    // same fov and window: the projection (and the sky's tangents) stand, only the aim's half is redone
    if (width_ / (float)std::max(1, height_) == camAspect_) aim(yaw, pitch);
    else setCamera(p, yaw, pitch, camFov_);
    // the view model's vertices were placed with translate(p) * rotY(y0) * rotX(p0): undo that, redo it with the new aim
    viewFix_ = Mat4::translate(p) * Mat4::rotY(yaw) * Mat4::rotX(pitch) * Mat4::rotX(-p0) * Mat4::rotY(-y0) * Mat4::translate(p * -1.0f);
    viewProjFix_ = viewProj_ * viewFix_;
}

void Renderer::setCamera(const Vec3& eye, float yaw, float pitch, float hfovDeg) {
    camPos_ = eye;
    camFov_ = hfovDeg;
    viewFix_ = Mat4();
    float aspect = width_ / (float)std::max(1, height_);
    camAspect_ = aspect;
    float vfov = 2.0f * std::atan(std::tan(deg2rad(hfovDeg) * 0.5f) / aspect);
    proj_ = Mat4::perspective(vfov, aspect, 0.05f, 400.0f);
    tanY_ = std::tan(vfov * 0.5f); tanX_ = tanY_ * aspect;
    aim(yaw, pitch);
    viewProjFix_ = viewProj_;   // (no relatch: no fix)
}

void Renderer::aim(float yaw, float pitch) {
    const Vec3& eye = camPos_;
    camYaw_ = yaw;
    camPitch_ = pitch;
    camFwd_ = angleForward(yaw, pitch);
    view_m_ = Mat4::lookAt(eye, eye + camFwd_, {0, 1, 0});
    camRight_ = normalize(cross(camFwd_, Vec3(0, 1, 0)));
    camUp_ = cross(camRight_, camFwd_);
    viewProj_ = proj_ * view_m_;   // (before the planes: they came from last frame's camera, so things popped in a frame late on flicks)
    {   // frustum planes (a, b, c, d, |normal|): culls dynamic objects and the world chunks
        const float* m = viewProj_.m;
        for (int i = 0; i < 3; i++)
            for (int sgn = 0; sgn < 2; sgn++) {
                float* p = frustum_[i * 2 + sgn];
                float k = sgn ? -1.0f : 1.0f;
                p[0] = m[3] + k * m[i]; p[1] = m[7] + k * m[4 + i]; p[2] = m[11] + k * m[8 + i]; p[3] = m[15] + k * m[12 + i];
                p[4] = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            }
    }
}

bool Renderer::project(const Vec3& p, float& sx, float& sy) const {
    const float* m = viewProj_.m;
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w < 0.05f) return false;
    sx = (x / w * 0.5f + 0.5f) * width_;
    sy = (0.5f - y / w * 0.5f) * height_;
    return true;
}

void Renderer::addQuad(std::vector<DynVertex>& out, const Vec3* c, const Vec3& n, const Vec3& col, float sv) {
    float w = length(c[1] - c[0]), h = length(c[3] - c[0]);
    const float eu[4] = {0, w, w, 0}, ev[4] = {0, 0, h, h};
    const size_t at = out.size();
    out.resize(at + 6);   // (written in place: six push_backs, each checking capacity, per face of every box)
    DynVertex* q = out.data() + at;
    const int corner[4] = {0, 1, 2, 5};   // corners 0 1 2 land in slots 0 1 2, corner 3 in slot 5 (triangles 0 1 2, 0 2 3)
    for (int i = 0; i < 4; i++) {
        DynVertex& v = q[corner[i]];
        memcpy(v.pos, &c[i].x, 12);
        memcpy(v.normal, &n.x, 12);
        v.color[0] = col.x; v.color[1] = col.y; v.color[2] = col.z; v.color[3] = sv;
        v.edge[0] = eu[i]; v.edge[1] = ev[i]; v.edge[2] = w; v.edge[3] = h;
        memcpy(v.outline, &outlineColor.x, 12);
        v.outline[3] = outlineWidth;
    }
    q[3] = q[0];
    q[4] = q[2];
}

void Renderer::emitBox(std::vector<DynVertex>& out, const Mat4& xf, const Vec3& h, const Vec3& color, float sunVis) {
    auto tp = [&](float x, float y, float z) {
        const float* m = xf.m;
        return Vec3(m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]);
    };
    auto tn = [&](float x, float y, float z) {
        const float* m = xf.m;
        return normalize(Vec3(m[0] * x + m[4] * y + m[8] * z, m[1] * x + m[5] * y + m[9] * z, m[2] * x + m[6] * y + m[10] * z));
    };
    // the 8 corners once (each face used to transform its own 4: 24) and 3 normals (a face's opposite is the negation)
    const Vec3 ppp = tp(h.x, h.y, h.z), ppm = tp(h.x, h.y, -h.z), pmp = tp(h.x, -h.y, h.z), pmm = tp(h.x, -h.y, -h.z);
    const Vec3 mpp = tp(-h.x, h.y, h.z), mpm = tp(-h.x, h.y, -h.z), mmp = tp(-h.x, -h.y, h.z), mmm = tp(-h.x, -h.y, -h.z);
    const Vec3 nx = tn(1, 0, 0), ny = tn(0, 1, 0), nz = tn(0, 0, 1);
    auto neg = [](const Vec3& v) { return Vec3(-v.x, -v.y, -v.z); };
    const Vec3 xPos[4] = {pmp, pmm, ppm, ppp}, xNeg[4] = {mmm, mmp, mpp, mpm};   // (each face's corners in the old order)
    const Vec3 yPos[4] = {mpp, ppp, ppm, mpm}, yNeg[4] = {mmm, pmm, pmp, mmp};
    const Vec3 zPos[4] = {mmp, pmp, ppp, mpp}, zNeg[4] = {pmm, mmm, mpm, ppm};
    addQuad(out, xPos, nx, color, sunVis);
    addQuad(out, xNeg, neg(nx), color * 0.92f, sunVis);
    addQuad(out, yPos, ny, color, sunVis);
    addQuad(out, yNeg, neg(ny), color * 0.8f, sunVis);
    addQuad(out, zPos, nz, color * 0.96f, sunVis);
    addQuad(out, zNeg, neg(nz), color * 0.96f, sunVis);
}

// Is a sphere (center, radius) at least partly inside the view frustum?
bool Renderer::inView(const Vec3& c, float r) const {
    if (cullPad > 0) r += cullPad * length(c - camPos_);
    for (int i = 0; i < 6; i++) if (frustum_[i][0] * c.x + frustum_[i][1] * c.y + frustum_[i][2] * c.z + frustum_[i][3] < -r * frustum_[i][4]) return false;
    return true;
}

void Renderer::setupDynVao(unsigned vao, unsigned vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)40);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)56);   // outline rgb + width
}

void Renderer::setStaticBoxes(const std::function<void()>& emit) {
    QZONE("static boxes");
    staticVerts_.clear();
    staticItems_.clear();
    buildingStatic_ = true;
    emit();
    buildingStatic_ = false;
    if (!staticVao_) { glGenVertexArrays(1, &staticVao_); glGenBuffers(1, &staticVbo_); setupDynVao(staticVao_, staticVbo_); }
    glBindBuffer(GL_ARRAY_BUFFER, staticVbo_);
    glBufferData(GL_ARRAY_BUFFER, staticVerts_.size() * sizeof(DynVertex), staticVerts_.data(), GL_STATIC_DRAW);
    staticCount_ = (int)staticVerts_.size();
    staticVerts_.clear();   // (its memory kept for the next rebuild: freed, every editor edit grew 30 MB again from nothing, ~16 ms on Midtown; clearStaticBoxes frees it)
}

void Renderer::box(const Vec3& center, const Vec3& half, float yaw, const Vec3& color, float sunVis, bool castShadow, bool visible) {
    if (buildingStatic_) {   // into the static batch (setStaticBoxes): every box, culled when drawn
        int first = (int)staticVerts_.size();
        emitBox(staticVerts_, Mat4::translate(center) * Mat4::rotY(yaw), half, color, sunVis);
        staticItems_.push_back({center, length(half), first, (int)staticVerts_.size() - first});
        return;
    }
    if (visible && !inView(center, length(half))) { visible = false; if (!castShadow) return; } // off screen: only its shadow can show
    Mat4 xf = Mat4::translate(center) * Mat4::rotY(yaw);
    if (visible) emitBox(dyn_, xf, half, color, sunVis);
    else if (castShadow && shadowCasters) emitBox(dynShadowOnly_, xf, half, color, sunVis);
}

void Renderer::boxPitched(const Vec3& pivot, const Vec3& offset, const Vec3& half, float yaw, float pitch, const Vec3& color, float sunVis, bool castShadow) {
    Mat4 xf = Mat4::translate(pivot) * Mat4::rotY(yaw) * Mat4::rotX(pitch) * Mat4::translate(offset);
    emitBox(castShadow ? dyn_ : dynNoCast_, xf, half, color, sunVis);   // (no shadow: small things scattered over the map, stuck props, would spread the shadow pass's caster rect over all of it)
}

void Renderer::viewBox(const Vec3& offset, const Vec3& half, const Vec3& color) {
    Mat4 xf = Mat4::translate(camPos_) * Mat4::rotY(camYaw_) * Mat4::rotX(camPitch_) * Mat4::translate(offset);
    emitBox(view_, xf, half, color, 1.0f);
}

void Renderer::viewBoxXf(const Mat4& local, const Vec3& half, const Vec3& color) {
    emitBox(view_, Mat4::translate(camPos_) * Mat4::rotY(camYaw_) * Mat4::rotX(camPitch_) * local, half, color, 1.0f);
}

// An override model (models.h): opaque triangles into `out` like a box's faces (lit, shadowed; no outline: the
// edge attribute sits mid-face), translucent ones into `flatOut` (a flat layer; none: left out). Colours times tint[slot].
void Renderer::emitMesh(std::vector<DynVertex>& out, const ModelMesh& m, const Mat4& xf, const Vec3* tint, float sunVis, std::vector<FlatVertex>* flatOut) {
    const float* x = xf.m;
    // (grow geometrically: reserving exactly what this mesh needs made every mesh past the high-water mark copy the
    // whole list again)
    if (out.size() + m.opaque.size() > out.capacity()) out.reserve(std::max(out.size() + m.opaque.size(), out.capacity() * 2));
    for (const ModelMesh::V& v : m.opaque) {
        DynVertex d;
        const float* p = v.pos; const float* n = v.nrm;
        d.pos[0] = x[0] * p[0] + x[4] * p[1] + x[8] * p[2] + x[12];
        d.pos[1] = x[1] * p[0] + x[5] * p[1] + x[9] * p[2] + x[13];
        d.pos[2] = x[2] * p[0] + x[6] * p[1] + x[10] * p[2] + x[14];
        Vec3 nn = normalize(Vec3(x[0] * n[0] + x[4] * n[1] + x[8] * n[2], x[1] * n[0] + x[5] * n[1] + x[9] * n[2], x[2] * n[0] + x[6] * n[1] + x[10] * n[2]));
        d.normal[0] = nn.x; d.normal[1] = nn.y; d.normal[2] = nn.z;
        const Vec3& t = tint[v.slot < TINT_COUNT ? v.slot : 0];
        d.color[0] = v.col[0] * t.x; d.color[1] = v.col[1] * t.y; d.color[2] = v.col[2] * t.z; d.color[3] = sunVis;
        d.edge[0] = d.edge[1] = 0.5f; d.edge[2] = d.edge[3] = 1.0f;
        memcpy(d.outline, &outlineColor.x, 12);
        d.outline[3] = 0.0f;   // (no edge: was left unset, so the shader read whatever was on the stack as the width)
        out.push_back(d);
    }
    if (!flatOut) return;
    for (const ModelMesh::V& v : m.translucent) {
        FlatVertex f;
        const float* p = v.pos;
        f.pos[0] = x[0] * p[0] + x[4] * p[1] + x[8] * p[2] + x[12];
        f.pos[1] = x[1] * p[0] + x[5] * p[1] + x[9] * p[2] + x[13];
        f.pos[2] = x[2] * p[0] + x[6] * p[1] + x[10] * p[2] + x[14];
        const Vec3& t = tint[v.slot < TINT_COUNT ? v.slot : 0];
        f.color[0] = v.col[0] * t.x; f.color[1] = v.col[1] * t.y; f.color[2] = v.col[2] * t.z; f.color[3] = v.col[3];
        flatOut->push_back(f);
    }
}

void Renderer::mesh(const ModelMesh& m, const Mat4& xf, const Vec3* tint, float sunVis, bool castShadow, bool visible) {
    const float* x = xf.m;
    Vec3 c = {x[0] * m.center.x + x[4] * m.center.y + x[8] * m.center.z + x[12], x[1] * m.center.x + x[5] * m.center.y + x[9] * m.center.z + x[13],
              x[2] * m.center.x + x[6] * m.center.y + x[10] * m.center.z + x[14]};
    float sc = std::max({length(Vec3(x[0], x[1], x[2])), length(Vec3(x[4], x[5], x[6])), length(Vec3(x[8], x[9], x[10]))});
    if (visible && !inView(c, m.radius * sc)) { visible = false; if (!castShadow) return; } // off screen: only its shadow can show
    if (visible) emitMesh(dyn_, m, xf, tint, sunVis, &flat_);
    else if (castShadow && shadowCasters) emitMesh(dynShadowOnly_, m, xf, tint, sunVis, nullptr);
}

void Renderer::viewMesh(const ModelMesh& m, const Vec3& offset, float scale, const Vec3* tint) {
    Mat4 s;
    s.m[0] = s.m[5] = s.m[10] = scale;
    Mat4 xf = Mat4::translate(camPos_) * Mat4::rotY(camYaw_) * Mat4::rotX(camPitch_) * Mat4::translate(offset) * s;
    emitMesh(view_, m, xf, tint, 1.0f, &viewFlat_);   // (translucent parts: drawn over the view model, in its pass)
}

void Renderer::viewMeshXf(const ModelMesh& m, const Mat4& local, const Vec3* tint) {
    emitMesh(view_, m, Mat4::translate(camPos_) * Mat4::rotY(camYaw_) * Mat4::rotX(camPitch_) * local, tint, 1.0f, &viewFlat_);
}

void Renderer::overlayBox(const Vec3& center, const Vec3& half, const Vec3& color) {
    emitBox(view_, Mat4::translate(center), half, color, 1.0f);
}

Vec3 Renderer::viewPoint(const Vec3& o) const {
    float cp = std::cos(camPitch_), sp = std::sin(camPitch_), cy = std::cos(camYaw_), sy = std::sin(camYaw_);
    Vec3 p = {o.x, cp * o.y - sp * o.z, sp * o.y + cp * o.z};          // rotX(pitch)
    return camPos_ + Vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z); // rotY(yaw)
}

void Renderer::translucentBox(const AABB& b, const Color& c) {
    Vec3 p[8];
    for (int i = 0; i < 8; i++) p[i] = {i & 1 ? b.mx.x : b.mn.x, i & 2 ? b.mx.y : b.mn.y, i & 4 ? b.mx.z : b.mn.z};
    const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (auto& f : faces) {
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int i : idx) {
            FlatVertex v;
            memcpy(v.pos, &p[f[i]].x, 12);
            v.color[0] = c.r; v.color[1] = c.g; v.color[2] = c.b; v.color[3] = c.a;
            flat_.push_back(v);
        }
    }
}

void Renderer::line(const Vec3& a, const Vec3& b, float width, const Color& c) {
    Vec3 d = normalize(b - a);
    Vec3 toCam = normalize(camPos_ - (a + b) * 0.5f);
    Vec3 side = normalize(cross(d, toCam)) * (width * 0.5f);
    Vec3 q[4] = {a - side, b - side, b + side, a + side};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i : idx) {
        FlatVertex v;
        memcpy(v.pos, &q[i].x, 12);
        v.color[0] = c.r; v.color[1] = c.g; v.color[2] = c.b; v.color[3] = c.a;
        flat_.push_back(v);
    }
}

void Renderer::prepareScene(bool shadows) {
    QZONE("prepareScene");
    QPLOT("dyn verts", dyn_.size());
#ifdef QUARK_TRACY
    g_dynVerts += dyn_.size() + view_.size() + flat_.size();
    g_shadowVerts += dyn_.size() + dynShadowOnly_.size();
#endif
    QPLOT("ui verts", ui_.size());
    const bool useShadows = shadows && shadowsOn_;
    prep_ = true;
    prepShadows_ = useShadows;
    // The frame's dynamic geometry, one upload laid out [shadow only | lit, casting | lit, not casting | view model]:
    // the shadow pass draws the first two, the dynamic pass the middle two, the view pass the last (it used to copy the
    // lists together and upload the dynamic ones twice, orphaning the buffer three times a frame).
    const size_t nShadow = useShadows ? dynShadowOnly_.size() : 0, nDyn = dyn_.size(), nNoCast = dynNoCast_.size(), nView = view_.size();
    {
        const size_t total = (nShadow + nDyn + nNoCast + nView) * sizeof(DynVertex);
        glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
#ifdef QUARK_TRACY
        g_uploadBytes += total; g_uploads++;
#endif
        if (total > dynCap_) dynCap_ = total * 2;
        glBufferData(GL_ARRAY_BUFFER, dynCap_, nullptr, GL_STREAM_DRAW);   // (orphan: no stall on last frame's draws)
        size_t at = 0;
        auto put = [&](const std::vector<DynVertex>& v, size_t n) {
            if (n) glBufferSubData(GL_ARRAY_BUFFER, at, n * sizeof(DynVertex), v.data());
            at += n * sizeof(DynVertex);
        };
        put(dynShadowOnly_, nShadow); put(dyn_, nDyn); put(dynNoCast_, nNoCast); put(view_, nView);
    }
    // Translucent geometry, one upload: the world's, then the view model's own translucent parts (an override model's
    // glass, glow), drawn in its pass.
    const size_t nFlat = flat_.size(), nViewFlat = viewFlat_.size();
    if (nViewFlat) flat_.insert(flat_.end(), viewFlat_.begin(), viewFlat_.end());   // (nothing to do for a model without any)
    if (!flat_.empty()) upload(flatVbo_, flatCap_, flat_);
    // Shadow pass: only dynamic objects (static shadows are baked). (Its focus follows the aim, snapped to texels: the
    // aim from before a late latch is as good.)
    Mat4& shadowMat = shadowMat_;
    shadowMat = Mat4();
    if (useShadows) {
        const float R = 32.0f;
        Vec3 up = std::fabs(sunDir_.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
        Mat4 lv = Mat4::lookAt(sunDir_ * 100.0f, {0, 0, 0}, up);
        Vec3 focus = camPos_ + angleForward(camYaw_, 0) * (R * 0.5f);
        const float* m = lv.m;
        float cx = m[0] * focus.x + m[4] * focus.y + m[8] * focus.z + m[12];
        float cy = m[1] * focus.x + m[5] * focus.y + m[9] * focus.z + m[13];
        float cz = m[2] * focus.x + m[6] * focus.y + m[10] * focus.z + m[14];
        float texel = 2 * R / shadowRes_;
        cx = std::floor(cx / texel) * texel;
        cy = std::floor(cy / texel) * texel;
        shadowMat = Mat4::ortho(cx - R, cx + R, cy - R, cy + R, -cz - 120.0f, -cz + 120.0f) * lv;

        QZONE("shadow pass");
        // Where in the shadow map the casters land (plus a 2 texel margin for the filter): every other pixel of the
        // world and of players skips the 4 shadow taps. Per triangle, clipped to the map: one that falls wholly outside
        // the 64 m box draws nothing (a far player used to stretch the rect over the whole map, and with it the taps).
        float x0 = 1, y0 = 1, x1 = 0, y1 = 0;
        const float* sm = shadowMat.m;
        auto cover = [&](const std::vector<DynVertex>& vs) {
            for (size_t i = 0; i + 2 < vs.size(); i += 3) {
                float tx[3], ty[3];
                for (int k = 0; k < 3; k++) {
                    const float* p = vs[i + k].pos;
                    tx[k] = (sm[0] * p[0] + sm[4] * p[1] + sm[8] * p[2] + sm[12]) * 0.5f + 0.5f;
                    ty[k] = (sm[1] * p[0] + sm[5] * p[1] + sm[9] * p[2] + sm[13]) * 0.5f + 0.5f;
                }
                float ax = std::min({tx[0], tx[1], tx[2]}), bx = std::max({tx[0], tx[1], tx[2]});
                float ay = std::min({ty[0], ty[1], ty[2]}), by = std::max({ty[0], ty[1], ty[2]});
                if (bx < 0 || ax > 1 || by < 0 || ay > 1) continue;
                x0 = std::min(x0, std::max(ax, 0.0f)); x1 = std::max(x1, std::min(bx, 1.0f));
                y0 = std::min(y0, std::max(ay, 0.0f)); y1 = std::max(y1, std::min(by, 1.0f));
            }
        };
        cover(dynShadowOnly_);
        cover(dyn_);
        float margin = 2.0f / shadowRes_;
        casterRect_[0] = x0 - margin; casterRect_[1] = y0 - margin; casterRect_[2] = x1 + margin; casterRect_[3] = y1 + margin;
        // nothing lands in the map: no pass (the empty rect means no pixel reads it, so last frame's depth can stay)
        if (x0 <= x1 && y0 <= y1) {
            glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
            glViewport(0, 0, shadowRes_, shadowRes_);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDisable(GL_CULL_FACE);
            glUseProgram(shadowProg_);
            setMat(uShadowViewProj_, shadowMat);
            glBindVertexArray(dynVao_);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(nShadow + nDyn));   // (shadow-only, then the lit ones: both cast)
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    prepN_[0] = nShadow; prepN_[1] = nDyn; prepN_[2] = nNoCast; prepN_[3] = nView; prepN_[4] = nFlat; prepN_[5] = nViewFlat;

    // The per-frame uniforms that don't depend on the aim (the view's matrices and the sky's vectors: drawScene)
    clear_[0] = std::pow(skyColor.x, 1 / 2.2f); clear_[1] = std::pow(skyColor.y, 1 / 2.2f); clear_[2] = std::pow(skyColor.z, 1 / 2.2f);
    glUseProgram(worldProg_);
    setMat(uWorld_.shadowMat, shadowMat);
    glUniform1i(uWorld_.shadowOn, useShadows ? 1 : 0);
    glUniform4fv(uWorld_.casterRect, 1, casterRect_);
    auto lin = [](const Vec3& c, float k) { return Vec3(std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f)) * k; };
    setVec3(uWorld_.baseL, lin(baseColor, 1.5f));
    setVec3(uWorld_.accentL, lin(accentColor, 1.5f));
    setVec3(uWorld_.wallbangL, lin(baseColor * 0.42f, 1.5f));
    setVec3(uWorld_.metalL, lin(metalColor, 1.5f));
    setVec3(uWorld_.floorL, lin(metalColor, 2.0f));
    setVec3(uWorld_.metal, metalColor);
    setVec3(uWorld_.glow, glowColor);
    setVec3(uWorld_.fogColor, skyColor);
    setVec3(uWorld_.camPos, camPos_);   // (a late latch turns the view, it doesn't move it)
    glUniform1f(uWorld_.bright, brightness);
    glUniform1f(uWorld_.full, fullbright ? 1.0f : 0.0f);
    setVec3(uWorld_.fullColor, fullbrightColor);
    glUniform1f(uWorld_.outlineW, worldOutline);
    glUseProgram(skyProg_);
    setVec3(uSky_.sky, skyColor); setVec3(uSky_.sunDir, sunDir_);
    glUniform1f(uSky_.bright, brightness);
    glUseProgram(dynProg_);
    setMat(uDyn_.shadowMat, shadowMat);
    glUniform1i(uDyn_.shadowOn, useShadows ? 1 : 0);
    glUniform4fv(uDyn_.casterRect, 1, casterRect_);
    setVec3(uDyn_.sunDir, sunDir_);
    setVec3(uDyn_.fogColor, skyColor);
    setVec3(uDyn_.camPos, camPos_);
    glUniform1f(uDyn_.bright, brightness);
}

void Renderer::drawScene(bool shadows) {
    QZONE("drawScene");
    if (!prep_) prepareScene(shadows);
    prep_ = false;
    const size_t nShadow = prepN_[0], nDyn = prepN_[1], nNoCast = prepN_[2], nView = prepN_[3], nFlat = prepN_[4], nViewFlat = prepN_[5];

    ensureMsaaTarget();
    if (msaaActive_) glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    glViewport(0, 0, width_, height_);
    glClearColor(clear_[0], clear_[1], clear_[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);

    // (only the view's own uniforms here: init set the fixed ones, prepareScene the rest of the frame's)
    if (worldVerts_) {
        QZONE("world pass");
        glUseProgram(worldProg_);
        setMat(uWorld_.viewProj, viewProj_);
        glBindTexture(GL_TEXTURE_2D, lightmapTex_);
        glBindVertexArray(worldVao_);
        drawWorldChunks();
        // sky after the world: depth LEQUAL at z = 1 only passes where nothing was drawn
        glUseProgram(skyProg_);
        setVec3(uSky_.fwd, camFwd_); setVec3(uSky_.right, camRight_); setVec3(uSky_.up, camUp_);
        glUniform2f(uSky_.tan, tanX_, tanY_);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_TRUE);
    }

    auto drawDyn = [&](size_t first, size_t count, bool receive, const Mat4& vp) {
        if (!count && !staticOn_) return;
        glUseProgram(dynProg_);
        setMat(uDyn_.viewProj, vp);
        glUniform1i(uDyn_.receive, receive ? 1 : 0);
        glBindVertexArray(dynVao_);
        if (count) glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
    };
    { QZONE("dynamic pass"); drawDyn(nShadow, nDyn + nNoCast, true, viewProj_); }   // (lit and shadowed; the no-cast ones cast none)
    if (staticOn_) {   // the static batch (the editor's map): drawDyn above set the program up; the boxes in view, one multi-draw
        QZONE("static boxes pass");
        staticOn_ = false;
        staticFirst_.clear();
        staticCnt_.clear();
        for (const StaticItem& it : staticItems_) {
            if (!inView(it.c, it.r)) continue;
            if (!staticFirst_.empty() && staticFirst_.back() + staticCnt_.back() == it.first) staticCnt_.back() += it.count;
            else { staticFirst_.push_back(it.first); staticCnt_.push_back(it.count); }
        }
        if (!staticFirst_.empty()) {
            glBindVertexArray(staticVao_);
            glMultiDrawArrays(GL_TRIANGLES, staticFirst_.data(), staticCnt_.data(), (GLsizei)staticFirst_.size());
        }
    }

    // Translucent geometry (blended, no depth writes): the world's, then in the view pass the view model's own
    // translucent parts, which share the one upload (prepareScene), after the world's.
    auto drawFlat = [&](size_t first, size_t count, const Mat4& vp) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glUseProgram(flatProg_);
        setMat(uFlatViewProj_, vp);
        glBindVertexArray(flatVao_);
        glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
    };
    if (nFlat) drawFlat(0, nFlat, viewProj_);

    if ((nView || nViewFlat) && viewOpacity > 0.005f) {   // (WEAPON MODEL OPACITY 0: not drawn at all)
        glClear(GL_DEPTH_BUFFER_BIT);
        if (viewOpacity < 0.995f) {
            // see-through: its depth first (no colour), then its colour blended over only its nearest faces, so it
            // fades as one solid thing (the parts behind its front faces don't show through). Twice the view's few
            // hundred triangles; nothing extra at full opacity.
            const float fade = 1.0f - viewOpacity;
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            drawDyn(nShadow + nDyn + nNoCast, nView, false, viewProjFix_);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glUniform1f(uDynFade_, fade);   // (drawDyn's program, still bound)
            drawDyn(nShadow + nDyn + nNoCast, nView, false, viewProjFix_);
            glUniform1f(uDynFade_, 0);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            if (nViewFlat) {
                glUseProgram(flatProg_);
                glUniform1f(uFlatFade_, fade);
                drawFlat(nFlat, nViewFlat, viewProjFix_);
                glUniform1f(uFlatFade_, 0);
            }
        } else {
            drawDyn(nShadow + nDyn + nNoCast, nView, false, viewProjFix_);   // (viewFix_: built for the aim before relatch)
            if (nViewFlat) drawFlat(nFlat, nViewFlat, viewProjFix_);   // (after its opaque parts: hidden behind them, blended over the rest)
        }
    }
    if (msaaActive_ >= 2) { // resolve the multisampled scene into the window; the UI draws on top un-multisampled
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (msaaActive_ == 1) { // edges: one fullscreen pass from the scene texture into the window
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(edgeProg_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, edgeTex_);
        const float rx = 1.0f / width_, ry = 1.0f / height_;
        if (rx != edgeRcp_[0] || ry != edgeRcp_[1]) { glUniform2f(uEdgeRcp_, rx, ry); edgeRcp_[0] = rx; edgeRcp_[1] = ry; }   // (only when the size changes)
        glBindVertexArray(worldVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
    }
    dyn_.clear();
    dynShadowOnly_.clear();
    dynNoCast_.clear();
    view_.clear();
    flat_.clear();
    viewFlat_.clear();
    viewOpacity = 1.0f;   // (this frame's: the game sets it again with its view model; the editor's handles stay solid)
}

// std::round (half away from zero) without the library call, bit for bit the same whole number: the 2D layer rounds
// ~12 values a glyph, and the calls were most of its cost. (2^23 and up, inf, NaN: already whole, returned as they are.)
static inline float uiRound(float v) {
    if (!(std::fabs(v) < 8388608.0f)) return v;
    int i = (int)v;
    float f = v - (float)i;   // (exact below 2^23)
    i += (f >= 0.5f) - (f <= -0.5f);
    return (float)i;
}
static inline uint8_t u8c(float v) { float x = clampf(v, 0, 1) * 255.0f; int i = (int)x; i += (x - (float)i) >= 0.5f; return (uint8_t)i; }   // (= lround)
static inline int16_t px(float v) { return (int16_t)clampf(uiRound(v), -32768, 32767); }
// font atlas texel coords as the vertex stores them, (texel + 1) * 32: whole texels, so plain integers
static inline uint16_t u16uv(int texel) { return (uint16_t)((texel + 1) * 32); }
struct Rgba8 { uint8_t v[4]; };
static inline Rgba8 rgba8(const Color& c) { return {{u8c(c.r), u8c(c.g), u8c(c.b), u8c(c.a)}}; }   // (once per rect / text)

// one quad (2 triangles) into the UI batch. Every glyph of every text is one: each distinct value is converted once
// (the corners share 2 x and 2 y; the colour comes packed, the texel coords as integers), written in place.
static void uiQuad(std::vector<Vertex2D>& out, float x0, float y0, float x1, float y1, uint16_t U0, uint16_t V0, uint16_t U1, uint16_t V1,
                   const Rgba8& c, uint8_t scaleInt, uint8_t scaleFrac) {
    const int16_t X0 = px(x0), X1 = px(x1), Y0 = px(y0), Y1 = px(y1);
    const size_t n = out.size();
    out.resize(n + 6);
    Vertex2D* d = out.data() + n;
    auto put = [&](Vertex2D& v, int16_t x, int16_t y, uint16_t u, uint16_t w) {
        v.pos[0] = x; v.pos[1] = y; v.uv[0] = u; v.uv[1] = w;
        memcpy(v.color, c.v, 4);
        v.scaleInt = scaleInt; v.scaleFrac = scaleFrac; v.pad[0] = v.pad[1] = 0;
    };
    // corners 0 (x0,y0) 1 (x1,y0) 2 (x1,y1) 3 (x0,y1), as triangles 0 1 2 and 0 2 3
    put(d[0], X0, Y0, U0, V0); put(d[1], X1, Y0, U1, V0); put(d[2], X1, Y1, U1, V1);
    d[3] = d[0]; d[4] = d[2]; put(d[5], X0, Y1, U0, V1);
}

void Renderer::rect(float x, float y, float w, float h, const Color& c) {
    // a texel inside the solid block (cell 0.5, 6.5), as stored: (texel + 1) * 32
    constexpr uint16_t u = (uint16_t)((kUiCell * 0.5f + 1) * 32.0f), v = (uint16_t)((kUiCell * 6.5f + 1) * 32.0f);
    uiQuad(ui_, x, y, x + w, y + h, u, v, u, v, rgba8(c), 0, 0);
}

void Renderer::rectGradient(float x, float y, float w, float h, const Color& tl, const Color& tr, const Color& br, const Color& bl) {
    constexpr uint16_t u = (uint16_t)((kUiCell * 0.5f + 1) * 32.0f), v = (uint16_t)((kUiCell * 6.5f + 1) * 32.0f);
    const size_t n = ui_.size();
    uiQuad(ui_, x, y, x + w, y + h, u, v, u, v, rgba8(tl), 0, 0);
    // corners 0 tl, 1 tr, 2 br, 5 bl (3 = 0, 4 = 2): their own colours
    auto col = [&](Vertex2D& d, const Color& c) { d.color[0] = u8c(c.r); d.color[1] = u8c(c.g); d.color[2] = u8c(c.b); d.color[3] = u8c(c.a); };
    Vertex2D* d = ui_.data() + n;
    col(d[1], tr); col(d[2], br); col(d[4], br); col(d[5], bl);
}

// The logo's size: the em of the game's text at ts (7.5 units) times Rubik's cap height matched to the old UI font's
// (0.66 / 0.70), as it was designed (ui-redesign-spec, "Title logo"); its baseline 5.7 units below the line top.
static constexpr float kLogoEmK = 0.66f / 0.70f, kLogoTilt = 0.16f, kLogoSeam = 12.0f / 844.0f;

void Renderer::logo(float cx, float y, float ts, const Color& friendly, const Color& enemy) {
    if (ts <= 0) return;
    logoA_ = friendly; logoB_ = enemy;
    if (!g_uiFont && logoTex_) {   // Rubik: the baked logo, one quad (its masks tinted in the shader)
        const float k = 7.5f * ts * kLogoEmK / kLogoEm;
        // whole pixels: the pen origin on the pixel grid, the quad its exact size (no shimmer as it's scaled)
        const float x0 = std::round(cx - kLogoAdv * k * 0.5f - kLogoOX * k), y0 = std::round(y + 5.7f * ts - kLogoOY * k);
        uiQuad(ui_, x0, y0, x0 + kLogoW * k, y0 + kLogoH * k, u16uv(0), u16uv(0), u16uv(kLogoW), u16uv(kLogoH), rgba8({1, 1, 1, friendly.a}), 255, 255);
        logoDrawn_ = true;
        return;
    }
    // another UI font: the word in it, as big (the same em), its own outline; the Q split at the same slant through the
    // middle of its advance and its ink, a black seam on the line (the text shader, for that glyph's quad only)
    const TextMetrics& m = uiMetrics();
    const float sc = ts * kLogoEmK, sk = sc * m.fitScale;
    const float w = textWidth("QUARK", sc), x0 = std::round(cx - w * 0.5f);
    const float top = y + 5.7f * ts - (6.0f * m.fitScale + m.fitShift) * sc;   // (its baseline on the logo's)
    const size_t n0 = ui_.size();
    text(x0, top, sc, "Q", friendly);
    for (size_t i = n0; i < ui_.size(); i++) ui_[i].pad[0] = 1;   // (the split Q)
    text(x0 + w - textWidth("UARK", sc), top, sc, "UARK", friendly);
    const int q = 'Q' - 32;
    const GlyphInk& ink = m.ink[q];
    const float qx = x0 + m.adv[q] * sk * 0.5f;
    const float qy = std::round(top) + (ink.empty() ? 3.0f * m.fitScale : (ink.y0 + ink.y1) * 0.5f * m.fitScale) * sc + m.fitShift * sc;
    // (the line x = qx - tilt (y - qy) in screen pixels, y down; the shader gets it in window pixels, y up)
    const float nl = std::sqrt(1 + kLogoTilt * kLogoTilt);
    splitP_[0] = qx; splitP_[1] = (float)height_ - qy;
    splitN_[0] = 1 / nl; splitN_[1] = -kLogoTilt / nl;
    splitSeam_ = 0.5f * kLogoSeam * 7.5f * ts * kLogoEmK;
    splitDrawn_ = true;
}

static inline float glyphAdv(int prev, int g) { return g_uiAdv[prev] + g_uiKern[prev * 95 + g] * (1.0f / 16.0f); }

void Renderer::text(float x, float y, float s, const std::string& str, const Color& c, float level) {
    if (s <= 0) return;
    if (s < minScale_) s = minScale_;
    // a font whose ink would leave the line box is drawn k times smaller, moved by its fit shift (k = 1: most fonts)
    const float k = uiMetrics().fitScale, sk = s * k;
    float cx = x, cy = uiRound(y);
    float sc = clampf(sk, 1.0f / 256, 255);
    uint8_t si = (uint8_t)sc, sf = (uint8_t)std::lround((sc - si) * 256 > 255 ? 255 : (sc - si) * 256);
    const float pad = kUiPad * sk, box = 8.0f * sk, top = cy + uiMetrics().fitShift * s, o = outlineUnits(sk);
    const TextMetrics& m = uiMetrics();
    const Rgba8 col = rgba8(c);
    int prev = -1;
    for (char ch : str) {
        int g = glyphIndex(ch);
        if (prev >= 0) cx += (glyphAdv(prev, g) - m.closeUp(prev, g, level, o)) * sk;   // (closed up: Ui's fitting)
        prev = g;
        if (g == 0) continue;
        // the glyph box plus its padding (the outline and anti-aliasing reach into it); snapped to whole pixels
        float qx = uiRound(cx - kUiOrigin * sk) - pad, qy = top - pad;
        int tx = (g % 16) * kUiCell, ty = (g / 16) * kUiCell;
        uiQuad(ui_, qx, qy, qx + box + 2 * pad, qy + box + 2 * pad, u16uv(tx), u16uv(ty), u16uv(tx + kUiCell), u16uv(ty + kUiCell), col, si, sf);
    }
}

// Monospaced (console): every glyph gets the same advance (a little over a digit's), centred in it, no kerning;
// glyphs wider than that (M, W, m, w) are narrowed to fit so columns line up and nothing overlaps.
static constexpr float kMonoAdv = 5.3f;

void Renderer::textMono(float x, float y, float s, const std::string& str, const Color& c) {
    if (s <= 0) return;
    if (s < minScale_) s = minScale_;
    const float k = uiMetrics().fitScale;   // (the line-box fit, as text())
    float sc = clampf(s * k, 1.0f / 256, 255);
    uint8_t si = (uint8_t)sc, sf = (uint8_t)std::lround((sc - si) * 256 > 255 ? 255 : (sc - si) * 256);
    const float pad = kUiPad * s * k, cy = uiRound(y) + uiMetrics().fitShift * s;
    const Rgba8 col = rgba8(c);
    for (size_t i = 0; i < str.size(); i++) {
        int g = glyphIndex(str[i]);
        if (g == 0) continue;
        float sx = s * k * std::min(1.0f, kMonoAdv / (g_uiAdv[g] * k)); // horizontal scale
        float cx = x + i * kMonoAdv * s + (kMonoAdv * s - g_uiAdv[g] * sx) * 0.5f;
        float qx = uiRound(cx - kUiOrigin * sx) - kUiPad * sx, qy = cy - pad;
        int tx = (g % 16) * kUiCell, ty = (g / 16) * kUiCell;
        uiQuad(ui_, qx, qy, qx + (8.0f + 2 * kUiPad) * sx, qy + 8.0f * s * k + 2 * pad, u16uv(tx), u16uv(ty), u16uv(tx + kUiCell), u16uv(ty + kUiCell), col, si, sf);
    }
}

float Renderer::textWidthMono(const std::string& str, float scale) const { return str.size() * kMonoAdv * std::max(scale, minScale_); }

float Renderer::textWidth(const std::string& str, float scale, float level) const {
    const float sk = std::max(scale, minScale_) * uiMetrics().fitScale;
    return textUnits(uiMetrics(), str, level, sk) * sk;
}

TextFit Renderer::fitText(const std::string& str, float scale, float maxW) const { return ::fitText(uiMetrics(), str, std::max(scale, minScale_), maxW); }

std::string Renderer::textClip(const std::string& str, float scale, float maxW) const {
    if (textWidth(str, scale) <= maxW) return str;
    size_t n = textFit(str, scale, maxW - textWidth("...", scale));
    return str.substr(0, n) + "...";
}

size_t Renderer::textFit(const std::string& str, float scale, float maxW) const {
    float w = 0;
    int prev = -1;
    for (size_t i = 0; i < str.size(); i++) {
        int g = glyphIndex(str[i]);
        float nw = w + (prev >= 0 ? glyphAdv(prev, g) - g_uiAdv[prev] : 0) + g_uiAdv[g];
        if (nw * std::max(scale, minScale_) * uiMetrics().fitScale > maxW) return i;
        w = nw; prev = g;
    }
    return str.size();
}

FontAtlasLayout Renderer::uiFontLayout() { return {kUiTexW, kUiTexH, kUiCell, kUiTpu, kUiPad, kUiOrigin, kUiSpread, kUiCapHeight}; }

void Renderer::setUiFont(std::unique_ptr<FontAtlas> font) {
    if (font && font->tex.size() != (size_t)kUiTexW * kUiTexH) font.reset();
    if (!font && !g_uiFont) return;   // (the default already)
    g_uiFont = std::move(font);
    g_uiAdv = g_uiFont ? g_uiFont->adv : kUiAdv;
    g_uiKern = g_uiFont ? g_uiFont->kern : kUiKern;
    minScale_ = g_uiFont && g_uiFont->minScale > 0 ? g_uiFont->minScale : kMinTextScale;
    {   // its ink, and the scale that keeps it in the line box (once per pick)
        TextMetrics& m = uiMetrics();
        m.adv = g_uiAdv; m.kern = g_uiKern; m.minScale = minScale_;
        measureInk(g_uiFont ? g_uiFont->tex.data() : kUiAtlas, {kUiTexW, kUiTexH, kUiCell, kUiTpu, kUiPad, kUiOrigin, kUiSpread}, m.ink);
        LineFit lf = lineFit(m.ink, defaultLineBox());
        m.fitScale = lf.scale; m.fitShift = lf.shift;
        fontGen_++;
    }
    if (!fontTex_) return;   // (before init: init uploads it)
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kUiTexW, kUiTexH, GL_RED, GL_UNSIGNED_BYTE, g_uiFont ? g_uiFont->tex.data() : kUiAtlas);
}

void Renderer::prepare2D() {
    upload(uiVbo_, uiCap_, ui_);
    uiUploaded_ = ui_.size();
}

void Renderer::flush2D() {
    if (ui_.empty()) { uiUploaded_ = 0; return; }
    QPLOT("ui verts", ui_.size());
#ifdef QUARK_TRACY
    static size_t uiTotal = 0; uiTotal += ui_.size();
#endif
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (uiUploaded_ != ui_.size()) upload(uiVbo_, uiCap_, ui_);   // (prepare2D did it, unless more was added since)
    uiUploaded_ = 0;
    glUseProgram(uiProg_);
    if ((float)width_ != uiScreen_[0] || (float)height_ != uiScreen_[1]) {   // (the rest are fixed: init)
        uiScreen_[0] = (float)width_; uiScreen_[1] = (float)height_;
        glUniform2f(uUiScreen_, uiScreen_[0], uiScreen_[1]);
    }
    if (logoDrawn_ || splitDrawn_) {   // (only a frame with the title sets its colours)
        glUniform3f(uLogoA_, logoA_.r, logoA_.g, logoA_.b);
        glUniform3f(uLogoB_, logoB_.r, logoB_.g, logoB_.b);
    }
    if (logoDrawn_) {   // (...and binds its texture; uLogo / uLogoSize are fixed: init)
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, logoTex_);
        logoDrawn_ = false;
    }
    if (splitDrawn_) {   // (the title in another font: its Q's split line)
        glUniform2f(uSplitP_, splitP_[0], splitP_[1]);
        glUniform2f(uSplitN_, splitN_[0], splitN_[1]);
        glUniform1f(uSplitSeam_, splitSeam_);
        splitDrawn_ = false;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glBindVertexArray(uiVao_);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ui_.size());
    glDisable(GL_BLEND);
    ui_.clear();
#ifdef QUARK_TRACY
    // profiling builds: per-frame averages to stderr every 5 s (and to Tracy plots)
    static double t0 = timeNow(); static int frames = 0; static size_t bytes = 0, ups = 0, dyn = 0, shadow = 0;
    QPLOT("upload bytes", g_uploadBytes);
    frames++; bytes += g_uploadBytes; ups += g_uploads; dyn += g_dynVerts; shadow += g_shadowVerts;
    g_uploadBytes = g_uploads = g_dynVerts = g_shadowVerts = 0;
    if (timeNow() - t0 > 5) {
        fprintf(stderr, "[stats] %d frames, per frame: %.1f KB uploaded in %.1f uploads, %zu ui verts, %zu dynamic verts, %zu shadow verts\n",
                frames, bytes / 1024.0 / frames, (double)ups / frames, uiTotal / frames, dyn / frames, shadow / frames);
        t0 = timeNow(); frames = 0; bytes = ups = dyn = shadow = uiTotal = 0;
    }
#endif
}

bool Renderer::screenshot(const std::string& path) {
    std::vector<uint8_t> px((size_t)width_ * height_ * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(width_ * 3 + 1) * height_);
    for (int y = height_ - 1; y >= 0; y--) {
        raw.push_back(0);
        raw.insert(raw.end(), px.begin() + (size_t)y * width_ * 3, px.begin() + (size_t)(y + 1) * width_ * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 255); z.push_back(n >> 8);
        z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, width_); be32(ihdr, height_);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    std::ofstream f(utf8Path(path), std::ios::binary);
    f.write((const char*)out.data(), out.size());
    return (bool)f;
}

// World pass: chunks outside the view frustum are skipped; consecutive visible chunks merge into one range.
void Renderer::drawWorldChunks() {
    drawFirst_.clear();
    drawCount_.clear();
    for (const WorldChunk& c : chunks_) {
        bool in = true;
        for (int i = 0; i < 6 && in; i++) { // the box corner farthest along the plane normal must be inside
            const float* p = frustum_[i];   // (setCamera's planes)
            float x = p[0] >= 0 ? c.mx.x : c.mn.x, y = p[1] >= 0 ? c.mx.y : c.mn.y, z = p[2] >= 0 ? c.mx.z : c.mn.z;
            in = p[0] * x + p[1] * y + p[2] * z + p[3] >= 0;
        }
        if (!in) continue;
        if (!drawFirst_.empty() && drawFirst_.back() + drawCount_.back() == c.first) drawCount_.back() += c.count;
        else { drawFirst_.push_back(c.first); drawCount_.push_back(c.count); }
    }
    if (!drawFirst_.empty()) glMultiDrawArrays(GL_TRIANGLES, drawFirst_.data(), drawCount_.data(), (GLsizei)drawFirst_.size());
}

// (Re)create the multisampled scene target when the setting or the window size changes.
void Renderer::ensureMsaaTarget() {
    int want = msaa_ > 1 ? msaa_ : 0;
    if (msaa_ == 1 && !worldVao_) return; // edges need a VAO to draw the fullscreen pass (any will do)
    static GLint64 maxS = -1;
    if (want && maxS < 0) glGetInteger64v(GL_MAX_SAMPLES, &maxS); // once
    if (want) { want = std::min(want, (int)std::max<GLint64>(0, maxS)); if (want < 2) want = 0; }
    if (msaa_ == 1) want = 1; // edges
    if (want == msaaActive_ && (!want || (msaaW_ == width_ && msaaH_ == height_))) return;
    if (msaaFbo_) { glDeleteFramebuffers(1, &msaaFbo_); glDeleteRenderbuffers(1, &msaaColor_); glDeleteRenderbuffers(1, &msaaDepth_); msaaFbo_ = msaaColor_ = msaaDepth_ = 0; }
    if (edgeTex_) { glDeleteTextures(1, &edgeTex_); edgeTex_ = 0; }
    msaaActive_ = 0;
    if (!want || width_ <= 0 || height_ <= 0) return;
    if (want == 1) { // edges: a plain colour texture + depth
        glGenFramebuffers(1, &msaaFbo_);
        glGenTextures(1, &edgeTex_);
        glGenRenderbuffers(1, &msaaDepth_);
        glBindTexture(GL_TEXTURE_2D, edgeTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH24_STENCIL8, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, edgeTex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok) { glDeleteFramebuffers(1, &msaaFbo_); glDeleteTextures(1, &edgeTex_); glDeleteRenderbuffers(1, &msaaDepth_); msaaFbo_ = edgeTex_ = msaaDepth_ = 0; return; }
        msaaActive_ = 1; msaaW_ = width_; msaaH_ = height_;
        return;
    }
    glGenFramebuffers(1, &msaaFbo_);
    glGenRenderbuffers(1, &msaaColor_);
    glGenRenderbuffers(1, &msaaDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_RGBA8, width_, height_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_DEPTH24_STENCIL8, width_, height_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { glDeleteFramebuffers(1, &msaaFbo_); glDeleteRenderbuffers(1, &msaaColor_); glDeleteRenderbuffers(1, &msaaDepth_); msaaFbo_ = msaaColor_ = msaaDepth_ = 0; return; }
    msaaActive_ = want; msaaW_ = width_; msaaH_ = height_;
}
