#include "renderer.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace {

// 5x7 bitmap font for ASCII 32..126 (lowercase drawn as uppercase). One byte per row, bit 4 = leftmost.
const uint8_t kFont[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x04,0x04,0x04,0x04,0x00,0x00,0x04},{0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00},
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},{0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},{0x0C,0x04,0x08,0x00,0x00,0x00,0x00},{0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},{0x00,0x04,0x15,0x0E,0x15,0x04,0x00},{0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08},{0x00,0x00,0x00,0x1F,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00},{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08},{0x02,0x04,0x08,0x10,0x08,0x04,0x02},{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08},{0x0E,0x11,0x01,0x02,0x04,0x00,0x04},{0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E},
    {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x19,0x15,0x13,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00},{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},{0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},{0x08,0x04,0x02,0x00,0x00,0x00,0x00},
    // a-z placeholder rows (remapped to uppercase at draw time)
    {0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},{0},
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02},{0x04,0x04,0x04,0x04,0x04,0x04,0x04},{0x08,0x04,0x04,0x02,0x04,0x04,0x08},
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00},
};
constexpr int kFontTexW = 96, kFontTexH = 56; // 16x6 cells of 6x8 + a solid block row

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

float outline(vec4 e) {
    vec2 d2 = min(e.xy, e.zw - e.xy);
    float d = min(d2.x, d2.y);
    float px = fwidth(d);
    return smoothstep(1.0, 2.0, d / max(px, 1e-5));
}
uniform sampler2DShadow uShadow;
uniform int uShadowOn;
uniform float uShadowTexel;
float shadowAt(vec4 sc) {
    if (uShadowOn == 0) return 1.0;
    vec3 p = sc.xyz / sc.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
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
uniform vec3 uSunColor, uSkyColor, uBase, uAccent, uFogColor, uCamPos;
uniform float uFogDensity;
out vec4 fragColor;
void main() {
    vec2 lm = texture(uLightmap, vUV).rg;
    vec3 albedo = mix(uBase, uAccent, vMat);
    vec3 light = uSkyColor * lm.g + uSunColor * lm.r * shadowAt(vShadow);
    vec3 c = albedo * light * outline(vEdge);
    float d = length(vPos - uCamPos);
    c = mix(c, uFogColor, 1.0 - exp(-d * uFogDensity));
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
})";

const char* kDynVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=3) in vec4 aEdge;
uniform mat4 uViewProj;
uniform mat4 uShadowMat;
out vec3 vPos; out vec3 vN; out vec4 vColor; out vec4 vShadow; out vec4 vEdge;
void main() {
    vPos = aPos; vN = aNormal; vColor = aColor; vEdge = aEdge;
    vShadow = uShadowMat * vec4(aPos + aNormal * 0.03, 1.0);
    gl_Position = uViewProj * vec4(aPos, 1.0);
})";

const char* kDynFS = R"(
in vec3 vPos; in vec3 vN; in vec4 vColor; in vec4 vShadow; in vec4 vEdge;
uniform vec3 uSunColor, uSkyColor, uSunDir, uFogColor, uCamPos;
uniform float uFogDensity;
uniform int uReceiveShadow;
out vec4 fragColor;
void main() {
    float sh = uReceiveShadow != 0 ? shadowAt(vShadow) : 1.0;
    float ndl = max(dot(normalize(vN), uSunDir), 0.0);
    vec3 light = uSkyColor * (0.6 + 0.25 * vN.y) + uSunColor * ndl * vColor.a * sh;
    vec3 c = vColor.rgb * (light * 0.7 + 0.3) * outline(vEdge); // partly emissive so team colors stay readable
    float d = length(vPos - uCamPos);
    c = mix(c, uFogColor, 1.0 - exp(-d * uFogDensity));
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
})";

const char* kFlatVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPos, 1.0); })";
const char* kFlatFS = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(pow(vColor.rgb, vec3(1.0 / 2.2)), vColor.a); })";

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
uniform vec2 uScreen;
out vec2 vUV; out vec4 vColor;
void main() {
    vUV = aUV; vColor = aColor;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";
const char* kUiFS = R"(#version 330 core
in vec2 vUV; in vec4 vColor;
uniform sampler2D uFont;
out vec4 fragColor;
void main() { fragColor = vec4(vColor.rgb, vColor.a * texture(uFont, vUV).r); })";

void setVec3(unsigned prog, const char* name, const Vec3& v) { glUniform3f(glGetUniformLocation(prog, name), v.x, v.y, v.z); }
void setMat(unsigned prog, const char* name, const Mat4& m) { glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, m.m); }

// Upload a vector into a growable stream buffer.
template <class T>
void upload(unsigned vbo, size_t& cap, const std::vector<T>& v) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    size_t bytes = v.size() * sizeof(T);
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
    std::string worldFS = std::string("#version 330 core\n") + kShadowFn + kWorldFS;
    std::string dynFS = std::string("#version 330 core\n") + kShadowFn + kDynFS;
    worldProg_ = compile(kWorldVS, worldFS.c_str(), err);
    dynProg_ = compile(kDynVS, dynFS.c_str(), err);
    flatProg_ = compile(kFlatVS, kFlatFS, err);
    shadowProg_ = compile(kShadowVS, kShadowFS, err);
    uiProg_ = compile(kUiVS, kUiFS, err);
    if (!worldProg_ || !dynProg_ || !flatProg_ || !shadowProg_ || !uiProg_) return false;

    // VAOs
    glGenVertexArrays(1, &dynVao_);
    glGenBuffers(1, &dynVbo_);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(DynVertex), (void*)40);

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
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)16);
    glBindVertexArray(0);

    // Font texture
    std::vector<uint8_t> font(kFontTexW * kFontTexH, 0);
    for (int g = 0; g < 95; g++) {
        int cx = (g % 16) * 6, cy = (g / 16) * 8;
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (kFont[g][r] & (0x10 >> b)) font[(cy + r) * kFontTexW + cx + b] = 255;
    }
    for (int y = 48; y < 56; y++) for (int x = 0; x < 8; x++) font[y * kFontTexW + x] = 255; // solid block
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kFontTexW, kFontTexH, 0, GL_RED, GL_UNSIGNED_BYTE, font.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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
    return true;
}

void Renderer::loadWorld(const Map& map, const std::function<void(float)>& progress) {
    BakedWorld bw;
    buildWorld(map, bw, progress);
    sunDir_ = map.sunDir;
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
}

void Renderer::setCamera(const Vec3& eye, float yaw, float pitch, float hfovDeg) {
    camPos_ = eye;
    camYaw_ = yaw;
    camPitch_ = pitch;
    float aspect = width_ / (float)std::max(1, height_);
    float vfov = 2.0f * std::atan(std::tan(deg2rad(hfovDeg) * 0.5f) / aspect);
    proj_ = Mat4::perspective(vfov, aspect, 0.05f, 400.0f);
    view_m_ = Mat4::lookAt(eye, eye + angleForward(yaw, pitch), {0, 1, 0});
    viewProj_ = proj_ * view_m_;
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
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    float w = length(c[1] - c[0]), h = length(c[3] - c[0]);
    const float eu[4] = {0, w, w, 0}, ev[4] = {0, 0, h, h};
    for (int i : idx) {
        DynVertex v;
        memcpy(v.pos, &c[i].x, 12);
        memcpy(v.normal, &n.x, 12);
        v.color[0] = col.x; v.color[1] = col.y; v.color[2] = col.z; v.color[3] = sv;
        v.edge[0] = eu[i]; v.edge[1] = ev[i]; v.edge[2] = w; v.edge[3] = h;
        out.push_back(v);
    }
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
    Vec3 c[4];
    // +X
    c[0] = tp(h.x, -h.y, h.z); c[1] = tp(h.x, -h.y, -h.z); c[2] = tp(h.x, h.y, -h.z); c[3] = tp(h.x, h.y, h.z);
    addQuad(out, c, tn(1, 0, 0), color, sunVis);
    // -X
    c[0] = tp(-h.x, -h.y, -h.z); c[1] = tp(-h.x, -h.y, h.z); c[2] = tp(-h.x, h.y, h.z); c[3] = tp(-h.x, h.y, -h.z);
    addQuad(out, c, tn(-1, 0, 0), color * 0.92f, sunVis);
    // +Y
    c[0] = tp(-h.x, h.y, h.z); c[1] = tp(h.x, h.y, h.z); c[2] = tp(h.x, h.y, -h.z); c[3] = tp(-h.x, h.y, -h.z);
    addQuad(out, c, tn(0, 1, 0), color, sunVis);
    // -Y
    c[0] = tp(-h.x, -h.y, -h.z); c[1] = tp(h.x, -h.y, -h.z); c[2] = tp(h.x, -h.y, h.z); c[3] = tp(-h.x, -h.y, h.z);
    addQuad(out, c, tn(0, -1, 0), color * 0.8f, sunVis);
    // +Z
    c[0] = tp(-h.x, -h.y, h.z); c[1] = tp(h.x, -h.y, h.z); c[2] = tp(h.x, h.y, h.z); c[3] = tp(-h.x, h.y, h.z);
    addQuad(out, c, tn(0, 0, 1), color * 0.96f, sunVis);
    // -Z
    c[0] = tp(h.x, -h.y, -h.z); c[1] = tp(-h.x, -h.y, -h.z); c[2] = tp(-h.x, h.y, -h.z); c[3] = tp(h.x, h.y, -h.z);
    addQuad(out, c, tn(0, 0, -1), color * 0.96f, sunVis);
}

void Renderer::box(const Vec3& center, const Vec3& half, float yaw, const Vec3& color, float sunVis, bool castShadow, bool visible) {
    Mat4 xf = Mat4::translate(center) * Mat4::rotY(yaw);
    if (visible) emitBox(dyn_, xf, half, color, sunVis);
    else if (castShadow) emitBox(dynShadowOnly_, xf, half, color, sunVis);
}

void Renderer::boxPitched(const Vec3& pivot, const Vec3& offset, const Vec3& half, float yaw, float pitch, const Vec3& color, float sunVis) {
    Mat4 xf = Mat4::translate(pivot) * Mat4::rotY(yaw) * Mat4::rotX(pitch) * Mat4::translate(offset);
    emitBox(dyn_, xf, half, color, sunVis);
}

void Renderer::viewBox(const Vec3& offset, const Vec3& half, const Vec3& color) {
    Mat4 xf = Mat4::translate(camPos_) * Mat4::rotY(camYaw_) * Mat4::rotX(camPitch_) * Mat4::translate(offset);
    emitBox(view_, xf, half, color, 1.0f);
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

void Renderer::drawScene(bool shadows) {
    bool useShadows = shadows && shadowsOn_;
    // Shadow pass: only dynamic objects (static shadows are baked).
    Mat4 shadowMat;
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

        std::vector<DynVertex>& casters = dynShadowOnly_;
        casters.insert(casters.end(), dyn_.begin(), dyn_.end());
        upload(dynVbo_, dynCap_, casters);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glViewport(0, 0, shadowRes_, shadowRes_);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);
        glUseProgram(shadowProg_);
        setMat(shadowProg_, "uViewProj", shadowMat);
        glBindVertexArray(dynVao_);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)casters.size());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glViewport(0, 0, width_, height_);
    glClearColor(std::pow(skyColor.x, 1 / 2.2f), std::pow(skyColor.y, 1 / 2.2f), std::pow(skyColor.z, 1 / 2.2f), 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    const Vec3 sunColor = {0.78f, 0.75f, 0.70f};
    const Vec3 skyLight = {0.42f, 0.45f, 0.52f};
    const float fog = 0.006f;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);

    if (worldVerts_) {
        glUseProgram(worldProg_);
        setMat(worldProg_, "uViewProj", viewProj_);
        setMat(worldProg_, "uShadowMat", shadowMat);
        glUniform1i(glGetUniformLocation(worldProg_, "uLightmap"), 0);
        glUniform1i(glGetUniformLocation(worldProg_, "uShadow"), 1);
        glUniform1i(glGetUniformLocation(worldProg_, "uShadowOn"), useShadows ? 1 : 0);
        glUniform1f(glGetUniformLocation(worldProg_, "uShadowTexel"), 1.0f / shadowRes_);
        setVec3(worldProg_, "uSunColor", sunColor);
        setVec3(worldProg_, "uSkyColor", skyLight);
        setVec3(worldProg_, "uBase", baseColor);
        setVec3(worldProg_, "uAccent", accentColor);
        setVec3(worldProg_, "uFogColor", skyColor);
        setVec3(worldProg_, "uCamPos", camPos_);
        glUniform1f(glGetUniformLocation(worldProg_, "uFogDensity"), fog);
        glBindTexture(GL_TEXTURE_2D, lightmapTex_);
        glBindVertexArray(worldVao_);
        glDrawArrays(GL_TRIANGLES, 0, worldVerts_);
    }

    auto drawDyn = [&](const std::vector<DynVertex>& v, bool receive) {
        if (v.empty()) return;
        upload(dynVbo_, dynCap_, v);
        glUseProgram(dynProg_);
        setMat(dynProg_, "uViewProj", viewProj_);
        setMat(dynProg_, "uShadowMat", shadowMat);
        glUniform1i(glGetUniformLocation(dynProg_, "uShadow"), 1);
        glUniform1i(glGetUniformLocation(dynProg_, "uShadowOn"), useShadows ? 1 : 0);
        glUniform1i(glGetUniformLocation(dynProg_, "uReceiveShadow"), receive ? 1 : 0);
        glUniform1f(glGetUniformLocation(dynProg_, "uShadowTexel"), 1.0f / shadowRes_);
        setVec3(dynProg_, "uSunColor", sunColor);
        setVec3(dynProg_, "uSkyColor", skyLight);
        setVec3(dynProg_, "uSunDir", sunDir_);
        setVec3(dynProg_, "uFogColor", skyColor);
        setVec3(dynProg_, "uCamPos", camPos_);
        glUniform1f(glGetUniformLocation(dynProg_, "uFogDensity"), fog);
        glBindVertexArray(dynVao_);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    };
    drawDyn(dyn_, true);

    if (!flat_.empty()) {
        upload(flatVbo_, flatCap_, flat_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glUseProgram(flatProg_);
        setMat(flatProg_, "uViewProj", viewProj_);
        glBindVertexArray(flatVao_);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)flat_.size());
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
    }

    if (!view_.empty()) {
        glClear(GL_DEPTH_BUFFER_BIT);
        drawDyn(view_, false);
    }
    dyn_.clear();
    dynShadowOnly_.clear();
    view_.clear();
    flat_.clear();
}

void Renderer::rect(float x, float y, float w, float h, const Color& c) {
    const float u = 2.0f / kFontTexW, v = 52.0f / kFontTexH; // inside the solid block
    Vertex2D q[4] = {{{x, y}, {u, v}, {c.r, c.g, c.b, c.a}}, {{x + w, y}, {u, v}, {c.r, c.g, c.b, c.a}},
                     {{x + w, y + h}, {u, v}, {c.r, c.g, c.b, c.a}}, {{x, y + h}, {u, v}, {c.r, c.g, c.b, c.a}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i : idx) ui_.push_back(q[i]);
}

void Renderer::text(float x, float y, float s, const std::string& str, const Color& c) {
    float cx = std::round(x), cy = std::round(y);
    for (char ch : str) {
        unsigned char uc = (unsigned char)ch;
        if (uc >= 'a' && uc <= 'z') uc -= 32;
        if (uc < 32 || uc > 126) uc = '?';
        int g = uc - 32;
        if (g != 0) {
            float u0 = (g % 16) * 6.0f / kFontTexW, v0 = (g / 16) * 8.0f / kFontTexH;
            float u1 = u0 + 6.0f / kFontTexW, v1 = v0 + 8.0f / kFontTexH;
            float w = 6 * s, h = 8 * s;
            Vertex2D q[4] = {{{cx, cy}, {u0, v0}, {c.r, c.g, c.b, c.a}}, {{cx + w, cy}, {u1, v0}, {c.r, c.g, c.b, c.a}},
                             {{cx + w, cy + h}, {u1, v1}, {c.r, c.g, c.b, c.a}}, {{cx, cy + h}, {u0, v1}, {c.r, c.g, c.b, c.a}}};
            const int idx[6] = {0, 1, 2, 0, 2, 3};
            for (int i : idx) ui_.push_back(q[i]);
        }
        cx += 6 * s;
    }
}

void Renderer::flush2D() {
    if (ui_.empty()) return;
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    upload(uiVbo_, uiCap_, ui_);
    glUseProgram(uiProg_);
    glUniform2f(glGetUniformLocation(uiProg_, "uScreen"), (float)width_, (float)height_);
    glUniform1i(glGetUniformLocation(uiProg_, "uFont"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glBindVertexArray(uiVao_);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ui_.size());
    glDisable(GL_BLEND);
    ui_.clear();
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
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)out.data(), out.size());
    return (bool)f;
}
