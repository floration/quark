#include "renderer.h"
#include "../common/u8file.h"
#include "../common/profile.h"
#include "gpuprofile.h"
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

// RENDER SCALE: the scene's pixels per window pixel (1 at 100%), so widths given in window pixels stay that wide
uniform float uPxScale;
// 1 at a face's middle, 0 on its edges: the outline. w = its width in window pixels (settings; 1 = the default look,
// smoothstep(1, 2)); 0 = no outline. Below 100% never under a scene pixel, and its 1 pixel ramp stays a scene pixel
// (thinner, the outlines went grey and shimmered).
float outline(vec4 e, float w) {
    if (w <= 0.0) return 1.0;
    w = max(w * uPxScale, min(w, 1.0));
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
uniform float uTime;     // seconds (wrapped every hour): water, lava and acid flow
uniform vec3 uSunDir;    // toward the sun: metals' highlights
out vec4 fragColor;
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
// value noise and three octaves of it (water, lava, acid flow; sand, grass, ice, rubber surfaces): no textures
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}
// Anti-aliased: noise finer than about 2 pixels on screen fades to its average (0.5), so a long strip seen from one end
// (each pixel spanning metres of it) is a calm surface instead of shimmer (owner, 2026-10-28: lava "gets fucky because
// of the angle"). The footprint is the longer of the pixel's two sides in noise cells: angle and distance alike.
float noiseFade(float cells) { return 1.0 - smoothstep(0.25, 0.5, cells); }
float footprint(vec2 p) { vec2 w = fwidth(p); return max(w.x, w.y); }
float vnoiseAA(vec2 p) { return mix(0.5, vnoise(p), noiseFade(footprint(p))); }
float fbm(vec2 p) {
    float f = footprint(p);
    return 0.5 * mix(0.5, vnoise(p), noiseFade(f)) + 0.3 * mix(0.5, vnoise(p * 2.03 + 17.1), noiseFade(f * 2.03))
         + 0.2 * mix(0.5, vnoise(p * 4.01 + 41.7), noiseFade(f * 4.01));
}
vec3 lin(vec3 c) { return pow(c, vec3(2.2)) * 1.5; }   // a display colour as the palette is given to the shader
vec2 hash22(vec2 p) { return vec2(hash2(p), hash2(p + 19.19)); }
vec2 worley(vec2 p) {   // cells: the nearest and second-nearest feature points' distances (cobblestone, crystal)
    vec2 i = floor(p), f = fract(p);
    float d1 = 9.0, d2 = 9.0;
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
        vec2 o = vec2(x, y), r = o + hash22(i + o) - f;
        float d = dot(r, r);
        if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
    }
    return sqrt(vec2(d1, d2));
}
// Holes blown in thin brick / cobblestone (Map::holes, two texels each: u v r axis, cw ch lo hi): a fragment of a cell
// one breaks out isn't drawn. The same test as Map's holeTakes.
uniform sampler2D uHoles;
uniform int uHoleCount;
bool inHole() {
    for (int i = 0; i < uHoleCount; i++) {
        vec4 a = texelFetch(uHoles, ivec2(2 * i, 0), 0), b = texelFetch(uHoles, ivec2(2 * i + 1, 0), 0);
        int ax = int(a.w + 0.5);
        float pa = ax == 0 ? vPos.x : ax == 1 ? vPos.y : vPos.z;
        if (pa < b.z - 0.01 || pa > b.w + 0.01) continue;
        vec2 p = ax == 0 ? vPos.zy : ax == 1 ? vPos.xz : vPos.xy;
        if (a.z < 0.0) { if (abs(p.x - a.x) <= b.x && abs(p.y - a.y) <= b.y) return true; continue; }   // (a whole wall that fell)
        if (abs(p.x - a.x) > a.z * 1.2 || abs(p.y - a.y) > a.z * 1.2) continue;
        int row = int(floor(p.y / b.y));
        float off = 0.5 * float(row & 1);
        int col = int(floor(p.x / b.x + off));
        float x = ((float(col) + 0.5 - off) * b.x - a.x) / a.z, y = ((float(row) + 0.5) * b.y - a.y) / a.z;
        float s = sqrt(sqrt(x * x * x * x + y * y * y * y));
        uint k = uint(row) * 73856093u ^ uint(col) * 19349663u;
        k ^= k >> 13; k *= 0x5bd1e995u; k ^= k >> 15;
        if (s + (float(k & 255u) / 255.0 - 0.5) * 0.3 < 1.0) return true;
    }
    return false;
}
vec3 finish(vec3 c) {
    float d = length(vPos - uCamPos);
    c = mix(c, uFogColor, 1.0 - exp(-d * uFogDensity)) * uBright;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0) * 1.08; // ACES-style filmic curve
    return pow(c, vec3(1.0 / 2.2));
}
void main() {
    int mat = int(vMat + 0.5);
    if (uHoleCount > 0 && (mat == 20 || mat == 24) && vMat - float(mat) > 0.1 && inHole()) discard;   // (0.25 over: thin, breakable)
    if (uFull > 0.5) {
        float g = mat == 2 ? 0.35 : mat == 5 ? 0.6 : mat == 6 ? 0.5 : mat == 7 || mat == 11 ? 0.9 : abs(vN.y) > 0.5 ? 0.8 : 0.95;
        fragColor = vec4(uFullColor * g * ((mat >= 4 && mat < 8) || mat == 11 ? 1.0 : outline(vEdge, uOutlineW)), 1.0);
        return;
    }
    if ((mat >= 4 && mat < 8) || mat == 11) { // glow trim / far scenery / water / lava / acid: unlit, skip everything below
        vec3 c;
        if (mat == 6 || mat == 7 || mat == 11) {
            // liquids: noise flowing across the surface (owner, 2026-10-28: "so you can tell its actually lava"), in the
            // face's plane (tops xz, sides along their face, sliding down them)
            vec3 an = abs(vN);
            vec2 q = an.y > 0.5 ? vPos.xz : an.x > 0.5 ? vPos.zy : vPos.xy;
            vec2 flow = an.y > 0.5 ? vec2(0.31, 0.19) : vec2(0.0, 0.6);
            if (mat == 6) {   // water: deep blue, lighter at grazing faces; two layers of ripples drifting apart, bright crests
                float r = fbm(q * 0.7 + flow * uTime) * 0.6 + fbm(q * 1.6 - flow.yx * uTime * 1.3) * 0.4;
                c = mix(vec3(0.02, 0.10, 0.20), vec3(0.10, 0.32, 0.50), 1.0 - an.y * 0.6) * (0.75 + 0.5 * r) + vec3(0.10, 0.18, 0.22) * smoothstep(0.62, 0.72, r);
            } else if (mat == 7) {   // lava: a dark crust broken by glowing veins that slowly flow and churn
                float n = fbm(q * 0.35 + flow * uTime * 0.25 + vec2(fbm(q * 0.3 - uTime * 0.05)) * 1.5);
                float r = 1.0 - abs(2.0 * n - 1.0), warm = smoothstep(0.4, 0.8, fbm(q * 0.8 + uTime * 0.03));
                // (the veins are thin: once the pattern is too fine to draw they blend to how much of it they cover)
                float vein = mix(0.12, smoothstep(0.86, 0.98, r), noiseFade(footprint(q * 0.35) * 4.0));
                c = mix(vec3(0.16, 0.025, 0.01) + vec3(0.35, 0.06, 0.0) * warm, vec3(1.9, 0.55, 0.08), vein) + vec3(0.8, 0.5, 0.1) * pow(vein, 4.0);
            } else {   // acid: glowing toxic green, darker sides, with drifting foam and a slow shimmer
                float n = fbm(q * 0.9 + flow * uTime * 0.6);
                c = mix(vec3(0.02, 0.22, 0.01), vec3(0.12, 0.85, 0.04), max(vN.y, 0.0)) * (0.7 + 0.45 * n)
                  * (0.9 + 0.1 * sin(vPos.x * 1.9 + uTime * 1.7) * sin(vPos.z * 1.6 - uTime * 1.3)) + vec3(0.25, 0.5, 0.1) * smoothstep(0.66, 0.76, n);
            }
        } else c = mat == 4 ? uGlow * 1.6   // (owner, 2026-10-29: glowing things brighter; was 0.9)
                 : mix(uMetal * (0.55 + 0.25 * vN.y + 0.15 * abs(vN.x)), uFogColor, 0.45); // far: hazy silhouette
        fragColor = vec4(finish(c), 1.0);
        return;
    }
    vec2 lm = texture(uLightmap, vUV).rg;
    vec3 albedo = mat == 0 ? uBaseL : (mat == 1 || mat == 8) ? uAccentL : mat == 2 ? uWallbangL : uMetalL; // 8: trim (decor, not solid)
    if (mat == 10) albedo = vec3(0.30, 0.33, 0.42) * (0.8 + 0.2 * step(0.5, fract((vPos.x + vPos.y + vPos.z) * 1.5))); // nograpple: slate blue with diagonal stripes
    // surface detail: 2 m panels (4 m on floors) with a faint per-panel tint and thin seams, world-space so it
    // tiles across brushes; floor contact darkening on walls
    vec3 an = abs(vN);
    vec2 q = an.y > 0.5 ? vPos.xz : an.x > 0.5 ? vPos.zy : vPos.xy;   // the face's plane, metres
    float spec = 0.0, gloss = 32.0;   // a highlight from the sun (metals, ice, plastic)
    vec3 emit = vec3(0.0);            // glowing parts (crystal edges, circuit traces), added after the light
    if (mat < 12) {
        vec2 pp = q * (an.y > 0.5 ? 0.25 : 0.5);
        if (mat == 3) pp = pp * vec2(2.0, 0.5);
        vec2 cell = floor(pp), f = fract(pp), w = fwidth(pp) * max(1.5 * uPxScale, 1.0);   // (1.5 window pixels, at least 1 scene pixel)
        float seam = min(smoothstep(0.0, w.x + 0.012, min(f.x, 1.0 - f.x)), smoothstep(0.0, w.y + 0.012, min(f.y, 1.0 - f.y)));
        albedo *= (0.93 + 0.1 * hash2(cell + floor(vPos.y * 0.01))) * mix(0.82, 1.0, seam);
        if (mat == 3) {   // metal: brushed streaks along each panel and a soft sheen (owner, 2026-10-28: "not just black box")
            albedo *= 1.15 + 0.12 * (vnoiseAA(vec2(q.x * 0.6, q.y * 40.0)) - 0.5);
            spec = 0.35; gloss = 24.0;
        }
    } else if (mat == 12) {   // ice: pale blue, cloudy depths and fine white cracks, glossy
        float n = fbm(q * 0.8), cr = abs(vnoiseAA(q * 3.0) - 0.5);
        albedo = lin(mix(vec3(0.55, 0.72, 0.86), vec3(0.80, 0.90, 0.97), n)) * (1.0 + 0.6 * (1.0 - smoothstep(0.0, 0.03, cr)));
        spec = 0.6; gloss = 64.0;
    } else if (mat == 13) {   // sand: warm tan, fine grain, wind ripples on tops
        float g = hash2(floor(q * 40.0)), rip = an.y > 0.5 ? sin(q.x * 3.0 + fbm(q * 0.5) * 6.0) : 0.0;
        albedo = lin(vec3(0.78, 0.67, 0.46)) * (0.88 + 0.12 * g + 0.05 * rip) * (0.9 + 0.2 * fbm(q * 0.3));
    } else if (mat == 14) {   // wood: planks 0.25 m wide along the face's long way, each its own shade, grain and rings
        vec2 r = an.y > 0.5 ? q : vec2(q.y, q.x);
        float plank = floor(r.y * 4.0), along = r.x + hash2(vec2(plank, 3.0)) * 7.0, across = fract(r.y * 4.0);
        float grain = sin((along * 0.6 + vnoiseAA(vec2(along * 0.3, plank)) * 4.0 + across * 2.0) * 9.0) * 0.5 + 0.5;
        float joint = min(smoothstep(0.0, 0.04, across), smoothstep(0.0, 0.04, 1.0 - across)) * smoothstep(0.0, 0.02, abs(fract(along / 2.4) - 0.5) * 2.0);
        albedo = lin(mix(vec3(0.42, 0.26, 0.14), vec3(0.62, 0.42, 0.24), 0.5 * hash2(vec2(plank, 9.0)) + 0.3 * grain)) * mix(0.55, 1.0, joint);
    } else if (mat == 15) {   // plastic: the map's accent colour, smooth and even, a tight shine
        albedo = uAccentL * 1.1;
        spec = 0.45; gloss = 80.0;
    } else if (mat == 16) {   // grass: green blotches and blades on tops; sides earth with a grass fringe
        float n = fbm(q * 0.6), blade = hash2(floor(q * 30.0));
        vec3 green = lin(mix(vec3(0.20, 0.42, 0.12), vec3(0.36, 0.60, 0.20), n)) * (0.85 + 0.25 * blade);
        if (an.y > 0.5) albedo = green;
        else {   // sides: earth, green down the top 1/4 m (ragged) where the top is open (the bake says: no 0.25 in vMat)
            float top = an.x > 0.5 ? vEdge.z - vEdge.x : vEdge.w - vEdge.y;   // (to the piece's top edge: the side's top)
            float fringe = abs(vMat - float(mat)) < 0.1 ? 1.0 - smoothstep(0.2, 0.26, top + 0.05 * (vnoiseAA(q * vec2(9.0, 3.0)) - 0.5)) : 0.0;
            albedo = mix(lin(vec3(0.36, 0.26, 0.17)) * (0.85 + 0.2 * fbm(q * 2.0)), green, fringe);
        }
    } else if (mat == 17) {   // rubber: near-black, matte, fine pebbled dots
        vec2 d = fract(q * 12.0) - 0.5;
        albedo = lin(vec3(0.13, 0.13, 0.14)) * (0.85 + 0.3 * smoothstep(0.32, 0.2, length(d)));
        spec = 0.05; gloss = 8.0;
    } else if (mat == 18) {   // steel: light grey, long brushed streaks, a bright sharp highlight
        albedo = lin(vec3(0.60, 0.62, 0.66)) * (0.92 + 0.16 * vnoiseAA(vec2(q.x * 0.5, q.y * 60.0)));
        spec = 0.7; gloss = 48.0;
    } else if (mat == 20) {   // brick: classic red, running bond (0.2 x 0.065 m bricks, 1 cm mortar), each brick its own shade
        vec2 r = an.y > 0.5 ? q : (an.x > 0.5 ? vec2(vPos.z, vPos.y) : vec2(vPos.x, vPos.y));
        float row = floor(r.y / 0.075);
        vec2 b = vec2(r.x / 0.21 + 0.5 * mod(row, 2.0), r.y / 0.075);
        vec2 cell = floor(b), f = fract(b) * vec2(0.21, 0.075);
        float mortar = min(min(f.x, 0.21 - f.x), min(f.y, 0.075 - f.y));
        // (mortar lines thinner than a pixel fade to the share they cover: no moire rings across a wall far off / edge on)
        float m = mix(0.85, smoothstep(0.004, 0.007, mortar), 1.0 - smoothstep(0.12, 0.3, footprint(r / 0.075)));
        vec3 brick = mix(vec3(0.55, 0.20, 0.13), vec3(0.70, 0.30, 0.20), hash2(cell)) * (0.9 + 0.15 * vnoiseAA(r * 40.0));
        albedo = lin(mix(vec3(0.62, 0.60, 0.55), brick, m));
    } else if (mat == 22) {   // asphalt: near-black with light and dark aggregate
        albedo = lin(vec3(0.20, 0.20, 0.21)) * (0.8 + 0.4 * hash2(floor(q * 60.0))) * (0.92 + 0.12 * fbm(q * 0.7));
    } else if (mat == 23) {   // mud: wet brown, darker glossy puddles
        float pud = smoothstep(0.55, 0.62, fbm(q * 0.8));
        albedo = lin(mix(vec3(0.32, 0.22, 0.13), vec3(0.18, 0.12, 0.07), pud)) * (0.85 + 0.25 * fbm(q * 4.0));
        spec = 0.7 * pud; gloss = 80.0;
    } else if (mat == 24) {   // cobblestone: rounded grey stones in dark joints
        vec2 w = worley(q * 3.0);
        float edge = w.y - w.x;
        albedo = lin(mix(vec3(0.25, 0.24, 0.22), vec3(0.55, 0.53, 0.5) * (0.8 + 0.3 * hash2(floor(q * 3.0))), smoothstep(0.03, 0.12, edge))) * (0.9 + 0.15 * fbm(q * 8.0));
    } else if (mat == 25) {   // concrete: cast slabs 1.2 x 0.6 m with form-tie holes
        vec2 c = fract(q / vec2(1.2, 0.6));
        float seam = min(min(c.x, 1.0 - c.x) * 1.2, min(c.y, 1.0 - c.y) * 0.6), tie = length((fract(q / vec2(0.6, 0.3)) - 0.5) * vec2(0.6, 0.3));
        albedo = lin(vec3(0.58, 0.58, 0.56)) * (0.85 + 0.2 * fbm(q * 3.0) + 0.06 * hash2(floor(q * 80.0))) * mix(0.7, 1.0, smoothstep(0.0, 0.01, seam)) * mix(0.5, 1.0, smoothstep(0.015, 0.025, tie));
    } else if (mat == 26) {   // tile: glazed cream 0.25 m squares in grey grout, each its own shade, a soft bevel and shine
        vec2 c = q / 0.25, f = fract(c);
        float g = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)) * 0.25;
        float bevel = mix(0.9, smoothstep(0.004, 0.012, g), 1.0 - smoothstep(0.05, 0.15, footprint(c)));
        vec3 glaze = mix(vec3(0.86, 0.84, 0.78), vec3(0.93, 0.92, 0.88), hash2(floor(c))) * (0.97 + 0.03 * vnoiseAA(q * 12.0));
        albedo = lin(mix(vec3(0.42, 0.42, 0.40), glaze * mix(0.85, 1.0, smoothstep(0.012, 0.03, g)), bevel));
        spec = 0.5 * bevel; gloss = 70.0;
    } else if (mat == 27) {   // marble: white with grey veins, polished
        float v = abs(sin(q.x * 1.5 + q.y * 0.7 + fbm(q * 1.2) * 7.0));
        albedo = lin(mix(vec3(0.35, 0.36, 0.40), vec3(0.93, 0.92, 0.90), smoothstep(0.0, 0.25, v))) * (0.95 + 0.05 * fbm(q * 6.0));
        spec = 0.6; gloss = 90.0;
    } else if (mat == 28) {   // corrugated metal: ribs, rust streaking down
        float w = sin(q.x * 40.0) * 0.5 + 0.5, rust = smoothstep(0.55, 0.8, fbm(vec2(q.x * 4.0, q.y * 0.4)));
        albedo = lin(mix(vec3(0.55, 0.57, 0.6), vec3(0.45, 0.22, 0.10), rust)) * (0.75 + 0.35 * w);
        spec = 0.3 * (1.0 - rust); gloss = 30.0;
    } else if (mat == 29) {   // hazard stripes: yellow and black diagonals
        albedo = lin(mod(floor((q.x + q.y) / 0.3), 2.0) < 0.5 ? vec3(0.95, 0.75, 0.05) : vec3(0.08)) * (0.92 + 0.08 * fbm(q * 5.0));
    } else if (mat == 30) {   // snow: soft white drifts, a few glints
        albedo = lin(vec3(0.93, 0.95, 1.0)) * (0.9 + 0.1 * fbm(q * 1.5)) + vec3(1.5) * step(0.997, hash2(floor(q * 120.0)));
    } else if (mat == 31) {   // stone blocks (ashlar): 0.8 x 0.4 m, rows offset, deep joints
        float row = floor(q.y / 0.4);
        vec2 b = vec2(q.x / 0.8 + 0.5 * mod(row, 2.0), q.y / 0.4), f = fract(b) * vec2(0.8, 0.4);
        float j = min(min(f.x, 0.8 - f.x), min(f.y, 0.4 - f.y));
        albedo = lin(vec3(0.6, 0.58, 0.54) * (0.8 + 0.25 * hash2(floor(b)))) * (0.85 + 0.2 * fbm(q * 5.0))
               * mix(0.35, 1.0, mix(0.92, smoothstep(0.01, 0.03, j), 1.0 - smoothstep(0.05, 0.15, footprint(q / 0.4))));
    } else if (mat == 32) {   // crystal: violet-blue facets with glowing edges
        vec2 w = worley(q * 2.0);
        albedo = lin(mix(vec3(0.35, 0.2, 0.6), vec3(0.2, 0.6, 0.8), hash2(floor(q * 2.0 + w.x)))) * (0.7 + 0.5 * w.x);
        emit = vec3(0.9, 1.35, 2.1) * (1.0 - smoothstep(0.0, 0.06, w.y - w.x));
        spec = 0.8; gloss = 120.0;
    } else if (mat == 33) {   // circuit board: green, traces that pulse
        vec2 c = q / 0.25, i = floor(c), f = fract(c);
        float h = hash2(i), tr = h < 0.5 ? abs(f.y - 0.5) : abs(f.x - 0.5);
        float pulse = 0.5 + 0.5 * sin((h < 0.5 ? c.x : c.y) * 3.0 - uTime * 4.0 + h * 20.0);
        albedo = lin(vec3(0.05, 0.22, 0.12)) * (0.9 + 0.1 * vnoiseAA(q * 20.0));
        emit = vec3(0.3, 2.1, 1.2) * (1.0 - smoothstep(0.03, 0.06, tr)) * (0.3 + 0.7 * pulse * pulse);
    } else if (mat == 34) {   // rusted steel: bare steel eaten by rust patches
        float rust = clamp(smoothstep(0.4, 0.75, fbm(q * 1.3)) + 0.3 * smoothstep(0.6, 0.9, fbm(q * 6.0)), 0.0, 1.0);
        albedo = lin(mix(vec3(0.55, 0.57, 0.6), vec3(0.5, 0.24, 0.1) * (0.8 + 0.4 * vnoiseAA(q * 20.0)), rust));
        spec = 0.5 * (1.0 - rust); gloss = 40.0;
    } else if (mat == 35) {   // terracotta roof tiles: curved, overlapping rows
        float row = floor(q.y / 0.25), x = q.x / 0.3 + 0.5 * mod(row, 2.0);
        float curve = sin(fract(x) * 3.1416), f = fract(q.y / 0.25);
        albedo = lin(vec3(0.75, 0.38, 0.2) * (0.85 + 0.2 * hash2(vec2(floor(x), row)))) * (0.55 + 0.5 * curve) * mix(0.5, 1.0, smoothstep(0.0, 0.15, f));
    } else {   // diamond plate: grey metal with raised diamonds (lit on one side, shadowed on the other), in rows offset
        vec2 r = vec2(q.x + q.y, q.x - q.y) * 2.5;
        vec2 cell = floor(r), f = fract(r) - 0.5;
        float lug = (mod(cell.x + cell.y, 2.0) < 0.5 ? f.x : f.y);
        float bump = smoothstep(0.09, 0.05, abs(lug) + 0.35 * abs((mod(cell.x + cell.y, 2.0) < 0.5 ? f.y : f.x)));
        albedo = lin(vec3(0.50, 0.52, 0.55)) * (0.9 + 0.1 * vnoiseAA(q * 9.0)) * (1.0 + 0.6 * bump * sign(lug + 0.0001));
        spec = 0.55; gloss = 40.0;
    }
    if (mat == 0 && vN.y > 0.5) albedo = mix(albedo, uFloorL, 0.55); // floors / tops: darker, cooler than walls
    if (an.y < 0.5) albedo *= mix(0.72, 1.0, smoothstep(0.0, 0.6, fract(vPos.y + 1000.0) + step(1.0, vPos.y)));
    float sun = lm.r * shadowAt(vShadow);
    vec3 light = uSkyColor * lm.g + uSunColor * sun;
    vec3 c = albedo * light * 0.62;
    if (spec > 0.0) {
        vec3 v = normalize(uCamPos - vPos), h = normalize(v + uSunDir);
        c += uSunColor * sun * spec * pow(max(dot(normalize(vN), h), 0.0), gloss);
    }
    c += emit;
    c *= outline(vEdge, uOutlineW);
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

// RENDER SCALE below 100%: the scene stretched to the window with a light contrast-adaptive sharpen (CAS-style, the
// 5-tap cross): each pixel pushed away from its 4 neighbours by an amount that shrinks where the cross already has
// strong contrast (no halos on the outlines) or is near black / white (no clipping). The taps are a scene pixel apart,
// read with linear filtering at the window pixel's own spot, so the stretch is the same pass. uSharp: -1/8 (light) to
// -1/5 (strong).
const char* kSharpFS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform vec2 uRcp;
uniform float uSharp;
out vec4 fragColor;
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    vec3 n = texture(uScene, vUV + vec2(0.0, -uRcp.y)).rgb, s = texture(uScene, vUV + vec2(0.0, uRcp.y)).rgb;
    vec3 w = texture(uScene, vUV + vec2(-uRcp.x, 0.0)).rgb, e = texture(uScene, vUV + vec2(uRcp.x, 0.0)).rgb;
    vec3 mn = min(c, min(min(n, s), min(w, e))), mx = max(c, max(max(n, s), max(w, e)));
    vec3 k = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0)) * uSharp;
    fragColor = vec4(clamp((c + (n + s + w + e) * k) / (1.0 + 4.0 * k), 0.0, 1.0), 1.0);
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
// a rounded rect (aText.w 1, Renderer::roundRect): its pixel position in it, its size (the last corner's: both
// triangles end on it), corner radius and ring width
out vec2 vLocal; flat out vec2 vSize; flat out vec3 vShape;
void main() {
    vScale = aText.x + aText.y / 256.0;   // (255 + 255/256: a logo texel, Renderer::logo)
    vSplit = aText.z;
    vShape = vec3(aText.w, aText.x, aText.y * 0.25);
    vLocal = aUV * 0.25; vSize = aUV * 0.25;
    vUV = (aUV / 32.0 - 1.0) / (vScale > 255.5 ? uLogoSize : uFontSize); vColor = aColor;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";
// Text: one distance-field sample per pixel. The glyph edge (0.5) is smoothed over one screen pixel; a band
// outside it (1 px, wider on big text) is the black outline, coverage-blended under the fill.
const char* kUiFS = R"(#version 330 core
in vec2 vUV; in vec4 vColor; flat in float vScale; flat in float vSplit;
in vec2 vLocal; flat in vec2 vSize; flat in vec3 vShape;
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
    if (vShape.x > 0.5) {   // a rounded rect: coverage from its signed distance (< 0 inside), a ring minus what's further in
        vec2 b = vSize * 0.5, p = vLocal - b;
        float r = min(vShape.y, min(b.x, b.y));
        vec2 q = abs(p) - b + r;
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        float a = clamp(0.5 - d, 0.0, 1.0);
        if (vShape.z > 0.0) a -= clamp(0.5 - d - vShape.z, 0.0, 1.0);
        if (a <= 0.004) discard;
        fragColor = vec4(vColor.rgb, vColor.a * a);
        return;
    }
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
template <class T, class A>
void upload(unsigned vbo, size_t& cap, const std::vector<T, A>& v) {
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
    QGPUCONTEXT();
    {   // the lightmap atlas is packed to fit the GPU's largest texture (PERF-13); the shadow map too (a size past it
        // left the framebuffer incomplete, so shadows turned off instead of using the largest the GPU has)
        GLint64 maxTex = 0;
        glGetInteger64v(GL_MAX_TEXTURE_SIZE, &maxTex);
        if (maxTex > 0) setLightmapMaxTextureSize((int)std::min<GLint64>(maxTex, 1 << 15));
        if (maxTex > 0 && shadowRes > maxTex) shadowRes = (int)maxTex;
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
    sharpProg_ = compile(kEdgeVS, kSharpFS, err);
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

    glGenVertexArrays(1, &fsVao_);   // (no attributes: the fullscreen passes make their triangle from gl_VertexID)

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
    glGenBuffers(1, &uiEbo_);
    uiIndices((1 << 15) / 4);   // (bound to the VAO: the index pattern for the reserved batch)
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
               U(worldProg_, "uMetal"), U(worldProg_, "uGlow"), U(worldProg_, "uFull"), U(worldProg_, "uFullColor"), U(worldProg_, "uOutlineW"),
               U(worldProg_, "uTime"), U(worldProg_, "uSunDir")};
    uHoleCount_ = U(worldProg_, "uHoleCount");
    glUniform1i(U(worldProg_, "uHoles"), 2);
    glUniform1i(uHoleCount_, 0);
    glUniform1i(U(worldProg_, "uLightmap"), 0);
    glUniform1i(U(worldProg_, "uShadow"), 1);
    glUniform1f(U(worldProg_, "uShadowTexel"), 1.0f / shadowRes_);
    setVec3(U(worldProg_, "uSunColor"), sunColor);
    setVec3(U(worldProg_, "uSkyColor"), skyLight);
    glUniform1f(U(worldProg_, "uFogDensity"), fog);
    glUniform1f(U(worldProg_, "uPxScale"), 1.0f);
    glUseProgram(dynProg_);
    uDyn_ = {U(dynProg_, "uViewProj"), U(dynProg_, "uShadowMat"), U(dynProg_, "uShadowOn"), U(dynProg_, "uCasterRect"),
             U(dynProg_, "uCamPos"), U(dynProg_, "uFogColor"), U(dynProg_, "uBright"), U(dynProg_, "uReceiveShadow"), U(dynProg_, "uSunDir")};
    glUniform1i(U(dynProg_, "uShadow"), 1);
    glUniform1f(U(dynProg_, "uShadowTexel"), 1.0f / shadowRes_);
    setVec3(U(dynProg_, "uSunColor"), sunColor);
    setVec3(U(dynProg_, "uSkyColor"), skyLight);
    glUniform1f(U(dynProg_, "uFogDensity"), fog);
    glUniform1f(U(dynProg_, "uPxScale"), 1.0f);
    uWorldPx_ = U(worldProg_, "uPxScale"); uDynPx_ = U(dynProg_, "uPxScale");
    pxScale_ = 1.0f;
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
    if (sharpProg_) {
        glUseProgram(sharpProg_);
        uSharpRcp_ = U(sharpProg_, "uRcp");
        glUniform1i(U(sharpProg_, "uScene"), 0);
        glUniform1f(U(sharpProg_, "uSharp"), -1.0f / 6.5f);   // (CAS's sharpness 0.5: between its -1/8 and -1/5)
    }
    glUseProgram(0);
    uiScreen_[0] = uiScreen_[1] = edgeRcp_[0] = edgeRcp_[1] = sharpRcp_[0] = sharpRcp_[1] = 0;   // (set on first use)

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

// The holes blown in thin brick / cobblestone (Map::holes): two RGBA32F texels each on texture unit 2, which nothing
// else uses. Sent when they change (Map::destructSerial), not every frame.
void Renderer::setHoles(const std::vector<Map::Hole>& holes) {
    const int n = (int)std::min<size_t>(holes.size(), kMaxHoles);
    glUseProgram(worldProg_);
    glUniform1i(uHoleCount_, n);
    glUseProgram(0);
    if (!n) return;
    if (!holeTex_) {
        glGenTextures(1, &holeTex_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, holeTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2 * kMaxHoles, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    } else {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, holeTex_);
    }
    std::vector<float> t((size_t)n * 8);
    for (int i = 0; i < n; i++) {
        const Map::Hole& h = holes[i];
        const float v[8] = {h.u, h.v, h.r, (float)h.axis, h.cw, h.ch, h.lo, h.hi};
        std::copy(v, v + 8, t.begin() + (size_t)i * 8);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2 * n, 1, GL_RGBA, GL_FLOAT, t.data());
    glActiveTexture(GL_TEXTURE0);
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

void Renderer::slope(const Brush& b, const Vec3& color) {
    std::vector<DynVertex>& out = buildingStatic_ ? staticVerts_ : dyn_;
    const int first = (int)out.size();
    slopePolygons(b, [&](const Vec3* pp, int npAll, const Vec3& n) {
      // (a cut's face can have more corners: drawn as a fan of quads and triangles, the cuts between them unoutlined)
      for (int s0 = 0; s0 + 2 < npAll; s0 += 2) {
        const int ia = s0 + 1, ib = s0 + 2, ic = std::min(s0 + 3, npAll - 1);
        const Vec3 p[4] = {pp[0], pp[ia], pp[ib], pp[ic]};
        const int np = ic > ib ? 4 : 3;
        const bool innerFirst = s0 > 0, innerLast = ic + 1 < npAll;   // (edge 0, and the edge back to corner 0)
        // each edge's distance, a linear function over the face: outline = min(edge.xy, edge.zw - edge.xy)
        const Vec3 cen = np == 3 ? (p[0] + p[1] + p[2]) * (1.0f / 3) : (p[0] + p[1] + p[2] + p[3]) * 0.25f;
        auto dist = [&](int e, const Vec3& q) {
            const Vec3 a = p[e], d = p[(e + 1) % np] - a;
            const Vec3 inward = cross(n, d);   // (points into the face when the corners run counterclockwise seen from n)
            const float l = length(inward);
            if (l < 1e-6f || (e == 0 && innerFirst) || (e == np - 1 && innerLast)) return 1000.0f;
            const float s = dot(cen - a, inward) >= 0 ? 1.0f : -1.0f;
            return s * dot(q - a, inward) / l;
        };
        DynVertex v[4];
        for (int k = 0; k < np; k++) {
            memcpy(v[k].pos, &p[k].x, 12);
            memcpy(v[k].normal, &n.x, 12);
            const Vec3 col = n.y < -0.5f || n.x < -0.5f ? color * 0.92f : color;
            v[k].color[0] = col.x; v[k].color[1] = col.y; v[k].color[2] = col.z; v[k].color[3] = 1.0f;
            const float d0 = dist(0, p[k]), d1 = dist(1, p[k]), d2 = dist(2, p[k]), d3 = np > 3 ? dist(3, p[k]) : 1000.0f;
            v[k].edge[0] = d0; v[k].edge[1] = d1; v[k].edge[2] = d0 + d2; v[k].edge[3] = d1 + d3;
            memcpy(v[k].outline, &outlineColor.x, 12);
            v[k].outline[3] = outlineWidth;
        }
        const bool ccw = dot(cross(p[1] - p[0], p[2] - p[0]), n) > 0;
        const DynVertex &a = v[0], &b1 = ccw ? v[1] : v[2], &c1 = ccw ? v[2] : v[1];
        if (np == 3) out.insert(out.end(), {a, b1, c1, a, c1, c1});
        else if (ccw) out.insert(out.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
        else out.insert(out.end(), {v[0], v[2], v[1], v[0], v[3], v[2]});
      }
    });
    if (buildingStatic_) staticItems_.push_back({b.box.center(), length(b.box.mx - b.box.mn) * 0.5f, first, (int)out.size() - first});
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
        QGPUZONE("GPU shadow pass");
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
    glUniform1f(uWorld_.time, (float)std::fmod(timeNow(), 3600.0));
    setVec3(uWorld_.sunDir, sunDir_);
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
    glViewport(0, 0, sceneW_, sceneH_);
    const float px = (float)sceneW_ / (float)std::max(1, width_);
    if (px != pxScale_) {   // (only when the render scale or the size changes: the outlines' width in window pixels)
        pxScale_ = px;
        glUseProgram(worldProg_); glUniform1f(uWorldPx_, px);
        glUseProgram(dynProg_); glUniform1f(uDynPx_, px);
    }
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
        QGPUZONE("GPU world pass");
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
    { QZONE("dynamic pass"); QGPUZONE("GPU dynamic pass"); drawDyn(nShadow, nDyn + nNoCast, true, viewProj_); }   // (lit and shadowed; the no-cast ones cast none)
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
    QGPUZONE("GPU resolve / scale");
    const bool scaled = sceneW_ != width_ || sceneH_ != height_;
    unsigned srcFbo = msaaFbo_, srcTex = edgeTex_;   // (what the window is drawn from, single-sampled)
    if (msaaActive_ >= 2 && !scaled) { // resolve the multisampled scene into the window; the UI draws on top un-multisampled
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (msaaActive_ >= 2) { // RENDER SCALE: a multisampled blit can't change size, so resolve at the scene's size first
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, sceneW_, sceneH_, 0, 0, sceneW_, sceneH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        srcFbo = resolveFbo_; srcTex = resolveTex_;
    }
    if (msaaActive_ == 1 || (msaaActive_ >= 2 && scaled)) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (scaled) glViewport(0, 0, width_, height_);
        // edges: one fullscreen pass from the scene texture into the window (its taps a scene pixel apart: stretched
        // in the same pass); RENDER SCALE below 100% with SHARPEN: the sharpening pass, which stretches it too
        const bool edges = msaa_ == 1, sharpen = !edges && sharpen_ && sharpProg_ && sceneW_ < width_;
        if (edges || sharpen) {
            glDisable(GL_DEPTH_TEST);
            glUseProgram(edges ? edgeProg_ : sharpProg_);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, srcTex);
            const float rx = 1.0f / sceneW_, ry = 1.0f / sceneH_;
            float* last = edges ? edgeRcp_ : sharpRcp_;
            if (rx != last[0] || ry != last[1]) { glUniform2f(edges ? uEdgeRcp_ : uSharpRcp_, rx, ry); last[0] = rx; last[1] = ry; }   // (only when the size changes)
            glBindVertexArray(fsVao_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glEnable(GL_DEPTH_TEST);
        } else {   // RENDER SCALE: stretched to the window, filtered (above 100%, 2x: each window pixel the mean of 4)
            glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
            glBlitFramebuffer(0, 0, sceneW_, sceneH_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
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
static inline Rgba8 rgba8(const Color& c, float a) { return {{u8c(c.r), u8c(c.g), u8c(c.b), u8c(c.a * a)}}; }   // (once per rect / text; a: setAlpha)

// The 2D batch is quads: 4 vertices each, corners 0 (x0,y0) 1 (x1,y0) 2 (x1,y1) 3 (x0,y1), drawn through a fixed index
// buffer as triangles 0 1 2 and 0 3 2 (both end on corner 2: the provoking vertex for the shader's flat inputs). 4
// vertices written a quad instead of 6: a third less to write and upload for every glyph and rect.
void Renderer::uiIndices(size_t quads) {   // (the VAO bound: the element buffer is part of its state)
    if (quads <= uiIdxQuads_) return;
    std::vector<uint32_t> idx(quads * 6);
    for (size_t q = 0; q < quads; q++) {
        const uint32_t b = (uint32_t)(q * 4);
        uint32_t* i = &idx[q * 6];
        i[0] = b; i[1] = b + 1; i[2] = b + 2; i[3] = b; i[4] = b + 3; i[5] = b + 2;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, uiEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
    uiIdxQuads_ = quads;
}

// one quad into the UI batch. Every glyph of every text is one: each distinct value is converted once (the corners
// share 2 x and 2 y; the colour comes packed, the texel coords as integers), written in place.
// (writes the 4 vertices at d: text() makes room for a whole string at once)
static inline void uiQuadAt(Vertex2D* d, float x0, float y0, float x1, float y1, uint16_t U0, uint16_t V0, uint16_t U1, uint16_t V1,
                            const Rgba8& c, uint8_t scaleInt, uint8_t scaleFrac) {
    const int16_t X0 = px(x0), X1 = px(x1), Y0 = px(y0), Y1 = px(y1);
    auto put = [&](Vertex2D& v, int16_t x, int16_t y, uint16_t u, uint16_t w) {
        v.pos[0] = x; v.pos[1] = y; v.uv[0] = u; v.uv[1] = w;
        memcpy(v.color, c.v, 4);
        v.scaleInt = scaleInt; v.scaleFrac = scaleFrac; v.pad[0] = v.pad[1] = 0;
    };
    put(d[0], X0, Y0, U0, V0); put(d[1], X1, Y0, U1, V0); put(d[2], X1, Y1, U1, V1); put(d[3], X0, Y1, U0, V1);
}

static void uiQuad(Vertex2DList& out, float x0, float y0, float x1, float y1, uint16_t U0, uint16_t V0, uint16_t U1, uint16_t V1,
                   const Rgba8& c, uint8_t scaleInt, uint8_t scaleFrac) {
    const size_t n = out.size();
    out.resize(n + 4);
    uiQuadAt(out.data() + n, x0, y0, x1, y1, U0, V0, U1, V1, c, scaleInt, scaleFrac);
}

void Renderer::rect(float x, float y, float w, float h, const Color& c) {
    // a texel inside the solid block (cell 0.5, 6.5), as stored: (texel + 1) * 32
    constexpr uint16_t u = (uint16_t)((kUiCell * 0.5f + 1) * 32.0f), v = (uint16_t)((kUiCell * 6.5f + 1) * 32.0f);
    uiQuad(ui_, x, y, x + w, y + h, u, v, u, v, rgba8(c, alpha_), 0, 0);
}

void Renderer::rectGradient(float x, float y, float w, float h, const Color& tl, const Color& tr, const Color& br, const Color& bl) {
    constexpr uint16_t u = (uint16_t)((kUiCell * 0.5f + 1) * 32.0f), v = (uint16_t)((kUiCell * 6.5f + 1) * 32.0f);
    const size_t n = ui_.size();
    uiQuad(ui_, x, y, x + w, y + h, u, v, u, v, rgba8(tl, alpha_), 0, 0);
    // corners 0 tl, 1 tr, 2 br, 3 bl: their own colours
    auto col = [&](Vertex2D& d, const Color& c) { d.color[0] = u8c(c.r); d.color[1] = u8c(c.g); d.color[2] = u8c(c.b); d.color[3] = u8c(c.a * alpha_); };
    Vertex2D* d = ui_.data() + n;
    col(d[1], tr); col(d[2], br); col(d[3], bl);
}

void Renderer::roundRect(float x, float y, float w, float h, float radius, const Color& c, float ring) {
    if (w <= 0 || h <= 0) return;
    if (radius < 0.5f && ring <= 0) { rect(x, y, w, h, c); return; }
    const int16_t X0 = px(x), X1 = px(x + w), Y0 = px(y), Y1 = px(y + h);
    if (X1 <= X0 || Y1 <= Y0) return;
    // the quad's own pixel coords in its uv (* 4: a quarter pixel, rects up to 16383 px); both triangles end on corner
    // 2 (the index pattern), whose uv is the size (the shader's flat vSize: the provoking vertex is the last)
    const uint16_t U1 = (uint16_t)std::min(65535, (X1 - X0) * 4), V1 = (uint16_t)std::min(65535, (Y1 - Y0) * 4);
    const Rgba8 col = rgba8(c, alpha_);
    const uint8_t rad = (uint8_t)clampf(std::round(radius), 0, 255), rg = (uint8_t)clampf(std::round(ring * 4), 0, 255);
    const size_t n = ui_.size();
    ui_.resize(n + 4);
    Vertex2D* d = ui_.data() + n;
    auto put = [&](Vertex2D& v, int16_t X, int16_t Y, uint16_t u, uint16_t w2) {
        v.pos[0] = X; v.pos[1] = Y; v.uv[0] = u; v.uv[1] = w2;
        memcpy(v.color, col.v, 4);
        v.scaleInt = rad; v.scaleFrac = rg; v.pad[0] = 0; v.pad[1] = 1;
    };
    put(d[0], X0, Y0, 0, 0); put(d[1], X1, Y0, U1, 0); put(d[2], X1, Y1, U1, V1); put(d[3], X0, Y1, 0, V1);
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
        uiQuad(ui_, x0, y0, x0 + kLogoW * k, y0 + kLogoH * k, u16uv(0), u16uv(0), u16uv(kLogoW), u16uv(kLogoH), rgba8({1, 1, 1, friendly.a}, alpha_), 255, 255);
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
    const Rgba8 col = rgba8(c, alpha_);
    // (room for every glyph at once, written in place, then trimmed to the glyphs drawn: one grow per string, not per
    // glyph)
    const size_t n0 = ui_.size();
    ui_.resize(n0 + 4 * str.size());
    Vertex2D* d = ui_.data() + n0;
    int prev = -1;
    for (char ch : str) {
        int g = glyphIndex(ch);
        if (prev >= 0) cx += (glyphAdv(prev, g) - m.closeUp(prev, g, level, o)) * sk;   // (closed up: Ui's fitting)
        prev = g;
        if (g == 0) continue;
        // the glyph box plus its padding (the outline and anti-aliasing reach into it); snapped to whole pixels
        float qx = uiRound(cx - kUiOrigin * sk) - pad, qy = top - pad;
        int tx = (g % 16) * kUiCell, ty = (g / 16) * kUiCell;
        uiQuadAt(d, qx, qy, qx + box + 2 * pad, qy + box + 2 * pad, u16uv(tx), u16uv(ty), u16uv(tx + kUiCell), u16uv(ty + kUiCell), col, si, sf);
        d += 4;
    }
    ui_.resize((size_t)(d - ui_.data()));
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
    const Rgba8 col = rgba8(c, alpha_);
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
    if (ui_.size() / 4 > uiIdxQuads_) uiIndices(ui_.size() / 4 * 2);   // (grown with room to spare: rare)
    glDrawElements(GL_TRIANGLES, (GLsizei)(ui_.size() / 4 * 6), GL_UNSIGNED_INT, nullptr);
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

// (Re)create the scene target when the anti-aliasing, the render scale or the window size changes (never otherwise:
// every other frame this is a few compares). No target at 100% with AA off: the scene draws straight into the window.
void Renderer::ensureMsaaTarget() {
    int want = msaa_ > 1 ? msaa_ : 0;
    static GLint64 maxS = -1, maxDim = -1;
    if (want && maxS < 0) glGetInteger64v(GL_MAX_SAMPLES, &maxS); // once
    if (want) { want = std::min(want, (int)std::max<GLint64>(0, maxS)); if (want < 2) want = 0; }
    if (msaa_ == 1) want = 1; // edges
    // RENDER SCALE: the scene at the window's size times it (a texture target even with AA off), stretched to the window
    int sw = width_, sh = height_;
    if (scalePct_ != 100 && width_ > 0 && height_ > 0) {
        if (maxDim < 0) {   // once: the largest target the driver makes (200% of 4K is 7680 x 4320)
            GLint64 t = 0, r = 0;
            glGetInteger64v(GL_MAX_TEXTURE_SIZE, &t);
            glGetInteger64v(GL_MAX_RENDERBUFFER_SIZE, &r);
            maxDim = std::max<GLint64>(1, std::min(t > 0 ? t : 4096, r > 0 ? r : 4096));
        }
        sw = (int)std::lround(width_ * (scalePct_ / 100.0)); sh = (int)std::lround(height_ * (scalePct_ / 100.0));
        const double fit = std::min(1.0, (double)maxDim / std::max(sw, sh));   // (too big: the largest that fits, same shape)
        sw = std::max(1, (int)(sw * fit)); sh = std::max(1, (int)(sh * fit));
        if (!want) want = 1;
    }
    const bool scaled = sw != width_ || sh != height_;
    // (what was asked for last: a target the driver turned down isn't tried again every frame)
    if (want == triedAa_ && sw == triedW_ && sh == triedH_) return;
    triedAa_ = want; triedW_ = sw; triedH_ = sh;
    if (msaaFbo_) { glDeleteFramebuffers(1, &msaaFbo_); glDeleteRenderbuffers(1, &msaaColor_); glDeleteRenderbuffers(1, &msaaDepth_); msaaFbo_ = msaaColor_ = msaaDepth_ = 0; }
    if (edgeTex_) { glDeleteTextures(1, &edgeTex_); edgeTex_ = 0; }
    if (resolveFbo_) { glDeleteFramebuffers(1, &resolveFbo_); glDeleteTextures(1, &resolveTex_); resolveFbo_ = resolveTex_ = 0; }
    msaaActive_ = 0;
    sceneW_ = width_; sceneH_ = height_;   // (no target: the window's own size)
    if (!want || width_ <= 0 || height_ <= 0) return;
    // a plain colour texture (linear: the stretch and the edge / sharpen passes read it) as fbo's colour
    auto colorTex = [&](unsigned fbo, unsigned& tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    };
    if (want == 1) { // edges, or AA off at another render scale: a plain colour texture + depth
        glGenFramebuffers(1, &msaaFbo_);
        glGenRenderbuffers(1, &msaaDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH24_STENCIL8, sw, sh);
        colorTex(msaaFbo_, edgeTex_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok) { glDeleteFramebuffers(1, &msaaFbo_); glDeleteTextures(1, &edgeTex_); glDeleteRenderbuffers(1, &msaaDepth_); msaaFbo_ = edgeTex_ = msaaDepth_ = 0; return; }
        msaaActive_ = 1; sceneW_ = sw; sceneH_ = sh;
        return;
    }
    glGenFramebuffers(1, &msaaFbo_);
    glGenRenderbuffers(1, &msaaColor_);
    glGenRenderbuffers(1, &msaaDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_RGBA8, sw, sh);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_DEPTH24_STENCIL8, sw, sh);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok && scaled) {   // RENDER SCALE: the single-sampled texture the samples resolve into, at the scene's size
        glGenFramebuffers(1, &resolveFbo_);
        colorTex(resolveFbo_, resolveTex_);
        ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!ok) { glDeleteFramebuffers(1, &resolveFbo_); glDeleteTextures(1, &resolveTex_); resolveFbo_ = resolveTex_ = 0; }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { glDeleteFramebuffers(1, &msaaFbo_); glDeleteRenderbuffers(1, &msaaColor_); glDeleteRenderbuffers(1, &msaaDepth_); msaaFbo_ = msaaColor_ = msaaDepth_ = 0; return; }
    msaaActive_ = want; sceneW_ = sw; sceneH_ = sh;
}
